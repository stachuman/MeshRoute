#!/usr/bin/env python3
"""Reproduce all eight canonical low-order refusals against a built native tree.
Usage: python3 run-low-order-proof.py /path/to/MeshRoute
Only private compiler outputs and the private false-success source are written.
"""
from pathlib import Path
import hashlib, json, re, subprocess, sys, tempfile
here = Path(__file__).resolve().parent
root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else here.parents[3]
source = root / 'lib/core/remote_client.cpp'
before = hashlib.sha256(source.read_bytes()).hexdigest()
points = json.loads(subprocess.check_output([sys.executable, str(here / 'low-order-reference.py')]))['points']
assert set(points) == set(re.findall(r'"([0-9a-f]{64})"', (here / 'low-order-proof.cpp').read_text()))
libraries = sorted((root / '.pio/build/native').glob('lib*/lib*.a'))
assert any(p.name == 'libcore.a' for p in libraries), 'run pio test -e native first'
with tempfile.TemporaryDirectory(prefix='mr-s8ac-low-order-proof-') as directory:
    out = Path(directory)
    original = source.read_text()
    needle = 'crypto_wipe(peer, sizeof peer); crypto_wipe(shared, sizeof shared); return st;'
    assert original.count(needle) == 1
    mutant = out / 'remote_client.cpp'
    mutant.write_text(original.replace(needle, needle.replace('return st;', 'return RemoteStatus::ok;')))
    common = ['g++', '-std=gnu++20', '-DMESHROUTE_NATIVE=1', '-DMR_N_LAYERS=2', '-ffunction-sections', '-fdata-sections']
    for directory in ['lib/core', 'lib/hal', 'lib/console', 'lib/monocypher/src', 'src']:
        common += ['-I' + str(root / directory)]
    for name, candidate, expected in [('positive', source, 0), ('false-success-control', mutant, 1)]:
        exe = out / name
        subprocess.run(common + [str(here / 'low-order-proof.cpp'), str(candidate), '-Wl,--gc-sections', '-Wl,--start-group'] + [str(p) for p in libraries] + ['-Wl,--end-group', '-o', str(exe)], check=True)
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        print(name + ': ' + result.stdout.strip())
        assert result.returncode == expected, result.stderr
        assert ('264 checks / 0 failed' if expected == 0 else '264 checks / 8 failed') in result.stdout
assert hashlib.sha256(source.read_bytes()).hexdigest() == before
print('PASS: eight independently enumerated canonical peers; 264 checks; one compiled control RED; source unchanged')
