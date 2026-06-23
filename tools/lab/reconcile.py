# MeshRoute lab harness — reconcile: match send-ledger <-> receive-ledgers BY TAG; per-msg verdict + tiered aggregate.
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# Pure (dicts in, dict out) -> unit-testable offline. Inputs (all host-collected by oracle.py):
#   send_ledger      [{tag, src, kind('dm'|'chan'), dst, chan, ack, enc, ctr, host_send_ts}]
#   inboxes_eventual {node_id: {"dm":[rec…], "chan":[rec…]}}   the LATE re-pull (@eventual_s) — the truth for delivery
#   inboxes_immediate{node_id: {"dm":[rec…], "chan":[rec…]}}   the settle-end pull — the flood/early-repair snapshot
#   acks             {src_node_id: {ctr: "ACKED"|"FAILED"}}    the src's live async ack verdicts
#   deliveries       {node_id: [{ts, kind('RECV'|'CH'), tag}]} the receiver's live pushes; tag pre-parsed
#   leaf_ids [node_id]   hangs [src…]   run_id   eventual_s
#
# Tiered verdict (the locked criteria):
#   - acked DMs  -> MUST be 100% ACKED (a FAILED / never-acked -a DM = FAIL, listed)
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
    `T…#10`; the counter has no terminator, so a substring `in` test false-credits #1/#10/#11… (review finding)."""
    out = {}
    for nid in leaf_ids:
        s = set()
        for r in inboxes.get(nid, {}).get(kind, []):
            t = parse_tag(r.get("body", ""))
            if t:
                s.add(make_tag(t["run"], t["src"], t["n"]))
        out[nid] = s
    return out


def reconcile(send_ledger, inboxes_eventual, inboxes_immediate, acks, deliveries, leaf_ids, run_id,
              hangs=None, eventual_s=None):
    hangs = list(hangs or [])
    dm_evt = _tagsets(inboxes_eventual, leaf_ids, "dm")                # DM delivery: judged on the most-complete pull
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
    dm_total = dm_delivered = dm_ack_req = dm_acked = 0
    dm_fails, dm_lats = [], []
    chan_total = chan_imm_hits = chan_evt_hits = chan_flood_hits = chan_exp = 0
    chan_fails = []

    for row in send_ledger:
        tag, src = row["tag"], row["src"]
        if row["kind"] == "dm":
            dm_total += 1
            dst = row["dst"]
            delivered = tag in dm_evt.get(dst, set())
            if delivered:
                dm_delivered += 1
            acked = None
            if row.get("ack"):
                dm_ack_req += 1
                acked = acks.get(src, {}).get(row.get("ctr")) == "ACKED"
                if acked:
                    dm_acked += 1
            ft = deliv_first.get(dst, {}).get(tag)
            lat = (ft - row["host_send_ts"]) if (ft is not None and row.get("host_send_ts") is not None) else None
            if lat is not None:
                dm_lats.append(lat)
            rv = {"tag": tag, "kind": "dm", "src": src, "dst": dst, "ctr": row.get("ctr"),
                  "delivered": delivered, "acked": acked, "latency_s": lat}
            per_msg.append(rv)
            if row.get("ack") and acked is not True:                  # locked: an acked DM that wasn't ACKED = FAIL
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

    dm_lats.sort()

    def _p(q):
        return round(dm_lats[min(len(dm_lats) - 1, int(q * len(dm_lats)))], 3) if dm_lats else None

    acked_ok = not dm_fails
    chan_ok = not chan_fails
    return {
        "run_id": run_id,
        "dm": {"total": dm_total, "delivered": dm_delivered, "delivered_pct": _pct(dm_delivered, dm_total),
               "ack_required": dm_ack_req, "acked": dm_acked, "fails": dm_fails,
               "latency_p50_s": _p(0.50), "latency_p95_s": _p(0.95)},
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
    send_ledger = [
        {"tag": "Ta1S1#0", "src": 1, "kind": "dm", "dst": 2, "chan": None, "ack": True, "enc": False, "ctr": 5, "host_send_ts": 100.0},
        {"tag": "Ta1S2#0", "src": 2, "kind": "dm", "dst": 3, "chan": None, "ack": True, "enc": False, "ctr": 6, "host_send_ts": 100.0},
        {"tag": "Ta1S1#1", "src": 1, "kind": "chan", "dst": None, "chan": 0, "ack": False, "enc": False, "ctr": 7, "host_send_ts": 100.0},
    ]
    # IMMEDIATE (settle pull): node 3 has NOT yet got the channel; node 4 missed entirely.
    inboxes_imm = {1: {"dm": [], "chan": []}, 2: {"dm": [{"body": "Ta1S1#0"}], "chan": [{"body": "Ta1S1#1"}]},
                   3: {"dm": [], "chan": []}, 4: {"dm": [], "chan": []}}
    # EVENTUAL (late re-pull): the repair filled node 3; node 4 still missing = a real loss.
    inboxes_evt = {1: {"dm": [], "chan": []}, 2: {"dm": [{"body": "Ta1S1#0"}], "chan": [{"body": "Ta1S1#1"}]},
                   3: {"dm": [], "chan": [{"body": "Ta1S1#1"}]}, 4: {"dm": [], "chan": []}}
    acks = {1: {5: "ACKED"}, 2: {6: "FAILED"}}                              # DM1 acked, DM2 failed
    deliveries = {2: [{"ts": 100.4, "kind": "RECV", "tag": "Ta1S1#0"}, {"ts": 100.6, "kind": "CH", "tag": "Ta1S1#1"}]}
    r = reconcile(send_ledger, inboxes_evt, inboxes_imm, acks, deliveries, leaf, rid, eventual_s=300)

    assert r["dm"]["total"] == 2 and r["dm"]["delivered"] == 1, r["dm"]
    assert r["dm"]["acked"] == 1 and len(r["dm"]["fails"]) == 1 and r["dm"]["fails"][0]["src"] == 2, r["dm"]
    assert r["dm"]["latency_p50_s"] == 0.4, r["dm"]
    # immediate counted only node 2 (1/3); eventual counted 2+3 (2/3); node 4 = the only real loss
    assert r["chan"]["immediate_hits"] == 1 and r["chan"]["eventual_hits"] == 2 and r["chan"]["expected"] == 3, r["chan"]
    assert r["chan"]["immediate_pct"] == round(100 / 3, 1) and r["chan"]["eventual_pct"] == round(200 / 3, 1), r["chan"]
    assert len(r["chan"]["fails"]) == 1 and r["chan"]["fails"][0]["missed"] == [4], r["chan"]
    assert r["chan"]["flood_pct"] == round(100 / 3, 1) and r["chan"]["eventual_s"] == 300, r["chan"]
    assert r["verdict"]["pass"] is False, r["verdict"]

    # late re-pull RESCUES a node that was absent at settle but present at eventual -> NOT a loss, no FAIL
    imm2 = {1: {"dm": [], "chan": []}, 2: {"dm": [{"body": "Tb1S1#0"}], "chan": []},
            3: {"dm": [], "chan": []}, 4: {"dm": [], "chan": []}}
    evt2 = {1: {"dm": [], "chan": []}, 2: {"dm": [{"body": "Tb1S1#0"}], "chan": [{"body": "Tb1S1#9"}]},
            3: {"dm": [], "chan": [{"body": "Tb1S1#9"}]}, 4: {"dm": [], "chan": [{"body": "Tb1S1#9"}]}}
    sl2 = [{"tag": "Tb1S1#0", "src": 1, "kind": "dm", "dst": 2, "ack": True, "ctr": 5, "host_send_ts": 0.0},
           {"tag": "Tb1S1#9", "src": 1, "kind": "chan", "chan": 0, "ack": False, "ctr": 6, "host_send_ts": 0.0}]
    r2 = reconcile(sl2, evt2, imm2, {1: {5: "ACKED"}}, {}, leaf, "b1", eventual_s=300)
    assert r2["chan"]["immediate_hits"] == 0 and r2["chan"]["eventual_hits"] == 3, r2["chan"]   # eventual-only, all 3
    assert r2["chan"]["fails"] == [] and r2["verdict"]["pass"] is True, r2                       # NOT a loss
    # hang fails the run
    r3 = reconcile(sl2, evt2, imm2, {1: {5: "ACKED"}}, {}, leaf, "b1", hangs=[3], eventual_s=300)
    assert r3["verdict"]["pass"] is False and r3["verdict"]["hangs"] == [3], r3["verdict"]

    # REGRESSION (review finding): #1 must NOT match #10 — exact tag, not substring. DM #1 was NEVER delivered;
    # only #10 sits in the dst inbox. The old `any(tag in body)` falsely credited it.
    sl4 = [{"tag": "Tc1S1#1", "src": 1, "kind": "dm", "dst": 2, "ack": False, "ctr": 1, "host_send_ts": 0.0}]
    evt4 = {1: {"dm": [], "chan": []}, 2: {"dm": [{"body": "Tc1S1#10"}], "chan": []},
            3: {"dm": [], "chan": []}, 4: {"dm": [], "chan": []}}
    r4 = reconcile(sl4, evt4, evt4, {}, {}, leaf, "c1", eventual_s=0)
    assert r4["dm"]["delivered"] == 0, r4["dm"]                       # #1 is NOT in {#10} -> not delivered
    print("reconcile selftest OK")


if __name__ == "__main__":
    _selftest()
