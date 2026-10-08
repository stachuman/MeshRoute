#!/bin/bash
# W7+W8 development: the FULL mutation union (selector a + selector b + uieditor), one battery at a time,
# default workers. Never edit the tree while this runs.
cd /home/staszek/MeshRoute || exit 1
A=artifacts/2026-10-04-standalone-mobile-home-w7w8
P=${1:-dev}
export PYTHONDONTWRITEBYTECODE=1
: > "$A/$P-union-summary.txt"
for t in model chrome uisend sliceCbudget sliceCsend w4aident w4bhome \
         uistatus uiteam uiinvite uiprov uijoin uipresets config consoleline uieditor; do
  s=$(date +%s)
  python3 -B tools/probe_ui_model_mutations.py --target=$t > "$A/$P-union-$t.log" 2>&1
  rc=$?
  echo "$t rc=$rc wall=$(( $(date +%s) - s ))s $(grep -E '^mutations:' "$A/$P-union-$t.log" | tail -1)" >> "$A/$P-union-summary.txt"
done
echo DONE >> "$A/$P-union-summary.txt"
