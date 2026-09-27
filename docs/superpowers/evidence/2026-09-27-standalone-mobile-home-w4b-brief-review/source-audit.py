#!/usr/bin/env python3
"""Read-only W4b-r1 review witnesses; no builds or repository edits."""
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]


def read(path):
    return (ROOT / path).read_text()


def witness(path, text):
    body = read(path)
    assert body.count(text) == 1, (path, text, body.count(text))
    return {"path": path, "line": body[:body.index(text)].count("\n") + 1,
            "text": text, "matches": 1}


ui_path = "src/firmware_ui.cpp"
model_path = "src/firmware_ui_model.h"
runner_path = "tools/probe_firmware_ui/run.sh"
ui = read(ui_path)
runner = read(runner_path)
controls = {}
for label in ("C102", "C103", "C104", "C108", "C123", "C124"):
    pattern = r'^  ctl "(' + label + r' [^"\n]+)" (yes|no) \\\n\s*\x27([^\x27]*)\x27'
    matches = list(re.finditer(pattern, runner, re.M))
    assert len(matches) == 1, label
    match = matches[0]
    name, must_build, script = match.groups()
    mutated = subprocess.run(["sed", "-e", script], input=ui, text=True,
                             capture_output=True, check=True).stdout
    assert mutated != ui, label
    controls[label] = {"label": name, "must_build": must_build,
                       "line": runner[:match.start()].count("\n") + 1,
                       "script": script, "changes_current_source": True}

mark = "    mrui::draw_bitmap(kStatusMarkX, kStatusMarkY, kStatusMarkW, kStatusMarkH, mrui::icons::kMarkMeshRoute);"
assert ui.count(mark) == 1
without_mark = ui.replace(mark, "    // Review-only counterfactual: Home mark call removed.")
control_after = subprocess.run(["sed", "-e", controls["C123"]["script"]],
                               input=without_mark, text=True, capture_output=True,
                               check=True).stdout
assert control_after == without_mark
controls["C123"]["after_removing_only_home_bitmap_call_changes_source"] = False
controls["C108"]["replacement_dependency"] = "status_text(row, l)"
controls["C124"]["replacement_dependencies"] = ["kStatusMarkX", "kStatusMarkY", "kStatusMarkW", "kStatusMarkH"]

source = [
    witness(model_path, "if (_st.provisioning == Provision::saved_key) enter_provision(Provision::menu);"),
    witness(model_path, "case ProvRow::back:        close_provisioning(); return;"),
    witness(model_path, "void close_provisioning() {\n        _st.settings = Settings::browsing;"),
    witness(ui_path, "const mrui::UiSnapshot s = build_snapshot(now_ms);"),
    witness(ui_path, "s_frame_snap  = s;"),
    witness(ui_path, "draw_frame(s_frame_state, s_frame_snap, s_frame_out, s_frame_cfg, s_frame_chrome);"),
    witness(ui_path, "s.own_fix              = mrui::ui_status_have_fix(own_cfg.lat_e7, own_cfg.lon_e7);"),
    witness("platformio.ini", "test_build_src = no"),
    witness("tools/probe_board_ui/run.sh", "code_flat \"$1\" | grep -qF 'if (c.nav == s) mrui::draw_rect(kRailX, y, kRailW, kRailH);'; }"),
    witness("docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md",
            "**Apart from the catalog estimate the owner accepted with D7, no\nallocation is granted by this document.**"),
]
paths = sorted({r["path"] for r in source} | {runner_path})
print(json.dumps({
    "scope": "Source and sed-anchor witnesses only; not a runtime or mutant gate",
    "inputs": {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in paths},
    "controls": controls, "source": source,
    "counterfactual": "One required bitmap-call deletion in an in-memory string; never written to production",
}, indent=2) + "\n", end="")
