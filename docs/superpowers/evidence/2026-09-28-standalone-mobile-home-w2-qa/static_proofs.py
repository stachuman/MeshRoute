"""QA-only W2 comment and mutation-list preservation proofs. Does not import the harness."""
from pathlib import Path
import ast, difflib, hashlib, json, subprocess, tempfile
ROOT=Path('/home/staszek/MeshRoute')
OUT=Path(__file__).resolve().parent
BASE='8079e545fb2e1065c2bbc347eb9a66160f6cbf8e'
def base(path): return subprocess.check_output(['git','show',f'{BASE}:{path}'],cwd=ROOT).decode()
def digest(s): return hashlib.sha256(s.encode()).hexdigest()
comments=[]
for name in ('tools/probe_board_ui/fakes/Arduino.h','tools/probe_inbox_verbs/transcript_main.cpp'):
    before=base(name); after=(ROOT/name).read_text(); a=before.splitlines(True); b=after.splitlines(True)
    changes=[]
    for tag,i,j,k,l in difflib.SequenceMatcher(None,a,b,autojunk=False).get_opcodes():
        if tag=='equal': continue
        assert all(line.lstrip().startswith('//') or not line.strip() for line in a[i:j]+b[k:l])
        changes.append({'old_lines':[i+1,j],'new_lines':[k+1,l],'old':a[i:j],'new':b[k:l]})
    with tempfile.TemporaryDirectory(prefix='w2-qa-comments-') as tmp:
        def tokens(text):
            p=Path(tmp)/'input.cpp'; p.write_text(text)
            run=subprocess.run(['g++','-fpreprocessed','-dD','-E','-P',str(p)],capture_output=True,text=True,check=True)
            return run.stdout
        t1=tokens(before); t2=tokens(after)
        assert t1==t2
        # A synthetic added executable declaration must be seen.
        assert tokens(after+'\nint w2_qa_executable_control;\n')!=t2
    comments.append({'path':name,'hunks':changes,'preprocessed_equal':True,'preprocessed_sha256':digest(t2),'added_code_control':'RED'})
(OUT/'step2-comments.json').write_text(json.dumps(comments,indent=2)+'\n')
name='tools/probe_ui_model_mutations.py'; before=base(name);after=(ROOT/name).read_text()
a=ast.parse(before); b=ast.parse(after)
def assign(tree,name):return next(n for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id==name for t in n.targets))
lst=assign(a,'MUTS_MODEL').value
removed=[n for n in lst.elts if ast.literal_eval(n)[0].startswith('M103 ')]
assert len(removed)==1
old_count=len(lst.elts); lst.elts.remove(removed[0])
assert ast.dump(a,include_attributes=False)==ast.dump(b,include_attributes=False)
oldlines=before.splitlines(True); newlines=after.splitlines(True)
ops=[op for op in difflib.SequenceMatcher(None,oldlines,newlines,autojunk=False).get_opcodes() if op[0]!='equal']
assert len(ops)==1
_,i,j,k,l=ops[0]
assert all(line.lstrip().startswith('#') or not line.strip() for line in newlines[k:l])
code_removed=''.join(x for x in oldlines[i:j] if not x.lstrip().startswith('#'))
assert ast.literal_eval(code_removed.strip().rstrip(','))==ast.literal_eval(removed[0])
labels=[ast.literal_eval(n)[0] for n in assign(b,'MUTS_MODEL').value.elts]
assert len(labels)==238 and not any(n.startswith('M103 ') for n in labels)
for label in ('M100','M101','M102','M105'):assert sum(s.startswith(label+' ') for s in labels)==1
h25=[ast.literal_eval(n)[0] for n in assign(b,'MUTS_W4BHOME').value.elts if ast.literal_eval(n)[0].startswith('H25 ')]
assert len(h25)==1
result={'base_count':old_count,'final_count':len(labels),'only_AST_delta':'remove M103 tuple','only_text_delta':'one hunk: M103 tuple and adjacent comments replaced by retirement comments','retired_tuple':ast.literal_eval(removed[0]),'live_witnesses':[s for s in labels if s.split()[0] in ['M100','M101','M102','M105']]+h25,'labels':labels,'retirement_text':''.join(newlines[k:l])}
(OUT/'step3-m103.json').write_text(json.dumps(result,indent=2)+'\n')
print('Step 2: both comment-only proofs PASS; executable controls RED')
print(f'Step 3: AST/text preservation PASS, model {old_count} -> {len(labels)}; five witnesses live')
