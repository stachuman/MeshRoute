"""Pre-check scratch diagnostic only; reuses the stock inbox builder without editing it.
Repairs are external generated fixtures, NOT a production/tool candidate or a gate PASS.
"""
from pathlib import Path
import importlib.util,sys,os,subprocess,shlex,hashlib,json,re
sys.dont_write_bytecode=True
ROOT=Path('/home/staszek/MeshRoute');HERE=ROOT/'tools/probe_inbox_verbs';OUT=Path('/tmp/mr-tool-precheck-as0y6n6_/transcript-diagnostic');OUT.mkdir(exist_ok=True)
spec=importlib.util.spec_from_file_location('stock_transcript',HERE/'transcript.py');t=importlib.util.module_from_spec(spec);spec.loader.exec_module(t)
runner=(HERE/'run.sh').read_text();boundary='rc=0\nif ! build_support;';assert runner.count(boundary)==1
prefix=runner[:runner.index(boundary)]
base=(HERE/'transcript_main.cpp').read_text();results=[]
profile_old='printf("MR0C-PROFILE MR_N_LAYERS=%d MR_FEAT_MOBILE=%d MR_FEAT_OLED=%d\\n",\n           (int)MR_N_LAYERS, (int)MR_FEAT_MOBILE, (int)MR_FEAT_OLED);'
profile_new='printf("MR0C-PROFILE MR_N_LAYERS=%d MR_FEAT_MOBILE=%d MR_FEAT_OLED=%d MR_FEAT_RADMIN_ACCEPT=%d MR_FEAT_RADMIN_CLIENT=%d\\n",\n           (int)MR_N_LAYERS, (int)MR_FEAT_MOBILE, (int)MR_FEAT_OLED, (int)MR_FEAT_RADMIN_ACCEPT, (int)MR_FEAT_RADMIN_CLIENT);'
assert base.count(profile_old)==1
# Two extra labelled DIAGNOSTIC boundaries; no source matrix is edited.
t.BOUNDARY_LINES += [('z'*1023,'diagnostic USB exact maximum'),('12345678','diagnostic eight-byte unknown')]
for profile,arm in [('full_headless','accept'),('mobile','client')]:
 out=OUT/profile;out.mkdir(exist_ok=True)
 hp,shape,sha,matrix,nprojected=t.generate(str(ROOT/'src/fw_main.cpp'),str(out),profile)
 raw=Path(hp).read_text(); variants=[]
 for name,array,live in [('pointer',False,False),('array',True,False),('array_live',True,True)]:
  header=out/(name+'.h');h=raw
  if array:
   old='static void mr0c_serial_tail(const char* line, size_t pos) {'
   assert h.count(old)==1
   h=h.replace(old,'static void mr0c_serial_tail(const char (&line)[meshroute::console::local_command_max_bytes + 1], size_t pos) {')
  header.write_text(h)
  driver=base.replace('#include "probe_main.cpp"','#include '+json.dumps(str(HERE/'probe_main.cpp')))
  driver=driver.replace('#include MR0C_ADAPTERS_HEADER','#include "device_ble.h"\n#include '+json.dumps(str(header)))
  driver=driver.replace(profile_old,profile_new)
  if array:
   driver=driver.replace('        mr0c_serial_tail(row.line, len);','        char line[meshroute::console::local_command_max_bytes + 1]{};\n        if (len >= sizeof line) return 4; // diagnostic refuses an unrepresentable fixture\n        std::memcpy(line, row.line, len + 1);\n        mr0c_serial_tail(line, len);')
  if live:driver=driver.replace('    seed_id("transcript", 10, 0x20);','    install_live_name_pos(seed_id("transcript", 10, 0x20));')
  cpp=out/(name+'.cpp');cpp.write_text(driver);variants.append((name,cpp,header))
 commands=['build_support || exit 2']
 for name,cpp,header in variants:
  commands += [f'build_variant "$FW_CMDS" "$FW_INBOX" "" "$OUT/{name}" {shlex.quote(str(cpp))} || {{ cat "$OUT/build.log"; exit 2; }}',f'cp "$OUT/{name}" {shlex.quote(str(out/(name+".bin")))}',f'cp "$OUT/build.log" {shlex.quote(str(out/(name+"-compile.log")))}',f'"$OUT/{name}" > {shlex.quote(str(out/(name+".txt")))} || exit 3']
 commands += [f'"$OUT/array_live" > {shlex.quote(str(out/"array_live-repeat.txt"))} || exit 3']
 script=prefix+'\n'+'\n'.join(commands)+'\n';(out/'build.sh').write_text(script)
 env=t.build_env();env['MR_PROBE_ARM']=arm
 p=subprocess.run(['bash','-c',script,str(HERE/'run.sh')],cwd=ROOT,env=env,capture_output=True)
 (out/'build-driver.log').write_bytes(p.stdout+p.stderr);assert p.returncode==0,(profile,p.returncode,p.stdout[-3000:],p.stderr[-1000:])
 rows={}
 for name,cpp,header in variants:
  txt=(out/(name+'.txt')).read_text();t.verify_profile(txt,profile)
  d=[];cur=None
  for line in txt.splitlines():
   m=re.match(r'LINE (\S+) len=(\d+) \[(.*)\]',line)
   if m:cur={'id':m[1],'len':int(m[2]),'command':m[3]};d.append(cur)
   elif cur and line.startswith('  SER '):cur['SER']=line
   elif cur and line.startswith('  BLE '):cur['BLE']=line
  assert len(d)==len(matrix)
  rows[name]=d
  # Extracted caller regions survive byte-for-byte inside every generated header.
  assert all(body in header.read_text() for body in t.extract_regions((ROOT/'src/fw_main.cpp').read_text()).values())
 assert (out/'array_live.txt').read_bytes()==(out/'array_live-repeat.txt').read_bytes()
 specific={name:{'above7_refused':sum(x['len']>7 and '> err bad_line too_long\\n' in x['SER'] for x in d),'examples':[x for x in d if x['command'] in ['acl list','regen','whoami','12345678'] or x['len']==1023]} for name,d in rows.items()}
 changes=[{'id':a['id'],'command':a['command'],'SER_before':a['SER'],'SER_live':b['SER'],'BLE_before':a['BLE'],'BLE_live':b['BLE']} for a,b in zip(rows['array'],rows['array_live']) if a!=b]
 results.append({'profile':profile,'arm':arm,'matrix':len(matrix),'projected':nprojected,'profile_verified':True,'caller_bytes_preserved':True,'repeat_same_binary_identical':True,'variants':specific,'live_precondition_changed_rows':changes})
 (out/'rows.json').write_text(json.dumps(rows,indent=2)+'\n')
(OUT/'results.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps([{k:v for k,v in x.items() if k not in ('variants','live_precondition_changed_rows')}|{'refused_above7':{k:v['above7_refused'] for k,v in x['variants'].items()},'live_changed':[r['command'] for r in x['live_precondition_changed_rows']]} for x in results],indent=2))
