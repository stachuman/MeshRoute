from pathlib import Path
import hashlib, importlib.util, json, re, subprocess, sys
r=Path('/home/staszek/MeshRoute'); s=Path('/tmp/mr-s10-active').read_text().strip(); s=Path(s)
sys.path.insert(0,str(r/'tools'))
import probe_board_abi as abi
node=(r/'lib/core/node.h').read_text(); nv=(r/'src/device_nv.h').read_text()
node,n=re.subn(r'    // Inert legacy admin mirrors retained until Slice 10.*?\n#endif\n','',node,count=1,flags=re.S);assert n==1
node,n=re.subn(r'    // ---- REMOTE-MGMT \(Node-global\) ----\n#if MR_FEAT_RADMIN_ACCEPT\n.*?\n#endif\n','',node,count=1,flags=re.S);assert n==1
node=node.replace('static_assert(sizeof(Node) == 235248,','static_assert(sizeof(Node) == 235208,')
nv,n=re.subn(r'    // v20: remote-management admin auth.*?\n    uint8_t  admin_provisioned[^\n]*\n','',nv,count=1,flags=re.S);assert n==1
nv=nv.replace('sizeof(Blob) == 280','sizeof(Blob) == 240').replace('remote_action_activation_ms) == 276','remote_action_activation_ms) == 236')
sh=s/'shadow';sh.mkdir();(sh/'node.h').write_text(node);(sh/'device_nv.h').write_text(nv)
fields={'node':'sizeof(meshroute::Node)','blob':'sizeof(mrnv::Blob)','blob_align':'alignof(mrnv::Blob)','activation_offset':'offsetof(mrnv::Blob, remote_action_activation_ms)','intro_offset':'offsetof(mrnv::Blob, intro_attach)','team_pub_offset':'offsetof(mrnv::Blob, team_ch_pub)','team_id_offset':'offsetof(mrnv::Blob, team_key_team_id)'}
tu='#include "node.h"\n#include "device_nv.h"\n'+''.join(f'char mr_abi_{k}[{v}];\n' for k,v in fields.items())
(s/'layout.cpp').write_text(tu);results={}
for env in ('native','gateway','heltec_mobile'):
    data=abi.idedata(env);(s/f'{env}-idedata.json').write_text(json.dumps(data,indent=2))
    results[env]={}
    for arm in ('base','shadow'):
        obj=s/f'{env}-{arm}.o';cmd=abi.compile_command(data,s/'layout.cpp',obj)
        if arm=='shadow':cmd[1:1]=['-I'+str(sh)]
        (s/f'{env}-{arm}-command.json').write_text(json.dumps(cmd))
        p=subprocess.run(cmd,cwd=r,capture_output=True,text=True);(s/f'{env}-{arm}-compile.log').write_text(p.stdout+p.stderr);assert p.returncode==0,(env,arm,p.stderr[-1500:])
        results[env][arm]=abi.read_sizes(abi.binutil(data['cxx_path'],'nm'),obj)
print(json.dumps(results,indent=2));(s/'layouts.json').write_text(json.dumps(results,indent=2)+'\n')
cmd=['python3','tools/probe_console_sink/structural.py','src/firmware_commands.cpp','src/firmware_commands.h','src/fw_main.cpp','src/firmware_help.h','src/device_nv.h','src/firmware_config.cpp','lib/console/console_json.cpp','src/firmware_inbox.cpp','src/firmware_command_context.h']
p=subprocess.run(cmd,cwd=r,capture_output=True,text=True);(s/'structural-base.log').write_text(p.stdout+p.stderr);assert p.returncode==0;assert '83 passed / 0 failed / 83 total' in p.stdout
print(p.stdout.splitlines()[-1])
runner=(r/'tools/probe_console_sink/run.sh').read_text();fn=runner[runner.index('pin_fail=0\npin_cmp()'):runner.index('\n# ⚠ THE SELFTEST HOOK.')]
checks=[]
for name,observed,expected in [('structural',83,83),('structural',84,83),('controls',149,149),('controls',152,149)]:
    script=fn+f'\npin_cmp {name} {observed} {expected}\nexit "$pin_fail"\n'
    p=subprocess.run(['bash','-c',script],capture_output=True,text=True)
    assert p.returncode==(0 if observed==expected else 1)
    checks.append({'name':name,'observed':observed,'pin':expected,'exit':p.returncode,'stdout':p.stdout})
(s/'stock-pin-comparator.json').write_text(json.dumps(checks,indent=2)+'\n');print(json.dumps(checks,indent=2))
# Real host load/save wrappers with a synthetically valid current record: host is an absent NV backend.
(s/'host-nv.cpp').write_text('#include "device_nv.h"\n#include <cstdio>\nint main(){mrnv::Blob b{}; b.magic=mrnv::kMagic;b.version=mrnv::kVersion; bool l=mrnv::load(b),w=mrnv::save(b);std::printf("host load=%d save=%d\\n",l,w);return l||w;}\n')
p=subprocess.run(['g++','-std=c++20','-I'+str(r/'src'),'-I'+str(r/'lib/core'),str(s/'host-nv.cpp'),'-o',str(s/'host-nv')],capture_output=True,text=True);assert p.returncode==0,p.stderr
p=subprocess.run([str(s/'host-nv')],capture_output=True,text=True);assert p.returncode==0;(s/'host-nv.log').write_text(p.stdout);print(p.stdout)
