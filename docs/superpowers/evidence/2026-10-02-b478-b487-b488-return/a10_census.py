# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# B492 / A10 proof 2 — every `ui_fmt_identity` caller in src/ and test/, following the wrappers (`label_from_hash`,
# `label_for_team_id`, `label_for_origin`) to their callers: the physical buffer extent, the capacity passed and the
# budget. Rows are keyed by CALL TEXT (pin by symbol; lines are hints); every row's needle must occur in its file the
# stated number of times, and every grep hit must belong to a row — a new or vanished caller fails this script.
# Constants are read from source, never typed here. Usage: python3 -B a10_census.py <out.json>
import json, os, re, sys
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
src = lambda p: open(os.path.join(ROOT, p), encoding="utf-8").read()
def const(path, name):
    m = re.search(r"constexpr[^=;]*\b%s\b\s*=\s*([^;]+);" % name, src(path))
    return m.group(1).strip()
K = {"kLabelCap": int(const("src/firmware_ui_model.h", "kLabelCap")),
     "kReviewLabelCols": int(const("src/firmware_ui_send.h", "kReviewLabelCols")),
     "kTeamLabelCols": int(const("src/firmware_ui_team.h", "kTeamLabelCols")),
     "kInviteNameCap": int(const("src/firmware_ui_invite.h", "kInviteNameCap")),
     "kInviteRowNameCols": int(const("src/firmware_ui_invite.h", "kInviteRowNameCols")),
     "kHomeNameCols": int(const("src/firmware_ui_status.h", "kHomeNameCols")),
     "kBodyCols": int(const("src/firmware_ui.cpp", "kBodyCols"))}
prefix = re.search(r'kComposeToPrefix\[\]\s*=\s*"([^"]*)"', src("src/firmware_ui.cpp")).group(1)
# derived exactly as src/firmware_ui.cpp derives them (each definition is checked below by needle)
K.update(kTeamNameCols=K["kTeamLabelCols"], kReplyWhoCols=K["kLabelCap"], kInviteNameCols=K["kInviteNameCap"] - 1,
         kInviteCandCols=K["kInviteRowNameCols"], kComposeToCols=K["kBodyCols"] - len(prefix), kDeliveredCols=K["kBodyCols"])
DERIVED = {"src/firmware_ui.cpp": ["constexpr uint8_t kTeamNameCols   = uint8_t(mrui::kTeamLabelCols);",
                                   "constexpr uint8_t kReplyWhoCols   = mrui::kLabelCap;",
                                   "constexpr uint8_t kInviteNameCols = uint8_t(mrui::kInviteNameCap - 1);",
                                   "constexpr uint8_t kInviteCandCols = mrui::kInviteRowNameCols;",
                                   "constexpr uint8_t kComposeToCols     = uint8_t(kBodyCols - int(sizeof kComposeToPrefix - 1));",
                                   "constexpr uint8_t kDeliveredCols     = uint8_t(kBodyCols);"],
           "src/firmware_ui_model.h": ["char     label[kLabelCap + 1] = {};   // `ui_fmt_identity` at the TEAM budget (6)"],
           "src/firmware_ui_invite.h": ["    char     name[kInviteNameCap] = {};    // the cached name as `ui_fmt_identity` formats it at 14"]}
L, C = K["kLabelCap"], K["kInviteNameCap"]
# (site, file, needle, occurrences, physical extent, capacity passed, budget) — None = forwarded / not applicable
ROWS = [
 ("ui_review_header — `TO <label>`", "src/firmware_ui_send.h", "ui_fmt_identity(label, sizeof label, name, name_len, b.peer_hash, kReviewLabelCols)", 1, K["kReviewLabelCols"] + 1, K["kReviewLabelCols"] + 1, K["kReviewLabelCols"]),
 ("ui_home_me_line — Home row 0", "src/firmware_ui_status.h", "ui_fmt_identity(id, sizeof id, s.own_name, s.own_name_len, s.my_key_hash32, kHomeNameCols)", 1, K["kHomeNameCols"] + 1, K["kHomeNameCols"] + 1, K["kHomeNameCols"]),
 ("label_from_hash (wrapper: forwards its caller's out/cap/cols)", "src/firmware_ui.cpp", "(void)mrui::ui_fmt_identity(out, cap, raw, n, hash, cols)", 1, None, None, None),
 ("invite NEW MEMBER carrier (InviteMember::name)", "src/firmware_ui.cpp", "ui_fmt_identity(mem.name, sizeof mem.name, raw, nn, hash, kInviteNameCols)", 1, C, C, K["kInviteNameCols"]),
 ("invite candidate row (name6)", "src/firmware_ui.cpp", "(void)mrui::ui_fmt_identity(name6, sizeof name6, r.cand.name, uint8_t(strlen(r.cand.name)), 0,", 1, K["kInviteCandCols"] + 1, K["kInviteCandCols"] + 1, K["kInviteCandCols"]),
 ("label_for_team_id -> label_from_hash (forwards)", "src/firmware_ui.cpp", "label_from_hash(hash, out, cap, cols); return hash;", 1, None, None, None),
 ("label_for_origin -> label_from_hash (forwards)", "src/firmware_ui.cpp", "label_from_hash(pu.sender_hash, out, cap, cols); return;", 1, None, None, None),
 ("TEAM row (TeamRow::label) via label_for_team_id", "src/firmware_ui.cpp", "label_for_team_id(r.id, r.label, uint8_t(sizeof r.label), kTeamNameCols)", 1, L + 1, L + 1, K["kTeamNameCols"]),
 ("DELIVERED via label_for_team_id", "src/firmware_ui.cpp", "char label[kDeliveredCols + 1]; label_for_team_id(st.compose_peer, label, uint8_t(sizeof label), kDeliveredCols)", 1, K["kDeliveredCols"] + 1, K["kDeliveredCols"] + 1, K["kDeliveredCols"]),
 ("compose TO via label_for_team_id", "src/firmware_ui.cpp", "char label[kComposeToCols + 1]; label_for_team_id(st.compose_peer, label, uint8_t(sizeof label), kComposeToCols)", 1, K["kComposeToCols"] + 1, K["kComposeToCols"] + 1, K["kComposeToCols"]),
 ("REPLY sender via label_for_origin", "src/firmware_ui.cpp", "char who[mrui::kLabelCap + 1]; label_for_origin(pu, who, uint8_t(sizeof who), kReplyWhoCols)", 1, L + 1, L + 1, K["kReplyWhoCols"]),
 ("native: id_fmt helper (IdBuf::out[48], guarded; per-call caps below)", "test/test_firmware_ui_model.cpp", "return ui_fmt_identity(b.out, cap, bytes, len, hash, cols);", 1, None, None, None),
 ("native: null destination", "test/test_firmware_ui_model.cpp", 'ui_fmt_identity(nullptr, 20, "Wolf", 4, 0, 6)', 1, 0, 20, 6),
 ("native: two-pass a6 (one spare byte)", "test/test_firmware_ui_model.cpp", "ui_fmt_identity(a6, 7, name, len, 0, 6)", 1, 8, 7, 6),
 ("native: two-pass c14 (one spare byte)", "test/test_firmware_ui_model.cpp", "ui_fmt_identity(c14, 15, name, len, 0, 14)", 1, 16, 15, 14),
 ("native: two-pass b6 (one spare byte)", "test/test_firmware_ui_model.cpp", "ui_fmt_identity(b6, 7, c14, uint8_t(std::strlen(c14)), 0, 6)", 2, 8, 7, 6),
 ("native: marker survives the second pass, c14", "test/test_firmware_ui_model.cpp", 'ui_fmt_identity(c14, 15, "Wolfgangetta-the-longest", 24, 0, 14)', 1, 16, 15, 14),
 ("native: preformatted REPLY sender (one spare byte)", "test/test_firmware_ui_model.cpp", 'ui_fmt_identity(who, kLabelCap + 1, "Wolfgangetta-the-longest", 24, 0, kLabelCap)', 1, L + 2, L + 1, L),
 ("native: w6-review member token", "test/test_firmware_ui_model.cpp", "ui_fmt_identity(tok, sizeof tok, nullptr, 0, 0xA0000011u, 7)", 1, 8, 8, 7),
 ("native: projected TEAM row (TeamRow::label)", "test/test_firmware_ui_team.cpp", "(void)ui_fmt_identity(t.label, sizeof t.label, name, uint8_t(name ? std::strlen(name) : 0), key_hash32,", 1, L + 1, L + 1, K["kTeamLabelCols"]),
 ("native: invite cand_row name6 (one spare byte)", "test/test_firmware_ui_invite.cpp", "(void)mrui::ui_fmt_identity(name6, mrui::kInviteRowNameCols + 1u, m.name, uint8_t(strlen(m.name)), 0,", 1, K["kInviteRowNameCols"] + 2, K["kInviteRowNameCols"] + 1, K["kInviteRowNameCols"]),
 ("native: invite high-byte name (InviteMember::name)", "test/test_firmware_ui_invite.cpp", "mrui::ui_fmt_identity(hb.name, sizeof hb.name, raw, 4, hb.key_hash32, uint8_t(mrui::kInviteNameCap - 1))", 1, C, C, C - 1),
 ("native: invite live[0] longest name", "test/test_firmware_ui_invite.cpp", "mrui::ui_fmt_identity(live[0].name, sizeof live[0].name, longest, uint8_t(strlen(longest)),", 1, C, C, C - 1),
 ("native: invite live[1] high bytes", "test/test_firmware_ui_invite.cpp", "mrui::ui_fmt_identity(live[1].name, sizeof live[1].name, hi, 4, live[1].key_hash32,", 1, C, C, C - 1),
]
problems, rows = [], []
for path, needles in DERIVED.items():
    problems += [f"{path}: derivation/declaration not found: {n[:70]}" for n in needles if n not in src(path)]
for site, path, needle, occ, ext, cap, cols in ROWS:
    n = src(path).count(needle)
    if n != occ: problems.append(f"{site}: needle occurs {n} times in {path}, expected {occ}")
    cls = None if cap is None else ("null destination" if ext == 0 else "SHORT" if cap < cols + 1 else
                                    "exact (cap == cols + 1): new F07 inert" if cap == cols + 1 else
                                    "ROOMY (cap >= cols + 2): new F07 ACTIVE")
    if ext and cap and cap > ext: problems.append(f"{site}: capacity {cap} exceeds the physical extent {ext}")
    rows.append(dict(site=site, file=path, call=needle, occurrences=occ, extent=ext, capacity=cap, budget=cols, cls=cls))
# Completeness: every non-comment line naming the formatter or a wrapper belongs to a row (or is a definition).
covered = {(r["file"], r["call"]) for r in rows}
uncovered = []
for d in ("src", "test"):
    for dp, _, fs in os.walk(os.path.join(ROOT, d)):
        for f in fs:
            rel = os.path.relpath(os.path.join(dp, f), ROOT)
            for i, line in enumerate(open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace"), 1):
                code = line.split("//")[0]
                if not re.search(r"\b(ui_fmt_identity|label_from_hash|label_for_team_id|label_for_origin)\s*\(", code):
                    continue
                if re.search(r"^\s*(inline IdentityFmt ui_fmt_identity|void label_from_hash|uint32_t label_for_team_id|void label_for_origin|IdentityFmt id_fmt)\b", code):
                    continue                                    # the definitions themselves
                if re.search(r"^\s*(void|uint32_t|IdentityFmt)\s+\w+\(.*\);\s*$", code):
                    continue                                    # a prototype
                if not any(f_ == rel and c_ in line for f_, c_ in covered):
                    uncovered.append(f"{rel}:{i}: {line.strip()[:120]}")
problems += [f"UNCOVERED caller {u}" for u in uncovered]
# The native suite's literal id_fmt calls (IdBuf::out[48]): classified by the capacity and budget each passes.
model = src("test/test_firmware_ui_model.cpp")
idf = []
for m in re.finditer(r"id_fmt\(b, (\d+),\s*([^,]+), (\d+), ([^,]+), (\w+)\)", model):
    cap, ln, cols = int(m.group(1)), int(m.group(3)), m.group(5)
    line = model.count("\n", 0, m.start()) + 1
    if cols.isdigit():
        c = int(cols)
        cls = "SHORT (deliberate no_fit)" if cap < c + 1 else "exact" if cap == c + 1 else "ROOMY"
    else:
        cls = f"variable budget `{cols}` (no name: the F07 line is never reached)"
    idf.append(dict(line_hint=line, cap=cap, name_len=ln, budget=cols, cls=cls,
                    f07_reachable=cls == "ROOMY" and ln > (int(cols) if cols.isdigit() else 99)))
summary = {"constants": dict(K, kComposeToPrefix=prefix), "rows": rows, "native_id_fmt_calls": idf,
           "valid_callers_all_cap_ge_cols_plus_1": all(r["capacity"] >= r["budget"] + 1 for r in rows if r["capacity"] and r["extent"]),
           "active_sites_production": [r["site"] for r in rows if r["file"].startswith("src/") and r["cls"] and "ACTIVE" in r["cls"]],
           "active_sites_native": [r["site"] for r in rows if r["file"].startswith("test/") and r["cls"] and "ACTIVE" in r["cls"]]
                                  + [f"id_fmt(b, {x['cap']}, …, {x['name_len']}, …, {x['budget']}) ~:{x['line_hint']}" for x in idf if x["f07_reachable"]],
           "problems": problems, "verdict": "PASS" if not problems else "FAIL"}
open(sys.argv[1], "w").write(json.dumps(summary, indent=1) + "\n")
for r in rows:
    print(f"  {r['file'].split('/')[-1]:28s} ext={str(r['extent']):4s} cap={str(r['capacity']):4s} cols={str(r['budget']):4s} {r['cls'] or 'forwards'} — {r['site']}")
from collections import Counter
print("  native id_fmt literal calls:", dict(Counter(x["cls"] for x in idf)), "| F07-reachable (roomy, name longer than budget):",
      sum(x["f07_reachable"] for x in idf))
print("  active in production:", summary["active_sites_production"])
print("  valid callers all cap >= cols + 1:", summary["valid_callers_all_cap_ge_cols_plus_1"], "| problems:", problems)
print("CENSUS", summary["verdict"])
