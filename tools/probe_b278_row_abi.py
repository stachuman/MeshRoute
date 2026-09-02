#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""§B278 — the correlation-row ABI mode: `sizeof`/`alignof`/`offsetof` for `Node::DelegAck`, on host/ARM/Xtensa.

WHY THIS EXISTS, AND WHY IT IS NOT A NEW FLAG DERIVATION
    B278's design §4.1 pins a 32-byte correlation row and a FIELD SEQUENCE, and states outright that *"the +64 B is
    a proposal, not an accepted estimate: the ABI probe and both ruled board builds must measure it"*. The standing
    probe cannot do it alone:

      · `Node::DelegAck` is PRIVATE (`lib/core/node.h`), and `tools/probe_board_abi.py` emits
        `char x[sizeof(T)]` at namespace scope, so it can only name namespace-scope types. Weakening that
        access — or hoisting the row out of `Node` — would be a production change made for an instrument.
      · the standing probe measures `sizeof`/`alignof` only. A 32-byte total says nothing about WHICH packing
        produced it; the spec pins a field SEQUENCE, so the offsets are the measurement that matters.

    ⇒ this file adds a narrowly-named mirror mode and ⛔ **derives no flags of its own**: it imports
    `tools/probe_board_abi.py` and reuses that module's `TARGETS`, `idedata()`, `compile_command()`,
    `binutil()`, `read_sizes()` and `measure()` verbatim. There is one PlatformIO flag authority in this repo
    and this is not a second one.

★★ THE BRIDGE, AND WHAT CHANGED AT §B278 S1a. In S0 the 32-byte row did not exist in the tree, so the mirror was
   SYNTHETIC and a second mirror of the then-current 24-byte row carried the tie to production. **S1a landed the row:
   `Node::DelegAck` IS this sequence now, and its own `static_assert(sizeof(DelegAck) == 32, ...)` lives in `node.h`,
   which this probe's generated TU `#include`s.** ⇒ every target's OWN toolchain compiles that production assert in
   the same TU that produces the mirror:

      · the PRODUCTION SIZE PIN is that paired compile — if the real row were not 32 B on a target, that target
        produces **no measurement at all**, because the TU does not compile (control (4)'s rule);
      · the MIRROR supplies the externally printable OFFSETS, which no namespace-scope probe can read off a
        private nested type.

   The 24-byte control mirror is therefore RETIRED (it mirrored a struct that no longer exists), and control (6)
   replaces it by proving the bridge cannot be silently unhooked: drop `node.h` from the include list and the probe
   REFUSES rather than measuring a spec sequence with nothing tying it to the tree.

⛔ A COMPILE FAILURE IS A FAILING VERDICT, NEVER "MEASURED" (`probe_board_abi.py`'s rule, [[B237]]'s class).
⛔ A FILTERED RUN IS NOT A GATE and says so in its own banner ([[B217]]/[[B235]]).
⛔ NOTHING under `src/`, `lib/` or `test/` is written: the generated TU lives in a throwaway temp dir.

USAGE:  tools/probe_b278_row_abi.py                  # full sweep: 3 targets, the production row, all controls  (THE GATE)
        tools/probe_b278_row_abi.py --no-neg         # checks only -- NOT a gate, use only while iterating
        tools/probe_b278_row_abi.py --target gateway # one target -- NOT a gate
        tools/probe_b278_row_abi.py --json           # machine-readable measurement beside the table

HOST LIMITATION: Linux/POSIX, and it needs the PlatformIO packages already installed (it invokes no download).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import probe_board_abi as abi                                    # noqa: E402  (path shim above is deliberate)


ProbeFailure = abi.ProbeFailure
ProbeRefusal = abi.ProbeRefusal
require = abi.require

SYMBOL_PREFIX = "mr_abi_b278_"        # ⚠ MUST start with `probe_board_abi.SYMBOL_PREFIX` — `read_sizes()` keeps
                                      #   only symbols with that prefix, so a private prefix reads back as an
                                      #   EMPTY table (which this file turns into a loud failure, not a zero).

# ---- the mirror ------------------------------------------------------------------------------------------------
# ★ THE FIELD SEQUENCE IS TRANSCRIBED FROM THE CODE (V1), NOT FROM THE SPEC OR A COMMENT: `lib/core/node.h`
#   `struct DelegAck` — ts_ms / mobile_hash / target / return_peer / ctr_h / ctr_m / layer / outward_type /
#   target_kind / return_kind / state / custody_state. It happens to equal spec §4.1's proposed order, and that
#   agreement is a RESULT rather than an input: if the two ever diverge, this mirror follows the CODE and the
#   production `static_assert` compiled beside it is what refuses a size that no longer matches.
#   The four enum members are `uint8_t`-based (`enum class DelegAckPeer : uint8_t`, `DelegAckState : uint8_t`,
#   `DelegAckCustody : uint8_t`), so `uint8_t` is the faithful mirror of their storage.
PRODUCTION_NAME = "MrB278ProductionRowMirror"
PRODUCTION_FIELDS: tuple[tuple[str, str], ...] = (
    ("uint64_t", "ts_ms"),
    ("uint32_t", "mobile_hash"),
    ("uint32_t", "target"),
    ("uint32_t", "return_peer"),
    ("uint16_t", "ctr_h"),
    ("uint16_t", "ctr_m"),
    ("uint8_t",  "layer"),
    ("uint8_t",  "outward_type"),
    ("uint8_t",  "target_kind"),
    ("uint8_t",  "return_kind"),
    ("uint8_t",  "state"),
    ("uint8_t",  "custody_state"),
)

MIRRORS: tuple[tuple[str, tuple[tuple[str, str], ...]], ...] = (
    (PRODUCTION_NAME, PRODUCTION_FIELDS),
)

# The header whose production `static_assert(sizeof(DelegAck) == 32)` IS the size pin. ⛔ Dropping it from the
# generated TU would leave a mirror measuring a sequence with nothing tying it to the tree — control (6).
BRIDGE_HEADER = "node.h"

# ---- the pins ---------------------------------------------------------------------------------------------------
# ★ THE PRODUCTION SIZE, pinned identically on all three targets ON PURPOSE. It is the value node.h's own
#   `static_assert(sizeof(DelegAck) == 32, "B278 S1a: ...")` asserts, transcribed. If the production assert ever
#   moves, this probe must go RED beside it rather than quietly following; if any ABI disagrees, the answer is a
#   STOP finding, not a re-pin.
#   ⓘ MEASURED 2026-09-02 (§B278 S1a): 32 / align 8 with an identical offset sequence on native (host x86-64),
#     heltec_mobile (Xtensa ESP32-S3) and gateway (ARM Cortex-M4). The prior S0 record — a SYNTHETIC 32-byte
#     candidate beside a 24-byte production control — is retired: the candidate is the production row now.
PRODUCTION_EXPECTED_SIZE = 32

# The number of negative controls a FULL sweep must run. Pinned so the control set cannot shrink unnoticed.
# ⓘ 2026-09-02: S0's control (3b) — "the 24-byte production control pin is moved" — RETIRED with the 24-byte
#   mirror it guarded, and control (6) (the bridge header cannot be silently dropped) took its place. Still 6.
FULL_SWEEP_CONTROLS = 6


def mirror_source(name: str, fields: tuple[tuple[str, str], ...]) -> str:
    body = "".join(f"    {ty:<9} {nm};\n" for ty, nm in fields)
    return f"struct {name} {{\n{body}}};\n"


def generate_tu(mirrors: tuple[tuple[str, tuple[tuple[str, str], ...]], ...] = MIRRORS,
                extra: str = "", omit_symbols_for: tuple[str, ...] = (),
                headers: tuple[str, ...] = abi.PROBE_HEADERS) -> str:
    """The probe TU: the mirror at namespace scope plus `char` arrays whose SIZES carry the numbers.

    `offsetof` can be 0 and a zero-length array is not a portable symbol, so every offset symbol is
    `offsetof(S, m) + 1` and the reader subtracts one. The `+1` is applied in exactly one place (`read_mirror`).

    ⛔ `node.h` MUST be in `headers`: the production `static_assert(sizeof(DelegAck) == 32)` compiled BY THIS
       TOOLCHAIN in this TU is the size pin. Without it the mirror measures a field sequence with nothing tying it
       to the tree, which is precisely the silently-disarmed-instrument shape [[B217]] cost this project once.
    """
    require(BRIDGE_HEADER in headers,
            f"the probe TU would not include `{BRIDGE_HEADER}`, so the production "
            f"`static_assert(sizeof(DelegAck) == {PRODUCTION_EXPECTED_SIZE})` would NOT be compiled beside the "
            f"mirror — the measurement would then say nothing about the tree. REFUSED rather than measured")
    lines = ["// GENERATED by tools/probe_b278_row_abi.py -- compile-only, never linked, never written into the tree.",
             "#include <cstddef>", "#include <cstdint>"]
    lines += [f'#include "{header}"' for header in headers]
    for name, fields in mirrors:
        lines.append(mirror_source(name, fields))
    for name, fields in mirrors:
        if name in omit_symbols_for:
            continue
        lines.append(f"char {SYMBOL_PREFIX}size__{name}[sizeof({name})];")
        lines.append(f"char {SYMBOL_PREFIX}align__{name}[alignof({name})];")
        for _ty, member in fields:
            lines.append(f"char {SYMBOL_PREFIX}off__{name}__{member}[offsetof({name}, {member}) + 1];")
    if extra:
        lines.append(extra.rstrip("\n"))
    return "\n".join(lines) + "\n"


def read_mirror(sizes: dict[str, int], name: str,
                fields: tuple[tuple[str, str], ...]) -> dict[str, object]:
    """Turn the raw symbol table into {size, align, offsets{}} — fail loud on any missing symbol."""
    size_sym = f"{SYMBOL_PREFIX}size__{name}"
    align_sym = f"{SYMBOL_PREFIX}align__{name}"
    require(size_sym in sizes, f"the probe TU emitted no size symbol for {name} — coverage lost, not 'zero'")
    require(align_sym in sizes, f"the probe TU emitted no align symbol for {name} — coverage lost, not 'zero'")
    offsets: dict[str, int] = {}
    for _ty, member in fields:
        sym = f"{SYMBOL_PREFIX}off__{name}__{member}"
        require(sym in sizes, f"the probe TU emitted no offset symbol for {name}.{member} — the field is MISSING "
                              f"from the measured layout, which is a FAILING verdict, not a smaller struct")
        offsets[member] = sizes[sym] - 1
    return {"size": sizes[size_sym], "align": sizes[align_sym], "offsets": offsets}


def measure_target(target: str, tu_text: str | None = None) -> dict[str, dict[str, object]]:
    """One target, the production-row mirror. Delegates the compile to `probe_board_abi.measure` (the flag authority).

    ⛔ A target whose real `Node::DelegAck` is not 32 B produces NO measurement here: node.h's own static_assert
       fails inside this same TU and `abi.measure` raises. That refusal IS the production size pin.
    """
    sizes = abi.measure(target, tu_text if tu_text is not None else generate_tu())
    return {name: read_mirror(sizes, name, fields) for name, fields in MIRRORS}


# ---- the checks -------------------------------------------------------------------------------------------------
def check_target(target: str, measured: dict[str, dict[str, object]],
                 production_expected: int = PRODUCTION_EXPECTED_SIZE) -> list[str]:
    """Return the list of PROBLEMS for one target (empty == clean). Never raises for a measurement mismatch."""
    problems: list[str] = []
    row = measured[PRODUCTION_NAME]

    if row["size"] != production_expected:
        problems.append(f"[{target}] sizeof({PRODUCTION_NAME}) = {row['size']}, node.h's live static_assert says "
                        f"{production_expected} — the mirror no longer mirrors the production row (a size/layout "
                        f"departure is a STOP finding, not a re-pin)")

    # Every declared field must have a DISTINCT, in-range offset, and the sequence must be monotonically
    # non-decreasing: a silently reordered mirror would otherwise measure the same 32 bytes and read as a pass.
    for name, fields in MIRRORS:
        entry = measured[name]
        offsets = entry["offsets"]                                # type: ignore[index]
        assert isinstance(offsets, dict)
        seen: dict[int, str] = {}
        previous = -1
        for _ty, member in fields:
            off = offsets[member]
            if off in seen:
                problems.append(f"[{target}] {name}.{member} and {name}.{seen[off]} share offset {off}")
            seen[off] = member
            if off <= previous:
                problems.append(f"[{target}] {name}.{member} is at offset {off}, not after the previous field "
                                f"at {previous} — the declared sequence is not the measured sequence")
            previous = off
            if off >= int(entry["size"]):                         # type: ignore[arg-type]
                problems.append(f"[{target}] {name}.{member} offset {off} is outside sizeof = {entry['size']}")
    return problems


def ring_footprint(measured: dict[str, dict[str, dict[str, object]]]) -> list[str]:
    """The ring's own RAM footprint, per ABI. ⛔ It is NOT a measured `Node` delta and never was: the containing
    object's padding is a separate fact, measured by `tools/probe_board_abi.py`'s `meshroute::Node` pins and by
    the per-board `RAM_used` diff from `tools/measure_board.py pair`."""
    lines = []
    for target in sorted(measured):
        row = int(measured[target][PRODUCTION_NAME]["size"])       # type: ignore[arg-type]
        lines.append(f"  {target:<15} ring 8 x {row} = {row * 8} B    "
                     f"(ARITHMETIC over the measured row -- NOT a measured Node RAM figure)")
    return lines


def print_table(measured: dict[str, dict[str, dict[str, object]]], targets: tuple[str, ...]) -> None:
    for name, fields in MIRRORS:
        print(f"\n  {name}   (field sequence as declared; offsets measured per ABI)")
        header = "  " + "field".ljust(26) + "".join(t.rjust(18) for t in targets)
        print(header)
        print("  " + "-" * (len(header) - 2))
        for ty, member in fields:
            cells = ""
            for target in targets:
                offsets = measured[target][name]["offsets"]        # type: ignore[index]
                assert isinstance(offsets, dict)
                cells += f"{offsets[member]}".rjust(18)
            print("  " + f"{member} ({ty})".ljust(26) + cells)
        cells_size = "".join(f"{measured[t][name]['size']}/{measured[t][name]['align']}".rjust(18) for t in targets)
        print("  " + "sizeof/alignof".ljust(26) + cells_size)


# ---- the negative controls --------------------------------------------------------------------------------------
# Each reverts ONE fact and must turn the probe RED. Every mutation is applied to a COPY of the derived data or to
# a throwaway TU; nothing under `src/`, `lib/` or `test/` is ever written.
def control_field_missing(targets: tuple[str, ...]) -> str:
    """(1) a field is DROPPED from the mirror -> the offset symbol vanishes -> coverage lost, never a smaller struct."""
    target = targets[0]
    reduced = tuple(f for f in PRODUCTION_FIELDS if f[1] != "custody_state")
    tu = generate_tu(((PRODUCTION_NAME, reduced),))
    sizes = abi.measure(target, tu)
    try:
        read_mirror(sizes, PRODUCTION_NAME, PRODUCTION_FIELDS)
    except ProbeFailure as exc:
        return str(exc)
    return ""


def control_fields_reordered(targets: tuple[str, ...]) -> str:
    """(2) alignment-sensitive fields are REORDERED -> the size grows and/or the sequence check fires.

    `ts_ms` (8-byte) moved behind the trailing `uint8_t` run is the exact shape a careless refactor produces:
    the tail padding it used to absorb becomes interior padding.
    """
    target = targets[0]
    reordered = tuple(f for f in PRODUCTION_FIELDS if f[1] != "ts_ms") + (("uint64_t", "ts_ms"),)
    tu = generate_tu(((PRODUCTION_NAME, reordered),))
    sizes = abi.measure(target, tu)
    problems = check_target(target, {PRODUCTION_NAME: read_mirror(sizes, PRODUCTION_NAME, PRODUCTION_FIELDS)})
    return problems[0] if problems else ""


def control_size_pin_mutated(measured: dict[str, dict[str, dict[str, object]]],
                             targets: tuple[str, ...]) -> str:
    """(3) the production 32-byte pin is moved by 8 -> every target must go RED against the real measurement."""
    target = targets[0]
    problems = check_target(target, measured[target], production_expected=PRODUCTION_EXPECTED_SIZE + 8)
    return problems[0] if problems else ""


def control_bridge_header_dropped(_targets: tuple[str, ...]) -> str:
    """(6) `node.h` is dropped from the generated TU -> the production `static_assert` would NOT be compiled.

    ★ THIS CONTROL REPLACES S0's RETIRED 24-BYTE MIRROR, and it guards the same property that mirror guarded:
      that the mirror is TIED TO THE TREE. A mirror compiled without `node.h` still measures a tidy 32 bytes on
      all three ABIs and reads as a clean PASS while proving nothing at all — the [[B217]] silently-disarmed
      shape. The probe must REFUSE to generate that TU, not measure it.
    """
    reduced = tuple(h for h in abi.PROBE_HEADERS if h != BRIDGE_HEADER)
    try:
        generate_tu(headers=reduced)
    except ProbeFailure as exc:
        return str(exc)
    return ""


def control_tu_uncompilable(targets: tuple[str, ...]) -> str:
    """(4) an unbuildable TU must be a FAILING verdict, never a measurement ([[B237]]).

    ⓘ This is also the mechanism the PRODUCTION size pin rides on: if `Node::DelegAck` stopped being 32 B, node.h's
      own `static_assert` would fail in exactly this way and the target would yield no measurement.
    """
    target = targets[-1]
    tu = generate_tu(extra="static_assert(sizeof(MrB278ProductionRowMirror) == 999, \"deliberate control failure\");")
    try:
        abi.measure(target, tu)
    except ProbeFailure as exc:
        return str(exc)
    return ""


def control_target_silently_omitted(targets: tuple[str, ...]) -> str:
    """(5) a target/toolchain is DROPPED from the sweep -> a full run must refuse, never quietly measure fewer.

    This is [[B217]]'s shape aimed at the axis this probe exists for: a 32-byte answer measured on the host
    alone would be precisely the [[B246]] blindness the standing probe was built to end.
    """
    reduced = tuple(t for t in targets if t != "gateway")
    try:
        require_full_sweep(reduced, filtered=False)
    except ProbeRefusal as exc:
        return str(exc)
    return ""


def require_full_sweep(targets: tuple[str, ...], filtered: bool) -> None:
    """A GATE run must carry every target in `abi.TARGETS`. `--target` sets `filtered` and is banner-marked."""
    if filtered:
        return
    missing = [t for t in abi.TARGETS if t not in targets]
    if missing:
        raise ProbeRefusal(f"a full sweep must measure every target; missing {missing} — a host-only or "
                           f"board-partial answer is exactly the [[B246]] blindness this probe exists to end")


def run_controls(measured: dict[str, dict[str, dict[str, object]]],
                 targets: tuple[str, ...]) -> tuple[int, int]:
    specs = [
        ("(1) a field is dropped from the mirror", lambda: control_field_missing(targets)),
        ("(2) alignment-sensitive fields are reordered", lambda: control_fields_reordered(targets)),
        ("(3) the production 32-byte pin is moved by 8", lambda: control_size_pin_mutated(measured, targets)),
        ("(4) the probe TU does not compile -- must be FAILING, never measured",
         lambda: control_tu_uncompilable(targets)),
        ("(5) a target/toolchain is silently omitted from the sweep",
         lambda: control_target_silently_omitted(targets)),
        ("(6) the node.h bridge is dropped, so no production static_assert would be compiled",
         lambda: control_bridge_header_dropped(targets)),
    ]
    red = 0
    print("negative controls:")
    for label, run in specs:
        detail = run()
        if detail:
            red += 1
            print(f"  RED: {label}")
            print(f"       -> {detail.splitlines()[0][:180]}")
        else:
            print(f"  ⛔ NOT RED (the instrument is blind here): {label}")
    return red, len(specs)


def main() -> None:
    parser = argparse.ArgumentParser(description="B278 delegated-flight correlation-row ABI mirror (production mode).")
    parser.add_argument("--target", action="append", metavar="ENV",
                        help="probe only these targets (repeatable) -- a FILTERED run, NOT a gate")
    parser.add_argument("--no-neg", action="store_true",
                        help="skip the negative controls -- NOT a gate, use only while iterating")
    parser.add_argument("--json", action="store_true", help="also print the measurement as JSON")
    args = parser.parse_args()

    filtered = False
    targets = tuple(abi.TARGETS)
    if args.target:
        unknown = [t for t in args.target if t not in abi.TARGETS]
        if unknown:
            print(f"REFUSED: unknown target(s) {unknown}; known: {list(abi.TARGETS)}", file=sys.stderr)
            sys.exit(8)
        targets = tuple(dict.fromkeys(args.target))
        filtered = True

    try:
        require_full_sweep(targets, filtered)
        measured = {target: measure_target(target) for target in targets}
    except (ProbeFailure, ProbeRefusal) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        sys.exit(1)

    print_table(measured, targets)
    print("\n  ring footprint (ARITHMETIC over the measured row, not a measured Node delta):")
    for line in ring_footprint(measured):
        print(line)
    print(f"\n  ⓘ node.h's own `static_assert(sizeof(DelegAck) == {PRODUCTION_EXPECTED_SIZE})` was compiled by EACH "
          f"target's toolchain in this\n    probe TU. A target that produced a measurement above therefore has a "
          f"REAL {PRODUCTION_EXPECTED_SIZE}-byte production row;\n    a target whose row differed would have "
          f"produced no measurement at all (control (4)'s rule).")

    problems: list[str] = []
    for target in targets:
        problems += check_target(target, measured[target])
    for line in problems:
        print(f"  PROBLEM: {line}")

    red = controls = 0
    if not args.no_neg:
        print()
        try:
            red, controls = run_controls(measured, targets)
        except (ProbeFailure, ProbeRefusal) as exc:
            print(f"FAIL: a negative control raised out of band: {exc}", file=sys.stderr)
            sys.exit(1)

    if args.json:
        print("\nJSON " + json.dumps(measured, sort_keys=True))

    gate = (not args.no_neg) and (not filtered)
    banner = "" if gate else "  ⛔ THIS RUN IS NOT A GATE (filtered targets and/or --no-neg).\n"
    if problems:
        print(f"\n{banner}FAIL: {len(problems)} problem(s); {red}/{controls} controls RED")
        sys.exit(1)
    if gate and (red != controls or controls != FULL_SWEEP_CONTROLS):
        print(f"\nFAIL: a full sweep must run {FULL_SWEEP_CONTROLS} controls and turn every one RED; "
              f"got {red}/{controls}")
        sys.exit(1)
    n_checks = sum(2 + len(fields) for _n, fields in MIRRORS) * len(targets)
    print(f"\n{banner}PASS: B278 production correlation-row ABI mirror ({n_checks} measurements, {red}/{controls} controls RED)")


if __name__ == "__main__":
    main()
