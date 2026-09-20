#!/usr/bin/env python3
import hashlib, importlib.util, json, pathlib, re, shutil, subprocess
OUT=pathlib.Path('/tmp/mr-b434-b435-r1-rwlxb7cv')
spec=importlib.util.spec_from_file_location('gate',OUT/'gate.py');gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
ROOT=gate.ROOT
EVIDENCE=ROOT/'docs/superpowers/evidence/2026-09-20-radmin-tools-b434-b435-r1-checkpoint'
RECEIPT=ROOT/'docs/superpowers/evidence/2026-09-20-radmin-tools-b434-b435.md'
REGISTER=ROOT/'docs/2026-07-30-open-bug-register.md'
assert not EVIDENCE.exists() and not RECEIPT.exists()
# Wait for the existing discovery to finish before adding any documentation to its source tree.
result=json.loads((OUT/'tools-discovery.json').read_text());log=(OUT/'tools-discovery.log').read_text()
assert result['rc']==0 and re.search(r'^Ran 356 tests in [\d.]+s\n\nOK$',log,re.M),log[-1000:]
assert not re.search(r'\.\.\. skipped',log)
current=gate.repo_hashes()
assert current==json.loads((OUT/'gate-inputs-before.json').read_text())
gate.dump('gate-inputs-after.json',current)
current_stage=gate.stage_hashes()
assert current_stage==json.loads((OUT/'stage-before.json').read_text())
gate.dump('stage-after.json',current_stage)
EVIDENCE.mkdir()
for item in OUT.iterdir():
    if item.is_file() and item.suffix in ('.py','.json','.log','.txt'):
        shutil.copyfile(item,EVIDENCE/item.name)
# Candidate and its permitted preparation, exactly as used by the gate, including the original register.
overlay=['tools/probe_ui_model_mutations.py','tools/test_mutation_unusable_reason.py']+list(json.loads((OUT/'preparation-inputs.json').read_text()))
for name in overlay:
    dest=EVIDENCE/'frozen-overlay'/name;dest.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(gate.STAGE/name,dest)
for name in ('build','run'):
    dest=EVIDENCE/'retained/selftest'/f'{name}.log';dest.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(ROOT/'.pio/mutation-unusable/selftest'/f'{name}.log',dest)
dest=EVIDENCE/'retained/radmin8node/N01.log';dest.parent.mkdir(parents=True,exist_ok=True)
shutil.copyfile(gate.STAGE/'.pio/mutation-unusable/radmin8node/N01.log',dest)
shutil.copyfile(ROOT/'docs/superpowers/evidence/2026-09-20-radmin-slice10-qa/mut.sh',EVIDENCE/'qa-union-selectors.sh')
(EVIDENCE/'candidate-harness.diff').write_bytes(subprocess.check_output(['git','diff','--','tools/probe_ui_model_mutations.py'],cwd=ROOT))
brief='docs/superpowers/plans/2026-09-20-radmin-tools-b434-b435-mutation-harness.md'
assert gate.file_hash(ROOT/brief)['sha256']=='c947d0c2953a30990e36726d00496baf4832a773b7862c8573435d12cc6d1d48'
register=REGISTER.read_text(); assert '| B436 ' not in register
marker='The next free finding is **B436**.'
assert register.count(marker)==1
register=register.replace(marker,'The next free finding is **B437**.')
anchor='\n**Slice 10 (the standalone main-NV cleanup, R-RA-6) INDEPENDENT QA PASS'
assert register.count(anchor)==1
note='\n**Coder STOP-1 2026-09-20 — B436; B434+B435 revision-1 implementation gate HOLD.** The frozen candidate implements the brief, but `pio test -e native` also runs the mutant: treating every nonzero command exit as a build failure changes the same real `radmin8node` N01 from **1 RED / 0 unusable** (base harness) to **0 RED / 1 unusable** (candidate). Direct mutant binary: **2950 / 195770 / 22 failed assertions**, exit **+1**. The union stopped at its off-floor condition; three active batteries were interrupted and 57 never started. Full tools discovery **356 OK / 0 skipped**; no full-union PASS. QA must fold the build-only command correction into the frozen brief before resume. Candidate, QA preparation and simulator preserved; no closure of B434/B435. [Coder checkpoint](superpowers/evidence/2026-09-20-radmin-tools-b434-b435.md).\n'
register=register.replace(anchor,note+anchor)
register+='\n| B436 (TOOLS-B434435-R1) | **OPEN / STOP-1 — REVISION-1 BUILD/TEST DISCRIMINATOR CONTRADICTS THE REQUIRED UNION FLOOR (coder 2026-09-20); QA BRIEF CORRECTION** | `run_suite` invokes `pio test -e native`, which builds **and executes tests**. The newly required `b.returncode != 0` guard therefore classifies a compiled assertion-failing mutant as an unusable build, before the explicit binary verdict. On the same existing `radmin8node` N01: candidate **0 RED / 1 unusable / exit 1**, pristine `e680271` harness **1 RED / 0 unusable / exit 0**; the fresh mutant binary itself exits **+1** with **2950 cases / 195770 assertions / 5 failed cases / 22 failed assertions / 0 skipped**. Captured PIO output shows Testing and failed checks; its SIGHUP text is a wrapper mislabel: installed PlatformIO 6.1.19 maps `abs(return_code)` to a signal even for positive 1. The required union was stopped, not reported green. **CLOSE BY:** QA makes the initial command explicitly build-only (installed CLI supports `pio test -e native --without-testing`), preserves strict actual-build failure refusal and separate binary verdict parsing, and fences a real regression proving an assertion-failing compiled mutant still goes RED. Then run the whole tools/61-battery gate afresh. No silent command repair, table/exit-policy relaxation, production change, or owner product ruling. Full discovery **356 OK / zero skipped** and both synthetic controls pass, but do not cover this real command interaction. Frozen brief unchanged; B434/B435 remain open. [Receipt and reproduction](superpowers/evidence/2026-09-20-radmin-tools-b434-b435.md). |\n'
REGISTER.write_text(register)
(EVIDENCE/'m1-register.diff').write_text(''.join(__import__('difflib').unified_diff((gate.STAGE/'docs/2026-07-30-open-bug-register.md').read_text().splitlines(True),register.splitlines(True),fromfile='permitted-preparation/register',tofile='checkpoint/register')))
receipt=(OUT/'receipt-draft.md').read_text()
receipt=receipt.replace('| Full tools discovery | PENDING |','| Full tools discovery | **356 tests OK / 0 skipped** (349 prior + 7 new), exit 0 |')
receipt=receipt.replace('PLACEHOLDER_HARNESS',gate.file_hash(ROOT/'tools/probe_ui_model_mutations.py')['sha256']).replace('PLACEHOLDER_TEST',gate.file_hash(ROOT/'tools/test_mutation_unusable_reason.py')['sha256'])
RECEIPT.write_text(receipt)
# Exclude the evidence directory from this source manifest; inventory its contents separately below.
files=gate.repo_hashes();prefix=str(EVIDENCE.relative_to(ROOT))+'/'
files={k:v for k,v in files.items() if not k.startswith(prefix)}
(EVIDENCE/'frozen-tree-inputs.json').write_text(json.dumps(files,indent=2,sort_keys=True)+'\n')
original=json.loads((OUT/'preflight-inputs.json').read_text())
delta=[n for n in sorted(set(original)|set(files)) if original.get(n)!=files.get(n)]
expected=sorted(['tools/probe_ui_model_mutations.py','tools/test_mutation_unusable_reason.py','docs/2026-07-30-open-bug-register.md',str(RECEIPT.relative_to(ROOT))])
assert delta==expected,delta
sim=pathlib.Path('/home/staszek/lora-universal-simulator')
assert subprocess.check_output(['git','status','--porcelain=v1'],cwd=sim)==b''
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=sim).decode().strip()=='6585649ea5a780f0542b2931853a667be56a5b2b'
(EVIDENCE/'final-scope.json').write_text(json.dumps({'changed_from_entry_excluding_evidence':delta,'evidence_root':prefix,'tools_frozen':{n:files[n] for n in ('tools/probe_ui_model_mutations.py','tools/test_mutation_unusable_reason.py')},'brief':files[brief],'simulator_head':'6585649ea5a780f0542b2931853a667be56a5b2b','simulator_status':'clean','overall':'STOP-1 / HOLD B436'},indent=2,sort_keys=True)+'\n')
(EVIDENCE/'final-status.txt').write_bytes(subprocess.check_output(['git','status','--short'],cwd=ROOT))
check=subprocess.run(['git','diff','--check'],cwd=ROOT,capture_output=True,text=True)
(EVIDENCE/'final-whitespace.log').write_text(check.stdout+check.stderr+f'exit={check.returncode}\n');assert check.returncode==0
(EVIDENCE/'README.md').write_text('''# Revision-1 HOLD checkpoint — B436

See the adjacent coder receipt. This archive does not claim the union passed.

- `frozen-overlay/` holds the two candidate tool files and the exact four permitted QA inputs used by the gate. Apply it over MeshRoute `e680271`; simulator pin is `6585649`.
- `gate-inputs-before.json` and `gate-inputs-after.json` match all 4,488 shared inputs during the attempt. `stage-before.json` / `stage-after.json` match 4,831 copied files, including ignored input files.
- `frozen-tree-inputs.json` covers the post-receipt tree outside this archive. The register differs only by mandatory B436 bookkeeping (`m1-register.diff`); the brief and implementation remain frozen.
- `tools-discovery.log`: 356 OK, no skips. `focused-discovery.log` names both executed RED controls. `selftest.log` and `retained/selftest/` retain the complete fabricated outcomes.
- `mut_radmin8node.log`: the completed off-floor battery; `retained/radmin8node/N01.log` is the actual parent-retained PIO output. Three other `mut_` logs end at interruption. No result from them counts as PASS.
- `b436-base-harness.log` and `b436-mutant-binary.log` prove that this same mutant is usable and fails real assertions. `base-harness.py` is byte-identical to the pinned base.
- `platformio-source-evidence.json` and `pio-test-help.log` support the build-plus-test diagnosis and proposed `--without-testing` correction. No such correction is landed.
- `gate.py`, `reproduce_b436.py` and `stop_audit.py` preserve the commands used. They name the original scratch paths; these are evidence drivers, not new repository tools. Do not run `land_checkpoint.py` again: it is the completed receipt/registration operation.
- `SHA256SUMS` hashes every archive file except itself.
''')
entries=[]
for p in sorted(EVIDENCE.rglob('*')):
    if p.is_file() and p.name!='SHA256SUMS':entries.append(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(EVIDENCE)}')
(EVIDENCE/'SHA256SUMS').write_text('\n'.join(entries)+'\n')
print('Landed HOLD checkpoint, B436 registered; '+str(len(files))+' frozen tree inputs / '+str(len(entries))+' evidence files.')
