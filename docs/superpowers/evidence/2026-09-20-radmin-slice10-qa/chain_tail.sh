#!/bin/bash
S=/tmp/claude-1001/-home-staszek-MeshRoute/29a5e6f2-869e-46c1-aa18-9fb7b0d23189/scratchpad/s10gate; cd /home/staszek/MeshRoute
echo "== CENSUS $(date +%H:%M:%S)"; tools/warning_census.sh > $S/census.log 2>&1; echo "rc=$?"; grep -E "^(gateway|heltec|xiao)|^PASS|^FAIL|Wswitch" $S/census.log
echo "== BOARD PAIR $(date +%H:%M:%S)"; python3 tools/measure_board.py pair --output .pio-measure/qa-s10-final --jobs=2 > $S/board.log 2>&1; echo "rc=$?"; grep -h "RAM=" $S/board.log; for e in gateway heltec_mobile; do echo "$e elf=$(sha256sum .pio-measure/qa-s10-final/$e/firmware.elf | cut -c1-16)"; done
echo "== XIAO one-off"; pio run -e xiao_mobile > $S/xiao.log 2>&1; echo "rc=$?"; grep -E "^RAM:|^Flash:" $S/xiao.log | sed "s/  */ /g"
echo "== END $(date +%H:%M:%S)"; touch $S/chain.done
