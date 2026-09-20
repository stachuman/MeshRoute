#!/usr/bin/env python3
import ast, hashlib, importlib.util, json, pathlib, subprocess
OUT=pathlib.Path('/tmp/mr-b434-b435-r1-rwlxb7cv')
spec=importlib.util.spec_from_file_location('gate',OUT/'gate.py');gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
after=gate.stage_hashes();before=json.loads((OUT/'stage-before.json').read_text())
gate.dump('stage-after.json',after)
assert before==after, [k for k in set(before)|set(after) if before.get(k)!=after.get(k)]
inputs=gate.repo_hashes();gate.dump('gate-inputs-after.json',inputs)
assert inputs==json.loads((OUT/'gate-inputs-before.json').read_text()),'frozen inputs moved'
source=(gate.ROOT/'tools/probe_ui_model_mutations.py').read_text();old=(OUT/'base-harness.py').read_text()
oldtree,newtree=ast.parse(old),ast.parse(source)
def calls(tree,name):
    return [ast.dump(n,include_attributes=False) for n in ast.walk(tree) if isinstance(n,ast.Call) and ast.unparse(n.func)==name]
oldexits=calls(oldtree,'sys.exit');newexits=calls(newtree,'sys.exit')
newexits.remove(ast.dump(ast.parse('sys.exit(selftest_unusable())').body[0].value,include_attributes=False));assert oldexits==newexits
for kind in ('RED','FAIL','VACUOUS'):
    def verdicts(tree):
        return [ast.dump(n,include_attributes=False) for n in ast.walk(tree) if isinstance(n,ast.Call) and ast.unparse(n.func)=='_verdict' and n.args and isinstance(n.args[0],ast.Constant) and n.args[0].value==kind]
    assert verdicts(oldtree)==verdicts(newtree),kind
# Compile-only filtering cannot be inferred from the old command: archive the installed implementation.
import platformio
pio_root=pathlib.Path(platformio.__file__).parent
snippets=[]
for name,lo,hi in [('test/cli.py',45,70),('test/runners/base.py',153,180),('test/runners/readers/native.py',90,132)]:
    path=pio_root/name; lines=path.read_text().splitlines()
    snippets.append({'path':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'first_line':lo,'last_line':hi,'source':'\n'.join(f'{i+1}: {lines[i]}' for i in range(lo-1,hi))})
gate.dump('platformio-source-evidence.json',{'version':platformio.__version__,'source':snippets})
summary={'result':'STOP-1 / HOLD — B436','candidate_completed':{'radmin8node':{'rc':1,'RED':0,'unusable':1}},'candidate_interrupted':['devicenv','radmin8client','radmin8verbs'],'not_started':57,'candidate_input_files':len(inputs),'stage_files_including_ignored_copy_inputs':len(before),'stage_before_after':'identical','shared_before_after':'identical before M1 finding registration','reference_same_mutant':{'rc':0,'RED':1,'unusable':0},'direct_mutant_binary_rc':1,'full_union_gate':'NOT PASSED / stopped at required off-floor condition','mutation_entries_target_map':'byte-identical','RED_FAIL_VACUOUS_calls':'AST-identical','existing_sys_exit_calls':'AST-identical'}
gate.dump('stop-audit.json',summary)
print(json.dumps(summary,indent=2))
