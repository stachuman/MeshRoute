"""B459 pre-check measurements only. Stock run plus fault copies; never edits tool inputs."""
from pathlib import Path
import subprocess,tempfile,shutil,os,json,re,hashlib,ast
ROOT=Path('/home/staszek/MeshRoute'); E=Path(__file__).resolve().parent; RAW=ROOT/'artifacts/2026-09-28-b459-precheck'
runner=ROOT/'tools/probe_board_ui/run.sh'; source=runner.read_text(); results=[]
env=dict(os.environ,PS4='+${FUNCNAME[0]:-main}@${LINENO}|',BASH_XTRACEFD='9')
if not (E/'board-reconciliation.json').exists():
    with (RAW/'board-traced.log').open('w') as log:
        p=subprocess.run(['bash','-c','exec 9>"$1"; bash -x "$2"','b459-trace',str(RAW/'board.trace'),str(runner)],env=env,stdout=log,stderr=subprocess.STDOUT)
    assert p.returncode==0
    helper=ROOT/'docs/superpowers/evidence/2026-09-28-standalone-mobile-home-w2/reconcile.py'
    subprocess.run(['python3','-B',str(helper),str(runner),str(runner.parent/'negctl.py'),str(RAW/'board.trace'),str(RAW/'board-traced.log'),str(E/'board-reconciliation.json'),str(E/'board-reconciliation.tsv')],check=True)
    assert json.loads((E/'board-reconciliation.json').read_text())['summary']['verdict']=='PASS'
needle='wchk "W1 mr_ui_on_push';assert source.count(needle)==1
for name,kind in [('false-guard','false'),('undefined-guard','b459_missing_guard'),('early-return','return'),('shrunk-negctl','negctl'),('removed-canvas-check','canvas')]:
    with tempfile.TemporaryDirectory(prefix='b459-measure-') as td:
        d=Path(td)/'probe_board_ui';shutil.copytree(runner.parent,d)
        text=source.replace('ROOT=$(cd ../.. && pwd)',f'ROOT={ROOT}')
        assert text!=source
        if kind in ('false','b459_missing_guard'): text=text.replace(needle,kind+' && \\\n'+needle)
        elif kind=='return':
            n='  local file=$1 label=$2 pred=$3; shift 3';assert text.count(n)==1
            text=text.replace(n,n+'\n  [[ "$label" != W1\\ * ]] || return 0')
        elif kind=='negctl':
            p=d/'negctl.py';s=p.read_text();n='for idx, (label, find, repl) in enumerate(MUT):';assert s.count(n)==1
            p.write_text(s.replace(n,'for idx, (label, find, repl) in enumerate(MUT[1:]):'))
        elif kind=='canvas':
            p=d/'probe_main.cpp';s=p.read_text();n='    CHK("P1b ... and its argument is 1 (DISPLAYOFF)",    g_u8.last_power_save_arg == 1);';assert s.count(n)==1
            p.write_text(s.replace(n,'    // B459 synthetic omitted assertion, no product change.'))
        (d/'run.sh').write_text(text)
        cmd=['bash',str(d/'run.sh')]+([] if kind=='negctl' else ['--no-neg'])
        with (RAW/(name+'.log')).open('w') as log:p=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT)
        out=(RAW/(name+'.log')).read_text()
        row={'case':name,'exit':p.returncode,'mode':'default' if kind=='negctl' else '--no-neg','summary':[l for l in out.splitlines() if re.search(r'passed /|controls run|command not found',l)],'note':'Synthetic omission in a temporary copy; product files read from frozen main checkout.'}
        if kind=='negctl':
            tree=ast.parse((runner.parent/'negctl.py').read_text());counts={}
            for node in tree.body:
                if isinstance(node,ast.Assign) and getattr(node.targets[0],'id','') in ('MUT_V3','MUT_V4'):
                    labels=[ast.literal_eval(el.elts[0]) for el in node.value.elts]
                    counts[node.targets[0].id]={'declared':len(labels),'observed':sum(out.splitlines().count(l) for l in labels)}
            row['executed_negctl_by_arm']=counts
        results.append(row);print(row,flush=True)
        assert p.returncode==0,row
(E/'board-fault-measurements.json').write_text(json.dumps(results,indent=2)+'\n')
