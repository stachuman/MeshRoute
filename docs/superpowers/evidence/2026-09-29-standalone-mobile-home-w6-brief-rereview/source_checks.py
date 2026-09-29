"""Focused source assertions and replay of the unchanged B477 fake reproduction.

This does not implement W6 or run its gates. Compilation is in external scratch.
"""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
OLD = ROOT / 'docs/superpowers/evidence/2026-09-29-standalone-mobile-home-w6-brief-review'

checks = []
def check(path, needle, count=1):
    text = (ROOT / path).read_text()
    actual = text.count(needle)
    assert actual == count, (path, needle, actual)
    checks.append(dict(path=path, needle=needle, matches=actual))

FAKE = 'tools/probe_inbox_verbs/fakes/Preferences.h'
RUNNER = 'tools/probe_inbox_verbs/run.sh'
check(FAKE, 'unsigned char data[2304]')
check(FAKE, 'if (!s || n > sizeof s->data) return;')
check(FAKE, 'return nv.fail_write ? 0 : n;')
check(FAKE, 'bool retain_on_fail = false;')
check(FAKE, 'bool drop_on_ok     = false;')
check(RUNNER, 'rc=0\nif ! build_support;')
check(RUNNER, 'build_support()')
check(RUNNER, 'build_variant()')
check(RUNNER, "'s|bool retain_on_fail = false;|bool retain_on_fail = true;|'")
check(RUNNER, "'s|bool drop_on_ok     = false;|bool drop_on_ok     = true;|'")
check('tools/probe_deferred_actions/run.py', "boundary='rc=0\\nif ! build_support;'", 1)
check('tools/probe_deferred_actions/probe.cpp', '#include "../probe_inbox_verbs/probe_main.cpp"')
check('src/firmware_commands.cpp', 'mrnv::UiPresetRead load(mrnv::UiPresetBlob& out) override { return mrnv::load_ui_presets(out); }')
check('src/firmware_commands.cpp', 'if (dispatch(line, len, stream, ctx.transport))')
check('src/firmware_commands.cpp', 'handle_ui(line + 2, len - 2, out); return true;')
check('src/firmware_commands.cpp', 'PresetPrintLines lines(out);')
for needle in ('the six reason spellings', 'FOUR-valued read', 'three 372-B records', '≈ 1.15 KB'):
    check('src/firmware_commands.cpp', needle)

scratch = Path(tempfile.mkdtemp(prefix='w6-r2-review-'))
source = OLD / 'fake-query.cpp'
command = ['g++', '-std=c++20', '-O2', str(source), '-o', str(scratch / 'fake-query')]
subprocess.run(command, check=True, capture_output=True)
output = subprocess.check_output([str(scratch / 'fake-query')], text=True)
previous = json.loads((OLD / 'measurements.json').read_text())
assert output == previous['fake_result']
receipt = dict(scope='Unmodified fake reproduction, not W6 implementation or gate',
               source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
               fake_sha256=hashlib.sha256((ROOT / FAKE).read_bytes()).hexdigest(),
               compile_command=command, output=output, checks=checks,
               previous_response_measurement='Hash-verified historical input; not rerun in scoped review')
(HERE / 'source-checks.json').write_text(json.dumps(receipt, indent=2) + '\n')
print(f'{len(checks)} source/anchor assertions match; unmodified fake reproduction matches prior result exactly')
