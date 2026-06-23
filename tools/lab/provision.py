# MeshRoute lab harness — Phase 0 provision: create/join one leaf, verify convergence, emit the topology.
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# Convergence proxy = id + level_id + sf_list (lineage_id/config_epoch are NOT console-exposed — firmware gap,
# flagged in the spec). sf_list-pulled means the joiner adopted the managed config. Fail loud, never hang:
# every step has a timeout; a rejected create/join or a non-converged/duplicate-id node raises ProvisionError.
import time
from . import parsers
from .parsers import normalize_sf_list


class ProvisionError(Exception):
    pass


def _accepted(lines, verb):
    """(ok, detail). Accept = a `> <verb> …` line that is NOT `> <verb> err …`. `JOIN REFUSED` (async; may or
    may not be in this burst) = reject. Order matters: test the `err` prefix before the bare accept prefix."""
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


def pick_mother(nodes, netspec):
    m = netspec.get("mother", "first")
    if m in (None, "first"):
        return nodes[0]
    for n in nodes:
        if n.node_id == int(m):
            return n
    raise ProvisionError(f"mother id {m} is not among the responsive nodes")


def _converged(cfg, want_level, want_sf):
    """A node is converged when it holds a real id + this leaf's level_id + this leaf's sf_list (config pulled)."""
    return bool(cfg) and cfg.get("node_id") not in (None, "0") \
        and cfg.get("level_id") == want_level \
        and normalize_sf_list(cfg.get("sf_list")) == want_sf


def provision(manager, netspec, timeout=60, poll_s=3.0, on_progress=None):
    """create the mother, join the rest, poll cfg until all converge, return the topology list (or raise)."""
    nodes = manager.responsive()
    if not nodes:
        raise ProvisionError("no responsive nodes to provision")
    mother = pick_mother(nodes, netspec)

    freq = netspec["freq_mhz"]; bw = netspec["bw_khz"]; sf = netspec["ctrl_sf"]; lvl = netspec["level_id"]
    sflist = netspec["sf_list"]; duty = netspec["duty_pct"]; name = netspec.get("leaf_name", "bench")
    want_level, want_sf = str(lvl), normalize_sf_list(sflist)

    # 1. mother: create
    ok, why = _accepted(manager.request(mother, f'create {freq} {bw} {sf} {lvl} {sflist} {duty} "{name}"', timeout=5.0), "create")
    if not ok:
        raise ProvisionError(f"mother {mother.label()} create REJECTED: {why}")

    # 2. joiners: join (concurrent)
    joiners = [n for n in nodes if n is not mother]
    jres = manager.broadcast(f"join {freq} {bw} {sf} {lvl}", timeout=5.0, nodes=joiners)
    for n in joiners:
        ok, why = _accepted(jres.get(n.port, []), "join")
        if not ok:
            raise ProvisionError(f"joiner {n.label()} join REJECTED: {why}")

    # 3. verify convergence (poll cfg; DAD + config-pull take ~tens of seconds, so this is the slow step)
    deadline = time.time() + timeout
    last_parsed = {}
    while True:
        cfgs = manager.broadcast("cfg", timeout=3.0)
        last_parsed = {n.port: parsers.parse_cfg(cfgs.get(n.port, [])) for n in nodes}
        for n in nodes:                                              # surface an async JOIN REFUSED if it landed in a burst
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
    cfgs = manager.broadcast("cfg", timeout=3.0)
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
