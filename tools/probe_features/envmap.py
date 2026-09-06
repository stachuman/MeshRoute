#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""Derive the ENVIRONMENT -> PROBE-CELL mapping for `tools/probe_features/run.sh` (remote-admin v2 slice 1).

WHY THIS FILE EXISTS, and why the mapping is derived rather than typed. `lib/core/mr_features.h` derives
`MR_FEAT_RADMIN_CLIENT` / `MR_FEAT_RADMIN_ACCEPT` from `defined(ARDUINO)` plus the profile. The probe compiles
NINE configurations and asserts eight flag values in each — but a configuration nobody builds proves nothing, and
a real env the probe does not cover is a hole. So the env set, its profiles, its frameworks and its OLED values
are read from `platformio.ini` (through PlatformIO's OWN resolver, so `extends` chains resolve exactly as a build
resolves them) and from the simulator's `CMakeLists.txt`, and every env must land in exactly one probe cell.

⛔ NOT A MODEL OF THE HEADER. This file contains no copy of the endpoint derivation and no expected CLIENT/ACCEPT
   value; it maps envs to cell NAMES. The values are asserted by `probe_main.cpp` against the runner's ruled table.

⛔ FAIL LOUD (C2). A missing `pio`, a missing simulator checkout, an unparsable env or a count that moved is a
   FAILURE, never a skipped check: a new env silently outside the matrix is exactly the hole this exists to close.

USAGE:  python3 tools/probe_features/envmap.py          # prints `  ok  `/`  FAIL ` lines; exit 1 on any failure
Env:    MR_LUS_SRC=<dir>   the simulator source checkout (default: <repo>/../lora-universal-simulator)
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INI = ROOT / "platformio.ini"
LUS_SRC = Path(os.environ.get("MR_LUS_SRC", str(ROOT.parent / "lora-universal-simulator")))

# The probe's nine configuration cells. This module assigns envs to these NAMES; run.sh owns their flag sets and
# their ruled expected values. The two lus cells have no platformio env by construction.
BOARD_CELLS = [
    "board_mobile_oled0", "board_mobile_oled1",
    "board_gateway_oled0", "board_gateway_oled1",
    "board_static_oled0", "board_static_oled1",
]
HOST_CELLS = ["host_native", "host_lus_normal", "host_lus_gateway"]

_n_ok = 0
_n_fail = 0


def check(label: str, cond: bool, detail: str) -> None:
    global _n_ok, _n_fail
    if cond:
        _n_ok += 1
        print(f"  ok   {label} {detail}")
    else:
        _n_fail += 1
        print(f"  FAIL {label} {detail}")


def die(msg: str) -> None:
    print(f"  FAIL E0 envmap could not run: {msg}")
    sys.exit(1)


def resolved_envs() -> dict[str, dict]:
    """PlatformIO's own `extends` resolution — never a hand-rolled ini parser."""
    try:
        proc = subprocess.run(["pio", "project", "config", "--json-output"],
                              cwd=str(ROOT), capture_output=True, text=True, timeout=300)
    except (OSError, subprocess.SubprocessError) as exc:      # C2: no silent skip
        die(f"`pio project config` could not be run ({exc})")
    if proc.returncode != 0:
        die(f"`pio project config` exited {proc.returncode}: {proc.stderr.strip()[:400]}")
    try:
        sections = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        die(f"`pio project config` did not return JSON ({exc})")

    envs: dict[str, dict] = {}
    for name, options in sections:
        if not name.startswith("env:"):
            continue
        opts = dict(options)
        framework = opts.get("framework", [])
        if isinstance(framework, str):
            framework = [framework]
        flags = opts.get("build_flags", [])
        if isinstance(flags, str):
            flags = [flags]
        tokens = " ".join(flags).split()
        envs[name[4:]] = {"framework": list(framework), "tokens": tokens}
    return envs


def defines(tokens: list[str]) -> dict[str, str]:
    out: dict[str, str] = {}
    for tok in tokens:
        if not tok.startswith("-D"):
            continue
        body = tok[2:]
        key, _, value = body.partition("=")
        out[key] = value          # later flags win, exactly as the compiler resolves them
    return out


def cell_of(name: str, env: dict) -> str:
    d = defines(env["tokens"])
    oled = d.get("MR_FEAT_OLED", "0") == "1"
    board = "arduino" in env["framework"]
    if not board:
        return "host_native" if name == "native" else f"host_UNMAPPED:{name}"
    if "MR_PROFILE_MOBILE" in d:
        role = "mobile"
    elif "MR_PROFILE_GATEWAY" in d:
        role = "gateway"
    else:
        role = "static"
    return f"board_{role}_oled{1 if oled else 0}"


def lus_variants() -> dict[str, dict[str, str]]:
    """The simulator's core-lib variants and the compile definitions each adds."""
    cml = LUS_SRC / "CMakeLists.txt"
    if not cml.is_file():
        die(f"simulator CMakeLists not found at {cml} (set MR_LUS_SRC)")
    text = cml.read_text(encoding="utf-8")
    variants: dict[str, dict[str, str]] = {}
    for match in re.finditer(r"^\s*add_meshroute_core_variant\(([^)]*)\)", text, re.M):
        args = match.group(1).split()
        if not args:
            continue
        extra: dict[str, str] = {}
        for arg in args[1:]:
            key, _, value = arg.partition("=")
            extra[key] = value
        variants[args[0]] = extra
    return variants


def main() -> int:
    envs = resolved_envs()
    mapping = {name: cell_of(name, env) for name, env in sorted(envs.items())}
    print(f"   platformio.ini  : {INI}")
    print(f"   simulator source: {LUS_SRC / 'CMakeLists.txt'}")
    for name, cell in mapping.items():
        d = defines(envs[name]["tokens"])
        shown = " ".join(f"{k}={v}" if v else k for k, v in sorted(d.items())
                         if k.startswith(("MR_PROFILE", "MESHROUTE_NATIVE", "MR_GATEWAY_BUILD",
                                          "MR_N_LAYERS", "MR_FEAT_")))
        fw = ",".join(envs[name]["framework"]) or "-"
        print(f"   map  {name:<22s} framework={fw:<8s} -> {cell:<20s} [{shown}]")

    by_cell: dict[str, list[str]] = {}
    for name, cell in mapping.items():
        by_cell.setdefault(cell, []).append(name)

    boards = [n for n, e in envs.items() if "arduino" in e["framework"]]
    hosts = [n for n, e in envs.items() if "arduino" not in e["framework"]]

    check("E1", len(boards) == 13 and all("arduino" in envs[n]["framework"] for n in boards),
          f"every board env declares framework=arduino: {len(boards)} board envs")
    check("E2", sorted(by_cell.get("board_mobile_oled0", []) + by_cell.get("board_mobile_oled1", []))
          == ["heltec_mobile", "heltec_v4_mobile", "xiao_esp32s3_mobile", "xiao_mobile"],
          f"MR_PROFILE_MOBILE envs = 4 {sorted(by_cell.get('board_mobile_oled0', []) + by_cell.get('board_mobile_oled1', []))}")
    check("E3", sorted(by_cell.get("board_gateway_oled0", []) + by_cell.get("board_gateway_oled1", []))
          == ["gateway", "gateway_esp32s3", "gateway_heltec", "gateway_heltec_v4"],
          f"MR_PROFILE_GATEWAY envs = 4 {sorted(by_cell.get('board_gateway_oled0', []) + by_cell.get('board_gateway_oled1', []))}")
    statics = sorted(by_cell.get("board_static_oled0", []) + by_cell.get("board_static_oled1", []))
    check("E4", statics == ["heltec_v3", "heltec_v4", "production", "xiao_esp32s3", "xiao_sx1262"],
          f"no-profile STATIC PRODUCT envs = 5 {statics}")
    native_defs = defines(envs.get("native", {"tokens": []})["tokens"])
    check("E5", hosts == ["native"] and "ARDUINO" not in native_defs
          and not any(k.startswith("MR_PROFILE") for k in native_defs),
          f"the only non-arduino env is `native`, and it defines neither ARDUINO nor MR_PROFILE_*: hosts={hosts}")
    check("E6", not any({"MR_PROFILE_MOBILE", "MR_PROFILE_GATEWAY"} <= set(defines(e["tokens"])) for e in envs.values()),
          "no env sets both MR_PROFILE_MOBILE and MR_PROFILE_GATEWAY")
    oled_envs = sorted(n for n, e in envs.items() if defines(e["tokens"]).get("MR_FEAT_OLED") == "1")
    check("E7", len(oled_envs) == 6 and all(
        f"board_{r}_oled0" in by_cell and f"board_{r}_oled1" in by_cell
        for r in ("mobile", "gateway", "static")),
        f"MR_FEAT_OLED=1 envs = 6 {oled_envs}; all three board roles occur at BOTH OLED values")
    variants = lus_variants()
    check("E8", len(variants) == 2 and not any(
        "ARDUINO" in extra or any(k.startswith("MR_PROFILE") for k in extra) for extra in variants.values()),
        f"simulator declares exactly 2 core variants {sorted(variants)}; neither defines ARDUINO or MR_PROFILE_*")
    gw = variants.get("meshroute_core_gw", {})
    check("E9", gw == {"MESHROUTE_NS": "meshroute_gw", "MR_N_LAYERS": "2", "MR_GATEWAY_BUILD": "1",
                       "MR_CAP_CHANNEL_BUFFER": "8", "MR_CAP_DEFERRED_SENDS": "16"},
          f"lus gateway variant defines exactly {gw} — MR_GATEWAY_BUILD/MR_N_LAYERS WITHOUT ARDUINO")
    check("E10", native_defs.get("MESHROUTE_NATIVE") == "1" and native_defs.get("MR_N_LAYERS") == "2",
          f"native flag set = MESHROUTE_NATIVE={native_defs.get('MESHROUTE_NATIVE')} "
          f"MR_N_LAYERS={native_defs.get('MR_N_LAYERS')}")
    unmapped = [n for n, c in mapping.items() if c.startswith("host_UNMAPPED")]
    covered = set(mapping.values())
    check("E11", not unmapped and set(BOARD_CELLS) <= covered and "host_native" in covered
          and len(mapping) == 14,
          f"all {len(mapping)} envs map into the probe matrix; all 6 board cells claimed; unmapped={unmapped}")
    feat_defines = sorted({k for e in envs.values() for k in defines(e["tokens"]) if k.startswith("MR_FEAT_")})
    check("E13", feat_defines == ["MR_FEAT_OLED"],
          f"the ONLY MR_FEAT_* an env sets directly is MR_FEAT_OLED {feat_defines} — so the probe's per-cell "
          f"projection {{ARDUINO, MR_PROFILE_*, MR_FEAT_OLED, MESHROUTE_NATIVE, MR_N_LAYERS, MR_GATEWAY_BUILD}} "
          f"is COMPLETE, not merely convenient")
    ini_text = INI.read_text(encoding="utf-8")
    check("E12", "MR_FEAT_RADMIN" not in ini_text and "MR_PROFILE_STATIC" not in ini_text,
          "platformio.ini names no MR_FEAT_RADMIN* and no MR_PROFILE_STATIC (R-RA-26: no ini change, no new profile)")

    print(f"envmap: {_n_ok + _n_fail} checks, {_n_fail} failed")
    return 1 if _n_fail else 0


if __name__ == "__main__":
    sys.exit(main())
