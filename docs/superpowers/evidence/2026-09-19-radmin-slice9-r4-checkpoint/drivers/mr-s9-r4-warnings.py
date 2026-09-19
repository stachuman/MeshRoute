from pathlib import Path
import subprocess,os,json,hashlib,re,collections,time
q=Path('/tmp/mr-s9-r4-j37ybea_');out=q/'warning-attribution';out.mkdir(exist_ok=True);results=[]
for cell in ['gateway_heltec','heltec_v4']:
 for side,root in [('base',Path('/tmp/mr-s9-r3-mg75mxe0/base')),('candidate',q/'final-boards')]:
  name=side+'-'+cell;log=out/(name+'.log');env=dict(os.environ,PLATFORMIO_BUILD_DIR=str(q/('warning-build-'+side)))
  cmd=['pio','run','-e',cell,'-j','2'];start=time.monotonic();print('START',name,flush=True)
  with log.open('wb') as f:rc=subprocess.run(cmd,cwd=root,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
  s=log.read_text(errors='replace');lines=[x for x in s.splitlines() if 'warning:' in x];normal=[]
  for line in lines:
   line=re.sub(r'\x1b\[[0-9;]*m','',line).replace(str(root)+'/','')
   line=re.sub(r':\d+:\d+: warning:',':LINE: warning:',line)
   normal.append(line)
  counts=dict(collections.Counter(normal));row=dict(name=name,command=cmd,cwd=str(root),exit=rc,seconds=time.monotonic()-start,warning_count=len(lines),switch=s.count('Wswitch'),warnings=counts,log_sha256=hashlib.sha256(log.read_bytes()).hexdigest());results.append(row)
  (out/'results.json').write_text(json.dumps(results,indent=2)+'\n');print('END',name,rc,len(lines),flush=True)
  assert rc==0
for cell in ['gateway_heltec','heltec_v4']:
 a=next(x for x in results if x['name']=='base-'+cell);b=next(x for x in results if x['name']=='candidate-'+cell)
 removed=collections.Counter(a['warnings'])-collections.Counter(b['warnings']);added=collections.Counter(b['warnings'])-collections.Counter(a['warnings']);print(cell,'removed',dict(removed),'added',dict(added),flush=True)
(out/'diff.json').write_text(json.dumps({cell:dict(removed=dict(collections.Counter(next(x for x in results if x['name']=='base-'+cell)['warnings'])-collections.Counter(next(x for x in results if x['name']=='candidate-'+cell)['warnings'])),added=dict(collections.Counter(next(x for x in results if x['name']=='candidate-'+cell)['warnings'])-collections.Counter(next(x for x in results if x['name']=='base-'+cell)['warnings']))) for cell in ['gateway_heltec','heltec_v4']},indent=2)+'\n')
