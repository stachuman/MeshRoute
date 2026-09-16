from pathlib import Path
import json,subprocess,hashlib
q=Path('/tmp/mr-codex-s7b3-0gt630zl');r=q/'gate';d=json.loads((q/'elf-attribution2.json').read_text());summary={}
for env,x in d.items():
 b,a=x['measurements'];delta={k:a[k]-b[k] for k in ['ram_bytes','flash_bytes','object_count','symbol_count','symbol_size_total']};summary[env]=dict(base=b,final=a,delta=delta,allocated_sections=x['allocated_sections'],text_coverage=x['text_coverage'],fixed_identity_equal=x['fixed_identity_equal'])
 for state in ['s7b3-base','s7b3-final-2']:
  p=r/'.pio-measure'/state/env;manifest=json.loads((p/'manifest.json').read_text());summary[env][state]={x.name:hashlib.sha256(x.read_bytes()).hexdigest() for x in p.iterdir() if x.is_file() and x.suffix in ['.elf','.bin','.hex','.json']}
 assert x['fixed_identity_equal']
 if env=='gateway':assert delta['ram_bytes']==80 and delta['flash_bytes']==6532
 else:assert delta['ram_bytes']==0 and delta['flash_bytes']==28
 if env=='heltec_mobile':assert len(x['removed_symbols'])==len(x['added_symbols'])==1 and 'exec_console_line' in x['added_symbols'][0]
cmd='/home/staszek/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-nm';bounds=[]
for state in ['s7b3-base','s7b3-final-2']:
 p=r/'.pio-measure'/state/'gateway/firmware.elf';s=subprocess.check_output([cmd,'-n','-S',str(p)]).decode();(q/'stack2'/('gateway-'+state+'-linked.nm')).write_text(s);names={line.split()[-1]:int(line.split()[0],16) for line in s.splitlines() if len(line.split())>=3};bounds.append({x:names[x] for x in ['__init_array_start','__init_array_end','__bss_start__','__bss_end__','__data_start__','__data_end__']})
summary['gateway']['link_boundaries']=bounds
(q/'resources-final.json').write_text(json.dumps(summary,indent=2)+'\n');print({env:x['delta'] for env,x in summary.items()})
