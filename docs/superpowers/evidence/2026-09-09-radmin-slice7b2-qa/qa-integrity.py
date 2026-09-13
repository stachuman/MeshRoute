from pathlib import Path
import json,hashlib,stat,os,subprocess
q=Path(__file__).resolve().parent;r=Path('/home/staszek/MeshRoute');sim=Path('/home/staszek/lora-universal-simulator');before=json.loads((q/'inputs-before.json').read_text())
def item(p):
 s=p.lstat();o={'mode':stat.S_IMODE(s.st_mode)}
 if p.is_symlink():o.update(kind='symlink',target=os.readlink(p))
 else:o.update(kind='file',sha256=hashlib.sha256(p.read_bytes()).hexdigest())
 return o
result={}
for name,root in [('shared',r),('frozen',q/'frozen'),('gate',q/'gate'),('mutations',q/'mutations')]:
 actual={p:item(root/p) for p in before};diff=[p for p in before if before[p]!=actual[p]];assert not diff,(name,diff)
 result[name]=dict(inputs_checked=len(actual),unchanged=True)
paths=set(x.decode() for x in subprocess.check_output(['git','ls-files','-z','--cached','--others','--exclude-standard'],cwd=r).split(b'\0') if x);assert paths==set(before)
for name,root,expected in [('shared',r,'564f460a3b755a104f146da70457e5c8c68e99b9'),('simulator',sim,'06746a97de5764415d6fcef10b97bca90569b9c7')]:
 head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip();assert head==expected
 p=subprocess.run(['git','diff','--check'],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);assert p.returncode==0
 result[name].update(head=head,diff_check_exit=p.returncode) if name in result else result.update({name:dict(head=head,diff_check_exit=p.returncode)})
ss=subprocess.check_output(['git','status','--porcelain'],cwd=sim,text=True);assert not ss;result['simulator']['clean']=True
raw={}
for p in sorted((q/'measure/.pio-measure').glob('qa-*/*/*')):
 if p.is_file():raw[str(p)]=hashlib.sha256(p.read_bytes()).hexdigest()
assert len(raw)==28
result['measurement_files_sha256']=raw
(q/'pre-landing-integrity.json').write_text(json.dumps(result,indent=2)+'\n');print('All 1087 frozen inputs in shared/frozen/gate/mutations unchanged; HEADs match; simulator clean; both whitespace checks pass; 28 raw measurement hashes recorded.')
