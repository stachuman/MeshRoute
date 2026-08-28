#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""Board-ABI `sizeof`/`alignof` of the pinned structs — compile-only, on the envs' OWN toolchains (§B246).

WHY THIS EXISTS
    ⛔⛔ **THE BOARD-ABI COST OF A MODEL FIELD IS INVISIBLE TO EVERY NATIVE GATE.** Measured, §UI-10/11 P3
    (2026-08-26): adding `UiState::compose_gen` (a `uint32_t`) cost **0 on the host** — the 8-byte adapter
    pointers open a 4-byte hole that swallows it — and **+8 on xtensa**, where 4-byte pointers leave no hole.
    The image carried the type twice ⇒ **+16 B of board RAM that no native `sizeof` case, no battery and no
    probe could see**; only the per-board `RAM_used` diff caught it (+336 measured vs +328 claimed).
    D2's native-alignment warning paying out exactly as written.

    Every `sizeof`/`offsetof` case under `test/` measures the HOST ABI and only the host ABI. This probe is the
    board-ABI half of the same pins: it compiles a generated TU with `-c` on each env's REAL toolchain and REAL
    flag set, then reads the sizes back out of the object with that toolchain's own `nm`. ⛔ NO LINK, NO BOARD
    BUILD — seconds, not minutes.

⛔⛔ WHAT THIS PROBE MEASURES, AND WHAT IT DOES **NOT** (QG correction 2026-08-28, and read it before quoting a
    number from it). It measures exactly two things per struct per target: **`sizeof` and `alignof` under that
    env's real flag set**, plus a third, weaker fact — the **T column: whether this env COMPILES the TU the
    type's feature lives in**, decided by the preprocessor on that env's own flags.
    ⛔ THE T COLUMN IS **NOT** A `.bss`/RAM RESIDENCY CLAIM. It does not prove a live instance exists, how many
      there are, or that any of them is static rather than automatic. "This env compiles the feature's TU" is
      strictly weaker than "an instance of this type occupies image RAM", and the probe never asserts the
      latter. A RAM figure still needs `sizeof` FROM HERE multiplied by an instance count established
      elsewhere — by reading the code, or by the per-board `RAM_used` diff, which remains the RAM authority.
    ⓘ FUTURE-OPTIONAL, ⛔ NOT BUILT THIS ROUND (QG ruled the layout half sufficient for [[B246]]): a linker-map
      pass over a real image could turn the T column into a genuine per-symbol `.bss` residency measurement.

★ THE STANDING CHECK (owner ruling 2026-08-28, adopting the offer recorded in [[B246]]). This probe is part of
  the **resources discipline**, beside the native `offsetof`/`sizeof` cases:
    · any slice whose report CLAIMS a struct cost runs it for the TOUCHED structs
          tools/probe_board_abi.py --struct mrui::UiState --struct mrui::UiModel
    · the FULL pinned sweep runs at the gate, with the controls
          tools/probe_board_abi.py
  A report that quotes a board struct SIZE without one of these is quoting the host ABI and calling it the board.
  ⓘ FUTURE-OPTIONAL, ⛔ NOT WIRED HERE: [[B206]]'s build-identity manifest could carry this table per image.
    That is the B246 row's own suggestion and it is deliberately left for a later slice.

★★ THE NEGATIVE CONTROLS RUN BY DEFAULT, DELIBERATELY (`probe_build_identity.py`'s rule, same reasons). Each
   reverts ONE fact — a pin, a probed struct, a derived flag, the divergence fixture — and must turn the probe
   RED. Nothing under `src/`, `lib/` or `test/` is ever written: the generated TU lives in a throwaway temp dir
   and every mutation is applied to a COPY of the derived data.

⛔ A COMPILE FAILURE OF THE PROBE TU IS A **FAILING** VERDICT, NEVER "MEASURED" (the [[B237]] class). There is
   no arm of this tool in which an unbuildable TU yields a size.

⛔ A FILTERED RUN IS NOT A GATE and says so in its own banner ([[B217]]/[[B235]]: a selection that matches
   nothing must be a LOUD REFUSAL, never a quiet "0 problems"). An unknown `--struct`/`--target` exits 8.

USAGE:  tools/probe_board_abi.py                          # full sweep: 3 targets, all pins, all controls
        tools/probe_board_abi.py --no-neg                 # checks only -- NOT a gate, use only while iterating
        tools/probe_board_abi.py --struct mrui::UiState   # touched-struct run (repeatable) -- NOT a gate
        tools/probe_board_abi.py --target gateway         # one target -- NOT a gate
        tools/probe_board_abi.py --repin                  # print a PIN_TABLE block from the current measurement

HOST LIMITATION: Linux/POSIX, and it needs the PlatformIO packages already installed (it invokes no download).
"""

from __future__ import annotations

import argparse
import copy
import json
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


# ★ DERIVED, NEVER HARDCODED: an absolute path baked into a tool measures somebody else's tree the moment it runs
#   from a worktree or a clone ([[B82]] is the same class).
ROOT = Path(__file__).resolve().parents[1]


class ProbeFailure(Exception):
    """A check went RED (raised instead of exiting so a control can catch it)."""


class ProbeRefusal(Exception):
    """The invocation itself is unusable (unknown selector). Exits 8 — never a quiet zero."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ProbeFailure(message)


# ---- the targets ------------------------------------------------------------------------------------------------
# ★ THE TWO BOARD TARGETS ARE THE TWO APPROVED ABIs and nothing else (the 2-env board-gate ruling 2026-08-18):
#   `heltec_mobile` carries the xtensa ESP32-S3 toolchain, `gateway` the ARM `toolchain-gccarmnoneeabi`. Building
#   more envs here would be the same over-reach the board gate already forbids — and would measure nothing new,
#   because an ABI is a property of the toolchain + flag set, not of the board name.
# ⓘ `native` is present as the REFERENCE COLUMN, not as a third ABI: it is what every `test/` case can already
#   see, and printing it beside the boards is the whole point — the divergence is the measurement.
TARGETS: dict[str, str] = {
    "native":        "HOST x86-64 (reference column — this is what test/ measures)",
    "heltec_mobile": "BOARD xtensa ESP32-S3 (toolchain-xtensa-esp-elf)",
    "gateway":       "BOARD ARM Cortex-M4 (toolchain-gccarmnoneeabi)",
}
BOARD_TARGETS = ("heltec_mobile", "gateway")

# The headers the generated TU includes. `mr_features.h` FIRST so `MR_FEAT_*` is derived before anything reads it.
PROBE_HEADERS = (
    "mr_features.h",
    "node.h",
    "device_nv.h",
    "firmware_team_keyring.h",
    "firmware_ui_model.h",
    "firmware_ui_chrome.h",
    "firmware_ui_invite.h",
    "firmware_ui_presets.h",
)

# ---- the pinned set ---------------------------------------------------------------------------------------------
# Each entry: (C++ name, INCLUDED_IF).
#
# ★ `INCLUDED_IF` IS A PREPROCESSOR EXPRESSION EVALUATED INSIDE THE PROBE TU ON THE ENV'S REAL FLAGS — ⛔ NOT a
#   hand-written {env: [structs]} table. It answers ONE narrow question: **"does this env compile the TU this
#   type's feature lives in?"** ⛔ It does NOT answer "is an instance of this type resident in the image", and
#   the T column must never be read that way (see the header). The `sizeof` itself is measured on every target
#   regardless — a type's layout is a fact of the ABI even where the image never instantiates it — so a pin can
#   never go dark just because a profile moved.
#
# ★★ THE `MR_FEAT_OLED` PREDICATE IS NOT ASSERTED, IT IS **CHECKED** — see `check_oled_feature_tu_derivation`:
#    across ALL effective environments, `MR_FEAT_OLED=1` and `firmware_ui.cpp` in `build_src_filter` must agree.
#    Measured 2026-08-28: they agree on all 14 (6 with, 8 without). If a future env breaks that correlation the
#    probe goes RED rather than deciding feature-TU inclusion from a flag that no longer decides it.
#
# ⓘ `mrfw::PresetCatalog` IS `MR_FEAT_OLED` — ★ RE-PINNED 2026-08-28 BY THE [[B255]] SLICE, and this note records
#   the OLD reading so the move is auditable rather than silent. It USED to be `1`, and correctly so: its one
#   instantiation site — the function-local `static mrfw::PresetCatalog cat` in `mrfw::preset_catalog()`,
#   `src/firmware_commands.cpp` — sat in a TU EVERY env compiles behind NO feature guard, which is exactly the
#   waste [[B255]] registered (QG's linked `gateway` ELF: `preset_catalog()::cat` in `.bss` at 0x46c = 1132 B).
#   The owner ruled (b): gate the catalog, the boot restore, the `status`/`help` surfacing and the `ui preset` verb
#   on `MR_FEAT_OLED`. ⇒ the instantiation, its two adapter statics and the accessor now sit inside
#   `#if MR_FEAT_OLED` in that TU, so the honest INCLUSION predicate is the flag. The `sizeof` is still measured on
#   every target (a layout is a fact of the ABI wherever the image instantiates it), so nothing goes dark.
#   ⛔ INCLUSION ONLY, still: what the image actually allocates is the per-board `RAM_used` diff's answer, not this
#      probe's — and for this slice that diff MEASURED −1160 B of `gateway` RAM (see the PIN_TABLE derivation).
PINNED: tuple[tuple[str, str], ...] = (
    # --- always compiled, every env ---
    ("meshroute::Node",        "1"),
    ("mrnv::UiPresetSlot",     "1"),
    ("mrnv::UiPresetBlob",     "1"),
    ("mrfw::SavedKeyEntry",    "1"),
    ("mrfw::SavedKeyList",     "1"),
    # --- the /mrui catalog: its INSTANTIATION + the `ui preset` verb surface are `#if MR_FEAT_OLED` in
    #     src/firmware_commands.cpp ([[B255]], owner ruling (b) 2026-08-28). ★ MOVED OUT of the unconditional group
    #     above by that slice — the OLD pin was `"1"` and the ⓘ ledger above records why it was right until then.
    #     ⓘ A DIFFERENT TU from the `mrui::` block below, same flag: the panel's model lives in firmware_ui.cpp,
    #       the catalog's one instance in firmware_commands.cpp. Both are decided by `MR_FEAT_OLED`.
    ("mrfw::PresetCatalog",    "MR_FEAT_OLED"),
    # --- the OLED panel's model: its feature TU (firmware_ui.cpp) is compiled only where MR_FEAT_OLED is on ---
    ("mrui::UiState",          "MR_FEAT_OLED"),
    ("mrui::UiSnapshot",       "MR_FEAT_OLED"),
    ("mrui::UiModel",          "MR_FEAT_OLED"),
    ("mrui::UiChrome",         "MR_FEAT_OLED"),
    ("mrui::InviteWindow",     "MR_FEAT_OLED"),
    ("mrui::InviteMember",     "MR_FEAT_OLED"),
    ("mrui::InviteGrantResult", "MR_FEAT_OLED"),
    ("mrui::UiProvAnswer",     "MR_FEAT_OLED"),
    ("mrui::UiProvIntent",     "MR_FEAT_OLED"),
    ("mrui::ComposeSlot",      "MR_FEAT_OLED"),
    ("mrui::ComposeList",      "MR_FEAT_OLED"),
    ("mrui::SendReq",          "MR_FEAT_OLED"),
)

# ⓘ COVERED ELSEWHERE — deliberately NOT duplicated here, because each already carries an UNGUARDED
#   `static_assert` in `src/device_nv.h` that EVERY board build compiles, i.e. it is already a per-ABI pin:
#       `mrnv::PeerRec` / `PeerBlob` (:216-217) · `mrnv::JoinProfile` / `JoinBlob` (:270-272)
#       `mrnv::TeamKeyRecord` / `TeamKeyBlob` (:318-320)
#   `meshroute::ChannelReofferPending` is likewise per-ABI asserted at `lib/core/node.h:1982`.
#   `mrnv::UiPresetSlot` / `UiPresetBlob` ARE in the set above even though `device_nv.h:381-386` asserts them:
#   a static_assert only TRIPS, it never reports a number, and the /mrui migration policy is a size comparison.
#
# ⛔⛔ `sizeof(Node)` IS **NOT** COVERED ELSEWHERE — the dispatch brief's premise was wrong and this is the
#    correction. `static_assert(sizeof(Node) == 222008, ...)` at `lib/core/node.h:3528` sits inside the
#    `#ifdef MESHROUTE_NATIVE` opened at `:3523`, so it is compiled by the HOST ONLY; the ledger attached to it
#    says "this line is native-ONLY" ten times over and records that the per-ABI proof was, every single slice,
#    an AD-HOC compile-only reveal on the real toolchains. (The "per-ABI, not native-only" note at `:1968`
#    belongs to the `ChannelReofferPending` assert at `:1982`, not to this one.) ⇒ `meshroute::Node` is pinned
#    here, and this probe is the standing form of a reveal fourteen slices have each rebuilt by hand.

# The divergence sentinel. ★ THE INCIDENT'S EXACT SHAPE: a `uint32_t` beside adapter pointers, landing in a hole
# that exists ONLY where pointers are 8 bytes. Host: `adapter_a`@0(8), `pick`@8, `gen`@12 — inside the pad that
# already ran to the 8-aligned `adapter_b`@16 ⇒ 24 B, and `gen` COSTS ZERO. Board: `adapter_a`@0(4), `pick`@4,
# `gen`@8, `adapter_b`@12 ⇒ 16 B, and `gen` COSTS FOUR. ⇒ the two ABIs MUST disagree, and the probe REQUIRES it:
# a mechanism that cannot see this divergence cannot see the one that cost §UI-10/11 P3 sixteen bytes.
FIXTURE_NAME = "MrAbiDivergenceFixture"
FIXTURE_SOURCE = (
    "struct MrAbiDivergenceFixture {\n"
    "    void*    adapter_a;   // 8 B host / 4 B board\n"
    "    uint8_t  pick;\n"
    "    uint32_t gen;         // the incident's field: free on the host, paid for on the board\n"
    "    void*    adapter_b;\n"
    "};\n"
)

# ---- the pins ---------------------------------------------------------------------------------------------------
# PIN_TABLE[target][struct] = (sizeof, alignof, feature_tu_included)
#
# ★★ THE PIN DISCIPLINE (`probe_ui_model_mutations.py`'s `PIN_CASES`, and [[B217]]'s lesson): these are updated
#    DELIBERATELY, with the old -> new derivation written in place and prior entries kept VISIBLE below. A pin
#    that no longer matches is a LOUD FAILURE, never a silent skip — a silently-disarmed instrument is the worst
#    shape this arc has recorded.
# ⛔ ⚠ A mismatch on a BOARD row with the host row unmoved is exactly the [[B246]] incident. Do not "fix" it by
#    re-pinning until the per-board `RAM_used` diff says the cost is intended.
#
# INITIAL PINS — MEASURED 2026-08-28 on the current tree (§B246 adoption slice; no prior entries exist yet).
# Derivation: `tools/probe_board_abi.py --repin`, i.e. the compile-and-read mechanism this file documents, run
# on the three targets' own `pio run -t idedata` flag sets. Divergences visible in the initial sweep:
#   · mrui::UiModel        host 928 vs board 912  (-16: four adapter pointers, 8 B host / 4 B board)
#   · mrui::UiProvAnswer   host  16 vs board  12  (-4, AND alignof 8 vs 4)
#   · mrfw::PresetCatalog  host 1144 vs board 1132 (-12, alignof 8 vs 4)
#   · meshroute::Node      222008 / 117848 / 148616 — three different feature+layer builds, not one number
#   ⓘ mrui::UiState measures 504 on ALL THREE today. That is NOT a contradiction of [[B246]]: the incident was
#     a DELTA divergence (host +0, xtensa +8) that ended with the two ABIs coinciding at 504. The absolute
#     equality is exactly why a standing probe is needed — the next field may split them again.
#
# ★★ RE-PIN 1 — 2026-08-28, THE [[B255]] SLICE. **ONE STRUCT, THE T COLUMN ONLY; ⛔ NOT ONE `sizeof` OR `alignof`
#    MOVED**, on any of the three targets, and that is the point: gating an INSTANTIATION cannot change a LAYOUT.
#      mrfw::PresetCatalog   native  True -> False   |   heltec_mobile  True -> True   |   gateway  True -> False
#    DERIVATION, and it is the preprocessor's answer rather than a decision taken here: the entry's INCLUDED_IF
#    moved from `"1"` to `"MR_FEAT_OLED"` because the type's one instantiation site moved inside
#    `#if MR_FEAT_OLED` in `src/firmware_commands.cpp` (owner ruling (b) on [[B255]] — a headless board cannot
#    select a preset). `native` and `gateway` declare no `MR_FEAT_OLED=1`, `heltec_mobile` does. ⇒ two columns
#    flip to `-` and the OLED column is unchanged, which is the ruling's shape exactly.
#    ⛔ AND THE T COLUMN IS STILL NOT A RAM CLAIM (the header's standing rule). The RAM authority for this slice is
#    the per-board `RAM_used` diff run with `tools/measure_board.py`, which MEASURED, gateway before -> after:
#        ram_bytes 196196 -> 195036  = **-1160 B**, decomposing EXACTLY as
#          .bss  -1144 = 1132 (`mrfw::preset_catalog()::cat`, the ELF symbol [[B255]] registered) + 12 (the three
#                        function-local statics' 4-B guard variables)
#          .data   -16 =    9 (`::st` 4 + `::gate` 4 + `s_preset_diag` 1) + 7 B of collapsed alignment padding
#      and `heltec_mobile` measured BYTE-IDENTICAL in all 17 allocated sections (the payload sha256 moved only in
#      the 32-B app-descriptor `app_elf_sha256` + the 33-B trailing image hash — the [[B254]] `__LINE__`/DWARF
#      class, `.debug_info` +9 / `.debug_line` -1 and nothing else).
PIN_TABLE: dict[str, dict[str, tuple[int, int, bool]]] = {
    "native": {
        "meshroute::Node":         (222008, 8, True),
        "mrnv::UiPresetSlot":      (21, 1, True),
        "mrnv::UiPresetBlob":      (372, 4, True),
        "mrfw::SavedKeyEntry":     (8, 4, True),
        "mrfw::SavedKeyList":      (36, 4, True),
        "mrfw::PresetCatalog":     (1144, 8, False),   # T: [[B255]] re-pin — native declares no MR_FEAT_OLED=1
        "mrui::UiState":           (504, 8, False),
        "mrui::UiSnapshot":        (1336, 8, False),
        "mrui::UiModel":           (928, 8, False),
        "mrui::UiChrome":          (20, 2, False),
        "mrui::InviteWindow":      (104, 4, False),
        "mrui::InviteMember":      (20, 4, False),
        "mrui::InviteGrantResult": (8, 4, False),
        "mrui::UiProvAnswer":      (16, 8, False),
        "mrui::UiProvIntent":      (32, 4, False),
        "mrui::ComposeSlot":       (20, 1, False),
        "mrui::ComposeList":       (161, 1, False),
        "mrui::SendReq":           (8, 4, False),
        FIXTURE_NAME:              (24, 8, True),
    },
    "heltec_mobile": {
        "meshroute::Node":         (117848, 8, True),
        "mrnv::UiPresetSlot":      (21, 1, True),
        "mrnv::UiPresetBlob":      (372, 4, True),
        "mrfw::SavedKeyEntry":     (8, 4, True),
        "mrfw::SavedKeyList":      (36, 4, True),
        "mrfw::PresetCatalog":     (1132, 4, True),
        "mrui::UiState":           (504, 8, True),
        "mrui::UiSnapshot":        (1336, 8, True),
        "mrui::UiModel":           (912, 8, True),
        "mrui::UiChrome":          (20, 2, True),
        "mrui::InviteWindow":      (104, 4, True),
        "mrui::InviteMember":      (20, 4, True),
        "mrui::InviteGrantResult": (8, 4, True),
        "mrui::UiProvAnswer":      (12, 4, True),
        "mrui::UiProvIntent":      (32, 4, True),
        "mrui::ComposeSlot":       (20, 1, True),
        "mrui::ComposeList":       (161, 1, True),
        "mrui::SendReq":           (8, 4, True),
        FIXTURE_NAME:              (16, 4, True),
    },
    "gateway": {
        # ⓘ `Node` differs from xtensa's by far more than the ABI: `gateway` is MR_PROFILE_GATEWAY
        #   (MR_FEAT_TEAM 0, MR_FEAT_MOBILE 0) with MR_N_LAYERS=2, `heltec_mobile` is MR_PROFILE_MOBILE with
        #   the default single layer. THAT is why the flag derivation has to be real — see control (4).
        "meshroute::Node":         (148616, 8, True),
        "mrnv::UiPresetSlot":      (21, 1, True),
        "mrnv::UiPresetBlob":      (372, 4, True),
        "mrfw::SavedKeyEntry":     (8, 4, True),
        "mrfw::SavedKeyList":      (36, 4, True),
        "mrfw::PresetCatalog":     (1132, 4, False),   # T: [[B255]] re-pin — headless build, the instance is compiled out
        "mrui::UiState":           (504, 8, False),
        "mrui::UiSnapshot":        (1336, 8, False),
        "mrui::UiModel":           (912, 8, False),
        "mrui::UiChrome":          (20, 2, False),
        "mrui::InviteWindow":      (104, 4, False),
        "mrui::InviteMember":      (20, 4, False),
        "mrui::InviteGrantResult": (8, 4, False),
        "mrui::UiProvAnswer":      (12, 4, False),
        "mrui::UiProvIntent":      (32, 4, False),
        "mrui::ComposeSlot":       (20, 1, False),
        "mrui::ComposeList":       (161, 1, False),
        "mrui::SendReq":           (8, 4, False),
        FIXTURE_NAME:              (16, 4, True),
    },
}

# ---- the probe TU -----------------------------------------------------------------------------------------------
SYMBOL_PREFIX = "mr_abi_"

# The number of negative controls a FULL sweep must run. Pinned so the control set cannot shrink unnoticed.
FULL_SWEEP_CONTROLS = 9


def slug(cpp_name: str) -> str:
    return cpp_name.replace("::", "_")


def generate_tu(entries: tuple[tuple[str, str], ...], fixture_source: str = FIXTURE_SOURCE,
                extra: str = "") -> str:
    """The probe TU. `char x[sizeof(T)]` puts the number where a symbol TABLE can read it, so nothing has to run
    on a device and nothing has to be linked. `extra` exists for the controls."""
    lines = ["// GENERATED by tools/probe_board_abi.py -- compile-only, never linked, never written into the tree.",
             "#include <cstddef>", "#include <cstdint>"]
    lines += [f'#include "{header}"' for header in PROBE_HEADERS]
    lines.append(fixture_source.rstrip("\n"))
    for cpp_name, included_if in (*entries, (FIXTURE_NAME, "1")):
        name = slug(cpp_name)
        lines.append(f"char {SYMBOL_PREFIX}size__{name}[sizeof({cpp_name})];")
        lines.append(f"char {SYMBOL_PREFIX}align__{name}[alignof({cpp_name})];")
        if included_if == "1":
            lines.append(f"char {SYMBOL_PREFIX}included__{name}[1];")
        else:
            lines.append(f"#if {included_if}")
            lines.append(f"char {SYMBOL_PREFIX}included__{name}[1];")
            lines.append("#endif")
    if extra:
        lines.append(extra.rstrip("\n"))
    return "\n".join(lines) + "\n"


# ---- the env's own toolchain + flags ------------------------------------------------------------------------------
_IDEDATA_CACHE: dict[str, dict] = {}


def idedata(env: str) -> dict:
    """`pio run -e <env> -t idedata` — PlatformIO's own answer to "what command compiles a TU in this env".

    ★ THIS IS THE FLAG DERIVATION, and it is the whole reason the probe is trustworthy. It resolves `extends`,
      the platform's and board's own defaults, the framework's defines and the arch flags (`-mlongcalls` /
      `-mcpu=cortex-m4 -mfloat-abi=hard`), and it hands back `cxx_path` — the env's PINNED toolchain, not
      whatever `arm-none-eabi-g++` happens to be on PATH. ⛔ A hand-written flag set would read a DIFFERENT
      feature build and the measurement would be a false alarm (node.h's own ledger says exactly this).
    """
    if env in _IDEDATA_CACHE:
        return _IDEDATA_CACHE[env]
    completed = subprocess.run(["pio", "run", "-e", env, "-t", "idedata"],
                               cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    require(completed.returncode == 0,
            f"`pio run -e {env} -t idedata` failed with exit {completed.returncode}: "
            f"{completed.stderr.decode('utf-8', 'replace')[-400:]}")
    text = completed.stdout.decode("utf-8", "replace")
    match = re.search(r"^\{.*\}$", text, re.M)
    require(match is not None, f"no idedata JSON object in the output of `pio run -e {env} -t idedata`")
    data = json.loads(match.group(0))
    for key in ("cxx_path", "cxx_flags", "defines", "includes"):
        require(key in data, f"idedata for env:{env} carries no '{key}'")
    _IDEDATA_CACHE[env] = data
    return data


def compile_command(data: dict, source: Path, obj: Path, defines: list[str] | None = None) -> list[str]:
    """The env's real command, minus everything that only matters to a LINK or a dependency file.

    ⓘ `includes['toolchain']` is deliberately DROPPED: those are the compiler's own system directories, which
      the real build never passes as `-I` either (and on the ESP32 platform the list even names a sibling
      riscv32 toolchain, which poisons `<cmath>` if injected ahead of xtensa's own).
    ⓘ Each flag token is `shlex.split`-ed because idedata emits SHELL-QUOTED entries (the espressif platform
      ships one `"-DCHIP_ADDRESS_RESOLVE_IMPL_INCLUDE_HEADER=<...>"`); passed verbatim as one argv element it
      reads as a filename and the compile fails for a reason that has nothing to do with a struct.
    """
    includes = list(data["includes"]["build"]) + list(data["includes"].get("compatlib", []))
    flags = [token for entry in data["cxx_flags"] for token in shlex.split(entry) if token != "-MMD"]
    macros = data["defines"] if defines is None else defines
    return ([data["cxx_path"], "-c", "-o", str(obj), str(source)]
            + flags + [f"-D{macro}" for macro in macros] + [f"-I{path}" for path in includes])


def binutil(cxx_path: str, tool: str) -> str:
    """The SAME package's binutils. A host `nm` cannot read an xtensa object, and a `nm` from PATH would silently
    be somebody else's toolchain."""
    path = Path(cxx_path)
    for suffix in ("g++", "c++", "gcc", "cc"):
        if path.name.endswith(suffix):
            sibling = path.with_name(path.name[: -len(suffix)] + tool)
            if sibling.exists():
                return str(sibling)
            found = shutil.which(sibling.name)
            if found:
                return found
    found = shutil.which(tool)              # the host target: `/usr/lib/ccache/g++` has no sibling `nm`
    require(found is not None, f"no '{tool}' for compiler {cxx_path}")
    return found


def read_sizes(nm: str, obj: Path) -> dict[str, int]:
    """`nm --print-size --defined-only` -> {symbol: size}. Only `mr_abi_*` symbols are kept."""
    completed = subprocess.run([nm, "--print-size", "--defined-only", str(obj)],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    require(completed.returncode == 0,
            f"`nm` failed on {obj.name}: {completed.stderr.decode('utf-8', 'replace')[:300]}")
    sizes: dict[str, int] = {}
    for line in completed.stdout.decode("utf-8", "replace").splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[3].startswith(SYMBOL_PREFIX):
            sizes[parts[3]] = int(parts[1], 16)
    return sizes


def measure(target: str, tu_text: str, defines: list[str] | None = None) -> dict[str, int]:
    """Compile the generated TU for one target and read the symbol sizes back.

    ⛔ A COMPILE FAILURE RAISES — it is never turned into an absent/zero measurement ([[B237]]).
    """
    data = idedata(target)
    with tempfile.TemporaryDirectory(prefix="probe_board_abi_") as tmp:
        source = Path(tmp) / "mr_abi_probe.cpp"
        source.write_text(tu_text, encoding="utf-8")
        obj = Path(tmp) / f"{target}.o"
        command = compile_command(data, source, obj, defines)
        completed = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        require(completed.returncode == 0,
                f"the probe TU DID NOT COMPILE for target '{target}' (exit {completed.returncode}) — this is a "
                f"FAILING verdict, not a measurement:\n"
                f"{completed.stderr.decode('utf-8', 'replace')[-1200:]}")
        require(obj.is_file(), f"the compile for '{target}' produced no object")
        return read_sizes(binutil(data["cxx_path"], "nm"), obj)


# ---- the checks -------------------------------------------------------------------------------------------------
def check_target(target: str, sizes: dict[str, int], entries: tuple[tuple[str, str], ...],
                 pins: dict[str, dict[str, tuple[int, int, bool]]]) -> int:
    """size / alignof / feature-TU inclusion against the pins, plus BOTH coverage directions. `pins` is a
    PARAMETER so a
    control can mutate a COPY."""
    require(target in pins, f"no pin block for target '{target}'")
    table = pins[target]
    checks = 0
    expected_symbols: set[str] = set()
    for cpp_name, _included_if in (*entries, (FIXTURE_NAME, "1")):
        require(cpp_name in table, f"[{target}] {cpp_name} is probed but NOT PINNED — add it to PIN_TABLE")
        pin_size, pin_align, pin_included = table[cpp_name]
        name = slug(cpp_name)
        size_symbol, align_symbol = f"{SYMBOL_PREFIX}size__{name}", f"{SYMBOL_PREFIX}align__{name}"
        included_symbol = f"{SYMBOL_PREFIX}included__{name}"
        expected_symbols |= {size_symbol, align_symbol}
        if pin_included:
            expected_symbols.add(included_symbol)
        # (a) COVERAGE LOSS IS LOUD: a struct that stopped being probed cannot pass by being absent.
        require(size_symbol in sizes,
                f"[{target}] {cpp_name} is PINNED but the probe TU emitted no size symbol — coverage lost")
        require(align_symbol in sizes,
                f"[{target}] {cpp_name} is PINNED but the probe TU emitted no alignment symbol — coverage lost")
        # (b) the numbers.
        require(sizes[size_symbol] == pin_size,
                f"[{target}] sizeof({cpp_name}) = {sizes[size_symbol]}, pinned {pin_size} "
                f"(delta {sizes[size_symbol] - pin_size:+d})")
        require(sizes[align_symbol] == pin_align,
                f"[{target}] alignof({cpp_name}) = {sizes[align_symbol]}, pinned {pin_align}")
        # (c) feature-TU inclusion, as the env's OWN flags decided it. ⛔ NOT a residency/RAM claim.
        measured_included = included_symbol in sizes
        require(measured_included == pin_included,
                f"[{target}] {cpp_name} feature-TU inclusion is {measured_included}, pinned {pin_included} — "
                f"this env no longer compiles the TU this type's feature lives in the way the pin says")
        checks += 3
    # (d) THE OTHER COVERAGE DIRECTION: nothing measured that no pin governs.
    unknown = sorted(set(sizes) - expected_symbols)
    require(not unknown, f"[{target}] the object carries unpinned probe symbols: {unknown}")
    return checks + 1


def check_fixture_divergence(measured: dict[str, dict[str, int]]) -> int:
    """★ THE PROBE MUST SEE ABI DIVERGENCE, and this is where it proves it rather than asserting it. The fixture
    is the incident's shape; if the host and a board ABI ever measure it the SAME, the mechanism has stopped
    distinguishing them and every green row above means nothing."""
    symbol = f"{SYMBOL_PREFIX}size__{slug(FIXTURE_NAME)}"
    if "native" not in measured:
        return 0            # a `--target`-filtered run has no host column; it is already declared NOT A GATE
    host = measured["native"][symbol]
    checks = 0
    for target in BOARD_TARGETS:
        if target not in measured:
            continue
        require(measured[target][symbol] != host,
                f"the divergence fixture measures {host} on BOTH the host and '{target}' — the probe is not "
                f"distinguishing the ABIs, so it cannot see the [[B246]] class at all")
        checks += 1
    return checks


def check_production_divergence(measured: dict[str, dict[str, int]],
                                entries: tuple[tuple[str, str], ...]) -> tuple[int, list[str]]:
    """The same question asked of the REAL set: at least one pinned production struct must differ host-vs-board.
    ⛔ Not decoration — it is what stops the pinned set drifting into an all-ABI-invariant list that would keep
    passing while measuring nothing anybody needed measured."""
    if "native" not in measured or not any(t in measured for t in BOARD_TARGETS):
        return 0, []
    diverging = []
    for cpp_name, _ in entries:
        symbol = f"{SYMBOL_PREFIX}size__{slug(cpp_name)}"
        host = measured["native"].get(symbol)
        for target in BOARD_TARGETS:
            if target in measured and measured[target].get(symbol) != host:
                diverging.append(cpp_name)
                break
    require(bool(diverging),
            "NO pinned production struct differs between the host and a board ABI — either the set has drifted "
            "to ABI-invariant types or the flag derivation collapsed; a probe with nothing to find is not a gate")
    return 1, diverging


# ---- the feature-TU predicate is DERIVED from the effective configuration, not asserted --------------------------
UI_TU = "firmware_ui.cpp"
OLED_FLAG = "MR_FEAT_OLED=1"


def effective_config() -> list:
    """PlatformIO's EFFECTIVE configuration (`extends` already applied) — the only honest source for
    'which environments exist and what do they compile'."""
    completed = subprocess.run(["pio", "project", "config", "--json-output"],
                               cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    require(completed.returncode == 0,
            f"`pio project config --json-output` failed with exit {completed.returncode}: "
            f"{completed.stderr.decode('utf-8', 'replace')[:300]}")
    return json.loads(completed.stdout.decode("utf-8"))


def oled_environments(config: list) -> dict[str, tuple[bool, bool]]:
    """{env: (declares MR_FEAT_OLED=1, compiles firmware_ui.cpp)} — both DERIVED, neither listed."""
    result: dict[str, tuple[bool, bool]] = {}
    for section, options in config:
        if not section.startswith("env:"):
            continue
        values = dict(options)
        flags = " ".join(values.get("build_flags", []))
        src_filter = " ".join(values.get("build_src_filter", []))
        result[section[len("env:"):]] = (OLED_FLAG in flags, UI_TU in src_filter)
    return result


def check_oled_feature_tu_derivation(config: list) -> int:
    """`MR_FEAT_OLED` is the probe's feature-TU predicate for the whole `mrui::` set. That is only legitimate
    while the flag and the TU those types belong to agree — so the agreement is CHECKED, per environment,
    against the effective configuration. `config` is a PARAMETER so a control can mutate the DERIVED structure."""
    environments = oled_environments(config)
    require(bool(environments), "no env:* sections in the effective PlatformIO configuration")
    for name, (flag, compiles_ui) in sorted(environments.items()):
        require(flag == compiles_ui,
                f"env:{name}: {OLED_FLAG} is {flag} but '{UI_TU}' in build_src_filter is {compiles_ui} — the "
                f"probe's MR_FEAT_OLED predicate no longer decides where the mrui:: model's TU is compiled")
    return len(environments)


# ---- reporting --------------------------------------------------------------------------------------------------
def print_table(measured: dict[str, dict[str, int]], entries: tuple[tuple[str, str], ...],
                targets: list[str]) -> None:
    """Bounded output: one line per struct, all targets side by side, so a divergence is READ not computed."""
    header = f"  {'struct':26s}" + "".join(f"{t:>24s}" for t in targets)
    print(header)
    print("  " + "-" * (len(header) - 2))
    for cpp_name, _ in (*entries, (FIXTURE_NAME, "1")):
        name = slug(cpp_name)
        cells, layouts = [], []
        for target in targets:
            sizes = measured[target]
            layout = f"{sizes.get(f'{SYMBOL_PREFIX}size__{name}')}/{sizes.get(f'{SYMBOL_PREFIX}align__{name}')}"
            included = "T" if f"{SYMBOL_PREFIX}included__{name}" in sizes else "-"
            layouts.append(layout)
            cells.append(f"{layout} {included}")
        # ★ THE MARKER IS ABOUT THE LAYOUT, ⛔ NOT the T column: a struct whose feature TU is simply not compiled
        #   on a gateway has not "diverged", and marking it would drown the four rows that actually did.
        mark = " *" if targets[:1] == ["native"] and any(l != layouts[0] for l in layouts[1:]) else ""
        print(f"  {cpp_name:26s}" + "".join(f"{c:>24s}" for c in cells) + mark)
    print("    size/alignof per target; * = the LAYOUT differs host vs board.")
    print("    T = this env COMPILES the TU this type's feature lives in."
          "  ⛔ NOT a .bss/RAM residency claim -- see the header.")


def repin_block(measured: dict[str, dict[str, int]], entries: tuple[tuple[str, str], ...],
                targets: list[str]) -> str:
    out = ["PIN_TABLE: dict[str, dict[str, tuple[int, int, bool]]] = {"]
    for target in targets:
        out.append(f'    "{target}": {{')
        for cpp_name, _ in (*entries, (FIXTURE_NAME, "1")):
            name = slug(cpp_name)
            sizes = measured[target]
            key = f'"{cpp_name}":'
            out.append(f"        {key:28s} ({sizes[f'{SYMBOL_PREFIX}size__{name}']}, "
                       f"{sizes[f'{SYMBOL_PREFIX}align__{name}']}, "
                       f"{f'{SYMBOL_PREFIX}included__{name}' in sizes}),")
        out.append("    },")
    out.append("}")
    return "\n".join(out)


# ---- negative controls ------------------------------------------------------------------------------------------
def control_fixture_replaced(entries, targets) -> str:
    """(1) The fixture is swapped for an ABI-INVARIANT struct. The divergence requirement must go RED — this is
    what proves the requirement is a measurement and not a tautology."""
    flat = "struct MrAbiDivergenceFixture { uint8_t bytes[24]; };\n"
    tu = generate_tu(entries, fixture_source=flat)
    measured = {t: measure(t, tu) for t in targets}
    check_fixture_divergence(measured)
    return ""


def control_pin_mutated(measured, entries, targets) -> str:
    """(2) One pinned size is moved by 8 — the [[B246]] delta exactly. Pure: no recompile, the pins are a COPY."""
    pins = copy.deepcopy(PIN_TABLE)
    target = targets[-1]
    victim = entries[0][0]
    size, align, included = pins[target][victim]
    pins[target][victim] = (size + 8, align, included)
    check_target(target, measured[target], entries, pins)
    return ""


def control_align_mutated(measured, entries, targets) -> str:
    """(2b) One pinned alignof is moved. `mrui::UiProvAnswer` really does change alignment between the ABIs, so
    the alignment half is not decorative."""
    pins = copy.deepcopy(PIN_TABLE)
    target = targets[-1]
    victim = entries[0][0]
    size, align, included = pins[target][victim]
    pins[target][victim] = (size, align * 2, included)
    check_target(target, measured[target], entries, pins)
    return ""


def control_struct_dropped(entries, targets) -> str:
    """(3) A struct is removed from the probe TU while its pin stays. Coverage loss must be LOUD — the failure
    mode [[B217]] recorded is an instrument that measures nothing and reports no problem."""
    reduced = tuple(e for e in entries if e[0] != entries[0][0])
    tu = generate_tu(reduced)
    target = targets[-1]
    sizes = measure(target, tu)
    check_target(target, sizes, entries, PIN_TABLE)      # still the FULL pin set
    return ""


def control_extra_struct(entries, targets) -> str:
    """(3b) An UNPINNED struct is emitted into the TU. The other coverage direction must be loud too, so a
    quietly-probed type can never accumulate outside the pin table."""
    extra = ("struct MrAbiUnpinnedStray { uint32_t a; };\n"
             f"char {SYMBOL_PREFIX}size__MrAbiUnpinnedStray[sizeof(MrAbiUnpinnedStray)];\n")
    tu = generate_tu(entries, extra=extra)
    target = targets[-1]
    check_target(target, measure(target, tu), entries, PIN_TABLE)
    return ""


def control_profile_flag_dropped(entries, targets) -> str:
    """(4a) THE FLAG DERIVATION IS BROKEN: `gateway` loses `MR_PROFILE_GATEWAY`, so `MR_FEAT_TEAM`/`MR_FEAT_MOBILE`
    default back ON and the feature-gated members of `meshroute::Node` reappear. A probe that measured the wrong
    feature build would report a plausible number for a struct nobody ships — it must FAIL instead."""
    target = "gateway"
    data = idedata(target)
    broken = [d for d in data["defines"] if not d.startswith("MR_PROFILE_GATEWAY")]
    if len(broken) == len(data["defines"]):
        raise AssertionError("MR_PROFILE_GATEWAY is not in the derived defines — the control cannot apply")
    tu = generate_tu(entries)
    check_target(target, measure(target, tu, defines=broken), entries, PIN_TABLE)
    return ""


def control_oled_flag_dropped(entries, targets) -> str:
    """(4b) THE FEATURE-TU PREDICATE IS BROKEN: `heltec_mobile` loses `MR_FEAT_OLED=1`, so the `mrui::` T markers
    vanish. The sizes are unchanged — which is exactly the point: a probe that only compared sizes would stay
    GREEN while reporting that the one board with a panel does not compile the panel's TU."""
    target = "heltec_mobile"
    data = idedata(target)
    broken = [d for d in data["defines"] if not d.startswith("MR_FEAT_OLED")]
    if len(broken) == len(data["defines"]):
        raise AssertionError("MR_FEAT_OLED is not in the derived defines — the control cannot apply")
    tu = generate_tu(entries)
    check_target(target, measure(target, tu, defines=broken), entries, PIN_TABLE)
    return ""


def control_tu_uncompilable(entries, targets) -> str:
    """(5) THE [[B237]] ARM: the TU does not compile. There must be no path in which that becomes a measurement,
    an empty symbol table, or a green 'nothing changed'."""
    tu = generate_tu(entries, extra="char mr_abi_size__NoSuchType[sizeof(mrui::NoSuchTypeAtAll)];\n")
    measure(targets[-1], tu)
    return ""


def control_feature_tu_derivation_broken(config: list) -> str:
    """(4c) The DERIVED configuration is mutated so one env declares `MR_FEAT_OLED=1` without compiling
    `firmware_ui.cpp`. The predicate's own justification must go RED."""
    mutated = copy.deepcopy(config)
    for section in mutated:
        if section[0] == "env:gateway":
            for option in section[1]:
                if option[0] == "build_flags":
                    option[1] = [*option[1], f"-D{OLED_FLAG}"]
                    break
            else:
                section[1].append(["build_flags", [f"-D{OLED_FLAG}"]])
            break
    else:
        raise AssertionError("env:gateway absent from the derived configuration")
    check_oled_feature_tu_derivation(mutated)
    return ""


def control_specs(measured, entries, targets, config) -> list[tuple[str, object, object]]:
    """(name, applicability, thunk) for every control.

    ★ EACH CONTROL DECLARES WHAT IT NEEDS FROM THE SELECTION, and a control whose subject is not in this run is
      reported `n/a` rather than counted GREEN. ⛔ THIS IS NOT A SOFTENED GATE: on the FULL sweep every one of
      them is applicable (`FULL_SWEEP_CONTROLS` is checked), so the gate figure is unchanged. It exists because
      a `--struct`-filtered run has no honest way to fire (4a) if `meshroute::Node` was not selected, and a
      control reported GREEN for the wrong reason teaches a reader to ignore GREEN.
    """
    selected = {name for name, _ in entries}
    both_abis = "native" in targets and any(t in targets for t in BOARD_TARGETS)
    oled_selected = any(included_if != "1" for _, included_if in entries)
    controls = [
        ("(1) the divergence fixture is replaced by an ABI-invariant struct",
         both_abis or "the host column and a board target",
         lambda: control_fixture_replaced(entries, targets)),
        ("(2) a pinned sizeof is moved by 8 (the [[B246]] delta)",
         bool(entries) or "at least one selected struct",
         lambda: control_pin_mutated(measured, entries, targets)),
        ("(2b) a pinned alignof is doubled",
         bool(entries) or "at least one selected struct",
         lambda: control_align_mutated(measured, entries, targets)),
        ("(3) a still-pinned struct is dropped from the probe TU",
         bool(entries) or "at least one selected struct",
         lambda: control_struct_dropped(entries, targets)),
        ("(3b) an unpinned struct is emitted into the probe TU",
         bool(targets) or "at least one target",
         lambda: control_extra_struct(entries, targets)),
        ("(4a) the flag derivation loses MR_PROFILE_GATEWAY (wrong feature build)",
         ("gateway" in targets and "meshroute::Node" in selected)
         or "target gateway + the feature-gated meshroute::Node",
         lambda: control_profile_flag_dropped(entries, targets)),
        ("(4b) the flag derivation loses MR_FEAT_OLED=1 (wrong feature-TU inclusion)",
         ("heltec_mobile" in targets and oled_selected) or "target heltec_mobile + an MR_FEAT_OLED struct",
         lambda: control_oled_flag_dropped(entries, targets)),
        ("(4c) an env declares MR_FEAT_OLED=1 without compiling firmware_ui.cpp",
         True,
         lambda: control_feature_tu_derivation_broken(config)),
        ("(5) the probe TU does not compile -- must be FAILING, never measured",
         bool(targets) or "at least one target",
         lambda: control_tu_uncompilable(entries, targets)),
    ]
    return controls


def run_controls(measured, entries, targets, config) -> tuple[int, int, int]:
    """Returns (red, unusable, applicable). A control that RAISES ProbeFailure is RED; one that returns is a
    probe that did not notice, and one that raises AssertionError could not be applied at all."""
    red = unusable = applicable = 0
    for name, needs, run in control_specs(measured, entries, targets, config):
        if needs is not True:
            print(f"  n/a (this run has no {needs}): {name}")
            continue
        applicable += 1
        try:
            run()
        except ProbeFailure as exc:
            head = str(exc).splitlines()[0]
            print(f"  RED: {name}\n       -> {head[:200]}")
            red += 1
        except AssertionError as exc:
            print(f"  UNUSABLE ({exc}): {name}")
            unusable += 1
        else:
            print(f"  ⛔ GREEN (control did not fire): {name}")
    return red, unusable, applicable


# ---- main -------------------------------------------------------------------------------------------------------
def main() -> None:
    parser = argparse.ArgumentParser(description="Board-ABI sizeof/alignof probe (§B246 standing check).")
    parser.add_argument("--struct", action="append", default=[], metavar="NAME",
                        help="probe only these pinned structs (repeatable) -- a FILTERED run, NOT a gate")
    parser.add_argument("--target", action="append", default=[], metavar="ENV",
                        help=f"probe only these targets ({', '.join(TARGETS)}) -- a FILTERED run, NOT a gate")
    parser.add_argument("--no-neg", action="store_true",
                        help="skip the negative controls -- NOT a gate, use only while iterating")
    parser.add_argument("--repin", action="store_true",
                        help="print a PIN_TABLE block from the current measurement (paste it WITH a derivation)")
    args = parser.parse_args()

    # ⛔ A SELECTION THAT MATCHES NOTHING IS A LOUD REFUSAL, NEVER A QUIET "0 problems" ([[B235]]).
    try:
        known = {name for name, _ in PINNED}
        unknown = [s for s in args.struct if s not in known]
        if unknown:
            raise ProbeRefusal(f"unknown --struct: {unknown}. Pinned: {sorted(known)}")
        unknown_targets = [t for t in args.target if t not in TARGETS]
        if unknown_targets:
            raise ProbeRefusal(f"unknown --target: {unknown_targets}. Known: {sorted(TARGETS)}")
    except ProbeRefusal as exc:
        print(f"REFUSED: {exc}", file=sys.stderr)
        raise SystemExit(8)

    entries = tuple(e for e in PINNED if not args.struct or e[0] in args.struct)
    targets = [t for t in TARGETS if not args.target or t in args.target]
    filtered = bool(args.struct or args.target or args.no_neg)

    try:
        tu = generate_tu(entries)
        measured = {target: measure(target, tu) for target in targets}
        checks = sum(check_target(t, measured[t], entries, PIN_TABLE) for t in targets)
        config = effective_config()
        environments = check_oled_feature_tu_derivation(config)
        checks += environments
        checks += check_fixture_divergence(measured)
        divergence_checks, diverging = check_production_divergence(measured, entries)
        checks += divergence_checks
    except ProbeFailure as exc:
        raise SystemExit(f"FAIL: {exc}")

    print_table(measured, entries, targets)
    print(f"  feature-TU derivation: {environments} effective environments agree on "
          f"{OLED_FLAG} <-> '{UI_TU}' in build_src_filter")
    if diverging:
        print(f"  host/board divergence in the PINNED set: {', '.join(diverging)}")

    if args.repin:
        print("\n" + repin_block(measured, entries, targets))

    if args.no_neg:
        print(f"PASS: board ABI ({checks} checks, CONTROLS SKIPPED -- NOT A GATE)")
        return

    print("negative controls:")
    red, unusable, applicable = run_controls(measured, entries, targets, config)
    if unusable or red != applicable:
        raise SystemExit(f"FAIL: controls {red}/{applicable} RED, {unusable} unusable")
    # ⛔ THE GATE'S CONTROL COUNT CANNOT SHRINK QUIETLY ([[B217]]): on the FULL sweep every control must have
    #   been applicable. If a future edit makes one inapplicable it becomes a visible failure, not a smaller pass.
    if not filtered and applicable != FULL_SWEEP_CONTROLS:
        raise SystemExit(f"FAIL: the full sweep ran {applicable} controls, expected {FULL_SWEEP_CONTROLS} — "
                         f"the control set shrank; that is a disarmed gate, not a pass")
    banner = ("FILTERED RUN -- NOT A GATE: "
              f"structs={args.struct or 'all'} targets={args.target or 'all'}"
              f"{' controls SKIPPED' if args.no_neg else ''}") if filtered else ""
    print(f"PASS: board ABI ({checks} checks, {red}/{applicable} controls RED, 0 unusable)"
          + (f"\n  !!  {banner}" if banner else ""))


if __name__ == "__main__":
    main()
