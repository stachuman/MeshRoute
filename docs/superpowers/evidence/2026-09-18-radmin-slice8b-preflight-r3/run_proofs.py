"""Re-run bounded characterizations without modifying the archived evidence."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--snapshot", required=True, type=Path)
args = parser.parse_args()
root = args.snapshot.resolve()
here = Path(__file__).resolve().parent
for name, digest in json.loads((here / "primary-inputs.json").read_text()).items():
    assert hashlib.sha256((root / name).read_bytes()).hexdigest() == digest, name
out = Path(tempfile.mkdtemp(prefix="mr-s8b-r3-reproduction-"))
libraries = sorted((root / ".pio/build/native").glob("lib*/lib*.a"))
assert any(p.name == "libcore.a" for p in libraries)
flags = ["g++", "-std=gnu++20", "-DMESHROUTE_NATIVE=1", "-DMR_N_LAYERS=2",
         "-ffunction-sections", "-fdata-sections"]
for path in ["lib/core", "lib/console", "lib/hal", "lib/monocypher/src", "src",
             ".pio/libdeps/native/doctest/doctest", "tools/probe_board_ui/fakes"]:
    flags.append("-I" + str(root / path))
for name in ["full-mapping", "fanout-order"]:
    binary = out / name
    command = flags + [str(here / (name + "-proof.cpp")), "-Wl,--gc-sections",
                       "-Wl,--start-group"] + [str(p) for p in libraries]
    command += ["-Wl,--end-group", "-o", str(binary)]
    result = subprocess.run(command, capture_output=True)
    (out / (name + "-compile.log")).write_bytes(result.stdout + result.stderr)
    assert result.returncode == 0, out
    result = subprocess.run([str(binary)], capture_output=True)
    (out / (name + "-run.log")).write_bytes(result.stdout + result.stderr)
    print(result.stdout.decode(), end="")
    assert result.returncode == 0, out
    if name == "fanout-order":
        result = subprocess.run([str(binary), "--after"], capture_output=True)
        (out / (name + "-control.log")).write_bytes(result.stdout + result.stderr)
        assert result.returncode == 1 and b"9 checks / 3 failed" in result.stdout, out
print("Characterizations PASS; after-fanout control RED. Logs:", out)
