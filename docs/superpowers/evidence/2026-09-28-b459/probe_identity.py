# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# B459 — probe_main.cpp changed ONLY by record emission and P4a's iteration keys (brief §2.2.2/§3): no assertion,
# expression, fixture, printed FAIL line or tally moved. Per arm, the BASE probe (pinned 6cffd422…) and the CANDIDATE
# probe are built with the same compiler flags as the runner and run:
#   1. on the real canvas, without a record file: stdout and exit byte-identical;
#   2. on the SAME mutated canvas (negctl's own C1 and C7a substitutions, applied to a scratch copy): stdout — every
#      FAIL line, its format and the tally — and exit byte-identical;
#   3. the candidate WITH a record file: stdout still identical to (1), and exactly one record per executed check
#      (124 / 110, P4a keyed /0…/9), every one `pass`.
# Also B461: the runner's diff on the W50 comment is that one comment line, with the line count unchanged there.
# Usage: python3 probe_identity.py <base_probe_main.cpp> <base_run.sh> <scratch> <out.json>
import ast, hashlib, json, os, re, shlex, subprocess, sys
R = '/home/staszek/MeshRoute'; BASE_PROBE, BASE_RUN, SCR, OUT = sys.argv[1:5]
assert hashlib.sha256(open(BASE_PROBE, 'rb').read()).hexdigest().startswith('6cffd422')
os.makedirs(SCR, exist_ok=True)
run_lines = open(BASE_RUN).read().split('\n')
def arm_defs(name):
    i = next(k for k, l in enumerate(run_lines) if l.startswith(f'{name}=('))
    txt = run_lines[i][len(name) + 2:]
    while ')' not in txt: i += 1; txt += ' ' + run_lines[i].strip()
    return shlex.split(txt[:txt.index(')')])
FLAGS = ['-std=gnu++20', '-fno-exceptions', '-fno-rtti', '-Wall', '-Wextra', '-Werror']
INC = [f'-I{R}/tools/probe_board_ui/fakes', f'-I{R}/variants/heltec_common', f'-I{R}/lib/hal', f'-I{R}/lib/core', f'-I{R}/src']
canvas = f'{R}/variants/heltec_common/board_ui.cpp'
negsrc = open(f'{R}/tools/probe_board_ui/negctl.py').read()
muts = {}
for node in ast.parse(negsrc).body:
    if isinstance(node, ast.Assign) and getattr(node.targets[0], 'id', '') == 'MUT_V3':
        for el in node.value.elts:
            lab = ast.literal_eval(el.elts[0]).split(' ', 1)[0]
            if lab in ('C1', 'C7a'): muts[lab] = (ast.literal_eval(el.elts[1]), ast.literal_eval(el.elts[2]))
orig = open(canvas).read()
mut_canvas = os.path.join(SCR, 'board_ui_mutant.cpp')
m = orig
for lab in ('C1', 'C7a'):
    assert m.count(muts[lab][0]) == 1; m = m.replace(muts[lab][0], muts[lab][1], 1)
open(mut_canvas, 'w').write(m)
env0 = {k: v for k, v in os.environ.items() if k != 'MR_BOARD_UI_RECORDS'}
def build_run(probe, cv, arm, defs, tag, records=None):
    exe = os.path.join(SCR, f'{tag}_{arm}')
    b = subprocess.run(['g++', *FLAGS, *defs, *INC, probe, cv, '-o', exe], capture_output=True, text=True)
    assert b.returncode == 0, b.stderr[-1500:]
    env = dict(env0, MR_BOARD_UI_RECORDS=records) if records else env0
    r = subprocess.run([exe], capture_output=True, text=True, env=env)
    return r.returncode, r.stdout
res = {}; ok = True
cand = f'{R}/tools/probe_board_ui/probe_main.cpp'
for arm, defs in (('v3', arm_defs('V3_DEFS')), ('v4', arm_defs('V4_DEFS'))):
    b_rc, b_out = build_run(BASE_PROBE, canvas, arm, defs, 'base')
    c_rc, c_out = build_run(cand, canvas, arm, defs, 'cand')
    bm_rc, bm_out = build_run(BASE_PROBE, mut_canvas, arm, defs, 'base_mut')
    cm_rc, cm_out = build_run(cand, mut_canvas, arm, defs, 'cand_mut')
    recf = os.path.join(SCR, f'records_{arm}.tsv')
    r_rc, r_out = build_run(cand, canvas, arm, defs, 'cand_rec', records=recf)
    recs = [l.split('\t') for l in open(recf).read().splitlines()]
    ids = [x[0] for x in recs]
    fails_m = [l for l in cm_out.splitlines() if l.startswith('  FAIL ')]
    a = {'real_identical': (b_rc, b_out) == (c_rc, c_out), 'real_exit': c_rc,
         'mutant_identical': (bm_rc, bm_out) == (cm_rc, cm_out), 'mutant_exit': cm_rc, 'mutant_fail_lines': len(fails_m),
         'with_records_stdout_identical': (r_rc, r_out) == (c_rc, c_out), 'records': len(recs),
         'records_all_pass': all(x[1] == 'pass' for x in recs), 'records_unique': len(ids) == len(set(ids)),
         'p4a_keys': [i for i in ids if i.startswith('P4a')], 'tally': [l for l in c_out.splitlines() if 'board_ui probe:' in l]}
    ok &= (a['real_identical'] and a['mutant_identical'] and a['with_records_stdout_identical'] and a['records_all_pass']
           and a['records_unique'] and a['records'] == {'v3': 124, 'v4': 110}[arm] and a['mutant_fail_lines'] > 0
           and a['p4a_keys'] == [f'P4a/{k}' for k in range(10)])
    res[arm] = a
    print(f"{arm}: real identical {a['real_identical']} (exit {c_rc}); mutant identical {a['mutant_identical']} (exit {cm_rc}, "
          f"{len(fails_m)} FAIL lines); with records stdout identical {a['with_records_stdout_identical']}; records {len(recs)} "
          f"unique {a['records_unique']} all pass {a['records_all_pass']}; P4a keys {a['p4a_keys'][:2]}…{a['p4a_keys'][-1:]}")
# B461: the W50 comment line
d = subprocess.run(['git', 'diff', '-U0', '--', 'tools/probe_board_ui/run.sh'], cwd=R, capture_output=True, text=True).stdout
minus = [l for l in d.splitlines() if l.startswith('-') and 'Control (c)' in l]
plus = [l for l in d.splitlines() if l.startswith('+') and 'Control 2 is the plausible SIMPLIFICATION' in l]
b461 = {'removed': minus, 'added': plus,
        'only_token_changed': len(minus) == len(plus) == 1 and minus[0][1:].replace('Control (c)', 'Control 2') == plus[0][1:],
        'is_comment': bool(plus) and plus[0][1:].lstrip().startswith('#')}
ok &= b461['only_token_changed'] and b461['is_comment']
res['b461'] = b461
print('B461:', b461['only_token_changed'], b461['is_comment'], minus, plus)
res['verdict'] = 'PASS' if ok else 'FAIL'
json.dump(res, open(OUT, 'w'), indent=1)
print('PROBE IDENTITY', res['verdict'])
