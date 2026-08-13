<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-7D slice B — the inbox detail/delete modal · dispatch brief · 2026-08-13

**Status: DISPATCHED 2026-08-13 on an owner ruling (ledger §1.21 — return to the OLED plan with this slice, then
settings dirty/save).** ★ Role split: the QA-gate wrote this brief and verifies your claims at the code; **the OWNER
runs QG and rules.** ⛔ **Never `git commit`. Never `git add -A`. NEVER `git checkout --` anything; never check out
another commit here** — the tree carries the uncommitted mobile-home arc.

⛔⛔ **AMENDED 2026-08-13 BY INDEPENDENT QG BEFORE DISPATCH — five corrections and two clarifications. READ THESE
FIRST; where they differ from anything below, they win.** All seven were verified at the code by the QA-gate.

**A · ONE KIND AUTHORITY — ⛔ do NOT add `kind` alongside `is_dm`.** `InboxRow` today carries **`bool is_dm`**
(`src/firmware_ui_model.h:182`), and it is **load-bearing**: `InboxRowBudget::add()` branches on `r.is_dm` to select
the `_dm`/`_ch` buffer. ⇒ **REPLACE `is_dm` with `InboxKind kind` (+ `seq`)** per the spec (`design.md:863`), and
**derive DM/channel rendering AND budgeting from `kind`.** ⛔ **Two kind fields = two authorities that can drift, and
then the DISPLAYED kind and the ERASE TARGET disagree — i.e. you delete from the other store.** ★ This is the
identity class in a new form: not a missing tuple member, but a **redundant** one.

**B · THE PURE-MODEL SEAM, prescribed.** `UiModel` **cannot** call `g_node.inbox()`. Required shape:
1. the **model emits an open/delete REQUEST carrying `(kind, seq)`**;
2. **`firmware_ui.cpp`** performs `pull()` / `erase()`;
3. the **callback copies AND sanitizes the body BEFORE returning**;
4. a **typed result** is fed back to the model;
5. the **renderer reads only frame-frozen modal data, never the live model buffer.**
★★ **Keep the full 242-byte buffer LIVE and freeze only the CURRENT 42-character page plus header/action state.**
⇒ That avoids duplicating the whole buffer and preserves the existing frame-freeze contract. ⛔ **Do not freeze 242
bytes** — this brief's earlier wording implied a whole-body freeze and is superseded.

**C · TERMINAL / ERROR BEHAVIOUR — the brief did not specify it. Required:**
- **activation-gone:** rebuild INBOX and show a **bounded `MESSAGE GONE` refusal**;
- **delete `not_found`:** a **TERMINAL modal state with NO active Delete**; short/double returns to a **rebuilt** INBOX;
- **`io_error`:** **remain in detail**, show `DELETE FAILED`, **reset selection to the safe `back`** ⇒ **a retry
  requires short → double again.**

**D · EMPTY AND BINARY BODIES — verified: `e.body` is `nullptr` when `body_len == 0`** and is valid only for the
callback's duration (`src/firmware_ui.cpp:233`; E2E-ack records legitimately have no body). Required:
- **`pages = max(1, ceil(body_len / 42))`** — ⛔ never zero pages;
- ⛔⛔ **use `body_len`, NEVER `strlen`** — the body is not a C string and may contain NUL;
- **boundary tests at 0, 1, 42, 43, 84 and 241 bytes**;
- **ONE shared display-byte sanitizer — prefer the existing `'.'` policy — covering NUL, control and high-bit bytes.**

**E · CLOSE DETAIL AT `long_arm`, NOT `long_fire`.** ⚠ Existing **compose** closes only when emergency **fires**
(`src/firmware_ui_model.h:933`, where `emergency_gesture` handles `long_arm` → arming, `long_cancel`, then
`long_fire`). **UI-7D requires closing BEFORE arming.** ⛔ Copying compose's behaviour gets this wrong. ★ **And test
the CANCEL path: after `long_arm` → `long_cancel`, the detail modal and its selected Delete must NOT reappear.**

**F · UNREAD COUNTERS — a completed DETAIL frame must NOT clear session unread counters.** Only a completed
**preview-list** frame should. ⇒ **`FrameGate::_fr_inbox` must EXCLUDE the detail modal**
(`src/firmware_ui_model.h:1045`). ⓘ **This follows the existing design exactly:** that block's own comment records
that the emergency overlay and the compose modal each **replace the body**, so a frame showing either *has not shown
the Inbox*. **The detail modal is the third of that kind — treat it the same way.**

**G · ⚠ THE [[B134]] WARNING, which the spec's wording hides:** on **Heltec V3 the inbox backing store is RAM-ONLY**.
⇒ §3.5 says *"request durable deletion"* and forbids *"a visual disappearance without durable success"* — but on V3
**"durable success" means a successful tombstone append WITHIN THAT RUNTIME, not persistence across power loss.**
⛔ Do not claim power-loss durability anywhere in the UI, the tests or the report.

---

**Normative spec: `docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md` §3.5** (and §3.2's gesture table).
Read it in full before coding; it is unusually complete and this brief does not restate all of it.

**Baseline:** HEAD **`c7bca52`**; native **1515 / 81320 / 0**; `lus` **`43a7b6eb`**; `sizeof(Node)` **221880**.
⛔ The delivery floor is **frozen/unratified** — irrelevant to this slice, and you must not touch it.

---
## 0 — What already exists, so you build only the UI half

- ✅ **Storage is DONE** (UI-7D slice A, landed 2026-08-06, QA-rejected the same day, **re-closed 2026-08-07**):
  **`InboxEraseResult Inbox::erase(InboxKind kind, uint32_t seq)`** (`lib/core/inbox.h:158`) — tombstone-based, **three
  outcomes**, console verb `del_msg`. Its two blockers ([[B135]] durable-store mid-frame tear, [[B136]] verb target
  parsing) are **fixed**. ⛔ **Do not redesign or re-verify storage; consume it.**
- ✅ **Browsing needs no new subsystem** — `Inbox::pull()` already visits both stores and every `InboxEntry` carries
  what a detail view needs (spec §6).
- ✅ **`mr_ui_tick(now_ms)` already exists** (`src/firmware_ui.cpp:685`). ⇒ ★★ **Use it for the 2 s page advance.**
  ⛔⛔ **DO NOT allocate a `Node` timer: `TimerWheel::kCap` is 91 and ALL ids are consumed.**
- ⓘ Today **a double press on INBOX intentionally does nothing.** That is the hook you are filling — not a bug.

---
## 1 — ★★ THE ONE REQUIREMENT MOST LIKELY TO BE GOT WRONG: identity

**Spec §3.5, verbatim in substance: selection identity is `(InboxKind, seq)` — NOT the visible row index, origin,
message counter or body. DM and channel sequence spaces are INDEPENDENT, so `seq` alone is insufficient.**

⇒ Therefore:
- **the preview snapshot must carry the PAIR**;
- **activation re-finds the exact record and copies it**;
- **if a refresh moves rows, the highlight follows the IDENTITY**;
- **if the record has disappeared, activation REFUSES with `MESSAGE GONE`** — ⛔ it must never open or delete **its
  replacement**.

⚠⚠ **This is the arc's most expensive recurring defect class — *identity is the whole tuple* — with five prior
instances: [[B133]] (`seq` without `InboxKind`, i.e. THIS EXACT PAIR at another site), [[B142]], [[B147]], [[B153]] and
the hosted-row tuple. ⛔ A row-index or `seq`-only shortcut here deletes the wrong message.** Assert the pair on the
snapshot, on activation and on the erase call.

---
## 2 — The modal, per §3.5

- **Layout/headers:** `DM from <origin>` with a `<page>/<pages>` indicator; for a channel row, **`CH<n> from <origin>`**.
- **Body:** ★ **copied** from the selected `InboxEntry` into a fixed **`inbox_max_body + 1`** buffer held for the
  modal's lifetime. ⛔⛔ **It must NEVER render or dereference the callback-owned `InboxEntry::body` after `pull()`
  returns** — that is a use-after-free, and the buffer is what prevents it. **Wrap without dropping bytes.**
- **Unsupported display bytes are replaced VISIBLY, never treated as control characters.**
- **Paging:** two body rows expose **42 chars/page**, so the maximum **241-byte** body needs at most **six** pages.
  Long bodies **advance automatically every 2 s and cycle** while the modal is open. ★ **A page change marks the model
  dirty but does NOT reset the user-inactivity deadline**, and every resulting repaint still obeys **§5's
  MAC-idle/page-buffer gate.**
- **Gestures:** **short** toggles the action selection · **double** activates it · ★ **`back` is selected initially,
  so deletion requires the deliberate sequence short → double.**
- **`back`:** close, return to INBOX, **change nothing in storage.**
- **`delete`:** request durable deletion of **this exact record**, then:
  - **success** → close, **rebuild the list, and preserve the neighbouring selection where possible**;
  - **`not_found`** → show **`MESSAGE GONE`** (the bounded store may have evicted it meanwhile) and ⛔ **never affect
    another row**;
  - **storage failure** → **stay in the modal** and show **`DELETE FAILED`**. ⛔⛔ **A visual disappearance without
    durable success is FORBIDDEN.**
- **Emergency interplay:** **long press closes the detail modal BEFORE arming emergency** — ⛔ **the hidden Delete
  action must not survive underneath an emergency overlay.** ⓘ Consistent with ledger **§1.4**: a double press *under*
  the overlay is absorbed entirely, so the two must not fight. **Ordinary modal timeout returns to INBOX without
  deleting.**

---
## 3 — Out of scope

⛔ **§3.6 / UI-13…UI-16 settings and provisioning** — that is the **next** slice (settings dirty/save) and must not be
started here: no SETTINGS screen, no draft marker, no team-create/static-join UI, no nearby-team onboarding.
⛔ Storage redesign · the `del_msg` console verb (exists) · [[B186b]] (owner-ruled unimplemented) · any `s07`,
anchor-table or delivery-floor change · the mobile-home arc.

★★ **AND THE STANDING GATE, which this slice does NOT satisfy and must not appear to:** [[B164]] and [[B189]] remain a
**mandatory gate before on-device registration / team onboarding and final Phase-A acceptance** (ledger §1.21).
⇒ ⛔ **Do not describe Phase A as complete**, and do not let a passing UI-7D imply the TX-truth question is settled.

---
## 4 — Gate

1. `pio test -e native`, **then RUN `./.pio/build/native/program`** (⚠ the wrapper prints a false *"0 test cases"*).
   From **1515 / 81320 / 0**.
2. **The board builds matter here** — this is `src/` + `MR_FEAT_OLED`. ★ **Run `warning_census.sh`** and report the
   multiset. ⚠ **[[B169]]'s shape is live for you:** board envs define `MESHROUTE_NO_TELEMETRY`, which deletes the
   `MR_TELEMETRY` body and orphans any variable whose only consumer is inside an `MR_EMIT` — invisible to native and to
   the corpus, and it survived four slices. **If you add an emit, test the telemetry-disabled compile.**
3. **Corpus: expect NO movement** — this is UI-only. ⛔ If a row moves, **stop and report**; it means you touched
   something that reaches the protocol. Print the `lus` md5 beside the result.
4. ★★ **Answer D2 explicitly.** `sizeof(Node)` is **221880** and must not move — the modal's body buffer belongs to
   the **UI model in `src/`**, not to `Node`. ⚠ But it **is** board RAM: report the per-board RAM/flash diff, and note
   `inbox_max_body + 1` is not free on a V3.
5. ⛔ **Allocate no `Node` timer** (`kCap == 91`, all consumed). The page advance rides `mr_ui_tick`.
6. **The UI probes exist and are the right instrument here** — `tools/probe_firmware_ui/` and `tools/probe_board_ui/`.
   ★ **Run them and report their control sets.** ⓘ [[B105]] unlocked `probe_firmware_ui` precisely so the feature layer
   is testable; use it rather than asserting by inspection.

### Test obligations
- ★ **Every new assertion mutation-proven, match counts printed.** This arc has **23** recorded instruments that could
  not fail — the most recent a durable fixture with empty `expect` arrays and no `sys.exit`.
- ★★ **The identity tests are the ones that matter:** a refresh that **moves rows** must keep the highlight on the same
  `(kind, seq)`; a **vanished** record must yield `MESSAGE GONE` and **leave every other row untouched**; and a
  DM/channel pair **sharing a `seq`** must not cross-select. ⛔ A test that only exercises a static list proves nothing
  about identity.
- **All three delete outcomes**, including the **`DELETE FAILED` stay-in-modal** path — ⛔ and a control proving the row
  is **still present in storage** after that failure, since "a visual disappearance without durable success is
  forbidden" is the requirement.
- **`back` changes nothing in storage** — assert the store, not the screen.
- **The long-press-closes-before-arming path**, and that Delete cannot be activated under the overlay.
- ★ **B101 precedent:** any existing test asserting *"double press on INBOX does nothing"* must be **rewritten in
  place** to assert the new behaviour — ⛔ never deleted, never disabled — with a heading saying what changed.

---
## 5 — Method

- ★★ **Identity is the whole tuple** (§1). Five prior instances; this is the sixth site, and [[B133]] was *this exact
  pair*.
- ★★ **A fact is established by the act, never inferred** — ⛔ **never show a row as gone before `erase()` returns
  success.**
- ★★ **Instruments that cannot fail — 23 instances.** Ask of every new test: could it have come out otherwise?
- ★ **A correction placed anywhere but the instruction a reader follows** — eleven-plus sites. If you supersede a spec
  or plan sentence, fix it **where a reader acts on it.**
- ⛔ **PROVENANCE (ledger §3, five incidents):** never claim an owner or QA approval; **never quote an owner ruling** —
  reported form only; ⚠ **a QA recommendation relayed by the owner is STILL a recommendation.**

**Report:** the modal with `file:line` · how `(InboxKind, seq)` is carried on the snapshot, re-found on activation and
passed to `erase()` · the body-copy lifetime and its buffer · paging (2 s, 42 chars, ≤6 pages) and the
dirty-without-deadline-reset behaviour · all three delete outcomes with their controls · the emergency interplay ·
native + `warning_census.sh` + corpus (expect no movement, with the `lus` md5) + the **D2 answer** + per-board RAM/flash
+ **both UI probes' control sets** · every rewritten test · exact final `git status --short` and that nothing was
committed. ⛔ **Anything you cannot establish, say so plainly, and do not describe Phase A as complete.**

**Stop and report rather than improvising if:** the identity pair cannot be carried on the snapshot without changing a
`lib/core` structure · a corpus row moves · `sizeof(Node)` moves · the body copy needs more RAM than a V3 can spare ·
or §3.5 and §3.2's gesture table disagree (⇒ **report the conflict, do not pick a side**).
