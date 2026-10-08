# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# W7+W8 — preflight (brief r2 §1) and the scope/freeze snapshot (§4.1 final step 1/13, §5). Reads only.
# Usage: python3 -B scope.py <preflight|final> <out.json>
import hashlib, json, os, subprocess, sys
R = '/home/staszek/MeshRoute'; SIM = '/home/staszek/lora-universal-simulator'
EV = 'docs/superpowers/evidence/'
PHASE, OUT = sys.argv[1], sys.argv[2]
HEAD = '4c1a000bc71706ac438e64161a9452966a66615f'; SIM_HEAD = '6585649ea5a780f0542b2931853a667be56a5b2b'
BRIEF = 'docs/superpowers/plans/2026-10-04-standalone-mobile-home-w7w8-editor-rename-messages.md'
BRIEF_SHA, BRIEF_LINES = '47cb5a461d07cc3cbaf0ab6ae81fe535e206eda996201885f4dbe878dd11cb8e', 732
DESIGN = 'docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md'
FENCE = {
 'src/firmware_ui_model.h': ('6ec20c82a720a09afe01afc847f8aba9433b0cbe8a656e2c555fc88fcccb61cf', 6478),
 'src/firmware_ui_send.h': ('2d6b09354adf590f48972013a1e47e3a933614f9ed837d83178a9d5ab18c5563', 787),
 'src/firmware_ui_chrome.h': ('b4c1ff24451725a03d2e966fd1da3a8102c61815b74cc995f1fd8f2c0ee6a181', 621),
 'src/firmware_ui.cpp': ('11789f4392624b2e312492ad19c221659df9ccd01276070ef145f1c9fa8a32b1', 2936),
 'test/test_firmware_ui_model.cpp': ('5f7d61cadf1b50a66cd07e2f229c9478c8c27c5e1f66a0597ff580a1b8397287', 12527),
 'test/test_firmware_ui_send.cpp': ('a79e804ac53cd714455232e4cfdc92ec27f599dd262119e52f5ce7ec9bfbe736', 2994),
 'test/test_firmware_ui_chrome.cpp': ('b19c08ad5a4e2e841d92f1a1b9972e6f1ec46cdadb7d58ed12d09db2d4b2e478', 1389),
 'tools/probe_firmware_ui/probe_main.cpp': ('188bb4eab5128de44536dd7897180bdf2bf29227093466f92e44c7e593a6776d', 7886),
 'tools/probe_firmware_ui/run.sh': ('ffcb62af6655a1a3a5eec8164f0152cc2136e2ad13b5adee922e75436afcaab5', 2116),
 'tools/probe_board_ui/run.sh': ('366a8bf089604c5391b3933b284561c33b6ce5e05cdf5a9e0c8255210beea5bf', 1665),
 'tools/probe_board_ui/expected.tsv': ('2a30e4d1f10683a17b46eccbdd1b31de4ac7252a7491d9ff60e2d202edcb2196', 598),
 'tools/probe_board_ui/accounting.py': ('2a23f7521805f38c6a48cd24008cd9eeeac1e8ad7b0b2aa8e3a85e38398f1e09', 256),
 'tools/probe_ui_model_mutations.py': ('27807b320427fb090840e927bf559bfff909f78761d98b29e3aff8b3cb4d177d', 13163),
 'tools/probe_board_abi.py': ('fabd78ac4f48177cb27d21181c724dd7a9898d6c062c58c721ca3522f0bd9e2f', 1087)}
NEW_FENCE = ('src/firmware_ui_editor.h', 'test/test_firmware_ui_editor.cpp')
READONLY = {
 'src/firmware_ui_status.h': '338548341e579a3faac514592103cda794e2cca8cd1dd5fcc28a3a645d4c38cd',
 'src/firmware_ui_input.h': '30c74299c9bb580e016ed7efa847f1e6af29b5b560891cdbd2bb732d0ce4ecc0',
 'src/firmware_ui_team.h': '63f9074be7ba73072816ba3a3d3654853da1a721b8dfeff7db57e854e9c72cdf',
 'src/firmware_ui_presets.h': '1d67253f74490dbb43d83eae06b7bb5389151d4256da37b3c97e114000aebe23',
 'src/firmware_ui_invite.h': 'f3f61d692752bb24b131390690b2529327c75b47a6a769d720f36749220b9c7c',
 'src/firmware_config.h': 'c72d59600745da4d7b65b8cbd37d3da5b9f5a74da33cc5a71a98a4fa8aac8bd6',
 'src/firmware_config.cpp': 'accb667703290ba57e590b4ec07d20ab6bf21dc8ae7d8b9ae1840e2bf3dfdc18',
 'src/device_nv.h': '36296bcbc1c6975a6cf0bea2caf1ca5eebd182832f2550c12de1a7185429fffa',
 'src/firmware_commands.cpp': '48f9b19a9f402585c8f3c061999d4c993ec53cf936b6f6b9a4921e586bc060fd',
 'src/fw_main.cpp': '4b9b86afd3303cec913a343faa159d4305591d31cf185fe8e8dfc27b509f7766',
 'lib/core/command.h': 'f537b1cc28d5ac6ff22f8243913933455aa371a2e50989a88a6bc1d28c9c612d',
 'variants/heltec_common/board_ui.cpp': '35d7c07995d2fd6886da46dd556a6af47bf414dbec4e31d36c1bebf9641daa46',
 'test/test_firmware_ui_input.cpp': '6aefa729e89321f1d09f215fd31188bc43fcfe71b96511e382431e047ee57fed',
 'test/test_firmware_ui_status.cpp': '4605ac3df22c451b1b639a7a970e90fb71b3370b92277796542b7f02f611f6d8',
 'test/test_firmware_ui_team.cpp': 'a97383e0df80825a0b8f39e047c6a261e8357f2ab714fad419c63fb586c296c8',
 'tools/probe_board_ui/probe_main.cpp': 'a06c448fb7bb28e25cb2f86fa2fd18a12bd57aadfc0d2bed3ae157389e67b204',
 'tools/probe_board_ui/negctl.py': '7634d66b9391b6fc90f293f51ef2b9376324c83da521fc8ba6abbcc0e1ff54c4',
 'tools/probe_board_ui/fakes/Arduino.h': 'deaa9f4a6f3d010fa4010722973c701c4f5effadb2442213812b7884f85ab853',
 'tools/probe_accounting.sh': '785cf0f70951e0647429ac0b29587055636bf269b47ff71f8fd15dc87a26d9f5',
 'tools/test_probe_firmware_ui.py': '836ba7ac6eccae3b73b18e6929765fbf12d2181515c9ee2361b0e9cc43784047',
 'tools/test_probe_board_ui.py': '82a344994d963adc80b02d1758b9a3c81a73cb3af7834f5df06cf66fbf7c51e2',
 'tools/test_mutation_unusable_reason.py': '7c04519c2f40f11fdac5fc1d55648349dd8ac10fa2237cad9313a72b186fb61d',
 'tools/test_worker_formula_derived.py': 'fb8704f2d3b0629b577b7c4c9ea0a4544e08f6c0d4c585321ad1f1f801c18176',
 'tools/gen_command_inventory.py': 'b9d141a4b6a560a3f4bfd0af68bbdb2c461fcd38a3b67eb5ac04fb206f3db68a'}
PREP = {
 DESIGN: '7ed161d6e777f1eb98fa65319de9bed2f7864bcb532004eeb00efc912a2325f6',
 'docs/2026-07-30-open-bug-register.md': 'b3ac76d9a49793a256b53d678b729138cd959ae895bcd8ccc6f2686c6919a3ef',
 'tracker.md': 'a0b5231a8c5a12a3f1b895a4a5c06b418a835b3a8ee4ce1f24fb4cf6d5844be9',
 'MEMORY.md': 'f8c41ba28bf903a3c66fd98bce740d50af0c59024922688f2dfec70729a740ad',
 EV + '2026-10-03-standalone-mobile-home-w7-precheck.md': 'e4148d274271addf9e0ff1535b4fce6a9db8155ce54df4b73f929e1d9524be1e',
 EV + '2026-10-03-standalone-mobile-home-w7-precheck/SHA256SUMS': '037b975bd3737808627ff90fb389ed0fea649121b5e0f1d60dd434c564b5af6f',
 EV + '2026-10-03-standalone-mobile-home-w8-precheck.md': '2598f88c2894f3b6896e62ce6e9a2a2f83357e7b7d93afd0f6baa696e9e79a13',
 EV + '2026-10-03-standalone-mobile-home-w8-precheck/SHA256SUMS': '7b51d522015834bdc906f0f534c8b3df0b23a3f0eaf593e84b7cf50a1933db55',
 EV + '2026-10-04-standalone-mobile-home-w7w8-brief-review.md': '3eb6c846f395257257df8ae7617d2919cc5174b4df888885c6a52d6b6b6ce877',
 EV + '2026-10-04-standalone-mobile-home-w7w8-brief-review/SHA256SUMS': 'ca6e3c27292220e00f99721308a3d1f18bc5ef608b9a9684c0f93065583cdd88',
 EV + '2026-10-02-b478-b487-b488-qa-regate.md': 'f5490a4fe7e1df92a3c8dd12e6295e5405b7af5622f16c9806bf8045013c1fbd',
 EV + '2026-10-02-b478-b487-b488-qa-regate/SHA256SUMS': 'c0e3b26fd893ee5d35f22f92eba726e829737a25f5f56b5ca1c17d0808e8907d'}
FOLDERS = {'W7 pre-check': ('2026-10-03-standalone-mobile-home-w7-precheck', 26),
           'W8 pre-check': ('2026-10-03-standalone-mobile-home-w8-precheck', 30),
           'brief review': ('2026-10-04-standalone-mobile-home-w7w8-brief-review', 10),
           're-gate': ('2026-10-02-b478-b487-b488-qa-regate', 40),
           're-review (QA addition after the brief)': ('2026-10-04-standalone-mobile-home-w7w8-brief-rereview', None)}
CHANGED_OK = (DESIGN, 'docs/2026-07-30-open-bug-register.md', 'tracker.md', 'MEMORY.md')
EXPLAINED_NEW = (EV + '2026-10-03-standalone-mobile-home-w8-precheck', EV + '2026-10-04-standalone-mobile-home-w7w8-brief-review',
                 EV + '2026-10-04-standalone-mobile-home-w7w8-brief-rereview', BRIEF)
OUT_PATHS = (EV + '2026-10-04-standalone-mobile-home-w7w8.md', EV + '2026-10-04-standalone-mobile-home-w7w8/')
# ★ THE AUTHORIZED CHECKPOINT DELTAS (QA stop resolution, 2026-10-06, SHA-256 8eabbaa9…8eb0e) — applied in `final` ONLY;
#   the preflight tables above stay as the preflight measured them (preflight.json is preserved, never re-derived):
#   · the newly fenced test at its checkpoint hash (= HEAD), edited for exactly two null-safe comparison guards;
#   · the register at its authorized new hash (the only preparation input QA changed);
#   · the QA receipt and its directory as explained new inputs, the receipt itself pinned, its seal verified (16).
SR = EV + '2026-10-06-standalone-mobile-home-w7w8-stop-resolution'
CHECKPOINT_FENCE = {'test/test_firmware_ui_presets.cpp': ('8811f177de4d94467505758a1b09f294e5f30283f8387865e3ce95e211831c03', 1044)}
CHECKPOINT_PREP = {'docs/2026-07-30-open-bug-register.md': '5ad4e8acb1e9a686d268453018a6e7824a450d4d6a2484aaa2ecb60822d0c835',
                   SR + '.md': '8eabbaa9f973c74766acffb63a85cad4460c320220e772453d25fbfb8ad8eb0e'}
CHECKPOINT_FOLDERS = {'stop resolution (QA, 2026-10-06)': ('2026-10-06-standalone-mobile-home-w7w8-stop-resolution', 16)}
# The checkpoint inventory (QA's `inputs.json`, 2569 paths): at the final, only this package's own evidence and the two
# guarded tests may differ from it, and only this package's evidence may be new beside the receipt itself.
# ⓘ The register differs from the inventory because QA updated it AFTER capturing `inputs.json`; it is explained ONLY
#   at its authorized hash, which CHECKPOINT_PREP pins (a register at any other hash fails `preparation`).
CHECKPOINT_CHANGED_OK = ('test/test_firmware_ui_model.cpp', 'test/test_firmware_ui_presets.cpp',
                         'docs/2026-07-30-open-bug-register.md')
if PHASE == 'final':
    FENCE = {**FENCE, **CHECKPOINT_FENCE}
    PREP = {**PREP, **CHECKPOINT_PREP}
    FOLDERS = {**FOLDERS, **CHECKPOINT_FOLDERS}
    EXPLAINED_NEW = EXPLAINED_NEW + (SR,)
def sha(root, rel): return hashlib.sha256(open(os.path.join(root, rel), 'rb').read()).hexdigest()
def nlines(rel): return open(os.path.join(R, rel), 'rb').read().count(b'\n')
git = lambda root, *a: subprocess.check_output(['git', *a], cwd=root, text=True)
bad, out = [], {'phase': PHASE}
out['head'] = git(R, 'rev-parse', 'HEAD').strip()
out['simulator'] = {'head': git(SIM, 'rev-parse', 'HEAD').strip(), 'status': git(SIM, 'status', '--porcelain')}
out['staged'] = git(R, 'diff', '--cached', '--name-only')
dc = subprocess.run(['git', 'diff', '--check'], cwd=R, capture_output=True, text=True); out['git_diff_check'] = {'exit': dc.returncode, 'out': dc.stdout[:2000]}
if out['head'] != HEAD: bad.append('HEAD (a later owner commit must be reconciled by content: brief §1 preflight 1)')
if out['simulator']['head'] != SIM_HEAD or out['simulator']['status'].strip(): bad.append('simulator')
if out['staged'].strip(): bad.append('staged')
if dc.returncode: bad.append('git diff --check')
out['brief'] = {'sha256': sha(R, BRIEF), 'lines': nlines(BRIEF)}
if (out['brief']['sha256'], out['brief']['lines']) != (BRIEF_SHA, BRIEF_LINES): bad.append('brief hash')
out['fence'] = {p: {'sha256': sha(R, p), 'lines': nlines(p), 'base': (sha(R, p), nlines(p)) == hl} for p, hl in FENCE.items()}
out['new_fence'] = {p: ({'sha256': sha(R, p), 'lines': nlines(p)} if os.path.exists(os.path.join(R, p)) else None) for p in NEW_FENCE}
if PHASE == 'preflight':
    bad += ['fence input ' + p for p, v in out['fence'].items() if not v['base']]
    bad += ['new path already present ' + p for p, v in out['new_fence'].items() if v is not None]
out['readonly'] = {p: sha(R, p) for p in READONLY}
bad += ['read-only ' + p for p, h in READONLY.items() if out['readonly'][p] != h]
out['preparation'] = {p: sha(R, p) for p in PREP}
bad += ['preparation ' + p for p, h in PREP.items() if out['preparation'][p] != h]
out['folders'] = {}
for name, (d, want) in FOLDERS.items():
    c = subprocess.run(['sha256sum', '-c', 'SHA256SUMS'], cwd=os.path.join(R, EV, d), capture_output=True, text=True)
    out['folders'][name] = {'exit': c.returncode, 'ok': c.stdout.count(': OK')}
    if c.returncode or (want is not None and out['folders'][name]['ok'] != want): bad.append(name + ' SHA256SUMS')
inv = json.load(open(os.path.join(R, EV, '2026-10-03-standalone-mobile-home-w8-precheck/inputs.json')))
out['inventory'] = {}
for key, root, files in (('meshroute', R, inv['files']), ('simulator', SIM, inv['simulator']['files'])):
    changed, missing = [], []
    for rel, meta in files.items():
        full = os.path.join(root, rel)
        if 'symlink' in meta:
            if not os.path.islink(full): missing.append(rel)
            elif os.readlink(full) != meta['symlink']: changed.append(rel)
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
                             'unexplained_changed': unexp_changed, 'missing': missing, 'new': len(new), 'unexplained_new': unexp_new,
                             'new_by_prefix': {p: sum(1 for n in new if n.startswith(p)) for p in allowed_new},
                             'new_hashes': {n: sha(root, n) for n in new if os.path.isfile(os.path.join(root, n))}}
if PHASE == 'final':
    ck = json.load(open(os.path.join(R, SR, 'inputs.json')))
    cchanged, cmissing = [], []
    for rel, meta in ck['files'].items():
        full = os.path.join(R, rel)
        if 'symlink' in meta:
            if not os.path.islink(full) or os.readlink(full) != meta['symlink']: cchanged.append(rel)
            continue
        if not os.path.isfile(full): cmissing.append(rel); continue
        if hashlib.sha256(open(full, 'rb').read()).hexdigest() != meta['sha256']: cchanged.append(rel)
    cur = subprocess.run(['git', 'ls-files', '-co', '--exclude-standard', '-z'], cwd=R, capture_output=True).stdout.split(b'\0')
    cnew = [x for x in sorted(os.fsdecode(y) for y in cur if y) if x not in ck['files']]
    cunexp_changed = [x for x in cchanged if x not in CHECKPOINT_CHANGED_OK and not x.startswith(OUT_PATHS)]
    cunexp_new = [x for x in cnew if not x.startswith(OUT_PATHS + (SR,))]
    if cunexp_changed or cunexp_new or cmissing or ck['head'] != out['head'] or ck['sim_head'] != out['simulator']['head']:
        bad.append('checkpoint inventory')
    out['checkpoint_inventory'] = {'inventoried': len(ck['files']), 'changed': sorted(cchanged),
                                   'unexplained_changed': cunexp_changed, 'missing': cmissing, 'new': cnew,
                                   'unexplained_new': cunexp_new}
out['verdict'] = 'PASS' if not bad else 'FAIL'; out['failures'] = bad
json.dump(out, open(OUT, 'w'), indent=1, sort_keys=True)
m = out['inventory']['meshroute']
print(f"[{PHASE}] HEAD {out['head'][:7]}; simulator {out['simulator']['head'][:7]} {'clean' if not out['simulator']['status'].strip() else 'DIRTY'}; staged {len(out['staged'].split())}; diff --check {dc.returncode}; brief {out['brief']['lines']} lines {'OK' if 'brief hash' not in bad else 'MISMATCH'}")
print(f"  fence at base: {sum(v['base'] for v in out['fence'].values())}/{len(FENCE)}; new paths present: {[p for p, v in out['new_fence'].items() if v]}; read-only {len(READONLY) - sum('read-only' in b for b in bad)}/{len(READONLY)}; preparation {sum(out['preparation'][p] == h for p, h in PREP.items())}/{len(PREP)}")
print("  seals: " + "; ".join(f"{k} {v['ok']} OK" for k, v in out['folders'].items()))
print(f"  meshroute: inventoried {m['inventoried']}, changed {m['changed']}, unexplained {m['unexplained_changed']}, missing {m['missing']}; new {m['new']} {m['new_by_prefix']}, unexplained {m['unexplained_new']}")
s = out['inventory']['simulator']; print(f"  simulator: inventoried {s['inventoried']}, changed {len(s['changed'])}, missing {len(s['missing'])}, new {s['new']}")
if 'checkpoint_inventory' in out:
    c = out['checkpoint_inventory']
    print(f"  checkpoint inventory: {c['inventoried']} paths; changed {len(c['changed'])} (unexplained {c['unexplained_changed']}), missing {c['missing']}, new {len(c['new'])} (unexplained {c['unexplained_new']})")
print('SCOPE', out['verdict'], bad)
