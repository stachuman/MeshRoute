"""W0 re-review diagnostic. Never a gate or a repository repair.

Runs the stock transcript tool, then supplies only scratch compile/link inputs to
observe downstream faults. All generated files stay in the supplied external
directory. Run: python3 -B <this-file> --out /tmp/<new-directory>
"""
import argparse
import hashlib
import importlib.util
import json
import re
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[4]
parser = argparse.ArgumentParser()
parser.add_argument("--out", type=Path, required=True)
args = parser.parse_args()
OUT = args.out.resolve()
assert not OUT.is_relative_to(ROOT), "diagnostics must stay outside the repository"
OUT.mkdir(parents=True, exist_ok=False)
WORK = OUT / "transcript"
records = []

def run(label, command, env=None):
    result = subprocess.run(command, cwd=ROOT, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (OUT / (label + ".log")).write_bytes(result.stdout)
    records.append({"label": label, "command": command, "exit": result.returncode,
                    "log_sha256": hashlib.sha256(result.stdout).hexdigest()})
    return result

tool = ROOT / "tools/probe_inbox_verbs/transcript.py"
stock = run("stock", [sys.executable, "-B", str(tool), "--out", str(WORK)])
assert stock.returncode == 2 and b"'mrble'" in stock.stdout.replace("‘".encode(), b"'").replace("’".encode(), b"'")
spec = importlib.util.spec_from_file_location("w0_transcript_drift", tool)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
flags, env = module.default_flags(str(WORK)), module.build_env()
driver = WORK / "diagnostic-driver.o"
result = run("include-only", ["g++", *flags, "-Wall", "-Wextra", "-Werror",
    "-Wno-volatile", "-Wno-deprecated-declarations", "-Wno-unused-function",
    "-include", str(ROOT / "src/device_ble.h"),
    '-DMR0C_ADAPTERS_HEADER="mr0c_adapters.h"', "-c",
    str(ROOT / "tools/probe_inbox_verbs/transcript_main.cpp"), "-o", str(driver)], env)
assert result.returncode == 0
objects = sorted(str(p) for p in WORK.glob("o_*.o"))
binary = WORK / "diagnostic.bin"
missing_link = run("missing-link-inputs", ["g++", str(driver), *objects,
                                          "-o", str(binary)], env)
assert missing_link.returncode != 0
extra = []
for stem in ("firmware_remote_actions", "firmware_remote_client"):
    obj = WORK / ("diagnostic-" + stem + ".o")
    result = run(stem, ["g++", *flags, "-Wall", "-Wextra", "-c",
                        str(ROOT / ("src/" + stem + ".cpp")), "-o", str(obj)], env)
    assert result.returncode == 0
    extra.append(str(obj))
# These are the standing inbox runner's actual link flags, not substitute symbols.
link = run("supplemented-link", ["g++", str(driver), *objects, *extra,
    "-Wl,--wrap=crypto_wipe", "-Wl,--wrap=_ZN9meshroute4Node10on_commandERKNS_7CommandE",
    "-o", str(binary)], env)
assert link.returncode == 0
runtime = run("runtime", [str(binary)], env)
assert runtime.returncode == 0
text = runtime.stdout.decode("utf-8")
current = None
long_rows = []
for line in text.splitlines():
    match = re.match(r"LINE (\S+) len=(\d+) \[(.*)\]", line)
    if match:
        current = {"id": match[1], "length": int(match[2]), "command": match[3]}
    elif line.startswith("  SER ") and current and current["length"] > 7:
        long_rows.append(current | {"too_long": "> err bad_line too_long\\n" in line})
try:
    module.verify_profile(text, module.PROBE_PROFILE)
    profile = "PASS"
except module.ExtractError as exc:
    profile = str(exc)
assert profile != "PASS"
capacity = WORK / "capacity.cpp"
capacity.write_text('#include <cstdio>\n#include "console_line.h"\n'
    'static size_t wrapper_limit(const char* line) { return sizeof(line) - 1; }\n'
    'int main() { char line[meshroute::console::local_command_max_bytes + 1] = {}; '
    'printf("wrapper_limit=%zu actual_array_limit=%zu\\n",wrapper_limit(line), sizeof(line)-1); }\n')
compiled = run("capacity-compile", ["g++", *flags, str(capacity), "-o", str(WORK / "capacity")], env)
assert compiled.returncode == 0
cap = run("capacity", [str(WORK / "capacity")], env)
assert cap.returncode == 0
summary = {
    "kind": "stock failure plus labelled scratch diagnostics; not an implementation gate",
    "stock_exit": stock.returncode, "matrix_rows": len(re.findall(r"^LINE ", text, re.M)),
    "rows_above_seven": len(long_rows),
    "above_seven_refused_too_long": sum(row["too_long"] for row in long_rows),
    "profile_check": profile, "capacity_measurement": cap.stdout.decode().strip(),
    "example": next(row for row in long_rows if row["command"] == "acl list"),
    "runs": records,
}
(OUT / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
print(json.dumps({k: v for k, v in summary.items() if k != "runs"}, indent=2))
