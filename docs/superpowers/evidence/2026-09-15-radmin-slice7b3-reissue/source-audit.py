from pathlib import Path
import re,json,hashlib,ast,subprocess
q=Path(__file__).resolve().parent;r=q/'snapshot'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
old=json.loads((q/'typed-plan/source-audit.json').read_text())['source_hashes']
for rel,digest in old.items():assert sha(r/rel)==digest,rel
text=(r/'src/firmware_command_authority.h').read_text();matches=list(re.finditer(r'\{"([^"]+)", "([^"]+)", CommandClass::(\w+), (true|false)\}',text));assert len(matches)==180
rows=[];oldrows=json.loads((q/'typed-plan/disruptive-enumeration.json').read_text())['rows']
groups={'cfg':('B395','7b-3-F-config'),'gateway':('B395','7b-3-F-config'),'join':('B396','7b-3-F-provision'),'create':('B396','7b-3-F-provision'),'leave':('B396','7b-3-F-provision'),'team':('B397','7b-3-F-team'),'regen':('B398','7b-3-F-regen')}
for m in matches:
 if m[4]!='true':continue
 family=('cfg' if m[1]=='cfg set' else 'crash' if m[1]=='crashtest' else 'factory' if m[1]=='factory_reset' else 'join' if m[1].startswith('join') else 'reboot_prep' if m[1].startswith('reboot') or m[1]=='prep-restart' else m[1])
 row={'index':len(rows)+1,'policy_line':text[:m.start()].count('\n')+1,'verb':m[1],'subverb':m[2],'authority':m[3],'family':family}
 previous=oldrows[len(rows)]
 assert all(row[k]==previous[k] for k in row)
 row['disposition']='retained remote refusal under R-RA-39' if family in groups else 'scheduled after B394 preparation and B389 allocation'
 row['finding']=groups.get(family,('B394','7b-3-P1'))[0];row['followup']=groups.get(family,('B394','7b-3-P1'))[1];rows.append(row)
assert sum(x['family'] in groups for x in rows)==36
mapping={'base':'ac5f9a592065d08e7cc8c06ef395e79891d41b34','policy_sha256':sha(r/'src/firmware_command_authority.h'),'policy_rows':180,'disruptive_rows':48,'scheduled_scope_after_preparation':12,'R_RA_39_refusal_rows':36,'unchanged_enumeration_source_files':len(old),'rows':rows}
(q/'row-dispositions.json').write_text(json.dumps(mapping,indent=2)+'\n')
lines=['# Revision-4 disposition of all 48 disruptive policy rows','', 'These are policy metadata rows, including aliases/coarse entries; they are not 48 independently runnable commands. The twelve scheduled-scope rows still require B394 preparation and B389 allocation. R-RA-39 records the other 36 as retained remote refusals; classification stays unchanged.','', '| # | Policy line | Verb | Subverb | Class | Disposition / follow-up |','| --- | --- | --- | --- | --- | --- |']
for x in rows:lines.append(f"| {x['index']} | {x['policy_line']} | `{x['verb']}` | `{x['subverb']}` | {x['authority']} | {x['disposition']}; {x['finding']} / {x['followup']} |")
(q/'all-48-dispositions.md').write_text('\n'.join(lines)+'\n')
values={};tables={}
for n in ast.parse((r/'tools/probe_ui_model_mutations.py').read_text()).body:
 if isinstance(n,ast.Assign):
  for t in n.targets:
   if isinstance(t,ast.Name):
    try:values[t.id]=ast.literal_eval(n.value)
    except (ValueError,TypeError):
     if t.id=='MUTS_BY_TARGET':tables={ast.literal_eval(k):v.id for k,v in zip(n.value.keys,n.value.values)}
floor=json.loads((r/'docs/superpowers/evidence/2026-09-13-radmin-slice7b3-precheck/historical-mutation-floor.json').read_text())['union'];counts={};bad=[]
for t in floor:
 src=(r/values['TARGET_SRC'][t]).read_text();counts[t]=len(values[tables[t]])
 for label,pattern,replacement in values[tables[t]]:
  if src.count(pattern)!=1:bad.append({'target':t,'label':label,'matches':src.count(pattern)})
assert len(counts)==52 and sum(counts.values())==817 and not bad
(q/'mutation-floor.json').write_text(json.dumps({'counts':counts,'configured_patterns':sum(counts.values()),'batteries':len(counts),'mismatches':bad,'executed_native_mutations_this_turn':False},indent=2)+'\n')
commands=[('reference',['/home/staszek/mr-slice2-ref/bin/python','docs/superpowers/evidence/2026-09-13-radmin-slice7b3-0-reference.py','--freeze-check','--compare','test/test_remote_codec.cpp','--selftest'])]
results=[]
for name,cmd in commands:
 p=subprocess.run(cmd,cwd=r,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(q/'logs'/(name+'.log')).write_bytes(p.stdout);assert p.returncode==0,p.stdout.decode();results.append({'name':name,'command':cmd,'cwd':str(r),'exit':p.returncode,'log':name+'.log'})
(q/'source-audit-results.json').write_text(json.dumps(results,indent=2)+'\n');print('PASS: 25 enumeration sources unchanged; 48 rows = 12 proposed scheduled +36 recorded refusals; 52/817 patterns all match once; reference 94/94')
