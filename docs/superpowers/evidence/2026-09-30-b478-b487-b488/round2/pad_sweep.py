# ASLR OFF, and the stack shifted one byte at a time by an environment pad: the F07 binary's count as a function of
# where the stack lands. Prediction (from the disassembly): 42 exactly when &m's low byte is 0 — 1/16 of paddings.
import hashlib, json, os, platform, re, subprocess, sys
prog, cwd, out = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(out, exist_ok=True)
base_env = {"PATH": "/usr/bin:/bin", "HOME": os.environ.get("HOME", "/tmp")}
rows = []
for pad in range(256):
    env = dict(base_env, MRPAD="x" * pad)
    p = subprocess.run(["setarch", platform.machine(), "-R", prog], cwd=cwd, env=env, capture_output=True)
    data = p.stdout + p.stderr
    open(f"{out}/pad-{pad:03d}.out", "wb").write(data)
    m = re.findall(rb"assertions: *(\d+) \| *(\d+) passed \| *(\d+) failed", p.stdout)
    rows.append({"pad": pad, "exit": p.returncode, "failed": int(m[-1][2]) if m else None,
                 "sha256": hashlib.sha256(data).hexdigest()})
json.dump(rows, open(f"{out}/sweep.json", "w"), indent=0)
f42 = [r["pad"] for r in rows if r["failed"] == 42]
runs, cur = [], []
for p_ in f42:
    if cur and p_ != cur[-1] + 1:
        runs.append(cur); cur = []
    cur.append(p_)
if cur: runs.append(cur)
print(json.dumps({"paddings": 256, "exit": sorted({r["exit"] for r in rows}),
                  "distribution": {str(k): sum(1 for r in rows if r["failed"] == k) for k in sorted({r["failed"] for r in rows}, key=str)},
                  "pads_giving_42": [[r[0], r[-1]] for r in runs]}))
