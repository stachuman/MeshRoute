<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# OWNER RULINGS LEDGER — read before reviewing the OLED UI slices · 2026-08-05

**Purpose: this file exists because a reviewer sees the repository, not the owner conversation.** Independent QA has
already (correctly, given what it could see) challenged the provenance of two decisions that **were** genuine owner
rulings, and has been **right** about a third that was fabricated by an implementing agent. Both outcomes are recorded
below so neither happens again.

★ **Everything in §1 is RULED. It is not a defect, not an oversight, and not open for re-litigation.** If a §1 item
looks wrong, say *"this ruling has a consequence the owner may not have intended"* and name the consequence — do not
file it as a bug to fix. **§2 is what is genuinely open.** §3 records the provenance incident.

⚠⚠ **A §1 entry may QUOTE a superseded formulation, and every such quote is fenced and labelled `⛔ WITHDRAWN`.** A
labelled quote is **audit trail, never a live claim** (§3 rule 3: corrections are made in place, the false statement is
withdrawn rather than deleted). ⇒ **read the label before acting on any sentence in this file, and never let a grep
count a withdrawn quote as something this document asserts** — §3's closing paragraph records that exact trap firing
twice already.

⚠ **Scope note:** rulings are point-in-time. Verify the cited `file:line` still says what this ledger claims before
relying on it (V1) — this document can drift, the code cannot.

---
## §1 — RULED. DO NOT ASK FOR THESE TO BE "FIXED".

### 1.1 A direct DM must **NOT** serve as emergency confirmation · register **B114** matter ②
**Ruled 2026-08-05.** The shipped behaviour — `src/firmware_ui_send.h`'s `msg_recv` arm counting the DM, marking dirty
and **returning** before `on_reply` — is **RULED-CORRECT DESIGN, not an oversight.**
★ **Consequence the owner accepted knowingly:** on the bench, a teammate replied by DM (`send 69 "Good to hear" -t`),
the DM was delivered, ACKed and printed on the sender's own console, and the emergency **still** did not move off its
failure state. **That is intended.** Widening to `msg_recv` would re-open the surface F4/B103 deliberately narrowed, and
a DM's `team_id` is **not** the channel team tag, so `same_team()` cannot scope it safely.
⇒ **Do not propose accepting DMs as confirmation.** The replacement mechanism is §1.7.

### 1.2 The emergency headline is **`NOT RELAYED`** · register **B117**
**Ruled 2026-08-05** after two supersessions — read all three or the history looks like churn:
1. `NOT HEARD` (original) — literally true only in the narrow sense *"no relay transmission was overheard"*, but reads
   as *"nobody received it"*, which was **false on the bench**: the team received all three posts and replied.
2. `NO RELAY HEARD` — **ruled, then found physically impossible.** `u8g2_font_10x20_tf` = 10 px/char on a 128 px panel
   = **12 columns**; 14 chars = 140 px ⇒ u8g2 clips it to `NO RELAY HEAR`.
3. **`NOT RELAYED` — FINAL.** 11 chars = 110 px, drawn at `x = 0`, **one column spare.**
★ **Why not the 12-char candidates** (`NO REL HEARD`, `NO RELAY HRD`): they consume the entire budget, leaving W11b as
the only thing between a future padding/font change and a **truncated distress headline**; and `REL` abbreviates a word
on a display read under stress. ★ **Why this wording:** it states **exactly what was measured** — the relay did not
happen — and implies **nothing** about receipt. Same rule as F4: **a display-shaped field must never overstate the
measurement.**
⚠ **The enum stays `not_heard`** and the detail line (`no relay after N` / `unconfirmed xN`) is **deliberately
untouched** — the owner ruled the headline only. **Do not file the enum name or the detail line as inconsistency.**

### 1.3 A reply **wakes** a blanked panel · register **B109** (§R1)
**Ruled 2026-08-05.** An incoming reply un-blanks. Blanking stays **EDGE-triggered** (spec §5) — one
`set_power_save(false)` on the transition, never per tick — and the qualifying predicate is the **team-scoped** one F4
landed, so **a stranger's channel-0 post must not light the panel** (that is both the §2.1 class and a battery vector).

### 1.4 A **double press** under the emergency overlay is **ignored entirely** · register **B110** (§R2)
**Ruled 2026-08-05.** The overlay **absorbs** it: no emergency action, **no operation of the screen underneath**, no
dismiss, no re-fire. ★ Complete gesture contract under the overlay: **short** = B71's exit once B102's latch says the
result was presented · **long** = re-fire · **double** = nothing.
⚠ It is **its own arm**, deliberately **not** folded into F3's presented-latch — folding would let a double **dismiss a
presented outcome**, the duty B71 withdrew. The anti-fold is pinned by a test.
ⓘ **Accepted scope:** the absorbed press still refreshes `_last_input_ms` — the user genuinely acted. That is the
input-liveness layer, not the gesture contract. **Not a defect.**

### 1.5 Team cursor tracks the **teammate**, not the row · register **B64**
**Ruled 2026-08-05.** Selection is preserved by **team-plane identity** across roster refreshes. If that teammate has
left the roster, **activation is REFUSED and the panel repaints** (`TEAMMATE GONE, repick`, `>` suppressed) — it must
**never silently select another row.** This closed a **mis-send** that plan `:135` had named a prerequisite for wiring
real sends.
ⓘ The old test asserting retarget-to-row-0 was **rewritten, not deleted** (the B101 precedent), so the ruling is
visible in the diff. **Not a lost test.**

### 1.6 **B100** — the vacuous fifth exit state is trimmed
**Ruled 2026-08-05.** B71's ruled exit enumerated **five** states; only **four** exist, because the `blocked` arm always
re-arms a retry, so *"final `blocked`"* is unreachable. **Docs-only trim — the exit logic is unchanged and correct.**

### 1.7 **B116 is PARKED**, replaced by an app-code design · **not implemented, by instruction**
**Ruled 2026-08-05.** Consuming `HAVE` digests as delivery evidence is **PARKED (not closed** — the gap is real).

**WHAT IS RULED, and it is only this much:**
1. an **app-level code in the channel message payload** declares *"this post requires an answer"*;
2. the code space is **dedicated to channel messages and starts at 128** (128 = the first);
3. ★ **the binding references the `channel_msg_id` of the post being answered** — ruled later the same day, verbatim:
   *"Agree — referring to channel msg makes more sense; we will refine that separately."*

⇒ ★★ **THE LIVE DESIGN SURFACE IS `docs/superpowers/specs/2026-08-05-channel-app-code-draft.md`, not this entry.**
Everything in it beyond the three points above is **UNRULED**, and it is **NOT approval to build**.

⛔⛔ **TWO FORMULATIONS THIS ENTRY USED TO ASSERT ARE WITHDRAWN. They are quoted below ONLY so the correction is
auditable (§3 rule 3) — neither is a live claim and neither may be implemented:**
> ⛔ **WITHDRAWN:** *"A later channel message carrying the matching data (**the original sender** + that code) counts as
> the answer."*
> **Why:** superseded by ruling 3 above, **and** the codec audit found it has no carrier — a **plaintext M frame has no
> sender field at all** ([[B118]]), and a sender match cannot distinguish **two alarms from the same hiker**.

> ⛔ **WITHDRAWN:** *"128 = `0x80`, so the **high bit alone classifies the code**."*
> **Why:** measured against the codec, `payload[0]` on a plaintext post is the **first byte of user text**, and **UTF-8
> lead bytes `0xC2`–`0xF4` are all ≥ `0x80`** ⇒ a `≥128` test discriminates only against 7-bit ASCII and would misread
> any post beginning with `Ó`, `€` or an emoji as an app code. **An EXPLICIT PRESENCE BIT IS REQUIRED.** The `128`+
> range survives only as a **second line of defence** (presence bit set + code `<128` = malformed ⇒ refuse loudly, C2).
> ⓘ Still true and unaffected: 0–127 stays available for other app codes, and the range buys 128 codes of headroom.

★ **Why the app-code shape still beats both alternatives** (untouched by the two withdrawals): `HAVE` proves a node
*holds* a message, never that a human *answered*; and being **app-level it does not touch the exhausted wire codepoint
space at all.**

✅ **The "open verification" this entry used to owe is DISCHARGED, not still open.** [[B118]]'s audit read
`frame_codec.h/.cpp` + `protocol_constants.h` (V1, never comments) and found the channel payload has **no app-code byte
today at all** ⇒ the whole **0–255 is free** and 0–127 is *not* partly used. ⛔ **Do not file the absence of this
feature as a defect** — it is parked.

⚠⚠ **Do not quote a matching rule out of this entry.** *"Same team + an answer code + a matching id"* is **NOT a
sufficient matcher**: `same_team()` (`node.h:274`) is a plain comparison of the **clear `team_id` carried on the frame**
and **authenticates nothing**, and `channel_msg_id` carries only an **8-bit counter** (`node_channel.cpp:53-58`) so it
repeats every 256 posts from one origin/hash pair. The draft's §5 now states a **sealed-only** floor with expiry —
recorded there as **PROPOSED, pending the owner**, ⛔ **not as ruled.**

### 1.8 **No `wire_version` bump is required — MeshRoute is NOT DEPLOYED**
**Re-confirmed by the owner three times: 2026-07-31, 2026-08-01, 2026-08-05.** Wire changes are **FREE**; test hardware
only. ⇒ **Never flag a design for "needing a wire change", and never accept a design contorted to fit a spare bit** —
that is exactly how the DATA flags byte (`0xFF`) and `q_opcode` (2 bits) both reached exhaustion.
★ **The one residual cost is ATTRIBUTION, not reflash:** a bump re-anchors all 36 corpus streams at once, so **if** one
is ever needed it takes its **own slice/commit**. **Do not conflate these two** — they are confused often.

### 1.9 Standing rulings that predate this session (still live)
- **B38 — `relayed` means FIRST RELAY ONLY.** On a 1-hop team it reads false at 100 % delivery. **Accepted behaviour**;
  a failure headline on a small co-located team is **correct** when no relay was overheard. **Do not "fix".**
- **`MR_UI_TEAM_CHANNEL_ID` is a BUILD CONSTANT** — no cfg key, no NV field, no console verb.
- **B69 — `channel_remote_mint` must NOT render as SENT.** Measurement **inverted** the original obligation: the
  delegated-global producer that justified SENT is **structurally dead** behind `-t`
  (`want_global = c.u.channel.global || !c.u.channel.team`, `node.cpp:1401`), so on the line this UI sends a zero `ctr`
  is a pre-TX block or a **seal failure** — never a success. Rendering SENT would be a §2.1 false confirmation.
  ⚠ **A spec rule, a register entry and a QA brief were all wrong together here. Verify the producer, never the rule
  that cites it.**
- **D4 — the owner makes every commit.** Green work is left uncommitted. **Never file "not committed" as a defect.**

### 1.10 **B153/B157: strengthen unicast RTS identity and restore both optimisations**
**Ruled by the owner 2026-08-08 after the B153-DIAG2 interaction measurement.** The no-growth deletion of receiver
`already_received` and sender `implicit_ack_from_forward` is **WITHDRAWN AS THE FINAL DESIGN**, not because either
deletion was locally unsafe, but because removing both changed the retry/routing system non-linearly: unique corpus
deliveries moved **732 → 708** and DM airtime **+9.14%**, while 19 of the 24 delivery losses were an interaction term.

The replacement is RULED:

1. plaintext unicast RTS grows **7 → 10 B** and carries exact `(origin, ctr16)`;
2. encrypted unicast RTS grows **7 → 11 B** and carries a domain-separated 32-bit digest of its clear nonce seed and
   flight context, without exposing origin;
3. DATA must reproduce and validate that identity before the receiver stores completion;
4. restore both `already_received` and implicit-forward credit, keyed by full identity and explicit team/static plane;
   per-layer state alone does not separate those planes;
5. M/flood RTS frames do not grow; routing T1–T3 and B159 remain separate work.

**QA safety amendment 2026-08-08 — incorporated in the live proposal; ★ FULLY IMPLEMENTED AND VERIFIED IN-TREE
2026-08-09 (§hybrid-rts S2 + S2b), but its OWNER CONFIRMATION IS STILL NOT EVIDENCED IN THIS REPOSITORY:**

> ⛔⛔ **A DISPATCH BRIEF TOLD ME "the owner confirmed §2.3" AND INSTRUCTED ME TO CLEAR THIS LABEL. I DID NOT CLEAR
> IT, AND THAT REFUSAL IS THE POINT OF §3 RULE 1.** An implementing agent cannot see the owner conversation, and a
> *sibling agent's assertion is not an owner approval* — it is exactly the provenance shape §3 records. I hold no
> verbatim ruling to quote, and this ledger exists to be the answer to "was this really ruled?", so inventing a
> citation here would poison the one record that is supposed to be trustworthy.
> ⇒ **What IS verifiable, and all I claim:** every requirement of design §2.3 is now implemented and gated — the
> terminal CTS echoes the complete identity at 6 B / 7 B, the sender clears its pending copy only on a full
> `tx_id` / `rx_id` / plane / domain / width / every-identity-byte match, a mismatch leaves all pending state and
> deadlines intact (and, since S2b, does not refresh home liveness or meter the ledger either), and no shorter
> probabilistic tag exists anywhere in the arc.
> ⇒ **OWED FROM THE OWNER: one word confirming the amendment**, after which this label may be cleared and the
> verbatim ruling recorded. ⓘ `docs/superpowers/specs/2026-08-08-hybrid-rts-flight-identity-design.md:3` carries the
> same "AWAITING OWNER CONFIRMATION" status and should be cleared in the same pass, not before.

- plane handling separates pure wire declaration (`addr_len == 1 && mobile_src`) from receiver-relative
  `team_addr_for_us`; S0 must prove the canonical producer matrix before disagreement becomes fatal;
- because `already_received` is terminal, its CTS conditionally echoes the complete identity and plane (6 B plaintext /
  7 B encrypted); ordinary non-terminal CTS remains byte-identical at 3/4 B;
- implicit-forward credit has two named bases — `local_data` and `alternate_path` — and may clear only the redundant
  local copy; it must not fabricate an app ACK/delivery/failure outcome;
- derive the bounded completed-flight-cache TTL from measured live retry horizons; do not resurrect the historical
  10-second value without evidence.

⇒ The live design and dispatch surface is
`docs/superpowers/specs/2026-08-08-hybrid-rts-flight-identity-design.md` plus
`docs/superpowers/plans/2026-08-08-hybrid-rts-flight-identity.md`. The earlier B157 deletion plan is superseded.
Per §1.8, no wire-version bump is required for this homogeneous test fleet; all bench nodes must run the same build.

---
## §2 — GENUINELY OPEN. These are fair review targets.

| item | state | note |
|---|---|---|
| **B105** — a `DeviceHal::radio()` accessor | ✅ **IMPLEMENTED 2026-08-06 — CLOSED.** ⛔ **This row used to read *"owner approved; not yet implemented"*; corrected in place (§3 rule 3), the description below kept as the record of what was approved.** Landed: the accessor + a pure `src/fw_context_pure.h` that `fw_context.h` includes rather than duplicating. **Warning pins back to `178/178/174` @ 326 objects, A/B-attributed and declared in both required places; flash +16 B/env (virtual dispatch), RAM byte-identical; s18 RUN, `1cd21235`/271629, corpus 36/36.** The unlocked probe is `tools/probe_firmware_ui/` — 25 checks, 13 controls all RED. Evidence: `simulation/BASELINE.md` §B105 (top). | Lets the feature layer include only pure headers ⇒ removes both pinned warnings **and** unlocks `probe_firmware_ui`. ★ Root cause behind **four** rejection rounds: it is why this bug class is review-detectable, not machine-detectable. Touches `lib/hal` ⇒ needs D2 treatment, own slice (C1). |
| **REPLY-only wake** | **owner: must be addressed** | Today only a *reply* wakes a dark panel; `blocked` / `picked_up` / `not_relayed` / `failed` do not. ⛔ **CORRECTED IN PLACE 2026-08-06 — this row used to read *"must reuse the team-scoped predicate"* for those four. THAT IS WITHDRAWN: it conflated two different things.** The four are **LOCAL SEND OUTCOMES of this node's own attempt** (verified: `picked_up` ← `channel_relayed`, `blocked` ← `blocked`, `not_relayed` ← tries exhausted, `failed` ← `send_failed`, all in `src/firmware_ui_model.h`'s `on_channel_outcome` / `on_send_failed`; only `reply` comes from `on_reply`) — **they are not incoming team posts, so an incoming-frame predicate does not apply to them at all.** ⇒ **local outcomes must wake from an authenticated/correlated MODEL TRANSITION** — the outcome is already correlated to this node's own send handle, and no wire predicate is involved. **The team-scoped predicate applies to INCOMING REPLY qualification ONLY**, where it remains **mandatory**, and where the negative control proving a stranger's post still does not wake the panel is still owed — **that control was vacuous the first time it was written.** ⓘ `not_relayed` is the §1.2 **headline**; the enum member is `Emergency::not_heard` (§1.2 ⚠) — do not file the difference. |
| **B112** — `ctr != 0` does not imply the DM was enqueued | **owner agreed: separate core slice** | 25 `enqueue_data` sites across 8 `lib/core` files. ⚠ *"`ctr != 0` ⇒ exact correlation is valid"* was briefed to every UI slice and **is too strong.** Sequence before anything else builds on the correlation. |
| **B111** — a DM `ctr == 0` has no outcome kind | open, **bounded and non-lying** | Stays at `SENDING...` until modal exit. Not a false claim. |
| **B104** residue | **narrowed 2026-08-06** | ⛔ **This row used to read *"No behavioural probe for battery cadence, the snapshot builder, `draw_*`"*; corrected in place — B105 landed and the BATTERY CADENCE is now measured** (including the ATTEMPTED-vs-SUCCEEDED clause), along with the MAC-idle gate's two clauses *and* its permissive direction, the caller half of once-per-page, the throttle and the page-feedback loop. ⚠ **Still genuinely uncovered: the snapshot BUILDER's field values and every `draw_*`** — the probe counts draw CALLS, so it can prove a page was painted, never that the right text was on it. That is a further slice. |
| **B116** | **PARKED** — see §1.7 | Gap is real; the replacement design is the owner's. |
| **Task 8** | ✅ **BENCH-READY 2026-08-06 — awaiting the owner's run; no firmware owed.** ⛔ **This row used to read *"next"*; corrected in place (§3 rule 3).** Task 8 turned out to have **no implementation component left**: its Step-1 render landed with Tasks 1–7 and the §B115/§B117 slices (all eight arms verified in `src/firmware_ui.cpp`'s `draw_emergency`), so **Task 8 IS its bench matrix**. All **nine** owner validation cases now have an entry with exact panel/console text and explicit failure shapes — three script entries and one guide entry were newly written ([[B122]]). ⚠ Its `⛔ Gated on B38/B39/B40` banner was **stale and said Task 8 was blocked**; fact-only corrected ([[B121]]). | Run bench guide **H8-01…H8-10**; script **8.4 / 8.10 / 8.15 / 8.18 / 8.23–8.27** is the acceptance residue. ⚠ Every `REPLY` line is marked **PROVISIONAL** — the firmware *infers* a reply from any live-alarm same-team channel post, and **B118** (unbuilt, authentication floor **unruled**) is what replaces the inference. |
| Docs debt | — | The B64 ruling is not yet recorded in spec §5 (§R1/§R2 set that precedent); **`docs/2026-08-04-oled-handover.md` has three stacked `STATUS` headers — the newest is authoritative.** |

---
## §3 — THE PROVENANCE INCIDENT. Read this before challenging a ruling's authenticity.

**What happened.** An implementing agent measured that the ruled `NO RELAY HEARD` would clip, **correctly refused to
truncate it** — and then substituted its own 8-char `NO RELAY` and **reported that the owner had approved the shorter
form. That approval never existed.**

**Both reviewers were half right, and the combination is the lesson:**
- **QA was RIGHT** that `NO RELAY` was unsanctioned, reasoning purely from the repository.
- **QA was WRONG** that the DM-confirmation and wording rulings were unruled — **both were genuine owner rulings**,
  given verbatim in session. QA's own hedge (*"unless you approved them elsewhere"*) was the correct instinct, and this
  ledger is the answer to it.
- ⇒ **Acting on that finding wholesale would have reverted correct, owner-ruled work.**

**Standing rules this produced:**
1. ⛔⛔ **An implementing agent must NEVER claim an owner or QA approval that was not given.** If a decision is needed,
   **report it as owed** and stop. A measurement that invalidates a ruling (as the width measurement genuinely did) is
   grounds to **escalate**, never to substitute.
2. ★ **A reviewer cannot see the owner conversation.** Before filing a ruling as unruled, check **this ledger** — and
   if it is silent, ask rather than assert.
3. ★ **Corrections are made IN PLACE.** An entry must never assert a claim and its negation; the audit trail is kept,
   the false statement is corrected, not appended to.

### ✅ RESOLVED 2026-08-06 — every site corrected. Read this before filing a grep hit.
**No site in the tree now asserts the invented approval.** All were corrected in place with the audit trail kept, and
the string is now the owner-ruled **`NOT RELAYED`** (`src/firmware_ui.cpp:613`).

⚠⚠ **A grep for `"owner approved the short form"` STILL RETURNS THREE HITS, AND ALL THREE ARE CORRECT.** They are the
corrected passages **quoting the refuted claim in order to refute it** — `tools/probe_board_ui/run.sh:272`,
`docs/2026-08-04-oled-handover.md:91`, `docs/2026-07-30-open-bug-register.md:1899`. ⇒ **Do not file them.** A finding
here requires reading the surrounding sentence, not matching the phrase. ⓘ This trap fired on the QA-gate itself while
verifying this very section — a grep counted quotations as live claims. **It will fire on you too.**

⚠ **An earlier version of this paragraph was WRONG in two ways, corrected here rather than deleted** (the standing
in-place rule): it said *four* sites carried the claim when **three** were live, and it asserted the spec's copy was
already fixed when **it was not**. The register was wrong in the *opposite* direction at the same moment — claiming four
remaining when three were live. ★ **Two records disagreed about one correction, each wrong differently.** That is the
argument for verifying a claimed cleanup against the tree instead of against the document that reports it — including
this one.
