from pathlib import Path
import hashlib,json,os,re,shutil,subprocess
R=Path('/home/staszek/MeshRoute');O=R/'artifacts/2026-10-02-b478-b487-b488-qa-regate';E=R/'docs/superpowers/evidence/2026-10-02-b478-b487-b488-qa-regate';B=json.loads((O/'inputs.json').read_text());sha=lambda d:hashlib.sha256(d).hexdigest()
names=set(subprocess.check_output(['git','ls-files','-c','-o','--exclude-standard','-z'],cwd=R).decode().split('\0'))-{''}
def state(root,p):
 f=root/p;return {'symlink':os.readlink(f)} if f.is_symlink() else {'sha256':sha(f.read_bytes()),'bytes':f.stat().st_size}
missing=sorted(B['files'].keys()-names);new=sorted(names-B['files'].keys());changed=sorted(p for p in B['files'] if state(R,p)!=B['files'][p])
allowed=sorted(['docs/2026-07-30-open-bug-register.md','tracker.md','MEMORY.md','docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md']);assert not missing and changed==allowed,changed
assert all(p==str(E.with_suffix('.md').relative_to(R)) or p.startswith(str(E.relative_to(R))+'/') for p in new),new
S=Path('/home/staszek/lora-universal-simulator');assert all(state(S,p)==v for p,v in B['simulator']['files'].items());assert not subprocess.check_output(['git','status','--porcelain=v1'],cwd=S)
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=R).decode().strip()==B['head'];assert not subprocess.check_output(['git','diff','--cached','--name-only'],cwd=R);assert subprocess.run(['git','diff','--check'],cwd=R).returncode==0
# Every historically frozen evidence input, brief and final implementation file was in B and remains identical.
landing={'verdict':'PASS','entry_inputs':len(B['files']),'unchanged_entry_inputs_except_named_status_landing':True,'changed':changed,'missing':missing,'new_QA_paths_before_seal':new,'final_status_hashes':{p:sha((R/p).read_bytes()) for p in changed},'head':B['head'],'simulator_head':B['simulator']['head'],'simulator_clean_and_inputs_unchanged':True,'index_empty':True,'whitespace_clean':True,'brief_and_coder_evidence_unchanged':True,'tool_product_test_inputs_unchanged':True}
(E/'qa-landing.json').write_text(json.dumps(landing,indent=2)+'\n');shutil.copy2(O/'land_docs.py',E/'land_docs.py');shutil.copy2(O/'seal.py',E/'seal.py')
# Compact independently verified outcome contracts from the passing Part B instrument.
(E/'part-b-controls.json').write_text(json.dumps({'instrument':'tools/test_probe_inbox_transcript.py, fresh 24-test QA run; assertions inspected in the hashed source','suite_exit':0,'B-C0':'empty/truncated valid-looking pairs accepted only when expected-matrix check is bypassed; regression RED','B-C1':{'over_seven_refused':164,'compare_exit':1,'affected_reference_rows':['L003','L201','L202']},'B-C2':{'compare_exit':1,'only_commands':['regen','whoami']},'B-C3':{'tool_exit':2,'child_exit':0,'named_invalid_profile':True},'B-C4':{'three_record_edits':1,'removed_or_duplicate_row':2},'B-C5':{'tool_exit':2,'child_exit':4,'named_unrepresentable_1024_byte_input':True},'B-C6':{'tool_exit':2,'required_compiler_diagnostic':'mrble undeclared'},'B491_fresh_driver':{'tool_exit':2,'child_exit':0,'named_bare_field_refusal':True}},indent=2)+'\n')
# Resolve every local link added to the report, and preserve the original statement of gate scope.
report=E.with_suffix('.md');links=[]
for dest in re.findall(r'\]\(([^)]+)\)',report.read_text()):
 if dest.startswith(('http:','https:','#')):continue
 target=(report.parent/dest.split('#',1)[0]);assert target.exists(),target;links.append(dest)
# Refresh raw hash index after all QA recipes have landed.
raw=[{'path':str(p.relative_to(R)),'bytes':p.stat().st_size,'sha256':sha(p.read_bytes())} for p in sorted(O.rglob('*')) if p.is_file()]
(E/'raw-files.json').write_text(json.dumps(raw,indent=2)+'\n')
files=sorted(p for p in E.iterdir() if p.is_file() and p.name!='SHA256SUMS');lines=[sha(p.read_bytes())+'  '+p.name for p in files];lines.append(sha(report.read_bytes())+'  ../'+report.name);(E/'SHA256SUMS').write_text('\n'.join(lines)+'\n')
for line in lines:
 h,p=line.split(None,1);assert sha((E/p).read_bytes())==h,p
print('QA PASS seal:',len(lines),'entries; report:',sha(report.read_bytes()),'seal:',sha((E/'SHA256SUMS').read_bytes()))
print('Status changes:',changed,'new finding B496; next free B497; all other frozen inputs preserved')
