from pathlib import Path
import importlib.util,sys,subprocess,json,hashlib,re
sys.dont_write_bytecode=True
root=Path('/home/staszek/MeshRoute');out=Path('/tmp/mr-w0-qa-akh045fd/stack-proof');out.mkdir(exist_ok=True)
spec=importlib.util.spec_from_file_location('qa_abi',root/'tools/probe_board_abi.py');abi=importlib.util.module_from_spec(spec);sys.modules[spec.name]=abi;spec.loader.exec_module(abi)
data=abi.idedata('gateway');obj=out/'firmware_config.o';cmd=[x for x in abi.compile_command(data,root/'src/firmware_config.cpp',obj) if x!='-flto']+['-fstack-usage','-fno-lto'];p=subprocess.run(cmd,cwd=root,capture_output=True);(out/'compile.log').write_bytes(p.stdout+p.stderr);assert p.returncode==0,p.stderr[-1000:]
su=obj.with_suffix('.su');rows=[s for s in su.read_text().splitlines() if any(t in s for t in ['id_candidate_from_live','rename_node','handle_cfg_set','0(const char*, size_t)'])]
objdump=Path(cmd[0]).with_name('arm-none-eabi-objdump');d=subprocess.run([str(objdump),'-drC',str(obj)],capture_output=True,check=True);(out/'disassembly.txt').write_bytes(d.stdout)
text=d.stdout.decode();blocks=[]
for block in text.split('\n\n'):
 if 'rename_node' in block and (re.search(r'<mrfw::rename_node\(',block) or '<mrfw::handle_cfg_set(' in block):blocks.append(block)
(out/'relevant-disassembly.txt').write_text('\n\n'.join(blocks)+'\n')
report={'kind':'Fresh compile-only gateway -fstack-usage and object disassembly; no product edits; not a runtime stack high-water mark.','command':cmd,'exit':p.returncode,'source_sha256':hashlib.sha256((root/'src/firmware_config.cpp').read_bytes()).hexdigest(),'su_rows':rows,'su_sha256':hashlib.sha256(su.read_bytes()).hexdigest(),'disassembly_sha256':hashlib.sha256(d.stdout).hexdigest()};(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'exit':p.returncode,'rows':rows},indent=2))
