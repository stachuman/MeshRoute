#!/bin/bash
S=/tmp/claude-1001/-home-staszek-MeshRoute/29a5e6f2-869e-46c1-aa18-9fb7b0d23189/scratchpad/s10gate; ST=/home/staszek/mr-s10-qa-stage
until [ -f $S/mut.done ]; do sleep 15; done
cd $ST; for t in radmin5session; do echo "== $t RERUN $(date +%H:%M:%S)"; python3 tools/probe_ui_model_mutations.py --target=$t --workers=3 > $S/mut_${t}_rerun.log 2>&1; echo "rc=$?"; grep -E "^mutations:|UNUSABLE|VACUOUS|baseline" $S/mut_${t}_rerun.log | tail -4; done
touch $S/mut2.done
