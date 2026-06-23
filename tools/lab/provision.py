# MeshRoute lab harness — Phase 0 provision: leave-reset, create/join one leaf, verify convergence, emit topology.
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# Amendment 1 (bench-proven): re-provisioning ON TOP of a standing managed leaf split-brains (the still-alive old
# leaf pulls nodes back). So provision FIRST `leave`s every node to a clean idle slate, then builds the new leaf.
# Convergence proxy = id + level_id + sf_list (lineage_id/config_epoch aren't console-exposed — firmware gap, see
# the spec). Fail loud, never hang: every request has a timeout + marker (Amendment 2); a rejected leave/create/join
# or a non-converged / duplicate-id node raises ProvisionError.
import time
try:                                     # package import (normal) …
    from . import parsers
    from .parsers import normalize_sf_list
except ImportError:                      # … or run directly as a script (python3 tools/lab/provision.py)
    import parsers
    from parsers import normalize_sf_list


class ProvisionError(Exception):
    pass


def _accepted(lines, verb):
    """(ok, detail) for create/join. Accept = a `> <verb> …` line that is NOT `> <verb> err …`. `JOIN REFUSED`
    (async; may or may not be in this burst) = reject. Test the `err` prefix BEFORE the bare accept prefix."""
    txt = "\n".join(lines)
    if "JOIN REFUSED" in txt:
        return (False, "JOIN REFUSED" + txt.split("JOIN REFUSED", 1)[1].strip()[:80])
    for ln in lines:
        s = ln.strip()
        if s.startswith(f"> {verb} err"):
            return (False, s)
        if s.startswith(f"> {verb}"):
            return (True, s)
    return (False, "no accept/reject line (silent?): " + (txt[:120] or "(empty)"))


def _left_ok(lines):
    """(ok, detail) for `leave` (Amendment 1). The success line is `> left network (kept freq=…) — idle` — it does
    NOT start with `> leave`, so this is a leave-SPECIFIC check. `> leave err …` (e.g. nv_save_failed) = reject."""
    txt = "\n".join(lines)
    for ln in lines:
        s = ln.strip()
        if "leave err" in s:
            return (False, s)
        if "> left network" in s:
            return (True, s)
    return (False, "no leave ack (silent?): " + (txt[:120] or "(empty)"))


def pick_mother(nodes, netspec):
    m = netspec.get("mother", "first")
    if m in (None, "first"):
        return nodes[0]
    for n in nodes:
        if n.node_id == int(m):          # NodeInfo.node_id is the discovery snapshot (survives the leave reset)
            return n
    raise ProvisionError(f"mother id {m} is not among the responsive nodes")


def _converged(cfg, want_level, want_sf):
    """Converged = a real id + this leaf's level_id + this leaf's sf_list (the joiner pulled the managed config)."""
    return bool(cfg) and cfg.get("node_id") not in (None, "0") \
        and cfg.get("level_id") == want_level \
        and normalize_sf_list(cfg.get("sf_list")) == want_sf


def _auto_timeout(n_nodes, beacon_ms):
    """Default convergence timeout (Amendment 3): config-pull cascades hop-by-hop on a ~linear topology, so it
    scales with depth — max(90, n_nodes × beacon_s × 1.5). A 10-node chain at a 30 s beacon needs ~7.5 min, not 90 s."""
    return max(90, int(n_nodes * (beacon_ms / 1000.0) * 1.5))


def _set_beacon(manager, nodes, beacon_ms):
    """Drive a SHORT provisioning beacon (Amendment 3) so DAD + config-pull retry in seconds, not the 15-min default.
    `leave` reset the period to 900000, so this runs AFTER create/join. Best-effort: a cfg-set miss is a warning,
    not a hard fail (slow convergence at worst). `cfg set beacon_ms` is a LIVE MAC knob (no reboot)."""
    res = manager.broadcast(f"cfg set beacon_ms {beacon_ms}", "> ", timeout=3.0, nodes=nodes)
    for n in nodes:
        if not any(" ok" in ln for ln in res.get(n.port, [])):
            print(f"WARNING: {n.label()} 'cfg set beacon_ms' not confirmed: {' '.join(res.get(n.port, []))[:100]}")


def provision(manager, netspec, timeout=None, reset=True, reset_settle_s=3.0, poll_s=3.0, on_progress=None):
    """leave-reset (unless reset=False), create the mother, join the rest, drive a short provisioning beacon, poll
    cfg until all converge -> topology. timeout=None auto-scales with node count + beacon period (Amendment 3)."""
    nodes = manager.responsive()
    if not nodes:
        raise ProvisionError("no responsive nodes to provision")
    mother = pick_mother(nodes, netspec)

    freq = netspec["freq_mhz"]; bw = netspec["bw_khz"]; sf = netspec["ctrl_sf"]; lvl = netspec["level_id"]
    sflist = netspec["sf_list"]; duty = netspec["duty_pct"]; name = netspec.get("leaf_name", "bench")
    beacon_ms = netspec.get("beacon_ms", 30000)
    want_level, want_sf = str(lvl), normalize_sf_list(sflist)
    if timeout is None:
        timeout = _auto_timeout(len(nodes), beacon_ms)
    if duty < 5:                                                   # don't override (spec) — just warn loud
        print(f"WARNING: duty_pct={duty} is very tight — a short provisioning beacon can exhaust the duty budget "
              f"(node goes SILENT -> can't beacon/pull). Consider duty_pct: 10 for the harness.")

    # 0. RESET (Amendment 1): leave ALL nodes -> settle -> verify idle, so the old leaf dies everywhere at once.
    if reset:
        lres = manager.broadcast("leave", "> ", timeout=5.0)
        for n in nodes:
            ok, why = _left_ok(lres.get(n.port, []))
            if not ok:
                raise ProvisionError(f"{n.label()} leave REJECTED: {why}")
        time.sleep(reset_settle_s)                                   # let the live-apply land + old beacons stop
        wres = manager.broadcast("whoami", "[whoami]", timeout=3.0)
        for n in nodes:
            w = parsers.parse_whoami(wres.get(n.port, []))
            if not w or w["node_id"] != 0:
                raise ProvisionError(f"not idle after leave: {n.label()} -> {w}")

    # 1. mother: create
    ok, why = _accepted(manager.request(mother, f'create {freq} {bw} {sf} {lvl} {sflist} {duty} "{name}"', "> ", timeout=5.0), "create")
    if not ok:
        raise ProvisionError(f"mother {mother.label()} create REJECTED: {why}")
    _set_beacon(manager, [mother], beacon_ms)                       # mother beacons fast BEFORE joiners pull (Amendment 3)

    # 2. joiners: join (concurrent)
    joiners = [n for n in nodes if n is not mother]
    jres = manager.broadcast(f"join {freq} {bw} {sf} {lvl}", "> ", timeout=5.0, nodes=joiners)
    for n in joiners:
        ok, why = _accepted(jres.get(n.port, []), "join")
        if not ok:
            raise ProvisionError(f"joiner {n.label()} join REJECTED: {why}")
    if joiners:
        _set_beacon(manager, joiners, beacon_ms)                    # joiners retry DAD + config-pull every ~beacon_ms

    # 3. verify convergence (DAD + config-pull take ~tens of seconds; this is the slow step)
    deadline = time.time() + timeout
    last_parsed = {}
    while True:
        cfgs = manager.broadcast("cfg", "[cfg]", timeout=3.0)
        last_parsed = {n.port: parsers.parse_cfg(cfgs.get(n.port, [])) for n in nodes}
        for n in nodes:                                             # surface an async JOIN REFUSED if it landed in a burst
            if any("JOIN REFUSED" in ln for ln in cfgs.get(n.port, [])):
                raise ProvisionError(f"{n.label()} JOIN REFUSED during convergence")
        conv = {n.port: _converged(last_parsed[n.port], want_level, want_sf) for n in nodes}
        ids = [last_parsed[n.port]["node_id"] for n in nodes
               if last_parsed[n.port] and last_parsed[n.port].get("node_id") not in (None, "0")]
        dup = len(ids) != len(set(ids))
        if on_progress:
            on_progress(sum(conv.values()), len(nodes), int(deadline - time.time()))
        if all(conv.values()) and not dup:
            break
        if time.time() >= deadline:
            if dup:
                raise ProvisionError(f"DAD id collision (not unique): ids={sorted(ids)}")
            bad = [n for n in nodes if not conv[n.port]]
            detail = {n.label(): last_parsed[n.port] for n in bad}
            raise ProvisionError(f"not converged in {timeout}s: {[n.label() for n in bad]} | last cfg: {detail}")
        time.sleep(poll_s)

    # 4. topology registry (re-read once for a clean snapshot)
    cfgs = manager.broadcast("cfg", "[cfg]", timeout=3.0)
    topo = []
    for n in nodes:
        c = parsers.parse_cfg(cfgs.get(n.port, [])) or last_parsed.get(n.port) or {}
        topo.append({
            "port": n.port,
            "node_id": int(c.get("node_id", 0) or 0),
            "hash": n.hash,
            "leaf_id": int(c.get("leaf_id", 0) or 0),
            "level_id": int(c.get("level_id", 0) or 0),
            "sf_list": c.get("sf_list", ""),
            "is_mother": n is mother,
        })
    return topo


# --------------------------------------------------------------------------------------------------
# Offline self-test (no device): `python3 tools/lab/provision.py` — the accept/reject + leave-ack checks.
# --------------------------------------------------------------------------------------------------
def _selftest():
    assert _accepted(['> create leaf lineage=31113 level_id=1 (leaf nibble 1) name="bench" — mother live'], "create")[0] is True
    assert _accepted(["> create err nv_save_failed"], "create")[0] is False
    assert _accepted(["> create err usage: create <freq_MHz> …"], "create")[0] is False
    assert _accepted(["> join freq=869.000 bw=125 ctrl_sf=9 level_id=1 (leaf nibble 1) — DADing id + pulling config (live)"], "join")[0] is True
    assert _accepted(["> join err usage: …"], "join")[0] is False
    assert _accepted(["JOIN REFUSED reason=wire_version their_ver=2 my_ver=1"], "join")[0] is False
    assert _accepted([], "create")[0] is False                      # silent -> reject

    assert _left_ok(["> left network (kept freq=869.000) — idle"]) [0] is True
    assert _left_ok(["> leave err nv_save_failed"])[0] is False
    assert _left_ok(["garbage"])[0] is False

    assert _converged({"node_id": "73", "level_id": "1", "sf_list": "9,7"}, "1", "7,9") is True   # sf order-insensitive
    assert _converged({"node_id": "0", "level_id": "1", "sf_list": "7,9"}, "1", "7,9") is False   # still DADing
    assert _converged({"node_id": "73", "level_id": "2", "sf_list": "7,9"}, "1", "7,9") is False   # wrong leaf
    assert _converged({"node_id": "73", "level_id": "1", "sf_list": "-"}, "1", "7,9") is False     # config not pulled
    assert _converged(None, "1", "7,9") is False

    assert _auto_timeout(2, 30000) == 90        # 2*30*1.5=90 -> floor
    assert _auto_timeout(4, 30000) == 180       # 4*30*1.5
    assert _auto_timeout(10, 30000) == 450      # deep chain needs minutes, not 90 s
    assert _auto_timeout(4, 5000) == 90         # fast beacon -> floored at 90
    print("provision selftest OK")


if __name__ == "__main__":
    _selftest()
