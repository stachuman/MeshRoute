#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""§B278 S0 — the SYNTHETIC-MIRROR ABI mode: `sizeof`/`alignof`/`offsetof` for a row that is not in the tree yet.

WHY THIS EXISTS, AND WHY IT IS NOT A NEW FLAG DERIVATION
    B278's design §4.1 proposes a 32-byte generalized correlation row and states outright that *"the +64 B is a
    proposal, not an accepted estimate: the ABI probe and both ruled board builds must measure it"*. S0 must
    measure that layout on host/ARM/Xtensa **with zero production change**, and the standing probe cannot do it:

      · `Node::DelegAck` is PRIVATE (`lib/core/node.h`), and `tools/probe_board_abi.py` emits
        `char x[sizeof(T)]` at namespace scope, so it can only name namespace-scope types. Weakening that
        access — or putting the candidate into `node.h` — is exactly the production edit S0 forbids.
      · the standing probe measures `sizeof`/`alignof` only. A 32-byte total says nothing about WHICH packing
        produced it; the spec pins a field SEQUENCE, so the offsets are the measurement that matters.

    ⇒ this file adds a narrowly-named synthetic-mirror mode and ⛔ **derives no flags of its own**: it imports
    `tools/probe_board_abi.py` and reuses that module's `TARGETS`, `idedata()`, `compile_command()`,
    `binutil()`, `read_sizes()` and `measure()` verbatim. There is one PlatformIO flag authority in this repo
    and this is not a second one.

⛔ WHAT A MIRROR IS AND IS NOT. A mirror is a probe-side struct declaring the spec's field sequence. It measures
   THE ABI's answer for that sequence. It does NOT prove the production tree contains such a type — it cannot,
   because in S0 the production tree deliberately does not. The bridge to production is the CONTROL row:
   `MrB278CurrentRowMirror` mirrors the CURRENT `Node::DelegAck` field-for-field, and the generated TU
   `#include`s `node.h`, so node.h's own `static_assert(sizeof(DelegAck) == 24, ...)` is compiled BY EACH
   TOOLCHAIN in the same TU. A mirror that measures 24 beside a production `static_assert` that held on that
   same toolchain is the strongest statement available without touching the tree.

⛔ A COMPILE FAILURE IS A FAILING VERDICT, NEVER "MEASURED" (`probe_board_abi.py`'s rule, [[B237]]'s class).
⛔ A FILTERED RUN IS NOT A GATE and says so in its own banner ([[B217]]/[[B235]]).
⛔ NOTHING under `src/`, `lib/` or `test/` is written: the generated TU lives in a throwaway temp dir.

USAGE:  tools/probe_b278_row_abi.py                  # full sweep: 3 targets, both rows, all controls  (THE GATE)
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

# ---- the two mirrors --------------------------------------------------------------------------------------------
# ★ THE CANDIDATE FIELD SEQUENCE IS TRANSCRIBED FROM THE SPEC, NOT INVENTED HERE:
#   `docs/superpowers/specs/2026-09-01-b278-mobile-custody-feedback-design.md` §4.1, in its stated order.
# ★ THE CONTROL SEQUENCE IS TRANSCRIBED FROM THE CODE (V1), NOT FROM A COMMENT:
#   `lib/core/node.h` `struct DelegAck` — ts_ms / mobile_hash / peer / ctr_h / ctr_m / layer / peer_kind / state.
#   The two enum members are `uint8_t`-based (`enum class DelegAckPeer : uint8_t`,
#   `enum class DelegAckState : uint8_t`), so `uint8_t` is the faithful mirror of their storage.
CANDIDATE_NAME = "MrB278CandidateRow"
CANDIDATE_FIELDS: tuple[tuple[str, str], ...] = (
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
    ("uint8_t",  "obligations"),
)

CURRENT_NAME = "MrB278CurrentRowMirror"
CURRENT_FIELDS: tuple[tuple[str, str], ...] = (
    ("uint64_t", "ts_ms"),
    ("uint32_t", "mobile_hash"),
    ("uint32_t", "peer"),
    ("uint16_t", "ctr_h"),
    ("uint16_t", "ctr_m"),
    ("uint8_t",  "layer"),
    ("uint8_t",  "peer_kind"),
    ("uint8_t",  "state"),
)

MIRRORS: tuple[tuple[str, tuple[tuple[str, str], ...]], ...] = (
    (CANDIDATE_NAME, CANDIDATE_FIELDS),
    (CURRENT_NAME,   CURRENT_FIELDS),
)

# ---- the pins ---------------------------------------------------------------------------------------------------
# ★ THE ONE PIN THAT IS A SPEC OBLIGATION RATHER THAN A MEASUREMENT RECORD: §12-S0's "STOP if ... the 32-byte row
#   is not the measured layout". It is pinned identically on all three targets ON PURPOSE — if any ABI disagrees,
#   S0 STOPs; it does not re-pin.
CANDIDATE_EXPECTED_SIZE = 32
# ★ THE CONTROL PIN: node.h's own live `static_assert(sizeof(DelegAck) == 24, "B251: ...")`, transcribed. If the
#   production assert ever moves, this probe must go RED beside it rather than quietly following.
CURRENT_EXPECTED_SIZE = 24

# The number of negative controls a FULL sweep must run. Pinned so the control set cannot shrink unnoticed.
FULL_SWEEP_CONTROLS = 6


def mirror_source(name: str, fields: tuple[tuple[str, str], ...]) -> str:
    body = "".join(f"    {ty:<9} {nm};\n" for ty, nm in fields)
    return f"struct {name} {{\n{body}}};\n"


def generate_tu(mirrors: tuple[tuple[str, tuple[tuple[str, str], ...]], ...] = MIRRORS,
                extra: str = "", omit_symbols_for: tuple[str, ...] = ()) -> str:
    """The probe TU: the mirrors at namespace scope plus `char` arrays whose SIZES carry the numbers.

    `offsetof` can be 0 and a zero-length array is not a portable symbol, so every offset symbol is
    `offsetof(S, m) + 1` and the reader subtracts one. The `+1` is applied in exactly one place (`read_mirror`).

    ⛔ `node.h` is included through `abi.PROBE_HEADERS`, so the production `static_assert(sizeof(DelegAck) == 24)`
       is compiled BY THIS TOOLCHAIN in this TU. That is the bridge from the control mirror to the real row.
    """
    lines = ["// GENERATED by tools/probe_b278_row_abi.py -- compile-only, never linked, never written into the tree.",
             "#include <cstddef>", "#include <cstdint>"]
    lines += [f'#include "{header}"' for header in abi.PROBE_HEADERS]
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
    """One target, both mirrors. Delegates the compile to `probe_board_abi.measure` (the flag authority)."""
    sizes = abi.measure(target, tu_text if tu_text is not None else generate_tu())
    return {name: read_mirror(sizes, name, fields) for name, fields in MIRRORS}


# ---- the checks -------------------------------------------------------------------------------------------------
def check_target(target: str, measured: dict[str, dict[str, object]],
                 candidate_expected: int = CANDIDATE_EXPECTED_SIZE,
                 current_expected: int = CURRENT_EXPECTED_SIZE) -> list[str]:
    """Return the list of PROBLEMS for one target (empty == clean). Never raises for a measurement mismatch."""
    problems: list[str] = []
    cand = measured[CANDIDATE_NAME]
    cur = measured[CURRENT_NAME]

    if cand["size"] != candidate_expected:
        problems.append(f"[{target}] sizeof({CANDIDATE_NAME}) = {cand['size']}, spec §12-S0 requires "
                        f"{candidate_expected} — S0 STOPs on this, it does not re-pin")
    if cur["size"] != current_expected:
        problems.append(f"[{target}] sizeof({CURRENT_NAME}) = {cur['size']}, node.h's live static_assert says "
                        f"{current_expected} — the control mirror no longer mirrors the production row")

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


def check_ring_projection(measured: dict[str, dict[str, dict[str, object]]]) -> list[str]:
    """The arithmetic the report may quote — LABELLED PROJECTED. ⛔ It is NOT a measured `Node` delta: only a real
    post-refactor `Node` build measures that, which is S1a's obligation (spec §4.1 / the brief's §S0-4)."""
    lines = []
    for target in sorted(measured):
        cand = int(measured[target][CANDIDATE_NAME]["size"])       # type: ignore[arg-type]
        cur = int(measured[target][CURRENT_NAME]["size"])          # type: ignore[arg-type]
        delta = (cand - cur) * 8
        lines.append(f"  {target:<15} ring 8 x {cur} = {cur * 8} B  ->  8 x {cand} = {cand * 8} B   "
                     f"PROJECTED delta {delta:+d} B  (NOT a measured Node RAM figure)")
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
    """(1) a candidate field is DROPPED from the mirror -> the offset symbol vanishes -> coverage lost."""
    target = targets[0]
    reduced = tuple(f for f in CANDIDATE_FIELDS if f[1] != "obligations")
    tu = generate_tu(((CANDIDATE_NAME, reduced), (CURRENT_NAME, CURRENT_FIELDS)))
    sizes = abi.measure(target, tu)
    try:
        read_mirror(sizes, CANDIDATE_NAME, CANDIDATE_FIELDS)
    except ProbeFailure as exc:
        return str(exc)
    return ""


def control_fields_reordered(targets: tuple[str, ...]) -> str:
    """(2) alignment-sensitive fields are REORDERED -> the size grows and/or the sequence check fires.

    `ts_ms` (8-byte) moved behind the trailing `uint8_t` run is the exact shape a careless refactor produces:
    the tail padding it used to absorb becomes interior padding.
    """
    target = targets[0]
    reordered = tuple(f for f in CANDIDATE_FIELDS if f[1] != "ts_ms") + (("uint64_t", "ts_ms"),)
    tu = generate_tu(((CANDIDATE_NAME, reordered), (CURRENT_NAME, CURRENT_FIELDS)))
    sizes = abi.measure(target, tu)
    measured = {CANDIDATE_NAME: read_mirror(sizes, CANDIDATE_NAME, reordered),
                CURRENT_NAME: read_mirror(sizes, CURRENT_NAME, CURRENT_FIELDS)}
    problems = check_target(target, {CANDIDATE_NAME: read_mirror(sizes, CANDIDATE_NAME, CANDIDATE_FIELDS),
                                     CURRENT_NAME: measured[CURRENT_NAME]})
    return problems[0] if problems else ""


def control_size_pin_mutated(measured: dict[str, dict[str, dict[str, object]]],
                             targets: tuple[str, ...]) -> str:
    """(3) the 32-byte spec obligation is moved by 8 -> every target must go RED against the real measurement."""
    target = targets[0]
    problems = check_target(target, measured[target], candidate_expected=CANDIDATE_EXPECTED_SIZE + 8)
    return problems[0] if problems else ""


def control_current_pin_mutated(measured: dict[str, dict[str, dict[str, object]]],
                                targets: tuple[str, ...]) -> str:
    """(3b) the 24-byte production control is moved -> the control mirror must refuse to follow silently."""
    target = targets[0]
    problems = check_target(target, measured[target], current_expected=CURRENT_EXPECTED_SIZE + 8)
    return problems[0] if problems else ""


def control_tu_uncompilable(targets: tuple[str, ...]) -> str:
    """(4) an unbuildable TU must be a FAILING verdict, never a measurement ([[B237]])."""
    target = targets[-1]
    tu = generate_tu(extra="static_assert(sizeof(MrB278CandidateRow) == 999, \"deliberate control failure\");")
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
        ("(1) a candidate field is dropped from the mirror", lambda: control_field_missing(targets)),
        ("(2) alignment-sensitive fields are reordered", lambda: control_fields_reordered(targets)),
        ("(3) the 32-byte spec obligation is moved by 8", lambda: control_size_pin_mutated(measured, targets)),
        ("(3b) the 24-byte production control pin is moved", lambda: control_current_pin_mutated(measured, targets)),
        ("(4) the probe TU does not compile -- must be FAILING, never measured",
         lambda: control_tu_uncompilable(targets)),
        ("(5) a target/toolchain is silently omitted from the sweep",
         lambda: control_target_silently_omitted(targets)),
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
    parser = argparse.ArgumentParser(description="B278 candidate correlation-row ABI mirror (S0 synthetic mode).")
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
    print("\n  ring projection (ARITHMETIC, not a measured Node delta):")
    for line in check_ring_projection(measured):
        print(line)
    print("\n  ⓘ node.h's own `static_assert(sizeof(DelegAck) == 24)` was compiled by EACH target's toolchain in "
          "this probe TU;\n    a green compile is that production row's per-toolchain 24-byte control.")

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
    print(f"\n{banner}PASS: B278 candidate-row ABI mirror ({n_checks} measurements, {red}/{controls} controls RED)")


if __name__ == "__main__":
    main()
