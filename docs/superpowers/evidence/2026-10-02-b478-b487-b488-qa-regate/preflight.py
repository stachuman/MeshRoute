from pathlib import Path
import hashlib,json,os,re,subprocess
R=Path('/home/staszek/MeshRoute');S=Path('/home/staszek/lora-universal-simulator');O=R/'artifacts/2026-10-02-b478-b487-b488-qa-regate';E=R/'docs/superpowers/evidence';B=R/'docs/superpowers/plans/2026-09-30-b478-b487-b488-tool-repairs.md'
sha=lambda b:hashlib.sha256(b).hexdigest()
def inv(root):
 names=sorted(set(subprocess.check_output(['git','ls-files','-c','-o','--exclude-standard','-z'],cwd=root).decode().split('\0'))-{''})
 return {'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root).decode().strip(),'status':subprocess.check_output(['git','status','--porcelain=v1'],cwd=root).decode(),'files':{p:({'symlink':os.readlink(root/p)} if (root/p).is_symlink() else {'sha256':sha((root/p).read_bytes()),'bytes':(root/p).stat().st_size}) for p in names},'staged':subprocess.check_output(['git','diff','--cached','--name-only'],cwd=root).decode()}
I=inv(R);I['simulator']=inv(S);(O/'inputs.json').write_text(json.dumps(I,indent=2)+'\n')
assert sha(B.read_bytes())=='4e6961662a3f38c039be5c069de8192f2049e8212f27cfd20e1c1fb5bc85c06d' and len(B.read_bytes().splitlines())==903
rows=[]
for row in json.loads((E/'2026-10-02-b478-b487-b488-brief-rereview-r4/pins.json').read_text())['pins']:
 p=row['path'];h=row['sha256'];src=row['source']
 data=subprocess.check_output(['git','show',('10f3332:' if src=='base' else 'HEAD:')+p],cwd=R) if src=='base' or (src=='candidate' and p=='tools/probe_ui_model_mutations.py') else (R/p).read_bytes()
 assert sha(data)==h,(p,src,sha(data),h);rows.append(row)
final={'tools/probe_ui_model_mutations.py':'27807b320427fb090840e927bf559bfff909f78761d98b29e3aff8b3cb4d177d','tools/test_mutation_unusable_reason.py':'7c04519c2f40f11fdac5fc1d55648349dd8ac10fa2237cad9313a72b186fb61d','tools/probe_inbox_verbs/transcript.py':'4dfba2a428495eca1fdb3c24e23fb96f900b548e7ac77752e46772dbc1a16429','tools/probe_inbox_verbs/transcript_main.cpp':'e5b5b51c709a5f379908f2f048f558251cf8a2ee57b9b79e8030bbbedf787863','tools/test_probe_inbox_transcript.py':'1650dcef78a2d95d26bbcb72874443cabce3cfa8a1d79227b171b78c4da7814b'}
for p,h in final.items():assert sha((R/p).read_bytes())==h,p
seals=[];approved=json.loads((E/'2026-10-02-b478-b487-b488-brief-rereview-r4/inputs.json').read_text())['files']
for stem in ['2026-09-30-b478-b487-b488-precheck','2026-09-30-b478-b487-b488-brief-review','2026-09-30-b478-b487-b488-brief-rereview','2026-09-30-b478-b487-b488','2026-09-30-b478-b487-b488/round2','2026-10-01-b478-b487-b488-qa','2026-10-02-b478-b487-b488-brief-review-r3','2026-10-02-b478-b487-b488-brief-rereview-r4','2026-10-02-b478-b487-b488-return']:
 d=E/stem;n=0
 for line in (d/'SHA256SUMS').read_text().splitlines():
  h,p=line.split(None,1);f=d/p.lstrip('*');f=f if f.exists() else R/p.lstrip('*');assert sha(f.read_bytes())==h,f;n+=1
  if stem=='2026-10-02-b478-b487-b488-brief-rereview-r4':approved[str(f.resolve().relative_to(R))]={'sha256':h,'bytes':f.stat().st_size}
 if stem=='2026-10-02-b478-b487-b488-brief-rereview-r4':approved[str((d/'SHA256SUMS').relative_to(R))]={'sha256':sha((d/'SHA256SUMS').read_bytes()),'bytes':(d/'SHA256SUMS').stat().st_size}
 seals.append({'path':stem,'entries':n,'valid':True})
assert I['head']=='4c1a000bc71706ac438e64161a9452966a66615f'
assert subprocess.check_output(['git','rev-parse','HEAD^'],cwd=R).decode().strip()=='10f333298ffad1098daeec37e756ed79ab0fd97f'
committed=subprocess.check_output(['git','diff','--name-only','HEAD^','HEAD'],cwd=R).decode().splitlines()
for p in committed:assert p in approved and sha(subprocess.check_output(['git','show','HEAD:'+p],cwd=R))==approved[p]['sha256'],p
assert len(committed)==136 and not I['staged'];assert I['simulator']['head']=='6585649ea5a780f0542b2931853a667be56a5b2b' and not I['simulator']['status']
old=json.loads((E/'2026-10-02-b478-b487-b488-brief-rereview-r4/inputs.json').read_text())
assert I['simulator']['files']==old['simulator']['files']
changed=[p for p in approved if I['files'].get(p)!=approved[p]];new=sorted(I['files'].keys()-approved.keys())
assert changed==['tools/probe_ui_model_mutations.py'],changed
assert all(p=='docs/superpowers/evidence/2026-10-02-b478-b487-b488-return.md' or p.startswith('docs/superpowers/evidence/2026-10-02-b478-b487-b488-return/') for p in new),new
assert not subprocess.run(['git','diff','--check'],cwd=R,capture_output=True).returncode
result={'verdict':'PASS: content-preserving owner commit accepted as reconciled base; literal old HEAD pin is superseded for this QA gate','brief_sha256':sha(B.read_bytes()),'pins':rows,'candidate':final,'seals':seals,'committed_paths':committed,'all_136_commit_blobs_match_authorized_tree':True,'changed_from_approved_tree':changed,'new_return_evidence':new,'paths':len(I['files']),'simulator_paths':len(I['simulator']['files'])}
(O/'preflight.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k not in ('pins','committed_paths','new_return_evidence','candidate')},indent=2))
