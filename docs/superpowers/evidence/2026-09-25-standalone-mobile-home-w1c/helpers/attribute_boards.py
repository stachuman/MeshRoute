#!/usr/bin/env python3
"""W1 base -> final board attribution (brief §4.1 step 5). NOT the stock `compare` (which would fail on the intended
source change by design): reads base-1 and final-1 manifests per env and reports every measurements.* difference and
artifacts.payload.sha256, records source.* / normal_pio_metadata.* differences as expected, and REQUIRES toolchain.*,
fixed_identity.*, paths.*, concurrency.*, host.* and schema/environment to be identical (the compatibility check).
Exit 1 if a compatibility field differs; exit 0 otherwise (deltas are attributed in prose, not allowed numerically)."""
import json, sys
from pathlib import Path

def flat(d, prefix=""):
    out = {}
    if isinstance(d, dict):
        for k, v in d.items():
            out.update(flat(v, f"{prefix}{k}."))
    else:
        out[prefix[:-1]] = d
    return out

root = Path(sys.argv[1])
bad = False
for env in ("gateway", "heltec_mobile"):
    b = json.loads((root / "base-1" / env / "manifest.json").read_text())
    f = json.loads((root / "final-1" / env / "manifest.json").read_text())
    fb, ff = flat(b), flat(f)
    keys = sorted(set(fb) | set(ff))
    print(f"== {env}")
    for group in ("measurements.", "artifacts.payload."):
        for k in keys:
            if k.startswith(group):
                same = fb.get(k) == ff.get(k)
                print(f"  {'same ' if same else 'DELTA'} {k}: base={fb.get(k)!r} final={ff.get(k)!r}")
    for k in keys:
        if k.startswith("artifacts.elf."):
            print(f"  {'same ' if fb.get(k) == ff.get(k) else 'delta'} {k} (diagnostic only): base={fb.get(k)!r} final={ff.get(k)!r}")
    for k in keys:
        if k.startswith(("source.", "normal_pio_metadata.")):
            print(f"  {'same ' if fb.get(k) == ff.get(k) else 'EXPECTED-DIFF'} {k}: base={fb.get(k)!r} final={ff.get(k)!r}")
    compat = [k for k in keys if k.startswith(("toolchain.", "fixed_identity.", "paths.", "concurrency.", "host."))
              or k in ("schema", "environment")]
    diff = [k for k in compat if fb.get(k) != ff.get(k)]
    print(f"  compatibility fields checked: {len(compat)}; differing: {len(diff)}")
    for k in diff:
        print(f"  INCOMPATIBLE {k}: base={fb.get(k)!r} final={ff.get(k)!r}")
        bad = True
    other = [k for k in keys if not k.startswith(("measurements.", "artifacts.", "source.", "normal_pio_metadata.",
             "toolchain.", "fixed_identity.", "paths.", "concurrency.", "host.")) and k not in ("schema", "environment")]
    for k in other:
        print(f"  {'same ' if fb.get(k) == ff.get(k) else 'UNCLASSIFIED-DIFF'} {k}")
        if fb.get(k) != ff.get(k):
            bad = True
print("VERDICT:", "INCOMPATIBLE" if bad else "compatible (toolchain/identity/paths identical); deltas listed above")
sys.exit(1 if bad else 0)
