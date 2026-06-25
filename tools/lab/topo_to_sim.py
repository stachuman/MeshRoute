#!/usr/bin/env python3
# MeshRoute lab harness — topo_to_sim: turn the REAL metal topology (`meshroute_lab.py topology --json`) + the same
# realistic workload into a lus sim scenario, so the IDENTICAL run executes on both. Compare the per-origin channel
# coverage: if the sim orphans the same origins -> a firmware-LOGIC bug (deterministic, traceable in sim); if not ->
# metal-specific (RF/timing/hardware). Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# The sim's `topology.links` are EXPLICIT + DIRECTED (from/to/snr, bidir:false) — so the real asymmetry is reproduced
# 1:1 (a one-way link = only one direction present). Sim node_ids are sequential (1..n), so we map real_id -> sim_id
# and name nodes "n<real_id>" (commands + links use names; SimController resolves them). Engine = meshroute.
import json
import sys
import types

try:
    from . import workload
except ImportError:
    import workload


def build_scenario(topo_db, scenario, run_id, *, sends=None, routing_sf=8, sf_list=(7, 9), duty_cycle=0.1,
                   snr_base=60.0, snr_std=0.0, beacon_ms=8000, converge_ms=40000, radio=None):
    """topo_db = {real_id: {heard_real_id: dB}} (A hears B). scenario = the workload flat-keys (workload: realistic …).
    Returns the lus scenario dict. snr_base+dB keeps the real RELATIVE link strengths while staying decodable (first
    pass: snr_std=0 => deterministic, no fading). `converge_ms` lets beacons settle before the workload fires."""
    real_ids = sorted(topo_db)                                        # 1:1 map real_id -> sim_id (1..n, sequential)
    sim_id = {r: i + 1 for i, r in enumerate(real_ids)}
    name = {r: f"n{r}" for r in real_ids}                             # node name carries the REAL id (readable links/cmds)
    rad = radio or {"sf": routing_sf, "bw": 125, "cr": 5, "max_packet_bytes": 255, "snr_coherence_ms": 0,
                    "duty_cycle": duty_cycle}

    nodes = [{"name": name[r], "node_id": sim_id[r], "engine": "meshroute",
              "lat": 0.0, "lon": 0.0,                                 # positions unused — explicit links drive connectivity
              "config": {"routing_sf": routing_sf, "allowed_data_sfs": list(sf_list)}}
             for r in real_ids]

    links = []                                                       # A hears B  =>  a frame FROM B reaches A (from=B, to=A)
    for a in real_ids:
        for b, db in topo_db[a].items():
            if b not in sim_id:
                continue
            links.append({"from": name[b], "to": name[a], "snr": round(snr_base + db, 2),
                          "rssi": round(-110 + snr_base + db, 2), "snr_std_dev": snr_std, "bidir": False})

    # workload -> timed commands. PREFER the metal run's exact send-ledger (sends=) for a byte-identical workload;
    # else re-generate from the scenario (NB: node-order changes the seeded Poisson draws, so this won't match a
    # specific metal run — use --ledger for that).
    if sends is None:
        ni = [types.SimpleNamespace(node_id=r) for r in real_ids]
        actions = workload.build_actions(scenario, ni, run_id)
        workload.schedule(actions, scenario)
        sends = [{"src": a.src, "kind": a.kind, "dst": a.dst, "chan": a.chan, "ack": a.ack, "tag": a.tag,
                  "at_ms": int(round(a.at * 1000))} for a in actions]
    cmds = []
    for s in sorted(sends, key=lambda x: x["at_ms"]):
        if s["src"] not in sim_id or (s["kind"] == "dm" and s.get("dst") not in sim_id):
            continue
        at = converge_ms + int(s["at_ms"])                           # fire AFTER convergence
        if s["kind"] == "dm":
            verb = "send_e2e" if s.get("ack") else "send"            # send_e2e sets DATA_FLAG_E2E_ACK_REQ (the ack path)
            line = f"{verb} {sim_id[s['dst']]} {s['tag']}"           # numeric dst id (handler atoi's it) + the tag body
        else:
            line = f"send_channel {s.get('chan', 0)} {s['tag']}"
        cmds.append({"at_ms": at, "node": name[s["src"]], "command": line})

    last = (cmds[-1]["at_ms"] if cmds else converge_ms)
    duration = last + int(float(scenario.get("settle_s", 30)) * 1000) + 30000
    return {
        "_name": f"topo_match_{run_id}",
        "_desc": "metal topology + realistic workload, 1:1 — firmware-logic vs metal-specific isolator",
        "simulation": {"duration_ms": duration, "step_ms": 1, "warmup_ms": 0, "beacon_period_ms": beacon_ms,
                       "seed": int(scenario.get("seed", 0)), "node_startup_jitter_ms": 2000, "radio": rad},
        "config": {"debug_start_ms": 0, "debug_end_ms": duration},
        "nodes": nodes,
        "topology": {"links": links},
        "commands": cmds,
        "expect": [],
        "_id_map": {str(r): sim_id[r] for r in real_ids},            # real_id -> sim_id (for mapping results back)
    }


def _load_topo(path):
    """A topology JSON. Accepts {real_id: {neighbor: dB}} OR the harness `topology --json` Q4 form {id:{id:q4}} (auto /16)."""
    raw = json.load(open(path))
    out = {}
    for a, nbrs in raw.items():
        out[int(a)] = {int(b): (v / 16.0 if abs(v) > 30 else float(v)) for b, v in nbrs.items()}  # Q4 (>30) -> dB
    return out


if __name__ == "__main__":
    import argparse
    import os
    ap = argparse.ArgumentParser(description="real metal topology + workload -> lus sim scenario (1:1 metal/sim isolator)")
    ap.add_argument("topo", help="topology JSON {real_id:{neighbor:dB|q4}} (e.g. `meshroute_lab.py topology --json`)")
    ap.add_argument("scenario", help="workload flat-key file (used only when --ledger is absent)")
    ap.add_argument("--ledger", help="a metal run's send_ledger.jsonl -> byte-identical workload (recommended)")
    ap.add_argument("--run", default="simcmp", help="run-id tag for the generated bodies")
    ap.add_argument("--converge-ms", type=int, default=40000, help="beacon-convergence window before the workload fires")
    ap.add_argument("--snr-base", type=float, default=60.0, help="sim SNR = snr_base + real_dB (keeps relative strengths)")
    ap.add_argument("--snr-std", type=float, default=0.0, help="per-link SNR std-dev (fading); 0 = deterministic")
    a = ap.parse_args()
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    topo = _load_topo(a.topo)
    scen = {}
    for ln in open(a.scenario):
        ln = ln.split("#", 1)[0].strip()
        if ":" in ln:
            k, v = ln.split(":", 1)
            scen[k.strip()] = v.strip()
    sends = [json.loads(l) for l in open(a.ledger) if l.strip()] if a.ledger else None
    print(json.dumps(build_scenario(topo, scen, a.run, sends=sends, converge_ms=a.converge_ms,
                                    snr_base=a.snr_base, snr_std=a.snr_std), indent=1))
