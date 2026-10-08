#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""W7+W8 QA (2026-10-07): a full read-only inventory of both repositories. Every git-visible path
(`git ls-files -co --exclude-standard`) with its SHA-256 (a symlink hashes its target text), HEAD, the staged set,
`git diff --check`, and the simulator's HEAD, status and inventory. Usage: qa_inventory.py <out.json>."""
import hashlib, json, os, subprocess, sys
from pathlib import Path

MR = Path("/home/staszek/MeshRoute")
SIM = Path("/home/staszek/lora-universal-simulator")


def git(root, *args):
    return subprocess.run(["git", *args], cwd=root, check=True, stdout=subprocess.PIPE).stdout


def inventory(root):
    files = {}
    for raw in sorted(n for n in git(root, "ls-files", "-co", "--exclude-standard", "-z").split(b"\0") if n):
        rel = os.fsdecode(raw)
        p = root / rel
        if p.is_symlink():
            files[rel] = "symlink:" + hashlib.sha256(os.fsencode(os.readlink(p))).hexdigest()
        elif p.is_file():
            files[rel] = hashlib.sha256(p.read_bytes()).hexdigest()
        else:
            files[rel] = "ABSENT"
    return files


out = {}
for name, root in (("meshroute", MR), ("simulator", SIM)):
    status = git(root, "status", "--porcelain=v1", "-z", "--untracked-files=all")
    out[name] = {
        "head": git(root, "rev-parse", "HEAD").decode().strip(),
        "staged": git(root, "diff", "--cached", "--name-only").decode().split(),
        "status_sha256": hashlib.sha256(status).hexdigest(),
        "status_records": len([r for r in status.split(b"\0") if r]),
        "files": inventory(root),
    }
chk = subprocess.run(["git", "diff", "--check"], cwd=MR, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
out["meshroute"]["diff_check"] = {"exit": chk.returncode, "out": chk.stdout.decode(errors="replace")}
out["meshroute"]["file_count"] = len(out["meshroute"]["files"])
out["simulator"]["file_count"] = len(out["simulator"]["files"])
absent = [k for r in ("meshroute", "simulator") for k, v in out[r]["files"].items() if v == "ABSENT"]
out["verdict"] = "PASS" if (not out["meshroute"]["staged"] and out["meshroute"]["diff_check"]["exit"] == 0
                           and out["simulator"]["status_records"] == 0 and not absent) else "FAIL"
json.dump(out, open(sys.argv[1], "w"), indent=1, sort_keys=True)
print(f"meshroute HEAD {out['meshroute']['head'][:12]} files {out['meshroute']['file_count']} staged "
      f"{len(out['meshroute']['staged'])} diff-check {out['meshroute']['diff_check']['exit']}; simulator HEAD "
      f"{out['simulator']['head'][:12]} files {out['simulator']['file_count']} status records "
      f"{out['simulator']['status_records']} -> {out['verdict']}")
sys.exit(0 if out["verdict"] == "PASS" else 1)
