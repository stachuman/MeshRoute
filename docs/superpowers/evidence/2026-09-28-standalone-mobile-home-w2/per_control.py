# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# W2 §4.1 step 4 — PER-CONTROL PROOFS for the 15 re-anchored / replaced / new / formerly dormant controls
# (W49 x6, W51 x4, W54 x4, W54-help x1), plus the W49 relocation's missing-anchor demonstration (W2R-1).
# For each control: the EXACT script the runner declares (read from source by declared.py) is applied to the real
# file in scratch; the proof records the anchor match count, that the mutant differs (with its diff), that the
# runner's OWN predicate is RED on it, and a DECOMPOSED evaluation of the check's clauses showing the §2.6 clause
# failing (for the W49 relocation and W54-help: ONLY that clause). The runner's helpers and predicates are EXTRACTED
# from its source and evaluated as-is — never re-typed — and the clauses are split out on top of them.
# Mutation scratch lives outside both repositories. Usage: python3 per_control.py <scratch-dir> <evidence-dir>
import json, os, re, subprocess, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from declared import declared_wiring
R = '/home/staszek/MeshRoute'; RUN = f'{R}/tools/probe_board_ui/run.sh'
SCR, EVD = sys.argv[1], sys.argv[2]
os.makedirs(SCR, exist_ok=True); os.makedirs(f'{EVD}/mutants', exist_ok=True)
src = open(RUN, encoding='utf-8').read().split('\n')
decl = {d['id']: d for d in declared_wiring(src)}
FILES = {'W49': f'{R}/src/firmware_commands.cpp', 'W51': f'{R}/src/fw_main.cpp',
         'W54': f'{R}/src/firmware_commands.cpp', 'W54-help': f'{R}/src/firmware_help.h'}

def extract(name):
    """The runner's own definition of a function or variable, verbatim."""
    for i, l in enumerate(src):
        if l.startswith(name + '() {'):
            if l.rstrip().endswith('}') and l.count('{') == l.count('}'): return l
            j = i
            while src[j] != '}': j += 1
            return '\n'.join(src[i:j + 1])
        if l.startswith(name + '='): return l
    raise KeyError(name)
PRELUDE = '\n'.join(['ROOT=' + R] + [extract(n) for n in (
    'fn_body', 'fn_flat', 'oled_guarded', 'DISPATCH_SIG', 'BLE_SIG', 'UI_ARM', 'UI_ARM_GUARDED', 'BLE_SEAM',
    'BLE_STREAMED', 'w49', 'w51', 'w54', 'w54_help')])
TEST_SIG = 'static void handle_teststatus(Print& out) {'
# The decomposed clauses — each prints `KEY=0|1` (1 = the clause holds), on top of the runner's own helpers.
CLAUSES = {
 'W49': r'''f=$1
  echo "A_arm_line_once_in_file=$([ "$(grep -cF 'handle_ui(line + 2, len - 2, out); return true;' "$f")" = 1 ] && echo 1 || echo 0)"
  echo "B_guarded_arm_inside_dispatch=$(fn_flat "$f" "$DISPATCH_SIG" | grep -qF "$UI_ARM_GUARDED" && echo 1 || echo 0)"
  echo "info_arm_lines_in_file=$(grep -cF 'handle_ui(line + 2, len - 2, out); return true;' "$f")"
  echo "info_guarded_arms_in_dispatch=$(fn_flat "$f" "$DISPATCH_SIG" | grep -oF "$UI_ARM_GUARDED" | wc -l)"
  echo "info_guarded_arms_in_handle_teststatus=$(fn_flat "$f" "''' + TEST_SIG + r'''" | grep -oF "$UI_ARM_GUARDED" | wc -l)"
''',
 'W51': r'''f=$1; b=$(fn_flat "$f" "$BLE_SIG")
  echo "S_seam_call_once=$([ "$(printf '%s\n' "$b" | grep -oF "$BLE_SEAM" | wc -l)" -eq 1 ] && echo 1 || echo 0)"
  echo "F_streamed_flush_once=$([ "$(printf '%s\n' "$b" | grep -oF "$BLE_STREAMED" | wc -l)" -eq 1 ] && echo 1 || echo 0)"
  echo "N_no_ui_token=$(printf '%s\n' "$b" | grep -qE '"ui|handle_ui|preset_' && echo 0 || echo 1)"
  echo "info_seam_calls=$(printf '%s\n' "$b" | grep -oF "$BLE_SEAM" | wc -l) info_flushes=$(printf '%s\n' "$b" | grep -oF "$BLE_STREAMED" | wc -l) info_ui_tokens=$(printf '%s\n' "$b" | grep -oE '"ui|handle_ui|preset_' | wc -l)"
''',
 'W54': r'''f=$1; g=$(oled_guarded "$f")
  echo "E_guarded_extraction_nonempty=$([ -n "$g" ] && echo 1 || echo 0)"
  k=0; for token in '#include "firmware_ui_preset_verbs.h"' 'static mrfw::PresetCatalog cat(st, gate);' \
      'void preset_boot_restore_console() {' 'void handle_ui(const char* args, size_t len, Print& out) {' \
      'const mrfw::PresetCatalog& pc = preset_catalog();'; do
    k=$((k+1))
    echo "C${k}_once_in_file=$([ "$(grep -cF "$token" "$f")" = 1 ] && echo 1 || echo 0)"
    echo "G${k}_inside_region=$(printf '%s\n' "$g" | grep -qF "$token" && echo 1 || echo 0)"
  done
''',
 'W54-help': r'''f=$1
  echo "C_ui_row_once_in_file=$([ "$(grep -cF 'out.println(F("ui"));' "$f")" = 1 ] && echo 1 || echo 0)"
  echo "G_ui_row_inside_region=$(oled_guarded "$f" | grep -qF 'out.println(F("ui"));' && echo 1 || echo 0)"
''',
}
W54_TOKENS = {1: 'the include', 2: 'the ONE instance', 3: 'preset_boot_restore_console', 4: 'handle_ui', 5: 'the status reference'}
PRED = {'W49': 'w49', 'W51': 'w51', 'W54': 'w54', 'W54-help': 'w54_help'}
# anchors: ('F', literal, whole_line) = grep -c[x]F line count; ('BLOCK', text) = occurrences in the whole file
BLOCK_ARM = ('#if MR_FEAT_OLED   // ★ [[B255]] the `ui` dispatch arm\n'
             "    if ((len == 2 || (len > 2 && line[2] == ' ')) && !strncmp(line, \"ui\", 2)) { handle_ui(line + 2, len - 2, out); return true; }\n"
             '#endif   // MR_FEAT_OLED\n')
SEAM_LINE = '    ' + 'const mrfw::LineExec ex = mrfw::exec_console_line(line, len, mrfw::LineFormat::json, ls, out, cap, ctx);'
STREAM_LINE = '    ' + 'if (ex.state == mrfw::LineExec::State::streamed) { ls.flush(); return 0; }'
ANCHORS = {
 ('W49', 1): [('F', 'handle_ui(line + 2, len - 2, out); return true;', False)],
 ('W49', 2): [('F', 'handle_ui(line + 2, len - 2, out)', False)],
 ('W49', 3): [('F', '{ handle_ui(line + 2, len - 2, out); return true; }', False)],
 ('W49', 4): [('F', ' && !strncmp(line, "ui", 2))', False)],
 ('W49', 5): [('BLOCK', '\n' + TEST_SIG + '\n'), ('BLOCK', BLOCK_ARM)],
 ('W49', 6): [('F', '#if MR_FEAT_OLED   // ★ [[B255]] the `ui` dispatch arm', True)],
 ('W51', 1): [('F', SEAM_LINE, True)], ('W51', 2): [('F', SEAM_LINE, True)], ('W51', 3): [('F', SEAM_LINE, True)],
 ('W51', 4): [('F', STREAM_LINE, True)],
 ('W54', 1): [('RE', r'^#if MR_FEAT_OLED   // ★ \[\[B255\]\] the include')],
 ('W54', 2): [('RE', r'^#if MR_FEAT_OLED   // ★ \[\[B255\]\] the catalog binding')],
 ('W54', 3): [('RE', r'^#if MR_FEAT_OLED   // ★ \[\[B255\]\] the `status` surface$')],
 ('W54', 4): [('F', '    static mrfw::PresetCatalog cat(st, gate);', False)],
 ('W54-help', 1): [('BLOCK', '\n#if MR_FEAT_OLED\n    out.println(F("ui"));\n#endif   // MR_FEAT_OLED\n')],
}
# §2.6's must-fail clause per control; ONLY = every other clause must still hold
NAMED = {('W49', 1): (['A_arm_line_once_in_file'], False), ('W49', 2): (['B_guarded_arm_inside_dispatch'], False),
         ('W49', 3): (['A_arm_line_once_in_file'], False), ('W49', 4): (['B_guarded_arm_inside_dispatch'], False),
         ('W49', 5): (['B_guarded_arm_inside_dispatch'], True), ('W49', 6): (['B_guarded_arm_inside_dispatch'], False),
         ('W51', 1): (['N_no_ui_token'], False), ('W51', 2): (['N_no_ui_token'], False),
         ('W51', 3): (['S_seam_call_once'], False), ('W51', 4): (['F_streamed_flush_once'], False),
         ('W54', 1): (['G1_inside_region'], False), ('W54', 2): (['G2_inside_region'], False),
         ('W54', 3): (['G5_inside_region'], False), ('W54', 4): (['C2_once_in_file'], False),
         ('W54-help', 1): (['G_ui_row_inside_region'], True)}

def bash(script, *args):
    r = subprocess.run(['bash', '--norc', '-c', PRELUDE + '\n' + script, 'proof', *args], capture_output=True, text=True)
    return r.returncode, r.stdout
def clauses(check, path):
    _, out = bash('main() { ' + CLAUSES[check] + '}\nmain "$1"', path)
    kv = dict(tok.split('=', 1) for line in out.split('\n') for tok in line.split() if '=' in tok)
    return {k: v for k, v in kv.items()}
def pred(check, path):
    rc, _ = bash(PRED[check] + ' "$1"', path)
    return rc == 0
def anchor_count(a, text, path):
    if a[0] == 'BLOCK': return text.count(a[1])
    if a[0] == 'F':
        return int(subprocess.run(['grep', '-c' + ('x' if a[2] else '') + 'F', '--', a[1], path], capture_output=True, text=True).stdout)
    return int(subprocess.run(['grep', '-c', '--', a[1], path], capture_output=True, text=True).stdout)

results, problems = [], []
for check in ('W49', 'W51', 'W54', 'W54-help'):
    real = FILES[check]; text = open(real, encoding='utf-8').read()
    live = clauses(check, real)
    live_ok = pred(check, real)
    if not live_ok or any(v == '0' for k, v in live.items() if not k.startswith('info')): problems.append(f'{check} live not GREEN')
    for n, script in enumerate(decl[check]['scripts'], 1):
        mut = f'{SCR}/{check}-{n}.mut'
        with open(mut, 'wb') as fo: subprocess.run(['sed', script, real], stdout=fo, check=True)
        differs = subprocess.run(['cmp', '-s', real, mut]).returncode != 0
        diff = subprocess.run(['diff', '-u', '--label', 'real/' + os.path.basename(real), '--label', f'mutant/{check}-{n}',
                               real, mut], capture_output=True, text=True).stdout
        open(f'{EVD}/mutants/{check}-{n}.diff', 'w').write(diff)
        hunks = diff.count('\n@@ ') + diff.startswith('@@ ')
        counts = [anchor_count(a, text, real) for a in ANCHORS[(check, n)]]
        red = not pred(check, mut)
        cl = clauses(check, mut)
        named, only = NAMED[(check, n)]
        named_fail = all(cl[k] == '0' for k in named)
        others = {k: v for k, v in cl.items() if not k.startswith('info') and k not in named}
        only_ok = (not only) or all(v == '1' for v in others.values())
        ok = all(c == 1 for c in counts) and differs and red and named_fail and only_ok
        if not ok: problems.append(f'{check}#{n}')
        results.append({'check': check, 'control': n, 'script': script, 'anchor_counts': counts, 'mutant_differs': differs,
                        'diff_hunks': hunks, 'runner_predicate_red': red, 'clauses_on_mutant': cl,
                        'must_fail': named, 'only': only, 'named_clause_fails': named_fail,
                        'other_clauses_hold': all(v == '1' for v in others.values()), 'verdict': 'PASS' if ok else 'FAIL'})

# ---- W2R-1: the W49 relocation with EACH anchor missing, on its own scratch copy
reloc = decl['W49']['scripts'][4]
real = FILES['W49']; text = open(real, encoding='utf-8').read()
missing = {}
for name, old, new in (('destination-signature-renamed', TEST_SIG + '\n', 'static void handle_teststatus_RENAMED(Print& out) {\n'),
                       ('source-marker-comment-changed', '#if MR_FEAT_OLED   // ★ [[B255]] the `ui` dispatch arm\n',
                        '#if MR_FEAT_OLED   // ★ [[B255]] the ui dispatch arm (marker changed, comment only)\n')):
    assert text.count(old) == 1, name
    cp = f'{SCR}/reloc-{name}.cpp'; open(cp, 'w', encoding='utf-8').write(text.replace(old, new, 1))
    out = f'{SCR}/reloc-{name}.out'
    with open(out, 'wb') as fo: subprocess.run(['sed', reloc, cp], stdout=fo, check=True)
    same = subprocess.run(['cmp', '-s', cp, out]).returncode == 0
    green = pred('W49', cp); cl = clauses('W49', cp)
    ok = same and green
    if not ok: problems.append('relocation ' + name)
    missing[name] = {'edit': [old.rstrip('\n'), new.rstrip('\n')], 'control_output_equals_input': same,
                     'wchk_in_would_report': 'CONTROL 5 — the revert changed NOTHING, so the check is vacuous' if same else 'a change',
                     'live_W49_green_on_copy': green, 'clauses_on_copy': cl, 'verdict': 'PASS' if ok else 'FAIL'}
# the retired recipe on the CURRENT file, for the record: deletion-only, so W49 fails on the WRONG clause
old5 = subprocess.check_output(['git', 'show', 'HEAD:tools/probe_board_ui/run.sh'], cwd=R).decode().split('\n')
old5 = [d for d in declared_wiring(old5) if d['id'] == 'W49'][0]['scripts'][4]
om = f'{SCR}/W49-5-retired-recipe.mut'
with open(om, 'wb') as fo: subprocess.run(['sed', old5, real], stdout=fo, check=True)
retired = {'script': old5, 'clauses_on_mutant': clauses('W49', om),
           'note': 'deletion-only on this base (dump_help is gone): the arm count falls to 0, so W49 fails on the presence clause, not placement'}
json.dump({'live': {c: clauses(c, FILES[c]) for c in FILES}, 'controls': results, 'relocation_missing_anchor': missing,
           'retired_W49_5_recipe_on_current_file': retired, 'problems': problems, 'verdict': 'PASS' if not problems else 'FAIL'},
          open(f'{EVD}/step4-per-control.json', 'w'), indent=1, ensure_ascii=False)
for r in results:
    print(f"{r['check']}#{r['control']}: anchors {r['anchor_counts']} differs {r['mutant_differs']} hunks {r['diff_hunks']} RED {r['runner_predicate_red']} "
          f"must-fail {r['must_fail']}{' ONLY' if r['only'] else ''} -> fails {r['named_clause_fails']}, others hold {r['other_clauses_hold']} "
          f"{ {k: v for k, v in r['clauses_on_mutant'].items() if not k.startswith('info')} } {r['verdict']}")
for k, v in missing.items():
    print(f"relocation / {k}: output == input {v['control_output_equals_input']}; live W49 GREEN on copy {v['live_W49_green_on_copy']} -> {v['verdict']}")
print(f"retired W49#5 recipe on the current file: {retired['clauses_on_mutant']}")
print('PER-CONTROL', 'PASS' if not problems else f'FAIL {problems}')
