<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-16 slice 10 — saved-key lifecycle / `FORGET KEY` (K6) · dispatch brief · 2026-08-25

**Status: ✅ COMPLETE — QG-PASSED 2026-08-25 (combined K6+K7 gate; three rounds: base + the typed
`keyring_full` carrier + the drain-fixture integrity round). Metal residue = bench Part 44.**
**The APPROVED spec is the AUTHORITY:** `docs/superpowers/specs/2026-08-22-ui16-nearby-onboarding-spec.md`
**§K6 (:955-1009)** — owner-ruled 2026-08-25 after metal testing filled all four `/mrteams` records. Read it IN
FULL; it is complete (problem, policy, service, console, OLED, pins 1-9, mutation classes, boundary). Also:
§8 entries **S-31 (now LIVE) / S-40..S-44** and the K1 policy block this COMPLETES rather than corrects (the
loud `KEYRING FULL`, no-silent-eviction). ⛔ **NO DEVICE CONTACT.**

## The section's load-bearing rules (quoted — the spec governs)
- **Saved-key retention management, ⛔ not key rotation.** Fixed four-record bound; ⛔ no record growth, ⛔ no
  automatic FIFO/LRU; a full store performs zero writes and zero eviction until the operator selects a
  SPECIFIC inactive record and confirms. **The active key is protected and cannot be removed.**
- **Two explicit transactions, never one disguised one:** removal completes and reports its own verdict; the
  create/grant is then retried BY THE OPERATOR. ⛔ `team new` never deletes as a side effect — "evict then
  create" cannot be one atomic commit across two durable records, and hiding both behind one action lets a
  failed create destroy an unrelated saved key.
- **Pure service:** metadata-only enumeration (`team_id`, active marker — ⛔ never key bytes) + typed
  `forget(team_id, binding)`: refuse id 0 · fail closed on invalid/unreadable storage · not-found = zero
  writes · active = refused, zero writes · remove exactly the selected record · compact deterministically ·
  **wipe the vacated record** · save exactly once · a failed save reported as a failed save (⛔ never "nothing
  changed" — the real backend's power-cut outcome is M2's).
- **Console:** `team keys` (list, active marked, no material) · `team forgetkey 0x<team-id> confirm` (one
  inactive record; missing confirm / active target / id 0 / absent record = zero writes, loud). ⛔
  `team exportkey` is not reused as the list.
- **OLED:** PROVISION gains **`SAVED KEYS`** (S-40); rows = shared fingerprint helper + **`ACTIVE`** marker
  (S-44); selection carried by the FULL 32-bit `team_id` (⛔ never the six-hex token or a row index); the
  irreversible confirmation shows the full id, `BACK` selected, `FORGET KEY` (S-31) only for an inactive row;
  active row ⇒ **`ACTIVE KEY` / `CANNOT FORGET`** (S-43), no destructive action; empty ⇒ **`NO SAVED KEYS`**
  (S-41); success ⇒ **`KEY FORGOTTEN`** (S-42) + the refreshed list; a storage failure stays visible, no false
  success. **`KEYRING FULL`'s acknowledgement ENTERS the list** — it never chooses a victim, deletes, or
  replays the original act.
- **Hygiene:** the confirmation carries the full id (a short-fingerprint collision cannot delete the wrong
  record — pin 7); names are metadata, never deletion identity; no key byte anywhere (model, renderer,
  console list, outcome token, telemetry, mutation output); K1's wipe guard on every return.

## Pins (spec, verbatim — every one a case)
(1) full + unconfirmed ⇒ zero writes, no eviction. (2) active target ⇒ zero writes; live key, binding,
membership and ALL records unchanged. (3) inactive target ⇒ exactly one save, the selected id absent, every
other record byte-identical, the vacated tail ZERO. (4) not-found / zero / unreadable ⇒ zero writes. (5) a
save failure never renders `KEY FORGOTTEN`. (6) the list exposes ids/status only. (7) a short-fingerprint
collision cannot delete the wrong record (drive two teams sharing six hex digits). (8) `KEYRING FULL`'s ack
opens management but never deletes or retries by itself. (9) re-keying an existing team remains an in-place
replace and never invokes K6.

## Mutations (spec's classes — each RED at match count 1)
silent oldest-record eviction in `put` · active-key deletion accepted · confirmation bypassed · delete keyed
on the fingerprint/cursor · compaction leaving a duplicate or a secret-bearing tail · save failure rendered as
success · the create automatically resumed · the list returning key material · missing-store and
unreadable-store collapsed. Plus the sentinel/count fences on any new enum (the established pattern).

## Operational contract (standing)
- C1: K6 only. Everything landed survives (through K5 + the STATUS-landing tweak). Files per the spec's
  boundary: `src/firmware_team_keyring.h` (pure policy) · `src/firmware_config.{h,cpp}` forwards (⚠ that TU's
  first compile is QG's boards — say so) · UI model/renderer/prov adapter · tests · batteries/probes. ⛔ No
  `lib/` of any kind. ⛔ **AMENDED 2026-08-25 (round 2, QG-required — the withdrawn "no lib/" kept visible):**
  the typed `keyring_full` carrier required **`lib/hal/mr_ui.h:117`** (the hook's parameter) — the ONE
  permitted HAL declaration/stub site per the spec §0 amended boundary. Measured simulator fact, stated at the
  gate: **`lib/hal/mr_ui.h` is not compiled into `lus`** (zero `lib/hal` references in the sim's explicit
  source list; the header is included only by `src/`/`tools/` TUs) ⇒ **s18 remains exact**.
- Batteries (parallel runner, per its header): iterate; ONE full pass per touched target; sync `PIN_*` with
  derivation; ⛔ never pipe; ⛔ no pollers; ⛔ never edit a target mid-run.
- Handoff seam WITH the unit: the probe drives list → confirm → forget through the REAL services (a real
  4-record store), incl. the active-row refusal and the KEYRING FULL → list entry; controls RED.
- Verification you run (QG runs boards): native (RUN the binary) · touched-target batteries · both probes
  green · `git diff --check` clean · ⛔ **corrected 2026-08-25: this line demanded "`git diff -- lib/` EMPTY;
  `src/`-only ⇒ s18-inert by construction" — round 2's boundary amendment supersedes it:**
  `git diff --stat -- lib/` = **exactly `lib/hal/mr_ui.h`** and nothing else, with the not-compiled-into-`lus`
  fact stated ⇒ s18 exact.
- ⛔ NEVER `git commit` / `git add` / `git checkout --`. ⛔ No docs/plans/specs/register/bench/tracker/
  platformio.ini/ccache_native.py/parallel-session files. Metal residue (the real-flash forget + power-cut
  mid-forget — [[B193]]'s class over a compacting write — and the on-glass KEYRING FULL → SAVED KEYS walk):
  DRAFT in your report.

## Report
Every pin (1-9) with case name and match count · the compaction/wipe proofs (byte-level) · the two-transaction
proof (`team new` never deletes) · the mutation ledger + full passes · native + PIN derivation · probe proofs ·
measured resources · the DRAFTED bench residue · exact final `git status --short`.
