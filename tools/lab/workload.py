# MeshRoute lab harness — workload generators. Phase 2 = `oracle` only (deterministic round-robin); Phase 3 adds
# random / channel-storm generators here (build_actions dispatches on scenario['workload']).
# PACING (2026-06-24): build_actions builds WHAT is sent; schedule() sets WHEN (each action's `.at` offset). `burst`
# (Phase-2 default) issues back-to-back; `poisson` spreads sends over a window so transmitters DESYNCHRONIZE — the
# realistic test (the back-to-back burst self-collides: every node TX'ing at once = the CRC storm we measured).
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
import random
try:
    from .tag import make_tag
except ImportError:
    from tag import make_tag


def _truthy(v, default=False):
    if v is None:
        return default
    return str(v).strip().lower() in ("1", "true", "yes", "on")


class SendAction:
    """One scheduled send. dst is a node_id (DM); chan is a channel id (channel). tag is the payload (verbatim)."""
    def __init__(self, src, kind, tag, dst=None, chan=None, ack=False, enc=False):
        self.src = src
        self.kind = kind            # 'dm' | 'chan'
        self.tag = tag
        self.dst = dst
        self.chan = chan
        self.ack = ack
        self.enc = enc
        self.at = 0.0               # scheduled arrival OFFSET (seconds from the issue-phase start); set by schedule()

    def command(self):
        """The exact console line (serial-cleanup syntax: `send <id> "<text>" [-a] [-e]` / `send_channel <ch> "<text>"`)."""
        if self.kind == "dm":
            return f'send {self.dst} "{self.tag}"' + (" -a" if self.ack else "") + (" -e" if self.enc else "")
        return f'send_channel {self.chan} "{self.tag}"'

    def __repr__(self):
        return f"SendAction({self.kind} src={self.src} dst={self.dst} chan={self.chan} tag={self.tag} ack={self.ack})"


def build_actions(scenario, nodes, run):
    """Phase 2 `oracle`: each node sends `dm_per_node` DMs (round-robin to OTHER nodes) + `chan_per_node` channels.
    Deterministic (round-robin, not random) so a bench-run is exactly reproducible. `nodes` = responsive [NodeInfo]."""
    wl = scenario.get("workload", "oracle")
    if wl != "oracle":
        raise ValueError(f"workload '{wl}' not supported in Phase 2 (only 'oracle')")
    dm_n = int(scenario.get("dm_per_node", 1))
    chan_n = int(scenario.get("chan_per_node", 1))
    chan_id = int(scenario.get("channel", 0))
    ack = _truthy(scenario.get("ack", "true"), default=True)
    enc = _truthy(scenario.get("enc", "false"), default=False)

    actions, nseq = [], {}
    for i, n in enumerate(nodes):
        others = [o for o in nodes if o.node_id != n.node_id]
        for k in range(dm_n):
            if not others:
                break
            dst = others[(i + k) % len(others)]            # round-robin through the other nodes (deterministic)
            tag = make_tag(run, n.node_id, nseq.get(n.node_id, 0)); nseq[n.node_id] = nseq.get(n.node_id, 0) + 1
            actions.append(SendAction(n.node_id, "dm", tag, dst=dst.node_id, ack=ack, enc=enc))
        for k in range(chan_n):
            tag = make_tag(run, n.node_id, nseq.get(n.node_id, 0)); nseq[n.node_id] = nseq.get(n.node_id, 0) + 1
            actions.append(SendAction(n.node_id, "chan", tag, chan=chan_id, ack=False, enc=enc))
    return actions


def schedule(actions, scenario):
    """Set each action's `.at` (offset seconds from the issue-phase start) per scenario['pacing'], and SORT by it.
      - 'burst' (default): .at = i * inter_gap_s -> the Phase-2 back-to-back behaviour (all transmit ~at once).
      - 'poisson': a Poisson process over `spread_s` (exponential inter-arrivals, mean spread_s/N) -> sends DESYNCHRONIZE.
        Seeded (scenario['seed'], default 0) so a run is reproducible; the action ORDER is shuffled so node assignment
        isn't biased by the round-robin build order. N.B. the oracle's active window must outlast spread_s + settle."""
    pacing = str(scenario.get("pacing", "burst")).strip().lower()
    n = len(actions)
    if pacing in ("", "burst", "none"):
        gap = float(scenario.get("inter_gap_s", 0.3))
        for i, a in enumerate(actions):
            a.at = i * gap
        return actions
    if pacing != "poisson":
        raise ValueError(f"pacing '{pacing}' not supported (burst | poisson)")
    if n == 0:
        return actions
    spread_s = float(scenario.get("spread_s", 120))
    rng = random.Random(int(scenario.get("seed", 0)))
    order = list(actions)
    rng.shuffle(order)                                                 # which send arrives when — unbias the build order
    rate = (n / spread_s) if spread_s > 0 else 0.0                     # mean arrivals/sec
    t = 0.0
    for a in order:
        t += rng.expovariate(rate) if rate > 0 else 0.0               # exponential gap -> a Poisson arrival process
        a.at = t
    actions.sort(key=lambda a: a.at)                                   # issue in arrival order
    return actions


def _selftest():
    class _N:
        def __init__(self, nid):
            self.node_id = nid
    nodes = [_N(254), _N(17), _N(18), _N(19)]
    acts = build_actions({"workload": "oracle", "dm_per_node": 1, "chan_per_node": 1, "channel": 0, "ack": "true"}, nodes, "r1")
    dms = [a for a in acts if a.kind == "dm"]
    chans = [a for a in acts if a.kind == "chan"]
    assert len(dms) == 4 and len(chans) == 4, acts
    assert all(a.dst != a.src for a in dms), dms                       # never to self
    assert dms[0].command() == 'send 17 "Tr1S254#0" -a', dms[0].command()
    assert chans[0].command() == 'send_channel 0 "Tr1S254#1"', chans[0].command()
    # ack=false -> no -a; enc=true -> -e
    a2 = build_actions({"workload": "oracle", "dm_per_node": 1, "chan_per_node": 0, "ack": "false", "enc": "true"}, nodes, "r1")
    assert a2[0].command() == 'send 17 "Tr1S254#0" -e', a2[0].command()
    try:
        build_actions({"workload": "random"}, nodes, "r1"); assert False
    except ValueError:
        pass

    # pacing: burst -> i*gap; poisson -> monotonic, spread ~spread_s, seeded-reproducible
    b = build_actions({"workload": "oracle", "dm_per_node": 1, "chan_per_node": 1}, nodes, "r1")
    schedule(b, {"pacing": "burst", "inter_gap_s": 0.5})
    assert [round(a.at, 3) for a in b] == [round(i * 0.5, 3) for i in range(len(b))], [a.at for a in b]
    p1 = build_actions({"workload": "oracle", "dm_per_node": 1, "chan_per_node": 1}, nodes, "r1")
    schedule(p1, {"pacing": "poisson", "spread_s": 100, "seed": 7})
    ats = [a.at for a in p1]
    assert ats == sorted(ats) and ats[0] >= 0, ats                     # sorted by arrival, non-negative
    assert 20 < ats[-1] < 400, ats[-1]                                 # spans roughly the window (Poisson tail varies)
    p2 = build_actions({"workload": "oracle", "dm_per_node": 1, "chan_per_node": 1}, nodes, "r1")
    schedule(p2, {"pacing": "poisson", "spread_s": 100, "seed": 7})
    assert [a.at for a in p2] == ats, "same seed -> reproducible schedule"
    try:
        schedule(build_actions({"workload": "oracle"}, nodes, "r1"), {"pacing": "nope"}); assert False
    except ValueError:
        pass
    print("workload selftest OK")


if __name__ == "__main__":
    _selftest()
