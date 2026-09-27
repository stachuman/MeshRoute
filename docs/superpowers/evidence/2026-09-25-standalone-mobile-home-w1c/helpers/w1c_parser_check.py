# W1c §2.4 parser compatibility (evidence only, no tool edit): feed the EXACT lines the probe's X21/X23/X25 rows checked
# (both arms) to tools/lab/parsers.py::parse_whoami; the name must be "" / "Bench 1" / the 32-byte name, identity intact.
import sys, re
sys.path.insert(0, "/home/staszek/MeshRoute/tools/lab")
from parsers import parse_whoami
want = {"X21": "", "X23": "Bench 1", "X25": "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345"}
bad = 0; n = 0
for ln in open(sys.argv[1], encoding="utf-8", errors="replace"):
    m = re.match(r"^  ok   (X2[135]) .*\[(\[whoami\] .*)\]$", ln.rstrip("\n"))
    if not m: continue
    row, line = m.group(1), m.group(2); n += 1
    r = parse_whoami([line])
    ok = (r is not None and r["name"] == want[row] and r["node_id"] == 5 and r["hash"] is not None
          and r["leaf"] == 0 and r["gw"] is False and r["gwonly"] is False and r["mobile"] is False)
    bad += not ok
    print(f"{'OK ' if ok else 'BAD'} {row} parse_whoami({line!r}) -> {r}")
print(f"rows parsed: {n}; bad: {bad}")
sys.exit(1 if bad or n == 0 else 0)
