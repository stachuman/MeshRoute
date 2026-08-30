#!/usr/bin/env python3
"""Deterministic board RAM/flash measurement and repeatability comparison.

The runner serializes only other invocations of itself. Source-mutating mutation
batteries in this checkout do not share its lock and must not run concurrently.
Artifacts produced here carry a fixed build identity and are measurement inputs,
not ordinary provenance-bearing firmware to flash.

This runner is Linux/POSIX-only: it relies on fcntl advisory locking and PTYs.

★★ THE 2026-08-30 OWNER RULING, RECORDED WHERE IT IS ACTED ON. D1 requires the board envs to be built
*sequentially* (the historical `.pio` nRF52 parallel-race false-fail). **That rule is RELAXED for the ruled pair
`gateway` + `heltec_mobile`, conditional on the isolation proven here — and ANY FUTURE ENV ADDITION RE-PROVES IT.**
The relaxation is NOT a licence to run two `measure_board.py` invocations at once: the `pair` subcommand takes the
ONE global lock ONCE and owns both builds itself. Two invocations still exclude each other, exactly as before.

★ WHAT MAKES IT SAFE, and why the per-environment roots below exist. Before this change every environment shared
ONE `.pio-measure/{build,libdeps,workspace}` hierarchy, and PlatformIO's `clean_build_dir()`
(`platformio/run/helpers.py:26`, called from `run/cli.py:119` on EVERY `pio run`, `-t clean` included) is handed
the PROJECT build dir — `config.get("platformio","build_dir")`, i.e. the shared root — and does
`fs.rmtree(build_dir)` whenever `project.checksum` does not match. Two concurrent invocations could therefore
delete each other's in-flight build. ⇒ each environment now gets its own `.pio-measure/env/<env>/` hierarchy, so
there is NO shared mutable directory between the two builds and the hazard is removed structurally rather than
argued away.

★ THE PATHS ARE THE SAME IN BOTH MODES, deliberately: `build --env X`, `pair --jobs=1` and `pair --jobs=2` all
build X at the identical path. [[B262]] proved `heltec_mobile`'s `payload_sha256` is PATH-DEPENDENT (xtensa), so a
mode that moved the build directory would make the payload hash incomparable across modes — the one figure the
determinism gate most wants to compare. ⚠ The move to per-env roots DID change the path relative to manifests
recorded before 2026-08-30, and therefore changes `heltec_mobile`'s payload hash relative to those (RAM, flash,
objects, sections and symbols are unaffected — that is B262's exact signature; `gateway`/ARM is path-insensitive
and its hash is unchanged). Same-path A/Bs — every comparison this runner is used for — remain exact.

★ WHAT THE DETERMINISM GATE COMPARES, stated exactly rather than loosely: RAM, flash, object count, allocated
PROGBITS section sizes by name, the flashed payload's sha256, the toolchain identity — and a NORMALIZED SYMBOL
INVENTORY whose property set is exactly **name · type · bind · visibility · section-name · size** (sorted; the
address is deliberately excluded — see `symbol_inventory`). It is NOT "the symbol table": addresses are out.

⚠ ccache: NOT in either board environment's path. `tools/ccache_native.py` is attached to `[env:native]` alone
(`platformio.ini:72`); `gateway` inherits `[env:xiao_sx1262]`'s `extra_scripts` (`:113`) and `heltec_mobile`
inherits `[env:heltec_v3]`'s (`:308`), and neither lists it. The manifest's `wrapper_active` fields record this
per build and `compare` refuses on a mismatch, so a future ccache wrapper cannot slip in unmeasured.
"""

from __future__ import annotations

import argparse
import errno
import fcntl
import hashlib
import json
import os
import pty
import re
import select
import shlex
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
MEASURE_ROOT = ROOT / ".pio-measure"
# ★ One hierarchy PER ENVIRONMENT — see the module docstring. Stable (never mode-dependent) and never shared.
ENV_ROOT = MEASURE_ROOT / "env"
LOCK_PATH = MEASURE_ROOT / "measurement.lock"


def build_root(environment_name: str) -> Path:
    return ENV_ROOT / environment_name / "build"


def libdeps_root(environment_name: str) -> Path:
    return ENV_ROOT / environment_name / "libdeps"


def workspace_root(environment_name: str) -> Path:
    return ENV_ROOT / environment_name / "workspace"

FIXED_SOURCE_DATE_EPOCH = "946684800"
FIXED_BUILD_STAMP = "Jan  1 2000 00:00:00"
FIXED_GIT_REVISION = "b206b206b206"
MUTATION_EXCLUSIVITY = (
    "The measurement lock excludes only this runner. Source-mutating mutation batteries in the same checkout "
    "do not share it and must not run concurrently."
)
HOST_LIMITATION = "Linux/POSIX-only: requires fcntl advisory locking and PTYs."

ENVIRONMENTS = {
    "gateway": "firmware.hex",
    "heltec_mobile": "firmware.bin",
}
STAMP_RE = re.compile(
    rb"(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)  ?[0-9]{1,2} "
    rb"[0-9]{4} [0-9]{2}:[0-9]{2}:[0-9]{2}"
)
ANSI_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
SECTION_RE = re.compile(
    r"^\s*\[\s*(\d+)\]\s+(\S+)\s+(\S+)\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+"
    r"([0-9a-fA-F]+)\s+\S+\s*([A-Z]*)\s+",
    re.MULTILINE,
)
# readelf -sW: `  Num:    Value  Size Type    Bind   Vis      Ndx Name`.
# Groups: 1=Size 2=Type 3=Bind 4=Vis 5=Ndx 6=Name. Value (the address) is matched but NOT captured — see
# symbol_inventory() for the normalization and why the address is the one field deliberately dropped.
SYMBOL_RE = re.compile(
    r"^\s*\d+:\s+[0-9a-fA-F]+\s+(0x[0-9a-fA-F]+|\d+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S*)\s*$",
    re.MULTILINE,
)


class MeasureError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise MeasureError(message)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def path_within(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def validate_output_dir(value: str) -> Path:
    output = Path(value)
    if not output.is_absolute():
        output = ROOT / output
    output = output.resolve()
    require(output != ROOT, "output directory cannot be the repository root")
    require(not path_within(output, ROOT / ".pio"), "output directory cannot be inside normal .pio/")
    require(not path_within(output, ENV_ROOT),
            f"output directory cannot be inside {ENV_ROOT.relative_to(ROOT)} (the per-env build hierarchies)")
    if path_within(output, ROOT):
        require(path_within(output, MEASURE_ROOT),
                "an output inside the repository must be below the gitignored .pio-measure/")
    if output.exists():
        require(output.is_dir(), f"output path exists and is not a directory: {output}")
        require(not any(output.iterdir()), f"output directory is not empty: {output}")
    return output


class MeasurementLock:
    def __init__(self, path: Path = LOCK_PATH) -> None:
        self.path = path
        self._handle = None

    def __enter__(self) -> "MeasurementLock":
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._handle = self.path.open("a+", encoding="utf-8")
        try:
            fcntl.flock(self._handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            self._handle.close()
            self._handle = None
            raise MeasureError(f"another measurement runner holds {self.path}") from exc
        self._handle.seek(0)
        self._handle.truncate()
        self._handle.write(f"pid={os.getpid()}\n")
        self._handle.flush()
        return self

    def __exit__(self, exc_type, exc, traceback) -> None:
        if self._handle is not None:
            fcntl.flock(self._handle.fileno(), fcntl.LOCK_UN)
            self._handle.close()
            self._handle = None


def source_snapshot() -> dict[str, Any]:
    listed = subprocess.run(
        ["git", "ls-files", "-co", "--exclude-standard", "-z"],
        cwd=ROOT,
        check=True,
        stdout=subprocess.PIPE,
    ).stdout
    names = sorted(name for name in listed.split(b"\0") if name)
    digest = hashlib.sha256()
    for encoded in names:
        relative = Path(os.fsdecode(encoded))
        path = ROOT / relative
        if path.is_symlink():
            content_hash = hashlib.sha256(os.fsencode(os.readlink(path))).digest()
        else:
            require(path.is_file(), f"source input disappeared while hashing: {relative}")
            content_hash = bytes.fromhex(sha256_file(path))
        digest.update(len(encoded).to_bytes(4, "big"))
        digest.update(encoded)
        digest.update(content_hash)

    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    status = subprocess.check_output(
        ["git", "status", "--porcelain=v1", "--untracked-files=all"], cwd=ROOT
    )
    return {
        "git_head": head,
        "git_status_sha256": hashlib.sha256(status).hexdigest(),
        "file_count": len(names),
        "tree_sha256": digest.hexdigest(),
    }


def metadata_snapshot(root: Path) -> dict[str, Any]:
    if not root.exists():
        return {"exists": False, "entry_count": 0, "sha256": hashlib.sha256(b"absent").hexdigest()}
    digest = hashlib.sha256()
    entries = [root, *sorted(root.rglob("*"))]
    for path in entries:
        stat = path.lstat()
        relative = b"." if path == root else os.fsencode(path.relative_to(root))
        digest.update(len(relative).to_bytes(4, "big"))
        digest.update(relative)
        digest.update(stat.st_mode.to_bytes(8, "big"))
        digest.update(stat.st_size.to_bytes(8, "big"))
        digest.update(stat.st_mtime_ns.to_bytes(8, "big"))
    return {"exists": True, "entry_count": len(entries), "sha256": digest.hexdigest()}


def measurement_environment(environment_name: str, compiler_state: Path | None = None) -> dict[str, str]:
    result = os.environ.copy()
    result.update({
        "SOURCE_DATE_EPOCH": FIXED_SOURCE_DATE_EPOCH,
        "MESHROUTE_GIT_REV_OVERRIDE": FIXED_GIT_REVISION,
        "PLATFORMIO_BUILD_DIR": str(build_root(environment_name)),
        "PLATFORMIO_LIBDEPS_DIR": str(libdeps_root(environment_name)),
        "PLATFORMIO_WORKSPACE_DIR": str(workspace_root(environment_name)),
        "PLATFORMIO_SETTING_ENABLE_TELEMETRY": "no",
        "NO_COLOR": "1",
    })
    if compiler_state is not None:
        result["MESHROUTE_MEASURE_COMPILER_STATE"] = str(compiler_state)
    return result


def bounded_tail(path: Path, count: int = 20, width: int = 300) -> str:
    if not path.exists():
        return "(log missing)"
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()[-count:]
    return "\n".join(line[:width] for line in lines)


def ensure_command_success(returncode: int, label: str, log_path: Path) -> None:
    if returncode != 0:
        raise MeasureError(f"{label} failed with exit {returncode}; tail of {log_path}:\n{bounded_tail(log_path)}")


class LoggedProcess:
    """ONE spawned build step, drained through a PTY, owning its own process group.

    ★ This is the single spawn path: `run_logged()` (sequential) and `run_group()` (the pair's parallel phase) are
    both thin drivers over it, so `--jobs=1` and `--jobs=2` differ ONLY in how many of these are alive at once.
    ⚠ `start_new_session=True`: the child leads its own process group, so a terminal Ctrl-C does not reach it
    behind this process's back — cancelling and REAPING it is this process's job, and only this process's job.
    """

    def __init__(self, command: list[str], environment: dict[str, str], log_path: Path, label: str) -> None:
        self.command = command
        self.label = label
        self.log_path = log_path
        self.returncode: int | None = None
        # PlatformIO's non-TTY spawn path can back-pressure warning-heavy nRF52 compilers before Python drains
        # their pipes. Give it a PTY, consume that PTY continuously, keep the complete stream only in the log.
        self.master, slave = pty.openpty()
        self.log = log_path.open("ab")
        self.log.write(f"\n=== {label} ===\n$ {shlex.join(command)}\n".encode())
        self.log.flush()
        self.process = subprocess.Popen(
            command, cwd=ROOT, env=environment, stdout=slave, stderr=slave, start_new_session=True
        )
        os.close(slave)
        self._open = True

    def fileno(self) -> int:
        return self.master

    def drain(self) -> bool:
        """Read whatever is available. Returns False once the PTY has reached end-of-stream."""
        try:
            chunk = os.read(self.master, 65536)
        except OSError as exc:
            if exc.errno == errno.EIO:  # Linux PTY end-of-stream after the slave closes.
                return False
            raise
        if not chunk:
            return False
        self.log.write(chunk)
        return True

    def cancel(self) -> None:
        if self.process.poll() is None:
            try:
                os.killpg(os.getpgid(self.process.pid), signal.SIGTERM)
            except (ProcessLookupError, PermissionError):
                pass

    def kill(self) -> None:
        if self.process.poll() is None:
            try:
                os.killpg(os.getpgid(self.process.pid), signal.SIGKILL)
            except (ProcessLookupError, PermissionError):
                pass

    def close(self) -> int:
        """Reap the child and release the PTY/log handles. Idempotent."""
        if self._open:
            os.close(self.master)
            self.returncode = self.process.wait()
            self.log.close()
            self._open = False
        return self.returncode


def run_group(specs: list[tuple[list[str], dict[str, str], Path, str]], jobs: int) -> None:
    """Run build steps with at most `jobs` alive at once. jobs=1 IS the sequential arbiter.

    ⛔ SIBLING CANCELLATION: the first nonzero exit cancels, kills and reaps every other live step before raising,
    so a failed pair never leaves a half-built tree with a `pio` still writing into it. The same cleanup runs on
    SIGINT/SIGTERM, which is why the interruption path is the `finally` and not a special case.
    """
    require(jobs >= 1, f"jobs must be >= 1, got {jobs}")
    interrupted: list[str] = []

    def on_signal(signum, _frame):
        interrupted.append(signal.Signals(signum).name)

    previous = {sig: signal.signal(sig, on_signal) for sig in (signal.SIGINT, signal.SIGTERM)}
    pending = list(specs)
    running: list[LoggedProcess] = []
    failure: tuple[str, int, Path] | None = None
    try:
        while pending or running:
            if interrupted:
                raise MeasureError(f"interrupted by {interrupted[0]}; cancelling {len(running)} running build(s)")
            while pending and len(running) < jobs:
                running.append(LoggedProcess(*pending.pop(0)))
            readable, _, _ = select.select([step.fileno() for step in running], [], [], 0.1)
            for handle in readable:
                step = next(item for item in running if item.fileno() == handle)
                if step.drain():
                    continue
                running.remove(step)
                returncode = step.close()
                if returncode != 0 and failure is None:
                    failure = (step.label, returncode, step.log_path)
                    pending.clear()
                    for sibling in running:
                        sibling.cancel()
    finally:
        deadline = time.monotonic() + 10.0
        while running and time.monotonic() < deadline:
            for step in list(running):
                step.cancel()
                if step.process.poll() is not None:
                    running.remove(step)
                    step.close()
            time.sleep(0.05)
        for step in running:
            step.kill()
            step.close()
        for sig, handler in previous.items():
            signal.signal(sig, handler)
    if failure is not None:
        ensure_command_success(failure[1], failure[0], failure[2])


def run_logged(command: list[str], environment: dict[str, str], log_path: Path, label: str) -> None:
    run_group([(command, environment, log_path, label)], jobs=1)


def parse_build_sizes(log_text: str) -> tuple[int, int]:
    ram = re.findall(r"^RAM:.*?\(used\s+([0-9,]+)\s+bytes", log_text, re.MULTILINE)
    flash = re.findall(r"^Flash:.*?\(used\s+([0-9,]+)\s+bytes", log_text, re.MULTILINE)
    require(len(ram) == 1, f"expected one RAM size result, found {len(ram)}")
    require(len(flash) == 1, f"expected one flash size result, found {len(flash)}")
    return int(ram[0].replace(",", "")), int(flash[0].replace(",", ""))


def parse_packages(log_text: str) -> list[dict[str, str]]:
    found = {
        (match.group(1).strip(), match.group(2).strip())
        for match in re.finditer(r"^\s+-\s+(.+?)\s+@\s+(.+?)\s*$", log_text, re.MULTILINE)
    }
    require(bool(found), "build log contains no PlatformIO package identity")
    return [{"name": name, "version": version} for name, version in sorted(found)]


def parse_platform(log_text: str) -> str:
    values = set(re.findall(r"^PLATFORM:\s+(.+?)\s*$", log_text, re.MULTILINE))
    require(len(values) == 1, f"expected one consistent platform identity, found {sorted(values)}")
    return next(iter(values))


COMMAND_FIELDS = {
    "command", "command_template", "tool_token", "executable_resolved", "executable_realpath",
    "wrapper_active", "wrapper_prefix",
}
COMMAND_ALIAS_FIELDS = (
    "command", "tool_token", "executable_resolved", "executable_realpath", "wrapper_active", "wrapper_prefix",
)


def compiler_commands_identity(state_path: Path) -> dict[str, Any]:
    require(state_path.is_file(), f"missing compiler state from PlatformIO hook: {state_path}")
    try:
        state = json.loads(state_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise MeasureError(f"cannot read compiler state {state_path}: {exc}") from exc
    require(set(state) == {"schema", "commands", "link_aliases"},
            f"compiler state top-level fields differ: {sorted(state)}")
    require(state["schema"] == 2, f"unsupported compiler-state schema: {state['schema']}")
    require(set(state["commands"]) == {"cc", "cxx", "link"},
            f"compiler command roles differ: {sorted(state['commands'])}")
    for role, command in state["commands"].items():
        require(set(command) == COMMAND_FIELDS,
                f"{role} command fields differ: {sorted(command)}")
        invoked = Path(command["executable_resolved"])
        require(invoked.is_file(), f"resolved {role} tool does not exist: {invoked}")
        command["version"] = subprocess.run(
            [str(invoked), "--version"], check=True, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True
        ).stdout.splitlines()[0]
    expected_aliases = [
        role for role in ("cc", "cxx")
        if all(state["commands"]["link"][key] == state["commands"][role][key]
               for key in COMMAND_ALIAS_FIELDS)
    ]
    require(state["link_aliases"] == expected_aliases,
            f"link alias evidence is inconsistent: {state['link_aliases']} != {expected_aliases}")
    return state


def readelf_name(compiler_token: str) -> str:
    name = Path(compiler_token).name
    require(name.endswith("g++"), f"cannot derive readelf from compiler {name}")
    return name[:-3] + "readelf"


def resolve_readelf(commands: dict[str, Any]) -> Path:
    cxx = commands["cxx"]
    readelf = Path(cxx["executable_resolved"]).with_name(readelf_name(cxx["tool_token"]))
    require(readelf.is_file(), f"readelf is not beside the resolved compiler: {readelf}")
    return readelf


def symbol_inventory(elf: Path, commands: dict[str, Any], section_names: dict[int, str],
                     output: Path) -> dict[str, Any]:
    """★ THE NORMALIZED SYMBOL INVENTORY — how every recent slice attributed its RAM/flash deltas.

    ★★ THE NORMALIZED PROPERTY SET IS EXACTLY, AND ONLY:
           name · type · bind · visibility · section-name · size
    sorted, one tab-separated row per symbol. Anything not in that list is not part of the fingerprint, and this
    list is the wording to use wherever the gate is described — "symbol tables" would over-claim.

    ⛔ THE ONE readelf FIELD DELIBERATELY DROPPED is **Value (the address)**: any size change shifts every
    following symbol's address, so an address-bearing inventory reports thousands of moved rows for a one-symbol
    change and is useless for the attribution job this exists for. Nothing is lost — byte-exactness INCLUDING
    addresses is already covered by `payload.sha256` and the ELF beside it.

    ⓘ **Ndx is carried as the section NAME, not the index** (QG round 2, GAP 2). Round 2 excluded Ndx arguing it
    was "a per-link table position, not a fact about the symbol". That argument is right about the INTEGER and
    wrong about the placement, and the evidence says the placement must be in:
      · `loadable_sections` filters `section_type != "NOBITS"`, so `.bss`/`.noinit`/`.heap` sizes are NOT in it;
      · NOBITS contributes no bytes, so `payload.sha256` cannot see them either;
      · MEASURED on this tree — `gateway` carries `.bss`, `.noinit` AND `.heap`; `heltec_mobile` carries
        `.noinit`, `.dram0.bss`, `.rtc_noinit` and more.
    ⇒ a variable moving `.bss` -> `.noinit` (retained-across-reset data on nRF52 — a real semantic change) keeps
    its name, type, bind, visibility and size, and NO other qualification field moves. Mapping the index to the
    stable section NAME closes that hole without reintroducing the churn the integer would have caused.

    ⓘ **Visibility is in for the same reason** (QG round 2): a DEFAULT -> HIDDEN change is ELF-only, and this tree
    already carries both — `gateway` has 53 HIDDEN symbols among 6048 DEFAULT, so it is a live property here.

    ⓘ Duplicate rows are KEPT, not deduped (a symbol present in both `.symtab` and `.dynsym` is a real fact about
      the image, and deduping would hide a table appearing or vanishing).

    The manifest carries the fingerprint (count, total size, sha256); the full inventory is written beside it,
    because `heltec_mobile` alone has ~13 000 symbols and a manifest is a comparison object, not an archive.
    """
    readelf = resolve_readelf(commands)
    completed = subprocess.run(
        [str(readelf), "-sW", str(elf)], check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    rows: list[str] = []
    total = 0
    for match in SYMBOL_RE.finditer(completed.stdout):
        size_text, kind, bind, vis, ndx, name = match.groups()
        size = int(size_text, 16) if size_text.lower().startswith("0x") else int(size_text)
        total += size
        # A numeric Ndx becomes the section's NAME; UND/ABS/COM are already stable tokens and stay as they are.
        section = section_names.get(int(ndx), f"?{ndx}") if ndx.isdigit() else ndx
        rows.append(f"{name}\t{kind}\t{bind}\t{vis}\t{section}\t{size}")
    require(bool(rows), f"ELF has no parsable symbol table: {elf}")
    rows.sort()
    body = "\n".join(rows) + "\n"
    output.write_text(body, encoding="utf-8")
    return {
        "symbol_count": len(rows),
        "symbol_size_total": total,
        "symbols_sha256": hashlib.sha256(body.encode("utf-8")).hexdigest(),
    }


def loadable_sections(elf: Path, commands: dict[str, Any], output: Path) -> tuple[dict[str, int], dict[int, str]]:
    """(non-empty allocated PROGBITS sizes by name, index -> name for EVERY section).

    ⚠ The first is the historical measurement and keeps its NOBITS filter; the second exists so
    `symbol_inventory` can name a symbol's section instead of numbering it — including the NOBITS sections the
    first deliberately excludes.
    """
    readelf = resolve_readelf(commands)
    completed = subprocess.run(
        [str(readelf), "-SW", str(elf)], check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    output.write_text(completed.stdout, encoding="utf-8")
    sections: dict[str, int] = {}
    index_names: dict[int, str] = {}
    for match in SECTION_RE.finditer(completed.stdout):
        index, name, section_type, size_hex, flags = match.groups()
        index_names[int(index)] = name
        size = int(size_hex, 16)
        if "A" in flags and section_type != "NOBITS" and size:
            sections[name] = size
    require(bool(sections), "ELF has no non-empty allocated loadable sections")
    require(bool(index_names), "ELF has no parsable section header table")
    return sections, index_names


def validate_build_stamp(data: bytes, expected: str = FIXED_BUILD_STAMP) -> str:
    stamps = [stamp.decode("ascii") for stamp in STAMP_RE.findall(data)]
    require(len(stamps) == 1, f"expected exactly one build stamp, found {len(stamps)}: {stamps}")
    require(stamps[0] == expected, f"build stamp is {stamps[0]!r}, expected fixed {expected!r}")
    return stamps[0]


def locate_artifacts(build_dir: Path, payload_name: str) -> tuple[Path, Path]:
    elf = build_dir / "firmware.elf"
    payload = build_dir / payload_name
    require(elf.is_file(), f"missing firmware ELF: {elf}")
    require(payload.is_file(), f"missing flashed artifact: {payload}")
    return elf, payload


def count_objects(build_dir: Path) -> int:
    count = sum(1 for path in build_dir.rglob("*.o") if path.is_file())
    require(count > 0, f"zero object files under {build_dir}")
    return count


def artifact_record(path: Path) -> dict[str, Any]:
    return {"filename": path.name, "bytes": path.stat().st_size, "sha256": sha256_file(path)}


def collect_manifest(environment_name: str, output_dir: Path, log_path: Path,
                     source: dict[str, Any], normal_pio: dict[str, Any]) -> dict[str, Any]:
    build_dir = build_root(environment_name) / environment_name
    elf, payload = locate_artifacts(build_dir, ENVIRONMENTS[environment_name])
    log_text = ANSI_RE.sub("", log_path.read_text(encoding="utf-8", errors="replace")).replace("\r", "")
    ram, flash = parse_build_sizes(log_text)
    packages = parse_packages(log_text)
    platform = parse_platform(log_text)
    command_state = compiler_commands_identity(output_dir / "compiler-state.json")
    object_count = count_objects(build_dir)
    elf_data = elf.read_bytes()
    stamp = validate_build_stamp(elf_data)
    revision_count = elf_data.count(FIXED_GIT_REVISION.encode("ascii"))
    require(revision_count == 1,
            f"expected exactly one fixed Git-revision literal, found {revision_count}")

    copied_elf = output_dir / elf.name
    copied_payload = output_dir / payload.name
    shutil.copyfile(elf, copied_elf)
    shutil.copyfile(payload, copied_payload)
    sections, section_names = loadable_sections(elf, command_state["commands"], output_dir / "sections.txt")
    symbols = symbol_inventory(elf, command_state["commands"], section_names, output_dir / "symbols.txt")

    pio = shutil.which("pio")
    require(pio is not None, "pio is not available on PATH")
    pio_version = subprocess.run(
        [pio, "--version"], check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    ).stdout.strip()

    return {
        "schema": 2,
        "environment": environment_name,
        "fixed_identity": {
            "source_date_epoch": FIXED_SOURCE_DATE_EPOCH,
            "build_stamp": stamp,
            "build_stamp_count": 1,
            "git_revision": FIXED_GIT_REVISION,
            "git_revision_count": revision_count,
        },
        "source": source,
        "paths": {
            "project_root": str(ROOT),
            "build_root": str(build_root(environment_name)),
            "libdeps_root": str(libdeps_root(environment_name)),
            "workspace_root": str(workspace_root(environment_name)),
            "env_build_dir": str(build_dir),
            "normal_pio_root": str(ROOT / ".pio"),
            "normal_pio_used": False,
        },
        "measurements": {
            "ram_bytes": ram,
            "flash_bytes": flash,
            "object_count": object_count,
            "loadable_sections": sections,
            "symbol_count": symbols["symbol_count"],
            "symbol_size_total": symbols["symbol_size_total"],
            "symbols_sha256": symbols["symbols_sha256"],
        },
        "artifacts": {
            "elf": artifact_record(copied_elf),
            "payload": artifact_record(copied_payload),
        },
        "toolchain": {
            "platformio": pio_version,
            "platform": platform,
            "packages": packages,
            "command_state_schema": command_state["schema"],
            "commands": command_state["commands"],
            "link_aliases": command_state["link_aliases"],
        },
        "normal_pio_metadata": normal_pio,
        # ⚠ DELIBERATELY MODE-BLIND: the manifest records no `--jobs` and no "was this a pair run". That is what
        #   lets `compare` put a jobs=1 manifest against a jobs=2 one and demand EXACT equality — a mode field
        #   here would mismatch by construction and the determinism gate could never pass. The mode lives in the
        #   pair's own `pair.json` summary instead.
        "concurrency": {
            "measurement_lock": str(LOCK_PATH),
            "mutation_batteries_share_lock": False,
            "limitation": MUTATION_EXCLUSIVITY,
        },
        "host": {
            "platform": sys.platform,
            "limitation": HOST_LIMITATION,
        },
    }


def require_pio() -> str:
    pio = shutil.which("pio")
    require(pio is not None, "pio is not available on PATH")
    return pio


Spec = tuple[list[str], dict[str, str], Path, str]


def prepare_measurement(environment_name: str, output_dir: Path, pio: str) -> tuple[Path, Spec, Spec]:
    """Everything up to (not including) spawning: the output dir, the log, the fixed-identity environment and the
    two commands. Identical for `build` and for `pair`, in both `--jobs` modes — that is what makes the per-env
    paths and the per-env log content mode-invariant."""
    output_dir.mkdir(parents=True, exist_ok=True)
    log_path = output_dir / "build.log"
    log_path.write_text("", encoding="utf-8")
    environment = measurement_environment(environment_name, output_dir / "compiler-state.json")
    clean: Spec = ([pio, "run", "--project-dir", str(ROOT), "-e", environment_name, "-t", "clean"],
                   environment, log_path, f"clean {environment_name}")
    build: Spec = ([pio, "run", "--project-dir", str(ROOT), "-e", environment_name],
                   environment, log_path, f"build {environment_name}")
    return log_path, clean, build


def finish_measurement(environment_name: str, output_dir: Path, log_path: Path,
                       source_before: dict[str, Any], normal_before: dict[str, Any]) -> dict[str, Any]:
    manifest = collect_manifest(environment_name, output_dir, log_path, source_before, normal_before)
    manifest_path = output_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"PASS: deterministic board measurement env={environment_name}")
    print(f"  RAM={manifest['measurements']['ram_bytes']} flash={manifest['measurements']['flash_bytes']} "
          f"objects={manifest['measurements']['object_count']}")
    print(f"  stamp={manifest['fixed_identity']['build_stamp']} git={FIXED_GIT_REVISION}")
    print(f"  payload_sha256={manifest['artifacts']['payload']['sha256']}")
    print(f"  manifest={manifest_path}")
    print(f"  full_log={log_path}")
    return manifest


def require_unchanged_inputs(source_before: dict[str, Any], normal_before: dict[str, Any]) -> None:
    require(source_snapshot() == source_before,
            "source tree changed during measurement; mutation batteries are exclusive")
    require(metadata_snapshot(ROOT / ".pio") == normal_before, "normal .pio/ changed during measurement")


def run_environments(environment_names: list[str], output_for, jobs: int) -> tuple[dict[str, Any], float]:
    """★ THE ONE prepare -> clean -> build -> verify -> collect sequence. `build` and `pair` both drive THIS; there
    is no second copy to drift (QG round 1 found the sequence duplicated, `require_unchanged_inputs` included).

    ★ The CLEANS are sequential in BOTH modes, deliberately: they are seconds long, and they are where PlatformIO
    resolves platforms/packages against the shared core dir (`~/.platformio`) — the one resource the per-env roots
    do not separate. Running them one at a time removes that window without costing anything measurable, and it
    keeps each env's `build.log` byte-comparable between the two modes.
    """
    require(jobs >= 1, f"jobs must be >= 1, got {jobs}")
    require(len(set(environment_names)) == len(environment_names), "an environment was named twice")
    roots = [build_root(name) for name in environment_names]
    require(len(set(roots)) == len(roots), "environments do not have distinct build roots")
    pio = require_pio()
    source_before = source_snapshot()
    normal_before = metadata_snapshot(ROOT / ".pio")

    prepared = {name: prepare_measurement(name, output_for(name), pio) for name in environment_names}
    started = time.monotonic()
    run_group([prepared[name][1] for name in environment_names], jobs=1)          # phase 1: cleans, sequential
    run_group([prepared[name][2] for name in environment_names], jobs=jobs)       # phase 2: builds, pooled
    wall = time.monotonic() - started
    require_unchanged_inputs(source_before, normal_before)                        # ← the ONLY call site

    manifests = {
        name: finish_measurement(name, output_for(name), prepared[name][0], source_before, normal_before)
        for name in environment_names
    }
    return manifests, wall


def run_measurement(environment_name: str, output_dir: Path) -> dict[str, Any]:
    """The single-env path IS the pair path with one environment."""
    manifests, _ = run_environments([environment_name], lambda name: output_dir, jobs=1)
    return manifests[environment_name]


def run_pair(environment_names: list[str], output_dir: Path, jobs: int) -> dict[str, dict[str, Any]]:
    """The ruled pair, under ONE lock, in ONE process. `--jobs=1` is the sequential arbiter for `--jobs=2`."""
    manifests, wall = run_environments(environment_names, lambda name: output_dir / name, jobs)
    summary = {
        "schema": 1,
        "jobs": jobs,
        "environments": environment_names,
        "wall_clock_s": round(wall, 3),
        "usable_cores": len(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else os.cpu_count(),
        "env_build_dirs": {name: manifests[name]["paths"]["env_build_dir"] for name in environment_names},
        "ruling": "2026-08-30 owner ruling: D1's 'sequentially' is relaxed for this ruled pair, conditional on the "
                  "proven isolation; any future env addition re-proves it.",
    }
    (output_dir / "pair.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"PASS: ruled pair {', '.join(environment_names)} jobs={jobs} wall={wall:.1f}s "
          f"usable_cores={summary['usable_cores']}")
    print(f"  summary={output_dir / 'pair.json'}")
    return manifests


def load_manifest(path: Path) -> dict[str, Any]:
    require(path.is_file(), f"manifest does not exist: {path}")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise MeasureError(f"cannot read manifest {path}: {exc}") from exc
    require(value.get("schema") == 2, f"unsupported manifest schema in {path}")
    return value


def nested(value: dict[str, Any], dotted: str) -> Any:
    current: Any = value
    for key in dotted.split("."):
        current = current[key]
    return current


QUALIFICATION_FIELDS = (
    "schema",
    "environment",
    "fixed_identity.source_date_epoch",
    "fixed_identity.build_stamp",
    "fixed_identity.build_stamp_count",
    "fixed_identity.git_revision",
    "fixed_identity.git_revision_count",
    "source.git_head",
    "source.git_status_sha256",
    "source.file_count",
    "source.tree_sha256",
    "paths.project_root",
    "paths.build_root",
    "paths.libdeps_root",
    "paths.workspace_root",
    "paths.env_build_dir",
    "paths.normal_pio_root",
    "paths.normal_pio_used",
    "measurements.ram_bytes",
    "measurements.flash_bytes",
    "measurements.object_count",
    "measurements.loadable_sections",
    "measurements.symbol_count",
    "measurements.symbol_size_total",
    "measurements.symbols_sha256",
    "artifacts.payload.filename",
    "artifacts.payload.bytes",
    "artifacts.payload.sha256",
    "toolchain.platformio",
    "toolchain.platform",
    "toolchain.packages",
    "toolchain.command_state_schema",
    "toolchain.commands.cc.command",
    "toolchain.commands.cc.command_template",
    "toolchain.commands.cc.tool_token",
    "toolchain.commands.cc.executable_resolved",
    "toolchain.commands.cc.executable_realpath",
    "toolchain.commands.cc.wrapper_active",
    "toolchain.commands.cc.wrapper_prefix",
    "toolchain.commands.cc.version",
    "toolchain.commands.cxx.command",
    "toolchain.commands.cxx.command_template",
    "toolchain.commands.cxx.tool_token",
    "toolchain.commands.cxx.executable_resolved",
    "toolchain.commands.cxx.executable_realpath",
    "toolchain.commands.cxx.wrapper_active",
    "toolchain.commands.cxx.wrapper_prefix",
    "toolchain.commands.cxx.version",
    "toolchain.commands.link.command",
    "toolchain.commands.link.command_template",
    "toolchain.commands.link.tool_token",
    "toolchain.commands.link.executable_resolved",
    "toolchain.commands.link.executable_realpath",
    "toolchain.commands.link.wrapper_active",
    "toolchain.commands.link.wrapper_prefix",
    "toolchain.commands.link.version",
    "toolchain.link_aliases",
    "normal_pio_metadata.exists",
    "normal_pio_metadata.entry_count",
    "normal_pio_metadata.sha256",
    "concurrency.measurement_lock",
    "concurrency.mutation_batteries_share_lock",
    "concurrency.limitation",
    "host.platform",
    "host.limitation",
)


def compare_qualification(first: dict[str, Any], second: dict[str, Any]) -> None:
    mismatches = [field for field in QUALIFICATION_FIELDS if nested(first, field) != nested(second, field)]
    require(not mismatches, f"repeatability mismatch in: {', '.join(mismatches)}")


def compare_command(first_path: Path, second_path: Path) -> None:
    first = load_manifest(first_path.resolve())
    second = load_manifest(second_path.resolve())
    compare_qualification(first, second)
    print(f"PASS: exact repeatability env={first['environment']}")
    print(f"  RAM={first['measurements']['ram_bytes']} flash={first['measurements']['flash_bytes']} "
          f"objects={first['measurements']['object_count']}")
    print(f"  payload_sha256={first['artifacts']['payload']['sha256']}")
    print("  ELF hashes are diagnostic only and were not used as the verdict")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(
        description=__doc__, epilog=f"{HOST_LIMITATION} {MUTATION_EXCLUSIVITY}"
    )
    subparsers = result.add_subparsers(dest="command", required=True)
    build = subparsers.add_parser("build", help="clean-build one approved ABI with fixed identity")
    build.add_argument("--env", required=True, choices=sorted(ENVIRONMENTS))
    build.add_argument("--output", required=True, help="new/empty artifact directory")
    pair = subparsers.add_parser(
        "pair", help="clean-build BOTH ruled envs under one lock; --jobs=1 is the sequential arbiter")
    pair.add_argument("--output", required=True, help="new/empty directory; each env gets a subdirectory")
    # ⛔ {1,2} ONLY: there are exactly two ruled envs, so --jobs=99 used to REPORT 99 while running two builds —
    #   an honest-reporting defect. A third env must widen this deliberately, and re-prove the isolation.
    pair.add_argument("--jobs", type=int, default=1, choices=(1, 2),
                      help="concurrent BUILD steps (cleans are always sequential); 1 is the arbiter, default 1")
    compare = subparsers.add_parser("compare", help="require two same-source manifests to match exactly")
    compare.add_argument("first", type=Path)
    compare.add_argument("second", type=Path)
    return result


def main() -> None:
    args = parser().parse_args()
    if args.command == "compare":
        compare_command(args.first, args.second)
        return
    output = validate_output_dir(args.output)
    with MeasurementLock():                     # ★ ONE lock, taken ONCE — `pair` never spawns a second invocation
        if args.command == "pair":
            run_pair(sorted(ENVIRONMENTS), output, args.jobs)
        else:
            run_measurement(args.env, output)


if __name__ == "__main__":
    try:
        main()
    except MeasureError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(2)
