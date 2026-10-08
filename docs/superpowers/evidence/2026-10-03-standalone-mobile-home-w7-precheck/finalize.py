"""Seal evidence and prove the pre-check preserved every entry input.

Run only in an unsealed scratch copy when replaying; outputs are evidence,
never production edits. The final source inventory deliberately excludes only
this pre-check's own added paths, not other uncommitted or untracked work.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess

ROOT = Path('/home/staszek/MeshRoute')
SIM = Path('/home/staszek/lora-universal-simulator')
OUT = Path(__file__).resolve().parent
REPORT = OUT.parent / (OUT.name + '.md')
PREFIX = 'docs/superpowers/evidence/' + OUT.name


def command(args, cwd):
    return subprocess.check_output(args, cwd=cwd).decode().strip()


def fingerprint(path):
    if path.is_symlink():
        return {'symlink': os.readlink(path)}
    return {'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'bytes': path.stat().st_size}


def inventory(root):
    names = subprocess.check_output(
        ['git', 'ls-files', '-c', '-o', '--exclude-standard', '-z'], cwd=root
    ).decode().split('\0')
    return {name: fingerprint(root / name) for name in sorted(set(names) - {''})}


entry = json.loads((OUT / 'inputs.json').read_text())
now = inventory(ROOT)
changed = [p for p, value in entry['files'].items() if now.get(p) != value]
added = sorted(now.keys() - entry['files'].keys())
assert not changed, changed
assert all(p == PREFIX + '.md' or p.startswith(PREFIX + '/') for p in added), added
assert command(['git', 'rev-parse', 'HEAD'], ROOT) == entry['head']
assert not command(['git', 'diff', '--cached', '--name-only'], ROOT)
assert inventory(SIM) == entry['simulator']['files']
assert command(['git', 'rev-parse', 'HEAD'], SIM) == entry['simulator']['head']
assert not command(['git', 'status', '--porcelain=v1'], SIM)
subprocess.run(['git', 'diff', '--check'], cwd=ROOT, check=True)
subprocess.run(['git', 'diff', '--check'], cwd=SIM, check=True)

# The panel-font inputs are ignored build dependencies, so verify their separate
# recorded hashes as well as the nonignored source inventory.
font = json.loads((OUT / 'font-measure.json').read_text())
assert all(hashlib.sha256((ROOT / p).read_bytes()).hexdigest() == sha
           for p, sha in font['font_inputs'].items())

result = {'verdict': 'PASS', 'head': entry['head'],
          'simulator_head': entry['simulator']['head'],
          'entry_inputs': len(entry['files']), 'changed_or_missing': [],
          'added_paths': added, 'staged': [], 'simulator_clean': True,
          'font_input_hashes_unchanged': True, 'whitespace_check': 'PASS'}
(OUT / 'preservation-final.json').write_text(json.dumps(result, indent=2) + '\n')

files = sorted(p for p in OUT.rglob('*') if p.is_file() and p.name != 'SHA256SUMS')
seal = ''.join(hashlib.sha256(p.read_bytes()).hexdigest() + '  '
               + p.relative_to(OUT).as_posix() + '\n' for p in files)
seal += hashlib.sha256(REPORT.read_bytes()).hexdigest() + '  ../' + REPORT.name + '\n'
(OUT / 'SHA256SUMS').write_text(seal)
# Check every local evidence link, including the newly created seal, before
# confirming the result. A missing link is an error, never a sealed PASS.
for target in re.findall(r'\[[^\]]*\]\(([^)]+)\)', REPORT.read_text()):
    if '://' in target:
        continue
    target = target.split('#', 1)[0]
    assert (REPORT.parent / target).exists(), target
checked = subprocess.run(['sha256sum', '-c', 'SHA256SUMS'], cwd=OUT,
                         capture_output=True, text=True, check=True)
assert len(checked.stdout.splitlines()) == len(files) + 1
print(json.dumps({'preservation': 'PASS', 'entry_inputs': len(entry['files']),
                  'simulator_inputs': len(entry['simulator']['files']),
                  'new_evidence_paths_before_seal': len(added),
                  'seal_entries': len(files) + 1,
                  'seal_sha256': hashlib.sha256((OUT / 'SHA256SUMS').read_bytes()).hexdigest(),
                  'report_sha256': hashlib.sha256(REPORT.read_bytes()).hexdigest()}, indent=2))
