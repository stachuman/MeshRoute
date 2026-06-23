#!/usr/bin/env python3
# MeshRoute lab harness CLI — Phases 0-1 (status | provision). Runs on the Pi, SSH-launched. Python 3 + pyserial.
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
#   meshroute_lab.py status [--ports a,b] [--json]
#   meshroute_lab.py provision <netspec.yaml> [--mother <id>] [--timeout 60] [--ports a,b] [--out provision.json]
#
# Identity is by whoami id/hash, never the ACM number. NEVER `debug on` (verbose). Every request has a timeout
# (a hung node must not stall the fleet). See docs/superpowers/specs/2026-06-23-stress-harness-phase0-1.md.
import sys
import os
import time
import json
import argparse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # tools/ on the path -> `lab` + meshroute_client
from lab import registry, parsers, oracle, report, topology     # noqa: E402
from lab.manager import NodeManager                               # noqa: E402
from lab.provision import provision, ProvisionError              # noqa: E402

_NETSPEC_NUM = {"freq_mhz": float, "bw_khz": int, "ctrl_sf": int, "level_id": int, "duty_pct": int, "beacon_ms": int}
_NETSPEC_REQ = ("freq_mhz", "bw_khz", "ctrl_sf", "level_id", "sf_list", "duty_pct")
_SCEN_NUM = {"dm_per_node": int, "chan_per_node": int, "channel": int, "settle_s": int, "eventual_s": int}


def _parse_flat(path):
    """Flat `key: value` loader (dependency-free). Quotes + `#` comments stripped; values stay strings."""
    spec = {}
    with open(path) as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if not line or ":" not in line:
                continue
            k, _, v = line.partition(":")
            spec[k.strip()] = v.strip().strip('"').strip("'")
    return spec


def _coerce(spec, nummap, path):
    for k, t in nummap.items():
        if k in spec:
            try:
                spec[k] = t(spec[k])
            except ValueError:
                sys.exit(f"{path}: {k}={spec[k]!r} is not a {t.__name__}")
    return spec


def load_netspec(path):
    spec = _coerce(_parse_flat(path), _NETSPEC_NUM, path)
    missing = [k for k in _NETSPEC_REQ if k not in spec]
    if missing:
        sys.exit(f"netspec {path}: missing required keys {missing}")
    return spec


def load_scenario(path):
    spec = _coerce(_parse_flat(path), _SCEN_NUM, path)
    if "workload" not in spec:
        sys.exit(f"scenario {path}: missing required key 'workload'")
    return spec


def _ports_arg(s):
    return [p.strip() for p in s.split(",") if p.strip()] if s else None


# --------------------------------------------------------------------------------------------------
def cmd_status(args):
    nodes = registry.discover(ports=_ports_arg(args.ports))
    if not nodes:
        sys.exit("no /dev/ttyACM* ports found (use --ports to override)")
    with NodeManager(nodes) as mgr:
        cfgs = mgr.broadcast("cfg", "[cfg]", timeout=2.0)
        stats = mgr.broadcast("status", "[status]", timeout=2.0)
        duties = mgr.broadcast("duty", "[duty]", timeout=2.0)
        rows = []
        for n in nodes:
            row = {"port": n.port, "serial": n.serial, "id": n.node_id, "hash": n.hash, "leaf": n.leaf,
                   "level_id": None, "sf_list": None, "routes": None, "duty": None,
                   "uptime_s": None, "state": "ok" if n.responsive else f"DEAD({n.error})"}
            if n.responsive:
                cfg = parsers.parse_cfg(cfgs.get(n.port, [])) or {}
                st = parsers.parse_status(stats.get(n.port, [])) or {}
                dt = parsers.parse_duty(duties.get(n.port, []))
                row["level_id"] = cfg.get("level_id")
                row["sf_list"] = cfg.get("sf_list")
                row["routes"] = st.get("routes")
                row["uptime_s"] = int(int(st["uptime_ms"]) / 1000) if "uptime_ms" in st else None
                if dt is None:
                    row["duty"] = None
                elif not dt["enabled"]:
                    row["duty"] = "—"
                else:
                    row["duty"] = f"{dt['pct']}%" + ("(silent)" if dt["silent"] else "")
            rows.append(row)
    if args.json:
        print(json.dumps(rows, indent=2))
        return
    hdr = ("port", "serial", "id", "hash", "leaf", "level_id", "sf_list", "routes", "duty", "uptime_s", "state")
    w = (16, 18, 5, 11, 5, 9, 9, 7, 9, 9, 22)
    def fmt(vals):
        return "  ".join(str(v if v is not None else "-").ljust(width) for v, width in zip(vals, w))
    print(fmt(hdr))
    for r in rows:
        print(fmt(tuple(r[k] for k in hdr)))


def cmd_topology(args):
    nodes = registry.discover(ports=_ports_arg(args.ports))
    if not nodes:
        sys.exit("no /dev/ttyACM* ports found (use --ports to override)")
    with NodeManager(nodes) as mgr:
        topo = topology.build(mgr)
    if args.json:
        print(json.dumps({str(k): v for k, v in topo.items()}, indent=2))
    else:
        print(topology.format_text(topo))


def cmd_provision(args):
    spec = load_netspec(args.netspec)
    if args.mother is not None:
        spec["mother"] = args.mother
    nodes = registry.discover(ports=_ports_arg(args.ports))
    if not nodes:
        sys.exit("no /dev/ttyACM* ports found (use --ports to override)")
    with NodeManager(nodes) as mgr:
        live = mgr.responsive()
        dead = [n for n in nodes if not n.responsive]
        print(f"discovered {len(live)} responsive node(s)"
              + (f", {len(dead)} DEAD ({[n.port for n in dead]})" if dead else ""))
        def progress(done, total, secs_left):
            print(f"  converging: {done}/{total}  ({secs_left}s left)", flush=True)
        try:
            topo = provision(mgr, spec, timeout=args.timeout, reset=not args.no_reset, on_progress=progress)
        except ProvisionError as e:
            sys.exit(f"PROVISION FAILED: {e}")
    with open(args.out, "w") as f:
        json.dump(topo, f, indent=2)
    print(f"\nconverged — wrote {args.out}")
    print(f"{'port':16}  {'node_id':7}  {'leaf':4}  {'level':5}  {'sf_list':8}  mother")
    for t in topo:
        print(f"{t['port']:16}  {t['node_id']:<7}  {t['leaf_id']:<4}  {t['level_id']:<5}  {t['sf_list']:8}  {'*' if t['is_mother'] else ''}")


def cmd_run(args):
    scenario = load_scenario(args.scenario)
    run_id = format(int(time.time()) & 0xFFFFFF, "x")             # short host-side run id (nodes have no clock)
    run_dir = os.path.join("runs", run_id)
    os.makedirs(run_dir, exist_ok=True)
    nodes = registry.discover(ports=_ports_arg(args.ports))
    if not nodes:
        sys.exit("no /dev/ttyACM* ports found (use --ports to override)")
    with NodeManager(nodes) as mgr:
        if scenario.get("netspec"):                              # provision first, then key off the fresh topology
            ns_path = scenario["netspec"]
            if not os.path.isabs(ns_path):
                ns_path = os.path.join(os.path.dirname(os.path.abspath(args.scenario)), ns_path)
            netspec = load_netspec(ns_path)
            print(f"provisioning from {ns_path} ...")
            try:
                topo = provision(mgr, netspec,
                                 on_progress=lambda d, t, s: print(f"  converging {d}/{t} ({s}s left)", flush=True))
            except ProvisionError as e:
                sys.exit(f"PROVISION FAILED: {e}")
            by_port = {n.port: n for n in nodes}                 # DAD assigned new ids -> refresh NodeInfo so the oracle targets current ids
            for t in topo:
                if t["port"] in by_port:
                    by_port[t["port"]].node_id = t["node_id"]
        else:
            print(f"running against the standing net ({len(mgr.responsive())} responsive node(s))")
        result = oracle.run(mgr, scenario, run_id, run_dir,
                            settle_s=scenario.get("settle_s", 30),
                            eventual_s=scenario.get("eventual_s", 300),
                            verbose=args.verbose)
    if args.verbose:                                              # -v: full per-message dump before the (unchanged) summary
        print("\n" + report.format_per_msg(result))
    print("\n" + report.format_summary(result))
    print(f"\nartifacts: {run_dir}/")
    sys.exit(0 if result["verdict"]["pass"] else 1)


def main():
    ap = argparse.ArgumentParser(description="MeshRoute lab harness (status | provision)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    ps = sub.add_parser("status", help="one-shot table of every connected node")
    ps.add_argument("--ports", help="comma list, e.g. /dev/ttyACM0,/dev/ttyACM1 (default: auto-discover)")
    ps.add_argument("--json", action="store_true")
    ps.set_defaults(func=cmd_status)
    pp = sub.add_parser("provision", help="create/join the nodes into one leaf + verify convergence")
    pp.add_argument("netspec", help="netspec YAML/flat-key file")
    pp.add_argument("--mother", type=int, default=None, help="mother node_id (default: netspec.mother or first)")
    pp.add_argument("--timeout", type=int, default=None,
                    help="convergence timeout seconds (default: auto = max(90, n_nodes × beacon_s × 1.5); config-pull "
                         "cascades hop-by-hop on a linear topology, so deep chains need longer)")
    pp.add_argument("--no-reset", action="store_true",
                    help="skip the `leave` reset preamble (additive provisioning onto an already-clean network)")
    pp.add_argument("--ports", help="comma list (default: auto-discover)")
    pp.add_argument("--out", default="provision.json", help="topology output path")
    pp.set_defaults(func=cmd_provision)
    pr = sub.add_parser("run", help="run a workload scenario (Phase 2: oracle) + reconcile -> runs/<id>/")
    pr.add_argument("scenario", help="scenario flat-key file (workload: oracle …; optional netspec: provisions first)")
    pr.add_argument("--ports", help="comma list (default: auto-discover)")
    pr.add_argument("-v", "--verbose", action="store_true",
                    help="live event stream ([send]/[recv]/[chan]/[e2e]/[hop]/[pull]) + full per-message reconcile dump")
    pr.set_defaults(func=cmd_run)
    pt = sub.add_parser("topology", help="print the network topology (direct links + asymmetric links)")
    pt.add_argument("--ports", help="comma list (default: auto-discover)")
    pt.add_argument("--json", action="store_true")
    pt.set_defaults(func=cmd_topology)
    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
