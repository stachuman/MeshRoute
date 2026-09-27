# Scratch reproduction of the W1c battery entries: apply each entry (exact text, match count 1) to a SCRATCH copy,
# build + run the native suite there, and record the failing TEST CASE names and assertion counts. Never touches the checkout.
import ast, subprocess, sys, pathlib, re
T = pathlib.Path(sys.argv[1]); OUT = pathlib.Path(sys.argv[2])
src = (T / "tools/probe_ui_model_mutations.py").read_text(); t = ast.parse(src)
env = {}; reg = {}
for n in t.body:
    if isinstance(n, ast.Assign) and len(n.targets) == 1 and isinstance(n.targets[0], ast.Name):
        nm = n.targets[0].id
        if nm == "TARGET_SRC" or nm.startswith("MUTS_W1C"): env[nm] = ast.literal_eval(n.value)
        if nm == "MUTS_BY_TARGET": reg = {k.value: v.id for k, v in zip(n.value.keys, n.value.values)}
for tgt in ("w1cname", "w1cretain"):
    path = T / env["TARGET_SRC"][tgt]; orig = path.read_text()
    for lab, find, repl in env[reg[tgt]]:
        tag = lab[:3]; assert orig.count(find) == 1, (tgt, tag)
        path.write_text(orig.replace(find, repl))
        b = subprocess.run(["pio", "test", "-e", "native"], cwd=T, capture_output=True, text=True)
        r = subprocess.run([str(T / ".pio/build/native/program")], cwd=T, capture_output=True, text=True)
        path.write_text(orig)
        cases = sorted(set(re.findall(r"TEST CASE:\s+(.*)", r.stdout)))
        summ = [l for l in r.stdout.splitlines() if l.startswith("[doctest] test cases") or l.startswith("[doctest] assertions")]
        with open(OUT / f"{tgt}-{tag}.txt", "w") as f:
            f.write(f"{tgt} {lab}\nbuild rc={b.returncode} run rc={r.returncode}\n" + "\n".join(summ) + "\nFAILING CASES:\n" + "\n".join("  " + c for c in cases) + "\n")
        print(tgt, tag, "run rc", r.returncode, summ, len(cases), "failing cases", flush=True)
