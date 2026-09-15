from pathlib import Path
import hashlib,json,stat,subprocess,re,gzip,shutil
q=Path(__file__).resolve().parent;r=Path('/home/staszek/MeshRoute');sim=Path('/home/staszek/lora-universal-simulator');e=r/'docs/superpowers/evidence/2026-09-13-radmin-slice7b3-0-precheck'
brief=r/'docs/superpowers/plans/2026-09-13-radmin-slice7b3-0-action-busy-codec.md';pre=r/'docs/superpowers/plans/2026-09-13-radmin-slice7b3-0-precheck.md'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def manifest(root):
 paths=subprocess.check_output(['git','ls-files','-z','--cached','--others','--exclude-standard'],cwd=root).decode().split('\0')
 return {rel:{'sha256':sha(root/rel),'mode':stat.S_IMODE((root/rel).stat().st_mode)} for rel in sorted(set(paths)-{''}) if (root/rel).is_file()}
before=json.loads((q/'inputs-before.json').read_text());current=manifest(r);oldsim=json.loads((q/'simulator-before.json').read_text());assert manifest(sim)==oldsim
changed={rel:{'before':data,'after':current.get(rel)} for rel,data in before.items() if current.get(rel)!=data}
allowed={'MEMORY.md','docs/2026-07-30-open-bug-register.md','docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md'}
assert set(changed)==allowed,changed
new=sorted(set(current)-set(before));prefix=e.relative_to(r).as_posix()+'/'
assert all(p.startswith(prefix) or p in {brief.relative_to(r).as_posix(),pre.relative_to(r).as_posix()} for p in new)
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=r).decode().strip()=='b9d75aaca55a0e350d9707512e9b6eec8a81ab22'
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=sim).decode().strip()=='06746a97de5764415d6fcef10b97bca90569b9c7'
for root in (r,sim):subprocess.run(['git','diff','--check'],cwd=root,check=True);subprocess.run(['git','diff','--cached','--check'],cwd=root,check=True)
text=brief.read_text();groups=re.findall(r'(?:^>[^\n]*(?:\n|$))+',text,re.M)
sources=[r/'docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md',r/'docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md',r/'docs/2026-09-02-agent-roles.md',r/'docs/superpowers/plans/2026-09-08-radmin-slice7b1-executor-transcript.md']
quotes=[]
for block in groups:
 quoted='\n'.join(line[2:] if line.startswith('> ') else line[1:] for line in block.splitlines()).strip();norm=lambda x:' '.join(x.split())
 matches=[str(p.relative_to(r)) for p in sources if norm(quoted) in norm(p.read_text())];assert matches,quoted
 quotes.append({'quote':quoted,'sources':matches})
links=[]
for p in [brief,pre,e/'README.md']:
 for target in re.findall(r'\]\(([^)]+)\)',p.read_text()):
  if '://' in target or target.startswith('#'):continue
  dest=(p.parent/target.split('#')[0]);assert dest.exists(),(p,target);links.append({'document':str(p.relative_to(r)),'target':target})
for rel in sorted(set(new)|allowed):
 p=r/rel
 if p.suffix in ('.md','.cpp','.py','.json'):
  lines=p.read_text().splitlines();bad=[i+1 for i,l in enumerate(lines) if l.rstrip()!=l]
  # Existing files' historical whitespace is governed by git diff --check.
  if rel in new:assert not bad,(rel,bad)
for name,meta in json.loads((e/'raw-logs.json').read_text()).items():assert hashlib.sha256(gzip.decompress((e/meta['artifact']).read_bytes())).hexdigest()==meta['uncompressed_sha256'],name
ci=json.loads((e/'corpus-identity.json').read_text())
assert ci['lus_sha256']=='862241173963b3c30a501849dea9e5e6184227575225aa8e263f0f5c48d1170a'
assert ci['baseline_sha256']=='71f140988e9d1a5bf1be49dd1feb8fc7ebb8e46c7a4b119e16db62f0df76962f'
assert len(json.loads((e/'independent-boundary-vectors.json').read_text())['proposed_additions'])==5
assert (r/'docs/2026-07-30-open-bug-register.md').read_text().count('| B393 ')==1
assert 'next free B393' not in (r/'MEMORY.md').read_text()
assert 'The next free finding is **B394**' in (r/'docs/2026-07-30-open-bug-register.md').read_text()
report={'result':'PASS','brief_sha256':sha(brief),'precheck_sha256':sha(pre),'verified_quotes':quotes,'verified_links':links,'raw_logs_verified':len(json.loads((e/'raw-logs.json').read_text())),'new_file_whitespace':'PASS','tracked_and_staged_whitespace_both_repositories':'PASS','B393_unique_and_next_B394':True}
(e/'document-validation.json').write_text(json.dumps(report,indent=2)+'\n')
preserved={'result':'PASS','baseline_inputs':len(before),'old_inputs_unchanged':len(before)-len(changed),'only_changed_existing_inputs':changed,'source_test_tool_simulation_production_changes':[],'simulator_inputs_preserved':len(oldsim),'owner_ledger_preserved':True,'behavior_revision3_preserved':True,'all_prior_coder_receipts_preserved':True,'no_git_index_change':not subprocess.check_output(['git','diff','--cached','--name-only'],cwd=r).strip(),'new_inputs_at_validation':new,'new_input_scope':prefix+' plus codec brief and pre-check; final artifact index inventories all evidence files'}
(e/'preservation.json').write_text(json.dumps(preserved,indent=2)+'\n')
print('PASS: quotes',len(quotes),'links',len(links),'old inputs unchanged',len(before)-len(changed),'simulator',len(oldsim),'only existing edits',sorted(changed))
