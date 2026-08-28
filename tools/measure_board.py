#!/usr/bin/env python3
"""Deterministic board RAM/flash measurement and repeatability comparison.

The runner serializes only other invocations of itself. Source-mutating mutation
batteries in this checkout do not share its lock and must not run concurrently.
Artifacts produced here carry a fixed build identity and are measurement inputs,
not ordinary provenance-bearing firmware to flash.

This runner is Linux/POSIX-only: it relies on fcntl advisory locking and PTYs.
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
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
MEASURE_ROOT = ROOT / ".pio-measure"
BUILD_ROOT = MEASURE_ROOT / "build"
LIBDEPS_ROOT = MEASURE_ROOT / "libdeps"
WORKSPACE_ROOT = MEASURE_ROOT / "workspace"
LOCK_PATH = MEASURE_ROOT / "measurement.lock"

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
    r"^\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+"
    r"([0-9a-fA-F]+)\s+\S+\s*([A-Z]*)\s+",
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
    for reserved in (BUILD_ROOT, LIBDEPS_ROOT, WORKSPACE_ROOT):
        require(not path_within(output, reserved), f"output directory cannot be inside {reserved.relative_to(ROOT)}")
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


def measurement_environment(compiler_state: Path | None = None) -> dict[str, str]:
    result = os.environ.copy()
    result.update({
        "SOURCE_DATE_EPOCH": FIXED_SOURCE_DATE_EPOCH,
        "MESHROUTE_GIT_REV_OVERRIDE": FIXED_GIT_REVISION,
        "PLATFORMIO_BUILD_DIR": str(BUILD_ROOT),
        "PLATFORMIO_LIBDEPS_DIR": str(LIBDEPS_ROOT),
        "PLATFORMIO_WORKSPACE_DIR": str(WORKSPACE_ROOT),
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


def run_logged(command: list[str], environment: dict[str, str], log_path: Path, label: str) -> None:
    # PlatformIO's non-TTY spawn path can back-pressure warning-heavy nRF52 compilers before Python drains their
    # pipes. Give it a PTY, consume that PTY continuously, and keep the complete stream only in the artifact log.
    master, slave = pty.openpty()
    with log_path.open("ab") as log:
        log.write(f"\n=== {label} ===\n$ {shlex.join(command)}\n".encode())
        log.flush()
        process = subprocess.Popen(
            command,
            cwd=ROOT,
            env=environment,
            stdout=slave,
            stderr=slave,
        )
        os.close(slave)
        try:
            while True:
                try:
                    chunk = os.read(master, 65536)
                except OSError as exc:
                    if exc.errno == errno.EIO:  # Linux PTY end-of-stream after the slave closes.
                        break
                    raise
                if not chunk:
                    break
                log.write(chunk)
        finally:
            os.close(master)
        returncode = process.wait()
    ensure_command_success(returncode, label, log_path)


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


def loadable_sections(elf: Path, commands: dict[str, Any], output: Path) -> dict[str, int]:
    cxx = commands["cxx"]
    readelf = Path(cxx["executable_resolved"]).with_name(readelf_name(cxx["tool_token"]))
    require(readelf.is_file(), f"readelf is not beside the resolved compiler: {readelf}")
    completed = subprocess.run(
        [str(readelf), "-SW", str(elf)], check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    output.write_text(completed.stdout, encoding="utf-8")
    sections: dict[str, int] = {}
    for match in SECTION_RE.finditer(completed.stdout):
        name, section_type, size_hex, flags = match.groups()
        size = int(size_hex, 16)
        if "A" in flags and section_type != "NOBITS" and size:
            sections[name] = size
    require(bool(sections), "ELF has no non-empty allocated loadable sections")
    return sections


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
    build_dir = BUILD_ROOT / environment_name
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
    sections = loadable_sections(elf, command_state["commands"], output_dir / "sections.txt")

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
            "build_root": str(BUILD_ROOT),
            "libdeps_root": str(LIBDEPS_ROOT),
            "workspace_root": str(WORKSPACE_ROOT),
            "normal_pio_root": str(ROOT / ".pio"),
            "normal_pio_used": False,
        },
        "measurements": {
            "ram_bytes": ram,
            "flash_bytes": flash,
            "object_count": object_count,
            "loadable_sections": sections,
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


def run_measurement(environment_name: str, output_dir: Path) -> dict[str, Any]:
    pio = shutil.which("pio")
    require(pio is not None, "pio is not available on PATH")
    source_before = source_snapshot()
    normal_before = metadata_snapshot(ROOT / ".pio")
    output_dir.mkdir(parents=True, exist_ok=True)
    log_path = output_dir / "build.log"
    log_path.write_text("", encoding="utf-8")
    environment = measurement_environment(output_dir / "compiler-state.json")

    clean = [pio, "run", "--project-dir", str(ROOT), "-e", environment_name, "-t", "clean"]
    build = [pio, "run", "--project-dir", str(ROOT), "-e", environment_name]
    run_logged(clean, environment, log_path, f"clean {environment_name}")
    run_logged(build, environment, log_path, f"build {environment_name}")

    source_after = source_snapshot()
    require(source_after == source_before, "source tree changed during measurement; mutation batteries are exclusive")
    normal_after = metadata_snapshot(ROOT / ".pio")
    require(normal_after == normal_before, "normal .pio/ changed during measurement")

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
    "paths.normal_pio_root",
    "paths.normal_pio_used",
    "measurements.ram_bytes",
    "measurements.flash_bytes",
    "measurements.object_count",
    "measurements.loadable_sections",
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
    with MeasurementLock():
        run_measurement(args.env, output)


if __name__ == "__main__":
    try:
        main()
    except MeasureError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(2)
