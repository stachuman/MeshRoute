# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# B459 §4.1 increment-1 step 4 — on SCRATCH COPIES of the firmware-UI runner (the B456 regression's own layout:
# <d>/tools/probe_firmware_ui/run.sh), with the synthetic stream the bypass regression uses (one control missing):
#   A. stock text + the library LINKED to the real one  -> exit 1: the REAL comparator resolved and refused the omission
#   B. ACCOUNTING_STATEMENT removed + the same link      -> exit 0: the real final call is what refused it
#   C. stock text, NO library                            -> exit 2 + its own FATAL message: it fails loudly, and it
#                                                           could never pass the bypass regression (which needs exit 0)
#   D. statement removed, NO library                     -> exit 2 as well: a missing library cannot fake the bypass
# plus the resolution itself: the link's target and an xtrace line showing the copy sourcing it.
# Usage: python3 inc1_copies.py <out.json>
import json, os, subprocess, sys, tempfile
from pathlib import Path
R = Path('/home/staszek/MeshRoute'); RUNNER = R / 'tools/probe_firmware_ui/run.sh'; LIB = R / 'tools/probe_accounting.sh'
STATEMENT = 'account_controls "$1" "$2" "$CTL_VERDICTS" "$CTL_GUARDS" "$3" || rc=1'
LABELS = [("S0  a synthetic build control", "no"), ("S1 the first synthetic control", "yes"),
          ("S2 the middle synthetic control", "yes"), ("S3 a synthetic \"quoted\" control", "yes"),
          ("S4 the last synthetic control", "yes")]
src = RUNNER.read_text(encoding='utf-8')
assert src.count(STATEMENT) == 1
def run(case, text, link):
    with tempfile.TemporaryDirectory(prefix='b459-inc1-') as d:
        dd = Path(d); cd = dd / 'tools' / 'probe_firmware_ui'; cd.mkdir(parents=True)
        (cd / 'run.sh').write_text(text, encoding='utf-8')
        if link: (dd / 'tools' / 'probe_accounting.sh').symlink_to(LIB)
        fx = dd / 'fx'; fx.mkdir()
        (fx / 'expected.tsv').write_text(''.join(f'{l}\t{m}\n' for l, m in LABELS))
        (fx / 'calls').write_text(f'{len(LABELS)}\n')
        ev = [('verdict', l, 'build_fail_ok' if m == 'no' else 'red') for l, m in LABELS]; del ev[2]
        (fx / 'events').write_text(''.join('\t'.join(e) + '\n' for e in ev))
        env = dict(os.environ, PS4='+${FUNCNAME[0]:-main}@${LINENO}|')
        p = subprocess.run(['bash', '-x', str(cd / 'run.sh'), '--selftest-accounting', str(fx)], capture_output=True, text=True, env=env)
        sourced = [l for l in p.stderr.splitlines() if l.startswith('+main@') and '|. ' in l]
        target = os.path.realpath(dd / 'tools' / 'probe_accounting.sh') if link else None
        out = [l for l in p.stdout.splitlines()]
        return {'case': case, 'exit': p.returncode, 'stdout_tail': out[-3:], 'sourced_trace': [s.replace(d, '<d>') for s in sourced],
                'link_resolves_to': target}
res = [run('A stock + real library linked', src, True),
       run('B statement removed + real library linked', src.replace(STATEMENT, ': # [[B456]] bypassed by the regression'), True),
       run('C stock, no library', src, False),
       run('D statement removed, no library', src.replace(STATEMENT, ': # [[B456]] bypassed by the regression'), False)]
ok = (res[0]['exit'] == 1 and res[1]['exit'] == 0 and res[2]['exit'] == 2 and res[3]['exit'] == 2
      and res[0]['link_resolves_to'] == str(LIB) and res[1]['link_resolves_to'] == str(LIB)
      and any('FATAL: the shared accounting library is missing' in l for l in res[2]['stdout_tail'])
      and any('FATAL: the shared accounting library is missing' in l for l in res[3]['stdout_tail']))
json.dump({'cases': res, 'verdict': 'PASS' if ok else 'FAIL'}, open(sys.argv[1], 'w'), indent=1)
for r in res: print(f"{r['case']}: exit {r['exit']}; link -> {r['link_resolves_to']}; sourced: {r['sourced_trace'][:1]}; tail: {r['stdout_tail'][-1:]}")
print('INC1 COPIES', 'PASS' if ok else 'FAIL')
