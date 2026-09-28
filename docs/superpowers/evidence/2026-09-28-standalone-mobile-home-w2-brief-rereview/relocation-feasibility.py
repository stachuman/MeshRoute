"""Labelled synthetic feasibility check for r3's whole-input transformation. Not an installed control."""
from pathlib import Path
import hashlib,json,re,subprocess,tempfile
ROOT=Path('/home/staszek/MeshRoute'); E=Path(__file__).resolve().parent
src=(ROOT/'src/firmware_commands.cpp').read_text();runner=(ROOT/'tools/probe_board_ui/run.sh').read_text()
dst='static void handle_teststatus(Print& out) {'; guard='#if MR_FEAT_OLED   // ★ [[B255]] the `ui` dispatch arm'
a=src.index(guard);block=src[a:src.index('\n',src.index('#endif',a))+1];assert src.count(block)==src.count(dst)==1
# Existing W49 as shipped, with just its current definition prefix selected (no production edit).
lines=runner.splitlines(keepends=True);bits=[next(l for l in lines if l.startswith(n+'()')) for n in ['fn_body','fn_flat']]
a=next(i for i,l in enumerate(lines) if l.startswith('w49()'));b=next(i for i in range(a,len(lines)) if lines[i].strip()=='}');bits+=lines[a:b+1];bits += [l for l in lines if l.startswith(('UI_ARM=','UI_ARM_GUARDED='))]
w49=''.join(bits)+"\nDISPATCH_SIG='bool dispatch(const char* line, size_t len, Print& out, CommandTransport transport) {'\nw49 \"$1\"\n"
def bre(s):return ''.join('\\n' if c=='\n' else '\\'+c if c in '[]\\.^$*/' else c for c in s)
recipe=':a\nN\n$!ba\ns/\\('+bre(dst+'\n')+'\\)\\(.*\\)\\('+bre(block)+'\\)/\\1\\3\\2/\n'
needle='handle_ui(line + 2, len - 2, out); return true;';rows=[]
with tempfile.TemporaryDirectory(prefix='w2-r3-feasibility-') as tmp:
 tmp=Path(tmp);(tmp/'recipe.sed').write_text(recipe);(tmp/'w49.sh').write_text(w49)
 for label,before in [('both_anchors_present',src),('destination_missing',src.replace(dst,dst.replace('handle_teststatus','handle_teststatus_renamed'))),('source_marker_changed',src.replace(guard,guard+' (renamed marker)'))]:
  p=tmp/'in.cpp';p.write_text(before);live=subprocess.run(['bash',str(tmp/'w49.sh'),str(p)],capture_output=True).returncode
  r=subprocess.run(['sed','-f',str(tmp/'recipe.sed'),str(p)],capture_output=True,text=True,check=True);q=tmp/'out.cpp';q.write_text(r.stdout);verdict=subprocess.run(['bash',str(tmp/'w49.sh'),str(q)],capture_output=True).returncode
  dispatch=r.stdout[r.stdout.index('bool dispatch(const char* line, size_t len, Print& out, CommandTransport transport) {'):];dispatch=dispatch[:dispatch.index('\n}\n')]
  rows.append({'case':label,'baseline_w49_exit':live,'destination_matches':before.count(dst),'source_block_matches':before.count(block),'output_identical':r.stdout==before,'arm_count_file':r.stdout.count(needle),'arm_count_dispatch':dispatch.count(needle),'output_w49_exit':verdict})
assert all(r['baseline_w49_exit']==0 for r in rows)
assert rows[0]['arm_count_file']==1 and rows[0]['arm_count_dispatch']==0 and rows[0]['output_w49_exit']==1
assert all(r['output_identical'] and r['output_w49_exit']==0 for r in rows[1:])
out={'scope':'Synthetic feasibility only; coder must implement and prove its own frozen control','source_sha256':hashlib.sha256(src.encode()).hexdigest(),'stock_runner_sha256':hashlib.sha256(runner.encode()).hexdigest(),'sed_recipe':recipe,'stock_w49_current_signature':w49,'cases':rows};(E/'relocation-feasibility.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(rows,indent=2))
