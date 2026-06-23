# MeshRoute lab harness — reconcile: match send-ledger <-> receive-ledgers BY TAG; per-msg verdict + tiered aggregate.
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# Pure (dicts in, dict out) -> unit-testable offline. Inputs (all host-collected by oracle.py):
#   send_ledger      [{tag, src, kind('dm'|'chan'), dst, chan, ack, enc, ctr, host_send_ts}]
#   inboxes_eventual {node_id: {"dm":[rec…], "chan":[rec…]}}   the LATE re-pull (@eventual_s) — the truth for delivery
#   inboxes_immediate{node_id: {"dm":[rec…], "chan":[rec…]}}   the settle-end pull — the flood/early-repair snapshot
#   acks             {src_node_id: {ctr: "ACKED"|"FAILED"}}    the src's live HOP-ack (first-hop only — reported, NOT gated)
#   deliveries       {node_id: [{ts, kind('RECV'|'CH'), tag}]} the receiver's live pushes; tag pre-parsed
#   e2e_ack_live     {(src_node_id, ctr): ts}                  the live `E2E-ACKED` push — round-trip LATENCY only (best-effort)
#   leaf_ids [node_id]   hangs [src…]   run_id   eventual_s
#
# The TRUE end-to-end ack ("the dest got it AND told the sender") is the SENDER's durable receipt: a DM-store record
# with type=="e2e_ack" (origin = the dest that confirmed, ctr = the acked ctr). It rides the src's inbox pull, so it's
# read straight from inboxes_eventual — the reliable signal (the live E2E-ACKED push drops under cap_push_ring burst,
# like RECV did, so it's latency-only). The old hop-ack (ACKED console line) is the first hop only -> reported, not gated.
#
# Tiered verdict (the locked criteria, lenient e2e gate per user 2026-06-23):
#   - acked DMs  -> gate on the E2E ACK (durable receipt). e2e-acked = success; DELIVERED-but-no-ack-return = WARNING
#                  (counted/listed, does NOT fail the run); neither delivered nor e2e-acked = the only hard FAIL (total loss)
#   - un-acked DMs -> delivered-to-dst-inbox rate (reported; threshold optional)
#   - channels   -> EVENTUAL (in the @eventual_s inbox) = 100% across the leaf (a miss there = a real loss = FAIL);
#                   IMMEDIATE (settle-end inbox) + flood-immediate (live CH push) are reported as flood-quality metrics
#   - no node hangs (a src that never returned a `queued ctr=`)
try:                                     # package import (normal) …
    from .tag import parse_tag, make_tag
except ImportError:                      # … or run directly as a script (python3 tools/lab/reconcile.py)
    from tag import parse_tag, make_tag


def _tagsets(inboxes, leaf_ids, kind):
    """Per-node SET of canonical tags present in an inbox. EXACT match (not substring) — `T…#1` must NOT match
    `T…#10`; the counter has no terminator, so a substring `in` test false-credits #1/#10/#11… (review finding).
    E2E-ack receipts have an EMPTY body -> parse_tag returns None -> they never pollute a delivery tagset."""
    out = {}
    for nid in leaf_ids:
        s = set()
        for r in inboxes.get(nid, {}).get(kind, []):
            t = parse_tag(r.get("body", ""))
            if t:
                s.add(make_tag(t["run"], t["src"], t["n"]))
        out[nid] = s
    return out


def _receipts(inboxes, leaf_ids):
    """Per-(would-be-sender) SET of {(receipt_origin, acked_ctr)} from its durable E2E-ack receipts. A receipt is a
    DM-store record with type=='e2e_ack': origin = the dest that confirmed, ctr = the acked ctr (the sender's send ctr).
    Same-layer (origin,ctr) is the match key (the single-leaf harness); cross-layer would also key on sender_hash."""
    out = {}
    for nid in leaf_ids:
        s = set()
        for r in inboxes.get(nid, {}).get("dm", []):
            if r.get("type") == "e2e_ack" and r.get("origin") is not None and r.get("ctr") is not None:
                s.add((int(r["origin"]), int(r["ctr"])))
        out[nid] = s
    return out


def reconcile(send_ledger, inboxes_eventual, inboxes_immediate, acks, deliveries, leaf_ids, run_id,
              hangs=None, eventual_s=None, e2e_ack_live=None):
    hangs = list(hangs or [])
    e2e_live = e2e_ack_live or {}
    dm_evt = _tagsets(inboxes_eventual, leaf_ids, "dm")                # DM delivery: judged on the most-complete pull
    src_receipts = _receipts(inboxes_eventual, leaf_ids)              # E2E ack: the sender's durable receipts
    chan_imm = _tagsets(inboxes_immediate, leaf_ids, "chan")
    chan_evt = _tagsets(inboxes_eventual, leaf_ids, "chan")
    deliv_first = {}                                                   # node -> {tag: earliest live-push ts}
    for nid in leaf_ids:
        d = {}
        for ev in deliveries.get(nid, []):
            tg = ev.get("tag")
            if tg and (tg not in d or ev["ts"] < d[tg]):
                d[tg] = ev["ts"]
        deliv_first[nid] = d

    per_msg = []
    dm_total = dm_delivered = dm_ack_req = dm_e2e_acked = dm_hop_acked = 0
    dm_fails, dm_warns, dm_lats, dm_rtts = [], [], [], []
    chan_total = chan_imm_hits = chan_evt_hits = chan_flood_hits = chan_exp = 0
    chan_fails = []

    for row in send_ledger:
        tag, src = row["tag"], row["src"]
        if row["kind"] == "dm":
            dm_total += 1
            dst, ctr = row["dst"], row.get("ctr")
            delivered = tag in dm_evt.get(dst, set())                 # the dst's inbox holds the message (the oracle truth)
            if delivered:
                dm_delivered += 1
            e2e_acked = hop_acked = status = None
            if row.get("ack"):
                dm_ack_req += 1
                e2e_acked = (ctr is not None) and ((dst, ctr) in src_receipts.get(src, set()))  # the durable receipt
                hop_acked = acks.get(src, {}).get(ctr) == "ACKED"     # the first-hop ack (reported, not gated)
                if e2e_acked:
                    dm_e2e_acked += 1
                if hop_acked:
                    dm_hop_acked += 1
                status = "acked" if e2e_acked else ("delivered_no_ack" if delivered else "lost")
            ft = deliv_first.get(dst, {}).get(tag)                    # delivery latency: send -> first RECV at the dst
            lat = (ft - row["host_send_ts"]) if (ft is not None and row.get("host_send_ts") is not None) else None
            if lat is not None:
                dm_lats.append(lat)
            at = e2e_live.get((src, ctr)) if ctr is not None else None  # e2e-ack round-trip: send -> live E2E-ACKED at the src
            rtt = (at - row["host_send_ts"]) if (at is not None and row.get("host_send_ts") is not None) else None
            if rtt is not None:
                dm_rtts.append(rtt)
            rv = {"tag": tag, "kind": "dm", "src": src, "dst": dst, "ctr": ctr, "delivered": delivered,
                  "e2e_acked": e2e_acked, "hop_acked": hop_acked, "status": status, "latency_s": lat, "ack_rtt_s": rtt}
            per_msg.append(rv)
            if status == "delivered_no_ack":                          # lenient: delivered, but the e2e-ack didn't return = WARN
                dm_warns.append(rv)
            elif status == "lost":                                    # neither delivered nor e2e-acked = the only hard FAIL
                dm_fails.append(rv)
        else:
            chan_total += 1
            receivers = [nid for nid in leaf_ids if nid != src]
            reached, missed, immediate, flood = [], [], [], []
            for nid in receivers:
                chan_exp += 1
                if tag in chan_imm.get(nid, set()):
                    chan_imm_hits += 1
                    immediate.append(nid)
                if tag in chan_evt.get(nid, set()):                   # gate on EVENTUAL
                    chan_evt_hits += 1
                    reached.append(nid)
                else:
                    missed.append(nid)
                if tag in deliv_first.get(nid, {}):
                    chan_flood_hits += 1
                    flood.append(nid)
            rv = {"tag": tag, "kind": "chan", "src": src, "chan": row.get("chan"),
                  "receivers": len(receivers), "reached": reached, "eventual": len(reached),
                  "immediate": immediate, "missed": missed, "flood_immediate": flood}
            per_msg.append(rv)
            if missed:                                                # locked: channel eventual must be 100% across the leaf
                chan_fails.append(rv)

    def _pct(num, den):
        return round(100.0 * num / den, 1) if den else None

    def _p(xs, q):
        xs = sorted(xs)
        return round(xs[min(len(xs) - 1, int(q * len(xs)))], 3) if xs else None

    acked_ok = not dm_fails                                            # lenient: only TOTAL losses fail (warns don't)
    chan_ok = not chan_fails
    return {
        "run_id": run_id,
        "dm": {"total": dm_total, "delivered": dm_delivered, "delivered_pct": _pct(dm_delivered, dm_total),
               "ack_required": dm_ack_req, "e2e_acked": dm_e2e_acked, "hop_acked": dm_hop_acked,
               "warnings": dm_warns, "fails": dm_fails,
               "latency_p50_s": _p(dm_lats, 0.50), "latency_p95_s": _p(dm_lats, 0.95),
               "ack_rtt_p50_s": _p(dm_rtts, 0.50), "ack_rtt_p95_s": _p(dm_rtts, 0.95)},
        "chan": {"total": chan_total, "expected": chan_exp, "eventual_s": eventual_s,
                 "immediate_hits": chan_imm_hits, "immediate_pct": _pct(chan_imm_hits, chan_exp),
                 "eventual_hits": chan_evt_hits, "eventual_pct": _pct(chan_evt_hits, chan_exp),
                 "flood_pct": _pct(chan_flood_hits, chan_exp), "fails": chan_fails},
        "verdict": {"acked_dms_ok": acked_ok, "channels_eventual_ok": chan_ok, "hangs": hangs,
                    "pass": acked_ok and chan_ok and not hangs},
        "per_msg": per_msg,
    }


def _selftest():
    leaf = [1, 2, 3, 4]
    rid = "a1"
    # DM#0 src1->dst2: e2e-ACKED (src1 holds a durable receipt + dst2 has the msg).  DM S2#0 src2->dst3: DELIVERED but
    # NO receipt at src2 -> WARNING.  DM#1 src1->dst4: neither -> the only hard FAIL (total loss).  + one channel.
    send_ledger = [
        {"tag": "Ta1S1#0", "src": 1, "kind": "dm", "dst": 2, "chan": None, "ack": True, "enc": False, "ctr": 5, "host_send_ts": 100.0},
        {"tag": "Ta1S2#0", "src": 2, "kind": "dm", "dst": 3, "chan": None, "ack": True, "enc": False, "ctr": 6, "host_send_ts": 100.0},
        {"tag": "Ta1S1#1", "src": 1, "kind": "dm", "dst": 4, "chan": None, "ack": True, "enc": False, "ctr": 7, "host_send_ts": 100.0},
        {"tag": "Ta1S1#2", "src": 1, "kind": "chan", "dst": None, "chan": 0, "ack": False, "enc": False, "ctr": 8, "host_send_ts": 100.0},
    ]
    inboxes_imm = {1: {"dm": [], "chan": []}, 2: {"dm": [{"body": "Ta1S1#0"}], "chan": [{"body": "Ta1S1#2"}]},
                   3: {"dm": [], "chan": []}, 4: {"dm": [], "chan": []}}
    inboxes_evt = {
        1: {"dm": [{"type": "e2e_ack", "origin": 2, "ctr": 5}], "chan": []},   # src1's durable receipt for DM#0
        2: {"dm": [{"body": "Ta1S1#0"}], "chan": [{"body": "Ta1S1#2"}]},
        3: {"dm": [{"body": "Ta1S2#0"}], "chan": [{"body": "Ta1S1#2"}]},        # S2#0 delivered (but src2 has no receipt)
        4: {"dm": [], "chan": []},                                              # DM#1 never arrived; channel missed
    }
    acks = {1: {5: "ACKED"}, 2: {6: "ACKED"}}                                   # BOTH hop-acked (first hop) — reported only
    deliveries = {2: [{"ts": 100.4, "kind": "RECV", "tag": "Ta1S1#0"}, {"ts": 100.6, "kind": "CH", "tag": "Ta1S1#2"}],
                  3: [{"ts": 100.9, "kind": "CH", "tag": "Ta1S1#2"}]}
    e2e_live = {(1, 5): 102.0}                                                  # live E2E-ACKED for DM#0 -> rtt 2.0s
    r = reconcile(send_ledger, inboxes_evt, inboxes_imm, acks, deliveries, leaf, rid, eventual_s=300, e2e_ack_live=e2e_live)

    assert r["dm"]["total"] == 3 and r["dm"]["delivered"] == 2, r["dm"]         # #0 + S2#0 delivered; #1 lost
    assert r["dm"]["ack_required"] == 3 and r["dm"]["e2e_acked"] == 1, r["dm"]  # only #0 has the durable receipt
    assert r["dm"]["hop_acked"] == 2, r["dm"]                                   # both hop-acked (NOT the gate)
    assert len(r["dm"]["warnings"]) == 1 and r["dm"]["warnings"][0]["src"] == 2, r["dm"]   # S2#0 delivered, ack lost
    assert len(r["dm"]["fails"]) == 1 and r["dm"]["fails"][0]["dst"] == 4, r["dm"]          # #1 total loss = the FAIL
    assert r["dm"]["latency_p50_s"] == 0.4, r["dm"]                             # #0 delivery latency
    assert r["dm"]["ack_rtt_p50_s"] == 2.0, r["dm"]                             # #0 e2e-ack round-trip
    # channel: immediate node2 (1/3); eventual node2+node3 (2/3); node4 missing -> the only channel loss
    assert r["chan"]["immediate_hits"] == 1 and r["chan"]["eventual_hits"] == 2 and r["chan"]["expected"] == 3, r["chan"]
    assert len(r["chan"]["fails"]) == 1 and r["chan"]["fails"][0]["missed"] == [4], r["chan"]
    assert r["verdict"]["acked_dms_ok"] is False and r["verdict"]["pass"] is False, r["verdict"]   # the total loss fails

    # WARNING does NOT fail the run: a delivered DM whose e2e-ack never returned, channel all-reached, no total loss.
    slw = [{"tag": "Tw1S1#0", "src": 1, "kind": "dm", "dst": 2, "ack": True, "enc": False, "ctr": 5, "host_send_ts": 0.0}]
    evtw = {1: {"dm": [], "chan": []}, 2: {"dm": [{"body": "Tw1S1#0"}], "chan": []},
            3: {"dm": [], "chan": []}, 4: {"dm": [], "chan": []}}
    rw = reconcile(slw, evtw, evtw, {1: {5: "ACKED"}}, {}, leaf, "w1", eventual_s=0)
    assert rw["dm"]["delivered"] == 1 and rw["dm"]["e2e_acked"] == 0, rw["dm"]
    assert len(rw["dm"]["warnings"]) == 1 and rw["dm"]["fails"] == [], rw["dm"]
    assert rw["verdict"]["acked_dms_ok"] is True and rw["verdict"]["pass"] is True, rw["verdict"]   # WARN != FAIL

    # late re-pull RESCUES a channel node absent at settle but present at eventual; DM#0 e2e-acked via the receipt.
    imm2 = {1: {"dm": [], "chan": []}, 2: {"dm": [{"body": "Tb1S1#0"}], "chan": []},
            3: {"dm": [], "chan": []}, 4: {"dm": [], "chan": []}}
    evt2 = {1: {"dm": [{"type": "e2e_ack", "origin": 2, "ctr": 5}], "chan": []}, 2: {"dm": [{"body": "Tb1S1#0"}], "chan": [{"body": "Tb1S1#9"}]},
            3: {"dm": [], "chan": [{"body": "Tb1S1#9"}]}, 4: {"dm": [], "chan": [{"body": "Tb1S1#9"}]}}
    sl2 = [{"tag": "Tb1S1#0", "src": 1, "kind": "dm", "dst": 2, "ack": True, "ctr": 5, "host_send_ts": 0.0},
           {"tag": "Tb1S1#9", "src": 1, "kind": "chan", "chan": 0, "ack": False, "ctr": 6, "host_send_ts": 0.0}]
    r2 = reconcile(sl2, evt2, imm2, {1: {5: "ACKED"}}, {}, leaf, "b1", eventual_s=300)
    assert r2["chan"]["immediate_hits"] == 0 and r2["chan"]["eventual_hits"] == 3, r2["chan"]   # eventual-only, all 3
    assert r2["dm"]["e2e_acked"] == 1 and r2["dm"]["fails"] == [] and r2["verdict"]["pass"] is True, r2
    # hang fails the run
    r3 = reconcile(sl2, evt2, imm2, {1: {5: "ACKED"}}, {}, leaf, "b1", hangs=[3], eventual_s=300)
    assert r3["verdict"]["pass"] is False and r3["verdict"]["hangs"] == [3], r3["verdict"]

    # REGRESSION (review finding): #1 must NOT match #10 — exact tag, not substring. DM #1 was NEVER delivered;
    # only #10 sits in the dst inbox. The old `any(tag in body)` falsely credited it. (un-acked -> not a fail.)
    sl4 = [{"tag": "Tc1S1#1", "src": 1, "kind": "dm", "dst": 2, "ack": False, "ctr": 1, "host_send_ts": 0.0}]
    evt4 = {1: {"dm": [], "chan": []}, 2: {"dm": [{"body": "Tc1S1#10"}], "chan": []},
            3: {"dm": [], "chan": []}, 4: {"dm": [], "chan": []}}
    r4 = reconcile(sl4, evt4, evt4, {}, {}, leaf, "c1", eventual_s=0)
    assert r4["dm"]["delivered"] == 0 and r4["dm"]["fails"] == [], r4["dm"]     # #1 is NOT in {#10} -> not delivered
    print("reconcile selftest OK")


if __name__ == "__main__":
    _selftest()
