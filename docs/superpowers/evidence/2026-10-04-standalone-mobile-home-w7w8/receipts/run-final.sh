#!/bin/bash
# W7+W8 §4.1 FINAL CHAIN, steps 1–13, on ONE freeze. Raw logs and every chain output go to the ignored artifacts
# folder; NOTHING is written to the checkout's git-visible files until the chain has ended (the evidence is copied in
# afterwards). Between final-1 and final-2: no edit, no evidence write, no native/probe build, no normal .pio change.
cd /home/staszek/MeshRoute || exit 1
A=artifacts/2026-10-04-standalone-mobile-home-w7w8
E=docs/superpowers/evidence/2026-10-04-standalone-mobile-home-w7w8
X=$A/final-chain
mkdir -p "$X"
export PYTHONDONTWRITEBYTECODE=1
S=$(mktemp -d /tmp/w7w8-final-XXXXXX); echo "$S" > $A/final-scratch.txt
# >>> step (B499: extracted verbatim by the evidence's runner_step_proof.py — keep the markers)
# ★ Every command's exit is RECORDED first; a failed step then ENDS the chain nonzero, with neither a later step nor
#   DONE. A chain that "finished" past a red step is not a chain result.
step() { local name=$1; shift; local t0=$(date +%s); "$@" > "$A/final-$name.log" 2>&1; local rc=$?
         printf '%s\t%s\t%s\n' "$name" "$rc" "$(( $(date +%s) - t0 ))" >> "$A/final-runs.tsv"
         if [ "$rc" -ne 0 ]; then echo "STOP: step $name exited $rc - the chain ends here (no later step, no DONE)" >&2; exit "$rc"; fi; }
# <<< step
: > "$A/final-runs.tsv"
# 0 B499: the helper this chain uses must stop on a failed step (synthetic proof against THIS file; no gate runs)
step b499-proof python3 -B $E/runner_step_proof.py $A/run-final.sh $X/b499-proof.json
# 1 scope and inputs (+ the start of 13's stability)
step scope-start python3 -B $E/scope.py final $X/scope-start.json
# 2 native, rebuilt on the frozen tree, then the binary
step native-build pio test -e native
step native-binary ./.pio/build/native/program
# 3 corpus: a fresh stock lus, then per-stream identity with baseline step 3
step sim-configure cmake -S /home/staszek/lora-universal-simulator -B "$S/sim-build" -DCMAKE_BUILD_TYPE=Release -DMESHROUTE_DIR=/home/staszek/MeshRoute
step sim-build cmake --build "$S/sim-build" --target lus -j 4
step lus-sha sha256sum "$S/sim-build/orchestrator/lus"
step corpus python3 -B tools/run_corpus.py --out "$S/corpus" --lus "$S/sim-build/orchestrator/lus" --require-anchors --jobs 4
step corpus-compare python3 -B $E/corpus_compare.py $E/receipts/corpus-manifest-base.json "$S/corpus/manifest.json" $X/corpus-compare.json
# 4 boards: final-1 then final-2 (sequence rules), the two stock compares, then base -> final attribution
step board-final-1 python3 -B tools/measure_board.py pair --jobs=1 --output .pio-measure/w7w8/final-1
step board-final-2 python3 -B tools/measure_board.py pair --jobs=1 --output .pio-measure/w7w8/final-2
step compare-gateway python3 -B tools/measure_board.py compare .pio-measure/w7w8/final-1/gateway/manifest.json .pio-measure/w7w8/final-2/gateway/manifest.json
step compare-heltec_mobile python3 -B tools/measure_board.py compare .pio-measure/w7w8/final-1/heltec_mobile/manifest.json .pio-measure/w7w8/final-2/heltec_mobile/manifest.json
step board-attribution python3 -B $E/board_attribution.py $X/board-attribution.json base-1 final-1
# 5 ABI: stock, then the supplemental manifest
step abi python3 -B tools/probe_board_abi.py
step abi-supplemental python3 -B tools/probe_board_abi.py --extra-pins $E/abi-supplemental.json
# 6 warning census
step warning-census tools/warning_census.sh
# 7 firmware-UI default, 8 board-UI default
step firmware-ui tools/probe_firmware_ui/run.sh
step board-ui tools/probe_board_ui/run.sh
# 9 the union (selectors a + b + uieditor), then its verification at the new native floor
step union-census python3 -B $E/union_census.py $X/union-census-final.json $E/union-census-base.json
step union $A/dev-union.sh final
step union-verify python3 -B $E/union_verify.py final $X/union-census-final.json "${MR_FLOOR_CASES:?}" "${MR_FLOOR_ASSERTS:?}" $X/union-verify.json
# 10 discovery + inventory
step discovery python3 -B -m unittest discover -s tools -p 'test_*.py'
step inventory python3 -B tools/gen_command_inventory.py --check
# 11 dependency safety nets (read-only inputs): the inbox-verbs default, then the two-profile stock transcript
step inbox-verbs tools/probe_inbox_verbs/run.sh
for p in full_headless mobile; do
  for n in 1 2; do
    step transcript-$p-$n python3 -B tools/probe_inbox_verbs/transcript.py --profile $p --out "$S/$p-$n" --emit "$S/$p-$n/ledger.txt"
  done
  step transcript-compare-$p python3 -B tools/probe_inbox_verbs/transcript.py --compare "$S/$p-1/ledger.txt" "$S/$p-2/ledger.txt"
done
# 12 reader audit (incl. the whole-tree scanners)
step reader-audit python3 -B $E/reader_audit.py $X/reader-audit.json
# 13 input stability: the frozen inputs at the end equal the start
step scope-end python3 -B $E/scope.py final $X/scope-end.json
step stability python3 -B $E/stability.py $X/scope-start.json $X/scope-end.json $X/stability.json
echo DONE >> "$A/final-runs.tsv"
