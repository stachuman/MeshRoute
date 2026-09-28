#!/usr/bin/env bash
cd /home/staszek/MeshRoute
S=$1; tag=$2; shift 2
for t in "$@"; do
  start=$(date +%s)
  timeout 5400 python3 tools/probe_ui_model_mutations.py --target=$t > $S/mut-$t-$tag.log 2>&1; rc=$?
  end=$(date +%s)
  printf '%s\trc=%s\t%ss\t%s\n' "$t" "$rc" "$((end-start))" "$(grep -a '^mutations:' $S/mut-$t-$tag.log)" >> $S/union-$tag.tsv
done
echo DONE >> $S/union-$tag.tsv
