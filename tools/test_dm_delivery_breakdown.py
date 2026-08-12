#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""★★★ [[B162b]] 2026-08-09 — DURABLE TESTS FOR THE AIRTIME/DELIVERY INSTRUMENT.

⛔⛔ WHY THIS FILE EXISTS. `tools/dm_delivery_breakdown.py` is the arc's *authority* for delivery and
airtime, and it has twice produced a **self-consistent, entirely wrong answer**: once a BASE total of
**659** with **14 of 36 scenarios reading exactly ZERO**, and once a "fix" for that which **silently
did nothing at all**. Both survived because nothing ever asserted that the instrument could detect
the thing it claimed to detect. A tool trusted as an authority and covered by no test is not an
instrument, it is an opinion with denominators.

★★★ THE RULE EVERY TEST HERE OBEYS: **each assertion is paired with a CONTROL that proves it COULD
have failed.** A test that passes against the broken code is worse than no test — it certifies the
defect. So for each of the four defects the control is either
  · the OLD algorithm re-implemented locally, shown to give the WRONG answer on the same fixture, or
  · a mutated fixture on which the NEW code must produce a different figure.
★ And every count asserted is asserted NONZERO where a zero would be vacuous, plus the refusal
counters — not just the totals. ⚠ `s27`-class lesson: a discriminator returning zero must itself be
controlled.

★★★★ TWO SUITES, REPORTED SEPARATELY — [[B182]]/QG 2026-08-12, AND THIS SPLIT IS ITSELF A FIX.
⛔⛔ THE DEFECT IT REMOVES, found by independent QA in the very suite that validates the fix for this
defect class: the corpus cases depended on `/tmp/<stem>_analyze.ndjson` and printed `SKIPPED` when a
stream was absent — so **hiding every stream left the run reporting ZERO FAILURES and EXITING 0**,
with two structural checks standing in for the whole corpus gate. That is *"a skipped gate looking
passed"* (D3) inside the file whose subject is instruments that fail toward "nothing happened".
  · **SYNTHETIC** — stdlib only, no corpus, no simulator. Always runnable, always the same answer.
  · **CORPUS GATE** — ⛔ REFUSES rather than skips. It does NOT trust whatever is in `/tmp`: it locates
    `lus` and **regenerates the streams it needs itself**, into a private temp dir, so provenance is by
    construction; the `lus` md5 and each stream's md5 + event count are PRINTED beside every figure.
    A missing `lus`, a missing config or a failed run is a **FAILED CHECK**, never a skip.
⇒ The two counts are printed separately and the exit status is non-zero if EITHER fails, so
  "N checks, 0 failed" can never again mean "2 checks ran and the rest were absent".

Run: `python3 tools/test_dm_delivery_breakdown.py`                  (both suites)
     `python3 tools/test_dm_delivery_breakdown.py --synthetic-only`  (stdlib only, no simulator)
"""
import argparse
import ast
import contextlib
import hashlib
import shutil
import subprocess
import io
import json
import os
import re
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dm_delivery_breakdown as T          # noqa: E402

FAILURES = []
CHECKS = [0]
SUITES = []          # [(suite name, checks, failures)] — ★ reported SEPARATELY, never merged


def check(cond, what):
    CHECKS[0] += 1
    if not cond:
        FAILURES.append(what)
        print(f"  FAIL: {what}")
    else:
        print(f"  ok:   {what}")


def write_stream(events, compact=True, extra_raw_lines=()):
    """Write an NDJSON fixture. `compact` chooses the ENCODING ONLY.

    ★★★ [[B162c]] 2026-08-09 — THIS HELPER'S OWN COMMENT USED TO SAY: *"COMPACT separators are
    LOAD-BEARING: the tool fast-paths on the literal substring `\"type\":\"tx\"`, so a pretty-printed
    fixture is invisible to it and every count reads 0."* ⛔⛔ **That was true, and writing it down as a
    fixture requirement DOCUMENTED THE DEFECT AS A FEATURE.** The requirement was never "fixtures must
    be compact"; it was "the instrument must parse its input". Both encodings below are valid NDJSON
    carrying byte-for-byte identical DATA, so the tool MUST return identical figures from either —
    which is what `test_8` now asserts, with a pre-fix control proving it did NOT.

    `extra_raw_lines` are written verbatim (no JSON encoding) so a test can inject a genuinely
    malformed line."""
    fd, path = tempfile.mkstemp(suffix=".ndjson", prefix="b162c-")
    with os.fdopen(fd, "w") as f:
        for e in events:
            if compact:
                f.write(json.dumps(e, separators=(",", ":")) + "\n")
            else:
                # ordinary `json.dumps()` spacing: `{"type": "tx", "time_ms": 1450, ...}`
                f.write(json.dumps(e) + "\n")
        for raw in extra_raw_lines:
            f.write(raw + "\n")
    return path


def tx(node, t_ms, label, nbytes, sf=8, bw_hz=125000, cr=5, airtime_ms=None):
    """A PHY tx event. `airtime_ms` defaults to the formula, so a fixture is self-consistent
    unless a test deliberately makes it disagree."""
    if airtime_ms is None:
        airtime_ms = T.lora_airtime_ms(sf, bw_hz, cr, nbytes)
    return {"type": "tx", "time_ms": t_ms, "node": node, "hex": "aa" * nbytes,
            "airtime_ms": airtime_ms, "sf": sf, "bw_hz": bw_hz, "cr": cr, "label": label}


def emit(slot, t_ms, emit_type, **data):
    return {"type": "script_emit", "node": slot, "time_ms": t_ms,
            "emit_type": emit_type, "data": data}


# --- the OLD algorithm, re-implemented as a CONTROL ------------------------------------------------
# ⓘ This is a deliberate, local duplicate of the code [[B162b]] replaced. It exists ONLY so each test
# can show the old answer differing from the new one on the same bytes. ⛔ Do not "dedupe" it into the
# tool (U1 does not apply: the point is that it is NOT the live path).
def old_index_and_global_phy(path, name_to_id):
    """Returns (index, ambiguous, bw_hz, cr) exactly as the pre-[[B162b]] tool computed them:
    a (node,label,ms)->LENGTH index that marks a duplicate ambiguous ONLY if the lengths differ, and
    ONE global (bw_hz, cr) taken from the FIRST tx event in the file."""
    index, ambiguous = {}, set()
    bw_hz, cr, first = 125000, 5, True
    for line in open(path):
        e = json.loads(line)
        if e.get("type") != "tx":
            continue
        if first:
            bw_hz, cr, first = e.get("bw_hz", bw_hz), e.get("cr", cr), False
        nbytes = len(e["hex"]) // 2
        base = {"RTS-fwd": "RTS", "RTS-rty": "RTS", "CTS-dup": "CTS"}.get(e["label"], e["label"])
        fid = name_to_id.get(e["node"])
        if fid is None:
            continue
        k = (fid, base, e["time_ms"])
        if k in index and index[k] != nbytes:     # ⛔ THE DEFECT: same length => silently collapsed
            ambiguous.add(k)
        index.setdefault(k, nbytes)
    return index, ambiguous, bw_hz, cr


# ==================================================================================================
def test_1_deferred_frame_is_counted():
    """DEFECT 1 — an LBT-DEFERRED frame airs LATER than its emit, so `(node,label,time_ms)` misses and
    the old code booked it as 'never aired (charged 0)'. It had aired. ⇒ every correlation failure
    silently REDUCED the total: a measurement that fails toward 'less airtime'."""
    print("\n[1] deferred-then-aired frame is in the TOTAL, in the unattributed bucket")
    slot_to_id = {0: 11, 1: 12}
    name_to_id = {"A": 11, "B": 12}
    # A emits rts_tx at t=1000; the LBT defers it and the frame actually airs at t=1450.
    # A second RTS (t=2000) airs on time, so the fixture contains BOTH a matched and an orphan frame.
    ev = [emit(0, 1000, "rts_tx", ctr=1, dst=12, next=12),
          tx("A", 1450, "RTS", 10),
          emit(0, 2000, "rts_tx", ctr=2, dst=12, next=12),
          tx("A", 2000, "RTS", 10)]
    path = write_stream(ev)
    try:
        one = T.lora_airtime_ms(8, 125000, 5, 10)
        frames, index, amb, census, xcheck = T.phy_tx_frames(path, name_to_id)
        check(len(frames) == 2, f"both aired frames are in `frames` (got {len(frames)})")
        _, air, stats, _, _, _ = T.analyse_airtime(path, slot_to_id, name_to_id)
        check(air["chargeable_frames"] == 2, "chargeable_frames == 2 (nonzero denominator)")
        check(air["phy_total_ms"] == 2 * one,
              f"TOTAL counts BOTH frames: {air['phy_total_ms']} == 2*{one}")
        check(air["attributed_ms"] == one, f"exactly one frame attributed ({air['attributed_ms']})")
        check(air["unattributed_ms"] == one,
              f"the deferred frame is in `unattributed_ms` ({air['unattributed_ms']}), not lost")
        check(air["unattributed"]["no_matching_emit"] == 1,
              "and it is bucketed as no_matching_emit == 1 (refusal counter, not just the total)")
        check(air["attributed_ms"] + air["unattributed_ms"] == air["phy_total_ms"],
              "reconciliation identity holds")
        check(stats["unaired"] == 1, "the emit-side counter also sees 1 emit with no frame at its ms")
        # ★ CONTROL: the OLD algorithm on the SAME bytes loses that frame entirely.
        old_idx, _, _, _ = old_index_and_global_phy(path, name_to_id)
        old_total = 0
        for t_ms, fid, et, d in T.walk_events(path, slot_to_id):
            n = old_idx.get((fid, "RTS", t_ms))
            if n is not None:
                old_total += T.lora_airtime_ms(8, 125000, 5, n)
        check(old_total == one and old_total != air["phy_total_ms"],
              f"CONTROL: the OLD code totals {old_total} ms, undercounting by {one} ms "
              f"— so this test could have failed and does detect the defect")
    finally:
        os.unlink(path)


# ==================================================================================================
def test_2_mixed_coding_rate():
    """DEFECT 2 — ONE global `bw_hz, cr` was taken from the FIRST tx event and applied to every
    transmission. `s32_dual_cr_gateway` and `s33_mixed_cr_channel_overhear` really do carry CR 4/5
    AND CR 4/8, at BW 250 kHz. Each frame must be priced with ITS OWN sf/bw/cr."""
    print("\n[2] mixed CR / BW / SF: every frame priced at its own PHY parameters")
    slot_to_id = {0: 11, 1: 12}
    name_to_id = {"A": 11, "B": 12}
    # first tx is a BCN at cr=5/bw=250k (so the old global picks that up), then two DATA frames whose
    # own parameters differ from it and from each other.
    ev = [tx("A", 100, "BCN", 20, sf=7, bw_hz=250000, cr=5),
          emit(0, 1000, "data_tx", ctr=1, dst=12, data_sf=7),
          tx("A", 1000, "DATA", 40, sf=7, bw_hz=250000, cr=5),
          emit(1, 2000, "data_tx", ctr=2, dst=11, data_sf=11),
          tx("B", 2000, "DATA", 46, sf=11, bw_hz=250000, cr=8)]
    path = write_stream(ev)
    try:
        want = (T.lora_airtime_ms(7, 250000, 5, 40) + T.lora_airtime_ms(11, 250000, 8, 46))
        _, air, _, _, _, _ = T.analyse_airtime(path, slot_to_id, name_to_id)
        check(air["chargeable_frames"] == 2, "2 chargeable frames (BCN excluded from the DM budget)")
        check(air["phy_params"]["cr"] == [5, 8], f"both CRs seen: {air['phy_params']['cr']}")
        check(air["phy_params"]["bw_hz"] == [250000], "BW read off the wire, not defaulted to 125 kHz")
        check(air["phy_total_ms"] == want, f"total {air['phy_total_ms']} == per-frame sum {want}")
        check(air["xcheck"]["agree"] == 2 and air["xcheck"]["disagree"] == 0,
              "and both agree with the PHY's OWN airtime_ms")
        # ★ CONTROL: price both frames with the ONE global (bw,cr) the old code would have used, and
        #   with the emit-declared SF, and show it gives a DIFFERENT (wrong) answer.
        _, _, g_bw, g_cr = old_index_and_global_phy(path, name_to_id)
        check((g_bw, g_cr) == (250000, 5), f"CONTROL: the old global is {(g_bw, g_cr)} — the BCN's")
        old_total = (T.lora_airtime_ms(7, g_bw, g_cr, 40) + T.lora_airtime_ms(11, g_bw, g_cr, 46))
        check(old_total != want,
              f"CONTROL: one global CR gives {old_total} ms vs the true {want} ms "
              f"(off by {want - old_total} ms) — the test discriminates")
    finally:
        os.unlink(path)


# ==================================================================================================
def test_3_same_length_key_collision():
    """DEFECT 3 — the ambiguity check contradicted its own contract: it marked a duplicate
    `(node,label,time_ms)` key ambiguous ONLY `if index[k] != nbytes`, so TWO SAME-LENGTH
    transmissions in one millisecond collapsed into one, silently. Same length is not same frame."""
    print("\n[3] two SAME-LENGTH frames in one millisecond are ambiguous, and both are counted")
    slot_to_id = {0: 11}
    name_to_id = {"A": 11}
    ev = [emit(0, 5000, "rts_tx", ctr=1, dst=12, next=12),
          tx("A", 5000, "RTS", 10),
          tx("A", 5000, "RTS", 10)]        # same node, same label, same ms, SAME length
    path = write_stream(ev)
    try:
        one = T.lora_airtime_ms(8, 125000, 5, 10)
        frames, index, amb, census, xcheck = T.phy_tx_frames(path, name_to_id)
        check(len(frames) == 2, "both frames are retained in `frames` (the total does not dedupe)")
        check(len(amb) == 1, f"the key IS marked ambiguous (got {len(amb)})")
        check((11, "RTS", 5000) not in index,
              "and it is withheld from the attribution index rather than silently resolved")
        _, air, stats, _, _, n_amb = T.analyse_airtime(path, slot_to_id, name_to_id)
        check(air["phy_total_ms"] == 2 * one, f"TOTAL still counts both ({air['phy_total_ms']})")
        check(air["unattributed"]["ambiguous_key"] == 2,
              "both are bucketed under ambiguous_key — counted, not attributed, not dropped")
        check(n_amb == 1 and stats["emit_refused_ambiguous"] == 1,
              "the REFUSAL counters are nonzero (asserted, not just the total)")
        check(air["attributed_ms"] == 0, "nothing is attributed on an ambiguous key")
        # ★ CONTROL: the OLD rule sees NO ambiguity here at all and would have charged one frame once.
        _, old_amb, _, _ = old_index_and_global_phy(path, name_to_id)
        check(len(old_amb) == 0,
              "CONTROL: the OLD `!= nbytes` rule finds 0 ambiguities on this fixture — it was blind")
        # ★ CONTROL 2: with DIFFERENT lengths the old rule DID fire, proving the fixture, not the rule,
        #   is what changed — i.e. the new rule is a strict superset, not a different check.
        ev2 = [ev[0], ev[1], tx("A", 5000, "RTS", 11)]
        p2 = write_stream(ev2)
        try:
            _, old_amb2, _, _ = old_index_and_global_phy(p2, name_to_id)
            check(len(old_amb2) == 1,
                  "CONTROL: the old rule fires on DIFFERENT lengths, so it was reachable — the gap "
                  "was specifically the same-length case")
            _, amb2 = T.phy_tx_frames(p2, name_to_id)[0:2][0], T.phy_tx_frames(p2, name_to_id)[2]
            check(len(amb2) == 1, "and the new rule also fires there (superset, not a replacement)")
        finally:
            os.unlink(p2)
    finally:
        os.unlink(path)


# ==================================================================================================
def test_4_runtime_id_alias_conflict_is_surfaced():
    """DEFECT 4 — `alias_stats` was computed, returned, and DISCARDED (3 mentions, 0 readers). A
    team/runtime-id collision empties the alias table, so records stop resolving, while
    `unresolved_configured_sends` stays 0 because the send text parsed perfectly. That counter was
    the only warning the authority might be short, and nothing printed it."""
    print("\n[4] a runtime-id alias CONFLICT reaches both the JSON and the text output")
    # two different nodes both claim to wear wire id 83 -> refused, and it must be REPORTED.
    cfg = {"nodes": [{"name": "A", "node_id": 11, "layer_id": 1},
                     {"name": "B", "node_id": 12, "layer_id": 1},
                     {"name": "C", "node_id": 13, "layer_id": 1}]}
    fd, cfg_path = tempfile.mkstemp(suffix=".json", prefix="b162b-cfg-")
    with os.fdopen(fd, "w") as f:
        json.dump(cfg, f)
    ev = [emit(0, 100, "tx_enqueue", origin=83, ctr=1, dst=13),
          emit(1, 200, "tx_enqueue", origin=83, ctr=1, dst=13)]
    path = write_stream(ev)
    try:
        slot_to_id = {0: 11, 1: 12, 2: 13}
        out = T.analyse(path, slot_to_id, {}, {11: 1, 12: 1, 13: 1})
        # ⓘ [[B182]] 2026-08-12: this read `out[-1]`, and analyse() grew two return values (the
        # observed hosting relation + the logical-correlation ledger), so the negative index silently
        # started asserting about the WRONG dict. Indexed by NAME-of-position now: `alias_stats` is
        # element 8. ⚠ A fixture that indexes from the end of a growing tuple is a fixture that will
        # lie again — recorded here rather than just fixed.
        alias_stats = out[8]
        check(alias_stats["refused_conflicts"] == 1,
              f"the conflict is detected (refused_conflicts={alias_stats['refused_conflicts']})")
        totals = T.delivery_totals([], None, path, None, alias_stats)
        check(totals["wire_alias_refused_conflicts"] == 1,
              "★ and it now APPEARS IN THE JSON totals (`wire_alias_refused_conflicts`)")
        check(totals["unresolved_configured_sends"] == 0,
              "⚠ while `unresolved_configured_sends` is 0 — which is exactly why the old output "
              "could look clean while the authority was short")
        # ★ CONTROL: no conflict -> the counter is 0, so a nonzero reading means something.
        ev_ok = [emit(0, 100, "tx_enqueue", origin=83, ctr=1, dst=13)]
        p_ok = write_stream(ev_ok)
        try:
            ok_stats = T.analyse(p_ok, slot_to_id, {}, {11: 1, 12: 1, 13: 1})[8]
            check(ok_stats["refused_conflicts"] == 0 and ok_stats["aliases"] == 1,
                  "CONTROL: one claimant -> 0 refusals and 1 accepted alias, so the counter varies")
        finally:
            os.unlink(p_ok)
        # ★ and the TEXT path must print it. Assert the banner exists in the source's live code, not
        #   in a comment: it is a print() argument.
        src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "dm_delivery_breakdown.py")).read()
        check("RUNTIME-ID ALIAS CONFLICT(S) REFUSED" in src,
              "the text renderer carries the loud banner too")
    finally:
        os.unlink(path)
        os.unlink(cfg_path)


# ==================================================================================================
def test_5_length_formula_agrees_with_the_phy():
    """★ RE-VALIDATION of the length formula against the PHY, with the `len+1` CONTROL — the same
    check the previous pass ran (it reported 7746/7746 with a len+1 control disagreeing on 2943).
    Here it runs on a synthetic multi-SF/multi-CR fixture so it holds with no corpus present; the
    corpus-wide figures are recorded in `simulation/BASELINE.md` §B162."""
    print("\n[5] the LoRa formula reproduces the PHY's own airtime_ms, with a len+1 control")
    name_to_id = {"A": 11}
    ev = []
    t = 0
    for sf in (7, 8, 9, 10, 11):
        for cr in (5, 8):
            for bw in (125000, 250000):
                # `DATA` is variable-length so any byte count is legal; the fixed shapes are added
                # at their ONLY legal lengths (ACK 3, NACK 4, RTS 10) — anything else must raise,
                # which test [6] asserts separately.
                for lab, n in (("DATA", 16), ("DATA", 40), ("DATA", 59),
                               ("ACK", 3), ("NACK", 4), ("RTS", 10)):
                    t += 10
                    ev.append(tx("A", t, lab, n, sf=sf, bw_hz=bw, cr=cr))
    path = write_stream(ev)
    try:
        frames, _, _, _, xcheck = T.phy_tx_frames(path, name_to_id)
        check(xcheck["frames"] == len(ev) and xcheck["frames"] > 0,
              f"every frame was cross-checked ({xcheck['frames']} of {len(ev)}, nonzero)")
        check(xcheck.get("disagree", 0) == 0,
              f"0 disagreements ({xcheck['agree']}/{xcheck['frames']} agree)")
        check(xcheck.get("control_len_plus1_agree", 0) < xcheck["frames"],
              f"CONTROL: a wrong len+1 agrees on only "
              f"{xcheck.get('control_len_plus1_agree', 0)}/{xcheck['frames']} — the agreement above "
              f"is discriminating, not vacuous")
    finally:
        os.unlink(path)


# ==================================================================================================
def test_5b_sf5_sf6_sx126x_case():
    """DEFECT 6 (found by the cross-check in test [5], not by inspection) — this file's Python copy of
    `airtime_ms` was MISSING the SX126x §6.1.4 SF5/SF6 case that `lib/core/airtime.cpp:27-37` has:
    a 6.25-symbol sync offset instead of 4.25, and +36 in the payload numerator instead of +44.
    ⇒ every SF6 frame was mispriced. Corpus-live: 402 of 23913 frames, ALL of them SF6."""
    print("\n[5b] the SX126x SF5/SF6 framing case matches lib/core/airtime.cpp")

    def old_formula(sf, bw_hz, cr, n, preamble_sym=T.PREAMBLE_SYM):
        """The pre-[[B162b]] Python formula — no low-SF case. CONTROL."""
        import math
        t_sym = (2 ** sf) / (bw_hz / 1000.0)
        t_pre = (preamble_sym + 4.25) * t_sym
        de = 1 if t_sym >= 16 else 0
        num = 8 * n - 4 * sf + 44
        den = 4 * (sf - 2 * de)
        return math.floor(t_pre + (8 + max(math.ceil(num / den) * cr, 0)) * t_sym)

    # ★ the anchor is `lib/core/airtime.h:17`'s own worked example: "SF6/len50: 60 -> 61 ms".
    check(T.lora_airtime_ms(6, 125000, 5, 50) == 61,
          f"SF6/BW125/CR5/50B = {T.lora_airtime_ms(6, 125000, 5, 50)} ms — the corrected value named "
          f"in lib/core/airtime.h:17")
    check(old_formula(6, 125000, 5, 50) == 60,
          f"CONTROL: the OLD formula gives {old_formula(6, 125000, 5, 50)} — the 'before' of that same "
          f"worked example, so the test is anchored on a documented pair, not on a self-fulfilling one")
    # ★ SF7-12 must be UNTOUCHED — a fix that also moved the common path would be a regression.
    check(T.lora_airtime_ms(7, 125000, 5, 76) == 146,
          f"SF7/BW125/CR5/76B = {T.lora_airtime_ms(7, 125000, 5, 76)} ms (the pre-existing anchor)")
    same = [(sf, bw, cr, n)
            for sf in (7, 8, 9, 10, 11, 12) for bw in (62500, 125000, 250000)
            for cr in (5, 8) for n in (3, 4, 16, 50, 200)]
    diff = [k for k in same if T.lora_airtime_ms(*k) != old_formula(*k)]
    check(not diff, f"all {len(same)} SF7-12 combinations are BYTE-IDENTICAL to the old formula "
                    f"({len(diff)} differ)")
    low = [(sf, bw, cr, n) for sf in (5, 6) for bw in (62500, 125000, 250000)
           for cr in (5, 8) for n in (3, 4, 16, 50, 200)]
    moved = [k for k in low if T.lora_airtime_ms(*k) != old_formula(*k)]
    check(len(moved) > 0, f"and {len(moved)} of {len(low)} SF5/SF6 combinations DID move (nonzero — a "
                          f"fix that moved nothing would be the [[B162]] 'silently did nothing' shape)")


def test_6_fail_loud_paths():
    """★ C2 — the refusals must actually refuse. Controlled in BOTH directions: legal shapes resolve,
    illegal ones raise, INCLUDING the retired `RTS_LEN = 8`."""
    print("\n[6] fail-loud: illegal shapes, missing hex, missing sf/bw/cr")
    name_to_id = {"A": 11}
    legal = [("RTS", 7), ("RTS", 9), ("RTS", 10), ("RTS", 11), ("RTS", 43),
             ("CTS", 3), ("CTS", 4), ("CTS", 6), ("CTS", 7), ("ACK", 3), ("NACK", 4),
             ("RTS-fwd", 10), ("CTS-dup", 4)]
    ok = 0
    for lab, n in legal:
        try:
            T.frame_shape(lab, n)
            ok += 1
        except T.FrameShapeError as e:
            check(False, f"legal shape {lab}/{n} was refused: {e}")
    check(ok == len(legal), f"{ok}/{len(legal)} legal (label,length) pairs resolve")
    illegal = [("RTS", 8), ("RTS", 12), ("CTS", 5), ("CTS", 8), ("ACK", 4), ("NACK", 3),
               ("MAC", 4), ("RTS", 0), ("ACK", 2)]
    refused = 0
    for lab, n in illegal:
        try:
            T.frame_shape(lab, n)
        except T.FrameShapeError:
            refused += 1
    check(refused == len(illegal),
          f"{refused}/{len(illegal)} illegal pairs RAISE — including the retired RTS_LEN = 8")
    # a tx event with no hex, and one with no sf
    for bad, why in (({"type": "tx", "time_ms": 1, "node": "A", "label": "ACK",
                       "sf": 8, "bw_hz": 125000, "cr": 5}, "missing hex"),
                     ({"type": "tx", "time_ms": 1, "node": "A", "label": "ACK",
                       "hex": "aabbcc", "bw_hz": 125000, "cr": 5}, "missing sf")):
        p = write_stream([bad])
        try:
            try:
                T.phy_tx_frames(p, name_to_id)
                check(False, f"a tx event with {why} was ACCEPTED")
            except T.FrameShapeError:
                check(True, f"a tx event with {why} is REFUSED loudly")
        finally:
            os.unlink(p)


# ==================================================================================================
def test_7_double_attribution_is_refused():
    """★ the inversion's own invariant: no frame may be charged twice. Two emits at the same
    (node,label,ms) must not double-bill the single frame that aired."""
    print("\n[7] two emits claiming one frame: charged once, refusal counted")
    slot_to_id = {0: 11}
    name_to_id = {"A": 11}
    ev = [emit(0, 3000, "rts_tx", ctr=1, dst=12, next=12),
          emit(0, 3000, "rts_retry", ctr=1, dst=12, next=12),
          tx("A", 3000, "RTS", 10)]
    path = write_stream(ev)
    try:
        one = T.lora_airtime_ms(8, 125000, 5, 10)
        _, air, stats, _, _, _ = T.analyse_airtime(path, slot_to_id, name_to_id)
        check(air["phy_total_ms"] == one, "one frame aired, so the total is one frame's airtime")
        check(air["attributed_ms"] == one, "charged exactly once")
        check(stats["emit_double_attribution"] == 1, "the second claim is REFUSED and counted")
        check(air["unattributed_ms"] == 0, "and nothing is stranded")
    finally:
        os.unlink(path)


# --- the PRE-[[B162c]] substring fast paths, re-implemented as a CONTROL --------------------------
# ⓘ Verbatim copies of the two lines this slice deleted (`dm_delivery_breakdown.py` :2576 and :1677 as
# they stood). ⛔ Do not dedupe into the tool — the whole point is that this is NOT the live path.
def _compact_substring_membership_tests(source):
    """Every `'"a":"b"' in X` / `not in X` in `source`, as (lineno, literal).

    ★ Structural, not textual: a docstring or comment that DISCUSSES the deleted code is not a
    membership test, and only the parse tree can tell the difference."""
    found = []
    for n in ast.walk(ast.parse(source)):
        if not isinstance(n, ast.Compare):
            continue
        if not any(isinstance(op, (ast.In, ast.NotIn)) for op in n.ops):
            continue
        lit = n.left
        if (isinstance(lit, ast.Constant) and isinstance(lit.value, str)
                and '":"' in lit.value):
            found.append((n.lineno, lit.value))
    return found


def old_substring_counts(path):
    """(tx-frame count, delivered count) as computed by the pre-fix LITERAL-SUBSTRING filters."""
    frames = sum(1 for line in open(path) if '"type":"tx"' in line)
    delivered = sum(1 for line in open(path) if '"emit_type":"delivered"' in line)
    return frames, delivered


def _ws_fixture():
    """One stream's worth of events: 3 chargeable frames and 2 `delivered` emits."""
    return [emit(0, 1000, "rts_tx", ctr=1, dst=12, next=12),
            tx("A", 1000, "RTS", 10),
            tx("B", 1100, "CTS", 4),
            emit(0, 1200, "data_tx", ctr=1, dst=12, next=12),
            tx("A", 1200, "DATA", 46),
            emit(1, 1300, "delivered", origin=11, ctr=1, dst=12, payload="p1"),
            emit(1, 1400, "delivered", origin=11, ctr=2, dst=12, payload="p2")]


# ==================================================================================================
def test_8_whitespace_independent_parsing():
    """★★★ [[B162c]] THE BLOCKER — VALID NDJSON WITH ORDINARY WHITESPACE PRODUCED A CLEAN ZERO.

    `phy_tx_frames` opened with `if '"type":"tx"' not in line: continue` and
    `raw_delivered_event_count` with `if '"emit_type":"delivered"' in line`. Both are parsers made of
    `str.__contains__`. Against `json.dumps()`'s default spacing they match NOTHING, so the
    AUTHORITATIVE airtime total printed **0 ms over 0 frames** and the delivery cross-check printed
    **0**, as measurements. ⚠ That is [[B162]]'s exact dangerous shape: **the authoritative instrument
    returns a clean zero instead of refusing or parsing.** ★ Third occurrence in this arc — a substring
    fast-path is a silent parser, and a silent parser in a measurement path fails toward "nothing
    happened".

    ★★ THE ASSERTIONS ARE NONZERO ON BOTH ARMS ON PURPOSE: a test that asserted only "compact ==
    pretty" would PASS with both at zero, i.e. it would pass against the very defect it exists to
    catch."""
    print("\n[8] identical data, two encodings: IDENTICAL and NONZERO figures")
    slot_to_id = {0: 11, 1: 12}
    name_to_id = {"A": 11, "B": 12}
    ev = _ws_fixture()
    p_compact = write_stream(ev, compact=True)
    p_pretty = write_stream(ev, compact=False)
    try:
        # the encodings really are different bytes, and the pretty one really has spaces
        raw_c, raw_p = open(p_compact).read(), open(p_pretty).read()
        check(raw_c != raw_p and '"type": "tx"' in raw_p and '"type":"tx"' in raw_c,
              "the two fixtures differ in BYTES and the pretty one carries `\"type\": \"tx\"`")

        got = {}
        for tag, p in (("compact", p_compact), ("pretty", p_pretty)):
            frames, index, amb, census, xcheck = T.phy_tx_frames(p, name_to_id)
            _, air, stats, _, _, _ = T.analyse_airtime(p, slot_to_id, name_to_id)
            got[tag] = {
                "phy_frames":     len(frames),
                "phy_total_ms":   air["phy_total_ms"],
                "chargeable":     air["chargeable_frames"],
                "xcheck_agree":   xcheck["agree"],
                "raw_delivered":  T.raw_delivered_event_count(p),
                "refusals":       T.ndjson_refusals(p)["lines"],
            }
        c, q = got["compact"], got["pretty"]

        # ★ NONZERO first — a zero here would make the equality below vacuous.
        check(c["phy_frames"] == 3 and q["phy_frames"] == 3,
              f"phy_frames NONZERO on both: compact={c['phy_frames']} pretty={q['phy_frames']} (==3)")
        check(c["phy_total_ms"] > 0 and q["phy_total_ms"] > 0,
              f"phy_total_ms NONZERO on both: {c['phy_total_ms']} / {q['phy_total_ms']}")
        check(c["raw_delivered"] == 2 and q["raw_delivered"] == 2,
              f"raw_delivered NONZERO on both: {c['raw_delivered']} / {q['raw_delivered']} (==2)")
        check(c["xcheck_agree"] == 3 and q["xcheck_agree"] == 3,
              f"formula/PHY agreement NONZERO on both: {c['xcheck_agree']} / {q['xcheck_agree']}")
        check(c["refusals"] == 0 and q["refusals"] == 0,
              "and NEITHER encoding produces a parse refusal (whitespace is not corruption)")
        # ★ then IDENTICAL, key by key.
        check(c == q, f"★ EVERY figure identical across encodings: {c} == {q}")

        # ★★ THE PRE-FIX CONTROL: the deleted substring filters DISAGREE between the two encodings.
        fc, dc = old_substring_counts(p_compact)
        fp, dp = old_substring_counts(p_pretty)
        check((fc, dc) == (3, 2), f"CONTROL: pre-fix filters on COMPACT give ({fc},{dc}) == (3,2)")
        check((fp, dp) == (0, 0),
              f"★★ CONTROL: pre-fix filters on PRETTY give ({fp},{dp}) — a CLEAN ZERO from valid "
              f"NDJSON. This is the defect, reproduced.")
        check((fc, dc) != (fp, dp),
              "★★ CONTROL: so the pre-fix code DID differ between the encodings and the fixed code "
              "does not — this test could have failed and detects the defect")

        # ⛔ and the fast path must not come back. ⚠⚠ THIS CHECK IS AST-BASED, AND THAT IS A LESSON
        #    LEARNED IN THE WRITING OF IT: my first version grepped non-`#` lines and FAILED on this
        #    slice's own DOCSTRING, which quotes the deleted code. A line-grep cannot tell live code
        #    from prose about code — the same trap as scoring a fenced-superseded sentence as current.
        #    So look for the SHAPE in the parse tree instead: a membership test whose left operand is a
        #    compact JSON fragment (`{"…":"…"}`-style literal).
        src_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "dm_delivery_breakdown.py")
        offenders = _compact_substring_membership_tests(open(src_path).read())
        check(not offenders,
              f"no LIVE compact-substring membership test remains in the tool (found {offenders})")
        # ★ CONTROL: the detector must actually fire on the code that WAS there.
        control = _compact_substring_membership_tests(
            'for line in f:\n'
            '    if \'"type":"tx"\' not in line:\n'
            '        continue\n'
            '    if \'"emit_type":"delivered"\' in line:\n'
            '        n += 1\n')
        check(len(control) == 2,
              f"CONTROL: the AST detector finds BOTH deleted fast paths in a snippet of the old code "
              f"({control}) — so the assertion above is discriminating, not vacuous")
    finally:
        os.unlink(p_compact)
        os.unlink(p_pretty)


# ==================================================================================================
def test_9_malformed_line_is_refused_counted_and_surfaced():
    """★★ C2 — A LINE THAT DOES NOT PARSE IS REFUSED, COUNTED AND SURFACED, NEVER SILENTLY SKIPPED.

    Every walker used to swallow `JSONDecodeError` with a bare `continue`, so a truncated or corrupt
    stream measured LOW with no indication at all — the same fail-toward-nothing-happened direction as
    the substring filters. ⚠ And per the `alias_stats` lesson (a refusal counter nobody reads is not a
    safeguard) the count must reach BOTH the JSON totals and the TEXT output. The text half is asserted
    by RUNNING THE CLI and reading stdout, not by grepping the source for a string."""
    print("\n[9] a malformed NDJSON line: refused, counted, and printed in both views")
    slot_to_id = {0: 11, 1: 12}
    name_to_id = {"A": 11, "B": 12}
    ev = _ws_fixture()
    bad_lines = ['{"type": "tx", "time_ms": 9999, "node": "A", "hex": "aa',   # truncated
                 '"just a string"']                                          # not an object
    p_bad = write_stream(ev, compact=False, extra_raw_lines=bad_lines)
    p_ok = write_stream(ev, compact=False)
    # a blank and a whitespace-only line must NOT be counted as corruption
    p_blank = write_stream(ev, compact=False, extra_raw_lines=["", "   "])
    cfg = {"nodes": [{"name": "A", "node_id": 11, "layer_id": 1},
                     {"name": "B", "node_id": 12, "layer_id": 1}]}
    fd, cfg_path = tempfile.mkstemp(suffix=".json", prefix="b162c-cfg-")
    with os.fdopen(fd, "w") as f:
        json.dump(cfg, f)
    try:
        ref = T.ndjson_refusals(p_bad)
        check(ref["lines"] == 2, f"both unparseable lines are REFUSED and counted ({ref['lines']})")
        check(len(ref["examples"]) == 2 and ref["examples"][0][0] == 8,
              f"with line numbers and text for the operator: {ref['examples']}")
        # ★ the valid lines are still measured — refusal is PER LINE, not a whole-file give-up.
        frames, _, _, _, _ = T.phy_tx_frames(p_bad, name_to_id)
        check(len(frames) == 3,
              f"the 3 VALID frames are still measured alongside the refusals ({len(frames)})")
        # ★ JSON surfacing
        totals = T.delivery_totals([], None, p_bad, None, None)
        check(totals["malformed_ndjson_lines"] == 2,
              "★ the count reaches the JSON totals (`malformed_ndjson_lines`)")
        check(len(totals["malformed_ndjson_examples"]) == 2,
              "with examples, so an operator can see WHICH lines were dropped")
        # ★ CONTROL: the clean stream reads 0 and the key is still PRESENT (a zero must be printable).
        clean = T.delivery_totals([], None, p_ok, None, None)
        check(clean["malformed_ndjson_lines"] == 0 and "malformed_ndjson_lines" in clean,
              "CONTROL: a clean stream reads 0 and the key is still present, so the counter varies")
        check(T.ndjson_refusals(p_blank)["lines"] == 0,
              "CONTROL: blank / whitespace-only lines are NOT refusals (a trailing newline is not "
              "corruption)")
        # ★★ TEXT surfacing — run the CLI for real and read stdout.
        tool = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "dm_delivery_breakdown.py")
        import subprocess
        r_bad = subprocess.run([sys.executable, tool, cfg_path, p_bad, "--mode", "dm"],
                               capture_output=True, text=True)
        r_ok = subprocess.run([sys.executable, tool, cfg_path, p_ok, "--mode", "dm"],
                              capture_output=True, text=True)
        check(r_bad.returncode == 0 and r_ok.returncode == 0,
              f"both CLI runs succeed (rc {r_bad.returncode}/{r_ok.returncode})")
        check("NDJSON LINE(S) REFUSED" in r_bad.stdout and "LOWER BOUND" in r_bad.stdout,
              "★ the TEXT output announces the refusal and says every figure is a LOWER BOUND")
        check("NDJSON LINE(S) REFUSED" not in r_ok.stdout,
              "★ CONTROL: the clean stream prints NO such banner, so the banner is discriminating")
    finally:
        for p in (p_bad, p_ok, p_blank, cfg_path):
            os.unlink(p)


# ==================================================================================================
# ★★★ [[B162d]] — THE TOPOLOGY CHECKERS. Both take SOURCE TEXT so the real tool and a deliberately
# broken control snippet can be run through the SAME code. ⚠ AST, never grep: [[B162c]]'s own line
# grep for the deleted fast paths FAILED ON ITS OWN DOCSTRING, and "no live path does X" is a
# structural question about the parse tree, not a textual one about the characters.
_CROSSING = "ndjson_refusal_crossing_point"


def _find_fn(source, name):
    for n in ast.walk(ast.parse(source)):
        if isinstance(n, ast.FunctionDef) and n.name == name:
            return n
    return None


def _output_before_refusal_crossing(source, fn_name="main"):
    """Offenders: output produced in `fn_name` at or before the refusal crossing point.

    "Output" = a `print(...)`, an `emit_json(...)`, any `render_*(...)` call, or a `return` — i.e.
    every way a mode can finish. ★ The proposition being proved is TOPOLOGICAL: the crossing call is
    a direct, unconditional statement of the function body and nothing that emits or returns precedes
    it, so a NEW mode added anywhere below inherits the guarantee without its author knowing it
    exists. ⛔ That is the property a per-mode banner copy cannot have."""
    fn = _find_fn(source, fn_name)
    if fn is None:
        return [(0, f"no function named {fn_name}")]
    direct = [s for s in fn.body
              if isinstance(s, ast.Expr) and isinstance(s.value, ast.Call)
              and isinstance(s.value.func, ast.Name) and s.value.func.id == _CROSSING]
    every = [n for n in ast.walk(fn)
             if isinstance(n, ast.Call) and isinstance(n.func, ast.Name)
             and n.func.id == _CROSSING]
    bad = []
    if len(direct) != 1:
        bad.append((fn.lineno, f"{len(direct)} unconditional {_CROSSING}() statements in "
                               f"{fn_name}() — must be exactly 1"))
    # ⛔ A call nested inside a branch is a PER-MODE COPY, which is the defect in a new shape: the
    #    guarantee then holds only for the modes whose author remembered it.
    direct_lines = {s.lineno for s in direct}
    for n in every:
        if n.lineno not in direct_lines:
            bad.append((n.lineno, f"per-mode copy of the crossing point (nested in a branch)"))
    if len(direct) != 1:
        return bad
    at = direct[0].lineno
    for n in ast.walk(fn):
        if isinstance(n, ast.Return) and n.lineno < at:
            bad.append((n.lineno, "return before the crossing point"))
        if isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.lineno < at:
            f = n.func.id
            if f == "print" or f == "emit_json" or f.startswith("render_"):
                bad.append((n.lineno, f"{f}() before the crossing point"))
    return bad


def _stdout_json_dumps_outside(source, sink="emit_json"):
    """Offenders: `json.dump(..., sys.stdout, ...)` calls NOT lexically inside `sink`.

    ★ The JSON half of the guarantee: `emit_json` attaches the refusal census, so if it is the ONLY
    writer of JSON to stdout then NO payload can be emitted without it — including one added by a
    future mode. ⛔ `--airtime --json` and `--mode channel --json` each had their own `json.dump`,
    and each therefore had its own private, censusless contract."""
    tree = ast.parse(source)
    inside = set()
    for fn in ast.walk(tree):
        if isinstance(fn, ast.FunctionDef) and fn.name == sink:
            for n in ast.walk(fn):
                inside.add(id(n))
    bad = []
    for n in ast.walk(tree):
        if not (isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute)
                and n.func.attr == "dump"):
            continue
        writes_stdout = any(isinstance(a, ast.Attribute) and a.attr == "stdout" for a in n.args)
        if writes_stdout and id(n) not in inside:
            bad.append((n.lineno, ast.unparse(n.func)))
    return bad


def test_10_the_refusal_promise_is_enforced_by_topology():
    """★★★ [[B162d]] THE BLOCKER — THE BANNER EXISTED AND THE AUTHORITATIVE RENDERER BYPASSED IT.

    [[B162c]] printed the refusal notice inside the DM/channel TEXT renderer. `main()` dispatched to
    five other outputs that `return`ed before it (`--trace`, `--copies`, `--airtime`, `--tail`) or
    built their payload inline without it (`--mode channel --json`). ⇒ on a stream with a corrupt
    line, `--airtime` printed `★★ TOTAL AIRTIME (AUTHORITY) = 346 ms` with rc 0 and NO banner, and
    `--airtime --json` carried neither the count nor the examples.

    ★★★ FOURTH OCCURRENCE OF ONE SHAPE IN THIS ARC, SECOND INSIDE [[B162]]: a measurement path that
    fails toward "nothing happened". THE STRUCTURAL LESSON: **a fail-loud promise made at ONE exit is
    not made at all when there are FIVE.** So this test does not check that each mode prints a
    banner — `test_11` does that, and it would still pass on a build where a sixth mode was added
    tomorrow. It checks the TOPOLOGY that makes the sixth mode safe."""
    print("\n[10] the refusal notice is unbypassable BY CONSTRUCTION (AST)")
    src_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "dm_delivery_breakdown.py")
    src = open(src_path).read()

    off = _output_before_refusal_crossing(src)
    check(not off, f"main(): NOTHING emits or returns before the single {_CROSSING}() "
                   f"statement (offenders {off})")
    # ★ CONTROL: the checker must fire on the pre-fix topology — the notice after the dispatch.
    ctl_pre = _output_before_refusal_crossing(
        "def main():\n"
        "    args = p.parse_args()\n"
        "    if args.airtime:\n"
        "        render_airtime(args.events)\n"
        "        return\n"
        f"    {_CROSSING}(args)\n"
        "    render_table(rows)\n")
    check(len(ctl_pre) == 2,
          f"★★ CONTROL: on the PRE-FIX shape (notice AFTER the dispatch) the checker reports the "
          f"render_airtime call AND the early return ({ctl_pre}) — so it discriminates")
    # ★ CONTROL: a per-mode copy is not a crossing point, even when EVERY current mode has one.
    ctl_copy = _output_before_refusal_crossing(
        "def main():\n"
        "    if args.airtime:\n"
        f"        {_CROSSING}(args)\n"
        "        render_airtime(args.events)\n"
        "        return\n"
        "    if args.tail:\n"
        f"        {_CROSSING}(args)\n"
        "        render_tail(args.events)\n"
        "        return\n")
    msgs = " | ".join(m for _, m in ctl_copy)
    check("must be exactly 1" in msgs and msgs.count("per-mode copy") == 2,
          f"★★ CONTROL: the PER-MODE COPY shape is REJECTED even with EVERY mode covered — a banner "
          f"repeated per mode is not a crossing point, it is the same defect in a new shape "
          f"({ctl_copy})")

    off2 = _stdout_json_dumps_outside(src)
    check(not off2, f"no `json.dump(..., sys.stdout)` outside emit_json() — one JSON sink, so every "
                    f"payload carries the census (offenders {off2})")
    # ★ CONTROL: the detector fires on the two private dumps that WERE there.
    ctl_json = _stdout_json_dumps_outside(
        "def render_airtime(x):\n"
        "    json.dump({'airtime': air}, sys.stdout, indent=2)\n"
        "def main():\n"
        "    json.dump(payload, sys.stdout, indent=2)\n"
        "def emit_json(payload, events_path):\n"
        "    json.dump(payload, sys.stdout, indent=2)\n")
    check(len(ctl_json) == 2,
          f"★★ CONTROL: the detector finds BOTH pre-fix private dumps and NOT the one inside "
          f"emit_json ({ctl_json}) — so the assertion above is discriminating")

    # ★ and emit_json really does attach the keys, with a zero PRESENT on clean input.
    p_ok = write_stream(_ws_fixture(), compact=False)
    p_bad = write_stream(_ws_fixture(), compact=False,
                         extra_raw_lines=['{"type": "tx", "hex": "aa', '"a string"'])
    try:
        got = {}
        for tag, p in (("clean", p_ok), ("bad", p_bad)):
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                T.emit_json({"probe": 1}, p)
            got[tag] = json.loads(buf.getvalue())
        check(got["clean"]["malformed_ndjson_lines"] == 0
              and got["clean"]["malformed_ndjson_examples"] == []
              and got["clean"]["measurement_is_lower_bound"] is False,
              "emit_json(): a CLEAN stream emits the keys with 0 / [] / false — ⚠ an absent key is "
              "indistinguishable from a zero to a consumer, and this tool is the authority")
        check(got["bad"]["malformed_ndjson_lines"] == 2
              and len(got["bad"]["malformed_ndjson_examples"]) == 2
              and got["bad"]["measurement_is_lower_bound"] is True,
              f"CONTROL: and a corrupt stream moves them to 2 / 2 examples / true "
              f"({got['bad']['malformed_ndjson_lines']}) — so the keys are not constants")
        check(got["clean"]["probe"] == 1 and got["bad"]["probe"] == 1,
              "and the caller's own payload is passed through untouched")
    finally:
        os.unlink(p_ok)
        os.unlink(p_bad)


# ==================================================================================================
def _copies_fixture():
    """`_ws_fixture()` plus a decode and a copy-creating switch, so `--copies` has a NONZERO figure.

    ⚠ Deliberately a SUPERSET rather than an edit of `_ws_fixture()`: `tx_enqueue` / `data_rx` /
    `path_cascade` are not chargeable PHY tx frames, so the airtime figures are unchanged
    (346 ms / 3 frames) and `test_8`'s fixture is left alone."""
    return [emit(0, 900, "tx_enqueue", origin=11, ctr=1, dst=12, payload="p1")] \
        + _ws_fixture() + [
        emit(1, 1250, "data_rx", origin=11, ctr=1, dst=12, payload="p1"),
        emit(0, 1260, "path_cascade", origin=11, ctr=1, from_next=12, to_next=13,
             trigger="stale", payload="p1")]


def _cli(cfg_path, events_path, extra):
    import subprocess
    tool = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "dm_delivery_breakdown.py")
    return subprocess.run([sys.executable, tool, cfg_path, events_path] + list(extra),
                          capture_output=True, text=True)


def _in_process(argv, patches):
    """Run `T.main()` in-process with module globals replaced — the PRE-FIX CONTROL vehicle.

    ★ Patching out `ndjson_refusal_crossing_point` (to a no-op) and `emit_json` (to a bare
    `json.dump`) reconstitutes EXACTLY the pre-fix topology: the banner then exists only inside the
    DM/channel text renderer, and each JSON payload is emitted with no census. That is how this test
    proves it could have failed, without keeping a copy of the old file."""
    buf = io.StringIO()
    saved = {k: getattr(T, k) for k in patches}
    old_argv = sys.argv
    try:
        for k, v in patches.items():
            setattr(T, k, v)
        sys.argv = ["dm_delivery_breakdown.py"] + list(argv)
        with contextlib.redirect_stdout(buf):
            try:
                T.main()
            except SystemExit:
                pass
        return buf.getvalue()
    finally:
        sys.argv = old_argv
        for k, v in saved.items():
            setattr(T, k, v)


def _nonzero_int_after(text, marker, before=None):
    """The first integer following `marker` (up to `before`), or None. Used for NONZERO probes."""
    i = text.find(marker)
    if i < 0:
        return None
    seg = text[i + len(marker):]
    if before:
        j = seg.find(before)
        seg = seg[:j] if j >= 0 else seg
    m = re.search(r"-?\d+", seg)
    return int(m.group(0)) if m else None


def test_11_every_mode_announces_refusals_beside_a_nonzero_figure():
    """★★ [[B162d]] CLI REGRESSIONS FOR ALL SIX OUTPUT PATHS, MALFORMED *AND* CLEAN.

    ★★ EACH CASE ASSERTS BOTH HALVES IN THE SAME BREATH: the banner/field AND a NONZERO figure.
    ⛔ A test that only checked "the banner appears" would pass on a build that reports nothing at
    all — which is precisely the family of defect this arc keeps producing. A test that only checked
    "the numbers match" would pass when both arms are zero (the `s27` lesson). Both, together.

    ★ Plus the PRE-FIX CONTROL: with the crossing point and the single JSON sink patched out, the
    same fixture prints the 346 ms total with NO banner and emits airtime JSON with NO census."""
    print("\n[11] all six output paths: banner/field + a NONZERO figure, on bad AND clean input")
    ev = _copies_fixture()
    bad_lines = ['{"type": "tx", "time_ms": 9999, "node": "A", "hex": "aa',   # truncated
                 '"just a string"']                                          # not an object
    p_bad = write_stream(ev, compact=False, extra_raw_lines=bad_lines)
    p_ok = write_stream(ev, compact=False)
    cfg = {"nodes": [{"name": "A", "node_id": 11, "layer_id": 1, "lat": 1.0, "lon": 1.0},
                     {"name": "B", "node_id": 12, "layer_id": 1, "lat": 1.01, "lon": 1.01}]}
    fd, cfg_path = tempfile.mkstemp(suffix=".json", prefix="b162d-cfg-")
    with os.fdopen(fd, "w") as f:
        json.dump(cfg, f)

    BANNER, LOWER = "NDJSON LINE(S) REFUSED", "LOWER BOUND"
    # (label, argv, is_json, probe(text_or_payload) -> (name, value) that MUST be nonzero)
    CASES = [
        ("--mode dm --all", ["--mode", "dm", "--all"], False,
         lambda t: ("DM table sent count", _nonzero_int_after(t, "A(11) -> B(12)"))),
        ("--mode dm --all --json", ["--mode", "dm", "--all", "--json"], True,
         lambda d: ("totals.unique_deliveries", d["totals"]["unique_deliveries"])),
        ("--mode all --all --json", ["--mode", "all", "--all", "--json"], True,
         lambda d: ("totals.unique_deliveries", d["totals"]["unique_deliveries"])),
        ("--airtime", ["--airtime"], False,
         lambda t: ("TOTAL AIRTIME (AUTHORITY) ms",
                    _nonzero_int_after(t, "TOTAL AIRTIME (AUTHORITY) = "))),
        ("--airtime --json", ["--airtime", "--json"], True,
         lambda d: ("airtime.phy_total_ms", d["airtime"]["phy_total_ms"])),
        ("--mode channel --json", ["--mode", "channel", "--json"], True,
         lambda d: ("channels key present", 1 if "channels" in d else 0)),
        ("--trace p1", ["--trace", "p1"], False,
         lambda t: ("traced events", _nonzero_int_after(t, "TRACE 'p1': "))),
        ("--copies", ["--copies"], False,
         lambda t: ("total switches", _nonzero_int_after(t, "total switches         : "))),
        ("--tail", ["--tail"], False,
         lambda t: ("top-10 attributed airtime ms",
                    _nonzero_int_after(t, "top-10 share of ATTRIBUTED airtime: 100.0%  ("))),
    ]
    try:
        for label, argv, is_json, probe in CASES:
            r_bad = _cli(cfg_path, p_bad, argv)
            r_ok = _cli(cfg_path, p_ok, argv)
            check(r_bad.returncode == 0 and r_ok.returncode == 0,
                  f"{label}: both CLI runs succeed (rc {r_bad.returncode}/{r_ok.returncode})")
            if is_json:
                d_bad, d_ok = json.loads(r_bad.stdout), json.loads(r_ok.stdout)
                nb, vb = probe(d_bad)
                no, vo = probe(d_ok)
                check(d_bad["malformed_ndjson_lines"] == 2
                      and len(d_bad["malformed_ndjson_examples"]) == 2
                      and d_bad["measurement_is_lower_bound"] is True
                      and bool(vb),
                      f"★ {label}: census 2 lines + 2 examples + lower_bound=true, ALONGSIDE a "
                      f"NONZERO {nb}={vb}")
                check(d_ok["malformed_ndjson_lines"] == 0
                      and d_ok["malformed_ndjson_examples"] == []
                      and d_ok["measurement_is_lower_bound"] is False
                      and bool(vo) and vo == vb,
                      f"★ CONTROL {label}: clean input → 0 / [] / false (keys PRESENT), same "
                      f"NONZERO {no}={vo} — so the census varies and the figure does not")
            else:
                nb, vb = probe(r_bad.stdout)
                no, vo = probe(r_ok.stdout)
                check(BANNER in r_bad.stdout and LOWER in r_bad.stdout and bool(vb),
                      f"★ {label}: banner + LOWER BOUND printed, ALONGSIDE a NONZERO {nb}={vb}")
                check(BANNER not in r_ok.stdout and bool(vo) and vo == vb,
                      f"★ CONTROL {label}: clean input prints NO banner but the same NONZERO "
                      f"{no}={vo} — the banner discriminates and the figure is not zero")

        # ⛔ C2: --json against a text-only view is REFUSED, not silently ignored.
        for argv in (["--trace", "p1", "--json"], ["--copies", "--json"], ["--tail", "--json"]):
            r = _cli(cfg_path, p_bad, argv)
            check(r.returncode != 0 and "not implemented" in (r.stderr + r.stdout)
                  and r.stdout == "",
                  f"{' '.join(argv)}: REFUSED loudly (rc {r.returncode}), no text emitted as if the "
                  f"flag had been honoured")

        # ★★ THE PRE-FIX CONTROL — the defect, reproduced on demand.
        pre = {"ndjson_refusal_crossing_point": lambda a: None,
               "emit_json": lambda payload, ep: (json.dump(payload, sys.stdout, indent=2),
                                                 sys.stdout.write("\n"))}
        out = _in_process([cfg_path, p_bad, "--airtime"], pre)
        tot = _nonzero_int_after(out, "TOTAL AIRTIME (AUTHORITY) = ")
        check(tot == 346 and BANNER not in out and LOWER not in out,
              f"★★ CONTROL: PRE-FIX topology prints the authoritative total ({tot} ms) with NO "
              f"banner and NO lower-bound warning — THE BLOCKER, reproduced")
        out_j = json.loads(_in_process([cfg_path, p_bad, "--airtime", "--json"], pre))
        check(out_j["airtime"]["phy_total_ms"] == 346
              and "malformed_ndjson_lines" not in out_j
              and "malformed_ndjson_examples" not in out_j,
              "★★ CONTROL: PRE-FIX airtime JSON carries the 346 ms total and NEITHER census key — "
              "an absent key a consumer cannot tell from a zero")
        # ★ and the same vehicle with NO patches shows the fix, so the vehicle itself is honest.
        out_fixed = _in_process([cfg_path, p_bad, "--airtime"], {})
        check(BANNER in out_fixed and _nonzero_int_after(
                  out_fixed, "TOTAL AIRTIME (AUTHORITY) = ") == 346,
              "★ CONTROL ON THE CONTROL: the same in-process vehicle, UNPATCHED, prints the banner "
              "and the same 346 ms — so the difference above is the patch, not the vehicle")
    finally:
        for p in (p_bad, p_ok, cfg_path):
            os.unlink(p)



# ==================================================================================================
# ★★★★ [[B182]] 2026-08-12 — THE TWO-IDENTITY-LAYER TESTS.
#
# ⛔⛔ WHAT THESE PIN, AND WHY A "the tool names the pair" CHECK WOULD HAVE PASSED ON THE DEFECT: the
# pre-fix tool DID emit `Sender(17) -> MobileM(50)` rows — and printed `0 / 3` for three DMs that
# arrived. So no assertion about ROWS EXISTING can detect this bug; only an assertion about the
# ARRIVAL COUNT of a hosted-mobile pair can. Every check below is therefore paired with the OLD
# algorithm re-implemented locally (`old_style_rows`, `old_configured_pairs`) and shown to give the
# WRONG number on the SAME bytes.
#
# ⚠ AND THE ARC'S OWN LESSON IS APPLIED: two controls in this arc were VACUOUS because of the state
# the test actually reached (one queried right after a clock restamp, one fired no timer). So each
# fixture below ESTABLISHES its state explicitly — the adoption is emitted before the DM, the lease id
# is asserted to be the one the delivery carries, and every counter asserted nonzero is also shown at
# zero on a neighbouring fixture, so it is a discriminator and not a constant.

def _mk_cfg(nodes, commands):
    fd, path = tempfile.mkstemp(suffix=".json", prefix="b182-cfg-")
    with os.fdopen(fd, "w") as f:
        json.dump({"nodes": nodes, "commands": commands}, f)
    return path


def _node(name, node_id, key_hash32=None, **cfgkv):
    n = {"name": name, "node_id": node_id}
    if key_hash32 is not None:
        n["key_hash32"] = key_hash32
    if cfgkv:
        n["config"] = dict(cfgkv)
    return n


def _dm_json(cfg_path, ev_path, extra=()):
    r = _cli(cfg_path, ev_path, ["--mode", "dm", "--json"] + list(extra))
    assert r.returncode == 0, r.stderr
    return json.loads(r.stdout)


def _dm_text(cfg_path, ev_path, extra=()):
    r = _cli(cfg_path, ev_path, ["--mode", "dm"] + list(extra))
    assert r.returncode == 0, r.stderr
    return r.stdout


def _row(payload, pair):
    for row in payload["summary"]:
        if f"{row['origin']} -> {row['dst']}" == pair:
            return row
    return None


# --- the OLD algorithm, re-implemented as the [[B182]] CONTROL ------------------------------------
# ⓘ A deliberate local duplicate of the code [[B182]] replaced, exactly as `old_index_and_global_phy`
# is for [[B162b]]. ⛔ Do not "dedupe" it into the tool: the point is that it is NOT the live path.
def old_configured_pairs(cfg, name_to_id, hash_layer_to_name):
    """PRE-[[B182]]: intents keyed on the CONFIG `node_id`. Five `node_id: 0` mobiles collapse to one."""
    pairs = T.Counter()
    id_to_layer, hash_to_ids = {}, T.defaultdict(set)
    for n in cfg.get("nodes", []):
        lyr = (n.get("config") or {}).get("layer_id")
        if lyr is not None:
            id_to_layer[n["node_id"]] = lyr
        h = T._hash_key_to_int(n.get("key_hash32"))
        if h is not None:
            hash_to_ids[h].add(n["node_id"])
    for c in cfg.get("commands", []):
        cmd, src_id = c.get("command", ""), name_to_id.get(c.get("node"))
        m_hash = T.SEND_HASH_RE.match(cmd)
        if m_hash:
            tok = m_hash.group(1)
            th = int(tok[2:] if tok[:2].lower() == "0x" else tok, 16)
            dst_name = hash_layer_to_name.get((id_to_layer.get(src_id), th))
            dst_id = name_to_id.get(dst_name) if dst_name else None
            if dst_id is None:
                cand = hash_to_ids.get(th) or set()
                dst_id = next(iter(cand)) if len(cand) == 1 else None
            if src_id is not None and dst_id is not None:
                pairs[(src_id, dst_id)] += 1
            continue
        m = T.SEND_RE.match(cmd)
        if not m:
            continue
        tok = m.group(1)
        dst_id = name_to_id.get(tok)
        if dst_id is None and tok.isdigit() and 1 <= int(tok) <= 254:
            dst_id = int(tok)
        if src_id is not None and dst_id is not None:
            pairs[(src_id, dst_id)] += 1
    return pairs


def old_style_rows(cfg_path, ev_path):
    """PRE-[[B182]] pairing, computed from the LIVE `analyse()` output on the same stream.

    Reconstitutes both halves of the defect without keeping a copy of the file:
      · the record-creation policy `fid == origin or alias[origin] == fid` — which DROPPED a hosted
        mobile's own DM entirely (its wire origin is the home's static id, and the alias pass
        deliberately never translates an id that is a real node's config id);
      · pair keys taken from the record's WIRE `origin`/`effective_dst`, canonicalised through the
        runtime alias — the map that cannot express a home id that is also a real static.
    Returns rows in the same shape `summarise()` produces, keyed `"name(id) -> name(id)"`."""
    (cfg, id_to_name, name_to_id, slot_to_id, hlt, i2l, slots) = T.load_config(cfg_path)
    out = T.analyse(ev_path, slot_to_id, hlt, i2l, slots)
    msgs, wire_to_config, hosted_by, logical = out[0], out[7], out[9], out[10]
    pairs = old_configured_pairs(cfg, name_to_id, hlt)
    w2c = dict(wire_to_config)
    keep = {}
    for k, r in msgs.items():
        if r["src_slot"] is None:
            continue
        fid = slot_to_id.get(r["src_slot"], r["src_slot"])
        if not (fid == r["origin"] or w2c.get(r["origin"]) == fid):
            continue                          # ⛔ the old policy: this record never existed
        keep[k] = r
    canon = T.Counter()
    for (o, dd), n in pairs.items():
        canon[(w2c.get(o, o), w2c.get(dd, dd))] += n
    by_pair = T.defaultdict(list)
    for k, r in keep.items():
        by_pair[(w2c.get(r["origin"], r["origin"]),
                 w2c.get(T.effective_dst(r), T.effective_dst(r)))].append(r)
    rows = {}
    for pk in set(by_pair) | set(canon):
        recs = by_pair.get(pk, [])
        unsent = max(0, canon.get(pk, 0) - len(recs))
        rows[f"{T.fmt_node(pk[0], id_to_name)} -> {T.fmt_node(pk[1], id_to_name)}"] = {
            "sent": len(recs) + unsent,
            "arrived": sum(1 for r in recs if T._arrived(r)),
            "unsent": unsent}
    return rows


# --- fixture builders -----------------------------------------------------------------------------
# ★ Every shape below was READ OFF A REAL STREAM (`/tmp/s22_mobile_team_meshroute_analyze.ndjson`,
#   `lus` 316b9cb1) rather than invented, so the fixtures cannot be self-serving. The measured s22
#   sequence for one static->hosted-mobile DM is, verbatim:
#     455820 S2 tx_enqueue {origin:30, dst:17, ctr:1}      <- wire dst = the HOME
#     456678 S1 data_rx    {origin:30, dst:17, ctr:1}
#     458142 M1 data_rx    {origin:30, dst:254, ctr:1}     <- wire dst = the LEASED id
#     458299 M1 delivered  {origin:30, dst:254, ctr:1, payload:"static_to_mobile"}
#   and for one hosted-mobile->static DM:
#     498000 M1 tx_enqueue {origin:17, dst:30, ctr:1}      <- wire ORIGIN = the HOME's static id
#     500685 S2 delivered  {origin:17, dst:30, ctr:1, payload:"mobile_to_static"}
def _adopt(mobile_slot, home_slot, home_id, mobile_key_dec, lease=254, t=1000, epoch=1):
    """The adoption, emitted BEFORE any DM — the state the last-mile correlation depends on. ⚠ Both
    halves are emitted because the tool reads both and must not depend on either alone."""
    return [emit(mobile_slot, t, "mobile_adopted", epoch=epoch, home=home_id, local_id=lease),
            emit(home_slot, t + 200, "mobile_registered", epoch=epoch, key=mobile_key_dec,
                 local_id=lease)]


def _static_to_mobile(t, s_slot, s_id, h_slot, h_id, m_slot, ctr, payload, lease=254,
                      deliver=True):
    ev = [emit(s_slot, t, "tx_enqueue", origin=s_id, dst=h_id, ctr=ctr),
          emit(h_slot, t + 800, "data_rx", origin=s_id, dst=h_id, ctr=ctr, ctr_lo=ctr),
          emit(s_slot, t + 900, "ack_rx", origin=s_id, dst=h_id, ctr=ctr, **{"from": h_id})]
    if deliver:
        ev += [emit(m_slot, t + 2300, "data_rx", origin=s_id, dst=lease, ctr=ctr, ctr_lo=ctr),
               emit(m_slot, t + 2400, "delivered", origin=s_id, dst=lease, ctr=ctr,
                    payload=payload)]
    return ev


def _mobile_to_static(t, m_slot, h_id, d_slot, d_id, ctr, payload, deliver=True):
    ev = [emit(m_slot, t, "tx_enqueue", origin=h_id, dst=d_id, ctr=ctr),
          emit(m_slot, t + 900, "ack_rx", origin=h_id, dst=d_id, ctr=ctr, **{"from": h_id})]
    if deliver:
        ev += [emit(d_slot, t + 2400, "data_rx", origin=h_id, dst=d_id, ctr=ctr, ctr_lo=ctr),
               emit(d_slot, t + 2500, "delivered", origin=h_id, dst=d_id, ctr=ctr,
                    payload=payload)]
    return ev


def _plain_dm(t, a_slot, a_id, b_slot, b_id, ctr, payload, deliver=True):
    ev = [emit(a_slot, t, "tx_enqueue", origin=a_id, dst=b_id, ctr=ctr),
          emit(a_slot, t + 900, "ack_rx", origin=a_id, dst=b_id, ctr=ctr, **{"from": b_id})]
    if deliver:
        ev += [emit(b_slot, t + 2400, "data_rx", origin=a_id, dst=b_id, ctr=ctr, ctr_lo=ctr),
               emit(b_slot, t + 2500, "delivered", origin=a_id, dst=b_id, ctr=ctr, payload=payload)]
    return ev


# ==================================================================================================
def test_12_hosted_mobile_both_directions():
    """★★★ [[B182]] TESTS 1-4 (hermetic form): a hosted mobile is scored in BOTH directions, and the
    TEAM-plane mobile->mobile row — the positive control that proves the machinery was never blind to
    mobiles in general — is UNMOVED. The fixture carries all three at once, so one algorithm has to
    satisfy all three: a "fix" that moves the team row is a regression by construction."""
    print("\n[12] [[B182]] static->hosted-mobile, hosted-mobile->static, and the team row unmoved")
    nodes = [_node("S1", 17, "0x11110017"),                    # slot 0 — the HOME
             _node("S2", 30, "0x11110030"),                    # slot 1 — a plain static
             _node("TeamA", 50, "0x2215F68F", is_mobile=True, team_id=305419896),   # slot 2
             _node("TeamC", 52, "0x38E8B9F4", is_mobile=True, team_id=305419896),   # slot 3
             _node("M1", 60, "0x11110060", is_mobile=True)]     # slot 4 — hosted by S1
    cmds = [{"at_ms": 400000, "node": "TeamA", "command": "send_hash 38e8b9f4 team_dm_ping -t"},
            {"at_ms": 420000, "node": "TeamA", "command": "send_hash 38e8b9f4 team_dm_ping -t"},
            {"at_ms": 455000, "node": "S2", "command": "send_hash 11110060 static_to_mobile"},
            {"at_ms": 478000, "node": "S2", "command": "send_hash 11110060 static_to_mobile"},
            {"at_ms": 498000, "node": "M1", "command": "send_e2e S2 mobile_to_static"},
            {"at_ms": 522000, "node": "M1", "command": "send_e2e S2 mobile_to_static"}]
    ev = _adopt(4, 0, 17, 0x11110060, lease=254, t=42862)
    # the TEAM plane: wire origin/dst are the team-local ids 241/224, learned by observation
    for i, t in ((1, 400000), (2, 420000)):
        ev += [emit(2, t, "tx_enqueue", origin=241, dst=224, ctr=i),
               emit(2, t + 900, "ack_rx", origin=241, dst=224, ctr=i, **{"from": 224}),
               emit(3, t + 942, "delivered", origin=241, dst=224, ctr=i, payload="team_dm_ping")]
    ev += _static_to_mobile(455820, 1, 30, 0, 17, 4, 1, "static_to_mobile")
    ev += _static_to_mobile(478000, 1, 30, 0, 17, 4, 2, "static_to_mobile")
    ev += _mobile_to_static(498000, 4, 17, 1, 30, 1, "mobile_to_static")
    ev += _mobile_to_static(522000, 4, 17, 1, 30, 2, "mobile_to_static")
    cfg_path, ev_path = _mk_cfg(nodes, cmds), write_stream(ev)
    try:
        p = _dm_json(cfg_path, ev_path)
        s2m, m2s = _row(p, "S2(30) -> M1(60)"), _row(p, "M1(60) -> S2(30)")
        team = _row(p, "TeamA(50) -> TeamC(52)")
        check(s2m is not None and (s2m["sent"], s2m["arrived"], s2m["unsent"]) == (2, 2, 0),
              f"static -> hosted mobile: 2/2 arrived, 0 unsent "
              f"(got {None if not s2m else (s2m['sent'], s2m['arrived'], s2m['unsent'])})")
        check(m2s is not None and (m2s["sent"], m2s["arrived"], m2s["unsent"]) == (2, 2, 0),
              f"hosted mobile -> static: 2/2 arrived, 0 unsent "
              f"(got {None if not m2s else (m2s['sent'], m2s['arrived'], m2s['unsent'])})")
        check(team is not None and (team["sent"], team["arrived"]) == (2, 2),
              f"★ REGRESSION GUARD: team mobile -> team mobile still 2/2 "
              f"(got {None if not team else (team['sent'], team['arrived'])})")
        tot = p["totals"]
        check(tot["unique_deliveries"] == 6,
              f"unique_deliveries = 6 over the three pairs (got {tot['unique_deliveries']})")
        check(tot["logical_lastmile_correlated"] == 2,
              f"★ MATCH COUNT: exactly 2 deliveries recovered through the (origin,ctr) last mile "
              f"(got {tot['logical_lastmile_correlated']}) — the 2 static->mobile ones, and NOT the "
              f"team pair, whose dst never changes")
        check(tot["logical_dst_from_delivery"] == 6 and tot["logical_dst_from_wire"] == 0,
              f"★ BASIS: all 6 pairs come from an OBSERVED delivery, none from the wire dst "
              f"(got {tot['logical_dst_from_delivery']}/{tot['logical_dst_from_wire']})")
        check(all(tot[k] == 0 for k in T.LOGICAL_REFUSAL_KEYS),
              "and every refusal counter is 0 on a clean fixture — so a nonzero one means something")

        # ★★ THE MUTATION CONTROL: restore id-keyed pairing + the id-based record-creation policy.
        old = old_style_rows(cfg_path, ev_path)
        o_s2m = old.get("S2(30) -> M1(60)", {})
        o_m2s = old.get("M1(60) -> S2(30)", {})
        o_team = old.get("TeamA(50) -> TeamC(52)", {})
        check(o_s2m.get("arrived") == 0 and o_s2m.get("sent") == 2,
              f"★★ CONTROL: id-keyed pairing reports static->mobile as {o_s2m.get('arrived')}/"
              f"{o_s2m.get('sent')} — RED, the defect reproduced on the same bytes")
        check(o_m2s.get("arrived") == 0 and o_m2s.get("sent") == 2,
              f"★★ CONTROL: id-keyed pairing reports mobile->static as {o_m2s.get('arrived')}/"
              f"{o_m2s.get('sent')} — RED (its record does not even exist under the old policy)")
        check(o_team.get("arrived") == 2 and o_team.get("sent") == 2,
              f"★★★ CONTROL ON THE CONTROL: the same old algorithm gets the TEAM row RIGHT "
              f"({o_team.get('arrived')}/{o_team.get('sent')}) — so the defect really is ONE "
              f"addressing path, and the two reds above are not a broken control")
    finally:
        os.unlink(cfg_path)
        os.unlink(ev_path)


# ==================================================================================================
def test_13_one_home_id_two_logical_senders_concurrently():
    """★★★★ [[B182]] TEST 6 — THE SHARPEST ONE, AND THE CASE AN ALIAS MAP CANNOT EXPRESS.

    A home id `17` is used, AT THE SAME TIME, as (a) the wire origin of its hosted mobile's DM and
    (b) the identity of the home's OWN DM. ⛔ No global `wire id -> config id` map can be right here:
    any entry for 17 is wrong for one of the two, and NO entry is wrong for the mobile. The fixture
    interleaves them (mobile at t, home at t+200, both to the same destination) so nothing can be
    resolved by ordering either.
    ★ And the MIRROR is asserted in the same fixture: one sender addressing the HOME and the HOSTED
    MOBILE concurrently, where the wire dst is `17` for BOTH."""
    print("\n[13] one home id, two logical senders, concurrently — no cross-attribution")
    nodes = [_node("H", 17, "0x11110017", host_mobiles=True),   # slot 0
             _node("D", 30, "0x11110030"),                      # slot 1
             _node("M", 60, "0x11110060", is_mobile=True)]       # slot 2, hosted by H
    cmds = [{"at_ms": 100000, "node": "M", "command": "send_e2e D from-mobile"},
            {"at_ms": 100200, "node": "H", "command": "send_e2e D from-home"},
            {"at_ms": 200000, "node": "D", "command": "send_e2e H to-home"},
            {"at_ms": 200200, "node": "D", "command": "send_hash 11110060 to-mobile"}]
    ev = _adopt(2, 0, 17, 0x11110060, lease=254, t=1000)
    # (a) the MOBILE's DM: wire origin 17 (its home), emitted at the MOBILE's slot, ctr 1
    ev += _mobile_to_static(100000, 2, 17, 1, 30, 1, "from-mobile")
    # (b) the HOME's OWN DM: wire origin 17 too, emitted at the HOME's slot, ctr 2 — CONCURRENT
    ev += _plain_dm(100200, 0, 17, 1, 30, 2, "from-home")
    # the mirror: D -> H (arrives AT H) and D -> M (wire dst 17, arrives at the leased id), concurrent
    ev += _plain_dm(200000, 1, 30, 0, 17, 3, "to-home")
    ev += _static_to_mobile(200200, 1, 30, 0, 17, 2, 4, "to-mobile")
    cfg_path, ev_path = _mk_cfg(nodes, cmds), write_stream(ev)
    try:
        p = _dm_json(cfg_path, ev_path)
        got = {f"{r['origin']} -> {r['dst']}": (r["sent"], r["arrived"]) for r in p["summary"]}
        check(got.get("M(60) -> D(30)") == (1, 1),
              f"the MOBILE's DM is attributed to the MOBILE: 1/1 (got {got.get('M(60) -> D(30)')})")
        check(got.get("H(17) -> D(30)") == (1, 1),
              f"the HOME's own DM is attributed to the HOME: 1/1 (got {got.get('H(17) -> D(30)')})")
        check(got.get("D(30) -> H(17)") == (1, 1),
              f"the mirror: D->H is 1/1 and NOT inflated by the mobile-bound frame "
              f"(got {got.get('D(30) -> H(17)')})")
        check(got.get("D(30) -> M(60)") == (1, 1),
              f"the mirror: D->M is 1/1 though its wire dst was the home "
              f"(got {got.get('D(30) -> M(60)')})")
        check(p["totals"]["unique_deliveries"] == 4,
              f"4 logical sends, 4 deliveries, nothing double-counted "
              f"(got {p['totals']['unique_deliveries']})")
        check(p["totals"]["logical_lastmile_correlated"] == 1,
              f"★ MATCH COUNT: exactly ONE of the four needed the last-mile correlation "
              f"(got {p['totals']['logical_lastmile_correlated']})")
        check(all(p["totals"][k] == 0 for k in T.LOGICAL_REFUSAL_KEYS),
              "and NOTHING had to be refused: the four are fully determined by observation")

        # ★★ THE MUTATION CONTROL — an alias map cannot express this, and here is the number it gets.
        old = old_style_rows(cfg_path, ev_path)
        check(old.get("M(60) -> D(30)", {}).get("arrived") == 0,
              f"★★ CONTROL: id-keyed -> the mobile's DM reads "
              f"{old.get('M(60) -> D(30)', {}).get('arrived')}/"
              f"{old.get('M(60) -> D(30)', {}).get('sent')} — RED")
        check(old.get("D(30) -> M(60)", {}).get("arrived") == 0,
              f"★★ CONTROL: id-keyed -> D->M reads "
              f"{old.get('D(30) -> M(60)', {}).get('arrived')}/"
              f"{old.get('D(30) -> M(60)', {}).get('sent')} — RED")
        d2h = old.get("D(30) -> H(17)", {})
        check(d2h.get("sent") == 2 and d2h.get("arrived") == 2,
              f"★★★ CONTROL, THE INFLATION HALF — id-keyed reports D->H as {d2h.get('arrived')}/"
              f"{d2h.get('sent')} because the MOBILE-BOUND frame is filed and CREDITED there. "
              f"⛔ That is a delivery attributed to the wrong recipient, not merely a missing one")
        h2d = old.get("H(17) -> D(30)", {})
        # ⚠ MEASURED, AND IT CORRECTED THIS TEST'S FIRST EXPECTATION (establish the state, not the
        #   name): the sender half does NOT come out as a 2/2 merge. Under the old policy the mobile's
        #   record fails `fid == origin or alias[origin] == fid` (its emitting node is 60, its wire
        #   origin is 17, and the alias pass refuses to translate 17 because it IS a real node's config
        #   id) — so the record is never created at all. ⇒ the mobile's DM is not MISFILED, it is
        #   DELETED, and H->D shows only the home's own send. That is strictly worse than a merge and
        #   is why `raw_delivered_events - unique_deliveries` was the only trace of it.
        check(h2d.get("sent") == 1 and h2d.get("arrived") == 1
              and old.get("M(60) -> D(30)", {}).get("sent") == 1,
              f"★★★ CONTROL, the sender half — id-keyed shows H->D as {h2d.get('arrived')}/"
              f"{h2d.get('sent')} (the home's own send only) while the MOBILE's DM produced NO "
              f"RECORD AT ALL and survives solely as an UNSENT intent "
              f"({old.get('M(60) -> D(30)', {}).get('unsent')}). ⛔ Deleted, not merged.")
    finally:
        os.unlink(cfg_path)
        os.unlink(ev_path)


# ==================================================================================================
def test_14_ambiguity_is_refused_not_guessed():
    """★★★★ [[B182]] ITEM 5 — THE THREE REFUSALS, EACH DRIVEN OFF ZERO AND EACH SHOWN AT ZERO.

    ⚠ These counters are instruments, and this arc has 20+ instruments that could not fail. So every
    case below is a PAIR: a fixture on which the counter is nonzero AND a neighbouring fixture,
    differing in exactly one fact, on which it is zero and the correlation succeeds instead."""
    print("\n[14] ambiguous (origin,ctr) / shared wire triple / undecidable dst are REFUSED")
    # --- (a) an AMBIGUOUS (origin, ctr): `ctr` is per-DESTINATION, so one origin legitimately holds
    #         (o,dstA,c) and (o,dstB,c) at once. If the delivering mobile has held a lease under BOTH
    #         of those homes, the delivery is genuinely undetermined.
    nodes = [_node("HA", 17, "0x11110017"), _node("HB", 18, "0x11110018"),
             _node("S", 30, "0x11110030"), _node("M", 60, "0x11110060", is_mobile=True)]
    cmds = [{"at_ms": 100000, "node": "S", "command": "send_hash 11110060 amb"}]
    base = _adopt(3, 0, 17, 0x11110060, lease=254, t=1000) \
        + _adopt(3, 1, 18, 0x11110060, lease=254, t=2000, epoch=2)
    both = base \
        + [emit(2, 100000, "tx_enqueue", origin=30, dst=17, ctr=1),
           emit(2, 100010, "tx_enqueue", origin=30, dst=18, ctr=1),
           emit(3, 103000, "delivered", origin=30, dst=254, ctr=1, payload="amb")]
    one = base \
        + [emit(2, 100000, "tx_enqueue", origin=30, dst=17, ctr=1),
           emit(3, 103000, "delivered", origin=30, dst=254, ctr=1, payload="amb")]
    cfg_path = _mk_cfg(nodes, cmds)
    p_both, p_one = write_stream(both), write_stream(one)
    try:
        tb = _dm_json(cfg_path, p_both)["totals"]
        check(tb["logical_lastmile_refused_ambiguous"] == 1
              and tb["logical_lastmile_correlated"] == 0
              and tb["unique_deliveries"] == 0,
              f"★★ TWO candidate records for one (origin,ctr) -> REFUSED, not picked "
              f"(refused={tb['logical_lastmile_refused_ambiguous']}, "
              f"correlated={tb['logical_lastmile_correlated']}, "
              f"deliveries={tb['unique_deliveries']})")
        check(_row(_dm_json(cfg_path, p_both), "S(30) -> M(60)")["sent"] == 1,
              "⇒ and the configured send is STILL in the denominator, via UNSENT — a refusal never "
              "shrinks the denominator")
        to = _dm_json(cfg_path, p_one)["totals"]
        check(to["logical_lastmile_refused_ambiguous"] == 0
              and to["logical_lastmile_correlated"] == 1
              and to["unique_deliveries"] == 1,
              f"★ CONTROL, ONE FACT DIFFERENT (the second record removed): the SAME code correlates "
              f"it and scores 1/1 (refused={to['logical_lastmile_refused_ambiguous']}, "
              f"correlated={to['logical_lastmile_correlated']})")
    finally:
        os.unlink(p_both)
        os.unlink(p_one)
        os.unlink(cfg_path)

    # --- (b) ONE wire triple, TWO emitting slots. ★ NOT hypothetical: measured on `s07`, where
    #         (19,27,1) is claimed by two different mobiles and (37,20,1) by a mobile AND by the real
    #         static `N7GRN5_Portage_Bay_r(37)`.
    nodes2 = [_node("A", 11, "0x11110011"), _node("B", 12, "0x11110012"),
              _node("C", 13, "0x11110013")]
    cmds2 = [{"at_ms": 100, "node": "A", "command": "send_e2e C p"},
             {"at_ms": 200, "node": "B", "command": "send_e2e C p"}]
    shared = _plain_dm(100, 0, 11, 2, 13, 1, "p") + \
        [emit(1, 200, "tx_enqueue", origin=11, dst=13, ctr=1)]          # B claims A's triple
    alone = _plain_dm(100, 0, 11, 2, 13, 1, "p")
    cfg2 = _mk_cfg(nodes2, cmds2)
    p_sh, p_al = write_stream(shared), write_stream(alone)
    try:
        ts = _dm_json(cfg2, p_sh)["totals"]
        check(ts["logical_refused_wire_key_shared"] == 1 and ts["unique_deliveries"] == 0,
              f"★★ two slots on one wire triple -> the logical sender is undetermined and the record "
              f"is REFUSED (shared={ts['logical_refused_wire_key_shared']}, "
              f"deliveries={ts['unique_deliveries']}) rather than credited to whichever came first")
        ta = _dm_json(cfg2, p_al)["totals"]
        check(ta["logical_refused_wire_key_shared"] == 0 and ta["unique_deliveries"] == 1,
              f"★ CONTROL: one claimant -> 0 refusals and the delivery scores "
              f"(shared={ta['logical_refused_wire_key_shared']}, "
              f"deliveries={ta['unique_deliveries']})")
    finally:
        os.unlink(p_sh)
        os.unlink(p_al)
        os.unlink(cfg2)

    # --- (c) an UNDELIVERED record whose wire dst is a HOME the sender ALSO addresses a hosted mobile
    #         at. ⛔ Home-or-mobile is undecidable from the wire; guessing would either depress the
    #         (S,H) pair or CREDIT it with a delivery that was for the mobile.
    nodes3 = [_node("H", 17, "0x11110017", host_mobiles=True), _node("S", 30, "0x11110030"),
              _node("M", 60, "0x11110060", is_mobile=True)]
    with_mobile_intent = [{"at_ms": 100, "node": "S", "command": "send_hash 11110060 to-mobile"},
                          {"at_ms": 200, "node": "S", "command": "send_e2e H to-home"}]
    home_only_intent = [{"at_ms": 200, "node": "S", "command": "send_e2e H to-home"}]
    # ONE record, wire dst 17, reaching H's radio but NEVER app-delivered anywhere.
    stalled = _adopt(2, 0, 17, 0x11110060, lease=254, t=1000) + [
        emit(1, 100, "tx_enqueue", origin=30, dst=17, ctr=1),
        emit(0, 900, "data_rx", origin=30, dst=17, ctr=1, ctr_lo=1)]
    cfg_amb, cfg_una = _mk_cfg(nodes3, with_mobile_intent), _mk_cfg(nodes3, home_only_intent)
    p_st = write_stream(stalled)
    try:
        ta = _dm_json(cfg_amb, p_st)["totals"]
        check(ta["logical_refused_ambiguous_dst"] == 1 and ta["unique_deliveries"] == 0,
              f"★★ undecidable home-vs-hosted-mobile destination -> REFUSED "
              f"(refused={ta['logical_refused_ambiguous_dst']}, "
              f"deliveries={ta['unique_deliveries']}) — ⛔ it is NOT credited to S->H")
        ra = _dm_json(cfg_amb, p_st)
        check(_row(ra, "S(30) -> H(17)")["sent"] == 1
              and _row(ra, "S(30) -> H(17)")["arrived"] == 0
              and _row(ra, "S(30) -> M(60)")["sent"] == 1,
              "⇒ both configured pairs keep their full denominator (1 each) and neither claims an "
              "arrival: the refusal is visible as sent-and-not-arrived, not as a hole")
        tu = _dm_json(cfg_una, p_st)["totals"]
        check(tu["logical_refused_ambiguous_dst"] == 0 and tu["logical_dst_from_wire"] == 1,
              f"★ CONTROL, ONE FACT DIFFERENT (the mobile-directed intent removed): the SAME record "
              f"is no longer ambiguous and resolves from the wire dst "
              f"(refused={tu['logical_refused_ambiguous_dst']}, "
              f"from_wire={tu['logical_dst_from_wire']}) — so the guard discriminates, it does not "
              f"blanket-refuse every send to a home")
    finally:
        os.unlink(p_st)
        os.unlink(cfg_amb)
        os.unlink(cfg_una)


# ==================================================================================================
def test_15_five_id_zero_mobiles_stay_distinct():
    """★★ [[B182]] TEST 5 — `s27`'s FIVE `node_id: 0` MOBILES REMAIN FIVE LOGICAL NODES.

    ⛔ Pre-fix they shared ONE config id, so `M1 -> M2` rendered as the self-send `M5(0) -> M5(0)` and
    six unrelated sends landed on one row — SILENTLY, because the `ALIASED_IDS` collision warning is
    suppressed for id 0 on the premise that such a mobile "never appears as origin 0 in the stream".
    ★ True of the STREAM, false of the CONFIGURED-SEND side, which read `node_id` from the JSON."""
    print("\n[15] five configured node_id-0 mobiles are five DISTINCT logical nodes")
    nodes = [_node("S1", 101, "0x44070011", layer_id=4),
             _node("M1", 0, "0x2716EFCD", is_mobile=True, layer_id=4),
             _node("M2", 0, "0x3A3E77A3", is_mobile=True, layer_id=4),
             _node("M3", 0, "0xBCC13CC5", is_mobile=True, layer_id=4),
             _node("M4", 0, "0x455FCF59", is_mobile=True, layer_id=4),
             _node("M5", 0, "0x44070031", is_mobile=True, layer_id=4)]
    cmds = [{"at_ms": 1, "node": "M1", "command": "send_hash 3a3e77a3 a"},
            {"at_ms": 2, "node": "M2", "command": "send_hash 2716efcd b"},
            {"at_ms": 3, "node": "M3", "command": "send_hash 455fcf59 c"},
            {"at_ms": 4, "node": "M4", "command": "send_hash bcc13cc5 d"},
            {"at_ms": 5, "node": "S1", "command": "send_hash 44070031 e"}]
    cfg_path = _mk_cfg(nodes, cmds)
    try:
        (cfg, id_to_name, name_to_id, slot_to_id, hlt, i2l, slots) = T.load_config(cfg_path)
        pairs, intended, unparsed = T.configured_pairs(cfg, slots, hlt)
        check(len(pairs) == 5 and sum(pairs.values()) == 5,
              f"5 commands -> 5 DISTINCT slot pairs, 5 intents (got {len(pairs)} pairs, "
              f"{sum(pairs.values())} intents)")
        labels = sorted(f"{slots.label(a)} -> {slots.label(b)}" for (a, b) in pairs)
        check(len(set(labels)) == 5 and "M5(0) -> M5(0)" not in labels,
              f"and 5 DISTINCT labels, none of them the collapsed self-send: {labels}")
        check(sum(unparsed.values()) == 0,
              f"with nothing unresolved (got {sum(unparsed.values())}: {dict(unparsed)})")
        # ★★ THE MUTATION CONTROL: the id-keyed grammar on the same config.
        old = old_configured_pairs(cfg, name_to_id, hlt)
        old_labels = sorted(f"{T.fmt_node(a, id_to_name)} -> {T.fmt_node(b, id_to_name)}"
                            for (a, b) in old)
        check(len(old) == 2 and "M5(0) -> M5(0)" in old_labels,
              f"★★ CONTROL: id-keyed collapses the same 5 commands onto {len(old)} pairs "
              f"{old_labels} — including the self-send M5(0) -> M5(0), 4 sends on one row. RED")
        check(old.get((0, 0)) == 4,
              f"★★ CONTROL, the match count: {old.get((0, 0))} of the 5 intents pile onto the single "
              f"(0,0) key — the aggregate the register measured as `sent = 6` on `s27`")
    finally:
        os.unlink(cfg_path)


# ==================================================================================================
def test_16_counters_reach_both_outputs():
    """★★ [[B182]] ITEM 7 — the refused/ambiguous counters appear in `--json` AND in the table, with
    the keys ALWAYS PRESENT. ⛔ The `alias_stats` lesson: it was computed, returned and discarded —
    three mentions, zero readers — while being NONZERO on the corpus."""
    print("\n[16] the logical counters are in --json AND in the text table, always present")
    nodes = [_node("H", 17, "0x11110017", host_mobiles=True), _node("S", 30, "0x11110030"),
             _node("M", 60, "0x11110060", is_mobile=True)]
    cmds = [{"at_ms": 100, "node": "S", "command": "send_hash 11110060 to-mobile"},
            {"at_ms": 200, "node": "S", "command": "send_e2e H to-home"}]
    stalled = _adopt(2, 0, 17, 0x11110060, lease=254, t=1000) + [
        emit(1, 100, "tx_enqueue", origin=30, dst=17, ctr=1),
        emit(0, 900, "data_rx", origin=30, dst=17, ctr=1, ctr_lo=1)]
    clean = _adopt(2, 0, 17, 0x11110060, lease=254, t=1000) \
        + _static_to_mobile(100, 1, 30, 0, 17, 2, 1, "to-mobile") \
        + _plain_dm(200, 1, 30, 0, 17, 2, "to-home")
    cfg_path = _mk_cfg(nodes, cmds)
    p_bad, p_ok = write_stream(stalled), write_stream(clean)
    try:
        for name, path, expect_nonzero in (("REFUSING", p_bad, True), ("clean", p_ok, False)):
            tot = _dm_json(cfg_path, path)["totals"]
            missing = [k for k in T.logical_total_keys({}) if k not in tot]
            check(not missing,
                  f"{name}: every logical_* key present in totals (missing: {missing}) — an absent "
                  f"key is indistinguishable from a zero")
            txt = _dm_text(cfg_path, path)
            check("logical identity ([[B182]])" in txt,
                  f"{name}: the TEXT output carries the basis line too")
            nz = any(tot[k] for k in T.LOGICAL_REFUSAL_KEYS)
            check(nz is expect_nonzero,
                  f"{name}: refusals nonzero == {expect_nonzero} (got "
                  f"{ {k: tot[k] for k in T.LOGICAL_REFUSAL_KEYS if tot[k]} }) — the counter is a "
                  f"DISCRIMINATOR, not a constant")
            banner = "LOGICAL CORRELATIONS REFUSED" in txt
            check(banner is expect_nonzero,
                  f"{name}: the loud refusal banner appears == {expect_nonzero}, and the clean run "
                  f"still states '0 logical correlations refused' rather than staying silent")
            if not expect_nonzero:
                check("0 logical correlations refused" in txt,
                      "clean: the ZERO is PRINTED, so a reader can tell 'nothing refused' from "
                      "'nobody looked'")
        # and `--mode all --json` (assembled inline, the path [[B162d]] found bypassing its promise)
        r = _cli(cfg_path, p_bad, ["--mode", "all", "--json"])
        allj = json.loads(r.stdout)
        check(r.returncode == 0 and allj["totals"]["logical_refused_ambiguous_dst"] == 1,
              f"--mode all --json carries the counters too (got "
              f"{allj['totals'].get('logical_refused_ambiguous_dst')})")
    finally:
        os.unlink(p_bad)
        os.unlink(p_ok)
        os.unlink(cfg_path)


# ==================================================================================================
# ★★★★ THE CORPUS GATE. ⛔ IT REFUSES; IT DOES NOT SKIP.
CORPUS = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "simulation")
REQUIRED_CORPUS = ("s21_mobile_dm_milestone_meshroute",
                   "s22_mobile_team_meshroute",
                   "s27_cross_layer_mobiles_meshroute")


def _provision_corpus_streams(outdir):
    """Locate `lus`, RUN it on each required scenario, and return (lus_path, lus_md5, {stem: info}).

    ★★★★ PROVENANCE BY CONSTRUCTION, AND THAT IS THE WHOLE POINT. The previous form of this gate read
    `/tmp/<stem>_analyze.ndjson` — a file it did not produce, whose `lus` it could not name, and whose
    ABSENCE it reported as `SKIPPED` while exiting 0. ⇒ it could neither vouch for a stream nor fail
    without one. Here the streams are generated in this process, from the configs in `simulation/`, by a
    `lus` whose md5 is printed beside every figure.
    ⛔ RAISES on any failure — a missing binary, a missing config, a non-zero exit. The caller turns that
    into FAILED CHECKS, never into a skip. ⚠ A stale `lus` reports the previous arm's streams and looks
    exactly like "nothing moved"; printing its md5 is what makes that visible."""
    try:
        lus = T.resolve_lus(None)
    except SystemExit as e:
        raise RuntimeError(f"cannot locate the `lus` binary ({e}) — the corpus gate CANNOT RUN. "
                           f"Pass $LUS, or run --synthetic-only and say so explicitly (D3)")
    lus_md5 = hashlib.md5(open(lus, "rb").read()).hexdigest()[:8]
    info = {}
    for stem in REQUIRED_CORPUS:
        cfg = os.path.join(CORPUS, stem + ".json")
        if not os.path.exists(cfg):
            raise RuntimeError(f"required scenario config MISSING: {cfg}")
        ev = os.path.join(outdir, stem + ".ndjson")
        r = subprocess.run([lus, cfg, ev], capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(ev):
            raise RuntimeError(f"`lus` failed on {stem} (rc {r.returncode}): "
                               f"{(r.stderr or r.stdout)[-400:]}")
        tail = (r.stdout + r.stderr).strip().splitlines()
        line = next((l for l in reversed(tail) if "events emitted" in l), "")
        info[stem] = {"cfg": cfg, "events": ev,
                      "md5": hashlib.md5(open(ev, "rb").read()).hexdigest()[:8],
                      "lus_line": line.strip()}
    return lus, lus_md5, info


def corpus_gate():
    """★★★ [[B182]] TESTS 1-5 ON THE REAL CORPUS — the exact figures the fix is required to produce.

    Every figure is asserted beside its id-keyed CONTROL computed on the SAME stream, so a green row
    cannot come from a coincidence in the fixture builder."""
    print("\n[CORPUS GATE] the required corpus figures — s21 3/3, s22 2/2 + 2/2 + 4/4, s27 15/15")
    tmpdir = tempfile.mkdtemp(prefix="b182-corpus-")
    try:
        try:
            lus, lus_md5, info = _provision_corpus_streams(tmpdir)
        except RuntimeError as e:
            # ⛔ A FAILED CHECK, NOT A SKIP. This is the D3 line: the gate did not run, so it did not pass.
            check(False, f"⛔ THE CORPUS GATE COULD NOT RUN — reported as a FAILURE, never as a skip: {e}")
            return
        tool_md5 = hashlib.md5(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                 "dm_delivery_breakdown.py"), "rb").read()).hexdigest()[:8]
        print(f"  provenance: lus={lus} md5={lus_md5}  ·  tool md5={tool_md5}")
        for stem, d in info.items():
            print(f"     {stem:<44} stream md5={d['md5']}  {d['lus_line']}")
        check(True, f"★ PROVENANCE: all {len(info)} required streams were GENERATED HERE by lus "
                    f"{lus_md5} (not read from /tmp), and every md5 is printed above")

        want = [
            ("s21_mobile_dm_milestone_meshroute", "Sender(17) -> MobileM(50)", 3, 3, 0),
            ("s22_mobile_team_meshroute", "S2(30) -> M1(60)", 2, 2, 0),
            ("s22_mobile_team_meshroute", "M1(60) -> S2(30)", 2, 2, 0),
            ("s22_mobile_team_meshroute", "TeamA(50) -> TeamC(52)", 4, 4, 4),   # the regression guard
        ]
        for stem, pair, sent, arrived, old_arrived in want:
            cfg_path, ev_path = info[stem]["cfg"], info[stem]["events"]
            row = _row(_dm_json(cfg_path, ev_path), pair)
            check(row is not None and (row["sent"], row["arrived"]) == (sent, arrived),
                  f"{stem}: {pair} = {arrived}/{sent} "
                  f"(got {None if not row else (row['arrived'], row['sent'])})")
            old = old_style_rows(cfg_path, ev_path).get(pair, {})
            check(old.get("arrived") == old_arrived,
                  f"★★ CONTROL on the same stream: id-keyed gives {old.get('arrived')}/"
                  f"{old.get('sent')} for {pair}"
                  + ("  — ★ RIGHT, which is why this row is the REGRESSION GUARD"
                     if old_arrived == arrived else "  — RED, the defect reproduced"))

        stem = "s27_cross_layer_mobiles_meshroute"
        cfg_path, ev_path = info[stem]["cfg"], info[stem]["events"]
        (cfg, id_to_name, name_to_id, slot_to_id, hlt, i2l, slots) = T.load_config(cfg_path)
        pairs, intended, unparsed = T.configured_pairs(cfg, slots, hlt)
        srcs = {slots.names[a] for (a, _b) in pairs} | {slots.names[b] for (_a, b) in pairs}
        check({"M1", "M2", "M3", "M4", "M5"} <= srcs,
              f"s27: all five id-0 mobiles appear as DISTINCT logical endpoints (got {sorted(srcs)})")
        old = old_configured_pairs(cfg, name_to_id, hlt)
        check((0, 0) in old,
              f"★★ CONTROL: id-keyed produces the collapsed self-pair (0,0) with "
              f"{old.get((0, 0))} intents on it — RED")
        # ★★★★ AND THE DELIVERIES, NOT MERELY THE DISTINCTNESS — the gap independent QA flagged: this case
        # used to assert only that the five id-0 mobiles were distinct, which passed while 14 of `s27`'s 15
        # arriving DMs were attributed to their HOMES and the authority scored 1. ⛔ Distinctness is not
        # delivery.
        pj = _dm_json(cfg_path, ev_path)
        got = {f"{r['origin']} -> {r['dst']}": (r["sent"], r["arrived"]) for r in pj["summary"]}
        want27 = {"S1(101) -> M5(0)": (1, 1), "M1(0) -> M2(0)": (3, 3), "M1(0) -> M3(0)": (3, 3),
                  "M1(0) -> M4(0)": (3, 3), "M2(0) -> M1(0)": (1, 1), "M3(0) -> M1(0)": (2, 2),
                  "M4(0) -> M1(0)": (2, 2)}
        check(got == want27,
              f"s27: EXACTLY the 7 intended logical pairs, all fully delivered — no false home-origin row "
              f"(got {got})")
        t = pj["totals"]
        check(t["unique_deliveries"] == 15 and t["configured_sends"] == 15
              and t["raw_delivered_events"] == 15,
              f"s27: 15/15 configured sends delivered, cross-checked against 15 raw `delivered` events "
              f"(got {t['unique_deliveries']}/{t['configured_sends']}, raw {t['raw_delivered_events']})")
        check(t["logical_deleg_reattributed_samelayer"] == 4
              and t["logical_deleg_reattributed_xl"] == 10,
              f"★ MATCH COUNTS on the corpus: 4 same-layer + 10 cross-layer re-attributions — the exact "
              f"count of `deleg_ack_put` (4) and `mobile_delegate_xl` (10) emits in the stream "
              f"(got {t['logical_deleg_reattributed_samelayer']} + "
              f"{t['logical_deleg_reattributed_xl']})")
        check(all(t[k] == 0 for k in T.LOGICAL_REFUSAL_KEYS),
              f"s27: and nothing had to be refused (got "
              f"{ {k: t[k] for k in T.LOGICAL_REFUSAL_KEYS if t[k]} })")
        old_rows = old_style_rows(cfg_path, ev_path)
        agg = old_rows.get("S1(101) -> M5(0)", {})
        coll = old_rows.get("M5(0) -> M5(0)", {})
        check(agg.get("sent") == 6 and agg.get("arrived") == 6
              and coll.get("sent") == 4 and coll.get("arrived") == 0
              and "M1(0) -> M3(0)" not in old_rows,
              f"★★ CONTROL on the same stream: id-keyed produces the register's measured shape exactly — "
              f"the ALIAS AGGREGATE `S1(101) -> M5(0)` {agg.get('arrived')}/{agg.get('sent')} where the "
              f"scenario configures ONE such send, the collapsed self-send `M5(0) -> M5(0)` "
              f"{coll.get('arrived')}/{coll.get('sent')}, and NO `M1(0) -> M3(0)` row at all. RED against "
              f"the 7 pairs / 15 deliveries above")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


# ==================================================================================================
# ★★★★ [[B185]] 2026-08-12 — THE DELEGATED-ORIGINATION TESTS (the third logical-identity break).
#
# ⛔⛔ THE SHAPE: a hosted mobile that cannot resolve its target itself sends a MOBILE_SEND *wrapper* to its
# home, and THE HOME re-originates the DM under its OWN identity. So the `tx_enqueue` emitter is the home and
# the application sender is the mobile — which makes "the emitting slot IS the logical sender" FALSE on this
# path, and it is the reason `s27` scored 1 while all 15 of its configured DMs physically arrived.
# ★ THE FIRMWARE ALREADY NAMES THE LINK (verified at the producers, V1 — an earlier draft of this work claimed
#   it did not, from `tx_enqueue`'s field list alone, without looking for a sibling emit):
#     · `deleg_ack_put{mobile_hash, ctr_h, ctr_m}` — `lib/core/node_hashlocate.cpp:1788`, fired at `:2052`/`:1712`
#     · `mobile_delegate_xl{home, ctr, dst_hash}`  — `lib/core/node_mac.cpp:813`
def _deleg_same_events(mobile_slot, mobile_hash_dec, home_slot, home_id, target_id, ctr_m, ctr_h, t_wrap,
                       t_reorig, recipient_slot, payload, lease=254):
    """One SAME-LAYER delegated origination, in the exact shape the firmware emits.

    ⚠ THE TWO EMITS AT `t_reorig` SHARE THEIR MILLISECOND ON PURPOSE — they are produced in one call chain with
    no clock advance between them (verified on `s27`: all four `deleg_ack_put` share their `tx_enqueue`'s exact
    timestamp), and that exact match is what tells a delegated re-origination from the home's OWN send at the
    same `ctr`."""
    return [
        # the WRAPPER leg, at the MOBILE: an ordinary-looking DM to its own home
        emit(mobile_slot, t_wrap, "tx_enqueue", origin=home_id, dst=home_id, ctr=ctr_m),
        # the RE-ORIGINATION, at the HOME, plus the emit that names the delegating mobile
        emit(home_slot, t_reorig, "tx_enqueue", origin=home_id, dst=target_id, ctr=ctr_h),
        emit(home_slot, t_reorig, "deleg_ack_put", mobile_hash=mobile_hash_dec, ctr_h=ctr_h, ctr_m=ctr_m),
        emit(recipient_slot, t_reorig + 5000, "delivered", origin=home_id, dst=lease, ctr=ctr_h,
             payload=payload)]


def test_18_delegated_origination_is_attributed_to_the_mobile():
    """★★★★ [[B185]] — a delegated DM is credited to the SENDING MOBILE, not to its home; the home's OWN send at
    the same `ctr` is NOT stolen; and the wrapper leg is excluded as transport rather than counted as a DM."""
    print("\n[18] [[B185]] delegated originations: same-layer via deleg_ack_put")
    M1H = 0x2716EFCD
    nodes = [_node("H1", 101, "0x44070011", host_mobiles=True),      # slot 0 — M1's home
             _node("H2", 104, "0x44070014", host_mobiles=True),      # slot 1 — M2's home
             _node("M1", 0, "0x2716EFCD", is_mobile=True),            # slot 2
             _node("M2", 0, "0x3A3E77A3", is_mobile=True),            # slot 3
             _node("H3", 105, "0x44070015")]                          # slot 4 — H1's own addressee
    cmds = [{"at_ms": 220000, "node": "M1", "command": "send_hash 3a3e77a3 deleg-msg"},
            {"at_ms": 900000, "node": "H1", "command": "send_e2e H3 home-own"}]
    ev = _adopt(2, 0, 101, M1H, lease=254, t=1000) \
        + _adopt(3, 1, 104, 0x3A3E77A3, lease=254, t=2000)
    ev += _deleg_same_events(2, M1H, 0, 101, 104, ctr_m=1, ctr_h=5, t_wrap=220000, t_reorig=276322,
                             recipient_slot=3, payload="deleg-msg")
    # ★★ THE HOME'S OWN DM, AT THE SAME `ctr_h` = 5 — `ctr` is per-DESTINATION, so this collision is legal and
    #    REAL: it is exactly what `s27` carries, and a `(home_slot, ctr)` key stole this send from H1.
    ev += _plain_dm(900000, 0, 101, 4, 105, 5, "home-own")
    cfg_path, ev_path = _mk_cfg(nodes, cmds), write_stream(ev)
    try:
        p = _dm_json(cfg_path, ev_path)
        got = {f"{r['origin']} -> {r['dst']}": (r["sent"], r["arrived"]) for r in p["summary"]}
        check(got.get("M1(0) -> M2(0)") == (1, 1),
              f"the delegated DM is credited to the MOBILE: M1 -> M2 = 1/1 "
              f"(got {got.get('M1(0) -> M2(0)')})")
        check(got.get("H1(101) -> H3(105)") == (1, 1),
              f"★ and the HOME'S OWN send at the same ctr is NOT stolen: H1 -> H3 = 1/1 "
              f"(got {got.get('H1(101) -> H3(105)')})")
        check("H1(101) -> M2(0)" not in got,
              f"⛔ NO false home-origin row: H1 -> M2 must not exist (rows: {sorted(got)})")
        t = p["totals"]
        check(t["logical_deleg_reattributed_samelayer"] == 1,
              f"★ MATCH COUNT: exactly 1 same-layer re-attribution "
              f"(got {t['logical_deleg_reattributed_samelayer']})")
        check(t["logical_deleg_wrappers_excluded"] == 1,
              f"★ MATCH COUNT: exactly 1 mobile->home wrapper leg excluded as transport "
              f"(got {t['logical_deleg_wrappers_excluded']})")
        check(t["unique_deliveries"] == 2 and t["configured_sends"] == 2,
              f"2 configured sends, 2 deliveries — the wrapper adds NO third send "
              f"(got {t['unique_deliveries']}/{t['configured_sends']})")
        check(all(t[k] == 0 for k in T.LOGICAL_REFUSAL_KEYS),
              "and nothing refused on a determinate fixture")

        # ★★ MUTATION CONTROL 1 — the emit removed. This is the pre-[[B185]] state exactly: the delivery is
        #    attributed to the HOME, so a false home-origin row appears and the mobile's pair reads 0.
        ev_no = [e for e in ev if e.get("emit_type") != "deleg_ack_put"]
        p_no = write_stream(ev_no)
        try:
            g = {f"{r['origin']} -> {r['dst']}": (r["sent"], r["arrived"])
                 for r in _dm_json(cfg_path, p_no)["summary"]}
            tn = _dm_json(cfg_path, p_no)["totals"]
            check(g.get("M1(0) -> M2(0)") == (1, 0) and tn["logical_deleg_reattributed_samelayer"] == 0,
                  f"★★ CONTROL: without `deleg_ack_put` the mobile's pair reads "
                  f"{g.get('M1(0) -> M2(0)')} — RED, the pre-[[B185]] defect reproduced")
        finally:
            os.unlink(p_no)

        # ★★ MUTATION CONTROL 2 — the TIME-BLIND key, i.e. `(home_slot, ctr_h)` with no timestamp. ⚠ This is not
        #    a hypothetical mutation: it is what this code did on its first pass, and it dropped `s27`'s
        #    `S1(101) -> M5(0)` from 1/1 to 0/1 by stealing the home's own send.
        (cfg, i2n, n2i, s2i, hlt, i2l, slots) = T.load_config(cfg_path)
        out = T.analyse(ev_path, s2i, hlt, i2l, slots)
        msgs, hosted_by, logical, deleg = out[0], out[9], out[10], out[12]
        pairs, intended, _u = T.configured_pairs(cfg, slots, hlt, out[11])
        blind = {"xl": deleg["xl"], "same": {}}
        for (sl, ch, _t), ms in deleg["same"].items():
            for r in msgs.values():                     # spread the claim over EVERY enqueue at that (slot, ctr)
                if r["src_slot"] == sl and r["ctr"] == ch:
                    blind["same"].setdefault((sl, ch, r["enqueued_ms"]), set()).update(ms)
        T.assign_logical_pairs(msgs, slots, hosted_by, intended, T.Counter(), blind)
        rows = T.summarise(msgs, pairs, slots, {}, intended)
        gb = {f"{r['origin']} -> {r['dst']}": (r["sent"], r["arrived"]) for r in rows}
        check(gb.get("H1(101) -> H3(105)") == (1, 0),
              f"★★ CONTROL: the TIME-BLIND `(slot, ctr)` key steals the home's own send — H1 -> H3 reads "
              f"{gb.get('H1(101) -> H3(105)')} instead of 1/1. RED, and this is the `s27` regression that "
              f"made the timestamp term load-bearing")
    finally:
        os.unlink(cfg_path)
        os.unlink(ev_path)


# ==================================================================================================
def test_19_delegated_xl_and_its_refusal():
    """★★★★ [[B185]] cross-layer half, via `mobile_delegate_xl` — plus the AMBIGUITY REFUSAL, driven off zero.

    ★ THE REFUSAL RULE IS ABOUT THE SENDER'S IDENTITY, NOT THE PAIRING, and that distinction is MEASURED: `s27`'s
    M1 delegates to M3 three times, so three unconsumed delegates fit each re-origination. A blanket
    "more than one candidate ⇒ refuse" read `M1 -> M3` as 1/3. ⇒ candidates that all name the SAME mobile leave
    the sender fully determined; ⛔ only candidates naming DIFFERENT mobiles are refused."""
    print("\n[19] [[B185]] delegated cross-layer originations + the ambiguity refusal")
    nodes = [_node("H", 101, "0x44070011", layer_id=4, host_mobiles=True),   # slot 0 — the home
             _node("GW", 10, "0x44070001", layer_id=4, is_gateway=True),      # slot 1
             _node("M1", 0, "0x2716EFCD", is_mobile=True, layer_id=4),        # slot 2
             _node("M2", 0, "0x3A3E77A3", is_mobile=True, layer_id=4),        # slot 3 — 2nd delegator
             _node("T", 111, "0x44070021", layer_id=7)]                       # slot 4 — the far target
    cmds = [{"at_ms": 240000, "node": "M1", "command": "send_layer 7 1141309473 xl-a"},
            {"at_ms": 260000, "node": "M1", "command": "send_layer 7 1141309473 xl-b"}]
    base = _adopt(2, 0, 101, 0x2716EFCD, lease=254, t=1000) \
        + _adopt(3, 0, 101, 0x3A3E77A3, lease=253, t=1500)

    def xl_pair(t_del, ctr_m, ctr_h, payload, delegator_slot):
        return [emit(delegator_slot, t_del, "mobile_delegate_xl", home=101, ctr=ctr_m,
                     enclosed_type=15, dst_hash=0x44070021),
                emit(delegator_slot, t_del, "tx_enqueue", origin=101, dst=101, ctr=ctr_m),
                emit(0, t_del + 778, "tx_enqueue_xl", origin=101, dst=10, ctr=ctr_h, target_layer=7),
                emit(4, t_del + 30000, "delivered", origin=101, dst=111, ctr=ctr_h, payload=payload)]

    ok_ev = base + xl_pair(240000, 2, 1, "xl-a", 2) + xl_pair(260000, 3, 2, "xl-b", 2)
    # ★★ the AMBIGUOUS fixture: TWO DIFFERENT mobiles delegate to the SAME target at the SAME home, so which of
    #    them a re-origination belongs to is genuinely undetermined -> REFUSE.
    amb_ev = base + [emit(2, 240000, "mobile_delegate_xl", home=101, ctr=2, enclosed_type=15,
                          dst_hash=0x44070021),
                     emit(2, 240000, "tx_enqueue", origin=101, dst=101, ctr=2),
                     emit(3, 240010, "mobile_delegate_xl", home=101, ctr=9, enclosed_type=15,
                          dst_hash=0x44070021),
                     emit(3, 240010, "tx_enqueue", origin=101, dst=101, ctr=9),
                     emit(0, 240778, "tx_enqueue_xl", origin=101, dst=10, ctr=1, target_layer=7),
                     emit(4, 270000, "delivered", origin=101, dst=111, ctr=1, payload="xl-a")]
    cfg_path = _mk_cfg(nodes, cmds)
    p_ok, p_amb = write_stream(ok_ev), write_stream(amb_ev)
    try:
        pj = _dm_json(cfg_path, p_ok)
        got = {f"{r['origin']} -> {r['dst']}": (r["sent"], r["arrived"]) for r in pj["summary"]}
        check(got.get("M1(0) -> T(111)") == (2, 2),
              f"BOTH delegated cross-layer DMs credited to M1: 2/2 (got {got.get('M1(0) -> T(111)')})")
        check("H(101) -> T(111)" not in got,
              f"⛔ NO false home-origin row (rows: {sorted(got)})")
        t = pj["totals"]
        check(t["logical_deleg_reattributed_xl"] == 2,
              f"★ MATCH COUNT: exactly 2 cross-layer re-attributions "
              f"(got {t['logical_deleg_reattributed_xl']})")
        check(t["logical_deleg_wrappers_excluded"] == 2,
              f"★ MATCH COUNT: 2 wrapper legs excluded (got {t['logical_deleg_wrappers_excluded']})")
        check(t["logical_deleg_refused_ambiguous"] == 0,
              f"★ and NOTHING refused: two delegates from ONE mobile leave the sender determined "
              f"(got {t['logical_deleg_refused_ambiguous']}) — ⛔ a blanket >1 refusal read this 1/2")
        ta = _dm_json(cfg_path, p_amb)["totals"]
        ga = {f"{r['origin']} -> {r['dst']}": (r["sent"], r["arrived"])
              for r in _dm_json(cfg_path, p_amb)["summary"]}
        check(ta["logical_deleg_refused_ambiguous"] == 1
              and ta["logical_deleg_reattributed_xl"] == 0,
              f"★★ TWO DIFFERENT delegating mobiles for one re-origination -> REFUSED "
              f"(refused={ta['logical_deleg_refused_ambiguous']}, "
              f"attributed={ta['logical_deleg_reattributed_xl']}) — ⛔ not assigned to either")
        check(ga.get("M1(0) -> T(111)", (0, 0))[1] == 0,
              f"⇒ and the refused record is credited to NO pair (M1 -> T reads "
              f"{ga.get('M1(0) -> T(111)')}), while its configured sends stay in the denominator")
    finally:
        os.unlink(p_ok)
        os.unlink(p_amb)
        os.unlink(cfg_path)


# ==================================================================================================
def _isolated(fn):
    """Run `fn()` with the global check ledger snapshotted; return (checks, failures, messages).

    ⓘ Needed because the case below exercises `corpus_gate()` ITSELF, and its deliberate failures must
    not be counted against the run."""
    c0, f0 = CHECKS[0], len(FAILURES)
    # ⚠ stdout is swallowed: the patched gate below prints its own `FAIL:` lines by design, and leaving
    #   them in the log would read as a real failure of this run — the opposite of the clarity this case is
    #   for. The outcome is asserted from the ledger delta instead, and reported by this case's own checks.
    with contextlib.redirect_stdout(io.StringIO()):
        fn()
    msgs = FAILURES[f0:]
    n, nf = CHECKS[0] - c0, len(FAILURES) - f0
    del FAILURES[f0:]
    CHECKS[0] = c0
    return n, nf, msgs


def test_20_the_corpus_gate_refuses_rather_than_skips():
    """★★★★ THE GATE'S OWN POSITIVE CONTROL — [[B182]]/QG 2026-08-12.

    ⛔⛔ WHAT THIS EXISTS TO STOP RECURRING: the corpus cases used to read `/tmp/<stem>_analyze.ndjson` and
    print `SKIPPED` when a stream was absent. Independent QA hid every stream: the run reported **ZERO
    FAILURES and EXITED 0** on two structural checks. ⇒ *"a skipped gate must never look passed"* (D3),
    violated inside the file whose whole subject is instruments that fail toward "nothing happened".
    ★ So the gate's INABILITY TO RUN is asserted here to be a FAILURE, twice over — once for a missing
    `lus`, once for a missing scenario config — because an unrunnable gate that returns success is worse
    than one that refuses."""
    print("\n[20] the CORPUS GATE records a FAILURE when it cannot run (it cannot silently skip)")
    saved_resolve, saved_corpus = T.resolve_lus, globals()["CORPUS"]
    try:
        # (a) `lus` cannot be located at all.
        T.resolve_lus = lambda explicit: (_ for _ in ()).throw(SystemExit("no lus for this test"))
        n, nf, msgs = _isolated(corpus_gate)
        check(nf == 1 and n == 1 and "COULD NOT RUN" in msgs[0],
              f"★★ no `lus` -> exactly ONE check, and it FAILS ({n} checks / {nf} failed), message names "
              f"the refusal: {msgs[0][:80] if msgs else '(none)'}…")
        check("skip" in (msgs[0].lower() if msgs else ""),
              "★ and the message says explicitly that it is NOT a skip, so a reader of the log cannot "
              "mistake it for one")
        # (b) `lus` resolves but the corpus is absent.
        T.resolve_lus = saved_resolve
        globals()["CORPUS"] = "/nonexistent-corpus-dir-b182"
        n2, nf2, msgs2 = _isolated(corpus_gate)
        check(nf2 == 1 and n2 == 1 and "MISSING" in msgs2[0],
              f"★★ missing scenario config -> a FAILED check too ({n2} checks / {nf2} failed): "
              f"{msgs2[0][:80] if msgs2 else '(none)'}…")
        # ★ CONTROL ON THE CONTROL: unpatched, the same gate runs its full census and passes — so the two
        #   reds above are the patches, not a gate that always fails.
        globals()["CORPUS"] = saved_corpus
        n3, nf3, _m = _isolated(corpus_gate)
        check(n3 > 10 and nf3 == 0,
              f"★ CONTROL ON THE CONTROL: unpatched, the gate runs {n3} checks with {nf3} failures — so "
              f"the refusals above are the patches, not a permanently-red gate")
    finally:
        T.resolve_lus = saved_resolve
        globals()["CORPUS"] = saved_corpus


# ==================================================================================================
def run_suite(name, fns):
    """Run a group of cases and record ITS OWN counts. ★ The two suites are never merged into one total:
    that merging is exactly how "185 checks, 0 failed" could have meant "2 checks ran"."""
    c0, f0 = CHECKS[0], len(FAILURES)
    for fn in fns:
        fn()
    SUITES.append((name, CHECKS[0] - c0, len(FAILURES) - f0))


SYNTHETIC_CASES = (
    test_1_deferred_frame_is_counted,
    test_2_mixed_coding_rate,
    test_3_same_length_key_collision,
    test_4_runtime_id_alias_conflict_is_surfaced,
    test_5_length_formula_agrees_with_the_phy,
    test_5b_sf5_sf6_sx126x_case,
    test_6_fail_loud_paths,
    test_7_double_attribution_is_refused,
    test_8_whitespace_independent_parsing,
    test_9_malformed_line_is_refused_counted_and_surfaced,
    test_10_the_refusal_promise_is_enforced_by_topology,
    test_11_every_mode_announces_refusals_beside_a_nonzero_figure,
    test_12_hosted_mobile_both_directions,
    test_13_one_home_id_two_logical_senders_concurrently,
    test_14_ambiguity_is_refused_not_guessed,
    test_15_five_id_zero_mobiles_stay_distinct,
    test_16_counters_reach_both_outputs,
    test_18_delegated_origination_is_attributed_to_the_mobile,
    test_19_delegated_xl_and_its_refusal,
    test_20_the_corpus_gate_refuses_rather_than_skips,
)
CORPUS_CASES = (corpus_gate,)


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("--synthetic-only", action="store_true",
                    help="run ONLY the stdlib/no-simulator suite. ⛔ The corpus gate is then NOT RUN, and "
                         "this is stated in the summary — it is not a pass.")
    args = ap.parse_args()
    print("=== [[B162b]]/[[B162c]]/[[B162d]]/[[B182]]/[[B185]] durable tests for "
          "tools/dm_delivery_breakdown.py ===")
    run_suite("SYNTHETIC (stdlib only, no corpus, no simulator)", SYNTHETIC_CASES)
    if not args.synthetic_only:
        run_suite("CORPUS GATE (self-provisioned streams; refuses rather than skips)", CORPUS_CASES)
    print()
    for name, n, f in SUITES:
        print(f"{name}: {n} checks, {f} failed")
    if args.synthetic_only:
        print("⛔ CORPUS GATE NOT RUN (--synthetic-only). This run does NOT certify the corpus figures "
              "(D3: a skipped gate is not a passed one).")
    print(f"TOTAL: {CHECKS[0]} checks, {len(FAILURES)} failed")
    if FAILURES:
        for f in FAILURES:
            print(f"  !! {f}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
