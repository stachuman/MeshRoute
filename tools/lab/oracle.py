# MeshRoute lab harness — Phase 2 oracle: live capture -> issue -> settle pull (immediate) -> late re-pull (eventual)
# -> reconcile. Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# Active phase runs in LIVE mode for every node (ACKED/RECV/CH arrive async); request() is used only for the quiet
# baseline + the two pull_inbox rounds (after stop_live). The durable inbox is the truth: the settle pull is the
# flood/early-repair snapshot (IMMEDIATE), the @eventual_s re-pull catches what the repair delivered late (EVENTUAL,
# the gated criterion). `-v` adds a host-relative live event stream + a full per-message dump; default is unchanged.
import os
import re
import time
import threading
from queue import Queue, Empty

from . import workload
from . import reconcile as _reconcile
from . import registry
from . import report
from . import parsers
from .inbox import parse_inbox_lines
from .tag import parse_tag, make_tag

_CTR = re.compile(r"ctr=(\d+)")
_FROM = re.compile(r"from=(\d+)")


def _tag_str(line):
    t = parse_tag(line)
    return make_tag(t["run"], t["src"], t["n"]) if t else None


def _filter(parsed, baseline_pair):
    bdm, bch = baseline_pair
    return {"dm":   [r for r in parsed["dm"]   if r.get("seq", 0) > bdm],
            "chan": [r for r in parsed["chan"] if r.get("seq", 0) > bch]}


def _drain_pending(client, node_id, acks, deliveries, e2e_live, lock):
    """After stop_live, lines route to _rxq instead of cb; the next request()'s _flush DISCARDS them. RECV/CH are
    recovered by the durable pull, but the hop-ACKED/FAILED + E2E-ACKED pushes are LIVE-ONLY (never in the inbox).
    A dropped E2E-ACKED only costs an rtt SAMPLE — the e2e verdict stands on the SENDER's durable receipt (the inbox).
    Drain _rxq here, folding hop-acks + e2e-ack timestamps + (for latency) deliveries, before the pull flushes."""
    if client is None:
        return
    while True:
        try:
            ts, line = client._rxq.get_nowait()
        except Empty:
            break
        if line.startswith("E2E-ACKED ctr="):                         # the END-TO-END ack (rtt sample); verdict = the receipt
            m = _CTR.search(line)
            if m:
                with lock:
                    e2e_live[(node_id, int(m.group(1)))] = ts
        elif line.startswith("ACKED ctr=") or line.startswith("FAILED ctr="):   # the first-HOP ack (reported, not gated)
            m = _CTR.search(line)
            if m:
                with lock:
                    acks[node_id][int(m.group(1))] = "ACKED" if line.startswith("ACKED") else "FAILED"
        elif line.startswith("RECV from=") or line.startswith("CH "):
            tg = _tag_str(line)
            if tg:
                with lock:
                    deliveries[node_id].append({"ts": ts, "kind": "RECV" if line.startswith("RECV") else "CH", "tag": tg})


def run(manager, scenario, run_id, run_dir, settle_s=30.0, eventual_s=300.0, verbose=False,
        inter_gap_s=0.3, send_ack_timeout=2.5, reattach_timeout=8.0,
        pull_attempts=6, pull_retry_delay_s=2.5):
    os.makedirs(run_dir, exist_ok=True)
    t0 = time.time()
    nodes = manager.responsive()
    if not nodes:
        raise RuntimeError("oracle: no responsive nodes")
    leaf_ids = [n.node_id for n in nodes]
    node_by_id = {n.node_id: n for n in nodes}
    lock = threading.Lock()

    def vprint(msg):
        if verbose:
            with lock:
                print(msg, flush=True)

    def _robust_pull(n, attempts, delay):
        """pull_inbox with HARD retry: a response with no `inbox_end` terminator = the pull FAILED (flaky USB-CDC) —
        reattach, wait `delay`s, retry, up to `attempts`. Returns (lines, ok); ok=False = the node is UNMEASURED this
        run (NOT scored 0 — a node we could not read is not a delivery miss). The flaky-fleet measurement-truth fix."""
        lines = []
        for k in range(attempts):
            lines = manager.request(n, "pull_inbox 0 0", "inbox_end", 8.0)
            if parse_inbox_lines(lines).get("end"):
                return lines, True
            if k + 1 < attempts:
                vprint(f"[pull-retry] {n.node_id} {k + 1}/{attempts}: no inbox_end — reattach + wait {delay:.0f}s")
                registry.reattach(n, exclude={m.dev for m in nodes if m is not n and m.responsive}, timeout=reattach_timeout)
                time.sleep(delay)
        return lines, False

    # 0. run-isolation baseline: pre-run inbox high-water seqs (reconcile only past these; the run-unique tag is
    #    the primary guard, this is belt-and-suspenders).
    baseline = {}
    for n in nodes:
        end = parse_inbox_lines(manager.request(n, "pull_inbox 0 0", "inbox_end", 6.0)).get("end") or {}
        baseline[n.node_id] = (end.get("dm_seq", 0), end.get("chan_seq", 0))

    # 0b. per-node duty/airtime BASELINE — re-read at the end for the consumption DELTA. Single-line console outputs:
    #     parse_duty -> pct (budget pressure), parse_status -> duty_ms = airtime_used_ms over the rolling hour (a <1h
    #     run loses nothing from the window, so final-start = airtime spent this run).
    duty = {n.node_id: {} for n in nodes}

    def _grab_duty(when):
        for n in nodes:
            dt = parsers.parse_duty(manager.request(n, "duty", "[duty]", 2.0))
            st = parsers.parse_status(manager.request(n, "status", "[status]", 2.0)) or {}
            duty[n.node_id][when + "_pct"] = dt.get("pct") if dt else None
            try:
                duty[n.node_id][when + "_airtime_ms"] = int(st["duty_ms"]) if st.get("duty_ms") is not None else None
            except (ValueError, TypeError):
                duty[n.node_id][when + "_airtime_ms"] = None
    _grab_duty("start")

    # 1. live capture: per-node queued-ctr queue + async ack map + delivery log (+ host-stamped events file).
    queued_q = {n.port: Queue() for n in nodes}
    acks = {n.node_id: {} for n in nodes}                              # src -> {ctr: ACKED|FAILED} (first-HOP ack)
    deliveries = {n.node_id: [] for n in nodes}
    e2e_ack_live = {}                                                  # (src_node_id, ctr) -> ts of the live E2E-ACKED (rtt)
    ev_files = {n.port: open(os.path.join(run_dir, f"events_{n.node_id}.log"), "w") for n in nodes}
    send_ts_by_tag = {}
    ctr_to_msg = {}                                                    # (node_id, ctr) -> (tag, send_ts)

    def cb(node, ts, line):
        try:
            ev_files[node.port].write(f"{ts:.3f} {line}\n")
        except Exception:
            pass
        if "queued ctr=" in line or "err ctr=" in line:
            queued_q[node.port].put((ts, line))
        elif line.startswith("E2E-ACKED ctr="):                       # the TRUE e2e ack arrived (rtt); verdict = the receipt
            m = _CTR.search(line)
            if not m:
                return
            ctr = int(m.group(1))
            with lock:
                e2e_ack_live[(node.node_id, ctr)] = ts
                tm = ctr_to_msg.get((node.node_id, ctr))
            fm = _FROM.search(line)
            rel = ts - (tm[1] if tm else t0)
            vprint(f"[e2e ] {node.node_id}  ctr={ctr} from={fm.group(1) if fm else '?'}  +{rel:.1f}s   {tm[0] if tm else ''}")
        elif line.startswith("ACKED ctr=") or line.startswith("FAILED ctr="):    # the first-HOP ack (reported, not gated)
            m = _CTR.search(line)
            if not m:
                return
            ctr, ok = int(m.group(1)), line.startswith("ACKED")
            with lock:
                acks[node.node_id][ctr] = "ACKED" if ok else "FAILED"
                tm = ctr_to_msg.get((node.node_id, ctr))
            rel = ts - (tm[1] if tm else t0)
            vprint(f"[hop {'ack' if ok else 'fl'}] {node.node_id}  ctr={ctr}  +{rel:.1f}s   {tm[0] if tm else ''}")
        elif line.startswith("RECV from=") or line.startswith("CH "):
            tg = _tag_str(line)
            if not tg:
                return
            kind = "RECV" if line.startswith("RECV") else "CH"
            with lock:
                deliveries[node.node_id].append({"ts": ts, "kind": kind, "tag": tg})
                sts = send_ts_by_tag.get(tg)
            fm = _FROM.search(line)
            rel = ts - (sts if sts else t0)
            vprint(f"[{'recv' if kind == 'RECV' else 'chan'}] {node.node_id} ← {fm.group(1) if fm else '?'}  +{rel:.1f}s   {tg}")

    manager.live_all(cb)

    # 2. issue actions ON THEIR SCHEDULE (burst = back-to-back, Phase-2 default; poisson = desynchronized over spread_s).
    actions = workload.build_actions(scenario, nodes, run_id)
    workload.schedule(actions, scenario)                             # assign each action's `.at` offset + sort by arrival
    if str(scenario.get("pacing", "burst")).strip().lower() == "poisson":
        vprint(f"[pacing] poisson spread_s={scenario.get('spread_s', 120)} seed={scenario.get('seed', 0)} — sends desynchronized")
    issue_t0 = time.time()
    send_ledger, hangs = [], []
    for a in actions:
        wait = (issue_t0 + a.at) - time.time()                       # hold until this action's scheduled arrival
        if wait > 0:
            time.sleep(wait)
        src = node_by_id[a.src]
        while not queued_q[src.port].empty():                         # drain any stale queued line
            try:
                queued_q[src.port].get_nowait()
            except Empty:
                break
        host_send_ts = time.time()
        with lock:
            send_ts_by_tag[a.tag] = host_send_ts
        if not manager.send(src, a.command()):
            # the USB-CDC link to src dropped (it re-enumerates after a moment) — re-attach by USB serial / whoami,
            # re-wire the live stream onto the fresh client, and retry THIS send. node.port stays the stable queue key.
            vprint(f"[reattach] {a.src} port dropped ({src.error}) — re-scanning up to {reattach_timeout:.0f}s…")
            exclude = {n.dev for n in nodes if n is not src and n.responsive}
            if registry.reattach(src, exclude=exclude, timeout=reattach_timeout):
                manager.on_live(src, cb)                                 # re-wire the live callback onto the new client
                vprint(f"[reattach] {a.src} back on {src.dev}")
                if not manager.send(src, a.command()):                   # retry on the fresh link
                    hangs.append(a.src)
                    continue
            else:
                vprint(f"[reattach] {a.src} did not return within {reattach_timeout:.0f}s — skipping its action")
                hangs.append(a.src)
                continue
        ctr = None
        try:
            _, line = queued_q[src.port].get(timeout=send_ack_timeout)
            m = _CTR.search(line)
            if m and "queued" in line:
                ctr = int(m.group(1))
        except Empty:
            hangs.append(a.src)                                       # never returned a `queued ctr=` -> a hang
        if ctr is not None:
            with lock:
                ctr_to_msg[(a.src, ctr)] = (a.tag, host_send_ts)
        send_ledger.append({"tag": a.tag, "src": a.src, "kind": a.kind, "dst": a.dst, "chan": a.chan,
                            "ack": a.ack, "enc": a.enc, "ctr": ctr, "host_send_ts": host_send_ts})
        vprint(f"[send] {a.src} → {'DM ' + str(a.dst) if a.kind == 'dm' else 'CH ' + str(a.chan)}  ctr={ctr}  @{a.at:.1f}s   {a.tag}")

    # 3. settle window (flood + early repair), then the IMMEDIATE pull.
    vprint(f"[settle] {int(settle_s)}s ...")
    time.sleep(settle_s)
    manager.stop_live()
    time.sleep(0.3)                                                   # let any in-flight cb finish (cb/_rxq/close race)
    imm_raw, inboxes_imm = {}, {}
    for n in nodes:
        _drain_pending(n.client, n.node_id, acks, deliveries, e2e_ack_live, lock)   # recover gap ACKs/RECV before _flush discards them
        lines, _ok = _robust_pull(n, 2, 1.0)                         # settle: light retry (the eventual pull is the truth)
        imm_raw[n.node_id] = lines
        inboxes_imm[n.node_id] = _filter(parse_inbox_lines(lines), baseline.get(n.node_id, (0, 0)))
        vprint(f"[pull@settle] {n.node_id}: {len(inboxes_imm[n.node_id]['dm'])} dm / {len(inboxes_imm[n.node_id]['chan'])} chan")

    # 4. EVENTUAL phase: wait out to ~eventual_s (repair is beacon-driven), re-live to catch late pushes, then re-pull.
    extra = (eventual_s or 0) - settle_s
    if extra > 0:
        manager.live_all(cb)
        vprint(f"[eventual] +{int(extra)}s more (~{int(eventual_s)}s total) for repair ...")
        time.sleep(extra)
        manager.stop_live()
        time.sleep(0.3)                                              # quiesce the cb before close + flush
    for f in ev_files.values():
        try:
            f.close()
        except Exception:
            pass

    final_raw, inboxes_evt, measured = {}, {}, set()
    if extra > 0:
        for n in nodes:
            _drain_pending(n.client, n.node_id, acks, deliveries, e2e_ack_live, lock)
            lines, ok = _robust_pull(n, pull_attempts, pull_retry_delay_s)   # the EVENTUAL pull = the truth -> retry HARD
            final_raw[n.node_id] = lines
            inboxes_evt[n.node_id] = _filter(parse_inbox_lines(lines), baseline.get(n.node_id, (0, 0)))
            if ok:
                measured.add(n.node_id)
            vprint(f"[pull@eventual] {n.node_id}: {'OK' if ok else 'UNMEASURED (pull failed)'} — "
                   f"{len(inboxes_evt[n.node_id]['dm'])} dm / {len(inboxes_evt[n.node_id]['chan'])} chan")
    else:
        final_raw, inboxes_evt = imm_raw, inboxes_imm                 # eventual disabled -> gate on the settle pull
        measured = {nid for nid in leaf_ids if parse_inbox_lines(imm_raw.get(nid, [])).get("end")}
    if len(measured) < len(leaf_ids):
        vprint(f"[measured] {len(measured)}/{len(leaf_ids)} nodes read — UNMEASURED: {sorted(set(leaf_ids) - measured)}")

    for n in nodes:                                                   # raw durable record (the most-complete pull)
        with open(os.path.join(run_dir, f"inbox_{n.node_id}.ndjson"), "w") as f:
            for ln in final_raw.get(n.node_id, []):
                if ln.strip().startswith("{"):
                    f.write(ln.strip() + "\n")

    # 5. final duty/airtime read (→ consumption delta) + the per-node CRC-storm count (events-log scan), then reconcile.
    _grab_duty("final")
    rxbad = {}                                                        # best-effort: counts `[rxbad st=-7]` CRC lines the node
    for n in nodes:                                                   # logged this run (richer with `debug on`; 0 if none emitted)
        cnt = 0
        try:
            with open(os.path.join(run_dir, f"events_{n.node_id}.log")) as f:
                cnt = sum(1 for ln in f if "rxbad" in ln or "st=-7" in ln)
        except OSError:
            pass
        rxbad[n.node_id] = cnt

    # 6. reconcile + report.
    result = _reconcile.reconcile(send_ledger, inboxes_evt, inboxes_imm, acks, deliveries, leaf_ids, run_id,
                                  hangs=sorted(set(hangs)), eventual_s=eventual_s, e2e_ack_live=e2e_ack_live,
                                  duty=duty, rxbad=rxbad, measured=sorted(measured))
    report.write_reports(run_dir, result, send_ledger)
    return result
