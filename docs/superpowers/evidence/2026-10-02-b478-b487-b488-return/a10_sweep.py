# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# B492 / A10 proof 1 — build and run the three-arm sweep (clean, new F07, old F07) on SCRATCH copies of the real header.
# Usage: python3 -B a10_sweep.py <artifact-dir> <out.json>. The harness is read with ast (never imported); the old F07
# replacement is read from the committed candidate (HEAD), the new one from the working tree. No product file changes.
import ast, hashlib, json, os, subprocess, sys
sys.dont_write_bytecode = True
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
art, out_json = os.path.abspath(sys.argv[1]), sys.argv[2]
HERE = os.path.dirname(os.path.abspath(__file__))
def f07(source):
    for n in ast.parse(source).body:
        if isinstance(n, ast.Assign) and isinstance(n.targets[0], ast.Name) and n.targets[0].id == "MUTS_W4AIDENT":
            rows = [r for r in ast.literal_eval(n.value) if r[0].startswith("F07 ")]
            assert len(rows) == 1
            return rows[0]
cur = open(os.path.join(ROOT, "tools/probe_ui_model_mutations.py"), "rb").read()
old_src = subprocess.check_output(["git", "show", "HEAD:tools/probe_ui_model_mutations.py"], cwd=ROOT)
(_, pat, new_rep), (_, old_pat, old_rep) = f07(cur), f07(old_src)
assert pat == old_pat
header = open(os.path.join(ROOT, "src/firmware_ui_model.h"), encoding="utf-8").read()
assert header.count(pat) == 1, "F07's pattern must match the real header exactly once"
DEFS = ["-std=gnu++2a", "-fno-exceptions", "-fno-rtti", "-O0", "-g", "-Wall", "-Wextra", "-DMESHROUTE_NATIVE=1",
        "-DMR_N_LAYERS=2", "-DMR_CONSOLE=1", "-DPROTOCOL_VERSION=1", "-DMR_RADIO_CANARY=1"]
INCS = ["-I" + os.path.join(ROOT, d) for d in ("src", "lib/core", "lib/hal", "lib/console", "lib/monocypher/src")]
result = {"pattern": pat, "new_replacement": new_rep, "old_replacement": old_rep, "arms": {}}
for arm, text in (("clean", header), ("new_f07", header.replace(pat, new_rep)), ("old_f07", header.replace(pat, old_rep))):
    d = os.path.join(art, "a10-sweep", arm)
    os.makedirs(d, exist_ok=True)
    open(os.path.join(d, "firmware_ui_model.h"), "w", encoding="utf-8").write(text)
    exe = os.path.join(d, "sweep")
    cc = subprocess.run(["g++", *DEFS, f'-DMR_A10_ARM="{arm}"', "-I" + d, *INCS, os.path.join(HERE, "a10_sweep.cpp"),
                         "-o", exe], capture_output=True, text=True)
    if cc.returncode:
        raise SystemExit(f"{arm}: the sweep did not compile:\n{cc.stderr[-3000:]}")
    run = subprocess.run([exe, os.path.join(d, "windows.bin")], capture_output=True, text=True)
    entry = {"header_sha256": hashlib.sha256(text.encode()).hexdigest(), "exit": run.returncode,
             "stderr": run.stderr[-500:]}
    if run.returncode != 0 or not run.stdout.strip().startswith("{"):
        entry["verdict"] = "CRASH OR NO SUMMARY — never a sweep verdict"
    else:
        entry.update(json.loads(run.stdout))
    result["arms"][arm] = entry
# Cross-arm: which calls does each mutant change, compared with the clean formatter?
K = 48
win = {a: open(os.path.join(art, "a10-sweep", a, "windows.bin"), "rb").read() for a in result["arms"]}
grid = [(h, l, c, k) for h in (0, 0xA0000011) for l in range(33) for c in range(33) for k in range(49)]
for arm in ("new_f07", "old_f07"):
    diff = [g for i, g in enumerate(grid) if win[arm][i * K:(i + 1) * K] != win["clean"][i * K:(i + 1) * K]]
    result["arms"][arm]["calls_differing_from_clean"] = len(diff)
    result["arms"][arm]["differing_calls_rule"] = {
        "len > cols >= 1 and cap >= cols + 2": sum(1 for h, l, c, k in diff if l > c >= 1 and k >= c + 2),
        "len > cols >= 1 and cap == cols + 1": sum(1 for h, l, c, k in diff if l > c >= 1 and k == c + 1),
        "other": sum(1 for h, l, c, k in diff if not (l > c >= 1 and k >= c + 1))}
a = result["arms"]
ok = (all(a[x].get("cap0_touched") == 0 and a[x].get("positive_cap_without_nul") == 0
          and a[x].get("calls_writing_outside_capacity") == 0 for x in ("clean", "new_f07"))
      and a["old_f07"].get("calls_writing_outside_capacity", 0) > 0 and a["new_f07"]["calls_differing_from_clean"] > 0)
result["verdict"] = ("PASS — clean and new F07 never touch capacity 0 and always leave a NUL inside a positive capacity "
                     "with nothing outside it changed; the old F07 shows its out-of-capacity writes" if ok else "FAIL")
open(out_json, "w").write(json.dumps(result, indent=1) + "\n")
for k, v in a.items():
    print(k, {x: v.get(x) for x in ("exit", "calls", "cap0_touched", "positive_cap_without_nul",
                                    "calls_writing_outside_capacity", "max_bytes_past_capacity",
                                    "calls_differing_from_clean", "differing_calls_rule")})
print(result["verdict"])
