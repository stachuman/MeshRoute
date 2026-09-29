# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# W6 — preflight (brief §1) and the input/scope snapshot for input stability and the freeze (§4.1, §5). Reads only.
# Usage: python3 scope.py <preflight|final> <out.json>
import hashlib, json, os, subprocess, sys
R = '/home/staszek/MeshRoute'; SIM = '/home/staszek/lora-universal-simulator'
PC = R + '/docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w6-precheck'
PHASE, OUT = sys.argv[1], sys.argv[2]
BRIEF = 'docs/superpowers/plans/2026-09-29-standalone-mobile-home-w6-phrases.md'
BRIEF_SHA = 'cdcef1ee1826c14a8e941e05ef4b4bfdb8e7004926cfd22f7e2a44025716828e'
FENCE = {  # brief §1 executable inputs at the base: sha256, lines
 'src/device_nv.h': ('47ca6053e56e4268c78b1044dbadad8e7d3eee2baf5258822cdf7df7912eac54', 1564),
 'src/firmware_ui_presets.h': ('7f0c2bbacf4e203229a2f15dfadecdf49f0973698d18fd99fd90bf19907827a3', 705),
 'src/firmware_ui_preset_verbs.h': ('2c003d4d29ed592cfd19d576a582a7edea68406f0dcd78f863beef2f1ac329dd', 394),
 'src/firmware_ui_model.h': ('ddf0025148a7723b24b31e0893ee7017506ce2d14b2c17c2e381fd0b76e3e286', 6212),
 'src/firmware_ui_send.h': ('0a12ffc21cef8c7d55abf06add55bcac1bf6da2f436da0fe3652d418eb0ce44d', 699),
 'src/firmware_ui.cpp': ('3d32d7cb9c81f39b7188861c973b8213261783810dc6ca8428c0fe305b9e9fe9', 2873),
 'src/firmware_commands.cpp': ('3eea954914b6b3d788dbe9c672972578da15144bd8f6944ae65d3367ef13893f', 1915),
 'test/test_device_nv.cpp': ('93865b037b534addf7f7bfcbc084d94372a30bbf91cbe70205bf26b1085d1212', 930),
 'test/test_firmware_ui_presets.cpp': ('d0f321aec4b96db54feb4d0dcb74d2eccf7180db3da493068d713de5677f49dd', 904),
 'test/test_firmware_ui_preset_verbs.cpp': ('a720c95b488aefa9c3ea04f8d9a249a81b67a875746ff91013cd8f5ab56b5b64', 520),
 'test/test_firmware_ui_model.cpp': ('3ac8c6f2457c4d784a0661a531ef467ace82ba55f675c156abf4180a130ff6c7', 11944),
 'test/test_firmware_ui_send.cpp': ('66527ed7c3d858fb8767e522a3fb19da86f225ce81e5c59ce2368ba68adba0dd', 2856),
 'test/test_firmware_ui_chrome.cpp': ('544875a89e6683d2cb50c33591f828d189925376369e60acc9d31f06b098641f', 1378),
 'tools/probe_firmware_ui/run.sh': ('107f243425bd7922241550d6fd47bc637ff02e913df0494965288de4c925fdcd', 2037),
 'tools/probe_firmware_ui/probe_main.cpp': ('fb45d28e69b0e7ae280c77d91840df659ade5d9f4784837f6317d84793aff9d3', 7691),
 'tools/probe_console_sink/run.sh': ('f543dbf1829bd09d2e672bdca1f32456b358cb0b5addc507a96f920061588b73', 457),
 'tools/probe_console_sink/probe_main.cpp': ('f087651e66daaaca4238042438d60371b79a4817d4659397fd96345efaaa9eb4', 556),
 'tools/test_probe_console_sink.py': ('4a3c3eae62dbdbe982f226c44777b974008abcf5920628ead907b8cdc2a90a1f', 376),
 'tools/probe_inbox_verbs/run.sh': ('06d49b25e3126970ac30e9e279d844a433f3b2f1fd1c994ebad762f43fcf76c4', 873),
 'tools/probe_inbox_verbs/probe_main.cpp': ('e6f6ef9f13431b7bbb2560dd4eeea51f84acb5526977ec0b4d9c8b55327bbab6', 2116),
 'tools/probe_inbox_verbs/fakes/Preferences.h': ('67e329fb4f21a90e2c603002e4a6d206d07c7e7f4427d0496ba32c67f382a68b', 127),
 'tools/probe_board_abi.py': ('4a5c0997fff20fb3c04cfd75c2527fd09f0b55edfb9b3f273c3afc99f0b2a579', 1069),
 'tools/test_probe_board_abi.py': ('c6630098421aed5fc2ad54ea29f512e624ed6ba1f9d19b188a0b1632707951c3', 524),
 'tools/probe_ui_model_mutations.py': ('81842cc53d806f1ed4dbce5bb7699a945e50dc6381f8258cf3c60eec73e2781a', 12699),
 'ios-companion/INBOX_SYNC_CONTRACT.md': ('7528854944693b3ff5d3db0735cc6a69f3220c9a737aaa985fab0d83faa7fd82', 1387),
 'docs/manual/command-reference.md': ('b438fdcdd203e272ca12ac41c57ec8c5f5a47b0e02ee0f7915c64b6d654f9ecf', 571)}
READONLY_RERUN = {'tools/probe_deferred_actions/run.py': ('694387bd', 'b55f', 190), 'tools/probe_deferred_actions/probe.cpp': ('57364be0', 'e587', 222)}
PREP = {'docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md': 'd6ad9a12443eb545cb4db181d5b19fcd35f7d02840f47aa87b20ee5ee5d34bca',
        'docs/2026-07-30-open-bug-register.md': 'b78da31ba37ad694f1817265829161fce177d14fc34f2d47fd3746a0bd0a1e85',
        'docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w6-precheck.md': '3b7305a06b699e6188a531b0395b767a0ac7aa0ccf8178bb4879ac87d94831e8',
        'docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w6-precheck/SHA256SUMS': 'dfd4b52d1e28717e80d7fb1f707ad4c6115a5b50551ba0adf50afc091023b039',
        'tracker.md': 'b9472fd2a1e421f28d6dad626bcac3743a17a315bae52aeaed238e71d0d07e21',
        'MEMORY.md': 'b90794dab03a4f8c5073b145a69557d884892878564cf12ae96140fb58834d0b'}
EXPLAINED_NEW = ('docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w6-precheck', BRIEF,
                 'docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w6-brief-review',
                 'docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w6-brief-rereview')
W6_OUT = ('docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w6.md', 'docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w6/')
def sha(root, rel):
    p = os.path.join(root, rel)
    if os.path.islink(p): return hashlib.sha256(os.fsencode(os.readlink(p))).hexdigest()
    return hashlib.sha256(open(p, 'rb').read()).hexdigest()
def lines(rel): return open(os.path.join(R, rel), 'rb').read().count(b'\n')
bad, out = [], {'phase': PHASE}
git = lambda root, *a: subprocess.check_output(['git', *a], cwd=root, text=True)
out['head'] = git(R, 'rev-parse', 'HEAD').strip(); out['simulator'] = {'head': git(SIM, 'rev-parse', 'HEAD').strip(), 'status': git(SIM, 'status', '--porcelain')}
out['staged'] = git(R, 'diff', '--cached', '--name-only')
dc = subprocess.run(['git', 'diff', '--check'], cwd=R, capture_output=True, text=True); out['git_diff_check'] = {'exit': dc.returncode, 'out': dc.stdout[:2000]}
if out['head'] != '70ff486b40c9b07001b33bcbcdf640ad494c1149': bad.append('HEAD')
if out['simulator']['head'] != '6585649ea5a780f0542b2931853a667be56a5b2b' or out['simulator']['status'].strip(): bad.append('simulator')
if out['staged'].strip(): bad.append('staged')
if dc.returncode: bad.append('git diff --check')
out['brief_sha256'] = sha(R, BRIEF)
if out['brief_sha256'] != BRIEF_SHA: bad.append('brief hash')
out['fence'] = {p: {'sha256': sha(R, p), 'lines': lines(p), 'base': sha(R, p) == h} for p, (h, n) in FENCE.items()}
if PHASE == 'preflight':
    for p, (h, n) in FENCE.items():
        if sha(R, p) != h or lines(p) != n: bad.append('fence input ' + p)
out['readonly_rerun'] = {p: {'sha256': sha(R, p), 'lines': lines(p)} for p in READONLY_RERUN}
for p, (a, z, n) in READONLY_RERUN.items():
    h = out['readonly_rerun'][p]['sha256']
    if not (h.startswith(a) and h.endswith(z)) or lines(p) != n: bad.append('read-only ' + p)
out['preparation'] = {p: sha(R, p) for p in PREP}
for p, h in PREP.items():
    if out['preparation'][p] != h: bad.append('preparation ' + p)
c = subprocess.run(['sha256sum', '-c', 'SHA256SUMS'], cwd=PC, capture_output=True, text=True)
out['precheck_sums'] = {'exit': c.returncode, 'ok': c.stdout.count(': OK')}
if c.returncode or out['precheck_sums']['ok'] != 45: bad.append('pre-check SHA256SUMS')
inv = json.load(open(PC + '/inputs.json'))
out['inventory'] = {}
for key, root in (('meshroute', R), ('simulator', SIM)):
    files = inv[key]['files']; changed, missing = [], []
    for rel, meta in files.items():
        full = os.path.join(root, rel)
        if meta['kind'] == 'symlink':
            if not os.path.islink(full): missing.append(rel); continue
            if hashlib.sha256(os.fsencode(os.readlink(full))).hexdigest() != meta['sha256']: changed.append(rel)
            continue
        if not os.path.isfile(full): missing.append(rel); continue
        if hashlib.sha256(open(full, 'rb').read()).hexdigest() != meta['sha256']: changed.append(rel)
    cur = subprocess.run(['git', 'ls-files', '-co', '--exclude-standard', '-z'], cwd=root, capture_output=True).stdout.split(b'\0')
    cur = sorted(os.fsdecode(x) for x in cur if x)
    new = [x for x in cur if x not in files]
    allowed_changed = set(PREP) | (set(FENCE) if PHASE == 'final' and key == 'meshroute' else set())
    unexp_changed = [x for x in changed if x not in allowed_changed]
    unexp_new = [n for n in new if not n.startswith(EXPLAINED_NEW + W6_OUT)]
    if unexp_changed or unexp_new or missing: bad.append('inventory ' + key)
    out['inventory'][key] = {'inventoried': len(files), 'git_visible_now': len(cur), 'changed': sorted(changed),
                             'unexplained_changed': unexp_changed, 'missing': missing, 'new': new, 'unexplained_new': unexp_new,
                             'new_hashes': {n: sha(root, n) for n in new}}
out['verdict'] = 'PASS' if not bad else 'FAIL'; out['failures'] = bad
json.dump(out, open(OUT, 'w'), indent=1, sort_keys=True)
m, s = out['inventory']['meshroute'], out['inventory']['simulator']
print(f"[{PHASE}] HEAD {out['head'][:7]}; simulator {out['simulator']['head'][:7]} {'clean' if not out['simulator']['status'].strip() else 'DIRTY'}; staged {len(out['staged'].split())}; diff --check {dc.returncode}; brief {'OK' if out['brief_sha256'] == BRIEF_SHA else 'MISMATCH'}")
print(f"  fence at base: {sum(v['base'] for v in out['fence'].values())}/{len(FENCE)}; read-only rerun inputs {len(READONLY_RERUN) - sum('read-only' in b for b in bad)}/{len(READONLY_RERUN)}; preparation {sum(out['preparation'][p] == h for p, h in PREP.items())}/{len(PREP)}; pre-check SUMS {out['precheck_sums']['ok']} OK")
print(f"  meshroute: inventoried {m['inventoried']}, changed {len(m['changed'])}, unexplained {m['unexplained_changed']}, missing {m['missing']}; new {len(m['new'])}, unexplained {m['unexplained_new']}")
print(f"  simulator: inventoried {s['inventoried']}, changed {len(s['changed'])}, missing {len(s['missing'])}, new {len(s['new'])}")
print('SCOPE', out['verdict'], bad)
