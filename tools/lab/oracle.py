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
from . import report
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
        inter_gap_s=0.3, send_ack_timeout=2.5):
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

    # 0. run-isolation baseline: pre-run inbox high-water seqs (reconcile only past these; the run-unique tag is
    #    the primary guard, this is belt-and-suspenders).
    baseline = {}
    for n in nodes:
        end = parse_inbox_lines(manager.request(n, "pull_inbox 0 0", "inbox_end", 6.0)).get("end") or {}
        baseline[n.node_id] = (end.get("dm_seq", 0), end.get("chan_seq", 0))

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

    # 2. issue actions (sequential round-robin; deterministic).
    actions = workload.build_actions(scenario, nodes, run_id)
    send_ledger, hangs = [], []
    for a in actions:
        src = node_by_id[a.src]
        while not queued_q[src.port].empty():                         # drain any stale queued line
            try:
                queued_q[src.port].get_nowait()
            except Empty:
                break
        host_send_ts = time.time()
        with lock:
            send_ts_by_tag[a.tag] = host_send_ts
        manager.send(src, a.command())
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
        vprint(f"[send] {a.src} → {'DM ' + str(a.dst) if a.kind == 'dm' else 'CH ' + str(a.chan)}  ctr={ctr}   {a.tag}")
        time.sleep(inter_gap_s)

    # 3. settle window (flood + early repair), then the IMMEDIATE pull.
    vprint(f"[settle] {int(settle_s)}s ...")
    time.sleep(settle_s)
    manager.stop_live()
    time.sleep(0.3)                                                   # let any in-flight cb finish (cb/_rxq/close race)
    imm_raw, inboxes_imm = {}, {}
    for n in nodes:
        _drain_pending(n.client, n.node_id, acks, deliveries, e2e_ack_live, lock)   # recover gap ACKs/RECV before _flush discards them
        lines = manager.request(n, "pull_inbox 0 0", "inbox_end", 8.0)
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

    final_raw, inboxes_evt = {}, {}
    if extra > 0:
        for n in nodes:
            _drain_pending(n.client, n.node_id, acks, deliveries, e2e_ack_live, lock)
            lines = manager.request(n, "pull_inbox 0 0", "inbox_end", 8.0)
            final_raw[n.node_id] = lines
            inboxes_evt[n.node_id] = _filter(parse_inbox_lines(lines), baseline.get(n.node_id, (0, 0)))
            vprint(f"[pull@eventual] {n.node_id}: {len(inboxes_evt[n.node_id]['dm'])} dm / {len(inboxes_evt[n.node_id]['chan'])} chan")
    else:
        final_raw, inboxes_evt = imm_raw, inboxes_imm                 # eventual disabled -> gate on the settle pull

    for n in nodes:                                                   # raw durable record (the most-complete pull)
        with open(os.path.join(run_dir, f"inbox_{n.node_id}.ndjson"), "w") as f:
            for ln in final_raw.get(n.node_id, []):
                if ln.strip().startswith("{"):
                    f.write(ln.strip() + "\n")

    # 5. reconcile + report.
    result = _reconcile.reconcile(send_ledger, inboxes_evt, inboxes_imm, acks, deliveries, leaf_ids, run_id,
                                  hangs=sorted(set(hangs)), eventual_s=eventual_s, e2e_ack_live=e2e_ack_live)
    report.write_reports(run_dir, result, send_ledger)
    return result
