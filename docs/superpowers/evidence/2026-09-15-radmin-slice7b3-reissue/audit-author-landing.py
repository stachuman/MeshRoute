"""Read-only document/input audit for this author's landing; no production gate."""
from pathlib import Path
import hashlib,json,subprocess,re,stat,gzip
root=Path('/home/staszek/MeshRoute');out=Path(__file__).resolve().parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
run=lambda *cmd:subprocess.run(cmd,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,check=True).stdout.decode()
allow={'MEMORY.md','docs/2026-07-30-open-bug-register.md','docs/2026-07-31-bench-test-script.md','docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md','docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md','docs/superpowers/plans/2026-09-13-radmin-slice7b3-deferred-actions.md','docs/superpowers/plans/2026-09-13-radmin-slice7b3-0-action-busy-codec.md'}
base='ac5f9a592065d08e7cc8c06ef395e79891d41b34';assert run('git','rev-parse','HEAD').strip()==base
before=json.loads((out/'inputs-before.json').read_text());changed=[];modes=[]
for rel,old in before.items():
 p=root/rel;assert p.exists(),rel
 if sha(p)!=old['sha256']:changed.append(rel)
 if stat.S_IMODE(p.stat().st_mode)!=old['mode']:modes.append(rel)
assert set(changed)==allow,changed;assert not modes,modes
sim=Path('/home/staszek/lora-universal-simulator');sim_before=json.loads((out/'simulator-before.json').read_text())
for rel,old in sim_before.items():
 p=sim/rel;assert sha(p)==old['sha256'] and stat.S_IMODE(p.stat().st_mode)==old['mode'],rel
assert run('git','-C',str(sim),'rev-parse','HEAD').strip()=='06746a97de5764415d6fcef10b97bca90569b9c7'
assert not run('git','-C',str(sim),'status','--porcelain').strip()
assert (root/'spec/dv_dual_sf.lua').is_symlink()
assert str((root/'spec/dv_dual_sf.lua').readlink())=='../../lora-universal-simulator/scenarios/dv_dual_sf.lua'
assert not run('git','diff','--cached','--name-only').strip()
for cmd in [('git','diff','--check'),('git','diff','--cached','--check'),('git','-C',str(sim),'diff','--check')]:assert not run(*cmd).strip()
new=run('git','ls-files','--others','--exclude-standard').splitlines()
newplans={'docs/superpowers/plans/2026-09-15-radmin-slice7b3-p1-simple-action-preparation.md','docs/superpowers/plans/2026-09-15-radmin-slice7b3-reissue-precheck.md'}
assert all(p in newplans or p.startswith(str(out.relative_to(root))+'/') for p in new),new
# No whitespace normalisation of historical artifacts/raw logs; compressed logs preserve exact bytes.
textissues=[]
for rel in new:
 p=root/rel
 if p.suffix=='.gz':continue
 for n,line in enumerate(p.read_text().splitlines(),1):
  if line.rstrip(' \t')!=line:textissues.append([rel,n])
assert not textissues,textissues
ledger='docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md'
assert (root/ledger).read_text().startswith(run('git','show',base+':'+ledger))
reg='docs/2026-07-30-open-bug-register.md';rs=(root/reg).read_text()
oldreg=run('git','show',base+':'+reg)
for bid in ['B391','B393']:
 line=lambda s:next(l for l in s.splitlines() if l.startswith('| '+bid+' '))
 assert line(rs)==line(oldreg),bid
for bid in range(394,399):assert len(re.findall(r'^\| B'+str(bid)+r' ',rs,re.M))==1
assert 'next free finding is **B394**' not in rs
oldbrief=out/'brief-revision-3.md';assert sha(oldbrief)=='f7256bf12d4cf56a7dff23b0db8523470b27de4f31be2306555625a031d9cdd8'
# Direct hashes for all four frozen codec/test/tool files and all 25 enumeration inputs.
freeze={'lib/core/remote_codec.cpp':'afc81d9205ba0e1da45ee9b4d0666717405040366ffe2418383c507087ac1954','lib/core/remote_codec.h':'dcd8d09c8e223dee0d889ead240f4f01be6c47eb242b7255ba81f5189de78c1e','test/test_remote_codec.cpp':'1e5d61080edf4c0aa742a3415a1484523894a41e1509bea9af479a688ab50b95','tools/probe_ui_model_mutations.py':'08c7471b91db70dbd8deba2ae75f1fb52e254677fd2bace55e1aa374bc58e677'}
for rel,digest in freeze.items():assert sha(root/rel)==digest,rel
oldsource=json.loads((out/'typed-plan/source-audit.json').read_text())['source_hashes']
assert len(oldsource)==25
for rel,digest in oldsource.items():assert sha(root/rel)==digest,rel
rows=json.loads((out/'row-dispositions.json').read_text());assert rows['disruptive_rows']==48 and len(rows['rows'])==48
from collections import Counter
counts=Counter(r['finding'] for r in rows['rows']);assert counts=={'B394':12,'B395':23,'B396':10,'B397':2,'B398':1}
brief=root/'docs/superpowers/plans/2026-09-13-radmin-slice7b3-deferred-actions.md';p1=root/'docs/superpowers/plans/2026-09-15-radmin-slice7b3-p1-simple-action-preparation.md'
links=[]
for p in [brief,p1,root/'docs/superpowers/plans/2026-09-15-radmin-slice7b3-reissue-precheck.md',out/'README.md']:
 for label,target in re.findall(r'\[([^\]]+)\]\(([^)]+)\)',p.read_text()):
  if target.startswith(('https:','http:','app:')):continue
  assert (p.parent/target.split('#')[0]).exists(),(p,target)
  links.append({'file':str(p.relative_to(root)),'target':target})
sources=['AGENTS.md','docs/CODE_GUIDELINES.md','docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md',ledger,'docs/superpowers/plans/2026-09-08-radmin-slice7b2-session-open-status.md']
hay={p:' '.join(re.sub(r'^> ?', '',(root/p).read_text(),flags=re.M).split()) for p in sources};quotes=[]
for p in [brief,p1]:
 for block in re.findall(r'(?:^>.*\n)+',p.read_text(),re.M):
  needle=' '.join(re.sub(r'^> ?', '',block,flags=re.M).split());matches=[s for s,h in hay.items() if needle in h];assert matches,needle
  quotes.append({'file':p.name,'text':needle,'sources':matches})
warning='prep-restart stops mesh radio and remote administration. Restart the target locally to restore access; remote reboot and rollover cannot recover it while halted.'
for rel in [str(brief.relative_to(root)),sources[2],'docs/2026-07-31-bench-test-script.md']:assert warning in (root/rel).read_text()
logs=json.loads((out/'logs.json').read_text())
for name,record in logs.items():
 raw=gzip.decompress((out/record['archive']).read_bytes());assert len(raw)==record['bytes'] and hashlib.sha256(raw).hexdigest()==record['sha256'],name
preservation={'base':base,'initial_inputs':len(before),'unchanged_initial_inputs':len(before)-len(changed),'authorized_changed_documents':{p:sha(root/p) for p in sorted(changed)},'mode_changes':modes,'codec_freeze':freeze,'enumeration_sources_identical':25,'simulator_inputs_unchanged':len(sim_before),'simulator_clean':True,'original_symlink_unchanged':True,'ledger_original_prefix_unchanged':True,'B391_B393_rows_unchanged':True,'original_coder_and_QA_reports_unchanged':True,'index_empty':True,'new_paths_at_audit':new,'production_test_tool_changes':False,'full_gate_this_turn':False}
(out/'preservation.json').write_text(json.dumps(preservation,indent=2)+'\n')
checks={'dispositions':dict(counts),'disruptive_rows':48,'policy_rows':180,'quotes':quotes,'local_links':links,'warning_identical_in_three_documents':True,'prior_revision_hash_unchanged':True,'next_free':'B399','tracked_and_untracked_text_whitespace_clean':True,'lossless_logs_verified':len(logs),'B389_allocation_unruled':True,'B394_preparation_open':True,'owner_commit_observed':base}
(out/'document-audit.json').write_text(json.dumps(checks,indent=2)+'\n')
(out/'artifact-sha256.json').write_text(json.dumps({str(p.relative_to(out)):sha(p) for p in sorted(out.rglob('*')) if p.is_file() and p.name!='artifact-sha256.json'},indent=2)+'\n')
print(f'PASS: {len(before)} original inputs accounted for; {len(changed)} author documents changed, all production/test/tool and {len(sim_before)} simulator inputs unchanged; {len(quotes)} source-matched quotations; {len(links)} links; 48 dispositions; logs/whitespace/owner-prefix preserved')
