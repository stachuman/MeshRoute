import collections, hashlib, json, os, re, subprocess, sys
prog, cwd, out, n = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
prefix = sys.argv[5] if len(sys.argv) > 5 else ""
os.makedirs(out, exist_ok=True)
rows = []
for i in range(n):
    cmd = ([*prefix.split()] if prefix else []) + [prog]
    p = subprocess.run(cmd, cwd=cwd, capture_output=True)
    data = p.stdout + p.stderr
    name = f"{out}/run-{i:03d}.out"
    open(name, "wb").write(data)
    m = re.findall(rb"assertions: *(\d+) \| *(\d+) passed \| *(\d+) failed", p.stdout)
    sites = sorted(set(x.decode() for x in re.findall(rb"(test/[\w./]+:\d+): ERROR", data)))
    rows.append({"run": i, "exit": p.returncode, "failed": int(m[-1][2]) if m else None,
                 "sha256": hashlib.sha256(data).hexdigest(), "sites": sites})
dist = collections.Counter(r["failed"] for r in rows)
base = set.intersection(*(set(r["sites"]) for r in rows))
varying = sorted(set().union(*(set(r["sites"]) for r in rows)) - base)
print(json.dumps({"runs": n, "prefix": prefix or None, "exit": sorted({r["exit"] for r in rows}),
                  "distribution": dict(sorted(dist.items(), key=lambda kv: str(kv[0]))),
                  "always_failing_sites": len(base), "varying_sites": varying}))
json.dump(rows, open(f"{out}/runs.json", "w"), indent=0)
