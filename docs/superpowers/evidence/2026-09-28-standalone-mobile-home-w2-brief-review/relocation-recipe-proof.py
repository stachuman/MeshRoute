"""Synthetic characterization of the brief's suggested insertion-flag recipe, NOT a W2 implementation."""
from pathlib import Path
import subprocess,tempfile,json,hashlib,re
ROOT=Path('/home/staszek/MeshRoute'); E=Path(__file__).resolve().parent
src=(ROOT/'src/firmware_commands.cpp').read_text(); runner=(ROOT/'tools/probe_board_ui/run.sh').read_text()
dst='static void handle_teststatus(Print& out) {'
guard='#if MR_FEAT_OLED   // ★ [[B255]] the `ui` dispatch arm'
start=src.index(guard); block=src[start:src.index('\n',src.index('#endif',start))+1].rstrip('\n'); assert len(block.splitlines())==3 and src.count(block)==1 and src.count(dst)==1
# Normal-output sed: insert and mark at the earlier destination; later delete the exact source block if marked.
def bre(s):return ''.join('\\'+c if c in '[]\\.^$*/' else c for c in s)
pat='\\n'.join(bre(l) for l in block.splitlines())
recipe='/^'+bre(dst)+'$/ {\n h\n a\\\n'+block.replace('\n','\\\n')+'\n}\n/^'+bre(guard)+'$/ {\n x\n /./ {\n  x\n  N\n  N\n  /^'+pat+'$/d\n  b\n }\n x\n}\n'
# Extract and execute the stock W49 predicate, only selecting the current dispatch signature.
lines=runner.splitlines(keepends=True); bits=[]
for name in ['fn_body','fn_flat']:bits.append(next(l for l in lines if l.startswith(name+'()')))
a=next(i for i,l in enumerate(lines) if l.startswith('w49()'));b=next(i for i in range(a,len(lines)) if lines[i].strip()=='}');bits+=lines[a:b+1]
bits+=[l for l in lines if l.startswith(('UI_ARM=','UI_ARM_GUARDED='))]
script=''.join(bits)+"\nDISPATCH_SIG='bool dispatch(const char* line, size_t len, Print& out, CommandTransport transport) {'\nw49 \"$1\"\n"
needle='handle_ui(line + 2, len - 2, out); return true;'
rows=[]
with tempfile.TemporaryDirectory(prefix='w2-brief-recipe-') as tmp:
 tmp=Path(tmp);(tmp/'recipe.sed').write_text(recipe);(tmp/'w49.sh').write_text(script)
 cases={'both_present':src,'destination_missing':src.replace(dst,dst.replace('handle_teststatus','handle_teststatus_renamed')),'source_anchor_missing_only_comment_changed':src.replace(guard,guard+' (renamed marker)')}
 for label,before in cases.items():
  p=tmp/'before.cpp';p.write_text(before); base=subprocess.run(['bash',str(tmp/'w49.sh'),str(p)],capture_output=True,text=True)
  after=subprocess.run(['sed','-f',str(tmp/'recipe.sed'),str(p)],capture_output=True,text=True,check=True).stdout
  q=tmp/'after.cpp';q.write_text(after); verdict=subprocess.run(['bash',str(tmp/'w49.sh'),str(q)],capture_output=True,text=True)
  rows.append({'synthetic_case':label,'source_block_matches':before.count(block),'destination_matches':before.count(dst),'baseline_w49_exit':base.returncode,'unchanged_output':before==after,'arm_count_before':before.count(needle),'arm_count_after':after.count(needle),'mutated_w49_exit':verdict.returncode})
assert rows[0]['arm_count_after']==1 and rows[0]['mutated_w49_exit']==1
assert rows[1]['unchanged_output']
assert rows[2]['baseline_w49_exit']==0 and not rows[2]['unchanged_output'] and rows[2]['arm_count_after']==2 and rows[2]['mutated_w49_exit']==1
out={'kind':'labelled synthetic example of suggested recipe; no candidate or production edit','source_sha256':hashlib.sha256(src.encode()).hexdigest(),'recipe_sed':recipe,'stock_w49_current_signature':script,'cases':rows};(E/'relocation-recipe-proof.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(rows,indent=2))
