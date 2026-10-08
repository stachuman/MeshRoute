# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# B478 + B487 + B488 + B490 + B491 + B492 — RETURN preflight (brief r4 §1) and the scope/freeze snapshot (§4.1 steps
# 1/12, §5). Reads only. Usage: python3 -B scope.py <preflight|final> <out.json>
import hashlib, json, os, subprocess, sys
R = '/home/staszek/MeshRoute'; SIM = '/home/staszek/lora-universal-simulator'
EV = 'docs/superpowers/evidence/'
PHASE, OUT = sys.argv[1], sys.argv[2]
BASE = '10f333298ffad1098daeec37e756ed79ab0fd97f'; SIM_HEAD = '6585649ea5a780f0542b2931853a667be56a5b2b'
# The dispatch's starting state: "the round-2 candidate, now committed" — the owner's commit on top of BASE.
COMMITTED = '4c1a000bc71706ac438e64161a9452966a66615f'
BRIEF = 'docs/superpowers/plans/2026-09-30-b478-b487-b488-tool-repairs.md'
BRIEF_SHA = '4e6961662a3f38c039be5c069de8192f2049e8212f27cfd20e1c1fb5bc85c06d'; BRIEF_LINES = 903
FENCE = {  # brief r4 §1 — the CANDIDATE hashes (round 2's working tree): sha256, lines
 'tools/probe_ui_model_mutations.py': ('72a62ae82d8db5528afd508b3ace932a7cb4559f4a1b5a5da5a5a1f5fd41dfe2', 13162),
 'tools/test_mutation_unusable_reason.py': ('7c04519c2f40f11fdac5fc1d55648349dd8ac10fa2237cad9313a72b186fb61d', 689),
 'tools/probe_inbox_verbs/transcript.py': ('4dfba2a428495eca1fdb3c24e23fb96f900b548e7ac77752e46772dbc1a16429', 770),
 'tools/probe_inbox_verbs/transcript_main.cpp': ('e5b5b51c709a5f379908f2f048f558251cf8a2ee57b9b79e8030bbbedf787863', 175),
 'tools/test_probe_inbox_transcript.py': ('1650dcef78a2d95d26bbcb72874443cabce3cfa8a1d79227b171b78c4da7814b', 555)}
READONLY = {
 'tools/test_worker_formula_derived.py': 'fb8704f2d3b0629b577b7c4c9ea0a4544e08f6c0d4c585321ad1f1f801c18176',
 'tools/probe_inbox_verbs/run.sh': 'db7add6c5fb335097bc84a192b925b43604f638f7951e422cd926f7ab6292948',
 'tools/probe_inbox_verbs/probe_main.cpp': '69cc26c59f0be12952421501fb0365e85195ff9343995cd4056068e20f0f7650',
 'tools/probe_inbox_verbs/fakes/Preferences.h': 'bd57ac322eafe89e4e882c35b8bc02b9fda60f4738e11bfe1dacb0699b617346',
 'tools/probe_board_ui/fakes/Arduino.h': 'deaa9f4a6f3d010fa4010722973c701c4f5effadb2442213812b7884f85ab853',
 'tools/probe_deferred_actions/run.py': '694387bd6fe1baa1671ff23e1fc0e2002eb844b6af7dd2538ab8de24dfb3b55f',
 'tools/probe_deferred_actions/probe.cpp': '57364be0188f908c62f1a4659c7625def338bf3b6cc9bc09865a2ad7a44ae587',
 'tools/gen_command_inventory.py': 'b9d141a4b6a560a3f4bfd0af68bbdb2c461fcd38a3b67eb5ac04fb206f3db68a',
 EV + '2026-09-04-radmin-command-inventory.md': 'c320046822c0cb47bb38ca76f4c1ea8ebd42c7f2346bb50791a35e05d74ae807',
 'src/fw_main.cpp': '4b9b86afd3303cec913a343faa159d4305591d31cf185fe8e8dfc27b509f7766',
 'lib/console/console_line.h': '640e4886ef9b0708828d2679cbc993853158475b6d100118bcc08bcf0f7c85a9',
 'src/device_ble.h': '56d68dd9270f670451115854e4861dceab8b25c7d81b1f923f73aa5a3f941ac5',
 'src/firmware_commands.cpp': '48f9b19a9f402585c8f3c061999d4c993ec53cf936b6f6b9a4921e586bc060fd',
 'src/firmware_remote_actions.cpp': '5d112359ee52dc991519dae3fa20ec48d988fa8aafec0341590e18901c2b682e',
 'src/firmware_remote_client.cpp': 'd1156cf963a4e3aed534f0e32f671864fb6cbbef4f279800beb665b4a8b98bda',
 'src/firmware_ui_model.h': '6ec20c82a720a09afe01afc847f8aba9433b0cbe8a656e2c555fc88fcccb61cf',
 'tools/probe_console_sink/structural.py': '753c2477e256a49a220565cddf419103faefeb1dbfbea61b1e55342368d36c19'}
PREP = {
 'docs/2026-07-30-open-bug-register.md': 'a07f652dc16d9fe6eb8bb12785713662df6780efaa77b019f0774c82fcf49694',
 EV + '2026-09-30-b478-b487-b488-precheck.md': '7a32af65fb6f68d78614a2df78f7340b3cd7b3269409a0e9d14a5fe19896a7c7',
 EV + '2026-09-30-b478-b487-b488-precheck/SHA256SUMS': 'b790bd21b8b80cd77f85aa50e072801402d4566a3839ffdc544f92ba07c2b64a',
 EV + '2026-09-30-b478-b487-b488-brief-review.md': 'bc0d680c8822496976163988726158fc28b2ad74aa1f631f1e92053f93ab5d03',
 EV + '2026-09-30-b478-b487-b488-brief-review/SHA256SUMS': '1e9186f050424caf9e7a47256dcb660f842a5b0662cd145ddb81d5ddd937b52b',
 EV + '2026-09-30-b478-b487-b488-brief-rereview.md': 'f0e9cb1b8848f024f5db31c15da1391d57a30058b16aa87252398ed363b4639c',
 EV + '2026-09-30-b478-b487-b488-brief-rereview/SHA256SUMS': '9f38b46e647d715117a5b8049c4883f2cb445972e732b0cbd5531c1165a5076b',
 EV + '2026-09-30-b478-b487-b488.md': '3df6c8513ae1d964a1574ccc4936015bee1d8b6a1b329aa66511de6e3736aa16',
 EV + '2026-09-30-b478-b487-b488/SHA256SUMS': '53cdf001450002d6b2199bcb2234a595797cfc4ecb9de292d55d76ba8216d51a',
 EV + '2026-09-30-b478-b487-b488/round2/SHA256SUMS': '329ea7af4f21d8e42b38c0396bd79c83a5a9c41861d20977224074a483422167',
 EV + '2026-10-01-b478-b487-b488-qa.md': '189a3eee4486574ad12d57af4b13bcaecc69ee343de53a9109d9136a03d64719',
 EV + '2026-10-01-b478-b487-b488-qa/SHA256SUMS': '6afbf83f424a7ef06e02ed23e0a163d1ab8a54e08ec4591c7621962c62cd1929',
 EV + '2026-10-02-b478-b487-b488-brief-review-r3.md': '674bfcb3eb2d621d6709714226bb16e1237690f677eaf71e0b7d1ec8f5a68359',
 EV + '2026-10-02-b478-b487-b488-brief-review-r3/SHA256SUMS': 'e02389c863f8958cb911f113dd3635d7f9f5d278a4e32f775daf809cc91e4846',
 'tracker.md': '78878667d370eaaa00081bbd6f7011054a3df0422f79a3c0e0b67c43c26fd1c6',
 'MEMORY.md': '1cd7202c2a948384b120bbdf27d0e173c1b002782516358c557ab5b0bee35fc7'}
FOLDERS = {'precheck': ('2026-09-30-b478-b487-b488-precheck', 16), 'review': ('2026-09-30-b478-b487-b488-brief-review', 12),
           'rereview': ('2026-09-30-b478-b487-b488-brief-rereview', 5), 'round1': ('2026-09-30-b478-b487-b488', 24),
           'round2': ('2026-09-30-b478-b487-b488/round2', 8), 'gate': ('2026-10-01-b478-b487-b488-qa', 29),
           'r3-review': ('2026-10-02-b478-b487-b488-brief-review-r3', 16),
           'r4-rereview (QA addition after the brief)': ('2026-10-02-b478-b487-b488-brief-rereview-r4', None)}
CHANGED_OK = ('docs/2026-07-30-open-bug-register.md', 'tracker.md', 'MEMORY.md') + tuple(FENCE)
EXPLAINED_NEW = (EV + '2026-09-30-b478-b487-b488-precheck', EV + '2026-09-30-b478-b487-b488-brief-review', BRIEF,
                 EV + '2026-09-30-b478-b487-b488-brief-rereview', EV + '2026-09-30-b478-b487-b488.md',
                 EV + '2026-09-30-b478-b487-b488/', EV + '2026-10-01-b478-b487-b488-qa',
                 EV + '2026-10-02-b478-b487-b488-brief-review-r3', EV + '2026-10-02-b478-b487-b488-brief-rereview-r4',
                 'tools/test_probe_inbox_transcript.py')
OUT_PATHS = (EV + '2026-10-02-b478-b487-b488-return.md', EV + '2026-10-02-b478-b487-b488-return/')
def sha(root, rel): return hashlib.sha256(open(os.path.join(root, rel), 'rb').read()).hexdigest()
def lines(rel): return open(os.path.join(R, rel), 'rb').read().count(b'\n')
git = lambda root, *a: subprocess.check_output(['git', *a], cwd=root, text=True)
bad, out = [], {'phase': PHASE}
out['head'] = git(R, 'rev-parse', 'HEAD').strip()
out['head_parent'] = git(R, 'rev-parse', 'HEAD^').strip()
out['committed_delta'] = git(R, 'diff', '--name-status', BASE, COMMITTED).splitlines()
delta_paths = [l.split('\t', 1)[1] for l in out['committed_delta']]
out['committed_delta_unexplained'] = [p for p in delta_paths if p not in CHANGED_OK and not p.startswith(EXPLAINED_NEW)]
out['simulator'] = {'head': git(SIM, 'rev-parse', 'HEAD').strip(), 'status': git(SIM, 'status', '--porcelain')}
out['staged'] = git(R, 'diff', '--cached', '--name-only')
dc = subprocess.run(['git', 'diff', '--check'], cwd=R, capture_output=True, text=True); out['git_diff_check'] = {'exit': dc.returncode, 'out': dc.stdout[:2000]}
if out['head'] != COMMITTED or out['head_parent'] != BASE or out['committed_delta_unexplained']: bad.append('HEAD')
if out['simulator']['head'] != SIM_HEAD or out['simulator']['status'].strip(): bad.append('simulator')
if out['staged'].strip(): bad.append('staged')
if dc.returncode: bad.append('git diff --check')
out['brief'] = {'sha256': sha(R, BRIEF), 'lines': lines(BRIEF)}
if (out['brief']['sha256'], out['brief']['lines']) != (BRIEF_SHA, BRIEF_LINES): bad.append('brief hash')
out['fence'] = {p: {'sha256': sha(R, p), 'lines': lines(p), 'candidate': (sha(R, p), lines(p)) == hl} for p, hl in FENCE.items()}
if PHASE == 'preflight':
    bad += ['fence input ' + p for p, v in out['fence'].items() if not v['candidate']]
out['readonly'] = {p: sha(R, p) for p in READONLY}
bad += ['read-only ' + p for p, h in READONLY.items() if out['readonly'][p] != h]
out['preparation'] = {p: sha(R, p) for p in PREP}
bad += ['preparation ' + p for p, h in PREP.items() if out['preparation'][p] != h]
out['folders'] = {}
for name, (d, want) in FOLDERS.items():
    c = subprocess.run(['sha256sum', '-c', 'SHA256SUMS'], cwd=os.path.join(R, EV, d), capture_output=True, text=True)
    out['folders'][name] = {'exit': c.returncode, 'ok': c.stdout.count(': OK')}
    if c.returncode or (want is not None and out['folders'][name]['ok'] != want): bad.append(name + ' SHA256SUMS')
inv = json.load(open(os.path.join(R, EV, '2026-09-30-b478-b487-b488-precheck/inputs.json')))
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
    allowed_changed = set(CHANGED_OK) if key == 'meshroute' else set()
    unexp_changed = [x for x in changed if x not in allowed_changed]
    unexp_new = [n for n in new if not n.startswith(EXPLAINED_NEW + OUT_PATHS)]
    if unexp_changed or unexp_new or missing: bad.append('inventory ' + key)
    out['inventory'][key] = {'inventoried': len(files), 'git_visible_now': len(cur), 'changed': sorted(changed),
                             'unexplained_changed': unexp_changed, 'missing': missing, 'new': len(new), 'unexplained_new': unexp_new,
                             'new_paths_by_prefix': {p: sum(1 for n in new if n.startswith(p)) for p in EXPLAINED_NEW + OUT_PATHS},
                             'new_hashes': {n: sha(root, n) for n in new if os.path.isfile(os.path.join(root, n))}}
out['verdict'] = 'PASS' if not bad else 'FAIL'; out['failures'] = bad
json.dump(out, open(OUT, 'w'), indent=1, sort_keys=True)
m = out['inventory']['meshroute']
print(f"[{PHASE}] HEAD {out['head'][:7]} (parent {out['head_parent'][:7]}; committed delta {len(delta_paths)} paths, unexplained {out['committed_delta_unexplained']}); simulator {out['simulator']['head'][:7]} {'clean' if not out['simulator']['status'].strip() else 'DIRTY'}; staged {len(out['staged'].split())}; diff --check {dc.returncode}; brief {'OK' if 'brief hash' not in bad else 'MISMATCH'} ({out['brief']['lines']} lines)")
print(f"  fence at candidate: {sum(v['candidate'] for v in out['fence'].values())}/{len(FENCE)} (differs: {[p for p, v in out['fence'].items() if not v['candidate']]}); read-only {len(READONLY) - sum('read-only' in b for b in bad)}/{len(READONLY)}; preparation {sum(out['preparation'][p] == h for p, h in PREP.items())}/{len(PREP)}")
print("  folders: " + "; ".join(f"{k} {v['ok']} OK" for k, v in out['folders'].items()))
print(f"  meshroute: inventoried {m['inventoried']}, changed {m['changed']}, unexplained {m['unexplained_changed']}, missing {m['missing']}; new {m['new']}, unexplained {m['unexplained_new']}")
print('SCOPE', out['verdict'], bad)
