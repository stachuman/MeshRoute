#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""W7+W8 §4.1 final step 12 (D6/P7): every reader of a changed symbol across lib/ src/ test/ tools/, split into the
fence (edited by this package) and outside it (byte-identical to HEAD = base, so its TEXT cannot have moved; whether
its PREDICATE moved is judged per reader in the report), the `SendReq{…}` aggregate initialisers, and the three
whole-tree scanners run stock. Usage: python3 -B reader_audit.py <out.json>. Exit 1 if a scanner fails or an
out-of-fence reader differs from HEAD."""
import hashlib, json, re, subprocess, sys
from pathlib import Path

R = Path("/home/staszek/MeshRoute")
SYMS = ["SendKind", "SendReq", "HomeView", "ComposeRow", "ReviewPhase", "send_gate_of", "ui_nav_slot", "SendLive",
        "SendTracker", "ui_pump_trackers", "match_dm", "match_aired", "match_channel_sent", "match_blocked",
        "send_dest_gate_of", "send_exec_gate_of", "compose_row_kind", "compose_row_count"]
FENCE = {"src/firmware_ui_editor.h", "src/firmware_ui_model.h", "src/firmware_ui_send.h", "src/firmware_ui_chrome.h",
         "src/firmware_ui.cpp", "test/test_firmware_ui_editor.cpp", "test/test_firmware_ui_model.cpp",
         "test/test_firmware_ui_send.cpp", "test/test_firmware_ui_chrome.cpp", "test/test_firmware_ui_presets.cpp",
         "tools/probe_firmware_ui/probe_main.cpp",
         "tools/probe_firmware_ui/run.sh", "tools/probe_board_ui/run.sh", "tools/probe_board_ui/expected.tsv",
         "tools/probe_board_ui/accounting.py", "tools/probe_ui_model_mutations.py", "tools/probe_board_abi.py"}
files = subprocess.run(["git", "ls-files", "-co", "--exclude-standard", "lib", "src", "test", "tools"], cwd=R,
                       capture_output=True, text=True).stdout.split()
out, bad = {"symbols": {}, "out_of_fence_identical_to_head": {}}, False
for sym in SYMS:
    rx = re.compile(r"\b" + re.escape(sym) + r"\b")
    rd = {}
    for f in files:
        p = R / f
        if not p.is_file():
            continue
        try:
            t = p.read_text(errors="replace")
        except OSError:
            continue
        n = len(rx.findall(t))
        if n:
            rd[f] = n
    out["symbols"][sym] = {"in_fence": {f: n for f, n in sorted(rd.items()) if f in FENCE},
                           "outside_fence": {f: n for f, n in sorted(rd.items()) if f not in FENCE}}
for f in sorted({f for s in out["symbols"].values() for f in s["outside_fence"]}):
    head = subprocess.run(["git", "show", "HEAD:" + f], cwd=R, capture_output=True).stdout
    same = hashlib.sha256(head).hexdigest() == hashlib.sha256((R / f).read_bytes()).hexdigest()
    out["out_of_fence_identical_to_head"][f] = same
    bad |= not same
agg = {}
for f in files:
    p = R / f
    if p.is_file() and p.suffix in (".h", ".cpp", ".py"):
        n = p.read_text(errors="replace").count("SendReq{")
        if n:
            agg[f] = n
out["sendreq_aggregate_initialisers"] = agg
scanners = {"check_data_type_literals": ["python3", "-B", "tools/check_data_type_literals.py"],
            "check_data_type_literals --selftest": ["python3", "-B", "tools/check_data_type_literals.py", "--selftest"],
            "probe_features/ownership": ["python3", "-B", "tools/probe_features/ownership.py"],
            "probe_features/ownership --controls": ["python3", "-B", "tools/probe_features/ownership.py", "--controls"],
            "probe_build_identity": ["python3", "-B", "tools/probe_build_identity.py"]}
out["whole_tree_scanners"] = {}
for name, cmd in scanners.items():
    r = subprocess.run(cmd, cwd=R, capture_output=True, text=True)
    tail = [l for l in (r.stdout + r.stderr).strip().splitlines() if l.strip()][-3:]
    out["whole_tree_scanners"][name] = {"exit": r.returncode, "tail": tail}
    bad |= r.returncode != 0
out["verdict"] = "PASS" if not bad else "FAIL"
json.dump(out, open(sys.argv[1], "w"), indent=1, sort_keys=True)
for s, v in out["symbols"].items():
    print(f"{s}: fence {len(v['in_fence'])} file(s); outside {v['outside_fence']}")
print("outside-fence readers identical to HEAD:", all(out["out_of_fence_identical_to_head"].values()),
      len(out["out_of_fence_identical_to_head"]))
print("SendReq{ initialisers:", agg)
for n, v in out["whole_tree_scanners"].items():
    print(f"scanner {n}: exit {v['exit']} | {v['tail'][-1] if v['tail'] else ''}")
print("READER AUDIT", out["verdict"])
sys.exit(1 if bad else 0)
