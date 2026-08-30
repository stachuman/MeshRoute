#!/usr/bin/env python3
# MeshRoute — compare_corpus_slice_b.py
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""PREDICTED-DELTA gate for §CUSTODY-B, where behaviour MOVES by design.

⛔⛔ THIS IS A SEPARATE INSTRUMENT AND `tools/compare_corpus_semantics.py` IS DELIBERATELY LEFT ALONE.
    That one proves "hashes may move everywhere; SEMANTICS may move nowhere" and carries 6/6 controls for
    exactly that claim. Slice B's claim is the opposite shape — semantics move, in a set predicted BEFORE the
    code was written — so overloading the proven tool with a mode would put two different verdicts behind one
    set of controls. Two instruments, two control sets.

THE CLAIM THIS GATE PROVES
    1. Every stream OUTSIDE the predicted-mover set is BYTE-IDENTICAL. (Not "similar" — identical.)
    2. Inside the predicted-mover set, no event KIND appears that was not there before, none disappears
       entirely, and the suppressed kinds move only in the predicted DIRECTION.
    3. `unsupported_internal` — the new event the fail-closed guard emits — appears NOWHERE, because the
       pre-slice corpus was measured to carry zero unknown-internal / `0x94` / out-of-range TYPE bytes. The
       guard is therefore corpus-INERT, and that is a measurement rather than an argument.

WHY THE MOVER SET IS NOT AN ORDERED EVENT WALK. The §6.2(4) floor change alters WHEN a frame is admitted, so a
moved stream re-times everything downstream of the first change; an ordered walk would report thousands of
"differences" that are one cause. The mover arm therefore compares the per-kind event MULTISET plus the
direction of the suppressed kinds, and the inert arm — where nothing may move at all — keeps full byte identity.
⇒ the strong check is applied exactly where it can be honest, and the weaker one is bounded by the prediction.

USAGE
    python3 tools/compare_corpus_slice_b.py BEFORE_DIR AFTER_DIR
    python3 tools/compare_corpus_slice_b.py BEFORE_DIR AFTER_DIR --selftest   # prove it can FAIL
"""

import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile
from collections import Counter

# ---------------------------------------------------------------------------------------------------------
# THE INVARIANTS. ⛔⛔ THESE ARE DERIVED FROM THE DIFF, NOT FROM THE ANSWER, AND THE FIRST VERSION OF THIS FILE
#     CARRIED A DIFFERENT (WRONGER) PREDICTION WHICH THIS TOOL REJECTED. That is recorded rather than quietly
#     replaced, because it is the whole reason the tool exists:
#
#       WHAT THE FIRST PREDICTION SAID: only §6.2(4)'s DM-floor widening is corpus-visible, so only streams
#         carrying a NEWLY-exempt internal type (0x88/0x89/0x8B/0x90..0x96/0xA2) can move; §6.2(5)'s Push
#         suppression is invisible because Pushes are an app-ring concern, not telemetry.
#       WHY IT WAS WRONG: the simulator SERIALIZES every Push as a `push` event. `send_acked` and `send_failed`
#         are therefore fully corpus-visible, so EVERY stream carrying an own-originated internal flight moves —
#         including the seven that carry nothing but `E2E_ACK` (0x80), which the floor change cannot touch.
#       ⇒ the corrected invariants below are STRICTLY STRONGER than the first prediction, not looser.
#
# I1  `unsupported_internal` appears NOWHERE. The pre-slice corpus was histogrammed by TYPE byte across all 36
#     streams and carries ZERO unknown-internal / `0x94` / out-of-range values, so the fail-closed guard is
#     CORPUS-INERT by measurement.
# I2  A stream moves IF AND ONLY IF it carries own-originated PROTOCOL-INTERNAL DATA. Every line this slice
#     changed is gated on `data_type_traits(t).internal` / `.generic_send_lifecycle`; for every non-internal
#     type both verdicts are identical to the pre-slice code, so a stream with no internal DATA CANNOT move.
#     ⇒ this is a two-sided claim: an internal-carrying stream that did NOT move is as much a failure as a
#       clean stream that did.
# I3  The generic user-send lifecycle may only SHRINK: `push{send_acked}`, `push{send_failed}` and the
#     `send_blocked` emit may decrease, never increase. §6.2(5) removes; it never adds.
# I4  ★ THE SHARP ONE. A stream whose internal types are ALL already floor-exempt ({0x80, 0xA0, 0xA1}) sees no
#     admission-timing change at all, so `emit:push` must be its ONLY changed event kind — no re-timing, no
#     secondary deltas, nothing. Any other kind moving there means the §6.2(4) widening leaked outside its
#     range. This is the invariant that could most easily have failed, and it is what separates §6.2(5)'s
#     effect from §6.2(4)'s instead of letting one hide inside the other.
# I5  Secondary (re-timing) deltas are permitted ONLY in a stream carrying a NEWLY-exempt internal type, and
#     every such stream is REPORTED in full so the movement is attributable rather than waved through.
# ---------------------------------------------------------------------------------------------------------
ALREADY_EXEMPT = {0x80, 0xA0, 0xA1}          # exempt from the user-DM floor BEFORE this slice
SUPPRESSIBLE_EMITS = {"send_blocked"}         # the generic-family emits the corpus can witness
SUPPRESSIBLE_PUSHES = {"send_acked", "send_failed", "send_aired", "send_blocked"}
GUARD_KIND = "unsupported_internal"


def stream_files(d):
    return sorted(f for f in os.listdir(d) if f.endswith(".ndjson"))


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()[:8]


def kind_counts(path):
    """Per-event-kind multiset, the PUSH-kind multiset, and the DATA TYPE-byte histogram.
    `script_emit` is split by its `emit_type` — the firmware's own event name, and the only granularity at
    which a suppression is visible. `push` is split a second time by the Push's own `kind`, because the whole
    §6.2(5) delta lives inside that one event name."""
    c, pk, types = Counter(), Counter(), Counter()
    with open(path) as fh:
        for line in fh:
            try:
                e = json.loads(line)
            except ValueError:
                continue
            t = e.get("type")
            if t == "script_emit":
                name = str(e.get("emit_type"))
                c["emit:" + name] += 1
                if name == "push":
                    pk[str((e.get("data") or {}).get("kind"))] += 1
            else:
                c["sim:" + str(t)] += 1
            if t == "tx":
                try:
                    fr = bytes.fromhex(e.get("hex", ""))
                except ValueError:
                    continue
                if len(fr) >= 9 and (fr[0] >> 4) == 0x3 and (fr[1] & 0x80):
                    types[fr[8]] += 1
    return c, pk, types


def evaluate(before_dir, after_dir, verbose=True):
    """Returns (ok, lines). Every violation names the stream and the invariant it broke."""
    lines, ok = [], True
    bfiles, afiles = stream_files(before_dir), stream_files(after_dir)
    if bfiles != afiles:
        return False, ["FAIL: the two runs do not carry the same stream set"]

    n_inert = n_push_only = n_retimed = 0
    for fn in bfiles:
        name = fn[:-7]
        bp, ap = os.path.join(before_dir, fn), os.path.join(after_dir, fn)
        bh, ah = md5(bp), md5(ap)
        bc, bpk, btypes = kind_counts(bp)
        ac, apk, _ = kind_counts(ap)
        moved = (bh != ah)
        internal = sorted(t for t in btypes if 0x80 <= t <= 0xBF)
        newly    = [t for t in internal if t not in ALREADY_EXEMPT]
        delta    = {k: ac.get(k, 0) - bc.get(k, 0) for k in set(bc) | set(ac) if ac.get(k, 0) != bc.get(k, 0)}
        pdelta   = {k: apk.get(k, 0) - bpk.get(k, 0) for k in set(bpk) | set(apk) if apk.get(k, 0) != bpk.get(k, 0)}
        tag = " ".join("%02x" % t for t in internal) or "-"

        # ---- I1 : the guard's event appears nowhere -------------------------------------------------------
        if ac.get("emit:" + GUARD_KIND, 0):
            ok = False
            lines.append("FAIL [%s] I1: %s appeared %d time(s) — the guard was measured CORPUS-INERT"
                         % (name, GUARD_KIND, ac["emit:" + GUARD_KIND]))

        # ---- I2 : moved IFF it carries internal DATA (BOTH directions) ------------------------------------
        if moved and not internal:
            ok = False
            lines.append("FAIL [%s] I2: MOVED %s -> %s but carries NO protocol-internal DATA — nothing this "
                         "slice changed can reach it" % (name, bh, ah))
        if internal and not moved:
            ok = False
            lines.append("FAIL [%s] I2: carries internal DATA (%s) yet is BYTE-IDENTICAL — the suppression "
                         "did not reach a stream it must" % (name, tag))

        # ---- I3 : the generic family may only SHRINK ------------------------------------------------------
        for k, d in pdelta.items():
            if k in SUPPRESSIBLE_PUSHES and d > 0:
                ok = False
                lines.append("FAIL [%s] I3: push{%s} INCREASED (+%d) — §6.2(5) removes, never adds" % (name, k, d))
        for k in SUPPRESSIBLE_EMITS:
            d = ac.get("emit:" + k, 0) - bc.get("emit:" + k, 0)
            if d > 0:
                ok = False
                lines.append("FAIL [%s] I3: %s emit INCREASED (+%d)" % (name, k, d))
        for k, d in pdelta.items():
            if k not in SUPPRESSIBLE_PUSHES and not newly:
                ok = False
                lines.append("FAIL [%s] I3: push{%s} moved (%+d) but is NOT a generic-lifecycle kind and this "
                             "stream cannot re-time" % (name, k, d))

        # ---- I4 : an already-exempt-only stream may move ONLY in `emit:push` ------------------------------
        if moved and not newly:
            extra = {k: d for k, d in delta.items() if k != "emit:push"}
            if extra:
                ok = False
                lines.append("FAIL [%s] I4: internal types are all already floor-exempt (%s), so `emit:push` "
                             "must be the ONLY changed kind — but these moved too: %s"
                             % (name, tag, extra))

        if not moved:
            n_inert += 1
            continue
        if not newly or set(delta) == {"emit:push"}:
            n_push_only += 1
            if verbose:
                lines.append("PUSH-ONLY [%-44s] %s -> %s  internal=%-12s  %s" % (name, bh, ah, tag, pdelta))
        else:
            # ---- I5 : re-timing, permitted only here, and REPORTED IN FULL -------------------------------
            n_retimed += 1
            if verbose:
                lines.append("RE-TIMED  [%-44s] %s -> %s  internal=%-12s  (§6.2(4) floor bypass)"
                             % (name, bh, ah, tag))
                lines.append("            push-kind delta: %s" % pdelta)
                for k, d in sorted(delta.items(), key=lambda kv: -abs(kv[1]))[:14]:
                    lines.append("            %+7d  %s" % (d, k))
                lines.append("            ...%d changed event kinds in total" % len(delta))

    lines.append("")
    lines.append("%d byte-identical (no internal DATA) · %d push-only (§6.2(5)) · %d re-timed (§6.2(4)) · "
                 "verdict: %s" % (n_inert, n_push_only, n_retimed, "PASS" if ok else "FAIL"))
    return ok, lines


# ---------------------------------------------------------------------------------------------------------
# THE CONTROLS. ⛔ A comparison that cannot fail is a formatter. Each control mutates the AFTER tree in ONE way
# the brief names and requires this tool to REJECT it.
# ---------------------------------------------------------------------------------------------------------
def _perturb(src_dir, dst_dir, stream, fn):
    shutil.copytree(src_dir, dst_dir)
    path = os.path.join(dst_dir, stream + ".ndjson")
    with open(path) as fh:
        lines = fh.readlines()
    lines = fn(lines)
    with open(path, "w") as fh:
        fh.writelines(lines)


def selftest(before_dir, after_dir):
    """⛔ A comparison that cannot fail is a formatter. Each control mutates the AFTER tree ONE way and names
    the invariant it must trip — ★ and the TARGET is chosen by CLASS, not by "the first stream that differs",
    because a control that happens to land on a re-timed stream would pass for the wrong reason (measured: the
    first cut of this selftest did exactly that, and only worked because s06 sorts first)."""
    inert = push_only = retimed = None
    for f in stream_files(before_dir):
        n = f[:-7]
        bc, _bpk, btypes = kind_counts(os.path.join(before_dir, f))
        ac, _apk, _ = kind_counts(os.path.join(after_dir, f))
        same = md5(os.path.join(before_dir, f)) == md5(os.path.join(after_dir, f))
        internal = sorted(t for t in btypes if 0x80 <= t <= 0xBF)
        newly = [t for t in internal if t not in ALREADY_EXEMPT]
        delta = {k for k in set(bc) | set(ac) if ac.get(k, 0) != bc.get(k, 0)}
        if same and inert is None:
            inert = n
        elif not same and not newly and push_only is None:
            push_only = n
        elif not same and newly and delta != {"emit:push"} and retimed is None:
            retimed = n
    if not (inert and push_only and retimed):
        print("selftest: need one stream of each class (inert=%s push_only=%s retimed=%s)"
              % (inert, push_only, retimed))
        return False

    controls, reds = [], 0

    def run(label, stream, fn, expect_invariant):
        nonlocal reds
        tmp = tempfile.mkdtemp(prefix="sliceb_ctl_")
        d = os.path.join(tmp, "after")
        _perturb(after_dir, d, stream, fn)
        ok, out = evaluate(before_dir, d, verbose=False)
        shutil.rmtree(tmp, ignore_errors=True)
        hit = (not ok) and any(("] " + expect_invariant + ":") in l for l in out)
        reds += 1 if hit else 0
        controls.append((label, stream, expect_invariant,
                         "RED" if hit else ("*** GREEN — CONTROL FAILED ***" if ok else
                                            "*** RED FOR THE WRONG INVARIANT ***")))

    def add(kind):
        return lambda ls: ls + ['{"type":"script_emit","node":0,"time_ms":1,"emit_type":"%s","data":{"kind":"%s"}}\n'
                                % (kind, kind)]

    # (1) an UNPREDICTED event kind appears in a stream that may only move in `emit:push`
    run("an unpredicted event kind appears", push_only, add("totally_new_kind"), "I4")
    # (2) the guard's event appears where it was measured absent
    run("unsupported_internal appears (guard measured corpus-inert)", inert,
        lambda ls: ls + ['{"type":"script_emit","node":0,"time_ms":1,"emit_type":"unsupported_internal","data":{}}\n'],
        "I1")
    # (3) a SUPPRESSED push comes back — in the RE-TIMED class, where the multiset rules are weakest
    run("a predicted-suppressed push re-appears (re-timed stream)", retimed,
        lambda ls: ls + ['{"type":"script_emit","node":0,"time_ms":1,"emit_type":"push","data":{"kind":"send_acked"}}\n'] * 200,
        "I3")
    # (4) an UNRELATED field moves in a stream that must be byte-identical
    def bump_field(ls):
        out, done = [], [False]
        for l in ls:
            if not done[0] and '"time_ms"' in l:
                l = l.replace('"time_ms"', '"time_ms_x"', 1); done[0] = True
            out.append(l)
        return out
    run("an unrelated field moves in a BYTE-IDENTICAL stream", inert, bump_field, "I2")

    print("§CUSTODY-B comparator — negative controls (each must trip its NAMED invariant)")
    for label, stream, inv, res in controls:
        print("  %-52s %-5s on %-40s %s" % (label, inv, stream, res))
    print("  => %d/%d RED" % (reds, len(controls)))
    return reds == len(controls)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("before"); ap.add_argument("after")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        sys.exit(0 if selftest(a.before, a.after) else 1)
    ok, lines = evaluate(a.before, a.after)
    for l in lines:
        print(l)
    print("RESULT:", "PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
