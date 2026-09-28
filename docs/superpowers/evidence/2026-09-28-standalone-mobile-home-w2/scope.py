# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# W2 §4.1 step 1 (scope and hygiene) + the input-stability snapshot. Reads only.
# Usage: python3 scope.py <snapshot-out.json>
import json, hashlib, os, subprocess, sys
R = '/home/staszek/MeshRoute'; SIM = '/home/staszek/lora-universal-simulator'
P = R + '/docs/superpowers/evidence/2026-09-28-standalone-mobile-home-w2-precheck'
W2_REPORT = 'docs/superpowers/evidence/2026-09-28-standalone-mobile-home-w2.md'
W2_EVID = 'docs/superpowers/evidence/2026-09-28-standalone-mobile-home-w2/'
BRIEF = 'docs/superpowers/plans/2026-09-28-standalone-mobile-home-w2-board-ui-readers.md'
BRIEF_SHA = '341d4bc0c401c8fd254107eea90915cefb2dee94580cdcaf912a99f015c89737'
FENCE = ['tools/probe_board_ui/run.sh', 'tools/probe_board_ui/fakes/Arduino.h',
         'tools/probe_inbox_verbs/transcript_main.cpp', 'tools/probe_ui_model_mutations.py']
PREP = {'docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md': 'a7b0515e633682bdb246f64b46dd345ca9b021764e078a10863a796d92796428',
        'docs/2026-07-30-open-bug-register.md': '56edcd1addf15879021a133b4726e2acf82aa9e483ca1cd4714e45ec7b7e5986',
        'docs/superpowers/evidence/2026-09-28-standalone-mobile-home-w2-precheck.md': 'e235319a7a4d4368a863a2b3e6645d50d51c9f0489fd8800a42c4b91dd651a24',
        'docs/superpowers/evidence/2026-09-28-standalone-mobile-home-w2-precheck/SHA256SUMS': '38d5a4821073a532e094e7798c98aca90e9eb2333edb32488f85cb5f95161169',
        'tracker.md': 'a40d074d643c6d1be6820a6a5da88ca8bd7e92743c819aeaaea8d6993f923f5c',
        'MEMORY.md': 'cb10bdcde351184ea91d7a0640d8d7312b929cbd4a20927e4ba2171f2793b33b'}
EXPLAINED_NEW_PREFIXES = ('docs/superpowers/evidence/2026-09-28-standalone-mobile-home-w2-precheck',
                          'docs/superpowers/evidence/2026-09-28-standalone-mobile-home-w2-brief-review',
                          'docs/superpowers/evidence/2026-09-28-standalone-mobile-home-w2-brief-rereview',
                          BRIEF, W2_REPORT, W2_EVID)
def sha(root, rel):
    p = os.path.join(root, rel)
    if os.path.islink(p): return hashlib.sha256(os.fsencode(os.readlink(p))).hexdigest()
    return hashlib.sha256(open(p, 'rb').read()).hexdigest()
bad = []; out = {}
head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=R, text=True).strip()
sim = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=SIM, text=True).strip()
simst = subprocess.check_output(['git', 'status', '--porcelain'], cwd=SIM, text=True)
dcheck = subprocess.run(['git', 'diff', '--check'], cwd=R, capture_output=True, text=True)
out['head'] = head; out['simulator'] = {'head': sim, 'status_porcelain': simst}
out['git_diff_check'] = {'exit': dcheck.returncode, 'output': dcheck.stdout}
if not head.startswith('8079e54'): bad.append('HEAD')
if not sim.startswith('6585649') or simst.strip(): bad.append('simulator')
if dcheck.returncode != 0: bad.append('git diff --check')
out['brief_sha256'] = sha(R, BRIEF)
if out['brief_sha256'] != BRIEF_SHA: bad.append('brief hash')
out['fence'] = {p: {'sha256': sha(R, p), 'lines': open(os.path.join(R, p), 'rb').read().count(b'\n')} for p in FENCE}
out['preparation'] = {p: sha(R, p) for p in PREP}
for p, h in PREP.items():
    if out['preparation'][p] != h: bad.append('preparation ' + p)
rec = json.load(open(P + '/recommended-inputs.json'))
ro = {}; ro_ok = 0
for d in rec['readonly_dependencies']:
    p = d['path']
    if p in FENCE or p in PREP: continue
    ro[p] = sha(R, p)
    if ro[p] == d['sha256']: ro_ok += 1
    else: bad.append('read-only ' + p)
out['readonly'] = {'matched': ro_ok, 'total': len(ro), 'hashes': ro}
inv = json.load(open(P + '/inputs.json'))
out['inventory'] = {}
for key, root in (('meshroute', R), ('simulator', SIM)):
    tree = inv[key]; files = tree['files']; links = tree.get('symlinks', {})
    changed, missing = [], []
    for rel, h in files.items():
        full = os.path.join(root, rel)
        if not os.path.exists(full): missing.append(rel); continue
        if hashlib.sha256(open(full, 'rb').read()).hexdigest() != h: changed.append(rel)
    for rel, meta in links.items():
        full = os.path.join(root, rel)
        if not os.path.islink(full): missing.append(rel + ' (symlink)'); continue
        if hashlib.sha256(os.fsencode(os.readlink(full))).hexdigest() != meta['sha256_of_link_text']: changed.append(rel + ' (link text)')
    cur = subprocess.run(['git', 'ls-files', '-co', '--exclude-standard', '-z'], cwd=root, capture_output=True).stdout.split(b'\0')
    cur = sorted(os.fsdecode(c) for c in cur if c)
    known = set(files) | set(links)
    new = [c for c in cur if c not in known]
    unexp_changed = [c for c in changed if c not in PREP and c not in FENCE]
    unexp_new = [n for n in new if not n.startswith(EXPLAINED_NEW_PREFIXES)]
    if unexp_changed or unexp_new or missing: bad.append(f'inventory {key}')
    out['inventory'][key] = {'inventoried_files': len(files), 'symlinks': len(links), 'git_visible_now': len(cur),
                             'changed': sorted(changed), 'unexplained_changed': unexp_changed, 'missing': missing,
                             'new': new, 'unexplained_new': unexp_new,
                             'new_hashes': {n: sha(root, n) for n in new}}
out['verdict'] = 'PASS' if not bad else 'FAIL'; out['failures'] = bad
json.dump(out, open(sys.argv[1], 'w'), indent=1, sort_keys=True)
m = out['inventory']['meshroute']
print(f"HEAD {head[:7]}; simulator {sim[:7]} {'clean' if not simst.strip() else 'DIRTY'}; git diff --check exit {dcheck.returncode}; brief {'OK' if out['brief_sha256'] == BRIEF_SHA else 'MISMATCH'}")
print(f"read-only {out['readonly']['matched']}/{out['readonly']['total']}; preparation {sum(out['preparation'][p] == h for p, h in PREP.items())}/{len(PREP)}")
print(f"meshroute changed {len(m['changed'])} (fence {sum(c in FENCE for c in m['changed'])}, preparation {sum(c in PREP for c in m['changed'])}, unexplained {m['unexplained_changed']}); missing {m['missing']}; new {len(m['new'])} (unexplained {m['unexplained_new']})")
s = out['inventory']['simulator']
print(f"simulator changed {len(s['changed'])}, missing {len(s['missing'])}, new {len(s['new'])}")
print('SCOPE', out['verdict'], bad)
