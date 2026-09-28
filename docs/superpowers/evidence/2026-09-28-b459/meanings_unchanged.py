# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# B459 §2.4 — every check, control, predicate, substitution and mutation stays as W2 froze it. BASE (run.sh 0ee0bc90…,
# negctl.py 067e1e1b…) against FINAL:
#   · wiring: W2's own source reader (its evidence `declared.py`, bash-tokenised) — every check's label, file argument,
#     predicate name and every sed script, in order; and every predicate FUNCTION body (`w…() {` blocks) byte-identical;
#   · negctl: MUT_V3 / MUT_V4 by AST — every label, find and replacement identical;
#   · traits: every `trait_control` call line identical; missing traits: the loop list identical;
#   · structural: each call's exact statement identical once the final's leading identity argument is dropped (S3's loop
#     words carry `<identity>|<pattern>` with the same three patterns).
# Usage: python3 meanings_unchanged.py <base run.sh> <base negctl.py> <out.json>
import ast, hashlib, json, re, shlex, subprocess, sys
sys.dont_write_bytecode = True
sys.path.insert(0, '/home/staszek/MeshRoute/docs/superpowers/evidence/2026-09-28-standalone-mobile-home-w2')
from declared import declared_wiring                         # W2's frozen reader, used read-only
R = '/home/staszek/MeshRoute'
BASE_RUN, BASE_NEG, OUT = sys.argv[1:4]
assert hashlib.sha256(open(BASE_RUN, 'rb').read()).hexdigest().startswith('0ee0bc90')
assert hashlib.sha256(open(BASE_NEG, 'rb').read()).hexdigest().startswith('067e1e1b')
b_src = open(BASE_RUN, encoding='utf-8').read().split('\n'); f_src = open(f'{R}/tools/probe_board_ui/run.sh', encoding='utf-8').read().split('\n')
res, bad = {}, []
# wiring checks + controls
bw, fw = declared_wiring(b_src), declared_wiring(f_src)
key = lambda d: (d['id'], d['label'], d['file'], d['pred'], tuple(d['scripts']))
res['wiring'] = {'base_checks': len(bw), 'final_checks': len(fw), 'base_controls': sum(len(d['scripts']) for d in bw),
                 'final_controls': sum(len(d['scripts']) for d in fw), 'identical_in_order': [key(d) for d in bw] == [key(d) for d in fw]}
if not res['wiring']['identical_in_order']: bad.append('wiring declarations differ')
def funcs(src):
    out, i = {}, 0
    while i < len(src):
        m = re.match(r'^(w\d[0-9a-z_]*|fn_body|fn_flat|code_flat|oled_guarded|nsite|cfg_writer_counts_ok|v3_env_code|pd_count|prod_files|pd_naming|sleep_fn_code|env_defs|code_of)\(\) \{', src[i])
        if m:
            j = i
            if not re.search(r'\{.*\}\s*(#.*)?$', src[i]):          # a one-liner may carry a trailing comment
                while src[j] != '}': j += 1
            out[m.group(1)] = '\n'.join(src[i:j + 1]); i = j + 1; continue
        i += 1
    return out
bf, ff = funcs(b_src), funcs(f_src)
diffs = sorted(k for k in set(bf) | set(ff) if bf.get(k) != ff.get(k))
res['predicate_functions'] = {'compared': len(bf), 'differing': diffs}
if diffs: bad.append(f'predicate/helper functions differ: {diffs}')
# negctl
def muts(path):
    t = ast.parse(open(path, encoding='utf-8').read())
    return {n.targets[0].id: [tuple(ast.literal_eval(x) for x in e.elts) for e in n.value.elts]
            for n in t.body if isinstance(n, ast.Assign) and getattr(n.targets[0], 'id', '') in ('MUT_V3', 'MUT_V4')}
bm, fm = muts(BASE_NEG), muts(f'{R}/tools/probe_board_ui/negctl.py')
res['negctl'] = {'v3': len(fm['MUT_V3']), 'v4': len(fm['MUT_V4']), 'identical': bm == fm}
if bm != fm: bad.append('negctl mutation lists differ')
# traits, missing
tr = lambda src: [l for l in src if l.startswith('trait_control "')]
res['traits'] = {'calls': len(tr(f_src)), 'identical': tr(b_src) == tr(f_src)}
if tr(b_src) != tr(f_src): bad.append('trait calls differ')
def loop(src, var):
    i = next(k for k, l in enumerate(src) if l.startswith(f'for {var} in '))
    h = src[i]
    while h.endswith('\\'): i += 1; h = h[:-1] + ' ' + src[i].strip()
    return shlex.split(re.sub(r'^for \w+ in (.*); do$', r'\1', h.strip()))
res['missing'] = {'list': loop(f_src, 'name'), 'identical': loop(b_src, 'name') == loop(f_src, 'name')}
if not res['missing']['identical']: bad.append('missing-trait list differs')
# structural: label + expression per call (the final's first argument is the identity)
def schk_calls(src, final):
    out = []
    for i, l in enumerate(src):
        if re.match(r'^\s*schk "', l):
            j, st = i, l
            while st.endswith('\\'): j += 1; st = st[:-1] + ' ' + src[j].strip()
            st = st.strip()
            if final:                                   # drop the leading identity argument, keep the exact rest
                m = re.match(r'^schk "[^"]*" (.*)$', st); assert m, st; st = 'schk ' + m.group(1)
            out.append(re.sub(r'\s+', ' ', st))
    return out
bs, fs = schk_calls(b_src, False), schk_calls(f_src, True)
res['structural'] = {'sites': len(fs), 'label_and_expression_identical': bs == fs,
                     's2_list_identical': loop(b_src, 'h') == loop(f_src, 'h'), 's6_list_identical': loop(b_src, 'nm') == loop(f_src, 'nm'),
                     's3_patterns': {'base': loop(b_src, 'pat'), 'final': [w.split('|', 1)[1] for w in loop(f_src, 's3')],
                                     'final_ids': [w.split('|', 1)[0] for w in loop(f_src, 's3')]}}
s = res['structural']
if not (s['label_and_expression_identical'] and s['s2_list_identical'] and s['s6_list_identical'] and s['s3_patterns']['base'] == s['s3_patterns']['final']):
    bad.append('structural checks differ')
res['problems'] = bad; res['verdict'] = 'PASS' if not bad else 'FAIL'
json.dump(res, open(OUT, 'w'), indent=1)
print(json.dumps({k: v for k, v in res.items() if k not in ('missing',)}, indent=None)[:1500])
print('MEANINGS', res['verdict'])
