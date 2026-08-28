#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""Structural and linked-image checks for the single build-identity authority (§B206/B138 slice S1, §B253).

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

    §B253 ADDS THE HOOK-COVERAGE HALF. `src/firmware_commands.cpp`'s `#ifndef GIT_REV` fallback is what an
    environment reaches when `pre:tools/git_rev.py` is MISSING from it — and that hole has now been found THREE times
    ([[B200]] heltec_v3, [[B213]] xiao_esp32s3, and the original xiao_sx1262-only spelling), each time only after a
    metal banner read `nogit`. Since B253 the hook itself aborts a build whose provenance is unusable, so the one
    remaining way a board image can still be unidentifiable is an environment that never runs the hook at all.
    ⇒ the coverage check below is DERIVED from `pio project config --json-output` (effective config, inheritance
    resolved), never from a hand-written list of today's 13 names: adding an environment must make it EVALUATED.

USAGE:  tools/probe_build_identity.py                         # source + hook-coverage checks + negative controls
        tools/probe_build_identity.py --elf <firmware.elf>    # + exactly-one-linked-stamp, with its own control
        tools/probe_build_identity.py --no-neg                # checks only -- NOT a gate, use only while iterating
"""

from __future__ import annotations

import argparse
import copy
import json
import re
import shutil
import subprocess
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


# ---- §B253: every board environment runs the provenance hook, exactly once -------------------------------------
GIT_REV_HOOK = "pre:tools/git_rev.py"
# `native` is the ONE environment that legitimately carries no provenance: it links no board image, and its
# compilation reaches the C++ `#ifndef GIT_REV` fallback by design. Spelled here as the exception, so a NEW name is
# treated as a board environment and must carry the hook until somebody deliberately widens this set.
NON_BOARD_ENVIRONMENTS = {"native"}


def effective_config(root: Path = ROOT) -> list:
    """PlatformIO's EFFECTIVE configuration (`extends`/inheritance already applied) — the only honest source for
    'which environments exist'. `platformio.ini` text is not: three of the four base envs acquired the hook through
    inheritance, and a hand-parsed ini would have to re-implement that resolution."""
    completed = subprocess.run(["pio", "project", "config", "--json-output"],
                               cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    require(completed.returncode == 0,
            f"`pio project config --json-output` failed with exit {completed.returncode}: "
            f"{completed.stderr.decode('utf-8', 'replace')[:300]}")
    return json.loads(completed.stdout.decode("utf-8"))


def environment_scripts(config: list) -> dict[str, list[str]]:
    return {section[len("env:"):]: list(dict(options).get("extra_scripts", []))
            for section, options in config if section.startswith("env:")}


def check_environment_coverage(config: list) -> int:
    """One check per effective environment. `config` is a PARAMETER so a control can mutate the DERIVED structure —
    mutating a separately hand-written list would prove nothing about the real configuration."""
    scripts = environment_scripts(config)
    require(bool(scripts), "no env:* sections in the effective PlatformIO configuration")
    for name in sorted(NON_BOARD_ENVIRONMENTS):
        require(name in scripts, f"the exempt environment '{name}' is absent — is the exemption still real?")
    for name, entries in sorted(scripts.items()):
        count = entries.count(GIT_REV_HOOK)
        if name in NON_BOARD_ENVIRONMENTS:
            require(count == 0, f"env:{name} is exempt from build provenance but runs {GIT_REV_HOOK} ({count}x)")
        else:
            require(count == 1,
                    f"env:{name} must run {GIT_REV_HOOK} exactly once (found {count}x) — a board image that skips "
                    f"the hook falls back to `nogit` and cannot be identified from its own banner")
    return len(scripts)


def _with_scripts(config: list, environment: str, transform) -> list:
    """Return a deep copy of the derived config with `env:<environment>`'s extra_scripts rewritten."""
    mutated = copy.deepcopy(config)
    for section in mutated:
        if section[0] == f"env:{environment}":
            for option in section[1]:
                if option[0] == "extra_scripts":
                    option[1] = transform(list(option[1]))
                    return mutated
            section[1].append(["extra_scripts", transform([])])
            return mutated
    raise AssertionError(f"env:{environment} absent from the derived configuration")


def coverage_controls(config: list) -> list[tuple[str, list]]:
    """The four §3.4 mutations, each applied to the DERIVED configuration."""
    board = next(name for name in sorted(environment_scripts(config)) if name not in NON_BOARD_ENVIRONMENTS)
    future = copy.deepcopy(config)
    future.append(["env:future_board", [["platform", "espressif32"], ["board", "synthetic"]]])
    return [
        (f"the hook is removed from the parsed env:{board}",
         _with_scripts(config, board, lambda entries: [e for e in entries if e != GIT_REV_HOOK])),
        ("a synthetic future board environment carries no hook", future),
        ("the hook is added to env:native",
         _with_scripts(config, "native", lambda entries: [*entries, GIT_REV_HOOK])),
        (f"the hook is duplicated in env:{board}",
         _with_scripts(config, board, lambda entries: [*entries, GIT_REV_HOOK])),
    ]


def run_coverage_controls(config: list) -> tuple[int, int]:
    red = 0
    controls = coverage_controls(config)
    for name, mutated in controls:
        if mutated == config:
            print(f"  UNUSABLE (mutation did not change the configuration): {name}")
            continue
        try:
            check_environment_coverage(mutated)
        except ProbeFailure as exc:
            print(f"  RED: {name}\n       -> {exc}")
            red += 1
        else:
            print(f"  ⛔ GREEN (control did not fire): {name}")
    return red, len(controls)


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
        config = effective_config()
        environments = check_environment_coverage(config)
        checks += environments
        print(f"  hook coverage: {environments} effective environments "
              f"({environments - len(NON_BOARD_ENVIRONMENTS)} board x1, {len(NON_BOARD_ENVIRONMENTS)} exempt x0)")
        if args.elf is not None:
            checks += check_elf(args.elf.resolve())
    except ProbeFailure as exc:
        raise SystemExit(f"FAIL: {exc}")

    if args.no_neg:
        print(f"PASS: build identity ({checks} checks, CONTROLS SKIPPED -- not a gate)")
        return

    print(f"negative controls ({len(CONTROLS)} source + 4 coverage"
          + (" + 1 ELF" if args.elf is not None else "") + "):")
    red, unusable = run_controls()
    expected = len(CONTROLS)
    coverage_red, coverage_expected = run_coverage_controls(config)
    red += coverage_red
    expected += coverage_expected
    elf_red = run_elf_control(args.elf.resolve()) if args.elf is not None else None
    if elf_red is not None:
        red += int(elf_red)
        expected += 1
    if unusable or red != expected:
        raise SystemExit(f"FAIL: controls {red}/{expected} RED, {unusable} unusable")
    print(f"PASS: build identity ({checks} checks, {red}/{expected} controls RED, 0 unusable)")


if __name__ == "__main__":
    main()
