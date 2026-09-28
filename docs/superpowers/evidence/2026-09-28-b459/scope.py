# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# B459 — preflight (brief §1) and the scope/input checks of both gates (§4.1 increment-1 step 1, final step 1),
# against the pre-check's inputs.json (the authority for the whole tree). Reads only.
# Usage: python3 scope.py <preflight|inc1|final> <out.json> [<inc1-freeze.json>]
import hashlib, json, os, subprocess, sys
R = '/home/staszek/MeshRoute'; SIM = '/home/staszek/lora-universal-simulator'
PC = R + '/docs/superpowers/evidence/2026-09-28-b459-precheck'
PHASE, OUT = sys.argv[1], sys.argv[2]
FREEZE = json.load(open(sys.argv[3])) if len(sys.argv) > 3 else None
BRIEF = 'docs/superpowers/plans/2026-09-28-b459-board-ui-accounting.md'
BRIEF_SHA = '443b95ea697353d36ef595975a002c1c1b3335dcb21cbe46886aea500d57de5a'
EXEC = {  # brief §1 executable inputs at the base
    'tools/probe_firmware_ui/run.sh': ('60478a2a541e6a6959f69312f096a5c011ae3ede99b96dda29ff8d0b1d7b5bdc', 2040, 1),
    'tools/test_probe_firmware_ui.py': ('50e517ed19079893d3b39fd0b795b581e528a68d5b494daabd901b132f122b16', 178, 1),
    'tools/probe_board_ui/run.sh': ('0ee0bc90df8d8a6e1d5adce73958f2dd32061388c0f3644f70d9e1f88ce56fe8', 1474, 2),
    'tools/probe_board_ui/probe_main.cpp': ('6cffd422588280136e09b05a1bdf9b602420336c7fba76315953a13d23af07b8', 659, 2),
    'tools/probe_board_ui/negctl.py': ('067e1e1b2e9bc464a4e4677b63c0058038a83568a4f801dd48192ba95917f76f', 375, 2)}
NEW = {'tools/probe_accounting.sh': 1, 'tools/probe_board_ui/expected.tsv': 2,
       'tools/probe_board_ui/accounting.py': 2, 'tools/test_probe_board_ui.py': 2}
PREP = {'docs/2026-07-30-open-bug-register.md': 'ebed97ce326b3a0d2be664d76e756388a5d9756462a12b3dbb8703d78f19bab5',
        'docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md': 'dd64b400cab260cfc7b9edd07c7739a88e71665a1efb0ca8b485f99d83d137a4',
        'docs/superpowers/evidence/2026-09-28-b459-precheck.md': '98d96dc538288145f4ea88b60a0271d02a53dfbeae8d0f8e01edc2c11734c710',
        'docs/superpowers/evidence/2026-09-28-b459-precheck/SHA256SUMS': 'a0eebbdd65cb494e3bc77f8c8e9d9008e919f44c7412c03709db9bddcb26265f',
        'tracker.md': 'ef73543d1f749b529a81751bc391613a6222b9e80f7d584685fa848e5a788276',
        'MEMORY.md': 'a55e27aa416a7ed64a694a8ddbf222a34fcd3eb71188a04dc3ad947b536d278c'}
INV_CHANGED_OK = {'docs/2026-07-30-open-bug-register.md', 'tracker.md', 'MEMORY.md'}   # brief §1 preflight step 4
EXPLAINED_NEW = ('docs/superpowers/evidence/2026-09-28-b459-precheck', BRIEF,
                 'docs/superpowers/evidence/2026-09-28-b459-brief-review', 'docs/superpowers/evidence/2026-09-28-b459-brief-rereview')
B459_OUT = ('docs/superpowers/evidence/2026-09-28-b459.md', 'docs/superpowers/evidence/2026-09-28-b459/')

def sha_file(p):
    return hashlib.sha256(open(p, 'rb').read()).hexdigest()
def sha(root, rel):
    p = os.path.join(root, rel)
    if os.path.islink(p): return hashlib.sha256(os.fsencode(os.readlink(p))).hexdigest()
    return sha_file(p)
def lines(rel): return open(os.path.join(R, rel), 'rb').read().count(b'\n')
bad, out = [], {'phase': PHASE}
head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=R, text=True).strip()
sim = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=SIM, text=True).strip()
simst = subprocess.check_output(['git', 'status', '--porcelain'], cwd=SIM, text=True)
staged = subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=R, text=True)
dcheck = subprocess.run(['git', 'diff', '--check'], cwd=R, capture_output=True, text=True)
out.update(head=head, simulator={'head': sim, 'status': simst}, staged=staged, git_diff_check={'exit': dcheck.returncode, 'out': dcheck.stdout})
if head != '8079e545fb2e1065c2bbc347eb9a66160f6cbf8e': bad.append('HEAD')
if sim != '6585649ea5a780f0542b2931853a667be56a5b2b' or simst.strip(): bad.append('simulator')
if staged.strip(): bad.append('staged files')
if dcheck.returncode != 0: bad.append('git diff --check')
out['brief_sha256'] = sha(R, BRIEF)
if out['brief_sha256'] != BRIEF_SHA: bad.append('brief hash')
# executable inputs: at the base until their increment may change them; increment-1 files frozen in `final`
inc_allowed = {'preflight': 0, 'inc1': 1, 'final': 2}[PHASE]
out['executable'] = {}
for p, (h, n, inc) in EXEC.items():
    cur = sha(R, p); out['executable'][p] = {'sha256': cur, 'lines': lines(p), 'base': cur == h}
    if inc > inc_allowed and cur != h: bad.append(f'{p} changed before its increment')
    if PHASE == 'final' and inc == 1 and FREEZE and cur != FREEZE[p]: bad.append(f'{p} differs from its increment-1 freeze')
out['new_paths'] = {}
for p, inc in NEW.items():
    ex = os.path.exists(os.path.join(R, p))
    out['new_paths'][p] = sha(R, p) if ex else None
    if ex and inc > inc_allowed: bad.append(f'{p} exists before its increment')
    if PHASE == 'final' and inc == 1 and FREEZE and out['new_paths'][p] != FREEZE.get(p): bad.append(f'{p} differs from its increment-1 freeze')
out['preparation'] = {p: sha(R, p) for p in PREP}
for p, h in PREP.items():
    if out['preparation'][p] != h: bad.append('preparation ' + p)
chk = subprocess.run(['sha256sum', '-c', 'SHA256SUMS'], cwd=PC, capture_output=True, text=True)
out['precheck_sums'] = {'exit': chk.returncode, 'ok': chk.stdout.count(': OK')}
if chk.returncode != 0 or out['precheck_sums']['ok'] != 19: bad.append('pre-check SHA256SUMS')
# read-only: recommended-inputs minus the fence and the preparation documents
rec = json.load(open(PC + '/recommended-inputs.json'))['existing_inputs']
ro = {p: v for p, v in rec.items() if p not in EXEC and p not in PREP}
out['readonly'] = {p: {'sha256': sha(R, p), 'match': sha(R, p) == v['sha256']} for p, v in ro.items()}
bad += [f'read-only {p}' for p, v in out['readonly'].items() if not v['match']]
# the whole-tree inventory
inv = json.load(open(PC + '/inputs.json'))
out['inventory'] = {}
fence_now = set(EXEC) if PHASE != 'preflight' else set()
for key, root in (('meshroute', R), ('simulator', SIM)):
    files = inv[key]['files']; changed, missing = [], []
    for rel, meta in files.items():
        full = os.path.join(root, rel)
        if meta['type'] == 'symlink':
            if not os.path.islink(full): missing.append(rel); continue
            if hashlib.sha256(os.fsencode(os.readlink(full))).hexdigest() != meta['sha256']: changed.append(rel)
            continue
        if not os.path.isfile(full): missing.append(rel); continue
        if sha_file(full) != meta['sha256']: changed.append(rel)
    cur = subprocess.run(['git', 'ls-files', '-co', '--exclude-standard', '-z'], cwd=root, capture_output=True).stdout.split(b'\0')
    cur = sorted(os.fsdecode(c) for c in cur if c)
    new = [c for c in cur if c not in files]
    allowed_changed = INV_CHANGED_OK | ({p for p, v in EXEC.items() if v[2] <= inc_allowed} if key == 'meshroute' else set())
    unexp_changed = [c for c in changed if c not in allowed_changed]
    allowed_new = EXPLAINED_NEW + B459_OUT + tuple(p for p, inc in NEW.items() if inc <= inc_allowed)
    unexp_new = [n for n in new if not n.startswith(allowed_new)]
    if unexp_changed or unexp_new or missing: bad.append('inventory ' + key)
    out['inventory'][key] = {'inventoried': len(files), 'git_visible_now': len(cur), 'changed': sorted(changed),
                             'unexplained_changed': unexp_changed, 'missing': missing, 'new': new,
                             'unexplained_new': unexp_new, 'new_hashes': {n: sha(root, n) for n in new}}
out['verdict'] = 'PASS' if not bad else 'FAIL'; out['failures'] = bad
json.dump(out, open(OUT, 'w'), indent=1, sort_keys=True)
m = out['inventory']['meshroute']; s = out['inventory']['simulator']
print(f"[{PHASE}] HEAD {head[:7]}; simulator {sim[:7]} {'clean' if not simst.strip() else 'DIRTY'}; staged {len(staged.split())}; git diff --check exit {dcheck.returncode}; brief {'OK' if out['brief_sha256'] == BRIEF_SHA else 'MISMATCH'}")
for p, v in out['executable'].items(): print(f"  exec {p}: {'base' if v['base'] else 'CHANGED'} ({v['lines']} lines) {v['sha256'][:16]}")
for p, v in out['new_paths'].items(): print(f"  new  {p}: {'absent' if v is None else v[:16]}")
print(f"  preparation {sum(out['preparation'][p] == h for p, h in PREP.items())}/{len(PREP)}; pre-check SHA256SUMS {out['precheck_sums']['ok']} OK; read-only {sum(v['match'] for v in out['readonly'].values())}/{len(ro)}")
print(f"  meshroute: inventoried {m['inventoried']}, changed {len(m['changed'])} {m['changed']}, unexplained {m['unexplained_changed']}, missing {m['missing']}; new {len(m['new'])}, unexplained {m['unexplained_new']}")
print(f"  simulator: inventoried {s['inventoried']}, changed {len(s['changed'])}, missing {len(s['missing'])}, new {len(s['new'])}")
print('SCOPE', out['verdict'], bad)
