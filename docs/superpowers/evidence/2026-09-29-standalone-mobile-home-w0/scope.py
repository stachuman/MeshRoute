# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# W0 — preflight (brief §1) and the input/scope snapshot for input stability and the freeze (§4.1, §5). Reads only.
# Usage: python3 -B scope.py <preflight|final> <out.json>
import hashlib, json, os, subprocess, sys
R = '/home/staszek/MeshRoute'; SIM = '/home/staszek/lora-universal-simulator'
PC = R + '/docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w0-precheck'
PHASE, OUT = sys.argv[1], sys.argv[2]
HEAD = '0f23aee8f5eeaa6e176e3ea37b2acaffb33622ec'; SIM_HEAD = '6585649ea5a780f0542b2931853a667be56a5b2b'
BRIEF = 'docs/superpowers/plans/2026-09-29-standalone-mobile-home-w0-identity-record.md'
BRIEF_SHA = '2197165149e58a25b3eeccfc88906e652e69641734d8a532a123ecc42e56a6d8'
FENCE = {  # brief §1 executable inputs at the base: sha256, lines
 'src/device_nv.h': ('5c0e4352ce99460146074a70151f94dde58a26e67ecc98142bbe004d4c9ce79c', 1590),
 'src/firmware_config.h': ('32cabbba4e56706a6abff945507f378278d39c067f4c4dcdba64050fde97015e', 237),
 'src/firmware_config.cpp': ('a104f0c87ecefb450f051a383b56b4c6e7195135d39399a2c6cc64aae2b32f2a', 2436),
 'src/firmware_commands.cpp': ('ce1113baa75ecc991a9c8219862334fc6354155b99fa06f65d10f961a72fe128', 1915),
 'test/test_device_nv.cpp': ('93865b037b534addf7f7bfcbc084d94372a30bbf91cbe70205bf26b1085d1212', 930),
 'tools/probe_inbox_verbs/run.sh': ('098fc041537e0fa64aaa3cfb5bea04066428737368071a8a9dd133e574648e61', 933),
 'tools/probe_inbox_verbs/probe_main.cpp': ('263dc475a6aed2309dc4d1fee2aeb4761c2dc6cd10f5a072340fb718b6b765f1', 2432),
 'tools/probe_ui_model_mutations.py': ('8dfc782a8e81acea1c52bf70299747665f15bea20643dbf272aa87a95edad5f1', 12894),
 'docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md': ('ca393c0d18451a252dd58c62db8a679793e2ec15cff1284f15a93334c93f6741', 259)}
NEW_FENCE = ('tools/probe_inbox_verbs/identity_main.cpp', 'tools/probe_inbox_verbs/identity_platform.h')
READONLY = {
 'tools/probe_deferred_actions/run.py': ('694387bd6fe1baa1671ff23e1fc0e2002eb844b6af7dd2538ab8de24dfb3b55f', 190),
 'tools/probe_inbox_verbs/transcript_main.cpp': ('3914de10acddb6a6e39384fa4f512461133e1d7ca538f50cb66939a8cd828163', 148),
 'tools/probe_deferred_actions/probe.cpp': ('57364be0188f908c62f1a4659c7625def338bf3b6cc9bc09865a2ad7a44ae587', 222),
 'tools/probe_console_sink/structural.py': ('753c2477e256a49a220565cddf419103faefeb1dbfbea61b1e55342368d36c19', 908),
 'tools/probe_board_ui/run.sh': ('366a8bf089604c5391b3933b284561c33b6ce5e05cdf5a9e0c8255210beea5bf', 1665),
 'tools/probe_board_ui/expected.tsv': ('2a30e4d1f10683a17b46eccbdd1b31de4ac7252a7491d9ff60e2d202edcb2196', 598),
 'tools/gen_command_inventory.py': ('b9d141a4b6a560a3f4bfd0af68bbdb2c461fcd38a3b67eb5ac04fb206f3db68a', 1186)}
PREP = {
 'docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md': 'da85f228c03d246cc50473caa1853a05bac778ab733d428da6e86f4838f166f0',
 'docs/2026-07-30-open-bug-register.md': '88fe7cea2aabb382dc3a50da32911957db2ab28f9400e4b243b00ab905834b86',
 'docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w0-precheck.md': 'a10f1186cfb595ea42f05b84efeac6c0717945e1ee18c80f526f45237fa40942',
 'docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w0-precheck/SHA256SUMS': '88fed7716cfe8d130d339d5e0307b275ffc5b8ca0bc32b547b1f0ed690b916c1',
 'tracker.md': 'ee6ae9daee59901a29b7c17378f61bcabe3ca2aa6a38b1f08733bc8f88e59669',
 'MEMORY.md': '242610cc1bca53d02e73e861f15b2eb7b9774fff892979cb33dc0f2db057f229'}
EXPLAINED_NEW = ('docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w0-precheck', BRIEF,
                 'docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w0-brief-review',
                 'docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w0-brief-rereview',
                 'docs/superpowers/evidence/2026-09-30-standalone-mobile-home-w0-brief-rereview-2')
W0_OUT = ('docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w0.md', 'docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w0/')
def sha(root, rel):
    return hashlib.sha256(open(os.path.join(root, rel), 'rb').read()).hexdigest()
def lines(rel): return open(os.path.join(R, rel), 'rb').read().count(b'\n')
git = lambda root, *a: subprocess.check_output(['git', *a], cwd=root, text=True)
bad, out = [], {'phase': PHASE}
out['head'] = git(R, 'rev-parse', 'HEAD').strip()
out['simulator'] = {'head': git(SIM, 'rev-parse', 'HEAD').strip(), 'status': git(SIM, 'status', '--porcelain')}
out['staged'] = git(R, 'diff', '--cached', '--name-only')
dc = subprocess.run(['git', 'diff', '--check'], cwd=R, capture_output=True, text=True); out['git_diff_check'] = {'exit': dc.returncode, 'out': dc.stdout[:2000]}
if out['head'] != HEAD: bad.append('HEAD')
if out['simulator']['head'] != SIM_HEAD or out['simulator']['status'].strip(): bad.append('simulator')
if out['staged'].strip(): bad.append('staged')
if dc.returncode: bad.append('git diff --check')
out['brief_sha256'] = sha(R, BRIEF)
if out['brief_sha256'] != BRIEF_SHA: bad.append('brief hash')
out['fence'] = {p: {'sha256': sha(R, p), 'lines': lines(p), 'base': sha(R, p) == h and lines(p) == n} for p, (h, n) in FENCE.items()}
out['new_fence'] = {p: ({'sha256': sha(R, p), 'lines': lines(p)} if os.path.exists(os.path.join(R, p)) else None) for p in NEW_FENCE}
if PHASE == 'preflight':
    for p, v in out['fence'].items():
        if not v['base']: bad.append('fence input ' + p)
    for p, v in out['new_fence'].items():
        if v is not None: bad.append('new path already present ' + p)
out['readonly'] = {p: {'sha256': sha(R, p), 'lines': lines(p)} for p in READONLY}
for p, (h, n) in READONLY.items():
    if out['readonly'][p]['sha256'] != h or out['readonly'][p]['lines'] != n: bad.append('read-only ' + p)
out['preparation'] = {p: sha(R, p) for p in PREP}
for p, h in PREP.items():
    if out['preparation'][p] != h: bad.append('preparation ' + p)
c = subprocess.run(['sha256sum', '-c', 'SHA256SUMS'], cwd=PC, capture_output=True, text=True)
out['precheck_sums'] = {'exit': c.returncode, 'ok': c.stdout.count(': OK')}
if c.returncode or out['precheck_sums']['ok'] != 36: bad.append('pre-check SHA256SUMS')
inv = json.load(open(PC + '/inputs.json'))
out['inventory'] = {}
for key, root in (('meshroute', R), ('simulator', SIM)):
    files = inv[key]['files']; changed, missing = [], []
    for rel, meta in files.items():
        full = os.path.join(root, rel)
        if 'gitlink' in meta:
            cur = subprocess.run(['git', 'ls-files', '-s', '--', rel], cwd=root, capture_output=True, text=True).stdout.rstrip('\n')
            if cur != meta['gitlink']: changed.append(rel)
            continue
        if not os.path.isfile(full): missing.append(rel); continue
        if hashlib.sha256(open(full, 'rb').read()).hexdigest() != meta['sha256']: changed.append(rel)
    cur = subprocess.run(['git', 'ls-files', '-co', '--exclude-standard', '-z'], cwd=root, capture_output=True).stdout.split(b'\0')
    cur = sorted(os.fsdecode(x) for x in cur if x)
    new = [x for x in cur if x not in files]
    allowed_changed = set(PREP) | (set(FENCE) if PHASE == 'final' and key == 'meshroute' else set())
    allowed_new = EXPLAINED_NEW + W0_OUT + (NEW_FENCE if PHASE == 'final' else ())
    unexp_changed = [x for x in changed if x not in allowed_changed]
    unexp_new = [n for n in new if not n.startswith(allowed_new)]
    if unexp_changed or unexp_new or missing: bad.append('inventory ' + key)
    out['inventory'][key] = {'inventoried': len(files), 'git_visible_now': len(cur), 'changed': sorted(changed),
                             'unexplained_changed': unexp_changed, 'missing': missing, 'new': new, 'unexplained_new': unexp_new,
                             'new_hashes': {n: sha(root, n) for n in new if os.path.isfile(os.path.join(root, n))}}
out['verdict'] = 'PASS' if not bad else 'FAIL'; out['failures'] = bad
json.dump(out, open(OUT, 'w'), indent=1, sort_keys=True)
m, s = out['inventory']['meshroute'], out['inventory']['simulator']
print(f"[{PHASE}] HEAD {out['head'][:7]}; simulator {out['simulator']['head'][:7]} {'clean' if not out['simulator']['status'].strip() else 'DIRTY'}; staged {len(out['staged'].split())}; diff --check {dc.returncode}; brief {'OK' if out['brief_sha256'] == BRIEF_SHA else 'MISMATCH'}")
print(f"  fence at base: {sum(v['base'] for v in out['fence'].values())}/{len(FENCE)}; new fence paths present: {[p for p, v in out['new_fence'].items() if v]}; read-only {len(READONLY) - sum('read-only' in b for b in bad)}/{len(READONLY)}; preparation {sum(out['preparation'][p] == h for p, h in PREP.items())}/{len(PREP)}; pre-check SUMS {out['precheck_sums']['ok']} OK")
print(f"  meshroute: inventoried {m['inventoried']}, changed {len(m['changed'])}, unexplained {m['unexplained_changed']}, missing {m['missing']}; new {len(m['new'])}, unexplained {m['unexplained_new']}")
print(f"  simulator: inventoried {s['inventoried']}, changed {len(s['changed'])}, missing {len(s['missing'])}, new {len(s['new'])}")
print('SCOPE', out['verdict'], bad)
