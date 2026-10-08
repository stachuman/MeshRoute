#!/bin/bash
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# W7+W8 QA (2026-10-07), brief rev 2 §4.2 + the 2026-10-06 checkpoint return, on the coder's freeze. Sequential.
# Every command's exit is recorded; a failed `step` ends the chain nonzero with no later step and no DONE (B499's
# corrected shape). `observe` records an exit WITHOUT stopping — used only for the stock cross-run compare, whose
# provenance mismatch is expected and is explained by qa_board_compare.py + the source reconstruction.
# NOTHING git-visible is written while this runs: logs/outputs go to the ignored artifacts folder and the scratchpad.
cd /home/staszek/MeshRoute || exit 1
Q=artifacts/2026-10-07-standalone-mobile-home-w7w8-qa
E=docs/superpowers/evidence/2026-10-04-standalone-mobile-home-w7w8
S=/tmp/claude-1001/-home-staszek-MeshRoute/8d52c413-0d36-408f-8ca7-2401acd9731c/scratchpad/qa-run
export PYTHONDONTWRITEBYTECODE=1
rm -rf "$S"; mkdir -p "$S"
step() { local name=$1; shift; local t0=$(date +%s); "$@" > "$Q/$name.log" 2>&1; local rc=$?
         printf '%s\t%s\t%s\n' "$name" "$rc" "$(( $(date +%s) - t0 ))" >> "$Q/runs.tsv"
         if [ "$rc" -ne 0 ]; then echo "STOP: step $name exited $rc" >> "$Q/runs.tsv"; exit "$rc"; fi; }
observe() { local name=$1; shift; local t0=$(date +%s); "$@" > "$Q/$name.log" 2>&1; local rc=$?
            printf '%s\t%s\t%s\tobserved\n' "$name" "$rc" "$(( $(date +%s) - t0 ))" >> "$Q/runs.tsv"; }
: > "$Q/runs.tsv"
step inv-start python3 -B $Q/qa_inventory.py $Q/inv-start.json
# B499: the coder's synthetic proof against the coder's real runner (no gate runs)
step b499-proof python3 -B $E/runner_step_proof.py artifacts/2026-10-04-standalone-mobile-home-w7w8/run-final.sh $Q/b499-proof.json
# native: the brief's literal command on the frozen tree, then an independent cold build (no ccache) in a copy
step native-build pio test -e native
step native-binary ./.pio/build/native/program
step native-cold-copy rsync -a --delete --exclude=.git --exclude=.pio --exclude=.pio-measure --exclude=artifacts ./ "$S/native-cold/"
step native-cold-build env CCACHE_DISABLE=1 bash -c "cd '$S/native-cold' && pio test -e native"
step native-cold-binary "$S/native-cold/.pio/build/native/program"
step native-cold-cleanup rm -rf "$S/native-cold"
# corpus: a fresh stock lus, then per-stream identity with the coder's baseline AND final manifests
step sim-configure cmake -S /home/staszek/lora-universal-simulator -B "$S/sim-build" -DCMAKE_BUILD_TYPE=Release -DMESHROUTE_DIR=/home/staszek/MeshRoute
step sim-build cmake --build "$S/sim-build" --target lus -j 4
step lus-sha sha256sum "$S/sim-build/orchestrator/lus"
step corpus python3 -B tools/run_corpus.py --out "$S/corpus" --lus "$S/sim-build/orchestrator/lus" --require-anchors --jobs 4
step corpus-copy cp "$S/corpus/manifest.json" $Q/corpus-manifest-qa.json
step corpus-vs-base python3 -B $Q/qa_corpus_compare.py $E/receipts/corpus-manifest-base.json $Q/corpus-manifest-qa.json $Q/corpus-vs-base.json e304147d99ae166fb815e6a2ef06c5ff905579b68add3b0ba142d7031e26caa2
step corpus-vs-final python3 -B $Q/qa_corpus_compare.py $E/receipts/corpus-manifest-final.json $Q/corpus-manifest-qa.json $Q/corpus-vs-final.json e304147d99ae166fb815e6a2ef06c5ff905579b68add3b0ba142d7031e26caa2
# boards: QA's own pair, then field-level reproduction of the coder's final-1/final-2
step board-qa-1 python3 -B tools/measure_board.py pair --jobs=1 --output .pio-measure/w7w8/qa-1
observe stock-compare-gateway-final-1 python3 -B tools/measure_board.py compare .pio-measure/w7w8/final-1/gateway/manifest.json .pio-measure/w7w8/qa-1/gateway/manifest.json
observe stock-compare-heltec_mobile-final-1 python3 -B tools/measure_board.py compare .pio-measure/w7w8/final-1/heltec_mobile/manifest.json .pio-measure/w7w8/qa-1/heltec_mobile/manifest.json
step board-fields python3 -B $Q/qa_board_compare.py .pio-measure/w7w8/qa-1 .pio-measure/w7w8/final-1 .pio-measure/w7w8/final-2 $Q/board-fields.json
# ABI: stock, then the coder's supplemental manifest
step abi python3 -B tools/probe_board_abi.py
step abi-supplemental python3 -B tools/probe_board_abi.py --extra-pins $E/abi-supplemental.json
# probes, defaults
step firmware-ui tools/probe_firmware_ui/run.sh
step board-ui tools/probe_board_ui/run.sh
# mutation: selector (a) + uieditor (default workers), then B498's three uipresets entries (serial reference)
for t in model chrome uisend sliceCbudget sliceCsend w4aident w4bhome uieditor; do
  step mut-$t python3 -B tools/probe_ui_model_mutations.py --target=$t
done
for e in U13 W6-P1 W6-P3; do
  step mut-uipresets-$e python3 -B tools/probe_ui_model_mutations.py --target=uipresets --workers=1 $e
done
# tools discovery + command inventory
step discovery python3 -B -m unittest discover -s tools -p 'test_*.py'
step inventory python3 -B tools/gen_command_inventory.py --check
# stability: the same inventory at the end
step inv-end python3 -B $Q/qa_inventory.py $Q/inv-end.json
step stability python3 -B $Q/qa_stability.py $Q/inv-start.json $Q/inv-end.json $Q/stability.json
echo DONE >> "$Q/runs.tsv"
