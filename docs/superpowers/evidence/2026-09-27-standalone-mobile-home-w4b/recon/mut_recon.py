import ast, subprocess, sys
def lists(src):
    t = ast.parse(src); out = {}
    for n in t.body:
        if isinstance(n, ast.Assign) and isinstance(n.targets[0], ast.Name) and n.targets[0].id.startswith('MUTS_') \
           and isinstance(n.value, ast.List):
            out[n.targets[0].id] = [ast.literal_eval(e) for e in n.value.elts]
    return out
b = lists(subprocess.run(['git', 'show', 'HEAD:tools/probe_ui_model_mutations.py'], capture_output=True, text=True).stdout)
c = lists(open('tools/probe_ui_model_mutations.py').read())
changed_lists = [k for k in sorted(set(b) | set(c)) if b.get(k) != c.get(k)]
print('batteries whose entry list changed:', ', '.join(changed_lists))
for k in changed_lists:
    be = {e[0].split()[0]: e for e in b.get(k, [])}; ce = {e[0].split()[0]: e for e in c.get(k, [])}
    kept = [i for i in be if i in ce and be[i] == ce[i]]
    rean = [i for i in be if i in ce and be[i] != ce[i]]
    ret = [i for i in be if i not in ce]; new = [i for i in ce if i not in be]
    print(f'{k}: base {len(be)} -> {len(ce)}; kept verbatim {len(kept)}; changed {len(rean)} [{" ".join(rean)}]; retired {len(ret)} [{" ".join(ret)}]; new {len(new)} [{" ".join(new)}]')
    for i in rean:
        what = []
        if be[i][0] != ce[i][0]: what.append('label')
        if be[i][1] != ce[i][1]: what.append('anchor')
        if be[i][2] != ce[i][2]: what.append('replacement')
        print(f'    {i}: {"+".join(what)}')
