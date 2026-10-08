from pathlib import Path
import hashlib,json,subprocess,os
R=Path('/home/staszek/MeshRoute');O=R/'artifacts/2026-10-02-b478-b487-b488-qa-regate';b=json.loads((O/'inputs.json').read_text())
def one(root,key):
 p=root/key
 if p.is_symlink():return {'symlink':os.readlink(p)}
 data=p.read_bytes();return {'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)}
def check(root,files):
 names=set(subprocess.check_output(['git','ls-files','-c','-o','--exclude-standard','-z'],cwd=root).decode().split('\0'))-{''};missing=sorted(set(files)-names);new=sorted(names-set(files));diff=[k for k in files if k not in missing and one(root,k)!=files[k]]
 return {'paths':len(files),'changed':diff,'missing':missing,'new':new,'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root).decode().strip(),'status':subprocess.check_output(['git','status','--porcelain=v1'],cwd=root).decode(),'staged':subprocess.check_output(['git','diff','--cached','--name-only'],cwd=root).decode(),'whitespace_exit':subprocess.run(['git','diff','--check'],cwd=root,capture_output=True).returncode}
a=check(R,b['files']);s=check(Path('/home/staszek/lora-universal-simulator'),b['simulator']['files']);assert not a['changed']+a['missing']+a['new'];assert not s['changed']+s['missing']+s['new'];assert a['head']==b['head'] and s['head']==b['simulator']['head'];assert not a['staged'] and not s['status'];assert not a['whitespace_exit'] and not s['whitespace_exit'];(O/'preservation-before-landing.json').write_text(json.dumps({'meshroute':a,'simulator':s,'all_inputs_unchanged':True},indent=2)+'\n');print('STABLE',a['paths'],s['paths'])
