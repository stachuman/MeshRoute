"""Execute existing console-sink source witnesses with their exact stock controls."""
import ast,importlib.util,pathlib,json,sys
R=pathlib.Path('/home/staszek/MeshRoute');E=R/'docs/superpowers/evidence/2026-09-28-standalone-mobile-home-w2-precheck';S=pathlib.Path((E/'scratch-path.txt').read_text().strip())/'witness-audit';S.mkdir(exist_ok=True)
sys.path.insert(0,str(R/'tools/probe_console_sink'))
spec=importlib.util.spec_from_file_location('w2_structural',R/'tools/probe_console_sink/structural.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
paths=[R/f for f in ['src/firmware_commands.cpp','src/firmware_commands.h','src/fw_main.cpp','src/firmware_help.h','src/device_nv.h','src/firmware_config.cpp','lib/console/console_json.cpp','src/firmware_inbox.cpp','src/firmware_command_context.h']]
rows=m.check(*map(str,paths));out={'baseline':{'total':len(rows),'failed':[r for r in rows if not r[2]],'S23_S24':[r for r in rows if r[0] in ('S23','S24')]},'controls':[]}
tree=ast.parse((R/'tools/probe_console_sink/negctl.py').read_text())
for key in ['X14','X15','X16','X17']:
 nodes=[n for n in ast.walk(tree) if isinstance(n,ast.Tuple) and len(n.elts)==5 and isinstance(n.elts[0],ast.Constant) and str(n.elts[0].value).startswith(key+' ')];assert len(nodes)==1
 n=nodes[0];label=ast.literal_eval(n.elts[0]);idx={'CMDS':0,'FWMAIN':2}[n.elts[1].id];old=ast.literal_eval(n.elts[2]);new=ast.literal_eval(n.elts[3]);need=ast.literal_eval(n.elts[4]);src=paths[idx].read_text(); assert src.count(old)==1
 p=S/(key+'.cpp');p.write_text(src.replace(old,new));args=paths.copy();args[idx]=p;rr=m.check(*map(str,args));bad=[x[0] for x in rr if not x[2]]
 assert all(k in bad for k in need),(key,bad)
 out['controls'].append({'label':label,'source':str(paths[idx].relative_to(R)),'matches':src.count(old),'required':need,'failed_rows':bad,'old':old,'new':new})
(E/'witness-audit.json').write_text(json.dumps(out,indent=2)+'\n');print(out['baseline']);print([(x['label'].split()[0],x['failed_rows']) for x in out['controls']])
