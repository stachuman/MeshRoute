#!/usr/bin/env bash
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# W2 §4.1 step 9 — reader audit (D6/P7): every reader, in lib/ src/ test/ tools/, of the text W2 edited. Reads only.
# Each section prints `grep -rn` hits (evidence dirs excluded — they are receipts, not readers). The disposition of
# every hit (does the edit touch its predicate?) is written by hand in the report, from this output.
cd /home/staszek/MeshRoute || exit 1
D=(lib src test tools)
X=(--exclude-dir=__pycache__ --exclude-dir=.pio --binary-files=without-match)
sec() { echo; echo "=== $1"; }
g() { grep -rnF "${X[@]}" -e "$1" "${D[@]}" | cut -c1-240; echo "   ($(grep -rlF "${X[@]}" -e "$1" "${D[@]}" | wc -l) file(s) for: $1)"; }
sec '1. who reads the board-UI runner (path references)'
g 'probe_board_ui/run.sh'
sec '2. the four check IDs and their labels (old and new text)'
for s in 'W49' 'W51' 'W54' 'W54-help' 'keeps the shared dispatch fallback' 'keeps the ONE shared seam call' \
         'boot restore, handle_ui, status, help)' 'boot restore, handle_ui, status)' "help index's ui row"; do g "$s"; done
sec '3. the anchors the edited controls read (production text they mutate)'
for s in 'bool dispatch(const char* line, size_t len, Print& out, CommandTransport transport) {' \
         'static void handle_teststatus(Print& out) {' 'the `ui` dispatch arm' \
         'const mrfw::LineExec ex = mrfw::exec_console_line(line, len, mrfw::LineFormat::json, ls, out, cap, ctx);' \
         'if (ex.state == mrfw::LineExec::State::streamed) { ls.flush(); return 0; }' \
         'out.println(F("ui"));' 'firmware_help.h'; do g "$s"; done
sec '4. M103 (active tuple retired) and the model cardinality'
g 'M103'
grep -rnE "${X[@]}" -e '\b239\b' tools/*.py tools/**/test_*.py 2>/dev/null | grep -iE 'model|entr' | cut -c1-200
sec '5. the fake fidelity note (Arduino.h) — the old and new comment text, and who hashes the fake'
for s in 'ACCEPTED AND IGNORED' 'Nothing this probe' 'FIDELITY LIMIT' 'probe_board_ui/fakes' 'fakes/Arduino.h'; do g "$s"; done
sec '6. the transcript locator (transcript_main.cpp) — S24/S27 references and who reads the file'
for s in 'transcript_main.cpp' '`structural.py` S24' '`structural.py` S27'; do g "$s"; done
sec '7. the in-run md5_sources lists (inbox-verbs and firmware-UI)'
grep -rn "${X[@]}" -e 'md5_sources' tools/probe_inbox_verbs/run.sh tools/probe_firmware_ui/run.sh | cut -c1-200
for f in tools/probe_inbox_verbs/run.sh tools/probe_firmware_ui/run.sh; do
  echo "--- $f: md5_sources definition"
  awk '/^md5_sources\(\)/{p=1} p{print FILENAME":"FNR": "$0} p && /^}/{exit}' "$f" | cut -c1-200
done
