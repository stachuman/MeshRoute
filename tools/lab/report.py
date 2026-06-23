# MeshRoute lab harness — report: summary.txt + the run-dir artifacts (send_ledger.jsonl, reconcile.json).
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# (events_<node>.log + inbox_<node>.ndjson are written live by oracle.py during the run.)
import os
import json


def format_summary(r):
    """Human-readable summary (per-type delivery + latency + the FAIL list). DEFAULT output — kept stable."""
    dm, ch, v = r["dm"], r["chan"], r["verdict"]
    es = ch.get("eventual_s")
    L = [f"=== oracle run {r['run_id']} — {'PASS' if v['pass'] else 'FAIL'} ===",
         f"DM:      {dm['delivered']}/{dm['total']} delivered ({dm['delivered_pct']}%)  "
         f"acked {dm['acked']}/{dm['ack_required']}  latency p50={dm['latency_p50_s']}s p95={dm['latency_p95_s']}s",
         f"CHANNEL: immediate {ch['immediate_hits']}/{ch['expected']} ({ch['immediate_pct']}%)  ·  "
         f"eventual@{es}s {ch['eventual_hits']}/{ch['expected']} ({ch['eventual_pct']}%)  "
         f"[flood-immediate {ch['flood_pct']}%]",
         f"VERDICT: acked_dms_ok={v['acked_dms_ok']}  channels_eventual_ok={v['channels_eventual_ok']}  hangs={v['hangs']}"]
    if dm["fails"]:
        L.append("  DM FAILS (acked DM not ACKED):")
        for f in dm["fails"]:
            L.append(f"    src={f['src']} -> dst={f['dst']} ctr={f['ctr']} delivered={f['delivered']} acked={f['acked']} {f['tag']}")
    if ch["fails"]:
        L.append(f"  CHANNEL FAILS (still missing @{es}s):")
        for f in ch["fails"]:
            L.append(f"    src={f['src']} chan={f['chan']} {f['tag']} reached {f['eventual']}/{f['receivers']} missed={f['missed']}")
    return "\n".join(L)


def format_per_msg(result):
    """The `-v` per-message reconcile dump (EVERY message, not just fails)."""
    L = ["--- per-message reconcile ---"]
    for m in result["per_msg"]:
        if m["kind"] == "dm":
            d = "✓" if m["delivered"] else "✗"
            a = "-" if m["acked"] is None else ("✓" if m["acked"] else "✗")
            lat = f"{m['latency_s']:.1f}s" if m.get("latency_s") is not None else "-"
            L.append(f"DM  {m['src']}→{m['dst']} ctr={m['ctr']}   deliv{d} ack{a}  lat={lat}   {m['tag']}")
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
         "dm": {"total": 2, "delivered": 1, "delivered_pct": 50.0, "ack_required": 2, "acked": 1,
                "latency_p50_s": 0.4, "latency_p95_s": 0.4,
                "fails": [{"src": 2, "dst": 3, "ctr": 6, "delivered": False, "acked": False, "tag": "Tr1S2#0"}]},
         "chan": {"total": 1, "expected": 3, "eventual_s": 300, "immediate_hits": 1, "immediate_pct": 33.3,
                  "eventual_hits": 2, "eventual_pct": 66.7, "flood_pct": 33.3,
                  "fails": [{"src": 1, "chan": 0, "tag": "Tr1S1#1", "eventual": 2, "receivers": 3, "missed": [4]}]},
         "verdict": {"acked_dms_ok": False, "channels_eventual_ok": False, "hangs": [], "pass": False},
         "per_msg": [
             {"kind": "dm", "src": 2, "dst": 3, "ctr": 6, "delivered": False, "acked": False, "latency_s": None, "tag": "Tr1S2#0"},
             {"kind": "chan", "src": 1, "receivers": 3, "reached": [2, 3], "eventual": 2, "missed": [4], "tag": "Tr1S1#1"},
         ]}
    s = format_summary(r)
    assert "FAIL" in s and "immediate 1/3" in s and "eventual@300s 2/3" in s and "still missing @300s" in s, s
    p = format_per_msg(r)
    assert "DM  2→3" in p and "CH  1   reach 2/3" in p and "✗4" in p and "✓2" in p, p
    print("report selftest OK")


if __name__ == "__main__":
    _selftest()
