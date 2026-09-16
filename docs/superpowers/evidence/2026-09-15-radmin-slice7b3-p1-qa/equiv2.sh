#!/bin/bash
S=/tmp/claude-1001/-home-staszek-MeshRoute/29a5e6f2-869e-46c1-aa18-9fb7b0d23189/scratchpad/p1gate; cd /home/staszek/MeshRoute
echo "== BASELINE (pristine c591721 handlers, corrected path)"; python3 tools/probe_deferred_actions/run.py --no-neg --baseline-source $S/baseline-src --out $S/probe-baseline > $S/equiv_baseline.log 2>&1; echo "rc=$?"; tail -3 $S/equiv_baseline.log
echo "== TRANSCRIPT COMPARISON (paired by relative file path)"; python3 - <<'PY'
import glob,re,os
S='/tmp/claude-1001/-home-staszek-MeshRoute/29a5e6f2-869e-46c1-aa18-9fb7b0d23189/scratchpad/p1gate'
def tr(root):
    out={}
    for p in glob.glob(root+'/**/*',recursive=True):
        if os.path.isfile(p) and not p.endswith('build.log'):
            t=re.findall(r'^TRANSCRIPT .*$',open(p,errors='replace').read(),re.M)
            if t: out[os.path.relpath(p,root)]=t
    return out
b=tr(S+'/probe-baseline'); f=tr(S+'/probe-final')
same=0; total=0
for k in sorted(set(b)|set(f)):
    x=b.get(k); y=f.get(k); total+=1
    st='IDENTICAL' if x==y else ('MISSING-IN-BASELINE' if x is None else ('MISSING-IN-FINAL' if y is None else 'DIFF'))
    if x==y: same+=1
    print(f"{k}: baseline {len(x) if x else 0} final {len(y) if y else 0} {st}")
print(f"SUMMARY: {same}/{total} transcript files byte-identical; lines baseline={sum(len(v) for v in b.values())} final={sum(len(v) for v in f.values())}")
PY
touch $S/equiv2.done
