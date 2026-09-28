"""Continue at step 7's output comparison after excluding the baseline shell-time wrapper, then steps 8-10."""
from pathlib import Path
import subprocess,time,json,re,collections
R=Path('/home/staszek/MeshRoute');E=Path(__file__).resolve().parent;RAW=R/'artifacts/2026-09-28-b459-qa'
results=json.loads((E/'gate-results.json').read_text());assert results[-1]['name']=='step7-firmware-ui' and all(r['exit']==0 for r in results)
def run(name,cmd):
 print('START',name,flush=True);t=time.monotonic()
 with (RAW/(name+'.log')).open('w') as f:p=subprocess.run(cmd,cwd=R,stdout=f,stderr=subprocess.STDOUT)
 results.append({'name':name,'command':cmd,'exit':p.returncode,'seconds':round(time.monotonic()-t,2),'log':str((RAW/(name+'.log')).relative_to(R))});(E/'gate-results.json').write_text(json.dumps(results,indent=2)+'\n');print('END',name,p.returncode,results[-1]['seconds'],flush=True);assert p.returncode==0,name
run('step7-firmware-ui-matched-locale',['env','LC_ALL=en_US.UTF-8','bash','tools/probe_firmware_ui/run.sh'])
def normal(text):
 return '\n'.join(re.sub(r'/tmp/tmp\.[A-Za-z0-9]+','/tmp/tmp.X',l) for l in text.splitlines() if not re.match(r'^(real|user|sys)\t',l)).rstrip('\n')
fw=(RAW/'step7-firmware-ui-matched-locale.log').read_text(errors='surrogateescape');base=(R/'artifacts/2026-09-28-b459/base-fwui.log').read_text(errors='surrogateescape');assert normal(fw)==normal(base)
src=(R/'tools/probe_firmware_ui/run.sh').read_text();decl=re.findall(r'^\s*ctl "(.*)" (?:yes|no) \\$',src,re.M);decl=[re.sub(r'\\([\\"$`])',r'\1',d) for d in decl]
seen=[]
for l in fw.splitlines():
 m=re.match(r'^  ok   (.*) -> RED \(\d+ check\(s\) failed\)$',l) or re.match(r'^  ok   (.*) \(build fails, as required\)$',l)
 if m:seen.append(m[1])
assert len(decl)==len(set(decl))==len(seen)==len(set(seen))==238 and sorted(decl)==sorted(seen)
(E/'firmware-ui-comparison.json').write_text(json.dumps({'normalized_output_identical':True,'normalization':'shell real/user/sys timing wrapper and trailing blank lines removed; temporary paths normalized','normalized_lines':len(normal(fw).splitlines()),'baseline_raw_lines':len(base.splitlines()),'qa_raw_lines':len(fw.splitlines()),'declared_controls':len(decl),'observed_controls':len(seen),'unique_controls':len(set(seen)),'qa_analysis_correction':'The fresh run exited 0; QA log reader initially assumed UTF-8, but panel output contains intentional Latin-1 0xBB. Re-read losslessly with surrogateescape. The first run had a different locale ordering for coverage diagnostics. A fresh stock run under en_US.UTF-8 matches the baseline exactly after excluding shell timing/footer blanks; no source input changed.'},indent=2)+'\n')
print('STEP7 COMPATIBILITY PASS',len(decl),'controls',flush=True)
run('step7-b456-tests',['python3','tools/test_probe_firmware_ui.py','-v'])
run('step8-discovery',['python3','-m','unittest','discover','-s','tools','-p','test_*.py'])
run('step9-inventory',['python3','tools/gen_command_inventory.py','--check'])
run('step10-reader-audit',['python3',str(E/'reader_audit.py')])
print('INDEPENDENT CHAIN COMPLETE',flush=True)
