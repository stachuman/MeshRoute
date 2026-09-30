# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""W0 coder gate §4.1 step 6 — compile-only `-fstack-usage` frames for the name, coordinate and `regen` paths on the
heltec_mobile toolchain, base vs final. Compiler frames and call-chain subtotals only: NOT a task high-water mark and
NOT a whole-program stack bound. Base = `git archive 0f23aee src`; final = the working tree. Raw `.su` files and
compiler logs go under the ignored artifacts folder. ⓘ The gateway compiles the same two TUs; its frames are recorded
too, for the board attribution (a headless build still links `cfg set` and `regen`)."""
from pathlib import Path
import json, importlib.util, sys, subprocess, tempfile, shutil, re
sys.dont_write_bytecode = True
R = Path('/home/staszek/MeshRoute'); E = Path(__file__).resolve().parent
A = R / 'artifacts' / '2026-09-29-standalone-mobile-home-w0' / 'stack'; A.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location('w0abi', R / 'tools/probe_board_abi.py')
abi = importlib.util.module_from_spec(spec); sys.modules[spec.name] = abi; spec.loader.exec_module(abi)
D = Path(tempfile.mkdtemp(prefix='w0-stack-'))
base = D / 'base'
subprocess.run(f'git -C {R} archive 0f23aee src | tar -x -C {D} && mv {D}/src {base}', shell=True, check=True)
roots = {'base': base, 'final': R / 'src'}
FUNCS = ['handle_cfg_set', 'rename_node', 'id_candidate_from_live', 'do_regen', 'dispatch', 'exec_console_line',
         'print_identity', 'mesh_service_once']
out = {'scope': __doc__, 'scratch': str(D), 'compiles': [], 'frames': {}}
for variant, source in roots.items():
    for target in ['heltec_mobile', 'gateway']:
        data = abi.idedata(target)
        for unit in ['firmware_config.cpp', 'firmware_commands.cpp', 'fw_main.cpp']:
            name = f'{variant}-{target}-{unit}'; obj = D / (name + '.o')
            cmd = [x for x in abi.compile_command(data, source / unit, obj) if x != '-flto'] + ['-fstack-usage', '-fno-lto']
            p = subprocess.run(cmd, cwd=R, capture_output=True)
            (A / (name + '.log')).write_bytes(p.stdout + p.stderr)
            su = obj.with_suffix('.su'); frames = {}
            if su.exists():
                shutil.copyfile(su, A / su.name)
                for line in su.read_text().splitlines():
                    m = re.match(r'^[^:]+:\d+:\d+:(.*)\t(\d+)\t(\S+)$', line)
                    if not m: continue
                    fn = m.group(1)
                    if '<lambda' in fn: continue
                    for f in FUNCS:
                        if re.search(r'(^|[ :])' + re.escape(f) + r'(\.[a-z]+\.\d+)*\(', fn):   # incl. .part/.isra/.constprop clones
                            frames[fn] = int(m.group(2))
            out['compiles'].append({'variant': variant, 'target': target, 'unit': unit, 'exit': p.returncode,
                                    'su': (A / su.name).name if su.exists() else None})
            out['frames'][name] = frames
            print(name, p.returncode, len(frames), flush=True)
(E / 'stack-measure.json').write_text(json.dumps(out, indent=2) + '\n')
