"""Compile-only stack-usage measurement, scratch source copies; not a runtime high-water or whole-program stack proof."""
from pathlib import Path
import json,importlib.util,sys,subprocess,tempfile,shutil
R=Path('/home/staszek/MeshRoute');E=Path(__file__).resolve().parent;A=R/'artifacts'/E.name;D=Path(tempfile.mkdtemp(prefix='w6-stack-'))
spec=importlib.util.spec_from_file_location('w6abi',R/'tools/probe_board_abi.py');abi=importlib.util.module_from_spec(spec);sys.modules[spec.name]=abi;spec.loader.exec_module(abi)
reply=json.loads((E/'reply-measure.json').read_text()); roots={'base':R/'src','v2_244':Path(reply['scratch'])/'src'}
small=D/'v2_boot_small';shutil.copytree(roots['v2_244'],small)
p=small/'firmware_ui_preset_verbs.h';s=p.read_text();at=s.index('inline mrnv::UiPresetRead preset_boot_restore(');tail=s[at:];assert tail.count('char b[kPresetLineMax];')==1;s=s[:at]+tail.replace('char b[kPresetLineMax];','char b[81];');p.write_text(s);roots['v2_boot_small']=small
result={'scope':__doc__,'scratch':str(D),'compiles':[]}
for variant,source in roots.items():
 for target in ['heltec_mobile','gateway']:
  data=abi.idedata(target)
  for unit in ['firmware_commands.cpp','fw_main.cpp']:
   # gateway stock has no OLED. Explicit alternate-profile compilation is only for pricing this dormant arm.
   name=variant+'-'+target+'-'+unit;obj=D/(name+'.o')
   cmd=abi.compile_command(data,source/unit,obj)
   cmd=[x for x in cmd if x!='-flto'];cmd+=['-fstack-usage','-fno-lto']
   if target=='gateway':cmd+=['-UMR_FEAT_OLED','-DMR_FEAT_OLED=1']
   out=subprocess.run(cmd,cwd=R,capture_output=True)
   (A/(name+'.log')).write_bytes(out.stdout+out.stderr)
   item=dict(variant=variant,target=target,unit=unit,command=cmd,exit=out.returncode,forced_oled=target=='gateway')
   su=obj.with_suffix('.su')
   if su.exists():
    shutil.copyfile(su,A/su.name);item['stack_entries']=su.read_text().splitlines()
   result['compiles'].append(item);(E/'stack-measure.json').write_text(json.dumps(result,indent=2)+'\n')
   print(name,out.returncode,flush=True)
   if out.returncode:print(out.stderr.decode(errors='replace')[-2000:],flush=True)
