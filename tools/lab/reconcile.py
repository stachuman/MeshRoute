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
              hangs=None, eventual_s=None, e2e_ack_live=None, duty=None, rxbad=None, measured=None):
    hangs = list(hangs or [])
    e2e_live = e2e_ack_live or {}
    # `measured` = nodes whose eventual inbox we actually READ. An UNMEASURED node (flaky USB-CDC at the final pull) is
    # NOT a delivery miss — exclude it from every denominator (else it false-deflates delivery + false-ORPHANs every
    # origin, as it did in run 3c1a64 where 4/8 nodes were unread). Default = all measured (offline/unit use).
    meas = set(measured) if measured is not None else set(leaf_ids)
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
    dm_total = dm_delivered = dm_deliv_den = dm_ack_den = dm_e2e_acked = dm_hop_acked = 0
    dm_fails, dm_warns, dm_lats, dm_rtts = [], [], [], []
    chan_total = chan_imm_hits = chan_evt_hits = chan_flood_hits = chan_exp = 0
    chan_fails = []

    for row in send_ledger:
        tag, src = row["tag"], row["src"]
        if row["kind"] == "dm":
            dm_total += 1
            dst, ctr = row["dst"], row.get("ctr")
            dst_meas, src_meas = dst in meas, src in meas
            delivered = (tag in dm_evt.get(dst, set())) if dst_meas else None   # unknown if we could not read the dst
            if dst_meas:
                dm_deliv_den += 1                                     # delivery-rate denominator = readable destinations
                if delivered:
                    dm_delivered += 1
            e2e_acked = hop_acked = status = None
            if row.get("ack"):
                hop_acked = acks.get(src, {}).get(ctr) == "ACKED"     # live hop-ack (reported; not inbox-dependent)
                if hop_acked:
                    dm_hop_acked += 1
                if src_meas:                                          # the e2e receipt sits in the SRC inbox -> need it read
                    dm_ack_den += 1
                    e2e_acked = (ctr is not None) and ((dst, ctr) in src_receipts.get(src, set()))
                    if e2e_acked:
                        dm_e2e_acked += 1
                if not dst_meas:
                    status = "unmeasured"                             # dst unread -> delivery unknown (NOT a loss)
                elif delivered:
                    status = "acked" if e2e_acked else ("delivered_no_ack" if src_meas else "delivered_ack_unknown")
                else:
                    status = "lost"                                   # dst READ + message absent = a real total loss
            elif not dst_meas:
                status = "unmeasured"
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
            receivers = [nid for nid in leaf_ids if nid != src and nid in meas]   # measured receivers only (unread ≠ miss)
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

    def _mean(xs):
        return round(sum(xs) / len(xs), 2) if xs else None

    # --- PER-NODE receiver rollup: DM-rx% (addressed→arrived), CH-rx% (others' broadcasts→reached), received-delay,
    #     duty (start→final + airtime delta), rxbad (the CRC-storm count). duty/rxbad are oracle-captured side-channels. ---
    send_ts_by_tag = {row["tag"]: row.get("host_send_ts") for row in send_ledger}
    duty, rxbad = duty or {}, rxbad or {}
    dm_rx = {nid: [0, 0] for nid in leaf_ids}                          # nid -> [received, addressed-to-it]
    ch_rx = {nid: [0, 0] for nid in leaf_ids}                          # nid -> [reached, broadcast-by-others]
    node_lat = {nid: [] for nid in leaf_ids}                           # nid -> [delay of each msg IT received]
    for row in send_ledger:
        tag = row["tag"]
        if row["kind"] == "dm":
            if row["dst"] in dm_rx:
                dm_rx[row["dst"]][1] += 1
                if tag in dm_evt.get(row["dst"], set()):
                    dm_rx[row["dst"]][0] += 1
        else:
            for nid in leaf_ids:
                if nid == row["src"]:
                    continue
                ch_rx[nid][1] += 1
                if tag in chan_evt.get(nid, set()):
                    ch_rx[nid][0] += 1
    for nid in leaf_ids:                                               # received-message delay from the live pushes
        for tg, ts in deliv_first.get(nid, {}).items():
            sts = send_ts_by_tag.get(tg)
            if sts is not None:
                node_lat[nid].append(ts - sts)
    per_node = []
    for nid in leaf_ids:
        d = duty.get(nid, {})
        at0, at1 = d.get("start_airtime_ms"), d.get("final_airtime_ms")
        per_node.append({
            "node": nid, "measured": nid in meas,                     # UNMEASURED -> the report shows that, not a false 0
            "dm_rx": dm_rx[nid][0], "dm_addressed": dm_rx[nid][1], "dm_rx_pct": _pct(dm_rx[nid][0], dm_rx[nid][1]),
            "ch_rx": ch_rx[nid][0], "ch_expected": ch_rx[nid][1], "ch_rx_pct": _pct(ch_rx[nid][0], ch_rx[nid][1]),
            "delay_mean_s": _mean(node_lat[nid]),
            "delay_max_s": round(max(node_lat[nid]), 2) if node_lat[nid] else None,
            "duty_start_pct": d.get("start_pct"), "duty_final_pct": d.get("final_pct"),
            "airtime_delta_s": (round((at1 - at0) / 1000.0, 1) if at0 is not None and at1 is not None else None),
            "rxbad": rxbad.get(nid),
        })

    # --- PER-ORIGIN channel coverage (the orphaning) + time-to-coverage (best-effort, from the live CH pushes) ---
    cov, t2c = {}, []
    for m in per_msg:
        if m["kind"] != "chan":
            continue
        c = cov.setdefault(m["src"], {"imm": [], "evt": [], "recv": m["receivers"]})
        c["imm"].append(len(m["immediate"]))
        c["evt"].append(m["eventual"])
        pushes = [deliv_first[nid][m["tag"]] for nid in leaf_ids                      # receivers whose live CH push we caught
                  if nid != m["src"] and m["tag"] in deliv_first.get(nid, {})]
        sts = send_ts_by_tag.get(m["tag"])
        if pushes and sts is not None:
            t2c.append(max(pushes) - sts)                             # send -> the latest receiver we timed (propagation span)
    chan_coverage = [{"origin": s, "msgs": len(c["evt"]), "receivers": c["recv"],
                      "flood_avg": _mean(c["imm"]), "eventual_avg": _mean(c["evt"]),
                      "worst": min(c["evt"]) if c["evt"] else None}
                     for s, c in sorted(cov.items())]
    repair_rescued = chan_evt_hits - chan_imm_hits                    # eventual-only = what the slow digest-repair added

    acked_ok = not dm_fails                                            # lenient: only TOTAL losses fail (warns don't)
    chan_ok = not chan_fails
    return {
        "run_id": run_id,
        "dm": {"total": dm_total, "delivered": dm_delivered, "deliv_den": dm_deliv_den,
               "delivered_pct": _pct(dm_delivered, dm_deliv_den),
               "ack_required": dm_ack_den, "e2e_acked": dm_e2e_acked, "hop_acked": dm_hop_acked,
               "warnings": dm_warns, "fails": dm_fails,
               "latency_p50_s": _p(dm_lats, 0.50), "latency_p95_s": _p(dm_lats, 0.95),
               "ack_rtt_p50_s": _p(dm_rtts, 0.50), "ack_rtt_p95_s": _p(dm_rtts, 0.95)},
        "chan": {"total": chan_total, "expected": chan_exp, "eventual_s": eventual_s,
                 "immediate_hits": chan_imm_hits, "immediate_pct": _pct(chan_imm_hits, chan_exp),
                 "eventual_hits": chan_evt_hits, "eventual_pct": _pct(chan_evt_hits, chan_exp),
                 "flood_pct": _pct(chan_flood_hits, chan_exp),
                 "repair_rescued": repair_rescued, "repair_rescued_pct": _pct(repair_rescued, chan_exp),
                 "t2c_mean_s": _mean(t2c), "t2c_p95_s": _p(t2c, 0.95), "t2c_timed": len(t2c),
                 "coverage": chan_coverage, "fails": chan_fails},
        "per_node": per_node,
        "measured": sorted(meas), "unmeasured": sorted(set(leaf_ids) - meas),
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
    # per-node receiver rollup + per-origin channel coverage + flood-vs-repair
    pn = {p["node"]: p for p in r["per_node"]}
    assert pn[2]["dm_rx_pct"] == 100.0 and pn[2]["ch_rx_pct"] == 100.0, pn[2]   # node2 got its DM + the channel
    assert pn[4]["dm_rx_pct"] == 0.0 and pn[4]["ch_rx_pct"] == 0.0, pn[4]       # node4 missed both (addressed/expected, 0 rx)
    assert pn[2]["delay_mean_s"] == 0.5, pn[2]                                   # node2 received-delays [0.4, 0.6] -> mean 0.5
    cov = {c["origin"]: c for c in r["chan"]["coverage"]}
    assert cov[1]["flood_avg"] == 1.0 and cov[1]["eventual_avg"] == 2.0 and cov[1]["worst"] == 2, cov[1]
    assert r["chan"]["repair_rescued"] == 1, r["chan"]                           # node3 was eventual-only = the repair rescued it
    # UNMEASURED node4 (flaky pull): its DM#1 loss + the channel-miss become UNKNOWN (not fails) -> the run now PASSES,
    # and node4 is excluded from the channel denominator (3 receivers -> 2). The false-orphan/false-loss fix.
    rm = reconcile(send_ledger, inboxes_evt, inboxes_imm, acks, deliveries, leaf, rid, eventual_s=300,
                   e2e_ack_live=e2e_live, measured=[1, 2, 3])
    assert rm["dm"]["fails"] == [] and rm["dm"]["deliv_den"] == 2, rm["dm"]       # DM#1 to the unread node4 = unmeasured
    assert rm["chan"]["expected"] == 2 and rm["chan"]["fails"] == [], rm["chan"]  # node4 dropped from the denominator
    assert rm["unmeasured"] == [4] and rm["verdict"]["pass"] is True, rm["verdict"]
    assert {p["node"]: p["measured"] for p in rm["per_node"]}[4] is False, rm["per_node"]

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
