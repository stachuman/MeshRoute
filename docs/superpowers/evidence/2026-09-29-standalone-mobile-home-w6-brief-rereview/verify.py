"""W6 revision-2 scoped brief review; read inputs, write only this evidence folder."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
SIM = Path('/home/staszek/lora-universal-simulator')
BRIEF = 'docs/superpowers/plans/2026-09-29-standalone-mobile-home-w6-phrases.md'
EXPECTED = 'cdcef1ee1826c14a8e941e05ef4b4bfdb8e7004926cfd22f7e2a44025716828e'
PREFIX = str(HERE.relative_to(ROOT))
PRECHECK = ROOT / 'docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w6-precheck'
REVIEW = ROOT / 'docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w6-brief-review'

def sha(path):
    return hashlib.sha256(os.readlink(path).encode() if path.is_symlink() else path.read_bytes()).hexdigest()

def git(root, *args):
    return subprocess.check_output(['git', *args], cwd=root).decode()

def inventory(root, exclude=''):
    paths = git(root, 'ls-files', '-z', '--cached', '--others', '--exclude-standard').split('\0')
    return {p: sha(root / p) for p in sorted(set(paths))
            if p and not (exclude and p.startswith(exclude))
            and ((root / p).is_file() or (root / p).is_symlink())}

def changed(old, now):
    return [p for p, h in old.items() if now.get(p) != h]

text = (ROOT / BRIEF).read_text()
assert sha(ROOT / BRIEF) == EXPECTED
assert (ROOT / BRIEF).read_bytes().count(b'\n') == 692
assert git(ROOT, 'rev-parse', 'HEAD').strip() == '70ff486b40c9b07001b33bcbcdf640ad494c1149'
assert git(SIM, 'rev-parse', 'HEAD').strip() == '6585649ea5a780f0542b2931853a667be56a5b2b'
assert not git(SIM, 'status', '--porcelain')
assert not git(ROOT, 'diff', '--cached', '--name-only')

pins = []
for line in text.splitlines():
    if not line.startswith('| `'):
        continue
    columns = line.split('|')
    paths = re.findall(r'`([^`]+)`', columns[1])
    hashes = re.findall(r'`([0-9a-f]{64})`', line)
    for path, expected in zip(paths, hashes):
        actual = sha(ROOT / path)
        row = dict(path=path, expected=expected, actual=actual)
        assert actual == expected, row
        if len(columns) == 5 and re.fullmatch(r'\s*\d+\s*', columns[3]):
            row['lines'] = (ROOT / path).read_bytes().count(b'\n')
            assert row['lines'] == int(columns[3]), row
        pins.append(row)
assert len(pins) == 32, len(pins)
assert sum('lines' in row for row in pins) == 26

precheck = json.loads((PRECHECK / 'inputs.json').read_text())
old = {p: v['sha256'] for p, v in precheck['meshroute']['files'].items()}
files = inventory(ROOT, PREFIX)
preparation = {'MEMORY.md', 'tracker.md', 'docs/2026-07-30-open-bug-register.md',
               'docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md'}
from_precheck = changed(old, files)
assert set(from_precheck) == preparation, from_precheck
review = json.loads((REVIEW / 'inputs.json').read_text())
from_review = changed(review['files'], files)
assert set(from_review) == {BRIEF, 'docs/2026-07-30-open-bug-register.md',
                           'docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md'}, from_review
additions = sorted(set(files) - set(old))
allowed = [str(PRECHECK.relative_to(ROOT)), str(REVIEW.relative_to(ROOT)), BRIEF]
assert all(any(p == a or p == a + '.md' or p.startswith(a + '/') for a in allowed) for p in additions), additions

readonly = []
for path, prefix, suffix, lines in [
    ('tools/probe_deferred_actions/run.py', '694387bd', 'b55f', 190),
    ('tools/probe_deferred_actions/probe.cpp', '57364be0', 'e587', 222),
]:
    h = sha(ROOT / path)
    assert h == old[path] and h.startswith(prefix) and h.endswith(suffix)
    assert (ROOT / path).read_bytes().count(b'\n') == lines
    readonly.append(dict(path=path, sha256=h, lines=lines, authority='precheck inputs.json'))

checksums = {}
for name, directory, count in [('precheck', PRECHECK, 45), ('first-review', REVIEW, 14)]:
    run = subprocess.run(['sha256sum', '-c', 'SHA256SUMS'], cwd=directory, capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
    assert len(run.stdout.splitlines()) == count
    (HERE / (name + '-checksums.txt')).write_text(run.stdout)
    checksums[name] = dict(entries=count, sha256=sha(directory / 'SHA256SUMS'))

links = []
for target in re.findall(r'\]\(([^)]+)\)', text):
    if '://' in target or target.startswith('#'):
        continue
    target = target.split('#')[0]
    path = ((ROOT / BRIEF).parent / target).resolve()
    assert path.is_file(), target
    links.append(target)

snapshot = dict(head=git(ROOT, 'rev-parse', 'HEAD').strip(), brief=EXPECTED,
                files=files, simulator=dict(head=git(SIM, 'rev-parse', 'HEAD').strip(), files=inventory(SIM)))
(HERE / 'inputs.json').write_text(json.dumps(snapshot, indent=2) + '\n')
result = dict(brief_lines=692, table_pins=pins, read_only_pins=readonly,
              changed_from_precheck=from_precheck, changed_from_first_review_entry=from_review,
              additions_since_precheck=additions, prior_checksums=checksums,
              links=links, status=git(ROOT, 'status', '--short'), simulator_status=git(SIM, 'status', '--short'),
              counts=dict(meshroute=len(files), simulator=len(snapshot['simulator']['files'])))
(HERE / 'verification.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(dict(table_pins=len(pins), line_counts=26, read_only_pins=len(readonly),
                      checksums=checksums, counts=result['counts'], changes=from_review,
                      links=len(links)), indent=2))
