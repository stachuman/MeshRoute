#!/usr/bin/env bash
# W4b coder gate, brief §4.1 steps 1-7 (the board pair and the union are separate runs). Raw logs -> artifacts/.
set -u
cd /home/staszek/MeshRoute
S=/tmp/claude-1001/-home-staszek-MeshRoute/7019fa4d-8324-4b18-b429-3c8de8a32587/scratchpad/w4b
A=artifacts/2026-09-27-standalone-mobile-home-w4b/final
mkdir -p "$A"
T="$A/steps.tsv"; : > "$T"
step() { local name=$1; shift; local t0=$(date +%s); "$@" > "$A/$name.log" 2>&1; local rc=$?; printf '%s\trc=%s\t%ss\n' "$name" "$rc" "$(( $(date +%s) - t0 ))" >> "$T"; return 0; }
step 01-diff-check git diff --check
step 02-native-build pio test -e native
step 02-native-binary ./.pio/build/native/program
SIMB=$(mktemp -d /tmp/w4b-final-sim-XXXXXX)
step 03-sim-configure cmake -S /home/staszek/lora-universal-simulator -B "$SIMB/sim-build" -DCMAKE_BUILD_TYPE=Release -DMESHROUTE_DIR=/home/staszek/MeshRoute
step 03-sim-build cmake --build "$SIMB/sim-build" --target lus -j 4
step 03-corpus python3 -B tools/run_corpus.py --out "$SIMB/corpus" --lus "$SIMB/sim-build/orchestrator/lus" --require-anchors --jobs 4
echo "$SIMB" > "$A/sim-dir.txt"
step 03-corpus-validate python3 -B tools/run_corpus.py --validate "$SIMB/corpus"
step 03-corpus-compare python3 "$S/corpus_compare.py" docs/superpowers/evidence/2026-09-27-standalone-mobile-home-w4b-precheck/corpus-manifest.json "$SIMB/corpus/manifest.json"
step 04-abi python3 -B tools/probe_board_abi.py
LAY=$(mktemp -d /tmp/w4b-final-layout-XXXXXX)
step 04-layout python3 -B "$S/w4b_layout.py" "$LAY" "$LAY/layout.json"
echo "$LAY" > "$A/layout-dir.txt"
step 05-fwui bash tools/probe_firmware_ui/run.sh
step 05-fwui-completeness python3 "$S/completeness.py" tools/probe_firmware_ui/run.sh "$A/05-fwui.log"
step 05-fwui-noneg bash tools/probe_firmware_ui/run.sh --no-neg
step 05-board-ui-noneg bash tools/probe_board_ui/run.sh --no-neg
step 06-discovery python3 -m unittest discover -s tools -p "test_*.py"
step 07-census bash tools/warning_census.sh
echo DONE >> "$T"
