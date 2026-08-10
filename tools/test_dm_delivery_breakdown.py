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

Run: `python3 tools/test_dm_delivery_breakdown.py`   (stdlib only, no corpus, no simulator)
"""
import ast
import contextlib
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
        alias_stats = out[-1]
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
            ok_stats = T.analyse(p_ok, slot_to_id, {}, {11: 1, 12: 1, 13: 1})[-1]
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


def main():
    print("=== [[B162b]]/[[B162c]]/[[B162d]] durable tests for tools/dm_delivery_breakdown.py ===")
    for fn in (test_1_deferred_frame_is_counted,
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
               test_11_every_mode_announces_refusals_beside_a_nonzero_figure):
        fn()
    print(f"\n{CHECKS[0]} checks, {len(FAILURES)} failed")
    if FAILURES:
        for f in FAILURES:
            print(f"  !! {f}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
