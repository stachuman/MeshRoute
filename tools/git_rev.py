#!/usr/bin/env python3
# MeshRoute — tools/git_rev.py
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# PlatformIO PRE-build hook (extra_scripts = pre:tools/git_rev.py): inject -DGIT_REV='"<short-sha>[-dirty]"' so the
# `version` banner reports the exact source the image was built from.
#
# ★ §B253 (2026-08-28) — TWO CORRECTIONS, both owner-ruled in the design's §0:
#   1. `-dirty` describes the WHOLE non-ignored worktree. The old discriminator was `git diff --quiet HEAD`, which is
#      blind to UNTRACKED paths: an untracked .cpp/.h/variant selected by a board build changed the image while the
#      banner still read the clean short id. One porcelain query now answers staged + unstaged + untracked at once.
#   2. An ordinary board build REQUIRES usable Git provenance: every failure arm ABORTS with one bounded diagnostic.
#      `nogit` is no longer produced here — a silently unidentifiable image is exactly the failure §B200/§B213 cost us.
# The ONE exception is the deterministic board-measurement runner's narrowly validated MESHROUTE_GIT_REV_OVERRIDE
# (§B206 S2, unchanged below): it bypasses Git entirely and logs `source=override`.
import os
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess

Import("env")   # noqa: F821  (PlatformIO injects `env` / `Import` into the script's globals)

# Short ids Git may emit: core.abbrev scales with repository size, so pin the SHAPE (ASCII hex) and a generous
# length window rather than today's 7. Only the revision is ever decoded (§2.1); path bytes never are.
_SHORT_REVISION = re.compile(r"[0-9a-f]{4,40}")


class GitRevisionError(Exception):
    """Ordinary-path Git provenance is unusable — the board build must not continue with an invented revision."""


def _run_git(args, cwd):
    """The default command seam: argument vector, NO shell, explicit working directory.

    Tests inject a substitute with the same contract (`(args, cwd) -> completed`, `.returncode` + bytes `.stdout`)
    so §3's matrices exercise THIS function's command construction and decision, never a test-only copy of it."""
    return subprocess.run(["git", *args], cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def _detail(completed):
    """One bounded, path-free excerpt of git's own complaint (stderr only — porcelain stdout is never printed)."""
    first = completed.stderr.split(b"\n", 1)[0] if completed.stderr else b""
    return first.decode("utf-8", "replace")[:200]


def git_revision(project_dir, runner=_run_git):
    """THE ordinary-path authority: `<short-sha>` or `<short-sha>-dirty` for the worktree containing `project_dir`.

    `project_dir` is PlatformIO's explicit `$PROJECT_DIR`, never the process's incidental CWD — a build invoked from
    elsewhere must not stamp a different tree's revision. Raises GitRevisionError on every unusable arm."""
    def git(args, cwd):
        try:
            return runner(args, cwd)
        except OSError as exc:                      # git absent, or the directory itself is gone
            raise GitRevisionError("cannot run `git %s` in %s: %s" % (args[0], cwd, exc.__class__.__name__)) from exc

    top = git(["rev-parse", "--show-toplevel"], project_dir)
    if top.returncode != 0:
        raise GitRevisionError("%s is not inside a Git worktree (%s)" % (project_dir, _detail(top)))
    # os.fsdecode, not .decode(): a repository path may itself carry non-UTF-8 bytes and must still round-trip to cwd.
    root = os.fsdecode(top.stdout.strip())
    if not root:
        raise GitRevisionError("git returned an empty worktree root for %s" % project_dir)

    head = git(["rev-parse", "--short", "HEAD"], root)
    if head.returncode != 0:                        # unborn branch / empty repository / broken HEAD (detached is fine)
        raise GitRevisionError("cannot resolve HEAD in %s (%s)" % (root, _detail(head)))
    revision = head.stdout.decode("ascii", "replace").strip()
    if not _SHORT_REVISION.fullmatch(revision):
        raise GitRevisionError("git produced a malformed short revision (%d chars) in %s" % (len(revision), root))

    # ONE query for staged + unstaged + untracked. `normal` is enough: an untracked DIRECTORY marks the tree dirty
    # without enumerating its children. Submodule dirtiness stays excluded (design §0 rule 4, the pre-B253 decision).
    status = git(["status", "--porcelain=v1", "--untracked-files=normal", "--ignore-submodules=all"], root)
    if status.returncode != 0:
        raise GitRevisionError("git status failed with exit %d in %s (%s)"
                               % (status.returncode, root, _detail(status)))
    # ★ stdout stays BYTES and only empty-vs-non-empty is tested: Git permits path bytes that are not valid UTF-8, and
    #   such a path must make the image `-dirty`, never fail an otherwise valid build. Filenames are never listed —
    #   they may be sensitive and an unbounded list would drown the build log.
    return revision + ("-dirty" if status.stdout else "")


def _validated_override(value):
    if not re.fullmatch(r"[0-9a-f]{7,40}(?:-dirty)?", value):
        raise ValueError("MESHROUTE_GIT_REV_OVERRIDE must be 7..40 lowercase hex digits, optionally followed by -dirty")
    return value


def _command_state(pio_env, variable, template_variable, suffixes, source_nodes, target_nodes):
    raw = str(pio_env.subst("$" + variable, source=source_nodes, target=target_nodes))
    tokens = shlex.split(raw)
    if not tokens:
        raise ValueError("PlatformIO supplied an empty %s command" % variable)
    tool_index = next(
        (index for index, token in enumerate(tokens) if Path(token).name.endswith(suffixes)),
        None,
    )
    if tool_index is None:
        raise ValueError("PlatformIO %s command contains no recognised tool executable: %s" % (variable, raw))
    child_path = pio_env.get("ENV", {}).get("PATH", os.environ.get("PATH", ""))
    tool_path = shutil.which(tokens[tool_index], path=child_path)
    if tool_path is None:
        raise ValueError("cannot resolve PlatformIO %s tool: %s" % (variable, tokens[tool_index]))
    invoked = Path(tool_path).absolute()
    real = invoked.resolve()
    wrapper_prefix = tokens[:tool_index]
    wrapper_active = bool(wrapper_prefix) or real.name in {"ccache", "sccache", "distcc", "icecc"}
    resolved_tokens = list(tokens)
    resolved_tokens[tool_index] = str(invoked)
    return {
        "command": shlex.join(resolved_tokens),
        "command_template": str(pio_env.get(template_variable, "")),
        "tool_token": tokens[tool_index],
        "executable_resolved": str(invoked),
        "executable_realpath": str(real),
        "wrapper_active": wrapper_active,
        "wrapper_prefix": wrapper_prefix,
    }


def _same_command(left, right):
    keys = (
        "command", "tool_token", "executable_resolved", "executable_realpath",
        "wrapper_active", "wrapper_prefix",
    )
    return all(left[key] == right[key] for key in keys)


def _write_measurement_compiler_state(target_value, pio_env, source_nodes, target_nodes):
    root = Path(str(pio_env.subst("$PROJECT_DIR"))).resolve()
    target = Path(target_value).resolve()
    try:
        target.relative_to(root / ".pio-measure")
    except ValueError as exc:
        raise ValueError("MESHROUTE_MEASURE_COMPILER_STATE must be below the repository .pio-measure/") from exc

    commands = {
        "cc": _command_state(pio_env, "CC", "CCCOM", ("gcc", "cc"), source_nodes, target_nodes),
        "cxx": _command_state(pio_env, "CXX", "CXXCOM", ("g++", "c++"), source_nodes, target_nodes),
        "link": _command_state(
            pio_env, "LINK", "LINKCOM", ("g++", "gcc", "c++", "cc", "ld"), source_nodes, target_nodes
        ),
    }
    state = {
        "schema": 2,
        "commands": commands,
        "link_aliases": [role for role in ("cc", "cxx") if _same_command(commands["link"], commands[role])],
    }
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(state, indent=2, sort_keys=True) + "\n", encoding="utf-8")


override = os.environ.get("MESHROUTE_GIT_REV_OVERRIDE")
if override is not None:
    # §B206 S2's ONE explicit seam, its validator untouched: no Git is invoked on this arm, and a malformed value
    # still fails loud through the validator rather than degrading to a Git-derived or invented revision.
    rev = _validated_override(override)
    rev_source = "override"
else:
    try:
        rev = git_revision(str(env.subst("$PROJECT_DIR")))
    except GitRevisionError as exc:
        # One bounded diagnosis instead of a traceback: SCons would otherwise surface the Python frames first.
        raise SystemExit(
            "git_rev.py: ABORT — board build provenance is unusable: %s\n"
            "  A board image must be identifiable from its own banner (§B253). Build from a Git worktree with a\n"
            "  resolvable HEAD, or set a validated MESHROUTE_GIT_REV_OVERRIDE (the deterministic measurement seam)."
            % exc)
    rev_source = "git"
env.Append(CPPDEFINES=[("GIT_REV", env.StringifyMacro(rev))])   # StringifyMacro -> a quoted C string literal
compiler_state_target = os.environ.get("MESHROUTE_MEASURE_COMPILER_STATE")
if compiler_state_target is not None:
    def _record_measurement_compiler_state(target, source, env):
        _write_measurement_compiler_state(compiler_state_target, env, source, target)

    env.AddPostAction("$PROGPATH", _record_measurement_compiler_state)
# `source=` makes an accidentally inherited override visible in EVERY board log, where a bare revision would not be.
print("git_rev.py: GIT_REV = %s (source=%s)" % (rev, rev_source))
