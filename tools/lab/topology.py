# MeshRoute lab harness — print the network topology (direct links + asymmetry) from each node's routes.
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# A node's hops==1 routes are its DIRECT links = who it can HEAR. On weak radios links are often ASYMMETRIC
# (A hears B but B doesn't hear A), so the graph is DIRECTED — and that asymmetry is exactly what bites floods
# (a node gets marked "covered" by an upstream it can't actually receive from). This surfaces it explicitly.
from . import parsers


def direct_neighbours(routes):
    """hops==1 routes -> {neighbour_id: score_q4}. The direct links this node HEARS (score = link quality, Q4 dB)."""
    nb = {}
    for r in routes:
        if r.get("hops") == "1" and "dest" in r:
            try:
                nb[int(r["dest"])] = int(r.get("score", "0"))
            except ValueError:
                pass
    return nb


def build(manager):
    """`routes` on every responsive node -> {node_id: {neighbour_id: score_q4}} (DIRECTED: key HEARS value)."""
    res = manager.broadcast("routes", "[routes]", timeout=3.0)
    return {n.node_id: direct_neighbours(parsers.parse_routes(res.get(n.port, [])))
            for n in manager.responsive()}


def _db(q4):
    return q4 / 16.0


def format_text(topo):
    """Human render: per-node adjacency + a symmetry table (the asymmetric one-way links flagged)."""
    ids = sorted(topo)
    internal = set(ids)
    out = ["=== topology — direct links (hops==1); 'A hears B', score ≈ link dB ===",
           f"{'node':>5}   hears →"]
    for a in ids:
        cells = "  ".join(f"{b}({_db(s):+.1f})" for b, s in sorted(topo[a].items(), key=lambda kv: -kv[1]))
        out.append(f"{a:>5}   {cells or '(none)'}")

    # unordered pairs where at least one endpoint hears the other
    pairs = set()
    external = set()
    for a in ids:
        for b in topo[a]:
            (pairs if b in internal else external).add((a, b) if b in internal else (a, b))
    pairs = {(min(a, b), max(a, b)) for a, b in pairs}

    out += ["", "=== link symmetry (⚠ asymmetric = one-way — the flood-coverage hazard) ==="]
    syms = asyms = 0
    for a, b in sorted(pairs):
        ab, ba = b in topo[a], a in topo[b]
        if ab and ba:
            syms += 1
            out.append(f"  {a} ↔ {b}     ({a}→{b} {_db(topo[a][b]):+.1f}dB · {b}→{a} {_db(topo[b][a]):+.1f}dB)")
        else:
            asyms += 1
            hi, lo = (a, b) if ab else (b, a)
            out.append(f"  {hi} → {lo}    ⚠ ASYMMETRIC  ({hi} hears {lo} {_db(topo[hi][lo]):+.1f}dB · {lo} does NOT hear {hi})")

    if external:
        out += ["", "=== external neighbours (heard, but not a probed node — stale/off-hub) ==="]
        for a, b in sorted(external):
            out.append(f"  {a} hears {b} ({_db(topo[a][b]):+.1f}dB)")

    out += ["", f"=== {len(ids)} nodes · {syms} symmetric · {asyms} asymmetric link(s) ==="]
    return "\n".join(out)
