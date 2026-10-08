#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""W7+W8 QA (2026-10-07): QA's own reader check (brief §4.1 step 12 / §4.2, D6/P7). (1) Every reader OUTSIDE the
fence of a changed symbol, across lib/src/test/tools, with each line classified comment vs code. (2) Inside the fenced
production sources, every remaining EXACT `SendKind::dm` / `SendKind::channel_canned` spelling (the family helpers
replace them wherever a written kind can reach), each with QA's reachability classification. Read-only.
Usage: qa_reader_audit.py <out.json>."""
import json, re, subprocess, sys
from pathlib import Path

ROOT = Path("/home/staszek/MeshRoute")
FENCE = ("src/firmware_ui_editor.h", "src/firmware_ui_model.h", "src/firmware_ui_send.h", "src/firmware_ui_chrome.h",
         "src/firmware_ui.cpp", "test/test_firmware_ui_editor.cpp", "test/test_firmware_ui_model.cpp",
         "test/test_firmware_ui_send.cpp", "test/test_firmware_ui_chrome.cpp", "test/test_firmware_ui_presets.cpp",
         "tools/probe_firmware_ui/", "tools/probe_board_ui/", "tools/probe_ui_model_mutations.py",
         "tools/probe_board_abi.py")
SYMS = ["SendKind", "SendReq", "HomeView", "ComposeRow", "ReviewPhase", "send_gate_of", "SendLive", "SendTracker",
        "ui_nav_slot", "DmState", "ChanState", "EditorPhase", "draft_id", "compose_row_count", "compose_row_kind",
        "take_send_request", "normal_tracking_open", "ui_pump_trackers", "ui_perform_send", "ui_review_capture"]
files = subprocess.run(["git", "ls-files", "-co", "--exclude-standard", "lib", "src", "test", "tools"], cwd=ROOT,
                       check=True, stdout=subprocess.PIPE, text=True).stdout.split()
files = [f for f in files if f.endswith((".h", ".cpp", ".py", ".sh", ".tsv", ".c"))]
outside = {}
for s in SYMS:
    rx = re.compile(r"\b" + re.escape(s) + r"\b")
    for f in files:
        if f.startswith(FENCE):
            continue
        for i, line in enumerate((ROOT / f).read_text(errors="replace").splitlines(), 1):
            if rx.search(line):
                st = line.strip()
                kind = "comment" if st.startswith(("//", "#", "*", "/*")) else "code"
                outside.setdefault(s, []).append({"at": f"{f}:{i}", "kind": kind, "text": st[:160]})
# QA's classification of every remaining exact spelling in the fenced production sources, by its content.
EXACT = {
    "const mrfw::PresetKind want = (req.kind == SendKind::dm)": "phrase-only: after send_gate_of's written early return",
    "if (req.kind == SendKind::dm) {": "phrase-only: ui_compose_send_line, after the written branch returned",
    "} else if (req.kind == SendKind::channel_canned) {": "phrase-only: ui_compose_send_line, after the written branch",
    "b.peer_known = (b.kind == SendKind::dm) && live.peer_found;": "phrase-only: ui_review_capture, after the written branch",
    "if (k == SendKind::dm)                   _dm   = DmState::preset_changed;": "phrase-only: preset_changed is never a written answer",
    "else if (k == SendKind::channel_canned)  _chan = ChanState::preset_changed;": "phrase-only: preset_changed is never a written answer",
    "_review.peer_known = (_review.kind == SendKind::dm) && peer_known;": "phrase-only: on_review_captured (phrase capture)",
    "open_review(dm ? SendKind::dm : SendKind::channel_canned, compose_row_slot(_st.cursor, list), s);": "phrase-only: a phrase row opens its review",
    "const bool dm = (_review.kind == SendKind::dm);": "phrase-only: review_close_with (phrase review)",
    "if (_review.kind == SendKind::dm) {": "phrase-only: review_check after the written early return",
    "if (_review.kind == SendKind::dm && _review.peer_known && hash != _review.peer_hash) {": "phrase-only: review_check after the written early return",
    "case SendKind::emergency: case SendKind::dm: case SendKind::channel_canned: return false;": "the family helper itself (send_kind_written)",
    "case SendKind::dm: case SendKind::dm_text: return true;": "the family helper itself (send_kind_dm)",
    "case SendKind::emergency: case SendKind::channel_canned: case SendKind::channel_text: return false;": "the family helper itself (send_kind_dm)",
    "case SendKind::channel_canned: case SendKind::channel_text: return true;": "the family helper itself (send_kind_team_post)",
    "case SendKind::emergency: case SendKind::dm: case SendKind::dm_text: return false;": "the family helper itself (send_kind_team_post)",
    "if (!s_tracker_normal.idle() && s_tracker_normal.kind() != mrui::SendKind::dm) s_tracker_normal.close();":
        "B501: NOT phrase-only — the alarm drain treats a dm_text transaction as a channel one; equivalent in every "
        "reachable flow (see the receipt), registered LOW/LATENT",
}
inside = []
for f in ("src/firmware_ui_model.h", "src/firmware_ui_send.h", "src/firmware_ui.cpp", "src/firmware_ui_chrome.h"):
    for i, line in enumerate((ROOT / f).read_text().splitlines(), 1):
        st = line.strip()
        if st.startswith("//"):
            continue
        if re.search(r"SendKind::dm\b(?!_)", line) or "SendKind::channel_canned" in line:
            if re.search(r"case SendKind::(dm|channel_canned):", st) and "return false" not in st and "return true" not in st:
                continue
            code = st.split("//")[0].strip()
            cls = next((v for k, v in EXACT.items() if code.startswith(k) or k in code), None)
            inside.append({"at": f"{f}:{i}", "code": code[:200], "classification": cls})
unclassified = [x for x in inside if x["classification"] is None]
code_outside = {s: [x for x in v if x["kind"] == "code"] for s, v in outside.items()}
out = {"outside_fence": outside, "outside_fence_code_lines": code_outside, "inside_exact_spellings": inside,
       "unclassified": unclassified}
json.dump(out, open(sys.argv[1], "w"), indent=1)
print("outside-fence code readers:", {s: [x["at"] for x in v] for s, v in code_outside.items() if v})
print("inside exact spellings:", len(inside), "unclassified:", [x["at"] + " " + x["code"] for x in unclassified])
