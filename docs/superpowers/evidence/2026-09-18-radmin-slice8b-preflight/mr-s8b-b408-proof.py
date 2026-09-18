from pathlib import Path
import subprocess,json
q=Path('/tmp/mr-s8b-r2-active').read_text().strip();q=Path(q);r=q/'snapshot';libs=sorted((r/'.pio/build/native').glob('lib*/lib*.a'));assert any(p.name=='libcore.a' for p in libs)
cmd=['g++','-std=gnu++20','-DMESHROUTE_NATIVE=1','-DMR_N_LAYERS=2','-ffunction-sections','-fdata-sections']
for d in ['lib/core','lib/hal','lib/console','lib/monocypher/src','src','test']:cmd+=['-I'+str(r/d)]
cmd+=[str(q/'b408-proof.cpp'),'-Wl,--gc-sections','-Wl,--start-group']+[str(p) for p in libs]+['-Wl,--end-group','-o',str(q/'b408-proof')]
with (q/'logs/b408-compile.log').open('wb') as f:c=subprocess.run(cmd,cwd=r,stdout=f,stderr=subprocess.STDOUT)
assert c.returncode==0,(q/'logs/b408-compile.log').read_text()[-2000:]
with (q/'logs/b408-run.log').open('wb') as f:p=subprocess.run([str(q/'b408-proof')],stdout=f,stderr=subprocess.STDOUT)
(q/'b408-commands.json').write_text(json.dumps(dict(compile=cmd,compile_exit=c.returncode,run_exit=p.returncode),indent=2)+'\n');print((q/'logs/b408-run.log').read_text());assert p.returncode==0
