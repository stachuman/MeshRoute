#!/bin/bash
# Author: coder (B478/B487/B488/B490 package) — §4.1 step 11 (D6/P7): every reader of the changed symbols and output
# in lib/ src/ test/ tools/. Read-only greps; the classification of each hit is in the report.
cd "$(dirname "$0")/../../../.." || exit 1
s() { echo; echo "### $1"; shift; "$@" 2>/dev/null; true; }
echo "# reader audit — $(git rev-parse --short HEAD), $(date -u +%FT%TZ)"
s "harness symbols (SuiteOutput/SuiteFailure/run_suite/unusable_verdict/retain_unusable/escape/bounded_view/ship/receive/retain_shipped)" \
  grep -rn -e 'SuiteOutput\|SuiteFailure\|run_suite\|unusable_verdict\|retain_unusable\|unusable_log_path\|escape_child_bytes\|bounded_view\|child_lines\|stream_record\|unretained_record\|ship_streams\|receive_streams\|retain_shipped' lib src test tools --include=*.py --include=*.sh --include=*.cpp --include=*.h
s "harness output: retained files, the self-test, the result-JSON fields (capture/evidence/refusal)" \
  grep -rln -e 'mutation-unusable\|selftest-unusable\|"capture"\|\["evidence"\]\|"refusal"' lib src test tools
s "files naming the harness (comment references included)" grep -rln 'probe_ui_model_mutations' lib src test tools
s "test_worker_formula_derived.py's patterns (it reads the harness text)" grep -n 're.compile\|count(' tools/test_worker_formula_derived.py
s "the unusable-reason controls' anchors, each in the harness (must be 1)" bash -c '
  for a in "if reason == \"build\":" "path.write_text(view, encoding=\"ascii\")" "    if agrees:" "shipped.write(data)" \
           "path.with_suffix(suffix).write_bytes(data)" "except OSError as error:" \
           ".pio/build/native/program\")], cwd=ROOT, capture_output=True)" \
           "if (len(data), hashlib.sha256(data).hexdigest()) != (want.get(\"bytes\"), want.get(\"sha256\")):" \
           "p = subprocess.Popen(cmd, cwd=tree, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=wenv)"; do
    printf "%s  <=  %s\n" "$(grep -cF -- "$a" tools/probe_ui_model_mutations.py)" "$a"; done'
s "transcript symbols and output (DEFAULT_DEFS/default_flags/build_and_run/MR0C_ADAPTERS_HEADER/mr0c_adapters/MR0C-*/transcript.py/transcript_main)" \
  grep -rn -e 'DEFAULT_DEFS\|default_flags\|build_and_run\|MR0C_ADAPTERS_HEADER\|mr0c_adapters\|MR0C-PROFILE\|MR0C-TRANSCRIPT\|MR0C-REFUSED\|transcript\.py\|transcript_main' lib src test tools
s "the transcript controls' anchors (must be 1 each)" bash -c '
  printf "%s  <=  SITE_POINTER\n" "$(grep -cF "static void mr0c_serial_tail(const char (&line)[%(extent)s], size_t pos) {" tools/probe_inbox_verbs/transcript.py)"
  printf "%s  <=  SITE_LIVE\n" "$(grep -cF "install_live_name_pos(seed_id(\"transcript\", 10, 0x20));" tools/probe_inbox_verbs/transcript_main.cpp)"
  printf "%s  <=  SITE_BLE_INCLUDE\n" "$(grep -cF "#include \"device_ble.h\"         // the real BLE line store" tools/probe_inbox_verbs/transcript_main.cpp)"'
s "builder-prefix consumers of run.sh's boundary" grep -rn 'rc=0\\nif ! build_support' tools
