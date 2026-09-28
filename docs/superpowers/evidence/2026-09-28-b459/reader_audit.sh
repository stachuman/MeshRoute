#!/usr/bin/env bash
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# B459 §4.1 final step 10 — reader audit (D6/P7): every reader, in lib/ src/ test/ tools/, of the files, lines and
# output B459 changed; then W2's frozen evidence parsers, which read this runner's source, trace and output. Reads only.
cd /home/staszek/MeshRoute || exit 1
D=(lib src test tools)
X=(--exclude-dir=__pycache__ --exclude-dir=.pio --binary-files=without-match)
sec() { echo; echo "=== $1"; }
g() { grep -rnF "${X[@]}" -e "$1" "${D[@]}" | cut -c1-220; echo "   ($(grep -rlF "${X[@]}" -e "$1" "${D[@]}" | wc -l) file(s) for: $1)"; }
sec '1. the changed files, by path — who reads or runs them'
for s in 'probe_board_ui/run.sh' 'probe_board_ui/probe_main.cpp' 'probe_main.cpp' 'negctl.py' 'probe_accounting.sh' \
         'expected.tsv' 'probe_board_ui/accounting.py' 'probe_firmware_ui/run.sh' 'test_probe_firmware_ui' 'test_probe_board_ui'; do g "$s"; done
sec '2. the environment variables that carry records'
for s in 'MR_BOARD_UI_RECORDS' 'MR_BOARD_UI_NEGCTL_RECORDS' 'MR_BOARD_UI_NEGCTL_ARM'; do g "$s"; done
sec '3. the changed and new output lines'
for s in 'controls run' 'identities accounted' 'declarations census' 'PROBE-ONLY' 'wiring predicate exits' 'SKIPPED by mode' \
         'traits:' 'missing:' 'structural:' 'wiring:' '== negative controls (each MUST fail) ==' 'V4 fixed-ADC source controls'; do g "$s"; done
sec '4. the changed functions and their callers'
for s in 'schk ' 'account_controls' 'record_verdict' 'record_guard_failure' 'controls_final' 'pa_compare' 'pa_record' \
         'CHK_ITER' 'selftest-accounting'; do g "$s"; done
sec '5. B461 — the W50 comment text, old and new'
for s in 'Control (c)' 'Control 2 is the plausible'; do g "$s"; done
sec '6. W2 frozen evidence parsers (docs/, not a gate input) — what they read'
for f in docs/superpowers/evidence/2026-09-28-standalone-mobile-home-w2/*.py docs/superpowers/evidence/2026-09-28-standalone-mobile-home-w2-qa/*.py; do
  [ -f "$f" ] || continue
  echo "--- $f"
  grep -nE 'run\.sh|negctl|probe_main|schk|wchk|trait_control|missing_trait|w_ctl|w_pass|MUT_V|== negative|V4 fixed|controls run' "$f" | cut -c1-200 | head -30
done
