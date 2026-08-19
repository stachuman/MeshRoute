<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-15 slice 3 — pure team-id fingerprint helper · dispatch brief · 2026-08-19

**Status: DISPATCHED.** ⛔ **NO DEVICE CONTACT.** ⛔ Build on the uncommitted UI-15 slice 1+2 tree (QG-passed
2026-08-19); do not revert or re-derive any of it. Normative spec: the UI-15 plan
(`docs/superpowers/specs/2026-08-18-ui15-provisioning-implementation-plan.md`) **§7**, slice table §9 row 3, and the
design doc `docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md` §3.6.4 (the future consumer). ⚠ The plan
keeps withdrawn wording visible; **the latest correction always wins.**

---
## The slice — ONE pure function, nothing else

★ **EXACTLY (plan §7, owner-adopted): the fingerprint is the uppercase, zero-padded hex of `team_id & 0x00FFFFFF` —
six characters.** Example: `0x12A1B2C3` → **`A1B2C3`**.

- ⛔ v3's *"six digits derived from the id"* was rejected as under-specified precisely because two independent
  implementations could disagree (high-bits / low-bits / hash). **This helper is THE single shared definition** —
  slices 5/6 and §3.6.4's nearby-join list will call it later so the invite screen and the joiner's list can never
  disagree. ⛔ **No call sites are wired in this slice** ("shared with §3.6.4 later" — the screens are slices 5/6);
  say so in-source: what it is for, who will consume it, and why it is currently uncalled (the done-vs-missing rule).
- ★ **DISPLAY-ONLY.** The retained full `team_id` remains the selection authority; the fingerprint must never make a
  routing, selection or airtime decision — the standing rule ([[B210]]/[[B214]]: a display-shaped field must never
  make an airtime decision; §3.6.4 point 3: confirmation selects the exact full id, never the fingerprint).
- ⛔ **ZERO wire bytes.** Derived locally from the id beacons already carry.
- ⛔ **No special cases, no defaults** (C2): the helper formats ANY `uint32_t` (0 → `000000`); whether a fingerprint
  is RENDERED for an id of 0 is the caller's decision in later slices, not this function's.
- ⛔ Two ids differing only in the top byte produce the SAME fingerprint — **by design** (the mask is the spec).
  Assert it as a positive property, do not "fix" it.

## Placement + reuse (verified 2026-08-19)

- **Home: `src/firmware_ui_chrome.h`, namespace `mrui`** — the pure, Arduino-free display formatters live there
  (`:104`, `:143` show the `snprintf(out, cap, …)` house idiom), it is native-tested
  (`test/test_firmware_ui_chrome.cpp`), and the mutation harness already has the **`chrome` target**
  (`tools/probe_ui_model_mutations.py:58`). ⛔ Stop and report if you find a reason this home is wrong — do not pick
  another silently.
- **U1, verified: no existing fingerprint helper exists.** The `fingerprint` grep hits in `src/` are unrelated
  (the §3.6.1 config-baseline `CfgValues` comparison; the admin-pubkey 4-byte fp print). The existing full-id
  `"%08lX"` sites (`firmware_commands.cpp:217/292`, `firmware_ui.cpp:892`, …) render a DIFFERENT token — the full
  id — ⛔ leave every one of them untouched; do not fold them into the helper.
- Match the surrounding chrome idiom for the signature (dest buffer + cap, like the neighbours) and the file's
  comment style. Output is exactly six characters + NUL.

## Pins

1. **The spec example verbatim:** `0x12A1B2C3` → `A1B2C3`.
2. **Zero-padding:** e.g. `0x00000001` → `000001`; `0` → `000000`.
3. **The mask:** two ids differing only in bits 24-31 fingerprint identically; bits 0-23 all reach the output.
4. **Uppercase**, exact length, NUL-terminated; cap handling per the chrome idiom.
5. **Non-vacuous, via the `chrome` battery:** defect-specific mutations go RED at match count 1 — at minimum
   (a) mask dropped (high byte leaks into the token), (b) width/zero-padding dropped, (c) lowercase. Unchanged
   positive controls stay GREEN (⛔ never "everything must go red").

ⓘ ★★ **[[B217]] STANDS: re-pin `BASE_CASES`/`BASE_ASSERTS`** in `tools/probe_ui_model_mutations.py` when your new
cases move the native counts (current pin **1764 / 84729**, `:83`), record the derivation in place as the prior
re-pins did, and **confirm the battery actually RAN** — an aborted battery prints no RED lines. Restore mutation
sources exactly; `git diff --check` clean.

## Verification you run (QG runs the full gate separately — no boards, no corpus)

1. `pio test -e native`, **then RUN `./.pio/build/native/program`** (the wrapper lies "0 test cases") — report the
   binary's real counts, 0 failed.
2. The **`chrome`** mutation battery — RED count + proof it ran. (The other targets are untouched by this slice; QG
   re-runs them.)
3. `git diff --check` clean.

## Report

The helper's exact signature and location · each pin with its test-case name · each mutation with its match count ·
final native counts and the new `BASE_CASES`/`BASE_ASSERTS` pin · proof the battery ran · exact final
`git status --short`.

⛔ **NEVER `git commit` / `git add` / `git checkout --`.** ⛔ Do not touch the bug register, the bench script, any
plan/brief, `tracker.md`, `B164.md` or `docs/manual/`. ⛔ Evidence lands IN THE REPO. ⛔ C1: this slice is the one
pure function + its tests + battery entries — no refactors, no screen work, no other files.
