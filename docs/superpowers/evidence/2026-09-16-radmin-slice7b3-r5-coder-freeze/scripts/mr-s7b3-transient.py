from pathlib import Path
import importlib.util,subprocess,json
q=Path('/tmp/mr-codex-s7b3-0gt630zl');r=q/'gate';out=q/'stack2';spec=importlib.util.spec_from_file_location('abi',r/'tools/probe_board_abi.py');abi=importlib.util.module_from_spec(spec);spec.loader.exec_module(abi)
data=json.loads((out/'gateway-idedata.json').read_text());src=out/'transient-types.cpp';src.write_text('#include "firmware_remote_actions.cpp"\n'+''.join('char mr_size_'+n+'[sizeof(mrfw::'+n+')];\nchar mr_align_'+n+'[alignof(mrfw::'+n+')];\n' for n in ['EffectSink','ActivationReport']))
obj=out/'gateway-transient-types.o';cmd=abi.compile_command(data,src,obj);p=subprocess.run(cmd,cwd=r,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(out/'gateway-transient-types.log').write_bytes(p.stdout);assert p.returncode==0,p.stdout[-3000:]
s=subprocess.check_output([abi.binutil(data['cxx_path'],'nm'),'-S','--size-sort',str(obj)]).decode();(out/'gateway-transient-types.nm').write_text(s);print('\n'.join(x for x in s.splitlines() if 'mr_size_' in x or 'mr_align_' in x));(out/'transient-command.json').write_text(json.dumps(cmd,indent=2)+'\n')
