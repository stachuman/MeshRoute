from pathlib import Path
import ast
import hashlib
import json
import os
import re
import subprocess

R = Path('/home/staszek/MeshRoute')
S = Path('/home/staszek/lora-universal-simulator')
E = Path(__file__).resolve().parent
report = E.parent / '2026-09-28-b459-brief-rereview.md'
prefix = 'docs/superpowers/evidence/2026-09-28-b459-brief-rereview'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


inventory = json.loads((E / 'inputs.json').read_text())
preservation = {}
for key, root in [('meshroute', R), ('simulator', S)]:
    changed = []
    for rel, old in inventory[key]['files'].items():
        path = root / rel
        if path.is_symlink():
            target = os.readlink(path)
            value = {'type': 'symlink', 'sha256': hashlib.sha256(target.encode()).hexdigest(), 'target': target}
        elif path.is_file():
            value = {'type': 'file', 'sha256': sha(path)}
        else:
            value = {'type': 'missing'}
        if value != old:
            changed.append(rel)
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    status = subprocess.check_output(['git', 'status', '--short'], cwd=root, text=True)
    staged = subprocess.check_output(['git', 'diff', '--cached', '--name-only'], cwd=root, text=True)
    assert not changed and head == inventory[key]['head'] and not staged, (key, changed)
    now = set(subprocess.check_output(['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=root).decode().split('\0')) - {''}
    added = sorted(now - set(inventory[key]['files']))
    if key == 'meshroute':
        assert all(path.startswith(prefix) for path in added), added
    else:
        assert not status and not added
    whitespace = subprocess.run(['git', 'diff', '--check'], cwd=root, capture_output=True)
    assert whitespace.returncode == 0, whitespace.stdout.decode()
    preservation[key] = {
        'head': head,
        'entry_paths_preserved': len(inventory[key]['files']),
        'changed': changed,
        'added_review_outputs': added,
        'staged': staged,
        'status': status,
        'whitespace': 'PASS',
    }

# The manifest is populated below; provision it before checking the report's links.
(E / 'SHA256SUMS').touch(exist_ok=True)
(E / 'final-preservation.json').write_text(json.dumps(preservation, indent=2) + '\n')
links = []
for target in re.findall(r'\]\(([^)]+)\)', report.read_text()):
    exists = (report.parent / target.split('#')[0]).exists()
    links.append({'path': target, 'exists': exists})
    assert exists, target
for path in E.glob('*.py'):
    ast.parse(path.read_text())
(E / 'validation.json').write_text(json.dumps({'links': links, 'evidence_python_syntax': 'PASS'}, indent=2) + '\n')
for link in links:
    assert (report.parent / link['path'].split('#')[0]).exists()
files = sorted(path for path in E.rglob('*') if path.is_file() and path.name != 'SHA256SUMS') + [report]
(E / 'SHA256SUMS').write_text(''.join(f'{sha(path)}  {os.path.relpath(path, E)}\n' for path in files))
for line in (E / 'SHA256SUMS').read_text().splitlines():
    digest, rel = line.split('  ', 1)
    assert sha(E / rel) == digest, rel
print(json.dumps({
    'verdict': 'PASS',
    'report_sha256': sha(report),
    'manifest_sha256': sha(E / 'SHA256SUMS'),
    'checksum_entries': len(files),
    'all_entry_inputs_preserved': True,
}, indent=2))
