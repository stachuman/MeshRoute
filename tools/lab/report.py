# MeshRoute lab harness — report: summary.txt + the run-dir artifacts (send_ledger.jsonl, reconcile.json).
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# (events_<node>.log + inbox_<node>.ndjson are written live by oracle.py during the run.)
import os
import json


def format_summary(r):
    """Human-readable summary (per-type delivery + e2e-ack + latency + the WARN/FAIL lists). DEFAULT output."""
    dm, ch, v = r["dm"], r["chan"], r["verdict"]
    es = ch.get("eventual_s")
    L = [f"=== oracle run {r['run_id']} — {'PASS' if v['pass'] else 'FAIL'} ===",
         f"DM:      {dm['delivered']}/{dm['total']} delivered ({dm['delivered_pct']}%)  "
         f"e2e-acked {dm['e2e_acked']}/{dm['ack_required']}  (hop {dm['hop_acked']}/{dm['ack_required']})",
         f"         deliv-lat p50={dm['latency_p50_s']}s p95={dm['latency_p95_s']}s  "
         f"ack-rtt p50={dm['ack_rtt_p50_s']}s p95={dm['ack_rtt_p95_s']}s",
         f"CHANNEL: immediate {ch['immediate_hits']}/{ch['expected']} ({ch['immediate_pct']}%)  ·  "
         f"eventual@{es}s {ch['eventual_hits']}/{ch['expected']} ({ch['eventual_pct']}%)  "
         f"[flood-immediate {ch['flood_pct']}%]",
         f"VERDICT: acked_dms_ok={v['acked_dms_ok']}  channels_eventual_ok={v['channels_eventual_ok']}  hangs={v['hangs']}"]
    if dm.get("warnings"):                                            # delivered, but the e2e-ack didn't return (lenient: not a fail)
        L.append("  DM WARN (delivered, but the e2e-ack didn't return — return-path loss):")
        for f in dm["warnings"]:
            L.append(f"    src={f['src']} -> dst={f['dst']} ctr={f['ctr']} hop_acked={f['hop_acked']} {f['tag']}")
    if dm["fails"]:                                                   # the only hard DM fail: an acked DM that NEVER arrived
        L.append("  DM FAILS (acked DM never delivered — total loss):")
        for f in dm["fails"]:
            L.append(f"    src={f['src']} -> dst={f['dst']} ctr={f['ctr']} delivered={f['delivered']} hop_acked={f['hop_acked']} {f['tag']}")
    if ch["fails"]:
        L.append(f"  CHANNEL FAILS (still missing @{es}s):")
        for f in ch["fails"]:
            L.append(f"    src={f['src']} chan={f['chan']} {f['tag']} reached {f['eventual']}/{f['receivers']} missed={f['missed']}")
    return "\n".join(L)


def format_per_msg(result):
    """The `-v` per-message reconcile dump (EVERY message, not just fails). deliv = the dst inbox; e2e = the sender's
    durable receipt (the gated signal); hop = the first-hop ACK (reported). lat = delivery; rtt = e2e-ack round-trip."""
    L = ["--- per-message reconcile ---"]
    for m in result["per_msg"]:
        if m["kind"] == "dm":
            d = "✓" if m["delivered"] else "✗"
            e = "-" if m.get("e2e_acked") is None else ("✓" if m["e2e_acked"] else "✗")
            h = "-" if m.get("hop_acked") is None else ("✓" if m["hop_acked"] else "✗")
            lat = f"{m['latency_s']:.1f}s" if m.get("latency_s") is not None else "-"
            rtt = f"{m['ack_rtt_s']:.1f}s" if m.get("ack_rtt_s") is not None else "-"
            L.append(f"DM  {m['src']}→{m['dst']} ctr={m['ctr']}   deliv{d} e2e{e} hop{h}  lat={lat} rtt={rtt}   {m['tag']}")
        else:
            marks = "  ".join([f"✓{n}" for n in m["reached"]] + [f"✗{n}" for n in m["missed"]])
            L.append(f"CH  {m['src']}   reach {m['eventual']}/{m['receivers']}   {marks}   {m['tag']}")
    return "\n".join(L)


def write_reports(run_dir, result, send_ledger):
    os.makedirs(run_dir, exist_ok=True)
    with open(os.path.join(run_dir, "reconcile.json"), "w") as f:
        json.dump(result, f, indent=2)
    with open(os.path.join(run_dir, "send_ledger.jsonl"), "w") as f:
        for row in send_ledger:
            f.write(json.dumps(row) + "\n")
    text = format_summary(result)
    with open(os.path.join(run_dir, "summary.txt"), "w") as f:
        f.write(text + "\n")
    return text


def _selftest():
    r = {"run_id": "r1",
         "dm": {"total": 3, "delivered": 2, "delivered_pct": 66.7, "ack_required": 3, "e2e_acked": 1, "hop_acked": 2,
                "latency_p50_s": 0.4, "latency_p95_s": 0.4, "ack_rtt_p50_s": 2.0, "ack_rtt_p95_s": 2.0,
                "warnings": [{"src": 2, "dst": 3, "ctr": 6, "delivered": True, "hop_acked": True, "tag": "Tr1S2#0"}],
                "fails": [{"src": 1, "dst": 4, "ctr": 7, "delivered": False, "hop_acked": False, "tag": "Tr1S1#1"}]},
         "chan": {"total": 1, "expected": 3, "eventual_s": 300, "immediate_hits": 1, "immediate_pct": 33.3,
                  "eventual_hits": 2, "eventual_pct": 66.7, "flood_pct": 33.3,
                  "fails": [{"src": 1, "chan": 0, "tag": "Tr1S1#1c", "eventual": 2, "receivers": 3, "missed": [4]}]},
         "verdict": {"acked_dms_ok": False, "channels_eventual_ok": False, "hangs": [], "pass": False},
         "per_msg": [
             {"kind": "dm", "src": 1, "dst": 2, "ctr": 5, "delivered": True, "e2e_acked": True, "hop_acked": True,
              "status": "acked", "latency_s": 0.4, "ack_rtt_s": 2.0, "tag": "Tr1S1#0"},
             {"kind": "dm", "src": 2, "dst": 3, "ctr": 6, "delivered": True, "e2e_acked": False, "hop_acked": True,
              "status": "delivered_no_ack", "latency_s": None, "ack_rtt_s": None, "tag": "Tr1S2#0"},
             {"kind": "chan", "src": 1, "receivers": 3, "reached": [2, 3], "eventual": 2, "missed": [4], "tag": "Tr1S1#1c"},
         ]}
    s = format_summary(r)
    assert "FAIL" in s and "e2e-acked 1/3" in s and "(hop 2/3)" in s, s
    assert "immediate 1/3" in s and "eventual@300s 2/3" in s, s
    assert "DM WARN" in s and "DM FAILS (acked DM never delivered" in s and "still missing @300s" in s, s
    p = format_per_msg(r)
    assert "DM  1→2" in p and "e2e✓ hop✓" in p and "rtt=2.0s" in p, p
    assert "DM  2→3" in p and "e2e✗" in p and "CH  1   reach 2/3" in p and "✗4" in p, p
    print("report selftest OK")


if __name__ == "__main__":
    _selftest()
