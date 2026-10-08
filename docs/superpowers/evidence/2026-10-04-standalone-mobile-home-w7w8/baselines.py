#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""W7+W8 §4.1 baselines/final figures: parse the raw run logs into one JSON record (no command is re-run here).

usage: python3 -B baselines.py <prefix: base|final> <out.json>
Reads artifacts/2026-10-04-standalone-mobile-home-w7w8/<prefix>-*.log and <prefix>-runs.tsv; the board manifests are
read from .pio-measure/w7w8/<run>/<env>/manifest.json.
"""
import hashlib, json, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
A = ROOT / 'artifacts/2026-10-04-standalone-mobile-home-w7w8'


def text(name):
    return (A / name).read_text(errors='replace')


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def one(rx, s, what):
    m = re.findall(rx, s, re.M)
    if len(m) != 1:
        raise SystemExit(f'{what}: expected exactly one match of {rx!r}, got {len(m)}')
    return m[0]


def main():
    prefix, out = sys.argv[1], Path(sys.argv[2])
    runs = [l.split('\t') for l in text(f'{prefix}-runs.tsv').splitlines() if l.strip()]
    rec = {'prefix': prefix, 'runs': [], 'done': runs[-1] == ['DONE']}
    for r in runs:
        if r == ['DONE']:
            continue
        name, rc, secs = r
        log = A / f'{prefix}-{name}.log'
        rec['runs'].append({'step': name, 'rc': int(rc), 'seconds': int(secs),
                            'log_sha256': sha(log) if log.exists() else None})
    nb = text(f'{prefix}-native-binary.log')
    c = one(r'^\[doctest\] test cases:\s+(\d+) \|\s+(\d+) passed \|\s+(\d+) failed \|\s+(\d+) skipped', nb, 'native cases')
    a = one(r'^\[doctest\] assertions:\s+(\d+) \|\s+(\d+) passed \|\s+(\d+) failed', nb, 'native assertions')
    rec['native'] = {'cases': int(c[0]), 'cases_passed': int(c[1]), 'cases_failed': int(c[2]),
                     'skipped': int(c[3]), 'assertions': int(a[0]), 'assertions_passed': int(a[1]),
                     'assertions_failed': int(a[2])}
    co = text(f'{prefix}-corpus.log')
    rec['corpus'] = {'verdict': one(r'^(PASS: 36/36 streams produced and validated, 0 failures)$', co, 'corpus'),
                     'anchors': one(r'^\s+(anchors: 36/36 rows reproduce simulation/BASELINE\.md)$', co, 'anchors'),
                     'manifest': one(r'^\s+manifest=(\S+)$', co, 'corpus manifest')}
    man = Path(rec['corpus']['manifest'])
    if man.exists():
        rec['corpus']['manifest_sha256'] = sha(man)
    boards = {}
    for run in ('1', '2'):
        for env in ('gateway', 'heltec_mobile'):
            p = ROOT / f'.pio-measure/w7w8/{prefix}-{run}/{env}/manifest.json'
            j = json.loads(p.read_text())
            boards[f'{prefix}-{run}/{env}'] = {'manifest_sha256': sha(p), 'path': str(p.relative_to(ROOT))}
    bl = text(f'{prefix}-board-{prefix}-1.log')
    for env in ('gateway', 'heltec_mobile'):
        blk = re.search(r'PASS: deterministic board measurement env=' + env + r'\n\s+RAM=(\d+) flash=(\d+) objects=(\d+)', bl)
        boards[f'{prefix}-1/{env}'].update({'ram': int(blk.group(1)), 'flash': int(blk.group(2)),
                                             'objects': int(blk.group(3))})
    for env in ('gateway', 'heltec_mobile'):
        cl = text(f'{prefix}-compare-{env}.log')
        boards[f'compare/{env}'] = one(r'^(PASS: exact repeatability env=' + env + r')$', cl, f'compare {env}')
    rec['boards'] = boards
    rec['warning_census'] = one(r'^(PASS — 6 OLED env\(s\) match their pinned warning baseline)$',
                                text(f'{prefix}-warning-census.log'), 'warning census')
    rec['abi'] = one(r'^(PASS: board ABI \(\d+ checks, \d+/\d+ controls RED, 0 unusable\))$',
                     text(f'{prefix}-abi.log'), 'abi')
    fw = text(f'{prefix}-firmware-ui.log')
    rec['firmware_ui'] = {'controls': one(r'^(controls: \d+ verified / 0 unusable)$', fw, 'fw controls'),
                          'accounted': one(r'^(controls accounted: .*)$', fw, 'fw accounting'),
                          'passed_lines': re.findall(r'^\s*(\d+ passed / \d+ failed / \d+ total)$', fw, re.M),
                          'final': fw.strip().splitlines()[-1]}
    bu = text(f'{prefix}-board-ui.log')
    rec['board_ui'] = {'final': bu.strip().splitlines()[-1],
                       'lines': [l for l in bu.splitlines() if re.match(r'^\s*(wiring|negctl|identities|canvas|traits|structural|declarations|PASS)', l)]}
    di = text(f'{prefix}-discovery.log')
    rec['discovery'] = {'ran': one(r'^(Ran \d+ tests in [0-9.]+s)$', di, 'discovery ran'),
                        'verdict': one(r'^(OK|FAILED.*)$', di, 'discovery verdict')}
    rec['inventory'] = text(f'{prefix}-inventory.log').strip().splitlines()[-1]
    out.write_text(json.dumps(rec, indent=2, sort_keys=True) + '\n')
    print(json.dumps({k: rec[k] for k in ('native', 'abi', 'warning_census', 'inventory')}, indent=1))


if __name__ == '__main__':
    main()
