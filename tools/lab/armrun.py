# MeshRoute lab harness — arm-and-read run: the LOW-USB oracle. Arm each node with the firmware scheduler
# (testclear + testsend/testch), wait with NO live serial stream, then read uptime (clock-align) + robust-pull the
# durable inbox -> reconcile. USB is touched only at ARM + at READ, never continuously, so nodes survive flaky
# USB-CDC (the wedge that killed the live-stream oracle). Pairs with the firmware scheduled-send (spec 2026-06-24).
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
import os
import re
import time
import threading
from collections import Counter

_ARMED_RE = re.compile(r"armed=(\d+)")

try:                                     # the selftest + _patch_arm_mode need only these two (both self-contained) …
    from . import schedule
    from . import reconcile as _reconcile
except ImportError:                      # … direct-run fallback (python3 tools/lab/armrun.py); run()'s heavier deps are lazy below
    import schedule
    import reconcile as _reconcile


def _p(xs, q):
    xs = sorted(xs)
    return round(xs[min(len(xs) - 1, int(q * len(xs)))], 3) if xs else None


def _patch_arm_mode(result, send_ledger, inboxes, node_offsets, rebooted, measured):
    """Reconcile() handled tag-based delivery/coverage/per-node; here we fix the two arm-mode specifics:
      • E2E-ack — the node assigns the on-wire ctr autonomously, so per-message (dst,ctr) matching is impossible.
        The per-message WARN it produced (delivered-but-no-ack-bind) is a measurement artifact, NOT a real ack loss ->
        clear it. Report e2e as an AGGREGATE: durable receipts at the (measured) senders vs the acked DMs armed.
      • Latency — no live pushes; compute it clock-aligned from each record's `@<sendms>` + rx_ms (rebooted nodes have
        no offset -> their messages are skipped). Overwrite the per-node delay + the network p50/p95.
    Mutates `result` in place."""
    leaf_ids = [p["node"] for p in result["per_node"]]
    receipts = _reconcile._receipts(inboxes, leaf_ids)                # {sender: {(origin, ctr)}}
    result["dm"]["e2e_acked"]   = sum(len(receipts.get(nid, set())) for nid in measured)
    result["dm"]["ack_required"] = sum(1 for r in send_ledger if r.get("ack") and r["src"] in measured)
    result["dm"]["e2e_note"]    = "aggregate count (arm-mode: per-send ctr is node-assigned, not bindable)"
    result["dm"]["warnings"]    = []                                 # delivered_no_ack is spurious in arm-mode

    per_tag, per_node_lat = schedule.arm_latencies(send_ledger, inboxes, node_offsets)
    result["dm"]["latency_p50_s"] = _p(list(per_tag.values()), 0.50)
    result["dm"]["latency_p95_s"] = _p(list(per_tag.values()), 0.95)
    result["dm"]["latency_note"]  = "clock-aligned, approximate (±tens of ms; rebooted nodes excluded)"
    for p in result["per_node"]:
        vs = per_node_lat.get(p["node"], [])
        p["delay_mean_s"] = round(sum(vs) / len(vs), 2) if vs else None
        p["delay_max_s"]  = round(max(vs), 2) if vs else None
    result["rebooted"] = sorted(rebooted)
    return result


def run(manager, scenario, run_id, run_dir, duration_s, settle_s=30.0, verbose=False,
        reattach_timeout=8.0, pull_attempts=6, pull_retry_delay_s=2.5, arm_pace_s=0.15, arm_attempts=3):
    from . import workload, registry, parsers, report          # run()-only deps (package context on metal)
    from .inbox import parse_inbox_lines
    from .oracle import _filter
    os.makedirs(run_dir, exist_ok=True)
    nodes = manager.responsive()
    if not nodes:
        raise RuntimeError("armrun: no responsive nodes")
    leaf_ids = [n.node_id for n in nodes]
    node_by_id = {n.node_id: n for n in nodes}
    lock = threading.Lock()

    def vprint(m):
        if verbose:
            with lock:
                print(m, flush=True)

    def _robust_pull(n):                                             # (mirrors oracle._robust_pull; minimal dup to avoid an oracle refactor)
        lines = []
        for k in range(pull_attempts):
            lines = manager.request(n, "pull_inbox 0 0", "inbox_end", 8.0)
            if parse_inbox_lines(lines).get("end"):
                return lines, True
            if k + 1 < pull_attempts:
                vprint(f"[pull-retry] {n.node_id} {k + 1}/{pull_attempts}: reattach + wait {pull_retry_delay_s:.0f}s")
                registry.reattach(n, exclude={m.dev for m in nodes if m is not n and m.responsive}, timeout=reattach_timeout)
                time.sleep(pull_retry_delay_s)
        return lines, False

    def _arm_node(n, cmds, expected):
        """Arm WITH CONFIRMATION: send each line via request() (waits for its echo) and parse the cumulative `armed=`
        from the testsend/testch echoes. The node is fully armed iff the final cumulative == `expected`. On a shortfall
        (a dropped command / parse reject — a USB hiccup at arm), reattach + re-arm from `testclear` (idempotent:
        testclear resets seq → the re-armed tags are identical). Returns (armed, ok). This catches the SILENT under-arm
        that made a well-connected node (e.g. 138/156) look orphaned when it had simply never been told to send."""
        armed = -1
        for k in range(arm_attempts):
            armed = 0
            for line in cmds:
                if line.startswith("testclear"):
                    manager.request(n, line, "cleared", 2.5)
                else:                                                # testsend/testch echo carries `armed=<cumulative>`
                    m = _ARMED_RE.search(" ".join(manager.request(n, line, "armed=", 3.0)))
                    if m:
                        armed = int(m.group(1))
                time.sleep(arm_pace_s)
            if armed == expected:
                return armed, True
            if k + 1 < arm_attempts:
                vprint(f"[arm-retry] {n.node_id} {k + 1}/{arm_attempts}: armed={armed}≠{expected} — reattach + re-arm")
                registry.reattach(n, exclude={m.dev for m in nodes if m is not n and m.responsive}, timeout=reattach_timeout)
                time.sleep(0.5)
        return armed, False

    # 0. baseline inbox high-water (run-isolation; the run-unique tag is the primary guard).
    baseline = {}
    for n in nodes:
        end = parse_inbox_lines(manager.request(n, "pull_inbox 0 0", "inbox_end", 6.0)).get("end") or {}
        baseline[n.node_id] = (end.get("dm_seq", 0), end.get("chan_seq", 0))

    # 1. build the schedule host-side + ARM each node (testclear + testsend/testch). Records the send-ledger.
    actions = workload.build_actions(scenario, nodes, run_id)
    workload.schedule(actions, scenario)                             # set each .at (realistic/poisson)
    per_node_cmds, send_ledger = schedule.build_schedule(actions, run_id)
    expected = Counter(r["src"] for r in send_ledger)
    under_armed = {}                                                 # nid -> (armed, expected): could NOT be fully armed (NOT a delivery loss)
    for nid, cmds in per_node_cmds.items():
        n = node_by_id.get(nid)
        if n is None:
            continue
        armed, ok = _arm_node(n, cmds, expected[nid])
        if ok:
            vprint(f"[arm] {nid}: armed {armed}/{expected[nid]} ✓ ({len(cmds)} lines)")
        else:
            under_armed[nid] = (armed, expected[nid])
            vprint(f"[arm] ⚠ {nid}: UNDER-ARMED {armed}/{expected[nid]} after {arm_attempts} tries — EXCLUDED (sends unreliable)")
    if under_armed:                                                  # an unreliably-armed ORIGIN: don't reconcile its sends as losses
        send_ledger = [r for r in send_ledger if r["src"] not in under_armed]
        vprint(f"[arm] ⚠ UNDER-ARMED {under_armed} excluded from the send-side — investigate (re-flash/USB) these nodes")
    vprint(f"[armed] {len(send_ledger)} msgs across {len(per_node_cmds) - len(under_armed)} nodes "
           f"({len(under_armed)} under-armed) — running {int(duration_s)}s autonomously, NO live USB")

    # 2. wait out the run window with ZERO USB (the nodes fire over the radio). Connect + `teststatus` by hand if curious.
    time.sleep(duration_s + settle_s)

    # 3. teardown: read uptime (clock offset; reboot-detect) + robust-pull the durable inbox.
    node_offsets, rebooted, final_raw, inboxes_evt, measured = {}, set(), {}, {}, set()
    run_ms = (duration_s + settle_s) * 1000.0
    for n in nodes:
        st = parsers.parse_status(manager.request(n, "status", "[status]", 2.0)) or {}
        host_now = time.time() * 1000.0
        try:
            up = int(st["uptime_ms"]) if st.get("uptime_ms") is not None else None
        except (ValueError, TypeError):
            up = None
        if up is not None and up >= run_ms * 0.9:                    # uptime covers the run -> a continuous clock to align
            node_offsets[n.node_id] = host_now - up
        elif up is not None:
            rebooted.add(n.node_id)                                  # booted mid-run -> millis reset -> no usable offset
        lines, ok = _robust_pull(n)
        final_raw[n.node_id] = lines
        inboxes_evt[n.node_id] = _filter(parse_inbox_lines(lines), baseline.get(n.node_id, (0, 0)))
        if ok:
            measured.add(n.node_id)
        vprint(f"[read] {n.node_id}: {'OK' if ok else 'UNMEASURED'}{' REBOOTED' if n.node_id in rebooted else ''} — "
               f"{len(inboxes_evt[n.node_id]['dm'])} dm / {len(inboxes_evt[n.node_id]['chan'])} chan")
    if rebooted:
        vprint(f"[rebooted] {sorted(rebooted)} reset their clock mid-run — latency excluded (delivery/coverage stand)")

    for n in nodes:                                                  # durable raw record
        with open(os.path.join(run_dir, f"inbox_{n.node_id}.ndjson"), "w") as f:
            for ln in final_raw.get(n.node_id, []):
                if ln.strip().startswith("{"):
                    f.write(ln.strip() + "\n")

    # 4. reconcile (tag-based delivery/coverage/per-node/measured; no acks/live-pushes), then patch the arm specifics.
    result = _reconcile.reconcile(send_ledger, inboxes_evt, inboxes_evt, acks={}, deliveries={},
                                  leaf_ids=leaf_ids, run_id=run_id, eventual_s=duration_s + settle_s,
                                  measured=sorted(measured))
    _patch_arm_mode(result, send_ledger, inboxes_evt, node_offsets, rebooted, measured)
    result["under_armed"] = {nid: list(v) for nid, v in under_armed.items()}   # origins we couldn't fully arm (excluded above)
    text = report.write_reports(run_dir, result, send_ledger)
    return result, text


def _selftest():
    # _patch_arm_mode: e2e becomes an aggregate count + the per-message WARN is cleared; latency comes from @sendms.
    send_ledger = [{"tag": "Tr1S7#0", "src": 7, "kind": "dm", "dst": 9, "ack": True},
                   {"tag": "Tr1S7#1", "src": 7, "kind": "dm", "dst": 9, "ack": True}]
    inboxes = {7: {"dm": [{"type": "e2e_ack", "origin": 9, "ctr": 4}], "chan": []},     # one durable receipt at sender 7
               9: {"dm": [{"body": "Tr1S7#0@200", "rx_ms": 900}, {"body": "Tr1S7#1@1000", "rx_ms": 1700}], "chan": []}}
    result = {"dm": {"warnings": [{"tag": "Tr1S7#0"}], "fails": [], "latency_p50_s": None, "latency_p95_s": None},
              "per_node": [{"node": 7, "delay_mean_s": None, "delay_max_s": None},
                           {"node": 9, "delay_mean_s": None, "delay_max_s": None}]}
    _patch_arm_mode(result, send_ledger, inboxes, {7: 1000, 9: 500}, set(), {7, 9})
    assert result["dm"]["e2e_acked"] == 1 and result["dm"]["ack_required"] == 2, result["dm"]   # 1 receipt / 2 acked armed
    assert result["dm"]["warnings"] == [], result["dm"]                                          # spurious WARN cleared
    assert result["dm"]["latency_p50_s"] is not None, result["dm"]                               # latency from @sendms
    pn9 = next(p for p in result["per_node"] if p["node"] == 9)
    assert pn9["delay_mean_s"] == 0.2, pn9    # both msgs: (500+900)-(1000+200)=200ms, (500+1700)-(1000+1000)=200ms
    assert result["rebooted"] == [], result
    print("armrun selftest OK")


if __name__ == "__main__":
    _selftest()
