from pathlib import Path
import importlib.util,json,hashlib,subprocess,re,sys
q=Path('/tmp/mr-codex-s7b3-0gt630zl');r=q/'gate';sys.path.insert(0,str(r/'tools/probe_deferred_actions'));spec=importlib.util.spec_from_file_location('actions',r/'tools/probe_deferred_actions/run.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
b=m.owners(q/'original',False);a=m.owners(r,False);assert a==b
rows=dict(owner_extract_sha256=hashlib.sha256(a.encode()).hexdigest(),owner_extract_bytes=len(a.encode()),unchanged_files={})
for rel in ['src/firmware_action_effects.h','src/firmware_config.cpp','src/device_ota.cpp','src/device_ota.h','src/firmware_command_authority.h','lib/core/remote_codec.cpp','lib/core/remote_codec.h']:
 assert (q/'original'/rel).read_bytes()==(r/rel).read_bytes(),rel
 rows['unchanged_files'][rel]=hashlib.sha256((r/rel).read_bytes()).hexdigest()
(q/'p1-and-wire-preservation.json').write_text(json.dumps(rows,indent=2)+'\n');print(rows)
rows=[]
for p in sorted((r/'test').glob('*.cpp')):
 old=q/'original/test'/p.name;before=old.read_text().count('TEST_CASE(') if old.exists() else 0;after=p.read_text().count('TEST_CASE(')
 if before!=after:rows.append(dict(path='test/'+p.name,base=before,final=after,added=after-before))
assert sum(x['added'] for x in rows)==15
(q/'native-case-additions.json').write_text(json.dumps(rows,indent=2)+'\n');print('CASE ADDITIONS',rows)
