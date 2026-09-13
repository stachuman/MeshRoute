import ast
import hashlib
import json
import pathlib
import subprocess
import time

gate = pathlib.Path(__file__).resolve().parent
root = gate / 'mutations'
out = gate / 'union'
out.mkdir()
values = {}
tables = {}
for node in ast.parse((root / 'tools/probe_ui_model_mutations.py').read_text()).body:
    if isinstance(node, ast.Assign):
        for target in node.targets:
            if isinstance(target, ast.Name):
                try:
                    values[target.id] = ast.literal_eval(node.value)
                except (ValueError, TypeError):
                    if target.id == 'MUTS_BY_TARGET':
                        tables = {ast.literal_eval(k): v.id for k, v in zip(node.value.keys, node.value.values)}
changed = set((gate / 'changed-from-base.txt').read_text().splitlines())
a = {t for t, p in values['TARGET_SRC'].items() if p in changed}
# Independently selected acceptance surfaces: actual call dependencies and retained historical obligations.
groups = {
    'retained 7b-1 source acceptance': 'a0rx b159map b159rx b161rx b251rx radmin3verbs radmin5rx radmin5session radmin7exec radmin7rx radmin7transcript sliceBnode sliceBrx sliceEnode sliceGrx teamgrant',
    'authenticated codec and command seam': 'radmin2codec consoleline cmdauthority',
    'acting ACL slot, identity invalidation, boot/runtime and persistence': 'radmin3acl radmin3id radmin5runtime devicenv teamkeyring',
    'by-hash transport, parked/queued admission and receive/return-path history': 'grantpark grantadmit b161hash b251hash b161mac b159mac b20mac b20codec sliceBmac',
    'inbox retained/read/delete/clear diagnostics and output': 'sliceAinbox sliceGinbox sliceCinbox sliceCpull sliceDclear sliceDstore sliceDtoken b134inbox b134store b134ram sliceAjson sliceDack sliceGjson b134ack',
}
b = set(' '.join(groups.values()).split())
union = a | b
counts = {t: len(values[tables[t]]) for t in sorted(union)}
selection = {'base': '564f460a3b755a104f146da70457e5c8c68e99b9', 'changed_source': sorted(a),
             'dependency_groups': groups, 'dependency_historical': sorted(b),
             'intersection': sorted(a & b), 'union': sorted(union), 'counts': counts}
(out / 'selectors.json').write_text(json.dumps(selection, indent=2) + '\n')
for t in sorted(union):
    source = (root / values['TARGET_SRC'][t]).read_text()
    for label, pattern, replacement in values[tables[t]]:
        assert source.count(pattern) == 1, (t, label, source.count(pattern))
paths = sorted(set(values['TARGET_SRC'].values()))
def hashes():
    return {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in paths}
before = hashes()
(out / 'targets-before.json').write_text(json.dumps(before, indent=2) + '\n')
print('QA SELECTORS', len(a), len(b), len(union), 'patterns', sum(counts.values()), flush=True)
results = {}
assert len(b) == 47
assert a-b == {'radmin72rx','radmin72session'}, a-b
assert len(a)==17 and len(union)==49
order = ['radmin72session','radmin72rx','radmin7exec','radmin2codec']
order += sorted(union - set(order))
for t in order:
    start = time.time()
    print('START', t, counts[t], flush=True)
    with (out / (t + '.log')).open('w') as log:
        rc = subprocess.run(['python3', 'tools/probe_ui_model_mutations.py', '--workers=4', '--target=' + t],
                            cwd=root, stdout=log, stderr=subprocess.STDOUT).returncode
    results[t] = {'rc': rc, 'seconds': round(time.time() - start, 2)}
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    print('END', t, results[t], flush=True)
    print('\n'.join((out / (t + '.log')).read_text(errors='replace').splitlines()[-7:]), flush=True)
    assert hashes() == before, 'source mutation not restored'
    assert rc == 0 or (t == 'sliceBmac' and rc == 1), ('Unexpected mutation exit', t, rc)
print('QA UNION COMPLETE; inspect every outcome, including known B342', flush=True)
