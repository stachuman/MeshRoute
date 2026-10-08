#!/bin/bash
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# W7+W8 QA (2026-10-07) — CONTINUATION of qa-chain.sh after its `mut-model` step stopped the chain with exit 9: every
# worker's CLEAN tree failed to build with ENOSPC (Errno 28) during the scratch-tree copies, so the harness measured
# nothing and returned no verdict (correctly refused). Not a product result. The same steps resume here with
# `--workers=4` (half the scratch footprint; the harness proves verdicts identical at every worker count). Same
# helpers, same stop-on-failure rule, the same runs.tsv. Nothing git-visible is written while this runs.
cd /home/staszek/MeshRoute || exit 1
Q=artifacts/2026-10-07-standalone-mobile-home-w7w8-qa
export PYTHONDONTWRITEBYTECODE=1
step() { local name=$1; shift; local t0=$(date +%s); "$@" > "$Q/$name.log" 2>&1; local rc=$?
         printf '%s\t%s\t%s\n' "$name" "$rc" "$(( $(date +%s) - t0 ))" >> "$Q/runs.tsv"
         if [ "$rc" -ne 0 ]; then echo "STOP: step $name exited $rc" >> "$Q/runs.tsv"; exit "$rc"; fi; }
mv "$Q/mut-model.log" "$Q/enospc-mut-model.log"
printf 'RESUME\tqa-chain-2.sh\t--workers=4 after ENOSPC (mut-model exit 9, no verdicts; log kept as enospc-mut-model.log)\n' >> "$Q/runs.tsv"
step disk-before df -h /
for t in model chrome uisend sliceCbudget sliceCsend w4aident w4bhome uieditor; do
  step mut-$t python3 -B tools/probe_ui_model_mutations.py --target=$t --workers=4
done
for e in U13 W6-P1 W6-P3; do
  step mut-uipresets-$e python3 -B tools/probe_ui_model_mutations.py --target=uipresets --workers=1 $e
done
step discovery python3 -B -m unittest discover -s tools -p 'test_*.py'
step inventory python3 -B tools/gen_command_inventory.py --check
step inv-end python3 -B $Q/qa_inventory.py $Q/inv-end.json
step stability python3 -B $Q/qa_stability.py $Q/inv-start.json $Q/inv-end.json $Q/stability.json
echo DONE >> "$Q/runs.tsv"
