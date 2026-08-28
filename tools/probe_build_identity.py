#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""Structural and linked-image checks for the single build-identity authority (§B206/B138 slice S1).

WHY THIS EXISTS
    `src/fw_main.cpp` and `src/firmware_commands.cpp` each used to expand `__DATE__ " " __TIME__` and read
    `GIT_REV`. Two compilations of one clean build can STRADDLE A SECOND, the two literals then differ, the
    linker can no longer merge them, and the image gains a phantom `.text +16` that no source change caused
    ([[B138]], reproduced 2026-08-27). S1 collapses both producers onto ONE authority in `firmware_commands.cpp`.
    These checks pin that collapse so it cannot silently rot back.

    Neither touched file is compiled by the native suite or by the simulator (the simulator's CMakeLists names
    `lib/core` only), so a source-level pin plus the linked-image stamp count is the ONLY automated cover the
    invariant can have.

★★ THE NEGATIVE CONTROLS RUN BY DEFAULT, DELIBERATELY. Each reverts ONE fact in a COPY of the tree and must turn
   the probe RED. A probe that stays green against a broken copy is measuring nothing — this project keeps
   finding exactly that failure, and a control documented as "not optional" while the standard command skips it
   is a control that never ran (`tools/probe_board_ui/run.sh`, QA 2026-08-04). Nothing under the real `src/` or
   `lib/` is ever written: every mutation is applied to a throwaway copy.

USAGE:  tools/probe_build_identity.py                         # source checks + negative controls
        tools/probe_build_identity.py --elf <firmware.elf>    # + exactly-one-linked-stamp, with its own control
        tools/probe_build_identity.py --no-neg                # checks only -- NOT a gate, use only while iterating
"""

from __future__ import annotations

import argparse
import re
import shutil
import tempfile
from pathlib import Path


# ★ DERIVED, NEVER HARDCODED: an absolute path baked into a tool measures somebody else's tree the moment it runs
#   from a worktree or a clone ([[B82]] is the same class).
ROOT = Path(__file__).resolve().parents[1]
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp"}

# The one authority literal and the two call sites, spelled once here so the checks and the controls cannot drift
# apart. `kBuildStamp` is an `extern const char[]` rather than an accessor function: measured on `gateway`, the
# array spelling is the smaller image (see the slice report), and the spec's invariant is one definition, not the
# seam spelling.
BANNER_CALL = "mrfault::format_version_banner(buf, sizeof buf, kBuildStamp, kGitRevision, board_name());"
BLE_CALL = "mrfw::kBuildStamp, mrfw::kGitRevision, board_name(),"


def source_files(root: Path, directory: str) -> list[Path]:
    return sorted(path for path in (root / directory).rglob("*") if path.suffix in SOURCE_SUFFIXES)


class ProbeFailure(Exception):
    """A structural check went RED (raised instead of exiting so a control can catch it)."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ProbeFailure(message)


def macro_sites(paths: list[Path], token: str) -> list[Path]:
    return [path for path in paths if token in path.read_text(encoding="utf-8", errors="replace")]


def check_source(root: Path = ROOT) -> int:
    """The 13 structural checks. `root` is a parameter so a control can run them against a mutated COPY."""
    src_paths = source_files(root, "src")
    lib_paths = source_files(root, "lib")
    commands_cpp = (root / "src/firmware_commands.cpp").read_text(encoding="utf-8")
    commands_h = (root / "src/firmware_commands.h").read_text(encoding="utf-8")
    fw_main = (root / "src/fw_main.cpp").read_text(encoding="utf-8")

    # (1-4) production `src/` expands each compile-time clock macro in exactly ONE file, exactly ONCE.
    for token in ("__DATE__", "__TIME__"):
        sites = macro_sites(src_paths, token)
        require(sites == [root / "src/firmware_commands.cpp"],
                f"{token} must occur only in src/firmware_commands.cpp; got {[p.relative_to(root) for p in sites]}")
        require(commands_cpp.count(token) == 1, f"{token} must occur exactly once in its authority TU")

    # (5-8) exactly one GIT_REV consumer file, one fallback guard, one fallback value, one literal fed by it.
    git_sites = macro_sites(src_paths, "GIT_REV")
    require(git_sites == [root / "src/firmware_commands.cpp"],
            f"GIT_REV must occur only in src/firmware_commands.cpp; got {[p.relative_to(root) for p in git_sites]}")
    require(commands_cpp.count("#ifndef GIT_REV") == 1, "one GIT_REV fallback guard is required")
    require(commands_cpp.count('#define GIT_REV "nogit"') == 1, "one nogit fallback is required")
    require(commands_cpp.count("const char kGitRevision[] = GIT_REV;") == 1,
            "the Git macro must feed exactly one read-only literal authority")

    # (9) the protocol engine never carries build identity — pinned so it stays true.
    for token in ("__DATE__", "__TIME__", "GIT_REV"):
        sites = macro_sites(lib_paths, token)
        require(not sites, f"{token} is forbidden under lib/; got {[p.relative_to(root) for p in sites]}")

    # (10-11) the authority is exported read-only, (12-13) and BOTH output paths bind to it rather than to a
    #         re-expanded macro or a hand-written literal.
    require(commands_h.count("extern const char kBuildStamp[];") == 1,
            "firmware_commands.h must expose one read-only build stamp")
    require(commands_h.count("extern const char kGitRevision[];") == 1,
            "firmware_commands.h must expose one read-only Git revision")
    require(commands_cpp.count('const char kBuildStamp[]  = __DATE__ " " __TIME__;') == 1,
            "firmware_commands.cpp must define the one build-stamp literal")
    require(re.search(r"format_version_banner\([^;]*kBuildStamp,\s*kGitRevision,\s*board_name\(\)\)",
                      commands_cpp, re.DOTALL) is not None,
            "the USB/banner formatter must consume the shared authority")
    require(re.search(r'"built\\\":\\\"%s.*?mrfw::kBuildStamp,\s*mrfw::kGitRevision,\s*board_name\(\)',
                      fw_main, re.DOTALL) is not None,
            "the BLE version formatter must consume the shared authority")
    return 13


# A build stamp as GCC spells it: `MMM D YYYY HH:MM:SS`, with __DATE__'s two spaces before a single-digit day.
STAMP_RE = (rb"(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)"
            rb"  ?[0-9]{1,2} [0-9]{4} [0-9]{2}:[0-9]{2}:[0-9]{2}")


def stamps_in(data: bytes) -> list[bytes]:
    return re.findall(STAMP_RE, data)


def check_elf(path: Path) -> int:
    """Exactly ONE build stamp in the linked image. Scanning the whole file is STRICTER than §1.2's
    loadable-sections method: the authority literal lives in `.rodata` (loadable), so a whole-file count of 1
    implies a loadable count of 1, and it additionally forbids a stray stamp in a non-loadable section."""
    require(path.is_file(), f"ELF does not exist: {path}")
    found = stamps_in(path.read_bytes())
    require(len(found) == 1,
            f"expected exactly one linked build stamp, found {len(found)}: {[s.decode() for s in found]}")
    print(f"  stamp={found[0].decode()}")
    return 1


# ---- negative controls -----------------------------------------------------------------------------------------
# Each entry: (name, relative file, exact old text, replacement). The old text MUST occur EXACTLY ONCE or the
# control is reported UNUSABLE — a mutation that did not apply is not a control, it is a green nothing.
CONTROLS: list[tuple[str, str, str, str]] = [
    ("a second __DATE__/__TIME__ is reintroduced in fw_main.cpp (the [[B138]] shape)",
     "src/fw_main.cpp", BLE_CALL, '__DATE__ " " __TIME__, mrfw::kGitRevision, board_name(),'),
    ("the BLE version arm bypasses the authority with a hand-written literal",
     "src/fw_main.cpp", BLE_CALL, '"Jan  1 2000 00:00:00", "deadbee", board_name(),'),
    ("the banner bypasses the authority by re-expanding the macros",
     "src/firmware_commands.cpp", BANNER_CALL,
     'mrfault::format_version_banner(buf, sizeof buf, __DATE__ " " __TIME__, GIT_REV, board_name());'),
    ("the banner bypasses the authority with a hand-written literal",
     "src/firmware_commands.cpp", BANNER_CALL,
     'mrfault::format_version_banner(buf, sizeof buf, "Jan  1 2000 00:00:00", "deadbee", board_name());'),
    ("a second GIT_REV fallback is reintroduced in fw_main.cpp",
     "src/fw_main.cpp", "#include <stdio.h>\n",
     '#include <stdio.h>\n#ifndef GIT_REV\n#define GIT_REV "nogit"\n#endif\n'),
    ("the read-only build-stamp export is dropped from the header",
     "src/firmware_commands.h", "extern const char kBuildStamp[];", "// stamp export removed"),
    ("the authority literal itself is deleted from firmware_commands.cpp",
     "src/firmware_commands.cpp", 'const char kBuildStamp[]  = __DATE__ " " __TIME__;',
     'const char kBuildStamp[]  = "frozen";'),
    ("the protocol engine acquires a build-identity macro",
     "lib/core/airtime.cpp", "// MeshRoute — airtime.cpp\n",
     '// MeshRoute — airtime.cpp\nstatic const char kLibStamp[] = __DATE__;\n'),
]


def run_controls() -> tuple[int, int]:
    """Returns (red, unusable). Every mutation lands in a throwaway COPY; the real tree is never written."""
    red = unusable = 0
    with tempfile.TemporaryDirectory(prefix="probe_build_identity_") as tmp:
        base = Path(tmp) / "tree"
        base.mkdir()
        for directory in ("src", "lib"):
            shutil.copytree(ROOT / directory, base / directory, symlinks=True)
        for name, rel, old, new in CONTROLS:
            target = base / rel
            pristine = target.read_text(encoding="utf-8")
            occurrences = pristine.count(old)
            if occurrences != 1:
                print(f"  UNUSABLE ({occurrences} matches, need 1): {name}")
                unusable += 1
                continue
            target.write_text(pristine.replace(old, new, 1), encoding="utf-8")
            try:
                check_source(base)
            except ProbeFailure as exc:
                print(f"  RED: {name}\n       -> {exc}")
                red += 1
            else:
                print(f"  ⛔ GREEN (control did not fire): {name}")
            finally:
                target.write_text(pristine, encoding="utf-8")
    return red, unusable


def run_elf_control(path: Path) -> bool:
    """The stamp COUNTER must reject a second stamp. A real two-stamp link is a build-scheduling accident that
    cannot be produced on demand, so the control feeds the counter an image that carries one extra stamp."""
    with tempfile.TemporaryDirectory(prefix="probe_build_identity_elf_") as tmp:
        mutated = Path(tmp) / path.name
        mutated.write_bytes(path.read_bytes() + b"\0Jan  1 2000 00:00:01\0")
        try:
            check_elf(mutated)
        except ProbeFailure as exc:
            print(f"  RED: a second linked stamp is present\n       -> {exc}")
            return True
    print("  ⛔ GREEN (control did not fire): a second linked stamp is present")
    return False


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, help="also require exactly one build timestamp in this ELF")
    parser.add_argument("--no-neg", action="store_true",
                        help="skip the negative controls -- NOT a gate, use only while iterating")
    args = parser.parse_args()

    try:
        checks = check_source()
        if args.elf is not None:
            checks += check_elf(args.elf.resolve())
    except ProbeFailure as exc:
        raise SystemExit(f"FAIL: {exc}")

    if args.no_neg:
        print(f"PASS: build identity ({checks} checks, CONTROLS SKIPPED -- not a gate)")
        return

    print(f"negative controls ({len(CONTROLS)} source" + (" + 1 ELF" if args.elf is not None else "") + "):")
    red, unusable = run_controls()
    expected = len(CONTROLS)
    elf_red = run_elf_control(args.elf.resolve()) if args.elf is not None else None
    if elf_red is not None:
        red += int(elf_red)
        expected += 1
    if unusable or red != expected:
        raise SystemExit(f"FAIL: controls {red}/{expected} RED, {unusable} unusable")
    print(f"PASS: build identity ({checks} checks, {red}/{expected} controls RED, 0 unusable)")


if __name__ == "__main__":
    main()
