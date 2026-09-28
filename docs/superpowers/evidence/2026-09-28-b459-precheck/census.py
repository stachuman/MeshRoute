"""Read-only source census. No probe module is imported (mutation harness import runs work)."""
from pathlib import Path
import ast,hashlib,json,re,subprocess
R=Path('/home/staszek/MeshRoute');E=Path(__file__).resolve().parent
# Ranges are source facts, not executed gates. Full file SHA pins the surrounding context.
specs=[
('B462','probe_inbox_verbs','count pins','tools/probe_inbox_verbs/run.sh',[(244,272),(849,875)],'Checks 1400/483 and controls 63/71 are pinned. An isolated lost increment fails. Within one arm, missing one ordinary control and duplicating another with the same accepted outcome preserves every final count; no identity-set comparison.'),
('B463','probe_console_sink','count pins and domain-specific coverage','tools/probe_console_sink/run.sh',[(191,223),(389,458)],'Profiles, checks, structural, BLE, ownership and control counts plus name-comparison file count are pinned. These are useful coverage checks, not an executed-label bijection. Replace one ordinary control by a duplicate in the same class: counts and finalizer inputs are unchanged.'),
('B464','probe_custody_usb','count pins','tools/probe_custody_usb/run.sh',[(83,85),(277,299)],'27 positive checks, 10 controls and zero failures/unusable are pinned. Missing-plus-duplicate within the same outcome class leaves the finalizer unchanged; no executed-label set.'),
('B465','probe_ble_line','count pins','tools/probe_ble_line/run.sh',[(90,92),(297,319)],'55 checks and 12 controls pinned; count-only finalization cannot distinguish an omitted check/control compensated by a duplicate.'),
('B466','probe_features','count pins and domain-specific sets','tools/probe_features/run.sh',[(100,110),(546,572)],'9 cells, 112 binary checks and 58 controls are pinned. Ownership/envmap have real domain coverage checks; these do not reconcile all executed control identities. Same-class duplicate-plus-missing leaves the aggregate finalizer unchanged.'),
('B467','probe_device_radio','neither global executed set nor count pins','tools/probe_device_radio/run.sh',[(37,70)],'Shell checks subprocess statuses and input hashes only. Positive binary and structural counters count what executes; mutation summary uses list lengths and a failure counter. Skipping an otherwise healthy structural call/control can reduce coverage and still exit zero.'),
('B468','probe_deferred_actions','mixed: live count pins; generated controls not reconciled','tools/probe_deferred_actions/run.py',[(8,31),(128,190)],'Declaration lengths 19+21 are pinned before generating shell commands. Final live backend checks/transcripts have pins, but no comparison of completed CONTROL RED identities against those 40 declarations. A skipped generation iteration or missing generated control block leaves live baselines green; no terminal control-set check. Some source-check functions return literal counts.'),
('B469','probe_prov_tx','neither executed set nor count pins','tools/probe_prov_tx/probe.py',[], 'CHECKS and CONTROLS drive loops; rc accumulates only observed failures. The positive results dictionary is never reconciled. Skip a control iteration without editing its declaration: all remaining checks can stay green and final OK/zero remains.'),
('B470','probe_board_abi','symbol-set validation plus control count pin','tools/probe_board_abi.py',[(633,689),(960,979),(1022,1068)],'Missing/unknown ABI symbols are checked, targets are measured by keyed dictionaries, nine full-sweep controls are pinned. Control results themselves reduce to red/unusable/applicable counts, not identities: one skipped control compensated by a duplicate can satisfy 9/9. This is not a missing-symbol defect.'),
('B471','probe_b278_row_abi','target/field validation plus control count pin','tools/probe_b278_row_abi.py',[(155,177),(326,363),(401,432)],'Full target sweep is explicit, mirror field reads required and six controls pinned. Terminal control count lacks identity reconciliation: same-count duplicate-plus-missing survives. Measurement count is calculated from mirror dimensions, not an assertion ledger.'),
('B472','probe_build_identity','relative list counts, no execution identities','tools/probe_build_identity.py',[(77,122),(180,213),(262,291),(306,350)],'red is compared to len(CONTROLS)+coverage-list length(+optional ELF); ordinary omissions in the unchanged list are caught, but duplicate-plus-missing is not. Source check count is a returned constant, not observed execution. No external source-control floor.'),
(None,'probe_ui_model_mutations','selected-index/label reconciliation','tools/probe_ui_model_mutations.py',[(12380,12428),(12450,12467),(12520,12536)],'Default parent reconciles selected indices and labels, detects missing and duplicate results, invalid index/label, non-verdict worker exits and restoration/integrity failures. Non-RED selected verdicts fail. No B459-class finding in the requested default merge census; no mutation battery run here. Stale native cross-check pin is deliberately warning-only and separate from this completeness question.')]
rows=[]
for bug,name,kind,file,ranges,conclusion in specs:
 p=R/file;s=p.read_text();lines=s.splitlines()
 if name=='probe_prov_tx':
  t=ast.parse(s);fn=next(n for n in t.body if isinstance(n,ast.FunctionDef) and n.name=='main');ranges=[(fn.lineno,fn.end_lineno)]
 excerpts=[{'start':a,'end':min(b,len(lines)),'text':'\n'.join(f'{i+1}: {lines[i]}' for i in range(a-1,min(b,len(lines))))} for a,b in ranges]
 rows.append(dict(finding=bug,runner=name,classification=kind,file=file,sha256=hashlib.sha256(p.read_bytes()).hexdigest(),basis='STATIC SOURCE ANALYSIS; not a full runner execution or runtime fault reproduction',conclusion=conclusion,excerpts=excerpts))
# Additional child layers named in the conclusions, with full pinned source excerpts.
extra={}
for file in ['tools/probe_device_radio/structural.py','tools/probe_device_radio/mutations.py','tools/probe_device_radio/probe_main.cpp','tools/probe_device_radio/board_rf_probe.cpp','tools/probe_features/envmap.py','tools/probe_features/ownership.py']:
 p=R/file;s=p.read_text();ls=s.splitlines();selected=[]
 for i,l in enumerate(ls):
  if re.search(r'passed|failed|failures|return 1 if|controls checked|def check|def main|n_checks|n_fail|CHECK|CHK|check\("E11"',l):selected.append({'line':i+1,'text':l})
 extra[file]={'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'relevant_lines':selected}
(E/'runner-census.json').write_text(json.dumps({'scope':'Q10 requested static census; no other runner is folded into B459','runners':rows,'child_layers':extra},indent=2)+'\n')
# Textual reader audit, including comments and evidence consumers. No mutation of any search target.
patterns=[r'probe_board_ui/(run\.sh|probe_main\.cpp|negctl\.py)',r'probe_firmware_ui/run\.sh|expected_controls|record_verdict|account_controls|controls_final|selftest-accounting',r'Control \(c\).*wrapper|W50.*Control \(c\)']
readers=[]
for pat in patterns:
 p=subprocess.run(['rg','-n','--hidden','-g','!.git/**','-g','!docs/superpowers/evidence/2026-09-28-b459-precheck/**',pat,'.'],cwd=R,capture_output=True,text=True)
 readers.append({'pattern':pat,'rg_exit':p.returncode,'hits':p.stdout.splitlines(),'stderr':p.stderr})
raw=R/'artifacts/2026-09-28-b459-precheck/reader-search-full.json'
raw.write_text(json.dumps(readers,indent=2)+'\n')
for block in readers:
    block['hits']=[h if len(h)<=360 else h[:360]+' [excerpt truncated; complete text in raw search log]' for h in block['hits']]
(E/'reader-search.json').write_text(json.dumps(readers,indent=2)+'\n')
print('12 runners examined statically; 11 accounting findings, distinct from prior runtime failures')
