# Author: coder (B478/B487/B488/B490 package) — §4.1 step 12: frozen-input hashes at the start and end of the chain
"""Usage: python3 -B stability.py <out.json> [start.json]

Hashes EVERY git-visible MeshRoute path (`git ls-files -co --exclude-standard`) except this package's own outputs
(the report and the evidence directory, which the chain writes), plus the simulator's HEAD and status. With a start
manifest, exits 1 unless nothing changed in between."""
import hashlib, json, os, subprocess, sys
R = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
SIM = os.path.join(os.path.dirname(R), "lora-universal-simulator")
OUT = ("docs/superpowers/evidence/2026-09-30-b478-b487-b488.md", "docs/superpowers/evidence/2026-09-30-b478-b487-b488/")
files = sorted(os.fsdecode(x) for x in subprocess.run(["git", "ls-files", "-co", "--exclude-standard", "-z"], cwd=R,
                                                      capture_output=True).stdout.split(b"\0") if x)
manifest = {f: hashlib.sha256(open(os.path.join(R, f), "rb").read()).hexdigest()
            for f in files if not f.startswith(OUT) and os.path.isfile(os.path.join(R, f))}
sim = {"head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=SIM, text=True).strip(),
       "status": subprocess.check_output(["git", "status", "--porcelain"], cwd=SIM, text=True)}
json.dump({"files": manifest, "simulator": sim}, open(sys.argv[1], "w"), indent=0, sort_keys=True)
print(f"{len(manifest)} frozen inputs hashed; simulator {sim['head'][:7]} {'clean' if not sim['status'] else 'DIRTY'}")
if len(sys.argv) > 2:
    ref = json.load(open(sys.argv[2]))
    moved = sorted(set(ref["files"]) ^ set(manifest)) + sorted(f for f in manifest if ref["files"].get(f, manifest[f]) != manifest[f])
    same = not moved and ref["simulator"] == sim
    print("STABLE — no input changed during the chain" if same else f"MOVED: {moved[:20]} simulator same={ref['simulator'] == sim}")
    sys.exit(0 if same else 1)
