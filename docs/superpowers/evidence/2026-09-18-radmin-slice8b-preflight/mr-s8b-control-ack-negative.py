from pathlib import Path
import subprocess,json,hashlib
q=Path('/tmp/mr-s8b-r2-active').read_text().strip();q=Path(q);r=q/'snapshot';source=r/'lib/core/remote_client.cpp';old=source.read_text();needle='state == Phase::request_ready && (p.core.flags & kAck)';assert old.count(needle)==1
mutant=q/'control-execute-only.cpp';mutant.write_text(old.replace(needle,'state == Phase::request_ready && opcode(p) == static_cast<uint8_t>(RemoteCmdOpcode::auth_execute) && (p.core.flags & kAck)'))
cmd=json.loads((q/'control-ack-commands.json').read_text())['compile'];cmd.insert(cmd.index('-Wl,--gc-sections'),str(mutant));cmd[-1]=str(q/'control-ack-corrected-private')
with (q/'logs/control-ack-negative-compile.log').open('wb') as f:rc=subprocess.run(cmd,cwd=r,stdout=f,stderr=subprocess.STDOUT).returncode
assert rc==0
with (q/'logs/control-ack-negative-run.log').open('wb') as f:rc=subprocess.run([cmd[-1]],stdout=f,stderr=subprocess.STDOUT).returncode
log=(q/'logs/control-ack-negative-run.log').read_text();assert rc==1 and '36 checks / 2 failed' in log;assert source.read_text()==old
(q/'control-ack-negative.json').write_text(json.dumps(dict(compile=cmd,run_exit=rc,note='Private execute-only guard makes the two current-leak assertions RED; no production edit.',source_sha256=hashlib.sha256(source.read_bytes()).hexdigest()),indent=2)+'\n');print(log)
