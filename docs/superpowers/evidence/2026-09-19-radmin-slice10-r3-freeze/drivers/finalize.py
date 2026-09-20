from pathlib import Path
import subprocess,json,hashlib
q=Path(__file__).parent;r=Path('/home/staszek/MeshRoute');sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
subprocess.run(['python3',str(q/'audit-results.py')],check=True)
brief=r/'docs/superpowers/plans/2026-09-19-radmin-slice10-main-nv-cleanup.md';assert sha(brief)=='5eab4af5cd08666368a58a0196bb034e7a8822b563175378b519fc136de71e9b'
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=r).decode().strip()=='4ad9c343b42bab4c91bd8e6721b45b2dfb769e7f'
receipt=r/'docs/superpowers/evidence/2026-09-19-radmin-slice10.md';s=receipt.read_text();assert '## 8. Revision 3 implementation freeze' not in s;receipt.write_text(s+(q/'final-receipt.md').read_text())
p=r/'docs/2026-07-30-open-bug-register.md';s=p.read_text();a=s.index('**Current dispatch — Slice 10 brief REVISION 3');b=s.index('\n\n',a)
s=s[:a]+'''**Slice 10 revision 3 — CODER FREEZE READY FOR INDEPENDENT QA (2026-09-19).** Base `4ad9c34` / simulator
`6585649`, frozen brief SHA-256 `5eab4af5…`. The preserved v26 candidate now includes the exact B431 custody
size-pin and B432 guard-census repairs. **The entire §8 chain is freshly rerun; nothing is inherited from the
interrupted revision-2 run.** Native **2950/195770/0/0 skipped**, both references PASS, corpus **36/36 actual
byte-identical** (s18 `32afbf11`), ABI **235208/122176/157304**, probes at their pins, tools **349/0 failures/0
skips**, inventory **197** (one source-location hint regenerated, zero semantic changes), authority/checkers
PASS, census **171/175/175/175/179/179**, zero -Wswitch. Fresh stock pair: gateway **203740 RAM /572240 flash
(−80/+16)**, mobile **211724/1394704 (−40/+184)**; XIAO **176556/699548**. Union **61 batteries /983 RED /1
known B342 /984 configured /0 vacuous**. B433 records my first discovery's ordering error (one stale inventory
anchor); the full discovery was rerun after generation and is green. **B430–B432 await independent QA closure;
Part 57f is metal NOT RUN.** No owner ruling, commit or additional production repair was needed. Complete
uncommitted inputs and fresh evidence: [receipt §8](superpowers/evidence/2026-09-19-radmin-slice10.md#8-revision-3-implementation-freeze--ready-for-independent-qa-2026-09-19).
Nothing staged or committed; simulator clean and unchanged.''' +s[b:]
assert '| B433 ' not in s;s=s.replace('**B433**','**B435**')
lines=s.splitlines()
notes={'B430':'Revision-3 coder freeze: both expanded instruments pass the fresh full chain (console 84/152; inbox 1394/477 and 61/69). Independent QA closure remains pending; receipt §8.','B431':'Revision-3 coder freeze: the exact one-literal/comment repair is landed, every custody-specific assertion preserved; fresh native 2950/195770/0. Independent QA closure pending; receipt §8.','B432':'Revision-3 coder freeze: the exact two-entry/comment repair is landed; fresh feature matrix 9/112/58 and ownership controls 43/0, with comparisons, controls and pins unchanged. Independent QA closure pending; receipt §8.'}
for i,line in enumerate(lines):
 for code,note in notes.items():
  if line.startswith('| '+code+' '):
   assert line.endswith(' |');lines[i]=line[:-2]+' **'+note+'** |'
s='\n'.join(lines)+'\n';s+='''
| B433 (S10-G1) | **CORRECTED IN CODER GATE 2026-09-19 — INDEPENDENT QA PENDING** | The coder launched full tools discovery before the required inventory regeneration completed. `test_gen_command_inventory.TestRealTree.test_tracked_table_equals_fresh_generation` saw the old `peerkey` / `service_console` hint `fw_main.cpp:1215`, while the preserved implementation puts that site at 1214: **349 run /348 passed /1 failed /0 skipped**. The mandatory `--write` output changes that one source-location reference, no semantic row (197 unchanged); P4 makes this an ordinary anchor relocation, not STOP-1 or a third instrument repair. The generated document is landed, the failed log retained, and **all 349 discovery tests rerun after generation: 349 passed /0 failed /0 skipped**. No test, runner or production fix was made for this failure. Gate ordering must keep a generated input's writer before its readers. [Receipt §8](superpowers/evidence/2026-09-19-radmin-slice10.md#8-revision-3-implementation-freeze--ready-for-independent-qa-2026-09-19). |
''';s+='''
| B434 (S10-G2) | **RECORD ONLY — NON-GATING MUTATION CROSS-CHECK (2026-09-19)** | Slice 10 adds two native assertions. The unchanged mutation harness's B217 advisory literal remains `PIN_CASES, PIN_ASSERTS = 2950, 195768`; every fresh worker instead derives **2950/195770/0** and the tool prints its intended discrepancy banner without shortening the selection. Measured full union **61 batteries /983 RED /1 known B342 /984 configured /zero vacuous**. The strict clean-baseline gate passes, no check/control is lost, and the advisory is not counted as an unusable mutation. Brief §5 permits a harness edit only if a `devicenv` pattern moved; all 984 match exactly once, so no harness edit was made under the owner's exact-two-repair instruction. Recorded for QA visibility; no owner ruling or additional implementation change is required for this freeze. [Receipt §8](superpowers/evidence/2026-09-19-radmin-slice10.md#8-revision-3-implementation-freeze--ready-for-independent-qa-2026-09-19). |
''';p.write_text(s)
p=r/'docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md';s=p.read_text();old='is implemented on the preserved candidate pending the two fenced repairs and the fresh full chain — the last slice';assert old in s;s=s.replace(old,'has a fresh full coder freeze ready for independent QA — the last slice');lines=s.splitlines(True)
for i,line in enumerate(lines):
 if line.startswith('| 10 | **REVISION 3 BRIEF'):
  lines[i]='''| 10 | **REVISION 3 CODER FREEZE READY FOR INDEPENDENT QA (2026-09-19)**, base `4ad9c34` / simulator `6585649`, brief SHA-256 `5eab4af5…`; [brief](../plans/2026-09-19-radmin-slice10-main-nv-cleanup.md), [receipt §8](../evidence/2026-09-19-radmin-slice10.md#8-revision-3-implementation-freeze--ready-for-independent-qa-2026-09-19): `/mrcfg` version/floor 26, Blob 240/8/236; legacy Node mirrors/API/boot copy gone, Node 235208/122176/157304; one schema boot line, four remote-admin stores preserved. Exact B431/B432 repairs landed; every gate fresh. B430–B432 await independent closure; B433 discovery ordering corrected and full suite rerun | native 2950/195770/0; corpus 36/36 whole-file byte-identical; both references and probes PASS; tools 349/0 skips; inventory 197, one source-location hint only; census unchanged, zero -Wswitch; stock pair gateway −80 RAM /+16 flash, mobile −40 /+184, attributed; XIAO 176556/699548; union 61/983 RED/1 known B342/984, zero vacuous | **Part 57f — METAL NOT RUN:** boot over v25 prints `not loaded`, four store reports unchanged, `cfg` has no admin fields, one `cfg set` later the next boot prints `loaded`, and a v2 `remote` round trip works |
''';break
else:raise AssertionError('current design row not found')
p.write_text(''.join(lines))
p=r/'MEMORY.md';s=p.read_text();old='''(standalone NV cleanup, R-RA-6; rev 3 after B430–B432, 2026-09-19; `/mrcfg` v26, Part 57f) is implemented on the
  preserved candidate — coder lands two fenced repairs + fresh full chain, then my gate''';new='''(standalone NV cleanup, R-RA-6; rev 3 after B430–B432, 2026-09-19; `/mrcfg` v26, Part 57f) has a fresh full
  coder freeze ready for independent QA; status/evidence in the register §0 and design §19.1''';assert old in s;p.write_text(s.replace(old,new))
p=r/'tracker.md';s=p.read_text();old='''revision-2 candidate; the coder lands the two fenced repairs and re-runs the full chain, then my independent gate.''';new='''revision-2 candidate with the two fenced repairs and the whole chain freshly rerun; coder freeze ready for independent QA (register §0 / design §19.1 / receipt §8).''';assert old in s;s=s.replace(old,new).replace('remote-admin Slice 10: coder source-validation → implementation → independent gate (the last slice)','remote-admin Slice 10: independent gate on the fresh revision-3 coder freeze (the last slice)');p.write_text(s)
# Named report-only deltas from the frozen gate inputs; generated inventory is recorded separately.
old=json.loads((q/'final-gate-inputs.json').read_text());delta=[]
for name,v in old.items():
 if 'sha256' in v and sha(r/name)!=v['sha256']:delta.append({'path':name,'pre_gate':v['sha256'],'freeze':sha(r/name)})
expected={'MEMORY.md','tracker.md','docs/2026-07-30-open-bug-register.md','docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md','docs/superpowers/evidence/2026-09-19-radmin-slice10.md','docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md'}
assert {x['path'] for x in delta}==expected,delta
(q/'post-run-report-delta.json').write_text(json.dumps(delta,indent=2)+'\n')
for root in [r,Path('/home/staszek/lora-universal-simulator')]:subprocess.run(['git','diff','--check'],cwd=root,check=True)
assert sha(brief)=='5eab4af5cd08666368a58a0196bb034e7a8822b563175378b519fc136de71e9b'
subprocess.run(['python3',str(q/'archive.py')],check=True)
