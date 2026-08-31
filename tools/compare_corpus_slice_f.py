#!/usr/bin/env python3
# MeshRoute — compare_corpus_slice_f.py
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""§CUSTODY-F CORPUS ACCOUNTANT — every custody notice attributed to an INDEPENDENTLY DERIVED failed carrier.

★★ WHAT THIS ANSWERS, AND WHAT IT DELIBERATELY DOES NOT. Slice F is the first traffic-ADDING slice of the
custody arc, so the AFTER streams are not the BEFORE streams minus lines (§CUSTODY-E's shape) nor equal to them
(§CUSTODY-A/B's shapes). New frames air. ⇒ the question is no longer "what moved" but **"is every new frame one
this node was entitled to send, and did nothing else appear?"**

⛔⛔ THE TWO PROOF DIRECTIONS ARE SPLIT, AND THE SPLIT IS THE POINT (QG correction 4 to the dispatch brief):
    a generated notice CANNOT prove its own eligibility, because both the notice and the eligibility decision come
    from the same AFTER binary. So:
      · **THIS TOOL proves `enqueued  ⇒ eligible`** — every observed notice is matched against facts derived
        from events this slice neither emits nor shapes (see the method correction below).
      · **The native cases and the mutation battery prove `eligible ⇒ enqueued`** — §10.1's twelve terms, each
        both ways, plus one mutation per term. That direction is not attempted here and must not be claimed.

⛔⛔ AND A METHOD CORRECTION THIS FILE OWES ITS FIRST CUT, RECORDED BECAUSE IT IS THE WHOLE DIFFICULTY OF A
    TRAFFIC-ADDING SLICE: **the BEFORE stream cannot be a per-event oracle for the AFTER run.** The first notice
    that airs occupies airtime, so from that instant the two simulations DIVERGE — later flights, later
    failures and later terminals in the AFTER arm have no BEFORE counterpart, and demanding one produced 11
    "unattributed" notices that were all perfectly legitimate. ⇒ attribution is done in TWO layers, and neither
    ever asks the custody code to vouch for itself:
      · **THE PREFIX IS BEFORE-ANCHORED.** Every moved stream must be BYTE-IDENTICAL to the BEFORE arm up to its
        FIRST custody line, and that first differing line must itself be custody-caused. That is what proves the
        divergence STARTS at the notice rather than somewhere the slice cannot explain.
      · **EVERY NOTICE IS ATTRIBUTED TO CUSTODY-INDEPENDENT FACTS.** The derivation below is re-applied to the
        AFTER stream using ONLY events that existed before this slice — `data_rx`, `nack_rx`, `e2e_ack_tx`,
        `path_cascade_exhausted` — none of which the custody code emits or influences in content. A notice must
        sit at a node that, in the SAME stream, had a selected TRANSIT terminal for that failed origin.

★ THE INDEPENDENT DERIVATION (applied to BOTH arms — to BEFORE for the census and the prediction, to AFTER for
  the per-notice attribution):
    ① A **SELECTED TERMINAL** is a `path_cascade_exhausted` that did NOT come from the NACK receive path.
      ⛔⛔ THIS DISTINCTION IS THE SINGLE MOST IMPORTANT LINE IN THIS FILE AND IT COST THIS SLICE ITS FIRST
         PREDICTION. `path_cascade_exhausted` is emitted at SIX sites, not three: the three §CUSTODY-E selected
         cascade terminals (`node_cascade.cpp`) **and three §10.2 DEFERRED sites in `node_mac_rx.cpp`** — the
         loop-duplicate NACK giveup (:2384), the long-busy requeue's queue-full arm (:2421) and the hop-budget
         NACK terminal (:2458). All three of the latter are on §10.2's explicitly-deferred list, use plain
         `giveup_flight`/`terminal_carrier_outcome`, and generate NOTHING. Counting them as candidates
         over-predicted the corpus by roughly a factor of two. They are told apart by the `nack_rx` this node
         emits in the same millisecond, immediately before — which is also this tool's §10.2 VERIFICATION.
      ⛔⛔ AND THE NACK TEST IS BY **REASON**, NOT BY PRESENCE — the second correction this file owes its own
         first cut. `handle_nack`'s DUTY-BUDGET arm (`nack_reason_budget` = 1) is a **SELECTED** terminal: it
         calls `try_cascade_requeue`, and §CUSTODY-E's banner names it as the fifth entry point. Only
         `busy_rx` (0), `hop_budget` (2) and `loop_dup` (3) reach the three §10.2 receive-path terminals.
         Treating any NACK-coincident exhaustion as deferred wrongly rejected two perfectly legitimate
         notices — the exact shape of defect this tool exists to prevent, found in the tool itself.
    ② A terminal is **TRANSIT** if that node previously received a DATA for the same `{dst, ctr}` whose `dst`
      is not itself — i.e. it accepted custody and installed a forward. `data_rx` carries `origin` and `from`,
      which are exactly §9.2's `failed_origin` and `previous_hop`.
    ③ A flight is an **E2E ACK** if its origin emitted `e2e_ack_tx{dst, ctr}` for it (§10.1(11) excludes it).
    ⓘ THE DERIVED TERMINAL SET IS A **SUPERSET** OF THE PRODUCTION ONE, AND THAT IS SAFE IN THE DIRECTION THAT
      MATTERS — stated because it is measurable and was measured. A scratch-tree build that emitted the
      eligibility vector at every production call showed `s18_meshroute` evaluating exactly THREE carriers (all
      own-origination), while this stream-only derivation calls two more of its exhaustions "transit" — the
      `{dst, ctr}` ambiguity above cannot always be resolved from telemetry alone. A superset can only make more
      terminals available to match, never fewer; what it can NEVER do is invent an origin, because every origin
      in a terminal's set comes from a real `data_rx` at that node. ⇒ an unattributed notice is always a real
      finding; an attributed one is proven to name a sender this relay genuinely carried a flight for.
    ④ ⛔⛔ `{dst, ctr}` IS **NOT** A FLIGHT IDENTITY — it is a flight identity ONLY WITHIN ONE ORIGIN, and
      assuming otherwise mis-attributed five legitimate notices on the first cut. Counters are minted PER
      DESTINATION, so in a mesh where several nodes talk to the same destination, `{dst = 9, ctr = 1}` names one
      flight per sender and a relay can carry two of them. ⇒ a terminal's candidate ORIGIN SET is every `origin`
      this node received a DATA for under that `{dst, ctr}`, and a notice is attributed only if the failed
      origin it names is IN that set. ⓘ The stream's `node` field is a simulator INDEX rather than a protocol id,
      which is why the derivation never compares the two: the notice's own `origin` field (the reporter's
      stamped id, written by the pre-existing `tx_enqueue` emit shape) is used where a protocol id is needed.

★ THE CHECKS (any failure => REFUSED, never a warning):
    C1  every `custody_notice_tx` is bound to the LATEST terminal OCCURRENCE at its node below its event
        ordinal — of ANY classification — and THAT occurrence must then be `selected_transit`, SYNCHRONOUS
        with the notice, carrying the notice's failed origin in its `data_rx`-derived origin set, and unused
        (§10.3). ⛔ NOT "at or before": an older candidate is never consulted;
    C1b every moved stream is byte-identical to the BEFORE arm up to its first custody-caused line;
    C2  ZERO notices about an E2E-ACK carrier (§10.1(11), corpus-wide) — an ack-carrier terminal is never in
        the attribution pool, so a notice about one is UNATTRIBUTED, which C1 already refuses; the count of
        excluded ack terminals is reported so the exclusion is visibly non-vacuous;
    C3  ZERO notices about a custody-notice carrier (§12's recursion gate, corpus-wide) — every notice flight
        identity `{reporter, failed_origin, ctr}` is collected and NO attributed terminal may name one;
    C4  every stream with no eligible terminal is BYTE-IDENTICAL between the arms;
    C5  every `custody_notice_refused` names a reason from the v1 vocabulary (`queue_full` / `pack`).
        ⛔ NARROWED 2026-08-31 (QG): this used to claim "unless its stream also shows the congestion that
        caused it" — the checker validates the REASON VOCABULARY and nothing else, and no congestion is
        proven anywhere in this tool. Building a congestion prover to justify the old wording would be
        thoroughness theatre; the honest move is to say what is checked;
    C6  deliveries and duplicates are counted in both arms and any movement is REPORTED (the standing
        owner-flag rule) — this tool does not judge it.

⚠ §18.0.3 — A ZERO RESULT IS EVIDENCE ONLY UNDER A CONTROL THAT CAN FIRE. `--selftest` fabricates each defect
  against the real streams and REQUIRES the corresponding check to go RED. A checker that cannot fail measures
  nothing.

USAGE:  python3 tools/compare_corpus_slice_f.py <before-run-dir> <after-run-dir>
        python3 tools/compare_corpus_slice_f.py <before> <after> --selftest
"""

import argparse
import collections
import hashlib
import json
import sys
from pathlib import Path


# --------------------------------------------------------------------------------------------------- loading
def stream_events(path):
    """Yield (node, time_ms, emit_type, data) for every script_emit line."""
    with path.open() as f:
        for line in f:
            if '"script_emit"' not in line:
                continue
            try:
                e = json.loads(line)
            except ValueError:
                continue
            if e.get("type") != "script_emit":
                continue
            yield e.get("node"), e.get("time_ms"), e.get("emit_type"), e.get("data", {})


def md5_of(path):
    h = hashlib.md5()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()[:8]


# ------------------------------------------------------------------------- the custody-INDEPENDENT derivation
class Facts:
    """Everything derived from events that predate this slice. Applied to BOTH arms, identically.

    ⛔ NOT ONE of the inputs is produced or shaped by the custody code: `data_rx`, `nack_rx`, `e2e_ack_tx` and
       `path_cascade_exhausted` all existed before Slice F and none of them changed. That is what makes an
       AFTER-arm derivation independent evidence rather than the feature vouching for itself.
    """

    def __init__(self, path):
        # ★ EVERY terminal occurrence: (node_idx, dst, ctr, origins-or-None, time_ms, seq, kind)
        #   kind in {selected_transit, deferred_nack, own_origin, ack_excluded}. ⛔ The binding below reads
        #   THIS list; `selected_transit` is a derived VIEW kept only for the census columns.
        self.terminals = []
        self.selected_own = 0
        self.deferred_nack_terminals = 0
        self.transit_e2e_ack = 0
        self.delivered = 0
        self.dup_drop = 0
        self.notices = []               # (node_idx, failed_origin, fresh_ctr, reporter_id, time_ms, seq)
        self.refused = []
        self.unsupported_internal = 0
        fwd = collections.defaultdict(set)   # (node_idx, dst, ctr) -> {origin, ...}
        ack_flights = set()                  # (origin, dst, ctr)
        # (node_idx, time_ms) of a NACK whose reason routes to a §10.2 DEFERRED receive-path terminal.
        # ⛔ reason 1 (`nack_reason_budget`) is deliberately ABSENT: its arm is a SELECTED cascade terminal.
        DEFERRED_NACK_REASONS = {0, 2, 3}    # busy_rx · hop_budget · loop_dup (protocol_constants.h:305-308)
        nack_at = set()
        # ★★ THE EVENT ORDINAL. `time_ms` is NOT an ordering key — a terminal, its give-up and the notice it
        #   produces all land in the SAME millisecond, and several nodes emit inside it too. The stream's own
        #   LINE ORDER is the causal order, so every event carries its ordinal and the binding below uses it.
        seq = 0
        for node, t, kind, d in stream_events(path):
            seq += 1
            if kind == "nack_rx":
                if d.get("reason") in DEFERRED_NACK_REASONS:
                    nack_at.add((node, t))
            elif kind == "e2e_ack_tx":
                ack_flights.add((d.get("origin"), d.get("dst"), d.get("ctr")))
            elif kind == "delivered":
                self.delivered += 1
            elif kind == "dup_drop":
                self.dup_drop += 1
            elif kind == "unsupported_internal":
                self.unsupported_internal += 1
            elif kind == "custody_notice_tx":
                self.notices.append((node, d.get("dst"), d.get("ctr"), d.get("origin"), t, seq))
            elif kind == "custody_notice_refused":
                self.refused.append((node, d.get("dst"), d.get("reason"), t))
            elif kind == "data_rx":
                # ⓘ NO "is it for me" GUARD IS NEEDED, and its absence is deliberate: this map is only ever
                #   consulted for a `{dst, ctr}` that ALSO has a cascade terminal at the same node, and a node
                #   that was the flight's destination or its originator has no live carrier to lose.
                fwd[(node, d.get("dst"), d.get("ctr"))].add(d.get("origin"))
            elif kind == "path_cascade_exhausted":
                # ⛔⛔ EVERY OCCURRENCE IS RECORDED, WITH ITS CLASSIFICATION — ⛔ NOTHING IS DROPPED HERE.
                #    This is the QG correction of 2026-08-31 round 3 and it is the whole shape of the fix:
                #    the first cut `continue`d on the deferred / own-origin / ack-excluded arms, so an
                #    INELIGIBLE terminal was INVISIBLE to the binding below and the sequence
                #      older eligible terminal -> current INELIGIBLE terminal -> erroneous notice
                #    still passed, because the older eligible one was still "the last one seen".
                #    ⇒ FILTER-THEN-BIND was the defect; the tool now BINDS FIRST and CLASSIFIES AFTER.
                key = (node, d.get("dst"), d.get("ctr"))
                origins = fwd.get(key)
                if (node, t) in nack_at:
                    kindname, live = "deferred_nack", None      # §10.2 — generates nothing, by design
                    self.deferred_nack_terminals += 1
                elif not origins:
                    kindname, live = "own_origin", None         # §10.1(2) — no relayed copy => an own send
                    self.selected_own += 1
                else:
                    live = {o for o in origins
                            if (o, d.get("dst"), d.get("ctr")) not in ack_flights}
                    if not live:
                        kindname, live = "ack_excluded", None   # §10.1(11) — every candidate is an ack
                        self.transit_e2e_ack += 1
                    else:
                        kindname = "selected_transit"
                        self.transit_e2e_ack += len(origins) - len(live)
                self.terminals.append((node, d.get("dst"), d.get("ctr"), live, t, seq, kindname))
        self.notice_flights = {(rep, dst, ctr) for (_n, dst, ctr, rep, _t, _s) in self.notices}
        # A derived VIEW, for the census columns only — never for the binding (see the banner above).
        self.selected_transit = [x for x in self.terminals if x[6] == "selected_transit"]


CUSTODY_MARKERS = ("custody_notice_tx", "custody_notice_refused")


def first_divergence(bp, ap):
    """(line_no, before_line, after_line) of the first differing line, or None when identical."""
    with bp.open() as bf, ap.open() as af:
        n = 0
        while True:
            n += 1
            b = bf.readline()
            a = af.readline()
            if not b and not a:
                return None
            if b != a:
                return (n, b.rstrip("\n"), a.rstrip("\n"))


# ------------------------------------------------------------------------------------------------ the checks
def attribute(terminals, notices):
    """BIND FIRST, CLASSIFY AFTER: bind each notice to the LATEST terminal OCCURRENCE at its node, then require
    that exact occurrence to be eligible.

    ⛔⛔ TWO QG CORRECTIONS ARE ENCODED HERE AND BOTH ARE NAMED SO NEITHER WEAKNESS IS RE-ADDED.
      (1) 2026-08-31 round 2 — the first cut searched a per-node pool for ANY earlier terminal carrying the
          notice's failed origin, DISCARDING the terminal's own `{dst, ctr}`. A relay that failed twice for the
          same sender could have its second notice credited to its first terminal.
      (2) 2026-08-31 round 3 — the second cut fixed the search but still received a PRE-FILTERED list: the
          deferred / own-origin / ack-excluded occurrences had already been dropped by the derivation. So an
          INELIGIBLE current terminal was invisible, and the sequence
              older ELIGIBLE terminal -> current INELIGIBLE terminal -> erroneous notice
          still passed, because the older eligible one was still the last one this function could see.
          ⇒ `terminals` is now the UNFILTERED occurrence list and eligibility is asked AFTER the binding.

    ★ THE BINDING, clause by clause:
      · **by EVENT ORDER, not by time** — a terminal, its give-up and the notice it produces come out of one
        synchronous call chain and share a `time_ms`, and several nodes emit inside that millisecond; the
        stream's line order is the causal order.
      · **the LATEST OCCURRENCE at that node below the notice's ordinal — of ANY classification.** That is the
        terminal this notice's own call chain came out of. There is no search and no second candidate.
      · **THEN the four requirements, on that exact occurrence**: it is `selected_transit`; it is SYNCHRONOUS
        with the notice; its origin set contains the failed origin (which pins the carrier's `{dst, ctr}`,
        because the set is derived from real `data_rx` for that terminal's own flight); and it is UNUSED
        (§10.3, one generation per terminal carrier).
      Anything else ⇒ UNATTRIBUTED, with the reason recorded.
    """
    by_node = collections.defaultdict(list)
    for term in terminals:
        by_node[term[0]].append(term)
    for k in by_node:
        by_node[k].sort(key=lambda x: x[5])           # by event ordinal
    consumed = set()
    matched, unmatched = [], []
    for (node, failed_origin, ctr, _rep, t, q) in sorted(notices, key=lambda x: x[5]):
        current = None
        for term in by_node.get(node, []):
            if term[5] < q:
                current = term                        # the LAST occurrence below the notice — any kind
            else:
                break
        if current is None:
            unmatched.append((node, failed_origin, ctr, t, "no terminal occurrence precedes this notice"))
            continue
        t_node, t_dst, t_ctr, t_origins, t_time, t_seq, t_kind = current
        if t_kind != "selected_transit":
            unmatched.append((node, failed_origin, ctr, t,
                              f"the CURRENT terminal {{dst={t_dst}, ctr={t_ctr}}} is `{t_kind}` — INELIGIBLE, "
                              f"so no notice may come out of it"))
            continue
        if id(current) in consumed:
            unmatched.append((node, failed_origin, ctr, t,
                              f"the CURRENT terminal {{dst={t_dst}, ctr={t_ctr}}} is already consumed (§10.3)"))
            continue
        if t_time != t:
            unmatched.append((node, failed_origin, ctr, t,
                              f"the CURRENT terminal is at t={t_time}, not synchronous"))
            continue
        if failed_origin not in t_origins:
            unmatched.append((node, failed_origin, ctr, t,
                              f"the CURRENT terminal {{dst={t_dst}, ctr={t_ctr}}} has origins "
                              f"{sorted(t_origins)}, which do not include {failed_origin}"))
            continue
        consumed.add(id(current))
        matched.append((node, failed_origin, ctr, t, t_dst, t_ctr, t_seq))
    return matched, unmatched


def check_stream(name, b, a, b_md5, a_md5, div, matched, unmatched):
    """Every per-stream claim, as a PURE function — which is what lets the selftest drive each one RED."""
    fails = []
    # C1 — every notice bound to its CURRENT synchronous terminal
    for (node, dst, ctr, t, why) in unmatched:
        fails.append(f"{name}: UNATTRIBUTED notice at node {node} -> {dst} (ctr {ctr}, t={t}): {why}")
    # C1b — the divergence STARTS at a custody line
    if a.notices and div is None:
        fails.append(f"{name}: notices were emitted yet the stream is byte-identical — impossible")
    if div is not None:
        n, bline, aline = div
        if not a.notices:
            fails.append(f"{name}: MOVED ({b_md5} -> {a_md5}) with ZERO notices emitted — unattributable")
        elif not any(m in aline for m in CUSTODY_MARKERS):
            fails.append(f"{name}: the FIRST divergence (line {n}) is not a custody line, so the divergence "
                         f"does not start where this slice acts:\n"
                         f"        BEFORE {bline[:140]}\n        AFTER  {aline[:140]}")
    # C3 — §12's RECURSION GATE. A relayed notice DYING in transit is expected; a notice ABOUT one is not.
    for (node, failed_origin, ctr, t, t_dst, t_ctr, t_seq) in matched:
        if (failed_origin, t_dst, t_ctr) in a.notice_flights:
            fails.append(f"{name}: node {node} generated a notice ABOUT a CUSTODY-NOTICE flight "
                         f"({failed_origin} -> {t_dst} ctr {t_ctr}) — §12's recursion gate FAILED")
    # C4 — a stream with no eligible terminal must be byte-identical
    if not a.selected_transit and b_md5 != a_md5:
        fails.append(f"{name}: MOVED ({b_md5} -> {a_md5}) with ZERO eligible terminals derived")
    # C5 — a refusal needs a known cause
    for (node, dst, reason, t) in a.refused:
        if reason not in ("queue_full", "pack"):
            fails.append(f"{name}: custody_notice_refused with an unknown reason {reason!r}")
    return fails


def run(before_dir, after_dir, selftest=False):
    b_streams = sorted((before_dir / "streams").glob("*.ndjson"))
    a_streams = sorted((after_dir / "streams").glob("*.ndjson"))
    if not b_streams or len(b_streams) != len(a_streams):
        print(f"REFUSED: stream sets differ ({len(b_streams)} vs {len(a_streams)}) — nothing to compare.")
        return 1

    fails, rows = [], []
    tot = collections.Counter()
    kept = {}
    for bp in b_streams:
        name = bp.name[: -len(".ndjson")]
        ap = after_dir / "streams" / bp.name
        if not ap.exists():
            fails.append(f"{name}: missing in the AFTER arm")
            continue
        b, a = Facts(bp), Facts(ap)
        b_md5, a_md5 = md5_of(bp), md5_of(ap)
        div = first_divergence(bp, ap)
        matched, unmatched = attribute(a.terminals, a.notices)   # ⛔ the UNFILTERED occurrence list
        fails.extend(check_stream(name, b, a, b_md5, a_md5, div, matched, unmatched))
        if a.notices:
            kept[name] = (b, a, b_md5, a_md5, div)

        if div is not None and a.notices and any(m in div[2] for m in CUSTODY_MARKERS):
            tot["prefix_ok"] += 1
            tot["prefix_lines"] += div[0] - 1
        for (tn, tdst, tctr, torigins, tt, tq, _tk) in a.selected_transit:
            if any((o, tdst, tctr) in a.notice_flights for o in torigins):
                tot["relayed_notice_deaths"] += 1

        rows.append(dict(name=name, b_md5=b_md5, a_md5=a_md5,
                         b_sel=len(b.selected_transit), a_sel=len(a.selected_transit),
                         sel_own=b.selected_own, deferred=b.deferred_nack_terminals,
                         ack_excl=b.transit_e2e_ack,
                         notices=len(a.notices), refused=len(a.refused),
                         unsupported=a.unsupported_internal,
                         b_deliv=b.delivered, a_deliv=a.delivered,
                         b_dup=b.dup_drop, a_dup=a.dup_drop))
        for k in ("b_sel", "a_sel", "sel_own", "deferred", "ack_excl",
                  "notices", "refused", "unsupported"):
            tot[k] += rows[-1][k]
        tot["b_deliv"] += b.delivered; tot["a_deliv"] += a.delivered
        tot["b_dup"] += b.dup_drop;    tot["a_dup"] += a.dup_drop
        tot["moved"] += (b_md5 != a_md5)

    # ---- the non-vacuity controls: a checker that never sees its subject proves nothing -------------------
    if tot["a_sel"] == 0:
        fails.append("VACUOUS: ZERO selected transit terminals were derived — the classifier never found its "
                     "subject, so 'every notice attributed' would be trivially true")
    if tot["notices"] == 0:
        fails.append("VACUOUS: ZERO custody notices in the AFTER arm — nothing was attributed")
    if tot["deferred"] == 0:
        fails.append("VACUOUS: ZERO §10.2 deferred NACK terminals were separated — the distinction this tool "
                     "exists to make was never exercised")
    if tot["ack_excl"] == 0:
        fails.append("VACUOUS: ZERO transit E2E-ACK carriers were excluded — §10.1(11)'s corpus arm is untested")
    if tot["prefix_ok"] != tot["moved"]:
        fails.append(f"the BEFORE-anchored prefix check covered {tot['prefix_ok']} of {tot['moved']} moved streams")

    # ---- report ------------------------------------------------------------------------------------------
    print(f"{'stream':45s} {'BEFORE':>8} {'AFTER':>8}   {'selTr':>5} {'selOwn':>6} {'§10.2':>5} "
          f"{'ack✗':>4} {'notice':>6} {'refus':>5} {'unsup':>5} {'deliv':>12} {'dup':>10}")
    for r in rows:
        mv = "*" if r["b_md5"] != r["a_md5"] else " "
        print(f"{r['name']:45s} {r['b_md5']:>8} {r['a_md5']:>8} {mv} "
              f"{r['a_sel']:5d} {r['sel_own']:6d} {r['deferred']:5d} {r['ack_excl']:4d} "
              f"{r['notices']:6d} {r['refused']:5d} {r['unsupported']:5d} "
              f"{r['b_deliv']:5d}->{r['a_deliv']:<5d} {r['b_dup']:4d}->{r['a_dup']:<4d}")
    print(f"\n  streams moved: {tot['moved']}/{len(rows)}   notices: {tot['notices']}   "
          f"refused: {tot['refused']}   unsupported_internal (the F-before-G drop): {tot['unsupported']}")
    print(f"  selected TRANSIT terminals — BEFORE {tot['b_sel']}, AFTER {tot['a_sel']}   "
          f"own-origination terminals (BEFORE): {tot['sel_own']}")
    print(f"  EXCLUDED, and measured as such: §10.2 deferred NACK terminals {tot['deferred']}   "
          f"§10.1(11) E2E-ACK carriers {tot['ack_excl']}")
    print(f"  ★ §12 RECURSION GATE, POSITIVELY EXERCISED: {tot['relayed_notice_deaths']} relayed CUSTODY-NOTICE "
          f"flight(s) reached a selected transit terminal and generated NOTHING")
    print(f"  BEFORE-anchored prefix: {tot['prefix_ok']}/{tot['moved']} moved streams identical for "
          f"{tot['prefix_lines']} lines before their first custody line")
    print(f"  ★ DELIVERIES {tot['b_deliv']} -> {tot['a_deliv']}   DUPLICATES {tot['b_dup']} -> {tot['a_dup']}"
          f"   {'(UNCHANGED)' if tot['b_deliv'] == tot['a_deliv'] else '(MOVED — OWNER FLAG, see the report)'}")

    if selftest:
        fails.extend(run_selftest(kept, tot))

    if fails:
        print(f"\nREFUSED: {len(fails)} failure(s)")
        for f in fails:
            print(f"   · {f}")
        return 1
    print("\nPASS — every custody notice is bound to the CURRENT synchronous selected-transit terminal it came "
          "out of, derived from custody-INDEPENDENT events; every moved stream diverges first at a custody "
          "line; no stream without an eligible terminal moved; every refusal names a known cause; no notice "
          "reports on a custody-notice flight.")
    return 0


def run_selftest(kept, tot):
    """★ EVERY CLAIM THE PASS LINE MAKES MUST HAVE A CONTROL THAT CAN FIRE (§18.0.3).

    ⛔ The claim-to-control audit is written out so a future reader can check the coverage rather than trust it:
         "bound to the CURRENT synchronous terminal"  -> S1 S2 S3 S4 S5 S6
         "diverges first at a custody line"           -> S7
         "no stream without an eligible terminal moved" -> S8
         "every refusal names a known cause"          -> S9
         "no notice reports on a custody-notice flight" -> S10
       plus S11/S12, the two separation counts the derivation depends on.
    """
    print("\n--- SELFTEST: every check must be able to FIRE ---------------------------------------------")
    fails = []
    controls = []
    name = next(iter(kept), None)
    if name is None:
        return ["SELFTEST: no stream with notices — the controls cannot be constructed"]
    b, a, b_md5, a_md5, div = kept[name]
    terms = list(a.terminals)                 # ⛔ the UNFILTERED occurrence list, as production uses it
    notes = list(a.notices)
    base_matched, base_unmatched = attribute(terms, notes)

    # S1 — a fabricated notice from a node that has no terminal at all
    fake = notes + [(9999, 252, 9, 0, 1, 10 ** 12)]
    controls.append(("a FABRICATED notice from a node with no terminal at all",
                     len(attribute(terms, fake)[1]) == len(base_unmatched) + 1))
    # S2 — the WRONG failed origin on an otherwise correct current terminal
    n0 = base_matched[0]
    wrong = [(n0[0], 253, n0[2], 0, n0[3], _seq_of(notes, n0)) ]
    controls.append(("a notice naming an origin the CURRENT terminal does not carry",
                     len(attribute(terms, wrong)[1]) == 1))
    # S3 ★ THE QG CONTROL: a STALE eligible terminal. An older terminal DOES match; the current one does not.
    #      The old binding accepted this; the corrected one must refuse it.
    stale_terms, stale_notes = _stale_case(terms, notes, base_matched)
    if stale_terms is None:
        controls.append(("a STALE eligible terminal (older matches, CURRENT does not)", False))
        fails.append("SELFTEST: the stale-terminal control could not be constructed — it must be")
    else:
        controls.append(("a STALE eligible terminal (older matches, CURRENT does not) is REFUSED",
                         len(attribute(stale_terms, stale_notes)[1]) == 1))
    # S3b ★★ THE ROUND-3 QG CONTROL, THE SEQUENCE VERBATIM: older ELIGIBLE -> current INELIGIBLE -> notice.
    #       Run for ALL THREE ineligible classifications, because a tool could plausibly preserve one and
    #       still drop the others.
    for kindname in ("deferred_nack", "own_origin", "ack_excluded"):
        st, sn = _stale_over_ineligible_case(terms, notes, base_matched, kindname)
        if st is None:
            controls.append((f"older ELIGIBLE -> current INELIGIBLE ({kindname}) -> notice", False))
            fails.append(f"SELFTEST: the older-eligible-over-{kindname} control could not be constructed")
        else:
            controls.append((f"older ELIGIBLE -> current INELIGIBLE (`{kindname}`) -> notice is REFUSED",
                             len(attribute(st, sn)[1]) == 1))
    # S4 ★ THE QG CONTROL: a WRONG CURRENT terminal — same node, same instant, different carrier.
    wrong_terms = _replace_current_origins(terms, base_matched[0])
    controls.append(("a WRONG CURRENT terminal (right node and instant, wrong carrier) is REFUSED",
                     len(attribute(wrong_terms, notes)[1]) == len(base_unmatched) + 1))
    # S5 — a notice whose current terminal is NOT synchronous with it
    desync = [(t[0], t[1], t[2], t[3], t[4] + 1, t[5], t[6]) for t in terms]
    controls.append(("a current terminal that is not SYNCHRONOUS with the notice is REFUSED",
                     len(attribute(desync, notes)[1]) == len(notes)))
    # S6 — §10.3: two notices cannot both consume one terminal
    dup = notes + [(n0[0], n0[1], n0[2], 0, n0[3], _seq_of(notes, n0) + 1)]
    controls.append(("MORE notices than terminals — §10.3's one-generation rule",
                     len(attribute(terms, dup)[1]) > len(base_unmatched)))
    # S7 — a first divergence that is NOT a custody line
    bogus_div = (1, '{"type":"tx","node":"x"}', '{"type":"tx","node":"y"}')
    controls.append(("a first divergence that is NOT a custody line",
                     any("not a custody line" in f
                         for f in check_stream(name, b, a, b_md5, "deadbeef", bogus_div,
                                               base_matched, base_unmatched))))
    # S8 — a stream that MOVED with zero eligible terminals
    empty = _facts_without_terminals(a)
    controls.append(("a stream that MOVED with ZERO eligible terminals",
                     any("ZERO eligible terminals" in f
                         for f in check_stream(name, b, empty, b_md5, "deadbeef", div, [], []))))
    # S9 — a refusal with an unknown cause
    odd = _facts_with_refusal(a, "mystery")
    controls.append(("a `custody_notice_refused` with an unknown reason",
                     any("unknown reason" in f
                         for f in check_stream(name, b, odd, b_md5, a_md5, div, base_matched, base_unmatched))))
    # S10 — §12's recursion gate: a notice bound to a terminal carrying a NOTICE flight
    recursive = _facts_with_notice_flight(a, base_matched[0])
    controls.append(("a notice generated ABOUT a custody-notice flight (§12's recursion gate)",
                     any("recursion gate FAILED" in f
                         for f in check_stream(name, b, recursive, b_md5, a_md5, div,
                                               base_matched, base_unmatched))))
    # S11/S12 — the two separations the whole derivation rests on
    controls.append(("the §10.2 deferred-NACK terminals were SEPARATED and excluded", tot["deferred"] > 0))
    controls.append(("the §10.1(11) E2E-ACK carriers were SEPARATED and excluded", tot["ack_excl"] > 0))

    red = 0
    for label, fired in controls:
        print(f"  {'RED  (control fired)' if fired else 'GREEN — CONTROL DID NOT FIRE'}  {label}")
        red += bool(fired)
    if red != len(controls):
        fails.append(f"SELFTEST: only {red}/{len(controls)} controls fired — the checker is not an instrument")
    else:
        print(f"\nSELFTEST PASS — {red}/{len(controls)} controls RED.")
    return fails


# ---- selftest fixture builders. Each returns a DOCTORED copy; none touches a real stream. -----------------
def _seq_of(notes, matched_row):
    for (node, dst, ctr, rep, t, q) in notes:
        if (node, dst, ctr, t) == (matched_row[0], matched_row[1], matched_row[2], matched_row[3]):
            return q
    return 10 ** 12


def _stale_case(terms, notes, matched):
    """One notice, an OLDER terminal that matches it, and a CURRENT SELECTED terminal that does not."""
    if not matched:
        return None, None
    node, origin, ctr, t, t_dst, t_ctr, t_seq = matched[0]
    other = origin + 1 if origin < 254 else origin - 1
    older = (node, t_dst, t_ctr, {origin}, t, t_seq - 2, "selected_transit")   # stale, MATCHING
    current = (node, t_dst, t_ctr, {other}, t, t_seq, "selected_transit")      # current, does NOT match
    note = [(node, origin, ctr, 0, t, t_seq + 1)]
    return [older, current], note


def _stale_over_ineligible_case(terms, notes, matched, kindname):
    """★ THE ROUND-3 QG CONTROL, VERBATIM: older ELIGIBLE terminal -> current INELIGIBLE terminal -> notice.

    The older terminal matches the notice perfectly. The CURRENT one is a classification that may never
    produce a notice (`deferred_nack` / `own_origin` / `ack_excluded`). A tool that filtered before binding
    could not see the current one at all and would happily credit the older one — which is exactly the defect
    this control exists to keep closed.
    """
    if not matched:
        return None, None
    node, origin, ctr, t, t_dst, t_ctr, t_seq = matched[0]
    older = (node, t_dst, t_ctr, {origin}, t, t_seq - 2, "selected_transit")   # ELIGIBLE and MATCHING
    current = (node, t_dst, t_ctr, None, t, t_seq, kindname)                   # INELIGIBLE — and CURRENT
    note = [(node, origin, ctr, 0, t, t_seq + 1)]
    return [older, current], note


def _replace_current_origins(terms, matched_row):
    node, origin, _ctr, _t, _d, _c, t_seq = matched_row
    out = []
    for term in terms:
        if term[0] == node and term[5] == t_seq:
            other = origin + 1 if origin < 254 else origin - 1
            out.append((term[0], term[1], term[2], {other}, term[4], term[5], term[6]))
        else:
            out.append(term)
    return out


class _FactsView:
    """A shallow, doctored view of a `Facts` — the selftest never mutates a real one."""
    def __init__(self, a, **over):
        for k, v in vars(a).items():
            setattr(self, k, v)
        for k, v in over.items():
            setattr(self, k, v)


def _facts_without_terminals(a):
    return _FactsView(a, terminals=[], selected_transit=[], notices=[])


def _facts_with_refusal(a, reason):
    return _FactsView(a, refused=list(a.refused) + [(1, 2, reason, 0)])


def _facts_with_notice_flight(a, matched_row):
    _node, origin, _ctr, _t, t_dst, t_ctr, _q = matched_row
    return _FactsView(a, notice_flights=set(a.notice_flights) | {(origin, t_dst, t_ctr)})


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("before", type=Path)
    ap.add_argument("after", type=Path)
    ap.add_argument("--selftest", action="store_true", help="prove every check can FIRE (§18.0.3)")
    args = ap.parse_args()
    sys.exit(run(args.before, args.after, args.selftest))


if __name__ == "__main__":
    main()
