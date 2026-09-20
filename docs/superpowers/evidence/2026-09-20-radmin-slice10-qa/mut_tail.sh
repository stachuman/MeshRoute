#!/bin/bash
S=/tmp/claude-1001/-home-staszek-MeshRoute/29a5e6f2-869e-46c1-aa18-9fb7b0d23189/scratchpad/s10gate; ST=/home/staszek/mr-s10-qa-stage; cd $ST
for t in teamkeyring; do echo "== $t $(date +%H:%M:%S)"; python3 tools/probe_ui_model_mutations.py --target=$t --workers=3 > $S/mut_$t.log 2>&1; echo "rc=$?"; grep -E "^mutations:|FAIL |UNUSABLE|VACUOUS|survivor|instrument|baseline" $S/mut_$t.log | tail -4; done
sha256sum src/device_nv.h src/firmware_config.h src/fw_main.cpp lib/core/node.h tools/probe_ui_model_mutations.py | cut -c1-16,65- > $S/mut_stage_hashes_after.txt
touch $S/mut.done
