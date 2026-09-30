"""W0 brief review: static census and synthetic contract counterexample only.

Run from any directory; prints the source-audit.json measurement. Does not import
the mutation harness, compile firmware, implement the service or write files.
"""
import ast
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
source = ROOT / "tools/probe_ui_model_mutations.py"
tree = ast.parse(source.read_text())
assign = {}
for node in tree.body:
    if isinstance(node, ast.Assign):
        for target in node.targets:
            if isinstance(target, ast.Name):
                assign[target.id] = node.value
names = {
    "devicenv": "MUTS_DEVICENV", "config": "MUTS_CONFIG",
    "w1cname": "MUTS_W1CNAME", "consoleline": "MUTS_CONSOLELINE",
    "radmin4verbs": "MUTS_RADMIN4VERBS",
}
targets = ast.literal_eval(assign["TARGET_SRC"])
entries = []
for name, var in names.items():
    node = assign[var]
    assert isinstance(node, ast.List)
    assert not any(isinstance(a, ast.AugAssign) and isinstance(a.target, ast.Name)
                   and a.target.id == var for a in ast.walk(tree))
    entries.append({"battery": name, "source": targets[name],
                    "count": len(node.elts), "list_line": node.lineno})

# Synthetic data, with the actual IdBlob field order/offsets. This calculation
# proves the brief's equality branch is possible, not a firmware execution.
seed = bytes(range(32))
live, requested = b"Live Name", b"Saved Name"
def record(name):
    return struct.pack("<IHH32s32sii", 0x4D524944, 1, len(name), seed,
                       name, 521234567, 211234567)
candidate, durable = record(requested), record(requested)
assert len(candidate) == 80 and candidate == durable and live != requested
counter = {
    "kind": "synthetic contract counterexample; not firmware execution",
    "layout": "<IHH32s32sii; source IdBlob offsets and pre-check three-ABI size",
    "candidate_equals_successfully_loaded_durable": candidate == durable,
    "live_name_hex": live.hex(), "requested_name_hex": requested.hex(),
    "record_bytes": len(candidate),
    "candidate_sha256": hashlib.sha256(candidate).hexdigest(),
    "brief_step_3_result": "unchanged; zero writes",
    "brief_step_4_condition": "otherwise, save then publish; not reached",
    "live_name_matches_request_without_publication": False,
}
print(json.dumps({
    "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
    "mutation_batteries": entries, "selector_a": entries[0]["count"],
    "selector_b": sum(e["count"] for e in entries[1:]),
    "union": sum(e["count"] for e in entries), "counterexample": counter,
}, indent=2))
