"""W6 coder gate §4.1 step 6 — compile-only `-fstack-usage` frames for the console and boot paths, base vs final.
Compiler frames and call-chain subtotals only: NOT a task high-water mark or a whole-program stack bound (the
pre-check's caveat, restated). The base source is `git archive 70ff486` (the frozen base); the final source is the
working tree. Raw `.su` files and compiler logs go under the ignored artifacts folder."""
from pathlib import Path
import json, importlib.util, sys, subprocess, tempfile, shutil, re
R = Path('/home/staszek/MeshRoute'); E = Path(__file__).resolve().parent
A = R / 'artifacts' / '2026-09-29-standalone-mobile-home-w6' / 'stack'; A.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location('w6abi', R / 'tools/probe_board_abi.py')
abi = importlib.util.module_from_spec(spec); sys.modules[spec.name] = abi; spec.loader.exec_module(abi)
D = Path(tempfile.mkdtemp(prefix='w6-stack-'))
base = D / 'base'; subprocess.run(f'git -C {R} archive 70ff486 src | tar -x -C {D} && mv {D}/src {base}', shell=True, check=True)
roots = {'base': base, 'final': R / 'src'}
FUNCS = ['mesh_service_once', 'exec_console_line', 'dispatch', 'handle_ui', 'preset_verb', 'preset_emit_list',
         'preset_emit_page', 'preset_emit_record', 'preset_emit_err', 'preset_render', 'preset_boot_restore',
         'preset_boot_restore_console', 'setup', 'loop', 'mr_ui_tick', 'ui_perform_send', 'ui_service_review',
         'ui_service_inbox_request', 'draw_frame', 'draw_compose', 'draw_review', 'build_snapshot']
out = {'scope': __doc__, 'scratch': str(D), 'compiles': [], 'frames': {}}
for variant, source in roots.items():
    for target in ['heltec_mobile', 'gateway']:
        data = abi.idedata(target)
        for unit in ['firmware_commands.cpp', 'fw_main.cpp', 'firmware_ui.cpp']:
            if target == 'gateway' and unit == 'firmware_ui.cpp': continue
            name = f'{variant}-{target}-{unit}'; obj = D / (name + '.o')
            cmd = [x for x in abi.compile_command(data, source / unit, obj) if x != '-flto'] + ['-fstack-usage', '-fno-lto']
            if target == 'gateway': cmd += ['-UMR_FEAT_OLED', '-DMR_FEAT_OLED=1']   # the pre-check's dormant-arm pricing
            p = subprocess.run(cmd, cwd=R, capture_output=True)
            (A / (name + '.log')).write_bytes(p.stdout + p.stderr)
            su = obj.with_suffix('.su'); frames = {}
            if su.exists():
                shutil.copyfile(su, A / su.name)
                for line in su.read_text().splitlines():
                    m = re.match(r'^[^:]+:\d+:\d+:(.*)\t(\d+)\t(\S+)$', line)
                    if not m: continue
                    fn = m.group(1)
                    for f in FUNCS:
                        if re.search(r'(^|[ :])' + re.escape(f) + r'\(', fn):
                            frames[fn] = int(m.group(2))
            out['compiles'].append({'variant': variant, 'target': target, 'unit': unit, 'exit': p.returncode,
                                    'forced_oled': target == 'gateway', 'su': (A / su.name).name if su.exists() else None})
            out['frames'][name] = frames
            print(name, p.returncode, len(frames), flush=True)
(E / 'stack-measure.json').write_text(json.dumps(out, indent=2) + '\n')
