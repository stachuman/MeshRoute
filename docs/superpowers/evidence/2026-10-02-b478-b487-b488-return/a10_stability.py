# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# B492 / A10 proofs 3-4 — ONE retained new-F07 binary (built by the stock harness, MR_MUT_KEEP_SCRATCH=1): its count
# and failing-site set over 200 runs WITH address randomization, then over round 2's 256-offset stack sweep WITHOUT it
# (`setarch -R`, an environment pad of 0..255 bytes). Required: one count and one site set on every run, including the
# `w4a-ident: lengths` roomy-capacity check. Usage: python3 -B a10_stability.py <program> <cwd> <raw-dir> <out.json>
import collections, hashlib, json, os, platform, re, subprocess, sys
prog, cwd, raw, out_json = sys.argv[1:5]
os.makedirs(raw, exist_ok=True)
def run(tag, cmd, env=None):
    p = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True)
    data = p.stdout + p.stderr
    open(os.path.join(raw, tag + ".out"), "wb").write(data)
    m = re.findall(rb"assertions: *(\d+) \| *(\d+) passed \| *(\d+) failed", p.stdout)
    c = re.findall(rb"test cases: *(\d+) \| *(\d+) passed \| *(\d+) failed", p.stdout)
    sites = sorted(set(x.decode() for x in re.findall(rb"(test/[\w./]+:\d+): ERROR", data)))
    return {"tag": tag, "exit": p.returncode, "failed_assertions": int(m[-1][2]) if m else None,
            "failed_cases": int(c[-1][2]) if c else None, "sites": sites, "sha256": hashlib.sha256(data).hexdigest()}
rows = [run(f"aslr-on-{i:03d}", [prog]) for i in range(200)]
base_env = {"PATH": "/usr/bin:/bin", "HOME": os.environ.get("HOME", "/tmp")}
rows += [run(f"aslr-off-pad-{k:03d}", ["setarch", platform.machine(), "-R", prog], dict(base_env, MRPAD="x" * k))
         for k in range(256)]
counts = collections.Counter(r["failed_assertions"] for r in rows)
sitesets = collections.Counter(tuple(r["sites"]) for r in rows)
exits = sorted({r["exit"] for r in rows})
one = rows[0]
stable = len(counts) == 1 and len(sitesets) == 1 and exits == [1]
roomy = "test/test_firmware_ui_model.cpp:2756"     # `w4a-ident: lengths`: id_fmt(b, 48, src, 32, 0, 6) -> "ABCDE»"
result = {"program_sha256": hashlib.sha256(open(prog, "rb").read()).hexdigest(), "runs": len(rows),
          "aslr_on": 200, "aslr_off_pad_sweep": 256, "exits": exits,
          "failed_assertions_distribution": {str(k): v for k, v in counts.items()},
          "failed_cases": sorted({r["failed_cases"] for r in rows}),
          "distinct_site_sets": len(sitesets), "sites": one["sites"],
          "roomy_capacity_check_failing": roomy in one["sites"],
          "verdict": "STABLE" if stable else "UNSTABLE — STOP-2",
          "per_run": [{k: r[k] for k in ("tag", "exit", "failed_assertions", "sha256")} for r in rows]}
open(out_json, "w").write(json.dumps(result, indent=1) + "\n")
print(json.dumps({k: result[k] for k in ("runs", "exits", "failed_assertions_distribution", "failed_cases",
                                         "distinct_site_sets", "roomy_capacity_check_failing", "verdict")}))
print("\n".join(one["sites"]))
