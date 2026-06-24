#!/usr/bin/env python3
# MeshRoute — tools/git_rev.py
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# PlatformIO PRE-build hook (extra_scripts = pre:tools/git_rev.py): inject -DGIT_REV='"<short-sha>[-dirty]"' so the
# `version` banner reports the exact source the image was built from. Defaults to "nogit" if git is unavailable
# (the fw_main fallback `#ifndef GIT_REV #define GIT_REV "nogit"` covers an env without this script too).
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


rev = _git_rev()
env.Append(CPPDEFINES=[("GIT_REV", env.StringifyMacro(rev))])   # StringifyMacro -> a quoted C string literal
print("git_rev.py: GIT_REV = %s" % rev)
