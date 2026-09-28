# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# W2 §4.1 step 5 — replay console-sink's X14–X17 (their EXACT old/new strings, read from negctl.py by AST — never
# imported) on scratch copies, run the stock structural.py against each, and list EVERY row the edit reddens.
# The stock run's own output names only each control's target row. Usage: python3 console_sink_replay.py <scratch> <out.json>
import ast, json, os, re, shutil, subprocess, sys
R = '/home/staszek/MeshRoute'; SCR, OUT = sys.argv[1], sys.argv[2]
t = ast.parse(open(f'{R}/tools/probe_console_sink/negctl.py', encoding='utf-8').read())
names = {n.targets[0].id: n.value for n in t.body if isinstance(n, ast.Assign) and len(n.targets) == 1
         and isinstance(n.targets[0], ast.Name) and isinstance(n.value, ast.Constant)}
ctl = {}
for n in ast.walk(t):
    if isinstance(n, ast.Tuple) and len(n.elts) == 5 and isinstance(n.elts[0], ast.Constant) \
       and isinstance(n.elts[0].value, str) and re.match(r'X1[4-7] ', n.elts[0].value):
        ctl[n.elts[0].value.split(' ', 1)[0]] = (n.elts[0].value, n.elts[1].id, ast.literal_eval(n.elts[2]),
                                                 ast.literal_eval(n.elts[3]), ast.literal_eval(n.elts[4]))
ARGS = ['src/firmware_commands.cpp', 'src/firmware_commands.h', 'src/fw_main.cpp', 'src/firmware_help.h', 'src/device_nv.h',
        'src/firmware_config.cpp', 'lib/console/console_json.cpp', 'src/firmware_inbox.cpp', 'src/firmware_command_context.h']
def rows(paths):
    r = subprocess.run(['python3', f'{R}/tools/probe_console_sink/structural.py', *paths], capture_output=True, text=True)
    return [m.group(1) for m in re.finditer(r'^\s+FAIL (S\d+)', r.stdout, re.M)], r.stdout
res = {}
base_fail, _ = rows([f'{R}/{a}' for a in ARGS])
res['live_failing_rows'] = base_fail
for cid in ('X14', 'X15', 'X16', 'X17'):
    label, var, old, new, targets = ctl[cid]
    srcrel = {'CMDS': 'src/firmware_commands.cpp', 'FWMAIN': 'src/fw_main.cpp'}.get(var)
    real = open(f'{R}/{srcrel}', encoding='utf-8').read()
    n = real.count(old)
    d = f'{SCR}/{cid}'; os.makedirs(d, exist_ok=True)
    mut = f'{d}/{os.path.basename(srcrel)}'; open(mut, 'w', encoding='utf-8').write(real.replace(old, new, 1))
    paths = [mut if a == srcrel else f'{R}/{a}' for a in ARGS]
    failing, _ = rows(paths)
    res[cid] = {'label': label, 'source': srcrel, 'anchor_matches': n, 'edit_old': old, 'edit_new': new,
                'declared_targets': list(targets), 'failing_rows': failing,
                'targets_red': all(x in failing for x in targets)}
    print(f"{cid} ({srcrel}, anchor x{n}) targets {list(targets)} -> failing rows {failing}")
res['verdict'] = 'PASS' if not base_fail and all(res[c]['targets_red'] and res[c]['anchor_matches'] == 1 for c in ('X14', 'X15', 'X16', 'X17')) else 'FAIL'
json.dump(res, open(OUT, 'w'), indent=1, ensure_ascii=False)
print('live failing rows', base_fail, '| REPLAY', res['verdict'])
