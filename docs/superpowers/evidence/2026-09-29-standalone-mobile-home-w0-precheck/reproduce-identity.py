"""W0 BEFORE-state measurement, not an implementation or gate.

Run from MeshRoute: python3 <this-file>. Builds the real command and config TUs.
The storage/RNG/radio are existing host fakes. BLE is two verbatim extracted
branches, NOT a build of all fw_main.cpp or a BLE hardware observation.
"""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
source = (ROOT / 'tools/probe_inbox_verbs/probe_main.cpp').read_text()
start = source.index('namespace mrfw {\nvoid handle_cfg_set')
end = source.index('}  // namespace mrfw', start) + len('}  // namespace mrfw')
source = source[:start] + source[end:]
source = source.replace('int main() {', 'int historical_probe_main() {', 1)
source += '\n' + (HERE / 'identity-cases.inc').read_text()
fw = (ROOT / 'src/fw_main.cpp').read_text()
start = fw.index('    if (len == 6 && !strncmp(line, "whoami", 6)) {', fw.index('static size_t ble_dispatch_line'))
end = fw.index('    if (len == 7 && !strncmp(line, "version", 7))', start)
who = fw[start:end]
start = fw.index('    if (len > 8 && !strncmp(line, "cfg set ", 8)) {', end)
end = fw.index('    if (len == 3 && !strncmp(line, "cfg", 3))', start)
cfg = fw[start:end]
assert who + cfg == (HERE / 'ble-source-fragments.txt').read_text()
assert who in source and cfg in source
with tempfile.TemporaryDirectory(prefix='mr-w0-identity-') as temp:
    scratch = Path(temp)
    probe = scratch / 'repro.cpp'
    probe.write_text(source)
    runner = (ROOT / 'tools/probe_inbox_verbs/run.sh').read_text()
    boundary = 'rc=0\nif ! build_support;'
    assert runner.count(boundary) == 1
    runner = runner.split(boundary)[0]
    runner = runner.replace('cd "$(dirname "$0")" || exit 1', f'cd "{ROOT}/tools/probe_inbox_verbs" || exit 1')
    runner = runner.replace('OUT=$(mktemp -d)', f'OUT="{scratch}/build"\nmkdir -p "$OUT"')
    runner = runner.replace("trap 'rm -rf \"$OUT\"' EXIT", '')
    runner = runner.replace('"$HERE/probe_main.cpp"', f'"{probe}"')
    flags = '-DLORA_FREQ=869.4625 -DLORA_TX_POWER=22 -DLORA_BW=125.0 -DLORA_SF=8 -DLORA_CR=5 -DLORA_DUTY_CYCLE_PCT=10'
    runner = runner.replace('STD=(-std=gnu++20', f'STD=({flags} -include "{HERE}/identity-platform-shim.h" -ffunction-sections -fdata-sections -std=gnu++20')
    runner = runner.replace('"$CXX" "$OUT/v_main.o"',
        '"$CXX" "${STD[@]}" "${DEFS[@]}" "${INCS[@]}" -c "$ROOT/src/firmware_config.cpp" -o "$OUT/v_config.o" 2>>"$OUT/build.log" \\\n    && "$CXX" -Wl,--gc-sections "$OUT/v_config.o" "$OUT/v_main.o"')
    runner += '\nif ! build_support; then cat "$OUT/pm.log"; exit 1; fi\n'
    runner += 'if ! build_variant "$FW_CMDS" "$FW_INBOX" "" "$OUT/repro"; then cat "$OUT/build.log"; exit 1; fi\n"$OUT/repro"\n'
    shell = scratch / 'run.sh'
    shell.write_text(runner)
    result = subprocess.run(['bash', str(shell)], cwd=ROOT, env=dict(os.environ, MR_PROBE_ARM='accept'), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print(result.stdout.decode('utf-8', errors='backslashreplace'), end='')
    raise SystemExit(result.returncode)
