#!/usr/bin/env python3
# MeshRoute — tools/git_rev.py
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# PlatformIO PRE-build hook (extra_scripts = pre:tools/git_rev.py): inject -DGIT_REV='"<short-sha>[-dirty]"' so the
# `version` banner reports the exact source the image was built from. Defaults to "nogit" if git is unavailable
# (firmware_commands.cpp's fallback covers an env without this script too). The deterministic board-measurement
# runner may supply one narrowly validated fixed revision; ordinary builds retain the Git-derived path below.
import os
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess

Import("env")   # noqa: F821  (PlatformIO injects `env` / `Import` into the script's globals)


def _git_rev():
    try:
        sha = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"],
                                      stderr=subprocess.DEVNULL).decode().strip()
        if not sha:
            return "nogit"
        # working tree differs from HEAD (the user commits separately; uncommitted work => -dirty) — accurate provenance
        dirty = subprocess.call(["git", "diff", "--quiet", "HEAD", "--ignore-submodules"],
                                stderr=subprocess.DEVNULL) != 0
        return sha + ("-dirty" if dirty else "")
    except Exception:
        return "nogit"


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
rev = _validated_override(override) if override is not None else _git_rev()
env.Append(CPPDEFINES=[("GIT_REV", env.StringifyMacro(rev))])   # StringifyMacro -> a quoted C string literal
compiler_state_target = os.environ.get("MESHROUTE_MEASURE_COMPILER_STATE")
if compiler_state_target is not None:
    def _record_measurement_compiler_state(target, source, env):
        _write_measurement_compiler_state(compiler_state_target, env, source, target)

    env.AddPostAction("$PROGPATH", _record_measurement_compiler_state)
print("git_rev.py: GIT_REV = %s" % rev)
