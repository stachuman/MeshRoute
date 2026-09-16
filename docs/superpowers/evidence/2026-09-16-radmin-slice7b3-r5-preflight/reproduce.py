from pathlib import Path
import hashlib,json,os,re,stat,subprocess,tempfile
r=Path('/home/staszek/MeshRoute'); sim=Path('/home/staszek/lora-universal-simulator')
q=Path(tempfile.mkdtemp(prefix='mr-codex-s7b3-r5-preflight-'))
Path('/tmp/mr-codex-s7b3-r5-preflight-active').write_text(str(q)+'\n')
sha=lambda b:hashlib.sha256(b).hexdigest()
def git(*args,root=r): return subprocess.check_output(['git',*args],cwd=root)
def write(name,value): (q/name).write_text(json.dumps(value,indent=2)+'\n')
def record(p):
    s=p.lstat(); v={'mode':oct(stat.S_IMODE(s.st_mode))}
    if p.is_symlink():
        v.update(kind='symlink',target=os.readlink(p))
        if p.is_file(): v['resolved_sha256']=sha(p.read_bytes())
    elif p.is_file(): v.update(kind='file',sha256=sha(p.read_bytes()),size=s.st_size)
    else: v.update(kind='other')
    return v
paths=sorted(set(x.decode() for x in git('ls-files','--cached','--others','--exclude-standard','-z').split(b'\0') if x))
write('inputs-before.json',{p:record(r/p) for p in paths if (r/p).exists() or (r/p).is_symlink()})
changed=sorted(set(x.decode() for x in (git('diff','--name-only','-z')+git('diff','--cached','--name-only','-z')+git('ls-files','--others','--exclude-standard','-z')).split(b'\0') if x))
allowed={
 'MEMORY.md','docs/2026-07-30-open-bug-register.md',
 'docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md',
 'docs/superpowers/plans/2026-09-13-radmin-slice7b3-deferred-actions.md',
 'docs/superpowers/plans/2026-09-15-radmin-slice7b3-p1-simple-action-preparation.md',
 'docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md',
 'docs/superpowers/evidence/2026-09-15-radmin-slice7b3-p1-qa-gate.md',
 'docs/superpowers/evidence/2026-09-15-radmin-slice7b3-reissue/brief-revision-4.md'}
assert all(p in allowed or p.startswith('docs/superpowers/evidence/2026-09-15-radmin-slice7b3-p1-qa/') for p in changed)
write('permitted-preparation.json',{p:record(r/p) for p in changed})
(q/'status-before.txt').write_bytes(git('status','--short'))
(q/'preparation.patch').write_bytes(git('diff','--binary','HEAD'))
(q/'simulator-status-before.txt').write_bytes(git('status','--short',root=sim))
brief='docs/superpowers/plans/2026-09-13-radmin-slice7b3-deferred-actions.md'
meta={'head':git('rev-parse','HEAD').decode().strip(),'simulator_head':git('rev-parse','HEAD',root=sim).decode().strip(),'brief':brief,'brief_sha256':sha((r/brief).read_bytes()),'permitted_preparation_files':len(changed),'total_inputs':len(paths)}
assert meta['head']=='7442e6f570abdcd74ceed20d4c0cb9e2855d0719'
assert meta['simulator_head']=='06746a97de5764415d6fcef10b97bca90569b9c7'
assert not git('status','--short',root=sim)
assert sha((r/'docs/superpowers/evidence/2026-09-15-radmin-slice7b3-reissue/brief-revision-4.md').read_bytes())=='4a7af9b19e723598470fa5b9b054403c63f7e88d44ffaffd7bb7aa717c31b397'
write('state.json',meta)
# Focused language/dependency proof, NOT a firmware build, mutation gate, or allocation model.
base=['g++','-std=c++20','-DMESHROUTE_NATIVE=1','-DMR_N_LAYERS=2']
base += ['-I'+str(r/p) for p in ['lib/core','lib/hal','lib/console','lib/monocypher/src','src']]
cases={
'core_only':'#include "remote_session.h"\nnamespace mrfw { struct ActionPlan; }\nstruct ProposedRecord { mrfw::ActionPlan plan; };\n',
'firmware_complete':'#include "firmware_action_effects.h"\n#include "remote_session.h"\nstatic_assert(sizeof(mrfw::ActionPlan)==2);\nstruct ProposedRecord { mrfw::ActionPlan plan; };\nstatic_assert(sizeof(ProposedRecord)==2);\n',
'core_baseline':'#include "remote_session.h"\nstatic_assert(sizeof(meshroute::TranscriptHeader)==24);\nstatic_assert(sizeof(meshroute::RemoteSessionState)==8824);\n'}
results={}
for name,code in cases.items():
    source=q/(name+'.cpp'); source.write_text(code)
    cmd=base+['-MMD','-MF',str(q/(name+'.d')),'-c',str(source),'-o',str(q/(name+'.o'))]
    p=subprocess.run(cmd,cwd=r,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (q/(name+'.log')).write_bytes(p.stdout)
    results[name]={'command':cmd,'exit':p.returncode,'log':name+'.log'}
    write('compile-results.json',results)
assert results['core_only']['exit']!=0 and b'incomplete type' in (q/'core_only.log').read_bytes()
assert results['firmware_complete']['exit']==0
assert results['core_baseline']['exit']==0
assert 'src/firmware_config_parse.h' in (q/'firmware_complete.d').read_text()
policy=(r/'src/firmware_command_authority.h').read_text()
rows=list(re.finditer(r'\{"([^"]+)", "([^"]+)", CommandClass::(\w+), (true|false)\}',policy))
disruptive=[m for m in rows if m[4]=='true']
old=json.loads((r/'docs/superpowers/evidence/2026-09-15-radmin-slice7b3-reissue/row-dispositions.json').read_text())
assert len(rows)==180 and len(disruptive)==48
assert sha(policy.encode())==old['policy_sha256']
for m,row in zip(disruptive,old['rows']):
    assert (m[1],m[2],m[3])==(row['verb'],row['subverb'],row['authority'])
write('policy-check.json',{'policy_rows':len(rows),'disruptive_rows':len(disruptive),'preserved_dispositions':48,'scheduled_scope':12,'retained_refusals':36,'policy_sha256':sha(policy.encode())})
# Enumerate all definitions instead of treating a comment/reference as a second carrier authority.
definitions=[]
for tree in ('src','lib'):
    for p in (r/tree).rglob('*.h'):
        for m in re.finditer(r'\bstruct\s+ActionPlan\s*\{',p.read_text()):
            definitions.append({'file':str(p.relative_to(r)),'line':p.read_text()[:m.start()].count('\n')+1})
assert definitions==[{'file':'src/firmware_action_effects.h','line':18}]
write('action-plan-definitions.json',definitions)
# Ensure read-only probes and snapshots did not alter any initial input.
before=json.loads((q/'inputs-before.json').read_text()); changed_inputs=[p for p,v in before.items() if record(r/p)!=v]
write('preservation-before-landing.json',{'changed_inputs':changed_inputs,'simulator_status':git('status','--short',root=sim).decode()})
assert not changed_inputs and not git('status','--short',root=sim)
print(json.dumps({'artifact_directory':str(q),**meta,'focused_dependency_proof':'expected incomplete-type refusal; positive firmware include; unchanged core baseline','production_edits':0,'full_gate_run':False},indent=2))
