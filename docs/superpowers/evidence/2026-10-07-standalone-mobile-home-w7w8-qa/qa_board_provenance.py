#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""W7+W8 QA (2026-10-07): explain the board manifests' SOURCE provenance difference between the coder's final-1 and
QA's qa-1. The path set the coder's chain measured is reconstructed from the checkpoint inventory (QA, 2026-10-06) plus
the 18 paths the coder's scope-start recorded as new; `measure_board.py::source_snapshot`'s digest is recomputed over
it with TODAY's contents (the algorithm is restated here, not imported) and must equal final-1's `tree_sha256`; the
porcelain status with the post-chain paths' records removed must hash to final-1's `git_status_sha256`. The same digest
over today's whole set must equal qa-1's own fields. Usage: qa_board_provenance.py <out.json>."""
import hashlib, json, os, subprocess, sys
from pathlib import Path

ROOT = Path("/home/staszek/MeshRoute")
E = ROOT / "docs/superpowers/evidence/2026-10-04-standalone-mobile-home-w7w8"
ck = json.loads((ROOT / "docs/superpowers/evidence/2026-10-06-standalone-mobile-home-w7w8-stop-resolution/inputs.json").read_text())
start = json.loads((E / "final/scope-start.json").read_text())
chain_set = set(ck["files"]) | set(start["checkpoint_inventory"]["new"])


def sha(p):
    return hashlib.sha256(p.read_bytes()).digest()


# ★ The coder's evidence files that existed at the chain's start are recorded there with their START hashes
#   (`inventory.meshroute.new_hashes`); a later evidence write (e.g. the ledger after the chain) must use those.
recorded = start["inventory"]["meshroute"]["new_hashes"]


def digest(names, use_recorded=False):
    d = hashlib.sha256()
    for enc in sorted(os.fsencode(n) for n in names):
        p = ROOT / os.fsdecode(enc)
        rel = os.fsdecode(enc)
        if use_recorded and rel in recorded and not recorded[rel].startswith("symlink"):
            h = bytes.fromhex(recorded[rel])
        else:
            h = hashlib.sha256(os.fsencode(os.readlink(p))).digest() if p.is_symlink() else sha(p)
        d.update(len(enc).to_bytes(4, "big")); d.update(enc); d.update(b"\1" + h)
    return d.hexdigest()


listed = subprocess.run(["git", "ls-files", "-co", "--exclude-standard", "-z"], cwd=ROOT, check=True,
                        stdout=subprocess.PIPE).stdout
now = {os.fsdecode(n) for n in listed.split(b"\0") if n}
status = subprocess.check_output(["git", "status", "--porcelain=v1", "-z", "--untracked-files=all"], cwd=ROOT)
post = sorted(now - chain_set)
recs = [r for r in status.split(b"\0") if r]
kept = [r for r in recs if os.fsdecode(r[3:]) not in set(post)]
status_chain = b"".join(r + b"\0" for r in kept)
out = {"chain_paths": len(chain_set), "now_paths": len(now), "missing_from_now": sorted(chain_set - now),
       "post_chain_paths": post,
       "post_chain_all_coder_evidence": all(p == "docs/superpowers/evidence/2026-10-04-standalone-mobile-home-w7w8.md"
                                            or p.startswith("docs/superpowers/evidence/2026-10-04-standalone-mobile-home-w7w8/")
                                            for p in post)}
out["chain_tree_sha256_current_contents"] = digest(chain_set)
out["chain_tree_sha256"] = digest(chain_set, use_recorded=True)
out["chain_files_rewritten_after_start"] = sorted(r for r in recorded if r in chain_set and (ROOT / r).is_file()
                                                  and hashlib.sha256((ROOT / r).read_bytes()).hexdigest() != recorded[r])
out["chain_status_sha256"] = hashlib.sha256(status_chain).hexdigest()
out["now_tree_sha256"] = digest(now)
out["now_status_sha256"] = hashlib.sha256(status).hexdigest()
for label, d in (("final-1", ROOT / ".pio-measure/w7w8/final-1"), ("qa-1", ROOT / ".pio-measure/w7w8/qa-1")):
    out[label] = {env: json.loads((d / env / "manifest.json").read_text())["source"] for env in ("gateway", "heltec_mobile")}
f1, q1 = out["final-1"]["gateway"], out["qa-1"]["gateway"]
checks = {
    "final1_envs_agree": out["final-1"]["gateway"] == out["final-1"]["heltec_mobile"],
    "qa1_envs_agree": out["qa-1"]["gateway"] == out["qa-1"]["heltec_mobile"],
    "chain_set_size_equals_final1_file_count": len(chain_set) == f1["file_count"],
    "chain_tree_equals_final1": out["chain_tree_sha256"] == f1["tree_sha256"],
    "chain_status_equals_final1": out["chain_status_sha256"] == f1["git_status_sha256"],
    "now_tree_equals_qa1": out["now_tree_sha256"] == q1["tree_sha256"],
    "now_status_equals_qa1": out["now_status_sha256"] == q1["git_status_sha256"],
    "now_count_equals_qa1": len(now) == q1["file_count"],
    "nothing_missing": not out["missing_from_now"],
    "post_chain_only_coder_evidence": out["post_chain_all_coder_evidence"],
}
out["checks"] = checks
out["verdict"] = "PASS" if all(checks.values()) else "FAIL"
json.dump(out, open(sys.argv[1], "w"), indent=1)
for k, v in checks.items():
    print(f"  {k}: {v}")
print(f"post-chain paths: {len(post)}; provenance -> {out['verdict']}")
sys.exit(0 if out["verdict"] == "PASS" else 1)
