from pathlib import Path
import subprocess,json,hashlib,os,tarfile,re
r=Path('/home/staszek/MeshRoute');q=Path('/tmp/mr-s9-active').read_text().strip();q=Path(q);rel='docs/superpowers/evidence/2026-09-19-radmin-slice9-r5-freeze';out=r/rel
sha=lambda b:hashlib.sha256(b).hexdigest()
def save(p,v):p.write_text(json.dumps(v,indent=2,sort_keys=True)+'\n')
def git(*args,cwd=r):return subprocess.check_output(['git',*args],cwd=cwd)
results=json.loads((q/'results.json').read_text());assert {x['name'] for x in results}=={'pair','tools','census'}
for row in results:assert row['exit']==0 and sha((q/'logs'/(row['name']+'.log')).read_bytes())==row['sha256']
s=(q/'logs/tools.log').read_text();assert re.search(r'^Ran 349 tests ',s,re.M) and re.search(r'^OK$',s,re.M);assert ' ... skipped' not in s
assert (q/'logs/census.log').read_text().endswith('PASS — 6 OLED env(s) match their pinned warning baseline\n')
assert sha((r/'docs/superpowers/plans/2026-09-18-radmin-slice9-legacy-deletion-and-protocol-docs.md').read_bytes())=='6eb070f1ada27081e96d84564a0655a53e3b011f71a43a03109a1aebcb2a2b9e'
sim=Path('/home/staszek/lora-universal-simulator');assert git('rev-parse','HEAD').decode().strip()=='84edd3ebfb08d807645d077269bace145ac2a2ab';assert git('rev-parse','HEAD',cwd=sim).decode().strip()=='6585649ea5a780f0542b2931853a667be56a5b2b';assert not git('status','--porcelain=v1',cwd=sim)
for root in [r,q/'tools',q/'boards']:assert not git('diff','--cached','--name-status',cwd=root)
(out/'README.md').write_text(Path('/tmp/mr-s9-r5-readme.md').read_text())
receipt=r/'docs/superpowers/evidence/2026-09-18-radmin-slice9.md';text=receipt.read_text();assert '## 11. Revision-5 implementation freeze' not in text;receipt.write_text(text+Path('/tmp/mr-s9-r5-final-receipt.md').read_text())
p=r/'docs/2026-07-30-open-bug-register.md';s=p.read_text();a=s.index('**Next dispatch — the Slice 9 brief');b=s.index('**8b (the mobile controller carrier)',a)
s=s[:a]+'''**Slice 9 revision 5 — CODER FREEZE READY FOR INDEPENDENT QA (2026-09-19).** Base `84edd3e` / simulator
`6585649`, brief SHA-256 `6eb070f1…`. The three fenced returns are implemented: B426's surviving-surface/absence
unit proof; B427's paired six-cell warning re-pin; B425's stock-tool deletion markers and fail-loud controls.
Fresh owner-requested reruns: **tools 349/349, zero failures/skips; census 171/175/175/175/179/179, zero
-Wswitch; stock pair gateway 203820 RAM /572224 flash, mobile 211764 /1394520**, with all eight deletions
recorded and no index/PATH workaround. B428's 87/15 fixture passes full discovery. Production/native/reference/
corpus/ABI/probe/mutation inputs are hash-identical to the prior chain; inherited results remain native
**2950/195768/0**, corpus **36/36 byte-identical**, Node unchanged, inventory **197**, union **61 batteries /
983 RED /1 known B342 /984 /0 vacuous**. Linked deltas remain gateway **−216 RAM /−2784 flash**, mobile
**−8 /−756**. B429 is a non-blocking counting typo in the frozen brief for the QA landing. Independent QA
and Part 57e on metal remain pending. Complete uncommitted inputs, fresh logs and the stock ELFs are archived
in [receipt §11](superpowers/evidence/2026-09-18-radmin-slice9.md#11-revision-5-implementation-freeze--ready-for-independent-qa-2026-09-19).
Nothing staged or committed; simulator clean. Then the Slice 10 author brief after independent PASS.

''' +s[b:]
s=s.replace('**OPEN / TOOL — OWNED BY SLICE 9 (brief revision 5 §1/§6: a deletion-marker arm in `measure_board.py::source_snapshot` with a fail-loud control); closes at the 9 gate**','**IMPLEMENTED / CODER FREEZE 2026-09-19 — INDEPENDENT QA PENDING**')
updates={'B425':'Revision-5 implementation: stock source snapshots record all eight unstaged tracked deletions with a presence-tagged hash and manifest list; real listed untracked/tracked disappearances still refuse. Three private controls fail as intended. Full tools 349/349 and stock board pair PASS without wrapper or staging. Independent closure awaits QA (receipt §11).','B426':'Revision-5 implementation: the surviving-surface assertion and inventory-wide legacy-surface absence check are landed; all eight authority tests pass inside fresh tools 349/349. Independent implementation gate pending (receipt §11).','B427':'Revision-5 implementation: runner and §B87 are re-pinned together; fresh six-cell census PASS at 171/175/175/175/179/179, zero -Wswitch. Independent implementation gate pending (receipt §11).','B428':'Revision-5 full tools discovery is now 349/349 with zero failures/skips; the repaired arithmetic passes in the full sweep. Independent closure awaits QA (receipt §11).'}
lines=s.splitlines()
for idx,line in enumerate(lines):
 for code,note in updates.items():
  if line.startswith('| '+code+' '):
   # Preserve the historical account; append the new implementation evidence in place.
   assert line.endswith(' |');lines[idx]=line[:-2]+' **'+note+'** |'
p.write_text('\n'.join(lines)+'\n')
primary=json.loads((q/'frozen-primary-inputs.json').read_text())
for root in [r,q/'tools',q/'boards']:
 for name,v in primary.items():
  f=root/name
  if v.get('deleted'):assert not f.exists(),(root,name)
  elif 'sha256'in v:assert sha(f.read_bytes())==v['sha256'],(root,name)
old=json.loads((q/'tools-inputs.json').read_text());delta=[]
for name,v in old.items():
 if 'sha256' in v:
  h=sha((r/name).read_bytes())
  if h!=v['sha256']:delta.append({'path':name,'tested':v['sha256'],'freeze':h})
assert {x['path'] for x in delta}=={'docs/2026-07-30-open-bug-register.md','docs/superpowers/evidence/2026-09-18-radmin-slice9.md'}
save(q/'post-run-report-delta.json',delta)
save(q/'gate-summary.json',{'disposition':'coder freeze ready for independent QA','fresh':{'tools':{'cases':349,'failed':0,'skipped':0},'census':{'pins':[171,175,175,175,179,179],'switch_warnings':0},'stock_pair':'PASS; eight deletion markers; no wrapper/index override/staging'},'inherited_from_revision4':'native, references, corpus, ABI, probes, inventory, xiao, mutation union; see input proof and receipt §11','primary_inputs_preserved':len(primary),'independent_QA':'pending'})
for root in [r,sim]:subprocess.run(['git','diff','--check'],cwd=root,check=True)
subprocess.run(['python3','/tmp/mr-s9-r5-archive.py'],check=True)
# Full overlay: all dirty tracked and prior untracked inputs, excluding only this archive's own directory.
names=set(os.fsdecode(p) for p in git('diff','--name-only','-z','HEAD').split(b'\0') if p)
names.update(os.fsdecode(p) for p in git('ls-files','--others','--exclude-standard','-z').split(b'\0') if p)
names={n for n in names if not n.startswith(rel+'/')};deleted=[];overlay={}
with tarfile.open(out/'freeze-overlay.tar.xz','w:xz',preset=3) as tar:
 for name in sorted(names):
  f=r/name
  if not f.exists() and not f.is_symlink():deleted.append(name);continue
  tar.add(f,arcname=name,recursive=False);overlay[name]={'symlink':os.readlink(f)} if f.is_symlink() else {'sha256':sha(f.read_bytes()),'size':f.stat().st_size}
assert len(deleted)==8;save(out/'freeze-deletions.json',deleted);save(out/'overlay-inputs.json',overlay)
with tarfile.open(out/'freeze-overlay.tar.xz','r:xz') as tar:
 assert set(tar.getnames())==set(overlay)
 for name,v in overlay.items():
  if 'sha256'in v:assert sha(tar.extractfile(name).read())==v['sha256'],name
  else:assert tar.getmember(name).linkname==v['symlink']
exclude={rel+'/freeze-inputs.json',rel+'/artifact-sha256.json'};inputs={}
names=set(os.fsdecode(p) for p in git('ls-files','--cached','--others','--exclude-standard','-z').split(b'\0') if p)
for name in sorted(names-exclude):
 f=r/name
 if f.is_symlink():inputs[name]={'symlink':os.readlink(f)}
 elif f.is_file():inputs[name]={'sha256':sha(f.read_bytes()),'size':f.stat().st_size}
 else:inputs[name]={'deleted':True}
save(out/'freeze-inputs.json',{'base':'84edd3ebfb08d807645d077269bace145ac2a2ab','simulator':'6585649ea5a780f0542b2931853a667be56a5b2b','brief_sha256':'6eb070f1ada27081e96d84564a0655a53e3b011f71a43a03109a1aebcb2a2b9e','disposition':'coder freeze ready for independent QA','excluded_circular_metadata':sorted(exclude),'inputs':inputs})
artifacts={str(p.relative_to(out)):sha(p.read_bytes()) for p in sorted(out.rglob('*')) if p.is_file() and p.name!='artifact-sha256.json'};save(out/'artifact-sha256.json',artifacts)
print('FROZEN',len(inputs),'input records;',len(overlay),'overlay files;',len(deleted),'deletions;',len(artifacts),'artifacts')
