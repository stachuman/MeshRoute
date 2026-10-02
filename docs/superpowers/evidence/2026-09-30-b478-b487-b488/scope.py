# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# B478 + B487 + B488 + B490 — preflight (brief §1) and the input/scope snapshot for input stability and the freeze
# (§4.1 steps 1/12, §5). Reads only. Usage: python3 -B scope.py <preflight|final> <out.json>
import hashlib, json, os, subprocess, sys
R = '/home/staszek/MeshRoute'; SIM = '/home/staszek/lora-universal-simulator'
EV = 'docs/superpowers/evidence/'
PC = R + '/' + EV + '2026-09-30-b478-b487-b488-precheck'
RV = R + '/' + EV + '2026-09-30-b478-b487-b488-brief-review'
PHASE, OUT = sys.argv[1], sys.argv[2]
HEAD = '10f333298ffad1098daeec37e756ed79ab0fd97f'; SIM_HEAD = '6585649ea5a780f0542b2931853a667be56a5b2b'
BRIEF = 'docs/superpowers/plans/2026-09-30-b478-b487-b488-tool-repairs.md'
BRIEF_SHA = '06e415797741412b0a364b7ff21b0a993a39eed85d3e7e40e5b9d8c113799cd1'
FENCE = {  # brief §1 executable inputs at the base: sha256, lines
 'tools/probe_ui_model_mutations.py': ('b71270ee2ed63d67bfbc4051a8b98cb80ce2e2da49cbba27aa548d781fb13a70', 12931),
 'tools/test_mutation_unusable_reason.py': ('f6ac8119d1f1b585dc6162254747114da08a453387d5a4e5ca7a08409d2838b5', 117),
 'tools/probe_inbox_verbs/transcript.py': ('ffb23ad6cecb3d74859136ce06b237027739ddc8dfcdad5599a9180649b9b000', 424),
 'tools/probe_inbox_verbs/transcript_main.cpp': ('3914de10acddb6a6e39384fa4f512461133e1d7ca538f50cb66939a8cd828163', 148)}
NEW_FENCE = ('tools/test_probe_inbox_transcript.py',)
READONLY = {
 'tools/test_worker_formula_derived.py': 'fb8704f2d3b0629b577b7c4c9ea0a4544e08f6c0d4c585321ad1f1f801c18176',
 'tools/probe_inbox_verbs/run.sh': 'db7add6c5fb335097bc84a192b925b43604f638f7951e422cd926f7ab6292948',
 'tools/probe_inbox_verbs/probe_main.cpp': '69cc26c59f0be12952421501fb0365e85195ff9343995cd4056068e20f0f7650',
 'tools/probe_inbox_verbs/fakes/Preferences.h': 'bd57ac322eafe89e4e882c35b8bc02b9fda60f4738e11bfe1dacb0699b617346',
 'tools/probe_board_ui/fakes/Arduino.h': 'deaa9f4a6f3d010fa4010722973c701c4f5effadb2442213812b7884f85ab853',
 'tools/probe_deferred_actions/run.py': '694387bd6fe1baa1671ff23e1fc0e2002eb844b6af7dd2538ab8de24dfb3b55f',
 'tools/probe_deferred_actions/probe.cpp': '57364be0188f908c62f1a4659c7625def338bf3b6cc9bc09865a2ad7a44ae587',
 'tools/gen_command_inventory.py': 'b9d141a4b6a560a3f4bfd0af68bbdb2c461fcd38a3b67eb5ac04fb206f3db68a',
 'docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md': 'c320046822c0cb47bb38ca76f4c1ea8ebd42c7f2346bb50791a35e05d74ae807',
 'src/fw_main.cpp': '4b9b86afd3303cec913a343faa159d4305591d31cf185fe8e8dfc27b509f7766',
 'lib/console/console_line.h': '640e4886ef9b0708828d2679cbc993853158475b6d100118bcc08bcf0f7c85a9',
 'src/device_ble.h': '56d68dd9270f670451115854e4861dceab8b25c7d81b1f923f73aa5a3f941ac5',
 'src/firmware_commands.cpp': '48f9b19a9f402585c8f3c061999d4c993ec53cf936b6f6b9a4921e586bc060fd',
 'src/firmware_remote_actions.cpp': '5d112359ee52dc991519dae3fa20ec48d988fa8aafec0341590e18901c2b682e',
 'src/firmware_remote_client.cpp': 'd1156cf963a4e3aed534f0e32f671864fb6cbbef4f279800beb665b4a8b98bda',
 'src/firmware_ui_model.h': '6ec20c82a720a09afe01afc847f8aba9433b0cbe8a656e2c555fc88fcccb61cf',
 'tools/probe_console_sink/structural.py': '753c2477e256a49a220565cddf419103faefeb1dbfbea61b1e55342368d36c19'}
PREP = {
 'docs/2026-07-30-open-bug-register.md': 'ca93842ab9e10fb915d210fa4c01c9f261c791420018126f592881b6ce6c7ab3',
 EV + '2026-09-30-b478-b487-b488-precheck.md': '7a32af65fb6f68d78614a2df78f7340b3cd7b3269409a0e9d14a5fe19896a7c7',
 EV + '2026-09-30-b478-b487-b488-precheck/SHA256SUMS': 'b790bd21b8b80cd77f85aa50e072801402d4566a3839ffdc544f92ba07c2b64a',
 EV + '2026-09-30-b478-b487-b488-brief-review.md': 'bc0d680c8822496976163988726158fc28b2ad74aa1f631f1e92053f93ab5d03',
 EV + '2026-09-30-b478-b487-b488-brief-review/SHA256SUMS': '1e9186f050424caf9e7a47256dcb660f842a5b0662cd145ddb81d5ddd937b52b',
 'tracker.md': 'ef643006629f5983220b9cdd1a5373fde211eccb1189fca747c03c1c39af63d3',
 'MEMORY.md': '61b9d8fb83546dc3af1a68902df0d82244390c8fe7a04313411801f52b02269f'}
CHANGED_OK = ('docs/2026-07-30-open-bug-register.md', 'tracker.md', 'MEMORY.md')
EXPLAINED_NEW = (EV + '2026-09-30-b478-b487-b488-precheck.md', EV + '2026-09-30-b478-b487-b488-precheck/', BRIEF,
                 EV + '2026-09-30-b478-b487-b488-brief-review.md', EV + '2026-09-30-b478-b487-b488-brief-review/',
                 EV + '2026-09-30-b478-b487-b488-brief-rereview.md', EV + '2026-09-30-b478-b487-b488-brief-rereview/')
OUT_PATHS = (EV + '2026-09-30-b478-b487-b488.md', EV + '2026-09-30-b478-b487-b488/')
def sha(root, rel): return hashlib.sha256(open(os.path.join(root, rel), 'rb').read()).hexdigest()
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
out['readonly'] = {p: sha(R, p) for p in READONLY}
for p, h in READONLY.items():
    if out['readonly'][p] != h: bad.append('read-only ' + p)
out['preparation'] = {p: sha(R, p) for p in PREP}
for p, h in PREP.items():
    if out['preparation'][p] != h: bad.append('preparation ' + p)
out['folders'] = {}
for name, d, want in (('precheck', PC, 16), ('review', RV, 12)):
    c = subprocess.run(['sha256sum', '-c', 'SHA256SUMS'], cwd=d, capture_output=True, text=True)
    out['folders'][name] = {'exit': c.returncode, 'ok': c.stdout.count(': OK')}
    if c.returncode or out['folders'][name]['ok'] != want: bad.append(name + ' SHA256SUMS')
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
    allowed_changed = (set(CHANGED_OK) | (set(FENCE) if PHASE == 'final' else set())) if key == 'meshroute' else set()
    allowed_new = EXPLAINED_NEW + OUT_PATHS + (NEW_FENCE if PHASE == 'final' else ())
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
print(f"  fence at base: {sum(v['base'] for v in out['fence'].values())}/{len(FENCE)}; new path present: {[p for p, v in out['new_fence'].items() if v]}; read-only {len(READONLY) - sum('read-only' in b for b in bad)}/{len(READONLY)}; preparation {sum(out['preparation'][p] == h for p, h in PREP.items())}/{len(PREP)}; folders precheck {out['folders']['precheck']['ok']} OK, review {out['folders']['review']['ok']} OK")
print(f"  meshroute: inventoried {m['inventoried']}, changed {m['changed']}, unexplained {m['unexplained_changed']}, missing {m['missing']}; new {len(m['new'])}, unexplained {m['unexplained_new']}")
print(f"  simulator: inventoried {s['inventoried']}, changed {len(s['changed'])}, missing {len(s['missing'])}, new {len(s['new'])}")
print('SCOPE', out['verdict'], bad)
