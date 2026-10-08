#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""W7+W8 §4.1 final step 4: base -> final board attribution (NOT the stock `compare`, which refuses a source change
by design). Per env, from `.pio-measure/w7w8/{base-1,final-1}/<env>/`: the manifest's measurements, every loadable
section, and every symbol whose size changed (RAM objects separately from code). The compatibility fields
(toolchain, fixed identity, paths, concurrency, host, schema, environment) must be identical.
Usage: python3 -B board_attribution.py <out.json> [base-run=base-1] [final-run=final-1]. Exit 1 if a compatibility field differs."""
import json, sys
from pathlib import Path

ROOT = Path("/home/staszek/MeshRoute")
M = ROOT / ".pio-measure/w7w8"
RAM_SECTIONS = (".dram0.data", ".dram0.bss", ".noinit", ".rtc.force_fast", ".rtc_noinit", ".rtc.force_slow")


def flat(d, prefix=""):
    out = {}
    if isinstance(d, dict):
        for k, v in d.items():
            out.update(flat(v, f"{prefix}{k}."))
    else:
        out[prefix[:-1]] = d
    return out


def symbols(p):
    out = {}
    for line in p.read_text(errors="replace").splitlines():
        f = line.split("\t")
        if len(f) != 6 or f[1] not in ("OBJECT", "FUNC"):
            continue
        key = (f[0], f[1], f[4])
        out[key] = out.get(key, 0) + int(f[5])
    return out


def sections(p):
    out = {}
    for line in p.read_text().splitlines():
        if "]" in line and line.strip().startswith("["):
            parts = line.split("]", 1)[1].split()
            if len(parts) >= 6 and parts[0].startswith("."):
                out[parts[0]] = {"size": int(parts[4], 16), "addr": parts[2]}
    return out


BASE_RUN = sys.argv[2] if len(sys.argv) > 2 else "base-1"
FINAL_RUN = sys.argv[3] if len(sys.argv) > 3 else "final-1"
res, bad = {"runs": [BASE_RUN, FINAL_RUN]}, False
for env in ("gateway", "heltec_mobile"):
    b, f = M / BASE_RUN / env, M / FINAL_RUN / env
    mb, mf = json.loads((b / "manifest.json").read_text()), json.loads((f / "manifest.json").read_text())
    fb, ff = flat(mb), flat(mf)
    keys = sorted(set(fb) | set(ff))
    compat = [k for k in keys if k.startswith(("toolchain.", "fixed_identity.", "paths.", "concurrency.", "host."))
              or k in ("schema", "environment")]
    compat_diff = [k for k in compat if fb.get(k) != ff.get(k)]
    bad |= bool(compat_diff)
    meas = {k: {"base": fb.get(k), "final": ff.get(k)} for k in keys
            if k.startswith(("measurements.", "artifacts.payload.")) and fb.get(k) != ff.get(k)}
    sb, sf = sections(b / "sections.txt"), sections(f / "sections.txt")
    sec = {s: {"base": sb.get(s, {}).get("size"), "final": sf.get(s, {}).get("size"),
               "delta": (sf.get(s, {}).get("size") or 0) - (sb.get(s, {}).get("size") or 0)}
           for s in sorted(set(sb) | set(sf)) if sb.get(s) != sf.get(s)}
    yb, yf = symbols(b / "symbols.txt"), symbols(f / "symbols.txt")
    changed = []
    for k in sorted(set(yb) | set(yf)):
        if yb.get(k, 0) != yf.get(k, 0):
            changed.append({"symbol": k[0], "type": k[1], "section": k[2], "base": yb.get(k), "final": yf.get(k),
                            "delta": yf.get(k, 0) - yb.get(k, 0)})
    ram_objs = [c for c in changed if c["type"] == "OBJECT" and c["section"] in RAM_SECTIONS]
    code = [c for c in changed if c not in ram_objs]
    ram_delta = (ff.get("measurements.ram_bytes") or 0) - (fb.get("measurements.ram_bytes") or 0)
    flash_delta = (ff.get("measurements.flash_bytes") or 0) - (fb.get("measurements.flash_bytes") or 0)
    obj_sum = sum(c["delta"] for c in ram_objs)
    res[env] = {"ram": {"base": fb.get("measurements.ram_bytes"), "final": ff.get("measurements.ram_bytes"),
                        "delta": ram_delta, "ram_object_delta_sum": obj_sum, "alignment_and_padding": ram_delta - obj_sum,
                        "ram_objects": ram_objs},
                "flash": {"base": fb.get("measurements.flash_bytes"), "final": ff.get("measurements.flash_bytes"),
                          "delta": flash_delta, "changed_code_and_rodata_symbols": len(code),
                          "code_symbol_delta_sum": sum(c["delta"] for c in code),
                          "largest": sorted(code, key=lambda c: -abs(c["delta"]))[:40]},
                "sections_changed": sec, "measurement_fields_changed": meas,
                "compatibility_fields": len(compat), "compatibility_differences": compat_diff}
    print(f"== {env}: RAM {res[env]['ram']['base']} -> {res[env]['ram']['final']} ({ram_delta:+}); RAM objects "
          f"{[(c['symbol'], c['base'], c['final']) for c in ram_objs]} sum {obj_sum:+}, alignment {ram_delta - obj_sum:+}; "
          f"flash {res[env]['flash']['base']} -> {res[env]['flash']['final']} ({flash_delta:+}); sections "
          f"{ {s: v['delta'] for s, v in sec.items()} }; compatibility diffs {compat_diff}")
json.dump(res, open(sys.argv[1], "w"), indent=1)
sys.exit(1 if bad else 0)
