#!/usr/bin/env python3
import ast, concurrent.futures, datetime, hashlib, json, os, pathlib, re, subprocess, sys, time
ROOT = pathlib.Path('/home/staszek/MeshRoute')
OUT = pathlib.Path('/tmp/mr-b434-b435-r1-rwlxb7cv')
RAM = pathlib.Path('/dev/shm/mr-b434-b435-r1-rwlxb7cv')
STAGE = RAM / 'mutation-stage'
def dump(name, value):
    (OUT/name).write_text(json.dumps(value, indent=2, sort_keys=True)+'\n')
def file_hash(p):
    data=p.read_bytes(); return {'sha256':hashlib.sha256(data).hexdigest(),'size':len(data)}
def repo_hashes():
    names=subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=ROOT).decode().split('\0')
    return {n:file_hash(ROOT/n) for n in sorted(set(names)-{''}) if (ROOT/n).is_file()}
def stage_hashes():
    return {p.relative_to(STAGE).as_posix():file_hash(p) for p in sorted(STAGE.rglob('*')) if p.is_file() and '.pio' not in p.relative_to(STAGE).parts}
def run(name, cmd, cwd=ROOT, env=None):
    start=time.time()
    with (OUT/(name+'.log')).open('w') as f:
        f.write('$ '+ ' '.join(map(str,cmd))+'\n');f.flush()
        result=subprocess.run(cmd,cwd=cwd,env=env,stdout=f,stderr=subprocess.STDOUT)
    record={'name':name,'argv':list(map(str,cmd)),'cwd':str(cwd),'rc':result.returncode,'elapsed_seconds':round(time.time()-start,3),'started_utc':datetime.datetime.fromtimestamp(start,datetime.timezone.utc).isoformat()}
    dump(name+'.json',record)
    print(json.dumps(record),flush=True)
    return record

def prepare():
    RAM.mkdir(exist_ok=True)
    (RAM/'scratch').mkdir(exist_ok=True);(RAM/'discovery-tmp').mkdir(exist_ok=True)
    # Preserve the tracked relative simulator link with the same sibling layout.
    sibling=RAM/'lora-universal-simulator'
    if not sibling.exists(): sibling.symlink_to('/home/staszek/lora-universal-simulator', target_is_directory=True)
    run('stage-rsync',['rsync','-a','--exclude=.git','--exclude=.pio','--exclude=.pio-measure','--exclude=.claude',str(ROOT)+'/',str(STAGE)+'/'])
    before=repo_hashes();dump('gate-inputs-before.json',before)
    earlier=json.loads((OUT/'preflight-inputs.json').read_text())
    changed=[n for n in sorted(set(earlier)|set(before)) if earlier.get(n)!=before.get(n)]
    assert changed==['tools/probe_ui_model_mutations.py','tools/test_mutation_unusable_reason.py'],changed
    stage=stage_hashes();dump('stage-before.json',stage)
    assert all(stage.get(n)==v for n,v in before.items()),'stage missing an input'
    source=(ROOT/'tools/probe_ui_model_mutations.py').read_text(); tree=ast.parse(source)
    old=(OUT/'base-harness.py').read_text(); oldtree=ast.parse(old)
    def assignments(tree):
        return {n.targets[0].id:n for n in tree.body if isinstance(n,ast.Assign) and isinstance(n.targets[0],ast.Name)}
    a,b=assignments(oldtree),assignments(tree)
    stable=[k for k in a if k.startswith('MUTS') or k=='TARGET_SRC']
    for k in stable:
        assert ast.get_source_segment(old,a[k])==ast.get_source_segment(source,b[k]),k
    dump('mutation-tables-byte-preserved.json',{'assignments':stable,'count':len(stable),'result':'PASS'})
    script=(ROOT/'docs/superpowers/evidence/2026-09-20-radmin-slice10-qa/mut.sh').read_text()
    targets=re.search(r'for t in (.+?); do',script).group(1).split()
    mapping=b['MUTS_BY_TARGET'].value
    names={ast.literal_eval(k):v.id for k,v in zip(mapping.keys,mapping.values)}
    files=ast.literal_eval(b['TARGET_SRC'].value)
    inventory=[]
    for t in targets:
        entries=ast.literal_eval(b[names[t]].value)
        text=(STAGE/files[t]).read_text()
        for label,pat,rep in entries:
            assert text.count(pat)==1,(t,label,text.count(pat))
        inventory.append({'target':t,'file':files[t],'entries':len(entries),'labels':[v[0] for v in entries]})
    assert len(inventory)==61 and sum(x['entries'] for x in inventory)==984
    dump('union-inventory.json',inventory)
    previous=json.loads((OUT/'source-preservation.json').read_text());previous['candidate_harness_sha256']=hashlib.sha256(source.encode()).hexdigest();dump('source-preservation.json',previous)
    print('Prepared frozen stage: 61 batteries / 984 entries; source patterns all match once.',flush=True)

def focused():
    for name,cmd in [('whitespace',['git','diff','--check']),('where',[sys.executable,'tools/probe_ui_model_mutations.py','--where']),('selftest',[sys.executable,'tools/probe_ui_model_mutations.py','--selftest-unusable']),('focused-discovery',[sys.executable,'-m','unittest','discover','-v','-s','tools','-p','test_mutation_unusable_reason.py'])]:
        assert run(name,cmd)['rc']==0,name

def discovery():
    env=dict(os.environ,MR_LUS_SRC='/home/staszek/lora-universal-simulator',TMPDIR=str(RAM/'discovery-tmp'))
    return run('tools-discovery',[sys.executable,'-m','unittest','discover','-v','-s','tools','-p','test_*.py'],env=env)

def union():
    inventory=json.loads((OUT/'union-inventory.json').read_text())
    env=dict(os.environ,MR_MUT_SCRATCH=str(RAM/'scratch'))
    # Three independent batteries, each using the brief's explicit three workers.
    def battery(row):
        result=run('mut_'+row['target'],[sys.executable,'tools/probe_ui_model_mutations.py','--target='+row['target'],'--workers=3'],cwd=STAGE,env=env)
        expected=1 if row['target']=='sliceBmac' else 0
        if result['rc']!=expected:
            raise RuntimeError('off-floor battery: '+row['target'])
        return result
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        futures=[pool.submit(battery,row) for row in inventory]
        try:
            results=[f.result() for f in concurrent.futures.as_completed(futures)]
        except BaseException:
            for f in futures:f.cancel()
            raise
    dump('union-runs.json',results)
    after=stage_hashes();dump('stage-after.json',after)
    assert after==json.loads((OUT/'stage-before.json').read_text()),'stage inputs changed'
    print('Union complete; stage preserved.',flush=True)

if __name__=='__main__':
    {'prepare':prepare,'focused':focused,'discovery':discovery,'union':union}[sys.argv[1]]()
