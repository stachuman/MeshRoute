# AST census of tools/probe_ui_model_mutations.py — NEVER imports it. Reports every entry of the named batteries whose
# anchor does not occur exactly once in its target file.
import ast, sys
src = open('tools/probe_ui_model_mutations.py').read()
tree = ast.parse(src)
lists, target_src, by_target = {}, {}, {}
for node in tree.body:
    if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
        name = node.targets[0].id
        if name == 'TARGET_SRC' and isinstance(node.value, ast.Dict):
            for k, v in zip(node.value.keys, node.value.values):
                try: target_src[ast.literal_eval(k)] = ast.literal_eval(v)
                except Exception: pass
        elif name == 'MUTS_BY_TARGET' and isinstance(node.value, ast.Dict):
            for k, v in zip(node.value.keys, node.value.values):
                if isinstance(v, ast.Name): by_target[ast.literal_eval(k)] = v.id
        elif name.startswith('MUTS_') and isinstance(node.value, ast.List):
            ents = []
            for e in node.value.elts:
                try: ents.append(ast.literal_eval(e))
                except Exception: ents.append(None)
            lists[name] = ents
want = sys.argv[1:] or ['model', 'chrome', 'uistatus', 'sliceCbudget', 'w4aident']
for tgt in want:
    path = target_src.get(tgt); lst = lists.get(by_target.get(tgt, ''), [])
    text = open(path).read()
    bad = 0
    for e in lst:
        if e is None: print(f"{tgt}\tUNPARSED"); bad += 1; continue
        label, old = e[0], e[1]
        n = text.count(old)
        if n != 1:
            bad += 1; print(f"{tgt}\tcount={n}\t{label[:110]}")
    print(f"== {tgt} ({path}): {len(lst)} entries, {bad} not exactly-once")
