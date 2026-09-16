from pathlib import Path
import subprocess,json,hashlib,stat
r=Path('/home/staszek/MeshRoute');q=Path('/tmp/mr-codex-s7b3p1-preflight-active').read_text().strip();q=Path(q)
def run(*args,cwd=r):return subprocess.check_output(args,cwd=cwd)
def inv(root):
 paths=run('git','ls-files','--cached','--others','--exclude-standard','-z',cwd=root).decode().split('\0');out={}
 for rel in sorted(set(paths)-{''}):
  p=root/rel
  if p.is_symlink():
   link=str(p.readlink());rec={'sha256':hashlib.sha256(link.encode()).hexdigest(),'mode':stat.S_IMODE(p.lstat().st_mode),'symlink':link,'hashes':'link text'}
   if p.is_file():rec['resolved_file_sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
  else:rec={'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'mode':stat.S_IMODE(p.stat().st_mode)}
  out[rel]=rec
 return out
base='ac5f9a592065d08e7cc8c06ef395e79891d41b34';head=run('git','rev-parse','HEAD').decode().strip();inputs=inv(r);sim=Path('/home/staszek/lora-universal-simulator');siminputs=inv(sim)
(q/'inputs-before.json').write_text(json.dumps(inputs,indent=2)+'\n');(q/'simulator-before.json').write_text(json.dumps(siminputs,indent=2)+'\n')
modified=run('git','diff','--name-only').decode().splitlines();staged=run('git','diff','--cached','--name-only').decode().splitlines();untracked=run('git','ls-files','--others','--exclude-standard').decode().splitlines()
assert len(modified)==7 and not staged and not untracked
committed=run('git','diff','--name-only',base+'..'+head).decode().splitlines();assert len(committed)==60 and all(x=='MEMORY.md' or x.startswith('docs/') for x in committed)
non_docs=[p for p in inputs if not (p=='MEMORY.md' or p.startswith('docs/'))]
assert run('git','diff',base,'--','AGENTS.md','lib','src','test','tools','simulation','platformio.ini')==b''
result={'named_base':base,'actual_head':head,'permitted_uncommitted_preparation':{p:inputs[p] for p in modified},'committed_documentation_successor_paths':committed,'initial_input_count':len(inputs),'input_manifest_sha256':hashlib.sha256((q/'inputs-before.json').read_bytes()).hexdigest(),'non_documentation_input_count':len(non_docs),'simulator_head':run('git','rev-parse','HEAD',cwd=sim).decode().strip(),'simulator_status':run('git','status','--porcelain',cwd=sim).decode(),'simulator_inputs':len(siminputs),'staged':staged,'untracked':untracked,'production_test_tool_profile_diff_against_named_base_empty':True}
(q/'input-receipt.json').write_text(json.dumps(result,indent=2)+'\n');print(q);print(json.dumps({k:v for k,v in result.items() if k not in ['permitted_uncommitted_preparation','committed_documentation_successor_paths']},indent=2));print(json.dumps(result['permitted_uncommitted_preparation'],indent=2))
