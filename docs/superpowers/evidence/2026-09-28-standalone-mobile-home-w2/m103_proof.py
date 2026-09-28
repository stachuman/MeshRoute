# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# W2 §4.1 step 3 — the M103 proof, by AST (never by importing the harness: its top level runs a battery).
#   1. no active M103 entry anywhere in the final file (every list literal of tuples is scanned);
#   2. the final module's AST == the base (HEAD) module's AST with ONLY M103's tuple removed from MUTS_MODEL — so every
#      other entry, every target and every worker line is identical (comments are not in an AST, hence also 3);
#   3. the file outside the one changed hunk is byte-identical, and the hunk touches only comment lines + M103's tuple;
#   4. the retirement record carries the four §2.5 items; the witnesses resolve to live entries of the named targets.
# Usage: python3 m103_proof.py <out.json>
import ast, difflib, json, subprocess, sys
R = '/home/staszek/MeshRoute'; P = 'tools/probe_ui_model_mutations.py'
base_src = subprocess.check_output(['git', 'show', 'HEAD:' + P], cwd=R).decode()
final_src = open(f'{R}/{P}', encoding='utf-8').read()
def lists(tree):
    out = {}
    for n in tree.body:
        if isinstance(n, ast.Assign) and len(n.targets) == 1 and isinstance(n.targets[0], ast.Name) and isinstance(n.value, ast.List):
            out[n.targets[0].id] = n
    return out
def entry_id(e):
    if isinstance(e, ast.Tuple) and e.elts and isinstance(e.elts[0], (ast.Constant, ast.JoinedStr)):
        try: return ast.literal_eval(e.elts[0]).split(' ', 1)[0]
        except Exception: return None
bt, ft = ast.parse(base_src), ast.parse(final_src)
res = {}
# 1. no active M103 anywhere
active = [(name, entry_id(e)) for name, n in lists(ft).items() for e in n.value.elts if entry_id(e) == 'M103']
res['active_M103_in_final'] = active
# 2. AST equality after removing M103 from the base's MUTS_MODEL
bl = lists(bt)['MUTS_MODEL']
base_ids = [entry_id(e) for e in bl.value.elts]
idx = [i for i, x in enumerate(base_ids) if x == 'M103']
res['base_MUTS_MODEL_entries'] = len(base_ids); res['base_M103_indices'] = idx
bl.value.elts = [e for e in bl.value.elts if entry_id(e) != 'M103']
res['final_MUTS_MODEL_entries'] = len(lists(ft)['MUTS_MODEL'].value.elts)
res['ast_equal_after_removing_M103'] = ast.dump(bt) == ast.dump(ft)
# 3. the textual hunk
b_lines = base_src.splitlines(keepends=True); f_lines = final_src.splitlines(keepends=True)
ops = [o for o in difflib.SequenceMatcher(a=b_lines, b=f_lines, autojunk=False).get_opcodes() if o[0] != 'equal']
res['hunks'] = [{'base_lines': [i1 + 1, i2], 'final_lines': [j1 + 1, j2]} for _, i1, i2, j1, j2 in ops]
removed = [l for _, i1, i2, _, _ in ops for l in b_lines[i1:i2]]
added = [l for _, _, _, j1, j2 in ops for l in f_lines[j1:j2]]
res['added_all_comments'] = all(l.lstrip().startswith('#') for l in added)
non_comment_removed = [l.rstrip('\n') for l in removed if not l.lstrip().startswith('#')]
res['removed_non_comment_lines'] = non_comment_removed
res['removed_non_comment_is_exactly_M103_tuple'] = (len(non_comment_removed) == 3 and non_comment_removed[0].lstrip().startswith('("M103 '))
# 4. the record's four items + witnesses resolve
record = ''.join(added)
items = {'id': 'M103' in record,
         'old_edit': all(s in record for s in ('close_settings_menu', '_st.cursor = 0', '_cfg_sel_valid = false')),
         'reason': all(s in record for s in ('go_menu_home', 'B457', 'neither renders the cursor nor reads the flag')),
         'witnesses': all(s in record for s in ('M100', 'M101', 'M102', 'M105', 'H25', '`model`', '`w4bhome`'))}
res['record_items'] = items
# resolve MUTS_BY_TARGET -> list names, then check the witnesses live where the record says
byt = next(n for n in ft.body if isinstance(n, ast.Assign) and getattr(n.targets[0], 'id', '') == 'MUTS_BY_TARGET').value
tmap = {ast.literal_eval(k): v.id for k, v in zip(byt.keys, byt.values) if isinstance(v, ast.Name)}
fl = lists(ft)
def ids_of(target): return [entry_id(e) for e in fl[tmap[target]].value.elts]
res['targets'] = {'model': tmap.get('model'), 'w4bhome': tmap.get('w4bhome')}
res['witnesses_live'] = {w: (w in ids_of('model')) for w in ('M100', 'M101', 'M102', 'M105')}
res['witnesses_live']['H25'] = 'H25' in ids_of('w4bhome')
res['M104_still_live'] = 'M104' in ids_of('model')
ok = (not active and len(idx) == 1 and res['ast_equal_after_removing_M103'] and len(ops) == 1 and res['added_all_comments']
      and res['removed_non_comment_is_exactly_M103_tuple'] and all(items.values()) and all(res['witnesses_live'].values())
      and res['final_MUTS_MODEL_entries'] == res['base_MUTS_MODEL_entries'] - 1)
res['verdict'] = 'PASS' if ok else 'FAIL'
json.dump(res, open(sys.argv[1], 'w'), indent=1, ensure_ascii=False)
print(f"active M103 in final: {active}; MUTS_MODEL {res['base_MUTS_MODEL_entries']} -> {res['final_MUTS_MODEL_entries']} (M103 at base index {idx})")
print(f"AST(final) == AST(base minus M103): {res['ast_equal_after_removing_M103']}; hunks {res['hunks']}; added all comments {res['added_all_comments']}; removed code = M103 tuple only {res['removed_non_comment_is_exactly_M103_tuple']}")
print(f"record items {items}; targets {res['targets']}; witnesses live {res['witnesses_live']}")
print('M103', res['verdict'])
