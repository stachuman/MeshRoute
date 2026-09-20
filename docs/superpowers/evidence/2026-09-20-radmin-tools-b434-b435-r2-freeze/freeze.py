#!/usr/bin/env python3
import ast, datetime, hashlib, importlib.util, json, pathlib, re, shutil, subprocess
OUT=pathlib.Path('/tmp/mr-b434-b435-r2-6kvsxgb3')
spec=importlib.util.spec_from_file_location('gate',OUT/'gate.py');gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
ROOT=gate.ROOT
ARCHIVE=ROOT/'docs/superpowers/evidence/2026-09-20-radmin-tools-b434-b435-r2-freeze'
RECEIPT=ROOT/'docs/superpowers/evidence/2026-09-20-radmin-tools-b434-b435.md'
BRIEF='docs/superpowers/plans/2026-09-20-radmin-tools-b434-b435-mutation-harness.md'
TOOLS=['tools/probe_ui_model_mutations.py','tools/test_mutation_unusable_reason.py']
assert not ARCHIVE.exists()
discovery=json.loads((OUT/'tools-discovery.json').read_text());log=(OUT/'tools-discovery.log').read_text()
assert discovery['rc']==0 and re.search(r'^Ran 356 tests in [\d.]+s\n\nOK$',log,re.M)
assert '... skipped' not in log
subprocess.run(['python3',str(OUT/'audit_union.py'),'--final'],check=True)
union=json.loads((OUT/'union-audit.json').read_text())['summary']
current=gate.repo_hashes();assert current==json.loads((OUT/'gate-inputs-before.json').read_text())
gate.dump('gate-inputs-after.json',current)
assert gate.stage_hashes()==json.loads((OUT/'stage-before.json').read_text())
assert current[BRIEF]['sha256']=='f6eafd394894fd65512fcd66ba9613ff7c94308763de3ef4af486daffa3a1048'
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT).decode().strip()=='e680271791397dbdaa3a7a34b368ebc8851e60a2'
simulator=pathlib.Path('/home/staszek/lora-universal-simulator')
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=simulator).decode().strip()=='6585649ea5a780f0542b2931853a667be56a5b2b'
assert subprocess.check_output(['git','status','--porcelain=v1'],cwd=simulator)==b''
assert subprocess.check_output(['git','diff','--cached','--name-only'],cwd=ROOT)==b''
# The only harness delta from the preserved checkpoint is the authorized build-only argument.
r1=(OUT/'probe_ui_model_mutations.py').read_text();r2=(ROOT/TOOLS[0]).read_text()
old='["pio", "test", "-e", "native"]';new='["pio", "test", "-e", "native", "--without-testing"]'
assert r1.count(old)==1 and r2==r1.replace(old,new)
# All test definitions remain; only the existing success-verdict test gains the B436 command checks.
t1=ast.parse((OUT/'test_mutation_unusable_reason.py').read_text());t2=ast.parse((ROOT/TOOLS[1]).read_text())
def tests(tree):return {n.name:ast.dump(n,include_attributes=False) for n in ast.walk(tree) if isinstance(n,ast.FunctionDef)}
a,b=tests(t1),tests(t2);assert set(a)==set(b)
assert [k for k in a if a[k]!=b[k]]==['test_existing_doctest_verdict_is_unchanged']
assert len([k for k in a if k.startswith('test_')])==7
gate.dump('r2-scope-audit.json',{'harness_delta':'one build-only argument from frozen revision 1','test_delta':'five lines in existing verdict test; no test removed or added','fresh_discovery':356,'full_union':union,'shared_inputs':len(current),'stage_files':len(json.loads((OUT/'stage-before.json').read_text())),'brief_unchanged':True,'QA_preparation_unchanged':True,'simulator_unchanged':True})
ARCHIVE.mkdir()
for p in OUT.iterdir():
    if p.is_file() and p.suffix in ('.py','.json','.log','.txt','.diff'):
        name=('r1-input-'+p.name) if p.name in ('probe_ui_model_mutations.py','test_mutation_unusable_reason.py') else p.name
        shutil.copyfile(p,ARCHIVE/name)
preparation=json.loads((OUT/'preparation-inputs.json').read_text())
for name in TOOLS+list(preparation):
    dest=ARCHIVE/'frozen-overlay'/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/name,dest)
for name in ('build','run'):
    dest=ARCHIVE/'retained-selftest'/f'{name}.log';dest.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(ROOT/'.pio/mutation-unusable/selftest'/f'{name}.log',dest)
shutil.copyfile(ROOT/'docs/superpowers/evidence/2026-09-20-radmin-slice10-qa/mut.sh',ARCHIVE/'qa-union-selectors.sh')
shutil.copyfile(RECEIPT,ARCHIVE/'revision-1-receipt.md')
(ARCHIVE/'candidate-harness-vs-base.diff').write_bytes(subprocess.check_output(['git','diff','--',TOOLS[0]],cwd=ROOT))
freeze_time=datetime.datetime.now(datetime.timezone.utc).isoformat()
hashes={n:gate.file_hash(ROOT/n) for n in TOOLS}
new_section=f'''
## 6. Revision-2 implementation freeze — ready for independent QA (2026-09-20)

**Coder gate PASS.** This section supersedes revision 1's HOLD for the current candidate; §§1–5 and the previous archive retain the failed attempt. B434, B435 and B436 await **independent QA** closure. Freeze timestamp: `{freeze_time}`.

**Source-validation:** base `e680271791397dbdaa3a7a34b368ebc8851e60a2`; simulator `6585649ea5a780f0542b2931853a667be56a5b2b`, clean. Authorized revision-2 brief SHA-256 **`f6eafd394894fd65512fcd66ba9613ff7c94308763de3ef4af486daffa3a1048`** matched. Both tool files matched the revision-1 freeze before editing. The changed QA inputs were the brief, register, MEMORY and tracker; all four were inventoried as permitted preparation and remained unchanged during this implementation and gate. The prior checkpoint, untracked evidence and all other inputs were preserved.

**Exact correction:** `run_suite` now invokes `pio test -e native --without-testing`, then executes the native binary once. This is the **only harness change from the preserved revision-1 candidate**. The strict actual-build return-code check, full captured output, failure classification/retention and existing binary-summary verdict remain intact. The existing success-verdict unit test now also checks the two subprocess commands; seven tests remain, with no deletion. A temporary-copy control removing only `--without-testing` fails that test (exit 1, one failure) with an empty PATH and no compiler invocation.

**Real B436 regression:** in this fresh full union, the unchanged `radmin8node` N01 is **1 RED / 0 unusable**, exit **0**, with **22 failed assertions**. The revision-1 misclassification is gone. This result comes from the real compiled mutant, not from the subprocess double.

PIN re-synced? YES — 2950/195768 + Slice 10's two assertions and zero cases = **2950/195770**. All **{union['worker_baselines_verified']}** fresh worker baselines independently derived **2950 / 195770 / 0**; all 61 candidate battery logs contain **zero** `re-pin PIN_CASES` banner lines. The B217 advisory-banner code and exit policy remain unchanged.

| Required instrument | Fresh measured result |
| --- | --- |
| Whitespace | `git diff --check` PASS |
| `--where` | PASS; unchanged five-field output shape |
| `--selftest-unusable` | PASS, exit 0; distinct build/run labels, exact excerpts and complete short captures retained |
| Focused discovery | **7 tests PASS**; both required executed controls RED |
| B436 removed-build-only control | **RED**, one failed existing verdict test; no build |
| Full tools discovery | **356 tests / 0 failures / 0 skipped**, exit 0 (**349 prior + 7 new**) |
| Full required union | **61 batteries / 983 RED / 1 known B342 / 984 configured / 0 vacuous** |
| Known B342 | Only `sliceBmac` **M04**; compiled survivor is printed as `FAIL` and counted in the existing “unusable” total; battery exit 1 as expected |
| Other battery exits | All other **60 exit 0**; no new unusable outcome or missing entry |
| B217 banners | **0 lines** across all 61 full logs |
| Worker baselines and restoration | **{union['worker_baselines_verified']}** at **2950/195770/0**; every worker's source restore matches |
| Full mutation-stage inventory | **{len(json.loads((OUT/'stage-before.json').read_text())):,}** SHA-256 entries identical before/after |
| Shared gate input inventory | **{len(current):,}** SHA-256 entries identical before/after the gate |
| Mutation tables / target map | **108 assignments byte-identical** to `e680271`; all 984 selected patterns match once |

The exact 61 selectors were read from Slice 10 QA's `mut.sh`, not reconstructed from the current file fence. Each battery was requested with **`--workers=3`**; the existing scheduler naturally uses fewer for smaller batteries. Three isolated batteries ran concurrently; discovery used the shared frozen source and its own temporary directory. The mutation rsync stage included the complete uncommitted and untracked candidate/preparation, with the same generated-directory exclusions as QA's script. Every selected entry is reconciled against the final merged report, not just the summary total. No failed or interrupted revision-1 run contributes to these figures; no battery was retried in this gate.

Per brief §5 this was **tools-only**: no standalone native, simulator/corpus, ABI, census or board run. Native compilation/execution occurred inside the required mutation instrument. No production/test/wire/simulator files changed. No source patterns, labels, mutation entries, worker formula, scratch lifecycle, existing RED/FAIL/VACUOUS calls or exit statements changed. QA retains the register closures and status landing; the coder changed no preparation/status document this turn.

**Frozen implementation hashes:**

- `tools/probe_ui_model_mutations.py`: `{hashes[TOOLS[0]]['sha256']}`
- `tools/test_mutation_unusable_reason.py`: `{hashes[TOOLS[1]]['sha256']}`

**Evidence:** [revision-2 freeze archive](2026-09-20-radmin-tools-b434-b435-r2-freeze/README.md) contains the preflight inventory, exact revision-1→2 diffs, fresh commands/return codes/timings, all 61 battery logs, complete discovery/self-test/control logs, per-entry union audit, before/after manifests and the full uncommitted overlay. `frozen-tree-inputs.json` covers the final tree outside that archive; `SHA256SUMS` covers every archive file except itself. The revision-1 receipt is copied into this archive before appending this section.

**Ready for independent QA on this frozen, uncommitted tree.** No staging, commit, reset or clean was performed; the simulator is unchanged. The source inputs remain frozen from here.
'''
receipt=RECEIPT.read_text()
assert '## 6. Revision-2 implementation freeze' not in receipt
receipt=receipt.replace('# B434+B435 mutation-harness tool follow-up — revision-1 coder checkpoint','# B434+B435 mutation-harness tool follow-up — coder receipts',1)
needle='\n**2026-09-20 — STOP-1 / HOLD (B436).'
assert receipt.count(needle)==1
receipt=receipt.replace(needle,'\n**Current handoff: revision 2, coder gate PASS — see §6. Sections 1–5 below retain the revision-1 HOLD checkpoint.**\n'+needle,1)
RECEIPT.write_text(receipt+new_section)
files=gate.repo_hashes();prefix=str(ARCHIVE.relative_to(ROOT))+'/'
files={k:v for k,v in files.items() if not k.startswith(prefix)}
initial=json.loads((OUT/'preflight-inputs.json').read_text())
changed=[n for n in sorted(set(initial)|set(files)) if initial.get(n)!=files.get(n)]
assert changed==sorted(TOOLS+[str(RECEIPT.relative_to(ROOT))]),changed
(ARCHIVE/'frozen-tree-inputs.json').write_text(json.dumps(files,indent=2,sort_keys=True)+'\n')
(ARCHIVE/'final-scope.json').write_text(json.dumps({'result':'CODER GATE PASS / READY FOR INDEPENDENT QA','timestamp_utc':freeze_time,'changed_from_r2_entry_excluding_evidence':changed,'QA_preparation':preparation,'implementation':hashes,'base':'e680271791397dbdaa3a7a34b368ebc8851e60a2','simulator':'6585649ea5a780f0542b2931853a667be56a5b2b','simulator_status':'clean'},indent=2,sort_keys=True)+'\n')
(ARCHIVE/'final-status.txt').write_bytes(subprocess.check_output(['git','status','--short'],cwd=ROOT))
result=subprocess.run(['git','diff','--check'],cwd=ROOT,capture_output=True,text=True)
(ARCHIVE/'final-whitespace.log').write_text(result.stdout+result.stderr+f'exit={result.returncode}\n');assert result.returncode==0
(ARCHIVE/'README.md').write_text('''# Revision-2 coder freeze — independent QA pending

See §6 of the adjacent coder receipt. All results here were rerun on revision 2.

- Base `e680271`, simulator `6585649`; authorized brief `f6eafd394894fd65512fcd66ba9613ff7c94308763de3ef4af486daffa3a1048`.
- `frozen-overlay/` contains the two final tool files and all four permitted QA documents. Apply over the named base; include the existing revision-1 checkpoint/receipt as recorded in the complete manifests. Never measure HEAD alone.
- `preflight-inputs.json` records entry; `gate-inputs-before.json` / `gate-inputs-after.json` are identical gate inventories; `stage-before.json` / `stage-after.json` are identical rsync-stage inventories.
- `frozen-tree-inputs.json` covers the final shared tree outside this archive, including the updated receipt. `final-scope.json` confirms the two tool files plus receipt are the only changes from revision-2 entry outside this archive.
- `mut_*.log` and matching JSON files contain every fresh battery command, return code and elapsed time. `union-audit.json` reconciles every configured label, baseline, restoration, exit and banner count. The only known unusable is B342 `sliceBmac` M04, a compiled survivor printed as FAIL.
- `tools-discovery.log`: 356 OK, zero skipped. `selftest.log`, `focused-discovery.log` and `retained-selftest/` cover the real classifier/retention and required controls. `b436-removed-build-only-control.*` records the additional executed B436 regression control (no build).
- `probe_ui_model_mutations.py.r1-to-r2.diff` is one argv correction; the test diff strengthens the existing verdict test. Files prefixed `r1-input-` are the preserved incoming candidate, not the final tools. `base-harness.py` is the original committed harness.
- `gate.py`, `audit_union.py` and `freeze.py` are archived evidence drivers with the original scratch paths. Do not rerun the completed freeze operation. The canonical tools remain under repository `tools/`.
- `SHA256SUMS` hashes every archive file except itself. No results from the failed revision-1 union are counted here.
''')
entries=[f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(ARCHIVE)}' for p in sorted(ARCHIVE.rglob('*')) if p.is_file() and p.name!='SHA256SUMS']
(ARCHIVE/'SHA256SUMS').write_text('\n'.join(entries)+'\n')
print(f'Frozen: {len(files)} tree inputs; {len(entries)} evidence files; full coder gate PASS; independent QA pending.')
