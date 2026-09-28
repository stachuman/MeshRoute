"""QA D6/P7 source-reader census; run as W2 chain step 9, never changes inputs."""
from pathlib import Path
import subprocess,json,re
R=Path('/home/staszek/MeshRoute'); Q=Path(__file__).resolve().parent
patterns=[
'probe_board_ui/run.sh','W49','W51','W54','W54-help','keeps the shared dispatch fallback','keeps the ONE shared seam call',
'bool dispatch(const char* line, size_t len, Print& out, CommandTransport transport) {',
'static void handle_teststatus(Print& out) {','the `ui` dispatch arm',
'const mrfw::LineExec ex = mrfw::exec_console_line(line, len, mrfw::LineFormat::json, ls, out, cap, ctx);',
'if (ex.state == mrfw::LineExec::State::streamed) { ls.flush(); return 0; }','out.println(F("ui"));','firmware_help.h',
'M103','probe_ui_model_mutations.py','239','ACCEPTED AND IGNORED','Nothing this probe','FIDELITY LIMIT','probe_board_ui/fakes','fakes/Arduino.h',
'Control (c) is the plausible SIMPLIFICATION','transcript_main.cpp','`structural.py` S24','`structural.py` S27','md5_sources']
rows=[]
for p in patterns:
 run=subprocess.run(['rg','-n','-F','--',p,'lib','src','test','tools'],cwd=R,capture_output=True,text=True)
 assert run.returncode in (0,1),run.stderr
 rows.append({'pattern':p,'files':sorted({x.split(':',1)[0] for x in run.stdout.splitlines()}),'matches':run.stdout.splitlines()})
(Q/'step9-reader-audit.json').write_text(json.dumps(rows,indent=2)+'\n')
with (Q/'step9-reader-audit.txt').open('w') as out:
 for x in rows:out.write(x['pattern']+'\n'+'\n'.join(x['matches'])+'\n\n')
# The W50 label drift is pre-existing; do not edit this outside-fence comment.
p='tools/probe_board_ui/run.sh'; source=(R/p).read_text(); old=subprocess.check_output(['git','show','8079e54:'+p],cwd=R).decode()
comment='# ★ Control (c) is the plausible SIMPLIFICATION, not a deletion: keep the load, drop the console wrapper.'
assert source.count(comment)==old.count(comment)==1
lines=source.splitlines(); start=next(i for i,line in enumerate(lines) if line.startswith('wchk_in "$FW_MAIN" "W50 '))
end=start
while lines[end].endswith('\\'):end+=1
call='\n'.join(lines[start:end+1])
assert 'preset_catalog' in call
record={'path':p,'comment_line':source[:source.index(comment)].count('\n')+1,'comment':comment,'present_at_base':True,'call':call,'measured_controls':3,'wrapper_removal_ordinal':2,'relocation_ordinal':3,'gate':'all three W50 controls executed RED in independent stock default trace; no executable defect','disposition':'register as B461, nonblocking comment-only follow-up','QA_setup_correction':'The first multiline regex stopped after control 1 and failed its preset_catalog assertion. Replaced with explicit shell-continuation collection; source and prior gate results unchanged.'}
(Q/'b461-comment-drift.json').write_text(json.dumps(record,indent=2)+'\n')
print('Reader census complete:',len(rows),'patterns; W50 drift reproduced at base and freeze')
