#!/usr/bin/env python3
"""Negative controls for tools/measure_board.py, its Git override seam, and §B253 build provenance."""

from __future__ import annotations

import contextlib
import copy
import io
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
    def __init__(self, project_dir: Path | str | None = None) -> None:
        self.stringified = []
        self.appended = []
        # §B253: the hook resolves the worktree from PlatformIO's explicit $PROJECT_DIR, so the fake must answer it.
        self.project_dir = str(project_dir if project_dir is not None else mb.ROOT)
        self.substitutions = []

    def StringifyMacro(self, value):
        self.stringified.append(value)
        return f'"{value}"'

    def Append(self, **values):
        self.appended.append(values)

    def subst(self, value, source=None, target=None):
        self.substitutions.append(value)
        assert value == "$PROJECT_DIR", value
        return self.project_dir


def execute_git_rev(override: str | None, project_dir: Path | str | None = None,
                    hook: Path | None = None) -> tuple[FakePioEnv, dict]:
    fake = FakePioEnv(project_dir)
    with mock.patch.dict(os.environ, {}, clear=False):
        if override is None:
            os.environ.pop("MESHROUTE_GIT_REV_OVERRIDE", None)
        else:
            os.environ["MESHROUTE_GIT_REV_OVERRIDE"] = override
        namespace = runpy.run_path(
            str(hook if hook is not None else mb.ROOT / "tools/git_rev.py"),
            init_globals={"Import": lambda _name: None, "env": fake},
        )
    return fake, namespace


def run_git_rev(override: str | None) -> FakePioEnv:
    return execute_git_rev(override)[0]


# ---- §B253 provenance harness ------------------------------------------------------------------------------------
# The hook is loaded UNDER A VALID OVERRIDE so its module scope invokes no Git; the real `git_revision` it defines is
# then called directly against temporary repositories. ★ There is deliberately NO test-only reimplementation of the
# Git rules: every row below exercises the production command construction and the production dirty decision.
def load_git_rev(hook: Path | None = None) -> dict:
    return execute_git_rev(mb.FIXED_GIT_REVISION, hook=hook)[1]


def git_in(repo: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", "-C", str(repo), *args], check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def head_short(repo: Path) -> str:
    return git_in(repo, "rev-parse", "--short", "HEAD").stdout.decode().strip()


@contextlib.contextmanager
def temporary_repository(commit: bool = True):
    """A throwaway worktree. ⛔ NOTHING here ever runs against the real checkout."""
    with tempfile.TemporaryDirectory(prefix="b253_repo_") as tmp:
        repo = Path(tmp) / "worktree"
        repo.mkdir()
        git_in(repo, "init", "-q", "-b", "main")
        git_in(repo, "config", "user.email", "b253@example.invalid")
        git_in(repo, "config", "user.name", "B253")
        git_in(repo, "config", "commit.gpgsign", "false")
        if commit:
            (repo / ".gitignore").write_text("*.ignored\nignored_dir/\n", encoding="utf-8")
            (repo / "tracked.cpp").write_text("int tracked() { return 0; }\n", encoding="utf-8")
            git_in(repo, "add", ".gitignore", "tracked.cpp")
            git_in(repo, "commit", "-q", "-m", "base")
        yield repo


def completed(returncode: int = 0, stdout: bytes = b"", stderr: bytes = b"") -> subprocess.CompletedProcess:
    return subprocess.CompletedProcess(args=["git"], returncode=returncode, stdout=stdout, stderr=stderr)


def patched_runner(namespace: dict, matches, reply):
    """Real Git for every command except the one a control replaces — so the arm under test is the ONLY difference."""
    real = namespace["_run_git"]

    def runner(args, cwd):
        if matches(args):
            if isinstance(reply, BaseException):
                raise reply
            return reply
        return real(args, cwd)

    return runner


class RecordingRunner:
    """Delegates to the production runner while recording (args, cwd) — proves WHERE the commands ran."""

    def __init__(self, namespace: dict) -> None:
        self.calls: list[tuple[list[str], str]] = []
        self._real = namespace["_run_git"]

    def __call__(self, args, cwd):
        self.calls.append((list(args), str(cwd)))
        return self._real(args, cwd)


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


class GitProvenanceTests(unittest.TestCase):
    """§B253. The defect this pins: `git diff --quiet HEAD` is blind to untracked paths, so an untracked source that
    entered a board image still produced a CLEAN banner. The clean/ignored/detached rows are negative controls — an
    implementation that simply always appends `-dirty` must fail them."""

    def revision(self, repo: Path, runner=None, namespace: dict | None = None) -> str:
        namespace = namespace if namespace is not None else load_git_rev()
        if runner is None:
            return namespace["git_revision"](str(repo))
        return namespace["git_revision"](str(repo), runner)

    # ---- §3.2 dirty matrix ---------------------------------------------------------------------------------
    def test_dirty_matrix(self) -> None:
        def unstaged(repo: Path) -> None:
            (repo / "tracked.cpp").write_text("int tracked() { return 1; }\n", encoding="utf-8")

        def staged(repo: Path) -> None:
            (repo / "staged.cpp").write_text("int staged() { return 2; }\n", encoding="utf-8")
            git_in(repo, "add", "staged.cpp")

        def untracked_file(repo: Path) -> None:
            (repo / "untracked.cpp").write_text("int untracked() { return 3; }\n", encoding="utf-8")
            # usability of the row itself: Git must agree the path is NOT ignored, else it proves nothing
            assert subprocess.run(["git", "-C", str(repo), "check-ignore", "-q", "untracked.cpp"]).returncode == 1

        def untracked_directory(repo: Path) -> None:
            (repo / "variant").mkdir()
            (repo / "variant" / "board.h").write_text("#define VARIANT 1\n", encoding="utf-8")

        def ignored_only(repo: Path) -> None:
            (repo / "artifact.ignored").write_text("build output\n", encoding="utf-8")
            (repo / "ignored_dir").mkdir()
            (repo / "ignored_dir" / "obj.o").write_bytes(b"\0")
            assert subprocess.run(["git", "-C", str(repo), "check-ignore", "-q", "artifact.ignored"]).returncode == 0

        def detached(repo: Path) -> None:
            git_in(repo, "checkout", "-q", "--detach", "HEAD")

        rows = [
            ("clean committed tree", lambda repo: None, ""),
            ("unstaged tracked edit", unstaged, "-dirty"),
            ("staged tracked edit", staged, "-dirty"),
            ("non-ignored untracked file", untracked_file, "-dirty"),
            ("non-ignored untracked directory", untracked_directory, "-dirty"),
            ("ignored untracked file only", ignored_only, ""),
            ("detached HEAD, otherwise clean", detached, ""),
        ]
        namespace = load_git_rev()
        for name, prepare, suffix in rows:
            with self.subTest(name), temporary_repository() as repo:
                prepare(repo)
                self.assertEqual(self.revision(repo, namespace=namespace), head_short(repo) + suffix)

    # ---- §3.1 mutation controls: the untracked row must turn RED again -------------------------------------
    def test_untracked_control_dies_under_the_pre_b253_discriminators(self) -> None:
        source = (mb.ROOT / "tools/git_rev.py").read_text(encoding="utf-8")
        mutations = [
            ("untracked reporting is disabled",
             '"--untracked-files=normal"', '"--untracked-files=no"'),
            ("the pre-B253 `git diff --quiet HEAD` discriminator is restored",
             'status = git(["status", "--porcelain=v1", "--untracked-files=normal", "--ignore-submodules=all"], root)',
             'status = git(["diff", "--quiet", "HEAD", "--ignore-submodules"], root)'),
        ]
        with tempfile.TemporaryDirectory(prefix="b253_mutation_") as tmp:
            for name, old, new in mutations:
                with self.subTest(name):
                    self.assertEqual(source.count(old), 1, f"UNUSABLE control (needs exactly 1 match): {name}")
                    hook = Path(tmp) / "git_rev_mutated.py"
                    hook.write_text(source.replace(old, new, 1), encoding="utf-8")
                    namespace = load_git_rev(hook)
                    with temporary_repository() as repo:
                        (repo / "untracked.cpp").write_text("int untracked() { return 3; }\n", encoding="utf-8")
                        self.assertEqual(self.revision(repo, namespace=namespace), head_short(repo),
                                         "control did not fire: the mutant still saw the untracked file")

    # ---- §2.1 the worktree comes from $PROJECT_DIR, never the process CWD ----------------------------------
    def test_project_dir_not_cwd_decides_the_worktree(self) -> None:
        namespace = load_git_rev()
        with temporary_repository() as repo:
            runner = RecordingRunner(namespace)
            # CWD is the real (dirty) MeshRoute checkout throughout; a CWD-based hook would stamp ITS revision.
            with contextlib.chdir(mb.ROOT):
                self.assertEqual(self.revision(repo, runner=runner), head_short(repo))
            self.assertTrue(runner.calls)
            for args, cwd in runner.calls:
                self.assertTrue(Path(cwd).resolve() == repo.resolve() or Path(cwd).resolve().is_relative_to(
                    repo.resolve()), f"{args} ran in {cwd}, outside the requested project directory")

    def test_whole_hook_stamps_the_project_dir_repository(self) -> None:
        with temporary_repository() as repo:
            with contextlib.chdir(mb.ROOT):
                fake, _ = execute_git_rev(None, project_dir=repo)
            expected = head_short(repo)
        self.assertEqual(fake.substitutions, ["$PROJECT_DIR"])
        self.assertEqual(fake.stringified, [expected])

    # ---- §3.3 fail-loud arms -------------------------------------------------------------------------------
    def test_missing_git_executable_aborts(self) -> None:
        namespace = load_git_rev()
        with temporary_repository() as repo, tempfile.TemporaryDirectory() as empty_path:
            with mock.patch.dict(os.environ, {"PATH": empty_path}):
                with self.assertRaisesRegex(namespace["GitRevisionError"], "cannot run `git"):
                    self.revision(repo, namespace=namespace)

    def test_not_a_worktree_aborts(self) -> None:
        namespace = load_git_rev()
        with tempfile.TemporaryDirectory(prefix="b253_plain_") as plain:
            with self.assertRaisesRegex(namespace["GitRevisionError"], "not inside a Git worktree"):
                self.revision(Path(plain), namespace=namespace)

    def test_repository_without_a_commit_aborts(self) -> None:
        namespace = load_git_rev()
        with temporary_repository(commit=False) as repo:
            with self.assertRaisesRegex(namespace["GitRevisionError"], "cannot resolve HEAD"):
                self.revision(repo, namespace=namespace)

    def test_missing_project_directory_aborts(self) -> None:
        namespace = load_git_rev()
        with self.assertRaisesRegex(namespace["GitRevisionError"], "cannot run `git"):
            self.revision(Path("/nonexistent/b253/project"), namespace=namespace)

    def test_failed_status_command_aborts(self) -> None:
        namespace = load_git_rev()
        runner = patched_runner(namespace, lambda args: args[0] == "status",
                                completed(returncode=128, stderr=b"fatal: synthetic status failure\n"))
        with temporary_repository() as repo:
            with self.assertRaisesRegex(namespace["GitRevisionError"], "git status failed with exit 128"):
                self.revision(repo, runner=runner, namespace=namespace)

    def test_empty_and_malformed_revisions_abort(self) -> None:
        namespace = load_git_rev()
        for stdout in (b"", b"\n", b"not-a-sha\n", b"DEADBEEF\n", b"1234567 8901234\n"):
            with self.subTest(stdout=stdout), temporary_repository() as repo:
                runner = patched_runner(namespace, lambda args: args[:2] == ["rev-parse", "--short"],
                                        completed(stdout=stdout))
                with self.assertRaisesRegex(namespace["GitRevisionError"], "malformed short revision"):
                    self.revision(repo, runner=runner, namespace=namespace)

    def test_empty_worktree_root_aborts(self) -> None:
        namespace = load_git_rev()
        runner = patched_runner(namespace, lambda args: args == ["rev-parse", "--show-toplevel"],
                                completed(stdout=b"\n"))
        with temporary_repository() as repo:
            with self.assertRaisesRegex(namespace["GitRevisionError"], "empty worktree root"):
                self.revision(repo, runner=runner, namespace=namespace)

    def test_no_failure_arm_returns_nogit(self) -> None:
        """The whole point of §0 rule 2: the hook can no longer PRODUCE the `nogit` sentinel on any arm."""
        source = (mb.ROOT / "tools/git_rev.py").read_text(encoding="utf-8")
        self.assertNotIn('"nogit"', source)
        self.assertNotIn("'nogit'", source)

    # ---- §3.3 invalid-UTF-8 porcelain: dirty, never decoded, never printed ---------------------------------
    def test_invalid_utf8_porcelain_is_dirty_without_decoding(self) -> None:
        namespace = load_git_rev()
        undecodable = b"?? \xff\xfe\x80variant/\xc3(.cpp\n"
        self.assertRaises(UnicodeDecodeError, undecodable.decode, "utf-8")   # the row is only meaningful if so
        runner = patched_runner(namespace, lambda args: args[0] == "status", completed(stdout=undecodable))
        with temporary_repository() as repo:
            printed = io.StringIO()
            with contextlib.redirect_stdout(printed):
                revision = self.revision(repo, runner=runner, namespace=namespace)
            expected = head_short(repo) + "-dirty"
        self.assertEqual(revision, expected)
        self.assertEqual(printed.getvalue(), "")            # no path list in the build log
        self.assertNotIn("variant", revision)

    # ---- §3.3 override arm ---------------------------------------------------------------------------------
    def test_override_arm_invokes_no_git(self) -> None:
        with mock.patch.object(subprocess, "run", side_effect=AssertionError("git was invoked")) as blocked:
            fake, _ = execute_git_rev(mb.FIXED_GIT_REVISION)
        self.assertEqual(fake.stringified, [mb.FIXED_GIT_REVISION])
        self.assertEqual(blocked.call_count, 0)
        self.assertEqual(fake.substitutions, [])            # $PROJECT_DIR is not even consulted

    def test_the_no_git_probe_is_capable_of_firing(self) -> None:
        """Control for the control above: without the override, the same block MUST be hit."""
        with mock.patch.object(subprocess, "run", side_effect=AssertionError("git was invoked")):
            with self.assertRaisesRegex(AssertionError, "git was invoked"):
                execute_git_rev(None)

    def test_override_arm_survives_a_git_less_host(self) -> None:
        with tempfile.TemporaryDirectory() as empty_path, mock.patch.dict(os.environ, {"PATH": empty_path}):
            fake, _ = execute_git_rev(mb.FIXED_GIT_REVISION)
        self.assertEqual(fake.stringified, [mb.FIXED_GIT_REVISION])

    def test_logged_revision_source(self) -> None:
        printed = io.StringIO()
        with contextlib.redirect_stdout(printed):
            fake, _ = execute_git_rev(mb.FIXED_GIT_REVISION)
        self.assertEqual(printed.getvalue().strip(),
                         f"git_rev.py: GIT_REV = {mb.FIXED_GIT_REVISION} (source=override)")
        printed = io.StringIO()
        with temporary_repository() as repo, contextlib.redirect_stdout(printed):
            execute_git_rev(None, project_dir=repo)
            expected = head_short(repo)
        self.assertEqual(printed.getvalue().strip(), f"git_rev.py: GIT_REV = {expected} (source=git)")

    def test_removing_the_override_returns_to_the_git_derived_state(self) -> None:
        with temporary_repository() as repo:
            with_override, _ = execute_git_rev(mb.FIXED_GIT_REVISION, project_dir=repo)
            without, _ = execute_git_rev(None, project_dir=repo)
            self.assertEqual(with_override.stringified, [mb.FIXED_GIT_REVISION])
            self.assertEqual(without.stringified, [head_short(repo)])

    # ---- §3.4 derived hook coverage ------------------------------------------------------------------------
    def test_effective_environment_coverage_and_controls(self) -> None:
        config = probe_build_identity.effective_config()
        self.assertGreaterEqual(probe_build_identity.check_environment_coverage(config), 2)
        controls = probe_build_identity.coverage_controls(config)
        self.assertEqual(len(controls), 4)
        for name, mutated in controls:
            with self.subTest(name):
                self.assertNotEqual(mutated, config, "UNUSABLE control: the mutation changed nothing")
                with self.assertRaises(probe_build_identity.ProbeFailure):
                    probe_build_identity.check_environment_coverage(mutated)


if __name__ == "__main__":
    unittest.main(verbosity=2)
