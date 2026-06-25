# MeshRoute lab harness — arm-mode schedule: workload SendActions -> per-node firmware commands + the send-ledger,
# and the clock-aligned (approximate) latency. PURE (no serial) -> unit-testable offline.
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# The low-USB run (firmware scheduled-send, spec 2026-06-24): instead of the host issuing every `send` over a live USB
# stream, it ARMS each node once with `testsend`/`testch` (explicit `-t` ms offsets) and the node fires the whole
# schedule autonomously over the radio. THE TAG CONTRACT: the firmware auto-builds each scheduled body
# `T<run>S<self>#<seq>@<sendms>` with seq = the ARM ORDER (a per-node counter, reset by `testclear`). So we assign seq
# the SAME way here (arm order) and the canonical tags match with zero host<->firmware divergence. `@<sendms>` (the
# node's millis() at TX) rides along for latency.
try:
    from .tag import make_tag, parse_tag, parse_sendms
except ImportError:
    from tag import make_tag, parse_tag, parse_sendms


def build_schedule(actions, run, rx_budget=460):
    """[SendAction] -> (per_node_commands {node_id: [console lines]}, send_ledger [rows]). Each node gets `testclear`
    then one `testsend <dst> <run> [-a][-e] -t …` / `testch <ch> <run> -t …` per (kind,target,flags) group, with the
    ms offsets chunked so a line stays under the firmware's console line buffer (`service_console` line[512], rejects
    >511) — 460 leaves margin for the prefix. (Raise toward ~960 once the USB-reliability spec bumps line[]→1024.)
    seq is assigned in the SAME order the lines are emitted, so it equals the firmware's per-node arm counter.
    Re-tags each action (overrides the workload tag) to that seq."""
    by_node = {}
    for a in actions:
        by_node.setdefault(a.src, []).append(a)
    per_node_commands, send_ledger = {}, []
    for nid in sorted(by_node):
        groups = {}                                                  # (kind, target, ack, enc) -> [actions]
        for a in by_node[nid]:
            key = (a.kind, a.dst if a.kind == "dm" else a.chan, bool(a.ack), bool(a.enc))
            groups.setdefault(key, []).append(a)
        lines, seq = ["testclear"], 0                                # testclear -> firmware seq resets to 0 (matches ours)
        for key in sorted(groups, key=lambda k: (k[0], k[1] if k[1] is not None else -1)):
            kind, target, ack, enc = key
            offs = []
            for a in sorted(groups[key], key=lambda a: a.at):        # by arrival; seq follows THIS order
                ms = max(0, int(round(a.at * 1000)))
                a.tag = make_tag(run, nid, seq)
                send_ledger.append({"tag": a.tag, "src": nid, "kind": kind,
                                    "dst": target if kind == "dm" else None,
                                    "chan": target if kind == "chan" else None,
                                    "ack": ack, "enc": enc, "ctr": None, "host_send_ts": None, "at_ms": ms, "seq": seq})
                offs.append(str(ms))
                seq += 1
            prefix = (f"testsend {target} {run}" + (" -a" if ack else "") + (" -e" if enc else "") + " -t "
                      if kind == "dm" else f"testch {target} {run} -t ")
            chunk, clen = [], 0                                       # chunk offsets so a line stays < rx_budget chars
            for tok in offs:
                if chunk and len(prefix) + clen + len(tok) > rx_budget:
                    lines.append(prefix + ",".join(chunk)); chunk, clen = [], 0
                chunk.append(tok); clen += len(tok) + 1
            if chunk:
                lines.append(prefix + ",".join(chunk))
        per_node_commands[nid] = lines
    return per_node_commands, send_ledger


def arm_latencies(send_ledger, inboxes, node_offsets):
    """Clock-aligned APPROXIMATE latency (no live pushes in arm-mode). Each delivered inbox record carries rx_ms
    (receiver clock) + a body `…@<sendms>` (sender clock at TX). node_offsets[nid] = host_ms - node_uptime_ms read at
    teardown, so a node-ms lifts to host-ms. latency = (off[R]+rx_ms) - (off[S]+sendms). Returns
    ({tag: best latency_s}, {node_id(receiver): [latency_s it saw]}). Clamped at 0 (a small negative = clock-align
    noise). ⚠ A node that REBOOTED mid-run has a reset millis() -> its pre-reboot rx_ms can't be aligned; the caller
    excludes such nodes (uptime < run, or boot_seq changed) -> their latency is dropped, delivery/coverage unaffected."""
    sender_of = {row["tag"]: row["src"] for row in send_ledger}
    per_tag, per_node = {}, {}
    for R, box in inboxes.items():
        offR = node_offsets.get(R)
        for kind in ("dm", "chan"):
            for rec in box.get(kind, []):
                t = parse_tag(rec.get("body", ""))
                if not t:
                    continue
                tag = make_tag(t["run"], t["src"], t["n"])
                sendms = parse_sendms(rec.get("body", ""))
                rx = rec.get("rx_ms")
                offS = node_offsets.get(sender_of.get(tag, t["src"]))
                if sendms is None or rx is None or offR is None or offS is None:
                    continue
                lat_s = max(0.0, ((offR + rx) - (offS + sendms)) / 1000.0)
                per_node.setdefault(R, []).append(lat_s)
                if tag not in per_tag or lat_s < per_tag[tag]:
                    per_tag[tag] = lat_s
    return per_tag, per_node


def _selftest():
    import types

    def A(src, kind, dst=None, chan=None, ack=False, enc=False, at=0.0):
        a = types.SimpleNamespace(src=src, kind=kind, dst=dst, chan=chan, ack=ack, enc=enc, at=at, tag=None)
        return a

    # node 7: two DMs to 9 (ack) + one DM to 5 + two channel posts -> grouped, seq in arm order, tags re-assigned.
    acts = [A(7, "dm", dst=9, ack=True, at=1.0), A(7, "dm", dst=9, ack=True, at=3.0),
            A(7, "dm", dst=5, ack=True, at=2.0), A(7, "chan", chan=0, at=0.5), A(7, "chan", chan=0, at=4.0),
            A(9, "dm", dst=7, ack=True, at=1.5)]
    cmds, ledger = build_schedule(acts, "r9", rx_budget=800)
    assert cmds[7][0] == "testclear" and cmds[9][0] == "testclear"
    # group order: dm before chan; dm-to-5 before dm-to-9 (target sort). seq follows emit order.
    j = "\n".join(cmds[7])
    assert "testsend 5 r9 -a -t 2000" in j, j                        # dm->5 @2000ms, ack
    assert "testsend 9 r9 -a -t 1000,3000" in j, j                   # dm->9 @1000,3000 (sorted by .at)
    assert "testch 0 r9 -t 500,4000" in j, j                         # channels @500,4000
    # seq/tag: dm->5 got seq0, dm->9 seq1+2, chan seq3+4 (arm order). The firmware assigns the same.
    t7 = {row["seq"]: row["tag"] for row in ledger if row["src"] == 7}
    assert t7[0] == "Tr9S7#0" and t7[1] == "Tr9S7#1" and t7[3] == "Tr9S7#3", t7
    assert sum(1 for r in ledger if r["src"] == 7) == 5 and all(r["ctr"] is None for r in ledger)

    # chunking: 100 offsets to one dst must split across lines, seq still 0..99 contiguous, tags match.
    big = [A(3, "dm", dst=4, at=i * 0.1) for i in range(100)]
    bc, bl = build_schedule(big, "rb", rx_budget=120)
    sends = [l for l in bc[3] if l.startswith("testsend")]
    assert len(sends) > 1, "100 offsets @ rx_budget=120 must chunk"
    assert [r["seq"] for r in bl] == list(range(100)) and bl[0]["tag"] == "Trb S3#0".replace(" ", "")

    # arm_latencies: node offsets align sender+receiver clocks. S=7 off=1000, R=9 off=500. sent@7:200 (host 1200),
    # rx@9:900 (host 1400) -> latency 200ms. A second receiver with a bigger gap is ignored (keep the min).
    sl = [{"tag": "Tr9S7#0", "src": 7}]
    inboxes = {9: {"dm": [{"body": "Tr9S7#0@200", "rx_ms": 900}], "chan": []}}
    pt, pn = arm_latencies(sl, inboxes, {7: 1000, 9: 500})
    assert abs(pt["Tr9S7#0"] - 0.2) < 1e-6, pt                       # (500+900)-(1000+200)=200ms
    assert abs(pn[9][0] - 0.2) < 1e-6, pn
    # negative (clock-align noise) clamps to 0
    pt2, _ = arm_latencies(sl, {9: {"dm": [{"body": "Tr9S7#0@900", "rx_ms": 100}], "chan": []}}, {7: 1000, 9: 500})
    assert pt2["Tr9S7#0"] == 0.0, pt2
    print("schedule selftest OK")


if __name__ == "__main__":
    _selftest()
