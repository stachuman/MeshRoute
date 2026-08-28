#!/usr/bin/env python3
"""Negative controls for tools/measure_board.py and its Git override seam."""

from __future__ import annotations

import copy
import os
import re
import runpy
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import measure_board as mb
import probe_build_identity


class FakePioEnv:
    def __init__(self) -> None:
        self.stringified = []
        self.appended = []

    def StringifyMacro(self, value):
        self.stringified.append(value)
        return f'"{value}"'

    def Append(self, **values):
        self.appended.append(values)


def execute_git_rev(override: str | None) -> tuple[FakePioEnv, dict]:
    fake = FakePioEnv()
    with mock.patch.dict(os.environ, {}, clear=False):
        if override is None:
            os.environ.pop("MESHROUTE_GIT_REV_OVERRIDE", None)
        else:
            os.environ["MESHROUTE_GIT_REV_OVERRIDE"] = override
        namespace = runpy.run_path(
            str(mb.ROOT / "tools/git_rev.py"),
            init_globals={"Import": lambda _name: None, "env": fake},
        )
    return fake, namespace


def run_git_rev(override: str | None) -> FakePioEnv:
    return execute_git_rev(override)[0]


class FakeCommandEnv:
    def __init__(self, cc: str, cxx: str, link: str) -> None:
        self.values = {
            "$PROJECT_DIR": str(mb.ROOT),
            "$CC": cc,
            "$CXX": cxx,
            "$LINK": link,
        }
        self.templates = {
            "ENV": {"PATH": os.environ.get("PATH", "")},
            "CCCOM": "$CC -c $SOURCES",
            "CXXCOM": "$CXX -c $SOURCES",
            "LINKCOM": "$LINK -o $TARGET $SOURCES",
        }

    def subst(self, value, source=None, target=None):
        return self.values[value]

    def get(self, key, default=None):
        return self.templates.get(key, default)


def command_record(executable: str, template: str) -> dict:
    return {
        "command": executable,
        "command_template": template,
        "tool_token": Path(executable).name,
        "executable_resolved": executable,
        "executable_realpath": executable,
        "wrapper_active": False,
        "wrapper_prefix": [],
        "version": "tool version 1",
    }


def qualification_manifest() -> dict:
    cc = "/tool/arm-none-eabi-gcc"
    cxx = "/tool/arm-none-eabi-g++"
    return {
        "schema": 2,
        "environment": "gateway",
        "fixed_identity": {
            "source_date_epoch": mb.FIXED_SOURCE_DATE_EPOCH,
            "build_stamp": mb.FIXED_BUILD_STAMP,
            "build_stamp_count": 1,
            "git_revision": mb.FIXED_GIT_REVISION,
            "git_revision_count": 1,
        },
        "source": {
            "git_head": "head",
            "git_status_sha256": "status",
            "file_count": 4,
            "tree_sha256": "source",
        },
        "paths": {
            "project_root": "/repo",
            "build_root": "/repo/.pio-measure/build",
            "libdeps_root": "/repo/.pio-measure/libdeps",
            "workspace_root": "/repo/.pio-measure/workspace",
            "normal_pio_root": "/repo/.pio",
            "normal_pio_used": False,
        },
        "measurements": {
            "ram_bytes": 1,
            "flash_bytes": 2,
            "object_count": 3,
            "loadable_sections": {".text": 2},
        },
        "artifacts": {"payload": {"filename": "firmware.hex", "bytes": 4, "sha256": "payload"}},
        "toolchain": {
            "platformio": "pio",
            "platform": "platform",
            "packages": [{"name": "toolchain", "version": "1"}],
            "command_state_schema": 2,
            "commands": {
                "cc": command_record(cc, "$CC -c $SOURCES"),
                "cxx": command_record(cxx, "$CXX -c $SOURCES"),
                "link": command_record(cxx, "$LINK -o $TARGET $SOURCES"),
            },
            "link_aliases": ["cxx"],
        },
        "normal_pio_metadata": {"exists": True, "entry_count": 5, "sha256": "untouched"},
        "concurrency": {
            "measurement_lock": "/repo/.pio-measure/measurement.lock",
            "mutation_batteries_share_lock": False,
            "limitation": mb.MUTATION_EXCLUSIVITY,
        },
        "host": {"platform": "linux", "limitation": mb.HOST_LIMITATION},
    }


EXPECTED_QUALIFICATION_FIELDS = (
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


def mutate_value(value):
    if isinstance(value, bool):
        return not value
    if isinstance(value, int):
        return value + 1
    if isinstance(value, str):
        return value + "-mismatch"
    if isinstance(value, list):
        return [*value, "mismatch"]
    if isinstance(value, dict):
        return {**value, "_mismatch": True}
    raise AssertionError(f"no mutation for qualification value {value!r}")


def mutate_nested(value: dict, dotted: str) -> None:
    current = value
    parts = dotted.split(".")
    for key in parts[:-1]:
        current = current[key]
    current[parts[-1]] = mutate_value(current[parts[-1]])


class MeasureBoardTests(unittest.TestCase):
    def test_source_identity_structure(self) -> None:
        self.assertEqual(probe_build_identity.check_source(), 13)

    def test_measurement_state_isolated_from_normal_pio(self) -> None:
        environment = mb.measurement_environment()
        self.assertEqual(environment["PLATFORMIO_BUILD_DIR"], str(mb.BUILD_ROOT))
        self.assertEqual(environment["PLATFORMIO_LIBDEPS_DIR"], str(mb.LIBDEPS_ROOT))
        self.assertEqual(environment["PLATFORMIO_WORKSPACE_DIR"], str(mb.WORKSPACE_ROOT))
        self.assertNotIn(str(mb.ROOT / ".pio"), {
            environment["PLATFORMIO_BUILD_DIR"],
            environment["PLATFORMIO_LIBDEPS_DIR"],
            environment["PLATFORMIO_WORKSPACE_DIR"],
        })
        self.assertIn(".pio-measure/", (mb.ROOT / ".gitignore").read_text(encoding="utf-8"))

    def test_missing_flashed_artifact_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build / "firmware.elf").write_bytes(b"elf")
            with self.assertRaisesRegex(mb.MeasureError, "missing flashed artifact"):
                mb.locate_artifacts(build, "firmware.hex")

    def test_zero_objects_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(mb.MeasureError, "zero object files"):
                mb.count_objects(Path(directory))

    def test_zero_and_multiple_stamps_are_rejected(self) -> None:
        with self.subTest("zero"):
            with self.assertRaisesRegex(mb.MeasureError, "exactly one build stamp, found 0"):
                mb.validate_build_stamp(b"no timestamp")
        with self.subTest("multiple"):
            data = (mb.FIXED_BUILD_STAMP + " " + mb.FIXED_BUILD_STAMP).encode()
            with self.assertRaisesRegex(mb.MeasureError, "exactly one build stamp, found 2"):
                mb.validate_build_stamp(data)

    def test_wrong_fixed_stamp_is_rejected(self) -> None:
        with self.assertRaisesRegex(mb.MeasureError, "expected fixed"):
            mb.validate_build_stamp(b"Aug 27 2026 20:00:00")

    def test_missing_size_results_are_rejected(self) -> None:
        with self.subTest("ram"):
            with self.assertRaisesRegex(mb.MeasureError, "RAM size result"):
                mb.parse_build_sizes("Flash: [=] (used 2 bytes from 3 bytes)\n")
        with self.subTest("flash"):
            with self.assertRaisesRegex(mb.MeasureError, "flash size result"):
                mb.parse_build_sizes("RAM: [=] (used 1 bytes from 3 bytes)\n")

    def test_build_failure_is_rejected_with_log_tail(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "build.log"
            with self.assertRaisesRegex(mb.MeasureError, "synthetic build failed with exit 7"):
                mb.run_logged(
                    [sys.executable, "-c", "raise SystemExit(7)"],
                    os.environ.copy(),
                    log,
                    "synthetic build",
                )
            self.assertIn("synthetic build", log.read_text(encoding="utf-8"))

    def test_malformed_and_empty_git_overrides_are_rejected(self) -> None:
        for value in ("", "xyz", "ABCDEF0", "1234567 dirty", "1234567/dirty"):
            with self.subTest(value=value):
                with self.assertRaisesRegex(ValueError, "MESHROUTE_GIT_REV_OVERRIDE"):
                    run_git_rev(value)

    def test_valid_git_override_is_used_exactly(self) -> None:
        fake = run_git_rev(mb.FIXED_GIT_REVISION)
        self.assertEqual(fake.stringified, [mb.FIXED_GIT_REVISION])
        self.assertEqual(len(fake.appended), 1)

    def test_normal_git_path_ignores_measurement_identity(self) -> None:
        fake = run_git_rev(None)
        self.assertEqual(len(fake.stringified), 1)
        self.assertRegex(fake.stringified[0], r"^[0-9a-f]{7}(?:-dirty)?$")
        self.assertNotEqual(fake.stringified[0], mb.FIXED_GIT_REVISION)

    def test_compiler_state_records_cc_cxx_link_and_alias(self) -> None:
        cc = shutil.which("gcc")
        cxx = shutil.which("g++")
        self.assertIsNotNone(cc)
        self.assertIsNotNone(cxx)
        _, namespace = execute_git_rev(None)
        mb.MEASURE_ROOT.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=mb.MEASURE_ROOT) as directory:
            path = Path(directory) / "compiler-state.json"
            namespace["_write_measurement_compiler_state"](
                path, FakeCommandEnv(cc, cxx, cxx), [], []
            )
            loaded = mb.compiler_commands_identity(path)
        self.assertEqual(set(loaded["commands"]), {"cc", "cxx", "link"})
        self.assertEqual(loaded["link_aliases"], ["cxx"])
        for role in ("cc", "cxx", "link"):
            self.assertTrue(loaded["commands"][role]["version"])

    def assert_wrapper_mismatch_rejected(self, role: str) -> None:
        first = qualification_manifest()
        second = copy.deepcopy(first)
        second["toolchain"]["commands"][role]["wrapper_active"] = True
        second["toolchain"]["commands"][role]["wrapper_prefix"] = ["ccache"]
        with self.assertRaisesRegex(mb.MeasureError, re.escape(f"toolchain.commands.{role}.wrapper_active")):
            mb.compare_qualification(first, second)

    def test_cc_wrapper_mismatch_fails_qualification(self) -> None:
        self.assert_wrapper_mismatch_rejected("cc")

    def test_cxx_wrapper_mismatch_fails_qualification(self) -> None:
        self.assert_wrapper_mismatch_rejected("cxx")

    def test_link_wrapper_mismatch_fails_qualification(self) -> None:
        self.assert_wrapper_mismatch_rejected("link")

    def test_qualification_inventory_is_independently_pinned(self) -> None:
        self.assertEqual(mb.QUALIFICATION_FIELDS, EXPECTED_QUALIFICATION_FIELDS)

    def test_every_qualification_field_mismatch_is_rejected(self) -> None:
        first = qualification_manifest()
        for field in EXPECTED_QUALIFICATION_FIELDS:
            with self.subTest(field=field):
                second = copy.deepcopy(first)
                mutate_nested(second, field)
                with self.assertRaisesRegex(mb.MeasureError, re.escape(field)):
                    mb.compare_qualification(first, second)

    def test_concurrent_runner_is_rejected_by_cli(self) -> None:
        with tempfile.TemporaryDirectory() as directory, mb.MeasurementLock():
            completed = subprocess.run(
                [sys.executable, str(mb.ROOT / "tools/measure_board.py"), "build",
                 "--env", "gateway", "--output", str(Path(directory) / "result")],
                cwd=mb.ROOT,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
        self.assertEqual(completed.returncode, 2)
        self.assertIn("another measurement runner holds", completed.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
