# MeshRoute lab harness — report: summary.txt + the run-dir artifacts (send_ledger.jsonl, reconcile.json).
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# (events_<node>.log + inbox_<node>.ndjson are written live by oracle.py during the run.)
import os
import json


def _pct(n, d):
    return round(100.0 * n / d, 1) if d else None


def _f(x, suf="", dash="-"):
    return f"{x}{suf}" if x is not None else dash


def format_summary(r):
    """Human-readable summary — three AGGREGATE blocks (NETWORK totals · PER-NODE receiver table · per-origin CHANNEL
    COVERAGE), then a one-line fail pointer. The full per-message WARN/FAIL detail lives in reconcile.json + the `-v`
    dump (format_per_msg) — at 200+ messages the per-line lists don't scale; the rollups are what you review."""
    dm, ch, v = r["dm"], r["chan"], r["verdict"]
    pn = r.get("per_node", [])
    unmeas = r.get("unmeasured", [])
    e2e_pct = _pct(dm["e2e_acked"], dm["ack_required"])
    deliv_den = dm.get("deliv_den", dm["total"])
    head = (f"=== oracle run {r['run_id']} — {'PASS' if v['pass'] else 'FAIL'} ===   "
            f"{len(pn)} nodes · {dm['total']} DM + {ch['total']} chan")
    if unmeas:
        head += (f"\n  ⚠ measured {len(pn) - len(unmeas)}/{len(pn)} — UNMEASURED {unmeas} "
                 f"excluded from all rates (flaky serial pull, NOT delivery loss)")
    under_armed = r.get("under_armed", {})
    if under_armed:
        head += (f"\n  ⚠ UNDER-ARMED {sorted(under_armed)} — these origins were not fully armed over USB "
                 f"(arm failure, NOT a flood loss); their sends are excluded — re-flash/check those nodes")
    L = [head,
         "",
         "NETWORK",
         f"  DM       {dm['delivered']}/{deliv_den} delivered {_f(dm['delivered_pct'],'%')}"
         f"{' (of ' + str(dm['total']) + ' sent)' if deliv_den != dm['total'] else ''}   "
         f"e2e-acked {dm['e2e_acked']}/{dm['ack_required']} {_f(e2e_pct,'%')}   "
         f"lat p50 {_f(dm['latency_p50_s'],'s')} / p95 {_f(dm['latency_p95_s'],'s')}",
         f"  CHANNEL  flood {_f(ch['immediate_pct'],'%')}  →  eventual {_f(ch['eventual_pct'],'%')}   "
         f"(repair rescued {_f(ch.get('repair_rescued_pct'),'%')})   missed {len(ch['fails'])}/{ch['expected']}   "
         f"t2c mean {_f(ch.get('t2c_mean_s'),'s')} / p95 {_f(ch.get('t2c_p95_s'),'s')}"]
    duties = [p["duty_final_pct"] for p in pn if p.get("duty_final_pct") is not None]
    rxbads = [p["rxbad"] for p in pn if p.get("rxbad") is not None]
    if duties or rxbads:
        seg = "  "
        if duties:
            mx = max((p for p in pn if p.get("duty_final_pct") is not None), key=lambda p: p["duty_final_pct"])
            seg += f"duty     mean {round(sum(duties) / len(duties))}%  max {mx['duty_final_pct']}%@{mx['node']}"
        if rxbads:
            seg += ("      " if duties else "") + f"CRC-storm  {sum(rxbads)} bad-RX total"
        L.append(seg)

    L += ["", "PER NODE (receiver view)",
          "  node   DM rx         CH rx          delay mean/max    duty s→f      air     rxbad"]
    for p in pn:
        if not p.get("measured", True):                              # pull failed -> don't render false zeros
            L.append(f"  {p['node']:<5}  UNMEASURED — serial pull failed (the node likely received fine; "
                     f"excluded from all rates)   rxbad {_f(p['rxbad'])}")
            continue
        dmrx = f"{p['dm_rx']}/{p['dm_addressed']} {_f(p['dm_rx_pct'],'%')}"
        chrx = f"{p['ch_rx']}/{p['ch_expected']} {_f(p['ch_rx_pct'],'%')}"
        dly = f"{_f(p['delay_mean_s'],'s')} / {_f(p['delay_max_s'],'s')}"
        duty = f"{_f(p['duty_start_pct'],'%','?')}→{_f(p['duty_final_pct'],'%','?')}"
        under = ((p["ch_rx_pct"] is not None and p["ch_rx_pct"] < 80)
                 or (p["dm_rx_pct"] is not None and p["dm_rx_pct"] < 80))
        L.append(f"  {p['node']:<5}  {dmrx:<12}  {chrx:<13}  {dly:<15}  {duty:<11}  "
                 f"{_f(p['airtime_delta_s'],'s'):<6}  {_f(p['rxbad']):<4}{' ⚠' if under else ''}")

    cov = ch.get("coverage", [])
    if cov:
        L += ["", "CHANNEL COVERAGE (per origin — propagation reach)",
              "  origin  msgs   flood avg    eventual avg   worst"]
        for c in cov:
            recv = c["receivers"]
            orphan = c["worst"] is not None and c["worst"] <= recv * 0.25      # a message reaching ≤¼ the leaf = orphaned
            L.append(f"  {c['origin']:<6}  {c['msgs']:<4}   {_f(c['flood_avg']):<4}/{recv:<6}  "
                     f"{_f(c['eventual_avg']):<4}/{recv:<7}  {_f(c['worst'])}/{recv}{' ⚠ ORPHAN' if orphan else ''}")

    nf_dm, nf_ch, nw = len(dm["fails"]), len(ch["fails"]), len(dm.get("warnings", []))
    if nf_dm or nf_ch or nw or v["hangs"]:
        L += ["", f"detail: {nf_dm} DM total-loss · {nw} DM ack-lost(warn) · {nf_ch} channel-miss · "
                  f"hangs {v['hangs'] or '-'}   → reconcile.json / -v"]
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
                  "repair_rescued": 1, "repair_rescued_pct": 33.3, "t2c_mean_s": 0.7, "t2c_p95_s": 0.9, "t2c_timed": 1,
                  "coverage": [{"origin": 1, "msgs": 1, "receivers": 3, "flood_avg": 1.0, "eventual_avg": 2.0, "worst": 2}],
                  "fails": [{"src": 1, "chan": 0, "tag": "Tr1S1#1c", "eventual": 2, "receivers": 3, "missed": [4]}]},
         "per_node": [
             {"node": 2, "measured": True, "dm_rx": 1, "dm_addressed": 1, "dm_rx_pct": 100.0, "ch_rx": 1, "ch_expected": 1, "ch_rx_pct": 100.0,
              "delay_mean_s": 0.5, "delay_max_s": 0.6, "duty_start_pct": 10, "duty_final_pct": 22, "airtime_delta_s": 4.2, "rxbad": 3},
             {"node": 4, "measured": True, "dm_rx": 0, "dm_addressed": 1, "dm_rx_pct": 0.0, "ch_rx": 0, "ch_expected": 1, "ch_rx_pct": 0.0,
              "delay_mean_s": None, "delay_max_s": None, "duty_start_pct": 8, "duty_final_pct": 15, "airtime_delta_s": 1.1, "rxbad": 9}],
         "measured": [2, 4], "unmeasured": [],
         "verdict": {"acked_dms_ok": False, "channels_eventual_ok": False, "hangs": [], "pass": False},
         "per_msg": [
             {"kind": "dm", "src": 1, "dst": 2, "ctr": 5, "delivered": True, "e2e_acked": True, "hop_acked": True,
              "status": "acked", "latency_s": 0.4, "ack_rtt_s": 2.0, "tag": "Tr1S1#0"},
             {"kind": "dm", "src": 2, "dst": 3, "ctr": 6, "delivered": True, "e2e_acked": False, "hop_acked": True,
              "status": "delivered_no_ack", "latency_s": None, "ack_rtt_s": None, "tag": "Tr1S2#0"},
             {"kind": "chan", "src": 1, "receivers": 3, "reached": [2, 3], "eventual": 2, "missed": [4], "tag": "Tr1S1#1c"},
         ]}
    s = format_summary(r)
    assert "FAIL" in s and "2 nodes · 3 DM + 1 chan" in s and "e2e-acked 1/3 33.3%" in s, s
    assert "flood 33.3%" in s and "eventual 66.7%" in s and "repair rescued 33.3%" in s, s
    assert "duty     mean 18%  max 22%@2" in s and "CRC-storm  12 bad-RX total" in s, s
    assert "PER NODE (receiver view)" in s and "CHANNEL COVERAGE" in s and "ORPHAN" not in s, s   # worst 2/3 > ¼ → not orphan
    assert "⚠" in s and "detail: 1 DM total-loss · 1 DM ack-lost(warn) · 1 channel-miss" in s, s
    # UNMEASURED rendering: the caveat header, the measured DM denominator ("of N sent"), and the UNMEASURED row.
    ru = {"run_id": "u1",
          "dm": {"total": 5, "delivered": 3, "deliv_den": 3, "delivered_pct": 100.0, "ack_required": 3, "e2e_acked": 3,
                 "hop_acked": 3, "warnings": [], "fails": [], "latency_p50_s": 1.0, "latency_p95_s": 1.0,
                 "ack_rtt_p50_s": None, "ack_rtt_p95_s": None},
          "chan": {"total": 0, "expected": 0, "eventual_s": 600, "immediate_hits": 0, "immediate_pct": None,
                   "eventual_hits": 0, "eventual_pct": None, "flood_pct": None, "repair_rescued": 0,
                   "repair_rescued_pct": None, "t2c_mean_s": None, "t2c_p95_s": None, "t2c_timed": 0, "coverage": [], "fails": []},
          "per_node": [{"node": 7, "measured": True, "dm_rx": 3, "dm_addressed": 3, "dm_rx_pct": 100.0, "ch_rx": 0,
                        "ch_expected": 0, "ch_rx_pct": None, "delay_mean_s": 1.0, "delay_max_s": 1.0,
                        "duty_start_pct": 5, "duty_final_pct": 9, "airtime_delta_s": 2.0, "rxbad": 1},
                       {"node": 9, "measured": False, "dm_rx": 0, "dm_addressed": 2, "dm_rx_pct": None, "ch_rx": 0,
                        "ch_expected": 0, "ch_rx_pct": None, "delay_mean_s": None, "delay_max_s": None,
                        "duty_start_pct": None, "duty_final_pct": None, "airtime_delta_s": None, "rxbad": 5}],
          "measured": [7], "unmeasured": [9], "under_armed": {138: [0, 2]},
          "verdict": {"acked_dms_ok": True, "channels_eventual_ok": True, "hangs": [], "pass": True}}
    su = format_summary(ru)
    assert "measured 1/2 — UNMEASURED [9]" in su, su
    assert "3/3 delivered 100.0% (of 5 sent)" in su, su
    assert "UNMEASURED — serial pull failed" in su, su
    assert "UNDER-ARMED [138]" in su and "NOT a flood loss" in su, su

    p = format_per_msg(r)
    assert "DM  1→2" in p and "e2e✓ hop✓" in p and "rtt=2.0s" in p, p
    assert "DM  2→3" in p and "e2e✗" in p and "CH  1   reach 2/3" in p and "✗4" in p, p
    print("report selftest OK")


if __name__ == "__main__":
    _selftest()
