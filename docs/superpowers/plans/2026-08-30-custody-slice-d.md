<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §CUSTODY-D — inbox-only clear · dispatch brief · 2026-08-30

**Status: DRAFT — awaiting the Quality Agent's review of THIS BRIEF before dispatch (standing process; ⛔ no
implementation until it passes).** Authority: the custody spec
(`docs/superpowers/specs/2026-08-23-internal-data-and-custody-outcome-design.md`) — **§7.5's seven semantics
ARE the contract**, with §17-Slice-D's four bullets and §18.3 item 9 the verification bar. Foundations:
§B134's observable `wipe()` + transition-driven epoch machinery (the exact substrate this verb composes),
§B260's shared store on both platforms, Slice C's raw-pull/classification doctrine (untouched here). ⛔ Slice
E (B263 typed terminal) and F/G are LATER. ⛔ NO DEVICE CONTACT.

## Scope (§7.5, made operational)
1. **The verb**: `clear_inbox confirm` on the established console router (all transports — serial/BLE ride
   the one dispatch; V1 the `factory_reset confirm` precedent for the token parsing and REUSE its shape —
   ⛔ no new confirmation idiom).
2. **Refusal + output family — ★ FROZEN BY QG BRIEF-REVIEW (correction 3; the factory-reset family has NO
   generic needs_confirm response, so the earlier reuse-or-STOP plan is replaced by this ruled family):**
   ```
   {"ack":"clear_inbox","result":"needs_confirm"}
   {"ack":"clear_inbox","result":"cleared","epoch":4,"dm_seq":12,"chan_seq":7}
   {"ack":"clear_inbox","result":"io_error","warning":"messages_may_remain","epoch":4,"dm_seq":12,"chan_seq":7}
   ```
   Without the EXACT `confirm` token ⇒ `needs_confirm` and NO change — ★ hardened token tests: bare
   `clear_inbox` · `clear_inbox confirm extra` · `clear_inbox confirmation` · trailing junk — all inert.
3. **The wipe — ★ ONE `Inbox::clear()` ORCHESTRATION AUTHORITY (QG correction 1, load-bearing):** the
   batch-persist gap is real — `set_next_seq()` persists every eight records (`inbox.cpp:155/:256`), so a
   direct store `wipe()` can erase the only records proving the newer high-water while persisted metadata
   holds an older value ⇒ seq REUSE after reboot; an in-memory "next seq is greater" test would falsely
   pass. Required shape: `Inbox::clear()` **persists both current `_dm_next` and `_chan_next` BEFORE
   erasing anything; if either persistence fails, erase NEITHER store and report failure.** Both DM and
   channel records wiped — messages, receipts, (future) custody reports, tombstones — via the landed
   observable `wipe()` (U1); both read cursors reset. ★ Preservation is proven by REMOUNTING a fresh
   `Inbox` over the cleared durable stores before recording the next DM/channel message (never the
   in-memory counter).
4. **The epoch — ★ THE SHARED-EPOCH DESIGN IS RULED (QG correction 2; my earlier per-store reading was
   FALSE):** the public contract exposes ONE epoch (`inbox.h:219` returns `_dm->storage_epoch()`; the boot
   banner prints one canonical value), so per-store bumping would let clearing a non-empty channel beside
   an empty DM leave the externally visible epoch UNCHANGED — the companion would reset nothing. Ruled:
   `Inbox::clear()` computes ONE new non-zero target epoch and passes it to both store wipes; **both stores
   persist that same target on a successful clear, even a store that was already empty**; Fixed storage
   updates its runtime epoch too; ⛔ existing prep-restart/factory_reset wipe behavior UNCHANGED; the
   existing metadata field is reused — NO format bump. ★ Tests: empty-DM/non-empty-channel · the inverse ·
   both non-empty · both empty · Fixed storage · TWO remounts proving epoch stability (the 19.12 class).
5. **Failure honesty — ★ PARTIAL FAILURE IS A DEFINED STATE (QG correction 3):** two independent wipes
   cannot be atomic. Ruled: BOTH wipes are attempted without short-circuiting; success requires BOTH stores
   empty WITH metadata persisted; failure is explicitly possibly-partial and NEVER prints `cleared` (the
   `io_error` + `messages_may_remain` ack above). ★ Tests: DM-fails/channel-succeeds ·
   DM-succeeds/channel-fails · both fail · and RETRY behavior (a later `clear_inbox confirm` after the
   medium recovers completes the clear).
6. **Isolation** (§7.5.6 / §17-D bullet 4): routes, membership, identity, configuration, keys, counters
   outside the inbox — ALL untouched, proven not asserted (a case snapshotting representative non-inbox
   state across a confirmed clear; plus the negative direction — `clear_inbox` is NOT reachable from
   `factory_reset`/`prep-restart` code paths, no shared-helper coupling that widens either verb).
7. **Independence from B59** (§17-D bullet 3): the refusal and destructive-success contracts stand on
   today's record types (DMs + receipts); no custody record exists yet and none is referenced.

## Boundaries
- ⛔ No OLED surface — the spec gives a console verb only; the panel's inbox views react to the store
  emptying exactly as they would to eviction (a case proves the OLED list/total go to 0 with no UI code
  change; if any UI code WOULD need changing, STOP and report).
- ⛔ No store-format, marker or trait change; §B134's meta v5 and `records_state` machinery are consumed
  as-is. ⛔ Slice C's raw-pull doctrine untouched (a cleared inbox pulls empty because it IS empty).
- The companion sees the epoch bump and re-syncs from zero — the ESTABLISHED contract behavior (V1 the
  contract's epoch section; the drafted contract addition documents `clear_inbox` per §19, no decoder
  change expected — state it).

## Gate
- Native: the seven semantics as cases (refusal-inert both no-token and wrong-token · the full wipe across
  both stores incl. a receipt and a tombstone present · high-water preserved (next new message takes a
  GREATER seq) · cursors reset · epoch exactly-once with the repeated-boot stability arm · non-inbox
  isolation snapshot · the §7.5.7 report line). Both backends where the seam allows (the B134 fake +
  segmented store paths). Baseline cross-check 2389/101117/0 (derive by RUNNING; written derivation).
- Mutations (`sliceD*` isolated-harness targets, match 1, full pass per touched target, anchors
  re-derived): the confirm gate dropped (unconfirmed clears) · the wipe verdict ignored (`cleared` printed
  on failure — the B134 lie class) · ★ the pre-erase high-water persistence dropped (seq reuse across the
  remount — QG correction 1 verbatim) · ★ the erase-neither-on-persist-failure guard dropped · cursors not
  reset · ★ the shared target epoch applied to only ONE store (the empty-store arm skipped — QG correction
  2 verbatim) · the epoch double-bumped (the ratchet class) · ★ the second wipe short-circuited on the
  first's failure · a non-inbox store touched (over-reach). ⚠ The verb body may be §B115-invisible
  (fw-side) — hoist the decision per the established idiom and say which half is host-tested vs
  structurally pinned.
- Corpus: predict FIRST (an unused verb + possibly a small `Inbox` orchestration method; the sim wires no
  stores and calls no console verbs ⇒ expected corpus-inert), prove with the canonical runner
  (`run_corpus.py --jobs 8` + `--compare` vs pristine HEAD; `--jobs=1` arbitration on doubt). Boards:
  `measure_board.py pair --jobs=2`, measured + attributed; ABI probe only if a pinned struct moves.
  ⚠ B271's standing lesson: `tools/probe_firmware_ui/run.sh` MUST BUILD (and pass) if anything it compiles
  is touched — run it regardless, it is cheap insurance now. Warning census; `git diff --check`.
- ⛔ NEVER `git commit`/`git add`/`git checkout --`; maintained docs = DRAFTS (the INBOX_SYNC_CONTRACT
  `clear_inbox` section per §19 · the command-reference line · register rows if findings) — supervisor
  lands. No `tracker.md`, no `platformio.ini`, no parallel-session files, no pollers, never pipe the
  battery runner.
- Metal residue: ONE drafted bench line expected — a confirmed clear on device, then power-cycle ×2: inbox
  stays empty, epoch bumped exactly once and stable (the 19.12 class on the clear path), non-inbox state
  intact (`status`/`/mrcfg` unchanged).

## Report
The §7.5 semantics one-by-one with evidence · the epoch-wording reconciliation · the seam/verdict list
(which half host-tested vs pinned) · the isolation proof · corpus prediction + runner result · native +
PIN · mutation ledger · board attribution · drafted contract/command-reference text + the bench line ·
exact final `git status --short`.
