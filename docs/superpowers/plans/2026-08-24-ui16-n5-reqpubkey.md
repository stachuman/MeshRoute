<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-16 slice 6 — the explicit `REQUEST PUBKEY` step (N5) · dispatch brief · 2026-08-24

**Status: DISPATCHED. The APPROVED spec is the AUTHORITY:**
`docs/superpowers/specs/2026-08-22-ui16-nearby-onboarding-spec.md` **§N5 (:771-806)** — read it IN FULL; this
brief quotes its load-bearing rules but the SPEC governs on any divergence. Also read the §8 entries
**S-18/S-19/S-20/S-21** and forbidden **S-34**, and the §3 rows P-7c/P-7d. ⛔ **NO DEVICE CONTACT.**

## Scope (spec :773-786)
The `no_pubkey` landing becomes its own confirmation: **`NEED PUBKEY`** with **`BACK` selected** and the action
**`REQUEST PUBKEY`**; confirming issues the **existing** `CmdKind::reqpubkey` on **`Plane::TEAM`** for the
candidate's hash; the screen then shows **`WAITING FOR PUBKEY`** until a **`peer_key_cached`** for that hash
enables `GRANT KEY`. ⛔ **No new verb, no new frame, no new wire byte, no automatic escalation.**
Files: `src/firmware_ui_invite.h` (the arm + the words, pure) · `src/firmware_ui_model.h` (the arm) · a device
forward for the command (⛔ the decision is not in the device TU).

## ★★★ The ruling this slice must PRESERVE (spec :777-781)
§no-auto-reqpubkey (`lib/core/node.cpp:185-196`, owner-ratified 2026-07-29, canonical home `Node::send_by_hash`'s
header) forbids the grant **silently** escalating to a WANT_PUBKEY. Here the **operator** authorises the on-air
identity request with a deliberate `short` + `double` — exactly what typing `reqpubkey <hash> -t` at the console
is, and the console is the remedy the ban's own text names. ⛔ **A slice that auto-issues it on entering the row
IS reversing the ruling.** Verify the ban's text at the tree before writing the arm (V2).

## Lexemes (spec :782-784 — ruled, one forbidden)
`NEED PUBKEY` (S-18) · `REQUEST PUBKEY` (S-19) · `WAITING FOR PUBKEY` (S-20/S-21 slots) — and ⛔ **never
`WAITING FOR KEY`** (S-34): it is ambiguous between the recipient's **pubkey** and the team **content key**, the
two different secrets this very screen sits between. Absence is a test.

## Pins (spec :787-799, verbatim — every one a case)
(1) ★ **No WANT_PUBKEY is emitted without the operator's `double` on `REQUEST PUBKEY`** — driven by counting
emitted commands, ⛔ not by reading a screen. (2) `BACK` is selected initially and `BACK` emits nothing. (3) The
request is **team-scoped** (`Plane::TEAM`), matching the plane the grant will fly on. (4) A `peer_key_cached`
**for that hash** enables `GRANT KEY`; one for a **different** hash ⛔ does not. (5) A timeout leaves
`WAITING FOR PUBKEY` and ⛔ grants nothing. (6) ⛔ The word `WAITING FOR KEY` appears nowhere in the tree on this
path. (7) ★ after the request **succeeds for that hash**, the candidate's row **fills its name column** from the
cache — the answer carried the name (`node_hashlocate.cpp:1251`) and `peer_key_set` cached it beside the key
(`:1142`, `:377`); ⛔ **no extra lookup, no second request, no new field** — render what the exchange already
stored. (8) A request that **fails or times out** leaves the name column **blank** and the fingerprint intact.
(9) ⛔ **The name never gates anything:** `GRANT KEY` is enabled by the **key**, ⛔ never by the presence of a
name. (10) The confirmation carries the **full hash** throughout, name or no name (P-7c).
ⓘ N5 ends at ENABLING `GRANT KEY` — the grant act, its outcome mapping and `send_aired` correlation are N6's
(spec :808-849). If the enable/act boundary is ambiguous anywhere, STOP and report.

## Mutations (spec :800-806 — `uiinvite` + `model`, each RED at match count 1)
★ **the request auto-issued on entering the row** (the headline control — the ruling-reversal shape) · the
confirmation defaulting to `REQUEST PUBKEY` · the plane changed to `GLOBAL`/`AUTO` · **any** `peer_key_cached`
enabling the grant (hash not compared) · the timeout enabling the grant · the request target derived from the
**display name** rather than the hash (P-7d) · the name column filled from **any** `peer_key_cached` rather than
the one for that hash (⇒ one member wearing another's name) · **the presence of a name enabling `GRANT KEY`**
(⇒ a describe-only field making an airtime-and-secret decision, the [[B48]] class).

## Operational contract (standing)
- C1: N5 only. Everything landed survives (S1-S8, K1/K2, N1-N4 + their QG fixes, §CHROME-5 — batteries, probes,
  UI-17 contract mutations). The invite window's zero-TX rule NARROWS here, deliberately: the ONE transmission
  this flow may cause is the operator-confirmed `reqpubkey` — everything else (open/hold/refresh/BACK/timeout)
  still transmits nothing, and the pin-1 command count proves both halves.
- Handoff seam WITH the unit: the confirmed request driven through the REAL device forward / command sink on the
  probe (count the emitted command there too), plus the `peer_key_cached` arrival arm (right hash vs wrong hash)
  and the name-fill-on-success arm; controls RED.
- Batteries: iterate; ONE full pass per touched target; sync `PIN_*` with derivation; exit 6/7/8 = fix the
  invocation; never probe+battery concurrently; ⛔ NO background poller shells that can outlive your final report
  — foreground waits on runner output markers only (a poller storm followed the last slice); ⛔ never edit a
  battery's target while its battery runs.
- Resources: measure any `UiState`/`UiSnapshot`/`InviteWindow` growth (offsetof-prove placements).
- Verification you run (QG runs boards/corpus — N5 is `src/`-only, s18-inert by construction; say so): native
  (RUN the binary) · touched-target batteries · probe both arms all controls · `git diff --check` clean · your
  diff stays `src/` + `test/` + `tools/`.
- ⛔ NEVER `git commit` / `git add` / `git checkout --`. ⛔ Do not touch the register, the bench script, any
  plan/brief, either UI spec, the design doc, `tracker.md`, `docs/manual/`, anything under `lib/`, or the
  parallel-session files. The metal residue (a real over-the-air pubkey exchange, spec §7's steps): DRAFT in
  your report.

## Report
Every pin (1-10) with case name and match count · the command-count proofs (zero without the double, one with,
team plane) · the right-hash/wrong-hash and name-fill arms · the mutation ledger + full-pass proofs · native
counts + cross-check with derivation · probe proofs · measured resources · the DRAFTED bench residue · exact
final `git status --short`.
