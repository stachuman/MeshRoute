# W1 coder ledger — plan: docs/superpowers/plans/2026-09-24-standalone-mobile-home-w1-label-termination.md (sha 617c2be5…d018)
Preflight: PASS — HEAD 4a230f4, sim 6585649 clean, 5/5 exec inputs clean+match, 10/10 prep-set match, 21 pinned evidence files + 3 rereview-2 files verify; git status fully classified (logs/preflight.txt)
Baseline probe: l2 433/433, v3 868/868, BLE 433/433; controls 223 verified / 0 unusable (l2 144 incl C0, v3 79); md5 f87779aa…; checks/arm l2 404 v3 839; coverage 703/840
Baseline native: 2950 / 195770 / 0 failed / 0 skipped
Baseline boards: base-1/base-2 PASS both; compare PASS gateway + heltec_mobile
Baseline label-controls: N4 8, N9 8, O2 1, O6 1, O8 2 (all RED)
Incident: importing tools/probe_ui_model_mutations.py ran its top-level main (model battery, /tmp scratch copies only); stopped via TaskStop; scratch removed; tree verified clean (src diff clean, git status unchanged)
Selector (a): NONE (static AST census of 105 TARGET_SRC; logs/selector-a-census.txt)
Selector (b): uiteam(20) uiinvite(32) uisend(15); excluded model(239: no entry on reply/label path), b161hash/b251hash/grantpark (no entry on the name API)
Finding (pre-existing, not W1): O8's 2nd sed cmd `mem.name\[nn\] = .\\\\0.;` matches nothing -> mutant blanks the name (nn=0) instead of publishing the 0x spelling. Report to QA; not fixed (fence).
Impl: probe seam (run.sh ui_wrapper; build_variant + BLE arm), trampoline decl + P28 phase (probe_main.cpp), fix (firmware_ui.cpp label_from_hash), native guard (test_node_hashlocate.cpp), PIN 2951/195777
RED (pre-fix, new probe, --no-neg): P28a 8 checks + P28c named header/DELIVERED fail on all 3 arms (logs/probe-red-prefix.log)
GREEN (--no-neg): l2 463/463 v3 898/898 BLE 463/463
Native: 2951/195777/0/0 (+1 case/+7 asserts)
Native guard RED proof: scratch tree w/ global cap-1 in peer_name_find -> only B241 case fails (4 asserts, 31==32) of 2951 (logs/native-capm1-*)
Controls repro (scratch, same wrapper route): B241a RED 10 (P28a x8 + P28c named x2), B241b RED 5 (H1,H2,rename + P28c named x2); B241a fn byte-identical to base fn
Existing label controls N4/N9/O2/O6/O8: identical reddened label sets base vs W1 (logs/res-*)
Full probe iter: PASS, 225 verified/0 unusable, C0 fails as required, coverage 729/870; unreddened new: P28 precondition, P28a NAMELESS/UNKNOWN fallback, P28a cap0
Ruling: B241b replacement spelled `out[cap - 1] = 0;` (not '\0') — a single-quoted sed script cannot carry an apostrophe; semantics identical — cost if wrong: none (same byte written)
Ruling: native guard uses CHECK not REQUIRE — the native TU is -fno-exceptions (file's own note at the REQUIRE comment) — cost if wrong: none
Final review (fresh opus reviewer, read-only): 0 Critical; re-graded: #2 (unknown peer missing from TEAM/compose consumers) Important -> FIXED (teammate 94 on uncached hash 0xB2EE41EE; unknown moved off 0xB24100kk so its 6-col clamp differs); #3 (precondition could not tell nameless-cached from absent) Important -> FIXED (peer_key_find presence/absence); #4 comment V1 -> FIXED; #5 g_exec reset for P26 -> FIXED; #6 wrapper write fail-loud -> FIXED (refusal proven exit 1); #1 O8 vacuous 2nd sed -> pre-existing, REPORT to QA (fence); #7 informational
Post-fix GREEN (--no-neg): l2 467/467 v3 902/902 BLE 467/467; controls repro B241a RED 10, B241b RED 5 (unchanged)
FREEZE: inputs final; stability-start inventory = logs/stability-start.txt
