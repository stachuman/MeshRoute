# W4b preflight item 4/5: every path in the pre-check inputs.json keeps its recorded hash (except the allowed changed
# prep documents); every current nonignored path not in the inventory is listed for classification. Both repos.
import json, hashlib, os, subprocess, sys
import os as _o; inv = json.load(open(_o.environ.get("INV", "/home/staszek/MeshRoute/docs/superpowers/evidence/2026-09-27-standalone-mobile-home-w4b-precheck/inputs.json")))
allowed = set(sys.argv[1:])
def h(p):
    if os.path.islink(p): return hashlib.sha256(os.fsencode(os.readlink(p))).hexdigest()
    with open(p, "rb") as f: return hashlib.sha256(f.read()).hexdigest()
bad = 0
for tree in inv:
    root = tree["root"]; files = tree["files"]
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    print(f"== {root}: HEAD {head} (inventory {tree['head']}) {'OK' if head == tree['head'] else 'MISMATCH'}")
    changed, missing = [], []
    for rel, meta in files.items():
        p = os.path.join(root, rel)
        if not os.path.lexists(p): missing.append(rel); continue
        if meta.get("kind") == "symlink":
            if not os.path.islink(p) or os.readlink(p) != meta["target"]: changed.append(rel)
        elif os.path.islink(p) or h(p) != meta["sha256"]: changed.append(rel)
    cur = subprocess.run(["git", "ls-files", "-co", "--exclude-standard", "-z"], cwd=root, capture_output=True).stdout.split(b"\0")
    cur = sorted(os.fsdecode(c) for c in cur if c)
    new = [c for c in cur if c not in files]
    print(f"   inventoried paths: {len(files)}; current nonignored: {len(cur)}")
    print(f"   changed vs inventory: {len(changed)}"); [print("     CHANGED", c, "(allowed: preparation document or W4b fenced file)" if c in allowed else "") for c in changed]
    print(f"   missing: {len(missing)}"); [print("     MISSING", m) for m in missing]
    print(f"   new since inventory: {len(new)}"); [print("     NEW", n) for n in new]
    bad += len([c for c in changed if c not in allowed]) + len(missing)
print("UNEXPLAINED CHANGED/MISSING:", bad)
