#!/usr/bin/env python3
"""Read-only Slice 9 r2 source validation; temporary files stay outside the checkout."""
import argparse
import dataclasses
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import unittest

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root', type=Path, default=Path.cwd())
parser.add_argument('--output', type=Path)
parser.add_argument('--simulator', type=Path)
parser.add_argument('--reference-python', default=sys.executable)
args = parser.parse_args()
ROOT = args.root.resolve()
OUT = args.output or Path(tempfile.mkdtemp(prefix='mr-slice9-r2-preflight-'))
OUT.mkdir(parents=True, exist_ok=True)
SIM = args.simulator or ROOT.parent / 'lora-universal-simulator'
REF = ROOT / 'docs/superpowers/evidence/2026-09-13-radmin-slice7b3-0-reference.py'
PYTHON = args.reference_python
BRIEF = 'docs/superpowers/plans/2026-09-18-radmin-slice9-legacy-deletion-and-protocol-docs.md'
records = []

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def save(name, value):
    (OUT / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')

def run(label, argv, cwd=ROOT, expected=0):
    started = time.time()
    result = subprocess.run(argv, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    log = OUT / (label + '.log')
    log.write_text(result.stdout)
    records.append(dict(label=label, argv=[str(x) for x in argv], cwd=str(cwd),
                        exit=result.returncode, expected=expected, seconds=time.time()-started,
                        log=log.name, sha256=digest(log)))
    save('commands.json', records)
    assert result.returncode == expected, (label, result.returncode, result.stdout[-2000:])
    print(label, 'exit', result.returncode, flush=True)
    return result.stdout

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module

os.chdir(ROOT)
assert run('meshroute-head', ['git', 'rev-parse', 'HEAD']).strip() == '84edd3ebfb08d807645d077269bace145ac2a2ab'
assert run('simulator-head', ['git', 'rev-parse', 'HEAD'], SIM).strip() == '6585649ea5a780f0542b2931853a667be56a5b2b'
assert not run('simulator-status', ['git', 'status', '--short'], SIM).strip()
run('initial-status', ['git', 'status', '--short'])
assert digest(ROOT / BRIEF) == 'bd538cae5e85b4bb92e2a3ae4fd118d1292f4363b090b8c327b6d22a7c4a1353'
paths = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z']).split(b'\0')
inputs = {}
for item in paths:
    if not item:
        continue
    name = os.fsdecode(item)
    p = ROOT / name
    inputs[name] = dict(symlink=os.readlink(p)) if p.is_symlink() else dict(sha256=digest(p), size=p.stat().st_size)
save('input-inventory.json', inputs)
changed = subprocess.check_output(['git', 'diff', '--name-only', '-z']).split(b'\0')
prep_names = sorted([os.fsdecode(p) for p in changed if p] + [BRIEF])
assert len(prep_names) == 6
save('preparation-inputs.json', {name: inputs[name] for name in prep_names})

g = load('slice9_inventory', ROOT / 'tools/gen_command_inventory.py')
rows, *_ = g.build_rows(str(ROOT))
removed = [r for r in rows if r.source.startswith('src/firmware_remote.cpp:') or r.verb in ('rcmd', 'password', 'lock', 'unlock')]
assert len(rows) == 208 and len(removed) == 11
save('inventory-deletion.json', dict(base_rows=len(rows), removed_rows=len(removed),
    implied_remaining_rows=len(rows)-len(removed), brief_remaining_rows=202,
    rows=[dataclasses.asdict(r) for r in removed]))
print('inventory: 208 - 11 = 197 (brief says 202)', flush=True)
run('inventory-check', ['python3', 'tools/gen_command_inventory.py', '--check'])
run('authority', ['python3', 'tools/check_command_authority.py'])
run('authority-controls', ['python3', 'tools/check_command_authority.py', '--selftest'])

sys.path.insert(0, str(ROOT / 'tools'))
t = load('slice9_console_tests', ROOT / 'tools/test_probe_console_sink.py')
cls = t.TestPrimaryProjectionOracle
names = ['test_the_gate_evaluator_agrees_with_the_real_gates', 'test_the_feature_gates_really_separate_the_profiles']
def unit_run(label):
    stream = io.StringIO()
    result = unittest.TextTestRunner(stream=stream, verbosity=2).run(unittest.TestSuite(cls(n) for n in names))
    (OUT / (label + '.log')).write_text(stream.getvalue())
    return dict(tests=result.testsRun, failures=len(result.failures), errors=len(result.errors), skips=len(result.skipped))
baseline = unit_run('omitted-console-tests-base')
assert baseline == dict(tests=2, failures=0, errors=0, skips=0)
old_build, old_profiles = t.GEN.build_rows, t.GEN.PROFILES
def projected_build(*args, **kwargs):
    base, *tail = old_build(*args, **kwargs)
    return ([r for r in base if not (r.source.startswith('src/firmware_remote.cpp:') or r.verb in ('rcmd','password','lock','unlock'))], *tail)
t.GEN.build_rows = projected_build
t.GEN.PROFILES = {name: {key: value for key, value in macros.items() if key != 'MR_FEAT_REMOTE_MGMT'} for name, macros in old_profiles.items()}
projected = unit_run('omitted-console-tests-projected-deletion')
assert projected == dict(tests=2, failures=1, errors=1, skips=0)
t.GEN.build_rows, t.GEN.PROFILES = old_build, old_profiles

cfg = (ROOT / 'src/firmware_config.cpp').read_text()
start = cfg.index('#if MR_FEAT_REMOTE_MGMT\n// `password')
end = cfg.index('#endif', start) + len('#endif')
projected_cfg = cfg[:start] + cfg[end:]
def counts(text):
    text = re.sub(r'//[^\n]*', '', text)
    return dict(notifications=text.count('mr_ui_on_config_saved()'), direct_saves=text.count('mrnv::save('))
assert counts(cfg) == dict(notifications=7, direct_saves=7)
assert counts(projected_cfg) == dict(notifications=6, direct_saves=6)
header = (ROOT / 'lib/core/mr_features.h').read_text()
assert header.count('#    error') == 3
save('omitted-instrument-users.json', dict(
    console_tests=dict(base=baseline, projected_deletion=projected),
    feature_tests=dict(file='tools/test_probe_features.py', lines=[270,277,280,281,285],
        facts=['requires at least five C controls', 'requires three board-only error diagnostics',
               'requires A3/A4 consistency refusals', 'requires REMOTE_MGMT=1 in host_lus_gateway output']),
    board_ui=dict(file='tools/probe_board_ui/run.sh', notification_pin=7,
        base=counts(cfg), projected_deletion=counts(projected_cfg),
        affected=['W19 password success/wipe sequence and its five controls', 'W20 shared writer census (also used by other nsite checks)']),
    scope='In-memory projections only; no candidate production or tool file edited.'))

source = (ROOT / 'test/test_remote_codec.cpp').read_text()
start = source.index('TEST_CASE("§radmin-2/legacy')
end = source.index('// =========================================================================================================\n// §9', start)
case_removed = source[:start] + source[end:]
assert case_removed.count('TEST_CASE(') == source.count('TEST_CASE(')-1
removed_path = OUT / 'test_remote_codec-case-removed.cpp'
removed_path.write_text(case_removed)
run('reference-base', [PYTHON, str(REF), '--freeze-check', '--compare', str(ROOT / 'test/test_remote_codec.cpp'), '--selftest'])
run('reference-case-removal-survives', [PYTHON, str(REF), '--freeze-check', '--compare', str(removed_path), '--selftest'])

capture = OUT / 'capture-legacy.cpp'
capture.write_text(r'''#include "admin_auth.h"
#include <cassert>
#include <cstdio>
#include <cstring>
int main() {
    using namespace meshroute;
    unsigned char a[32], n[32];
    for (int i=0;i<32;++i) { a[i]=0x11+i; n[i]=0x71+i; }
    Identity admin{}, node{};
    identity_from_seed(admin,a); identity_from_seed(node,n);
    const unsigned char cmd[6]={'s','t','a','t','u','s'};
    const unsigned char rand8[8]={0xde,0xad,0xbe,0xef,1,2,3,4};
    unsigned char frame[128]{}, pt[64]{};
    const auto len=admin_cmd_seal(frame,sizeof frame,admin,node.ed_pub,node.key_hash32,7,cmd,sizeof cmd,rand8,1);
    assert(len==40);
    AdminCmd decoded{};
    assert(admin_cmd_open(frame,len,admin.ed_pub,node,decoded,pt,sizeof pt));
    assert(decoded.node_key_hash==node.key_hash32);
    assert(decoded.counter==7);
    assert(decoded.cmd_len==sizeof cmd);
    assert(std::memcmp(decoded.cmd,cmd,sizeof cmd)==0);
    assert(frame[0]==rand8[0]);
    printf("checks=7 length=%zu body=",len);
    for (size_t i=0;i<len;++i) printf("%02x",frame[i]);
    printf("\n");
}
''')
run('capture-compile', ['g++', '-std=c++20', '-O2', '-Ilib/core', '-Ilib/monocypher/src', str(capture),
    'lib/core/admin_auth.cpp','lib/core/identity.cpp','lib/core/dm_crypto.cpp','lib/monocypher/src/monocypher.c', '-o', str(OUT / 'capture-legacy')])
literal_log = run('capture-legacy', [str(OUT / 'capture-legacy')])
body = bytes.fromhex(re.search(r'body=([0-9a-f]+)', literal_log).group(1))
assert len(body)==40
save('legacy-capture.json', dict(length=len(body), body=body.hex(), sha256=hashlib.sha256(body).hexdigest(), checks=7,
    base='84edd3ebfb08d807645d077269bace145ac2a2ab', fixture='test/test_remote_codec.cpp §8 fixed inputs'))
added = OUT / 'test_remote_codec-extra-legacy-literal.cpp'
added.write_text(source + '\nconst uint8_t kRefLegacySealed[40] = {' + ','.join(f'0x{x:02x}' for x in body) + '};\n')
run('reference-extra-legacy-refused', [PYTHON,str(REF),'--freeze-check','--compare',str(added)], expected=1)
save('reference-boundary.json', dict(base='94/94, five comparator controls RED',
    entire_legacy_case_removed='94/94, exit 0: deletion is invisible to the reference comparator',
    added_legacy_literal='exit 1: EXTRA kRefLegacySealed',
    required_change='fence a reference extension, update 94->95 total while retaining the original 94, and check case presence separately'))
for name, record in inputs.items():
    p = ROOT / name
    assert (os.readlink(p) == record['symlink']) if 'symlink' in record else (digest(p) == record['sha256']), name
run('meshroute-whitespace', ['git','diff','--check'])
run('simulator-whitespace', ['git','diff','--check'], SIM)
print('preflight complete: STOP-1; all initial inputs preserved', flush=True)
