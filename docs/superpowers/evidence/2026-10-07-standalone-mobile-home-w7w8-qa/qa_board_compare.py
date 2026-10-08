#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""W7+W8 QA (2026-10-07): QA's own board manifests against the coder's final-1 and final-2, per environment, on the
stock `measure_board.py` QUALIFICATION_FIELDS (read from the tool's source by AST — never imported, never edited).
A field under `source.` or `normal_pio_metadata.` is PROVENANCE (the checkout and the normal `.pio/` at the moment of
the run); it is listed and must be explained separately. Every other qualification field — measurements, payload,
symbols, toolchain, identity, paths — must be equal. Every differing leaf of the whole manifest is also listed.
Usage: qa_board_compare.py <qa_dir> <final1_dir> <final2_dir> <out.json>."""
import ast, json, sys
from pathlib import Path

tool = Path("/home/staszek/MeshRoute/tools/measure_board.py")
fields = None
for node in ast.parse(tool.read_text()).body:
    if isinstance(node, ast.Assign) and any(getattr(t, "id", None) == "QUALIFICATION_FIELDS" for t in node.targets):
        fields = ast.literal_eval(node.value)
assert fields, "QUALIFICATION_FIELDS not found"
PROVENANCE = ("source.", "normal_pio_metadata.")


def nested(v, dotted):
    for k in dotted.split("."):
        v = v[k]
    return v


def leaves(v, prefix=""):
    if isinstance(v, dict):
        for k, x in v.items():
            yield from leaves(x, f"{prefix}{k}.")
    else:
        yield prefix[:-1], v


qa_dir, f1, f2, out_path = map(Path, sys.argv[1:5])
out = {"qualification_fields": len(fields), "envs": {}}
ok = True
for env in ("gateway", "heltec_mobile"):
    qa = json.loads((qa_dir / env / "manifest.json").read_text())
    e = {"qa_measurements": qa["measurements"], "qa_payload": qa["artifacts"]["payload"]}
    for label, ref_dir in (("final-1", f1), ("final-2", f2)):
        ref = json.loads((ref_dir / env / "manifest.json").read_text())
        mism = [f for f in fields if nested(qa, f) != nested(ref, f)]
        prov = [f for f in mism if f.startswith(PROVENANCE)]
        hard = [f for f in mism if not f.startswith(PROVENANCE)]
        lq, lr = dict(leaves(qa)), dict(leaves(ref))
        e[label] = {"qualification_mismatches": mism, "provenance": prov, "non_provenance": hard,
                    "all_differing_leaves": sorted(k for k in set(lq) | set(lr) if lq.get(k) != lr.get(k)),
                    "ref_source": ref["source"], "ref_normal_pio_metadata": ref["normal_pio_metadata"]}
        ok &= not hard
    e["qa_source"], e["qa_normal_pio_metadata"] = qa["source"], qa["normal_pio_metadata"]
    out["envs"][env] = e
    m = qa["measurements"]
    print(f"{env}: RAM {m['ram_bytes']} flash {m['flash_bytes']} objects {m['object_count']} symbols "
          f"{m['symbol_count']} payload {qa['artifacts']['payload']['sha256'][:12]}; vs final-1 non-provenance "
          f"{e['final-1']['non_provenance'] or 'none'}, provenance {e['final-1']['provenance'] or 'none'}; vs final-2 "
          f"non-provenance {e['final-2']['non_provenance'] or 'none'}")
out["verdict"] = "PASS" if ok else "FAIL"
json.dump(out, open(out_path, "w"), indent=1)
print("board fields:", out["verdict"])
sys.exit(0 if ok else 1)
