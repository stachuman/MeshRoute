#!/bin/bash
S=/tmp/claude-1001/-home-staszek-MeshRoute/29a5e6f2-869e-46c1-aa18-9fb7b0d23189/scratchpad/s8acgate; LUS=/home/staszek/lora-universal-simulator/build/orchestrator/lus; cd /home/staszek/MeshRoute
echo "== SIM REBUILD (source list + remote_client.cpp)"; cmake -S /home/staszek/lora-universal-simulator -B /home/staszek/lora-universal-simulator/build > $S/lus_configure2.log 2>&1; echo "configure rc=$?"; cmake --build /home/staszek/lora-universal-simulator/build -j8 > $S/lus_build2.log 2>&1; echo "cmake rc=$?"; grep -cE "Building|Linking" $S/lus_build2.log; grep -c remote_client $S/lus_build2.log; md5sum $LUS
echo "== CORPUS (rerun)"; python3 tools/run_corpus.py --jobs=8 --require-anchors --out $S/corpus2 --lus $LUS > $S/corpus2.log 2>&1; echo "corpus rc=$?"; grep -vE '^\s+\[' $S/corpus2.log | tail -2; md5sum $S/corpus2/streams/s18_meshroute.ndjson | cut -c1-8; grep -l radmin $S/corpus2/streams/*.ndjson 2>/dev/null | wc -l
touch $S/sim2.done
