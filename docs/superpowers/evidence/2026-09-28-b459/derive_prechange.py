# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# B459 — THE PRE-CHANGE IDENTITY SET, derived WITHOUT the new census (brief §2.2.1 "coverage preservation"):
#   · shell + negctl layers: the pre-check's independent reconciliation (board-reconciliation.json, verified against its
#     SHA256SUMS) — its rows name every wiring check with its control count, every structural call site with its
#     execution count, every trait, the missing-trait site and every negctl control; the loop sites' element names come
#     from the BASE runner's literal lists (base run.sh pinned by hash);
#   · canvas layer: the BASE probe_main.cpp (pinned by hash), per arm, by TWO methods that must agree — (a) EXECUTED: a
#     scratch copy whose CHK also prints `@@CHK <first token>` is built with the base runner's arm defines and run, and
#     P4a's repeated label becomes P4a/0…/9 by occurrence; (b) STATIC: the arm-preprocessed base text's CHK first tokens,
#     with the P4a loop's literal bound. Totals must reproduce 124 (V3) and 110 (V4).
# The ONLY naming choices this makes are B459's stable keys for loop elements, stated here and in the report:
#   struct/S2/<hook>, struct/S3/<slug> (pattern -> slug table below), struct/S6/<MACRO>, missing/<MACRO>, P4a/<i>.
# Usage: python3 derive_prechange.py <scratch-dir> <out.json> [<write-manifest-path>]
import hashlib, json, os, re, shlex, subprocess, sys
R = '/home/staszek/MeshRoute'; PC = R + '/docs/superpowers/evidence/2026-09-28-b459-precheck'
SCR, OUTJ = sys.argv[1], sys.argv[2]; WRITE = sys.argv[3] if len(sys.argv) > 3 else None
BASE_RUN_SHA = '0ee0bc90df8d8a6e1d5adce73958f2dd32061388c0f3644f70d9e1f88ce56fe8'
BASE_PROBE_SHA = '6cffd422588280136e09b05a1bdf9b602420336c7fba76315953a13d23af07b8'
S3_SLUG = {'draw_frame(s_frame_state': 'draw_frame', 'mrui::begin_frame()': 'begin_frame',
           's_gate.on_page(mrui::next_page(), s_model, s_counters)': 'on_page'}
os.makedirs(SCR, exist_ok=True)
def sha(b): return hashlib.sha256(b).hexdigest()
# ---- inputs, pinned
chk = subprocess.run(['sha256sum', '-c', '--quiet', 'SHA256SUMS'], cwd=PC, capture_output=True, text=True)
assert chk.returncode == 0, 'pre-check SHA256SUMS does not verify'
rows = json.load(open(PC + '/board-reconciliation.json'))['rows']
base_run = open(os.path.join(SCR, 'base_run.sh'), 'rb').read() if os.path.exists(os.path.join(SCR, 'base_run.sh')) else None
assert base_run is not None and sha(base_run) == BASE_RUN_SHA, 'put the base run.sh (0ee0bc90…) at <scratch>/base_run.sh'
base_probe = open(os.path.join(SCR, 'base_probe_main.cpp'), 'rb').read() if os.path.exists(os.path.join(SCR, 'base_probe_main.cpp')) else None
assert base_probe is not None and sha(base_probe) == BASE_PROBE_SHA, 'put the base probe_main.cpp (6cffd422…) at <scratch>/base_probe_main.cpp'
run_lines = base_run.decode().split('\n')
def loop_list(var):
    for i, l in enumerate(run_lines):
        if l.startswith(f'for {var} in '):
            h, j = l, i
            while h.endswith('\\'): j += 1; h = h[:-1] + ' ' + run_lines[j].strip()
            return shlex.split(re.sub(r'^for \w+ in (.*); do$', r'\1', h.strip()))
    raise KeyError(var)
def arm_defs(name):
    i = next(k for k, l in enumerate(run_lines) if l.startswith(f'{name}=('))
    txt = run_lines[i][len(name) + 2:]
    while ')' not in txt: i += 1; txt += ' ' + run_lines[i].strip()
    return shlex.split(txt[:txt.index(')')])
# ---- shell + negctl layers from the pre-check reconciliation
ids = {'trait': [], 'missing': [], 'struct': [], 'wiring': [], 'negctl/v3': [], 'negctl/v4': []}
loops = {'S2': ('h', None), 'S3': ('pat', S3_SLUG), 'S6': ('nm', None)}
for r in rows:
    k = r['kind']
    if k == 'wiring':
        ids['wiring'].append((f"wiring/{r['id']}", 'pass'))
        ids['wiring'] += [(f"wiring/{r['id']}/control/{n}", 'red') for n in range(1, r['declared_controls'] + 1)]
    elif k == 'trait':
        ids['trait'].append((f"trait/{r['id']}", 'red'))
    elif k == 'missing':
        names = loop_list('name'); assert len(names) == r['declared'] == r['executed'], r
        ids['missing'] += [(f'missing/{n}', 'named_compile_error') for n in names]
    elif k == 'structural':
        if r['declared'] == 1:
            ids['struct'].append((f"struct/{r['id']}", 'pass'))
        else:
            var, slug = loops[r['id']]; words = loop_list(var); assert len(words) == r['declared'] == r['executed'], r
            ids['struct'] += [(f"struct/{r['id']}/{slug[w] if slug else w}", 'pass') for w in words]
    elif k in ('negctl-v3', 'negctl-v4'):
        ids['negctl/' + k[-2:]].append((f"negctl/{k[-2:]}/{r['id']}", 'red'))
# ---- canvas: (a) executed, (b) static
probe_src = base_probe.decode()
chk_def = re.search(r'#define CHK\(label, expr\) do \{.*?\} while \(0\)\n', probe_src, re.S).group(0)
traced = probe_src.replace(chk_def, chk_def.replace('const bool ok_ = (expr);',
                                                  'printf("@@CHK %.*s\\n", (int)strcspn((label), " "), (label)); const bool ok_ = (expr);'))
assert traced != probe_src
tdir = os.path.join(SCR, 'traced'); os.makedirs(tdir, exist_ok=True)
open(os.path.join(tdir, 'probe_main.cpp'), 'w').write(traced)
inc = [f'-I{R}/tools/probe_board_ui/fakes', f'-I{R}/variants/heltec_common', f'-I{R}/lib/hal', f'-I{R}/lib/core', f'-I{R}/src']
canvas, static = {}, {}
for arm, defs in (('v3', arm_defs('V3_DEFS')), ('v4', arm_defs('V4_DEFS'))):
    exe = os.path.join(tdir, f'probe_{arm}')
    b = subprocess.run(['g++', '-std=gnu++20', '-fno-exceptions', '-fno-rtti', *defs, *inc,
                        os.path.join(tdir, 'probe_main.cpp'), f'{R}/variants/heltec_common/board_ui.cpp', '-o', exe],
                       capture_output=True, text=True)
    assert b.returncode == 0, b.stderr[-2000:]
    r = subprocess.run([exe], capture_output=True, text=True)
    toks = [l.split(' ', 1)[1] for l in r.stdout.splitlines() if l.startswith('@@CHK ')]
    seen, out = {}, []
    for t in toks:
        k = seen.get(t, 0); seen[t] = k + 1
        out.append(t)
    reps = {t: n for t, n in seen.items() if n > 1}
    assert set(reps) == {'P4a'} and reps['P4a'] == 10, reps
    idx, lst = 0, []
    for t in toks:
        if t == 'P4a': lst.append(f'canvas/{arm}/P4a/{idx}'); idx += 1
        else: lst.append(f'canvas/{arm}/{t}')
    canvas[arm] = {'executed': lst, 'probe_exit': r.returncode, 'tally_line': [l for l in r.stdout.splitlines() if 'board_ui probe:' in l]}
    # (b) static: directives-only preprocessing of the BASE text, CHK first tokens in probe_main.cpp's own lines
    pp = subprocess.run(['g++', '-std=gnu++20', '-E', '-fdirectives-only', *defs, *inc, '-x', 'c++', '-'], input=probe_src,
                        capture_output=True, text=True, cwd=f'{R}/tools/probe_board_ui')
    assert pp.returncode == 0, pp.stderr[-2000:]
    cur, st = None, []
    for line in pp.stdout.split('\n'):
        mk = re.match(r'^# \d+ "([^"]+)"', line)
        if mk: cur = mk.group(1); continue
        if cur != '<stdin>' or line.lstrip().startswith('#'): continue
        for m in re.finditer(r'(?<![A-Za-z0-9_])CHK\s*\(\s*"([^"]*)"', line):
            tok = m.group(1).split(' ', 1)[0]
            loop = re.search(r'for \(int (\w+) = 0; \1 < (\d+); \+\+\1\)\s*CHK\(', line)
            st += [f'canvas/{arm}/{tok}/{k}' for k in range(int(loop.group(2)))] if loop else [f'canvas/{arm}/{tok}']
    static[arm] = st
    assert st == lst, (arm, [x for x in st if x not in lst], [x for x in lst if x not in st])
assert len(canvas['v3']['executed']) == 124 and len(canvas['v4']['executed']) == 110
full = ([(i, 'pass') for i in canvas['v3']['executed']] + [(i, 'pass') for i in canvas['v4']['executed']]
        + ids['trait'] + ids['missing'] + ids['struct'] + ids['wiring'] + ids['negctl/v3'] + ids['negctl/v4'])
names = [i for i, _ in full]
assert len(names) == len(set(names)), 'duplicate identity in the pre-change set'
counts = {'canvas/v3': 124, 'canvas/v4': 110, 'trait': len(ids['trait']), 'missing': len(ids['missing']),
          'struct': len(ids['struct']), 'wiring': sum(1 for i, _ in ids['wiring'] if '/control/' not in i),
          'wiring/control': sum(1 for i, _ in ids['wiring'] if '/control/' in i),
          'negctl/v3': len(ids['negctl/v3']), 'negctl/v4': len(ids['negctl/v4']), 'total': len(full)}
res = {'inputs': {'board_reconciliation_sha256': sha(open(PC + '/board-reconciliation.json', 'rb').read()),
                  'base_run_sha256': BASE_RUN_SHA, 'base_probe_sha256': BASE_PROBE_SHA},
       's3_slug_table': S3_SLUG, 'counts': counts, 'canvas_methods_agree': True,
       'canvas_probe_exits': {a: canvas[a]['probe_exit'] for a in canvas}, 'canvas_tally': {a: canvas[a]['tally_line'] for a in canvas},
       'identities': full}
json.dump(res, open(OUTJ, 'w'), indent=1)
print('pre-change identity counts:', counts)
if WRITE:
    with open(WRITE, 'w') as f:
        f.write('# tools/probe_board_ui/expected.tsv — [[B459]] THE BOARD-UI PROBE\'S IDENTITY MANIFEST.\n'
                '# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>\n'
                '# The expected set of every run: one `<identity><TAB><accepted outcome>` per line; `#` lines are comments.\n'
                '# Adding, retiring or renaming a check or control is an edit HERE, visible in review — the brief that makes the\n'
                '# change must list it. ⛔ A check removed together with its line here is invisible to the census and the run;\n'
                '# only review sees it.\n')
        for i, o in full: f.write(f'{i}\t{o}\n')
    print('manifest written:', WRITE, len(full), 'identities')
