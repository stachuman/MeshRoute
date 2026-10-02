"""Verify the pre-check's entry inventory; writes only its evidence receipt.

Run from MeshRoute with python3 -B. No build, cleanup or repository mutation.
The entry inventory includes tracked and nonignored untracked inputs.
"""
from pathlib import Path
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
SIM = ROOT.parent / "lora-universal-simulator"
REPORT = HERE.with_suffix(".md")
REGISTER = "docs/2026-07-30-open-bug-register.md"


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args]).decode().strip()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


entry = json.loads((HERE / "inputs.json").read_text())
result = {}
for name, root in (("meshroute", ROOT), ("simulator", SIM)):
    before = entry[name]
    changed, missing = [], []
    for rel, info in before["files"].items():
        p = root / rel
        if not p.exists():
            missing.append(rel)
        elif "sha256" in info and digest(p) != info["sha256"]:
            changed.append(rel)
        elif "gitlink" in info:
            # Entry collector labels directory symlinks with their ls-files --stage
            # record. They are mode 120000 links, not nested repository HEAD pins.
            if git(root, "ls-files", "--stage", "--", rel) != info["gitlink"]:
                changed.append(rel)
            elif p.is_symlink() and str(p.readlink()) != git(root, "show", "HEAD:" + rel):
                changed.append(rel)
    current = set(subprocess.check_output(
        ["git", "-C", str(root), "ls-files", "-z", "--cached", "--others", "--exclude-standard"]
    ).decode().split("\0")) - {""}
    added = sorted(current - set(before["files"]))
    unexplained = []
    if name == "meshroute":
        allowed = lambda x: (root / x) == REPORT or (root / x).is_relative_to(HERE)
        unexplained = [x for x in added if not allowed(x)]
        assert changed == [REGISTER], changed
    else:
        assert not changed and not added and not git(root, "status", "--porcelain"), (changed, added)
    assert not missing and not unexplained, (missing, unexplained)
    assert git(root, "rev-parse", "HEAD") == before["head"]
    assert not git(root, "diff", "--cached", "--name-only")
    result[name] = {
        "head": before["head"], "entry_paths_checked": len(before["files"]),
        "changed": changed, "missing": missing, "unexplained_new": unexplained,
        "index_empty": True, "status": git(root, "status", "--short"),
        "new_path_policy": "Only this report/evidence folder" if name == "meshroute" else "None",
    }
result["register_sha256"] = digest(ROOT / REGISTER)
result["scope"] = "Entry-input preservation; output receipt and SHA256SUMS do not inventory themselves."
(HERE / "preservation.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2))
