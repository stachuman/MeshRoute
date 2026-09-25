#!/usr/bin/env python3
"""QA-only replay of pinned UI controls, reusing this QA run's own support archive.
No repository input is changed. Additional faults are labelled synthetic; they do
not extend the maintained runner's reported coverage.
"""
import hashlib,json,re,subprocess,sys
from pathlib import Path
root=Path(sys.argv[1]).resolve(); run=Path(sys.argv[2]).resolve(); out=run/'focused-controls';out.mkdir()
runner=(root/'tools/probe_firmware_ui/run.sh').read_text(); live=(root/'src/firmware_ui.cpp').read_bytes()
base=subprocess.check_output(['git','show','4a230f4:src/firmware_ui.cpp'],cwd=root)
base_runner=subprocess.check_output(['git','show','4a230f4:tools/probe_firmware_ui/run.sh'],cwd=root).decode()
defs=['-DARDUINO=100','-DMR_FEAT_OLED=1','-DMR_UI_BTN_PIN=0','-DMR_UI_TEAM_CHANNEL_ID=0','-DMR_CONSOLE=1']
incs=['-I'+str(root/p) for p in ['tools/probe_board_ui/fakes','variants/heltec_common','lib/hal','lib/core','lib/console','src','lib/monocypher/src']]
std=['-std=gnu++20','-fno-exceptions','-fno-rtti','-O0','-Wall','-Wextra']
def flags(arm):return std+defs+(['-DMR_N_LAYERS=2'] if arm=='l2' else ['-DMR_UI_ADC_CTRL=37','-DMR_UI_VBAT_READ=1'])+incs

def script(text,label):
 line=re.search(r'^  ctl "'+re.escape(label)+r' [^\n]+\n',text,re.M)
 assert line, label
 start=text.index("'",line.end())+1;end=text.index("'",start)
 return text[start:end]

def checked(cmd,log):
 p=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT);assert p.returncode==0,cmd

base_probe=out/'base-probe.cpp';base_probe.write_bytes(subprocess.check_output(['git','show','4a230f4:tools/probe_firmware_ui/probe_main.cpp'],cwd=root))
with (out/'base-probe-build.log').open('w') as f:
 checked(['g++',*flags('v3'),'-Werror','-c',str(base_probe),'-o',str(out/'base-probe.o')],f)
records=[]
cases=[('B241a','l2','frozen'),('B241b','l2','frozen')]
cases += [(label,'v3',tree) for label in ['N4','N9','O2','O6','O8'] for tree in ['base','frozen']]
cases += [('synthetic-fallback','l2','frozen'),('synthetic-capacity-zero','l2','frozen')]
for label,arm,tree in cases:
 src=live if tree=='frozen' else base
 if label.startswith('synthetic'):
  old,new=(b'if (cap == 0) return;',b'if (cap == 0) { out[0] = 0; return; }') if label.endswith('zero') else (b'"0x%08lx"',b'"0y%08lx"')
  start=src.index(b'void label_from_hash('); end=src.index(b'\n}',start)+2; fn=src[start:end];assert fn.count(old)==1
  mutant=src[:start]+fn.replace(old,new)+src[end:]
 else:
  sc=script(runner if tree=='frozen' else base_runner,label)
  if tree=='base':assert sc==script(runner,label)
  mutant=subprocess.check_output(['sed',sc],input=src)
 assert mutant!=src
 stem=out/(label+'-'+tree); source=stem.with_suffix('.cpp');source.write_bytes(mutant)
 compile_source=source
 if tree=='frozen':
  compile_source=out/(stem.name+'-wrap.cpp');compile_source.write_text('#include '+json.dumps(str(source))+'\nvoid mr_probe_label_from_hash(uint32_t h, char* p, uint8_t n) { label_from_hash(h,p,n); }\n')
 obj=stem.with_suffix('.o');exe=stem.with_suffix('.bin')
 harness=run/'probe-support'/('probe_main-'+arm+'.o') if tree=='frozen' else out/'base-probe.o'
 with stem.with_suffix('.build.log').open('w') as f:
  checked(['g++',*flags(arm),'-c',str(compile_source),'-o',str(obj)],f)
  checked(['g++',str(harness),str(obj),str(run/'probe-support'/('libsupport-'+arm+'.a')),'-o',str(exe)],f)
 p=subprocess.run([str(exe)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);stem.with_suffix('.log').write_bytes(p.stdout)
 fails=[line for line in p.stdout.decode(errors='replace').splitlines() if line.startswith('  FAIL ')]
 assert p.returncode==1 and fails,(label,p.returncode,fails)
 if label in ['B241a','B241b']:assert any('P28a H1 lands exactly' in line for line in fails)
 if label=='synthetic-fallback':assert all(any(word in line for line in fails) for word in ['P28a a NAMELESS','P28a an UNKNOWN'])
 if label=='synthetic-capacity-zero':assert any('P28a synthetic: capacity 0' in line for line in fails)
 record={'label':label,'tree':tree,'arm':arm,'mutant_sha256':hashlib.sha256(mutant).hexdigest(),'exit':p.returncode,'failed_checks':fails,'failed_count':len(fails)};records.append(record)
 print(label,tree,p.returncode,len(fails),flush=True)
for label in ['N4','N9','O2','O6','O8']:
 a,b=[r for r in records if r['label']==label]; assert a['failed_checks']==b['failed_checks'],label
(run/'focused-controls.json').write_text(json.dumps({'results':records,'existing_failure_sets_identical':True},indent=2)+'\n')
