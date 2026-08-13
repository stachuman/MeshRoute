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

### 1.11 §2.3 — a terminal CTS may clear sender state ONLY on complete correlation · ✅ **OWNER-CONFIRMED 2026-08-09**

⛔ **RENUMBERED 2026-08-10 from `1.10` to `1.11`: this heading and §1.10 above BOTH read `1.10`** — the same
id-used-twice maintenance defect the register recorded as [[B155]], in this file. ⚠ Any external reference to
*"ledger §1.10 = the verbatim §2.3 ruling"* means **this section, now §1.11**; the §2.3 content itself is unchanged.

★★ **THE OWNER'S RULING, VERBATIM, GIVEN DIRECTLY IN SESSION 2026-08-09:**
> **CONFIRMED. I approve §2.3: a terminal CTS may clear sender state only after complete endpoint, plane, domain,
> width and identity-byte correlation. A mismatch may be billed as physical airtime, but must not refresh liveness
> or alter timers, routing, pending state, or application outcomes.**

⇒ **This is now a §1 RULING and is not open for re-litigation.** Its two halves are separately load-bearing:
1. **Clearing sender state requires COMPLETE correlation** — endpoint · plane · domain · width · **every identity
   byte**. ⛔ A shorter probabilistic tag or endpoint-only correlation is **not acceptable anywhere in this arc**,
   because a terminal CTS clears sender state and a false clear is a **silent message loss** (that is B153's whole
   defect class).
2. ★ **A mismatch MAY be billed as physical airtime, but must change NOTHING else** — no liveness refresh, no timer,
   routing, pending-state or application-facing effect. ⇒ **Accounting measures airtime that DID occur; trust
   decisions wait for evidence.** Without the billing half, a sender could evade accounting with the terminal bit
   plus a bogus identity.
⚠ **Consequence for S3 and beyond:** the comparison happens **before** timer cancellation or **any** state/telemetry
change — ordering is part of the ruling, not an implementation detail.

★ **Provenance, stated because this entry is the answer to "was this really ruled?":** the words above were given by
the owner to the QA-gate in conversation on 2026-08-09 and are recorded verbatim, not paraphrased.

<details><summary>⛔ AUDIT TRAIL — why this label existed for two days, kept because the refusals were CORRECT</summary>

★★ **Two agents were told by QA-gate dispatch briefs that "the owner confirmed §2.3", and both refused to act on it.
Both were right, and the rule that made them right is §3 rule 1.** The QA-gate held the claim only **second-hand** and
had **no verbatim ruling** — so an implementing agent declined to clear this label, and the QA-gate itself later had to
**withdraw a "§2.3 HOLD CLEARED" line it had written into
`docs/superpowers/plans/2026-08-08-hybrid-rts-flight-identity.md`**, which independent QA caught as a contradiction
against this ledger and the design doc.
⇒ ★ **The lesson, and it is the reason this ledger exists:** *a sibling agent's assertion — including the QA-gate's — is
not an owner approval.* An invented citation here would poison the one record meant to be trustworthy. The label was
cleared **only** when the owner's own words arrived, which is exactly the intended behaviour of the mechanism.

**The pre-confirmation text, retained verbatim:**
> **QA safety amendment 2026-08-08 — incorporated in the live proposal; ★ FULLY IMPLEMENTED AND VERIFIED IN-TREE
> 2026-08-09 (§hybrid-rts S2 + S2b), but its OWNER CONFIRMATION IS STILL NOT EVIDENCED IN THIS REPOSITORY:**
</details>

ⓘ **What was already verifiable in-tree before the confirmation** (unchanged by it — the implementation did not wait):

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
  - ✅ **THE FIRST LABEL IS NOW `local_admitted`, AND THAT SPELLING IS ITSELF OWNER-RULED 2026-08-10 — see §1.12.**
    The ruling's substance — two named bases, clear only the redundant copy, fabricate no app outcome — is UNCHANGED
    and was never challenged; only the label's spelling moved. ⛔ This bullet used to read *"IMPLEMENTATION NOTE, NOT A
    RULING … NO OWNER CONFIRMATION OF THE NEW LABEL IS CLAIMED"*, with a standing offer to revert the label if the
    owner preferred the original spelling. **That offer is now closed by §1.12; corrected in place (§3 rule 3).**
- derive the bounded completed-flight-cache TTL from measured live retry horizons; do not resurrect the historical
  10-second value without evidence.

⇒ The live design and dispatch surface is
`docs/superpowers/specs/2026-08-08-hybrid-rts-flight-identity-design.md` plus
`docs/superpowers/plans/2026-08-08-hybrid-rts-flight-identity.md`. The earlier B157 deletion plan is superseded.
Per §1.8, no wire-version bump is required for this homogeneous test fleet; all bench nodes must run the same build.

### 1.12 `local_admitted` is the telemetry basis name · ✅ **OWNER-RULED 2026-08-10**

★★ **THE RULING, AS THE OWNER STATES IT (2026-08-10):**
> **I did approve `local_admitted`**

⛔⛔ **THIS SECTION NO LONGER PRESENTS ANY QUOTATION AS VERBATIM, AND THAT IS THE CORRECTION — read it before
re-filing.** The approval is **valid and owner-given**; what was wrong was the QA-gate's *transcription discipline*.
Two earlier renderings stood here as **bold verbatim quotations** (*"I approve `local_admitted` as the telemetry basis
name"* and *"Yes, I do confirm `local_admitted` and will clarify to quality agent"*), and the owner has stated that
**neither is their actual wording.** ⇒ Both are **WITHDRAWN as quotations**, and the claim that **two distinct owner
statements** backed this section is **WITHDRAWN** too. The single line above is the owner's own supplied wording.

★★★ **THE RULE THIS COST, AND IT BINDS EVERY FUTURE ENTRY IN §1: DO NOT USE QUOTATION MARKS FOR AN OWNER RULING
UNLESS THE EXACT CHARACTERS ARE HELD.** A reconstruction dressed as a quotation is **indistinguishable from an
invention** — by a reviewer, and (as this incident proves) even by the QA-gate that wrote it, which believed its
renderings were exact. ⇒ **Record rulings in REPORTED form** (*"the owner approved X"*) **unless the wording is
copied, not remembered.** A reported-form ruling that is accurate beats a quoted one that is embellished, because the
embellishment is what destroys the ledger's only value: being the trustworthy answer to *"was this really ruled?"*
ⓘ **Provenance history of this section, kept because it is instructive both ways:** a review pass flagged it as wholly
fabricated and asked for deletion; the ruling itself turned out to be **real**, so deletion would have destroyed a
genuine record — ⛔ **the right resolution of that ambiguity is to ASK THE OWNER, never to delete on a reviewer's
inference, and never to defend a quotation the owner disowns.** §3's earlier firings were all *"the QA-gate asserted a
ruling it did not have"*; this one adds a **third** failure mode — **a ruling it did have, recorded in words that were
not the owner's.**

⇒ **The implicit-forward credit's two bases are `local_admitted` and `alternate_path`.** The first replaces the
§1.10 ruling's original spelling `local_data`; §1.10's *substance* is untouched by this and was never in question.
The C++ field is `PendingTx::data_ever_admitted` (was `data_ever_transmitted`).

⚠ **PROVENANCE, stated precisely, because this label took two rounds to land honestly.** The owner approved **the
name**. The gloss that has circulated alongside it — *"It means HAL admission, not proof that the DATA aired"* — was
**drafted by the independent QA agent, not spoken by the owner**, and it is recorded here as a **verified-in-code
fact**, not as part of the ruling:
- `_hal.tx()` returns `ok` on **ENQUEUE** — `lib/hal/device_hal.cpp:10-12` says so outright (*"Returns ok when
  queued"*), with the on-air send deferred to `pump_tx()`;
- the flag is set at the **one crossing point every admission passes** (`lib/core/node_mac.cpp:1742`, on
  `tag == FrameTag::data` with a non-broadcast pending flight, immediately before `return TxHandOff::handed`);
- ⇒ the name is accurate about **admission** and makes no claim about **airing**, which is exactly why [[B164]]'s
  airing half remains **OPEN** and separately registered.

★ **Why the split matters and is not pedantry:** an earlier dispatch brief asserted a §2.3 owner confirmation the
QA-gate held only second-hand, and **two agents correctly refused it** while independent QA caught a
*"HOLD CLEARED"* line the QA-gate had written on the same second-hand basis (§1.11's audit trail, §3 rule 1). ⇒ The
verbatim ruling above is the owner's; the surrounding semantics are the code's. **Neither is attributed to the other.**

### 1.13 Lua parity is **NOT** a final MeshRoute jitter requirement · ✅ **OWNER-RULED 2026-08-10**

★★★ **THE RULING, IN REPORTED FORM — ⛔ NO QUOTATION IS OFFERED, DELIBERATELY:**
**The owner ruled on 2026-08-10 that Lua parity is not a final MeshRoute jitter requirement, and that [[B158]] stays
open until MeshRoute-native jitter has been independently measured and selected.**

⛔⛔ **A BOLD BLOCKQUOTE STOOD HERE AND IS WITHDRAWN — it summarised the ruling accurately but was NOT the owner's exact
words**, which is the identical defect §1.12 above had just established a rule against. ⇒ **§1.12's rule applies to
every entry in §1, including the ones written after it: record in REPORTED form unless the exact characters are held.**
ⓘ That this was violated *in the very next entry written* is the strongest evidence for the rule: the QA-gate was not
being careless about provenance — it was **unable to tell its own paraphrase from a transcript**, twice in one session.
⛔ **Do not "restore" a quotation here from any agent's recollection.**

⛔⛔ **THIS SUPERSEDES THE `retry_jitter_ms` HALF OF THE 2026-08-10 B158 DISPOSITION.** That disposition had two
outcomes — *retain `retry_jitter_ms()` for Lua parity* and *accept `exchange_airtime_ms()` as measured-and-left*.
**Outcome 1 is WITHDRAWN by this ruling**; outcome 2 stands. ⇒ **B158 is REOPENED**, with a scope that is no longer
*"change 8 to 10/11"* but **the design of MeshRoute-native jitter**.

★★ **WHY IT IS BIGGER THAN A CONSTANT (owner's finding, verified at the code 2026-08-10): ONE HELPER CONTROLS FOUR
UNRELATED POLICIES, so retuning any one of them silently retunes the other three.** All four consume
`Node::retry_jitter_ms()` (`lib/core/node_mac.cpp:65`):

| policy | site | how it consumes the helper |
|---|---|---|
| fresh DM origination spreading | `node_mac.cpp:316` | `rand_range(0, retry_jitter_ms()+1)`, app DMs only, `nav_enabled` only |
| RTS/ACK same-hop retry spreading | `node_cascade.cpp:365`, `:398` | `protocol::retry_backoff_window(retry_jitter_ms(), attempt, max_shift)` |
| BUSY_RX release spreading | `node_mac_rx.cpp:2077` | `rand_range(0, retry_jitter_ms()+1)` on the short-busy same-hop wait |
| **default LBT release backoff** | `node.cpp:508`, `:913` | ⚠ **`lbt_backoff_ms = max(1, retry_jitter_ms()/2)`** — a **÷2 derivative**, confirmed at `node_carriers.h:244` |

⇒ ★ **"changing retry jitter must no longer silently resize LBT backoff"** is the concrete coupling to break first.

**THE OWNER'S PROPOSED PROGRAMME (recorded as the owner's proposal; ⛔ the multiplier is explicitly NOT ruled):**
1. **Split into four semantic helpers, initially BEHAVIOUR-IDENTICAL** — `dm_origination_jitter_ms()` ·
   `same_hop_retry_jitter_ms(crypted)` · `busy_rx_release_jitter_ms()` · `lbt_release_jitter_ms()`. ★ A
   behaviour-identical split is byte-inert by construction, so it is a clean C1 refactor slice on its own.
2. **Define a MeshRoute airtime quantum:**
   `rts_contention_quantum_ms = airtime_routing_ms(unicast_rts_wire_len(/*worst case*/ true));` **(11 B)**.
   ⚠ **This is the SCALE ONLY — it is NOT automatically the final window.**
3. **Same-hop retries, first candidate:** `retry_window = 3 * rts_contention_quantum_ms; jitter = rand(0, retry_window)`.
   ★★ **KEEP THE WINDOW FLAT ACROSS ATTEMPTS.** MeshRoute already expands the CTS timeout up to **×4**, and a previous
   **24-seed** experiment showed additional exponential jitter **reduced** delivery. ⛔ **Do not add a second BEB
   layer.** ⓘ Verified consistent with the tree: `protocol::retry_backoff_max_shift` is **globally 0**
   (`protocol_constants.h:143`), i.e. the window is already flat today.
4. ⛔⛔ **TREAT MULTIPLIER 3 AS A CANDIDATE, NOT A CONCLUSION. Measure `K = 1, 2, 3, 4, 6`** with
   `retry_window = K × airtime(11-byte RTS)`, evaluated across **all 36 scenarios · the 24-seed saturated twin ·
   `s06`, `s07`, `s16`, `s18`, `s27` individually · real-hardware simultaneous-send tests.**
   **Primary metrics:** unique deliveries and `send_failed` · p50/p95/p99 delivery latency · RTS attempts and airtime
   **per delivered DM** · collisions, no-CTS retries and cascades · pending-TX residence and queue blocking · false
   liveness penalties caused by congestion.
   ⛔ **DO NOT CHOOSE A MULTIPLIER FROM ONE DETERMINISTIC CORPUS RUN** — `s16` has already demonstrated chaotic,
   non-monotone responses to small timing changes (§B158-EXCHANGE-ARM: 56/56/60/56/**70**/55 on an N-of-one row).
5. **Then tune origination, BUSY_RX and LBT INDEPENDENTLY**, after the retry window is chosen.

ⓘ ★ **THE OWNER'S EXPECTATION, EXPLICITLY NOT A RULING:** *a flat 2–3 actual-RTS-airtime window will win*, with
`3 × airtime(11 B)` the safest first arm because it preserves the existing design intent while removing the Lua
dependency; the measurements may justify reducing toward **2×** for faster failure and earlier cascade. ⛔ **This
paragraph must never be cited as the decision** — it is the hypothesis the K-sweep exists to test.

### 1.14 An expired hosted row provides NO service, before compaction · ✅ **OWNER-RULED 2026-08-10**

★★★ **THE RULING, IN REPORTED FORM — ⛔ no quotation, per §3 rule 4:**
**The owner ruled that a mobile registry row at or beyond `mobile_liveness_ms` (25 min) must NOT provide direct
hosted service or last-mile service, EVEN BEFORE physical compaction — and that the rule must be applied
CONSISTENTLY to every such service path.**

⇒ **What this settles:** §MH-S5 had wired physical expiry (`mobile_reg_age_out`) but left the *service* question to
each consumer, so an expired row still served on some paths until the next 60-second sweep. §MH-S5-FIX then added
the boundary to **one** last-mile path, producing a half-and-half state QA called the least desirable option.
**This ruling makes the boundary systematic**: physical compaction stays on the aging timer; *service refusal* is
immediate everywhere. Live dispatch:
`docs/superpowers/plans/2026-08-10-mobile-home-s5-livedirect-consistency.md`.

⚠⚠ **THE RULING IS ABOUT SERVICE, NOT ABOUT THE REDIRECT MECHANISM.** A redirect row must still **answer a
hash-location redirect** — that is the redirect doing its job, and `node_hashlocate.cpp`'s redirect fork is
deliberately **not** liveness-gated. ⛔ **Gating the redirect answer would be a regression**, and §MH-S5-FIX's
*"the redirect still redirects"* positive control exists to catch exactly that over-fix.

⛔ **IT ALSO SUPERSEDES AN IN-SOURCE INSTRUCTION:** `lib/core/node.h:1483-1487` says *"⛔ NOT the same question as
the bare `redirect_home_id == 0` tests … Do not mechanically fold them in here."* **Withdrawn for service paths**,
and it must be corrected in place — otherwise it is another correction placed anywhere but the instruction a
reader follows.

ⓘ **Related decision, same day:** [[B170]]'s re-anchor of `s07_seattle_mobile_meshroute` to **`2ce470f9` / 108951**
is **APPROVED BUT CONDITIONAL** — it holds only if the completed corrective work still reproduces that value. A
different value is a **new** decision, not a transfer of this approval.

✅ **IMPLEMENTED 2026-08-10 by §MH-S5-FIX2 — evidence in `simulation/BASELINE.md` §MH-S5-FIX2; register
[[B174]]/[[B175]]/[[B176]].** The boundary now reaches **ten** consumers (four from §MH-S5-FIX plus the MOBILE_SEND
ownership scan · the `send_by_hash` direct last mile · `forward_requester_key_to_mobile` · `host_mobile_ed_pub` · the
team-key-grant pre-check · `presence_mark_deleg_fail`), all through the ONE non-mutating `host_row_live_direct()`, and
the withdrawn `node.h:1483-1487` instruction is **corrected in place with its old text kept as a labelled
`⛔ WITHDRAWN` quote** (§3 rule 3). ★ **The redirect answer is untouched and both over-fix shapes are now pinned:** the
pre-existing *"the redirect still redirects"* control turned out to cover only the KIND shape — an AGE-shaped over-fix
left it GREEN — so a second control was added for an **expired** redirect row. ✅ **[[B170]] IS APPROVED AND CLOSED 2026-08-11, AND THE `s07` ROW IS LANDED** (`2ce470f9` / 108951 in the live
anchor table; all 36 rows were byte-identical to the pre-slice arm, so `s07` could not have moved, and an
independent re-measurement at `lus` `79b01d8a` reproduced it exactly). ⛔ **This sentence previously read *"the
condition is MET and reported as still the owner's call"* — accurate when written, false once the approval's
condition was verified and the row landed. No further owner ruling is needed.** ⛔ **No owner or QA approval is claimed for any figure in that note.**

### 1.15 [[B178]] — 728 REJECTED. Land trigger 2, DEFER trigger 1 as a conservative interim · ✅ **OWNER-RULED 2026-08-11**

★★★ **THE RULING, IN REPORTED FORM (⛔ no quotation — §3 rule 4):** **the owner REJECTED the 728 outcome and ruled
that §MH-S5b lands as OPTION (ii) — §8.3 trigger 2 plus items 2 and 3 — with trigger 1 DEFERRED under [[B178]].**

**What (ii) preserves, per the owner:** home-loss detection · searching after a **genuine missed response** ·
proper host-row refresh · bidirectionally verified switching · **737 deliveries, above the existing anchor**.

⚠⚠ **THE OWNER NAMED THE LIMITATION AND IT MUST NOT BE PRESENTED AS COMPLETION: a weak but CONSISTENTLY RESPONDING
home will not proactively initiate candidate verification, so the mobile changes home only AFTER connectivity begins
failing.** ⇒ **This is a CONSERVATIVE INTERIM POLICY, not completed proactive roaming.** ⛔ Do not record §8.3 as
satisfied, and do not let a later reader infer that §S6.4-C's *leave a weak home BEFORE loss* purpose is met.

⛔⛔ **THE BROAD FORM OF OPTION (iii) IS REJECTED AS TOO BROAD:** *weak home + any audible candidate* — in a dense
scenario virtually every mobile probably has an audible candidate, so it would **reproduce the same storm
unchanged.** ⇒ the audibility of a candidate is not evidence that switching is possible.

★★ **THE REFINED (iii) THE OWNER SPECIFIED, to be implemented and measured (reported form):** send a proactive
searching probe **only** when the home is **weak/critical** *and* there is at least one candidate that is **fresh**,
**compatible**, **passively observed**, **still unverified**, and whose **measured one-way quality could possibly
satisfy the two-tier improvement rule** — *and* the **candidate hold** and **anti-flap dwell** are already
satisfied. ⓘ The point of the extra terms is that a canvass is only spent where a switch could actually complete.

★ **THE RULED SEQUENCE:**
1. ✅ **LANDED 2026-08-11 by §MH-S5b-ii — option (ii): trigger 2 + items 2 and 3.** The `_presence_prescan` disjunct
   is gone from `Node::presence_searching_probe_due()`; items 2 and 3 untouched. **737 / `s06` 110 / `s07` 83 / raw
   763**, `s27` **0** assertion failures, 31/36 anchor-identical with the 5 movers re-attributed by in-tree A/B,
   `sizeof(Node)` **221880**, `kCap` **91**, native **1509/81105/0**. ★ The rebuilt `lus` md5 is **`1c0c63cb`**, the
   SAME binary §MH-S5b published for its *"trigger 2 only"* arm ⇒ the landed tree IS the arm that was priced.
   ⛔ **UNCOMMITTED (D4); the `^### 36/36 corpus` table was NOT edited; no approval beyond this ruling is claimed.**
2. ✅ **DONE in the same slice — the spec marks trigger 1 deferred under [[B178]] and explicitly does NOT record §8.3
   as satisfied**, carrying the named limitation. Also carried at the predicate + its declaration (`node_mobile.cpp` /
   `node.h`), `docs/protocol.md` (whose *"proactively re-home"* line was corrected in place), the bench script §18.2
   (rewritten in place to pin the deferral) and `BASELINE.md` §MH-S5b-ii.
3. ⚠ **fix [[B177]] SEPARATELY AND BEFORE step 4** — its erroneous beacon refresh **can alter the liveness and
   quality inputs that trigger 1 reads**, so measuring the refined (iii) on top of it would measure the wrong tree;
4. **implement and measure the refined (iii)**;
5. **keep the `≥733` floor**, and additionally require: **no increase in `presence_home_lost` in `s07`** ·
   **bounded roster airtime and peak-window collisions** · **no repeated canvass once a plausible candidate is
   verified**.

### 1.16 [[B177]] — beacons are HINTS ONLY; epoch-bearing P probes are the sole hosted-row authority · ✅ **OWNER-RULED 2026-08-11** · ✅ **IMPLEMENTED 2026-08-11 (§B177-FIX)**

★★★ **IMPLEMENTATION STATUS, added 2026-08-11 — all five ruled points landed, plus the adjacent site as option (a):**
1. ✅ the beacon → `mobile_reg_touch()` loop is **REMOVED** (`lib/core/node_beacon.cpp`, `if (b.is_mobile)` arm; the
   `_mobile_peer` write above it kept); 2. ✅ **both** P-probe arms are the sole authority, sharing ONE new predicate
`Node::host_row_probe_refreshable()` (`node.h`); 3. ✅ ⛔ **no epoch byte/TLV was added to the beacon**; 4. ✅ §9.1's
wording is corrected in place to *"validated registration probes"* with the beacon half **withdrawn with its reason**,
and `docs/protocol.md` carries the same correction; 5. ✅ the stale in-source §3-D rationale is **withdrawn in place**,
quoted so a reader cannot "restore" the touch. ★ **The adjacent SELECTED arm: option (a) — fixed here, and it is
measurably free: byte-identical to the pre-slice tree on all 36 corpus streams and delivery-neutral.**
⛔⛔ **AND ONE THING IS *NOT* SETTLED, AND IT IS THE OWNER'S: DELIVERY FALLS 737 → 732 (`s06` 110, `s07` 78), BELOW THE
`≥733` FLOOR. The beacon touch was NOT restored to recover it and no approval of 732 is claimed** — registered as
[[B179]] with three priced-but-untaken options. All of the −5 is `s07`'s and all of it is the beacon removal's, by
in-tree A/B; **zero DM deliveries reach a mobile in `s07` in either arm**, so it is a static↔static collision reshuffle,
not lost hosted-mobile service. Evidence: `simulation/BASELINE.md` §B177-FIX. ⛔ UNCOMMITTED (D4).


★★★ **THE RULING, IN REPORTED FORM (⛔ no quotation — §3 rule 4). The owner ruled that:**
1. **beacons are treated as presence/candidate HINTS ONLY**;
2. the **beacon → `mobile_reg_touch()` registry refresh is REMOVED**;
3. **epoch-bearing P probes become the SOLE ongoing authority** for hosted-row liveness and SNR refresh;
4. ⛔ **no epoch byte or TLV is added to every beacon** — that would spend **permanent airtime** to preserve a
   now-redundant mechanism;
5. **§9.1's wording changes** from *"mobile beacons/probes refresh `last_heard_ms`"* to **"validated registration
   probes refresh it."**

★ **WHY IT IS SAFE, per the owner: P checks run every ~1–8 minutes, well inside the 25-minute host expiry.** The
in-source comment claiming a **stationary** mobile needs its beacons to avoid expiry **predates that presence
mechanism** and is stale.

⛔⛔ **AND THE REGISTER'S PROPOSED ONE-LINE FIX WAS IMPOSSIBLE — this is why the ruling is a REMOVAL, not a gate.**
[[B177]] recorded *"the fix is the same one-line shape item 2 uses (`host_row_live_direct` + the low-byte epoch
match)"*. ★★ **A MOBILE BEACON CARRIES THE HASH BUT NOT THE REGISTRATION EPOCH** (BCN layout,
`lib/core/frame_codec.h`; verified at the code 2026-08-11) ⇒ that fix **cannot** deliver the epoch term it claims.
**Copying item 2's shape would have shipped a gate asserting a guarantee it does not hold** — the
instruments-that-cannot-fail class, this time inside a fix rather than a test. The sentence is withdrawn in place.

⚠⚠ **THE ADJACENT SITE THE OWNER REQUIRED BE HANDLED HONESTLY:** the **SELECTED**-probe arm
(`lib/core/node_join.cpp:804`) also finds the hosted row by **hash alone** and refreshes it with **no**
`host_row_live_direct()` and **no** `reg_epoch` check — `sel_me` only proves the probe names us as home. Since
§MH-S5b's *searching* arm carries both terms, **the two arms are inconsistent and the older one is weaker.**
⇒ **Either fix it inside B177 as the same hosted-row identity invariant, or register it separately — but ⛔ B177
may NOT be reported as closing stale-row refresh while it stands.**

★ **REQUIRED TESTS (owner-specified):** a beacon cannot refresh a **live**, **redirect**, **expired** or
**wrong-epoch** hosted row · a **correct-epoch** P probe refreshes a live direct row · **wrong-epoch, redirect and
expired rows are refreshed by NEITHER P-probe arm** · a beacon **still performs** its unrelated mobility/team/
candidate functions · corpus movement **measured and attributed**, with ⛔ **no [[B178]] trigger work bundled in.**

### 1.17 ★★ THE CANONICAL DELIVERY FLOOR IS `≥732`, PROVISIONAL PENDING [[B163]] · ✅ **OWNER-RULED 2026-08-11**

★★★ **THE RULING, IN REPORTED FORM (⛔ no quotation offered, per §3 rule 4). The owner took [[B179]] option (i) and
ruled:** ⓘ **PROVENANCE NOTE: the owner subsequently RESTATED this ruling and the reported form below matched it on
every point** — so this entry is confirmed against the owner's own restatement, not merely transcribed from one
reading. ⛔ **A quotation is still not offered**: §3 rule 4 says reported form unless the exact characters are held,
and that rule was broken twice in this arc by an agent that believed its paraphrase was exact.
1. **ACCEPT the correctness-driven 737 → 732 delivery movement**;
2. **RE-ANCHOR `s07`** (landed: `2ce470f9`/108951 → **`b3b7ce31`/107989**, the only row edited);
3. ★★ **the CURRENT CANONICAL FLOOR IS `≥732`, EXPLICITLY PROVISIONAL PENDING [[B163]]**;
4. ⛔ **DO NOT reinstate beacon authority**, and ⛔ **do NOT open a recovery slice solely to regain the five
   deliveries.**

★ **THE ATTRIBUTION THE OWNER REQUIRED ON RECORD, and it is what makes 732 acceptable rather than a regression:**
- the delta is **entirely attributable to the beacon-refresh removal** — measured both ways: the beacon-removal-only
  arm is identical to the landed arm on **all 36 streams** (732 / `s07` 78), while the **selected-arm fix alone is
  36/36 BYTE-IDENTICAL to the pre-slice tree and delivery-NEUTRAL** (737 / `s07` 83);
- it consists of a **static-to-static collision reshuffle** — 15 changed static↔static pair rows after the presence
  plane goes quieter (`presence_roster_tx` 185 → 148), with DM airtime *rising* slightly (667 393 → 670 107 ms);
- ★★ **it contains ZERO observed mobile-delivery delta: no DM delivery reaches a mobile in `s07` in EITHER arm.** ⛔⛔ **BUT READ WHAT THAT DOES AND DOES NOT PROVE — OWNER-CORRECTED 2026-08-11, and the first framing was TOO STRONG: because `s07` delivers ZERO DMs to mobiles in BOTH arms, this metric is BLIND to hosted-mobile service ENTIRELY.** ⇒ *"no mobile-delivery regression was observed"* is the accurate claim; **"hosted-mobile service is unharmed" is NOT** — the absence of a delta here is the absence of any signal, not evidence of health. ★★ **POSITIVE hosted-mobile service coverage rests on the NATIVE TESTS and the METAL GATE, not on this figure.** ⚠ This is the instruments-that-cannot-fail class applied to a delivery metric: a zero from an instrument that cannot see the thing is not a measurement of it.

⚠ **`≥732` is PROVISIONAL, not a new settled number.** [[B163]] is open — `s07` carries a *correct* leased-id alias
refusal whose shortfall is not derivable without a time-windowed alias map — and `s07` is where **both** the B163
question and this −5 live. ⇒ **Quote the floor with that caveat attached**, and ⛔ **do not present `≥732` as exact.**

ⓘ **Reading older records:** every slice note and brief written before 2026-08-11 states the floor as `≥733` (and
earlier ones `≥732`, retired by [[B162]] for being unreproducible — ⛔ **not the same 732**). Those are **accurate as
of their date** and are deliberately not rewritten. **This section is the canonical statement; a figure in a slice
record is history.**

### 1.18 ⛔⛔ THE `≥732` FLOOR IS FROZEN AS AN ACCEPTANCE GATE, PENDING [[B182]] · ✅ **OWNER-RULED 2026-08-12**

★★★ **THE RULING, IN REPORTED FORM (⛔ no quotation — §3 rule 4). The owner ruled:**
1. **FREEZE `≥732` as an acceptance gate NOW.** ⛔ **It is no longer authoritative for accepting or rejecting a
   slice.** Preserve it as **HISTORICAL**, and mark it **temporarily NON-AUTHORITATIVE pending [[B182]]**.
2. ⛔ **After the tool fix, RE-RUN — do NOT arithmetically adjust — every figure that is still a decision input:**
   the **current tree** · **pre-[[B177]]** · **beacon-removal-only** · **selected-arm-only** · **all [[B178]] trigger
   arms** · and **the [[B162]] / hybrid-RTS comparison ladder** if those figures remain decision inputs.
   ★ **The reason it must be a re-run and not an offset: the correction may affect arms DIFFERENTLY, because their
   attachment windows differ.** ⇒ A single delta applied to every arm would re-bake the error under a new name —
   the [[B162]] lesson exactly.

★★ **WHY: [[B182]] is a LOGICAL-IDENTITY defect on the hosted-mobile/static plane, and it is measurement-only.** The
decisive control is `s22`: **team mobile → team mobile is 4/4 and correctly attributed**, while **static ↔ hosted
mobile messages ARRIVE and the tool reports ZERO.** ⇒ So the tool is **not** blind to mobiles; it mis-attributes one
addressing path. ⛔ **Everything landed remains as landed** — the arms were compared against each other on one tool
revision, so their RELATIVE ordering stands. What is suspended is the **absolute** floor and what it certifies.

ⓘ **Supersedes §1.17's status, not its history.** §1.17 recorded `≥732` as the canonical floor, provisional pending
[[B163]]. That provisionality is now **stronger**: the floor is **frozen and non-authoritative** until B182 lands and
the arms are re-run. ⛔ **Do not quote `≥732` as a gate in any brief until then.**

### 1.19 The 1-hour rolling duty window STAYS; `s07` saturation is EXPECTED · ✅ **OWNER-RULED 2026-08-12**

★★★ **THE RULING, IN REPORTED FORM (⛔ no quotation — §3 rule 4). The owner ruled:**
1. **RETAIN the 1-hour rolling duty window.**
2. **Treat `s07` saturation as VALID STRESS BEHAVIOUR** — ⛔ **do NOT retune its window or its load merely to restore
   delivery.**
3. **RECLASSIFY [[B187]] as EXPECTED SATURATION** (not a defect).
4. **Register the ROLLING-WINDOW-BOUNDARY coverage gap SEPARATELY**, and test it with a **compressed explicit
   window**.
5. **Correct the stale [[B183]] documentation, then proceed with [[B186]].**
6. ⛔⛔ **NO `s07` RE-ANCHOR IS AUTHORISED BY THIS RULING.**

★★ **WHAT THIS SETTLES, and why the original B187 wording would have caused harm:** the measurement behind it was
that **36 of 36 corpus scenarios are in the ONE-SHOT duty regime** — not one sets `duty_cycle_window_ms`, so all
inherit the 1 h default and every duration is ≤ 1 h. ⇒ B187 was first worded as *"`s07`'s duty window equals its own
`duration_ms`"*, which reads as an `s07` authoring defect. It is not: a 1 h scenario seeing exactly one window is
**correct modelling** of a 1 %-of-rolling-hour limit. ⛔ **Shortening `s07`'s window to force rolling behaviour would
have made the scenario LESS faithful, moved an anchored row, and inflated delivery for no protocol reason.**

⇒ ★ **The consequence for [[B183]]: duty exhaustion in `s07` is a LEGITIMATE STRESS CONDITION, not the defect.** The
defect B183 located is purely that the **core mishandles a busy-refused, `FrameTag::beacon`-tagged J frame** — no
retry, no attributable emit, while `mobile_offer_tx` has already fired. **Any B183 document that presents the duty
window as the cause or trigger is stale and must be corrected in place.**

ⓘ **The separately-registered gap is [[B188]]**: the rolling window's boundary behaviour is **corpus-dark** — never
exercised anywhere — and is to be tested with a **compressed explicit `duty_cycle_window_ms`**, not by lengthening a
scenario or shortening `s07`'s.

### 1.20 The bounded B186a / HAL-audit / B188 slice · ✅ **OWNER-RULED 2026-08-12**

★★★ **THE RULING, IN REPORTED FORM (⛔ no quotation — §3 rule 4). THE OBJECTIVE, in the owner's framing: HONEST TX
OUTCOMES AND EVIDENCE FOR RECOVERY — ⛔ NOT forcing `s07` to reattach, and ⛔ NOT creating a generic retry
mechanism.** Bounded: ⛔ **do not attempt to "fix `s07`" and do not broadly redesign mobile attachment.**

**1 · [[B186a]] OBSERVABILITY FIRST.** Give mobile **DISCOVER, OFFER, initial CLAIM and re-CLAIM distinct internal TX
identities** · ⛔ **no wire-format change** · ⛔ **no generic beacon retries** · **ordinary beacons and floods
unchanged** · ★★ **capture initial-CLAIM versus re-CLAIM BEFORE TX HANDOFF — do NOT reconstruct it after FSM state has
changed** · on asynchronous TX refusal **report the exact mobile operation, reason, SF and `busy_until`** ·
ⓘ **`BusyInfo` has `tag` and SF but NOT length — do NOT add length plumbing in this slice** · **positive AND negative
tests for every subtype, INCLUDING telemetry-disabled compilation.**

**2 · AUDIT REACHABILITY BEFORE [[B186b]].** Enumerate **every** HAL implementation and establish whether it can
**accept a frame and later invoke `on_radio_busy`**. **Distinguish simulator-only behaviour from behaviour reachable on
physical boards.** ⛔ **Record evidence, not assumptions.**

**3 · [[B188]] AS A SEPARATE COVERAGE SLICE.** A **purpose-built fixture with an explicitly compressed
`duty_cycle_window_ms`**, verifying **refusal at exhaustion · correct `busy_until` · incremental rolling expiry ·
resumed transmission once budget is available.** ⛔ **Do NOT shorten `s07`'s duty window and do NOT lengthen the
general corpus merely to exercise rollover.**

**4 · ⛔ DO NOT IMPLEMENT [[B186b]] BEHAVIOURAL RECOVERY YET.** After B186a and the HAL audit, **report**: which paths
are **simulator-only** and which are **hardware-reachable** · **corpus movement caused ONLY by new diagnostics** · and
**a minimal recovery proposal per reachable mobile operation.**

**5 · PRESERVED RULINGS.** `s07` saturation is **legitimate stress behaviour, not a defect** · ⛔ **do not change
`s07` load/window or re-anchor it in this slice** · **the delivery floor remains FROZEN/UNRATIFIED** · **[[B187]]
stands as expected saturation/reframed, and [[B188]] owns the missing rolling-window coverage.**

### 1.21 The mobile-home investigation STOPS; return to the OLED plan · ✅ **OWNER-RULED 2026-08-13**

★★★ **THE RULING, IN REPORTED FORM (⛔ no quotation — §3 rule 4). The owner ruled:**
1. **STOP the mobile-home investigation.** ⛔ **Do NOT implement [[B186b]] yet** — it stays OPEN and unimplemented,
   with its minimal recovery proposals recorded as proposals only.
2. **RETURN to the Heltec OLED plan**, with the **inbox detail/delete slice**, followed by **settings dirty/save**.
3. ★★ **KEEP [[B164]] AND [[B189]] AS A MANDATORY GATE** before **on-device registration / team onboarding** and
   before **final Phase-A acceptance.**

★★ **WHY ITEM 3 MATTERS AND MUST NOT BE FORGOTTEN WHEN THE OLED WORK LOOKS DONE:** both are *unobservability*
findings, and both bear directly on the feature the OLED UI is being built to drive.
- **[[B164]]** — admission truth is exact, but **actual on-air completion remains unobservable after a
  post-admission rejection**. ⇒ A UI that reports a send as done can be reporting an admission, not a transmission.
- **[[B189]]** — ⛔ **`Node::on_radio_busy` has NO caller on hardware at all** (whole-tree evidence, §B186a's HAL
  audit), so the async busy path **and the entire R4.5b stash-retry machinery are SIMULATOR-ONLY**; conversely the
  synchronous `DeviceHal` refusal is **corpus-dark** (`tx_hal_rejected`/`tx_failed`/`oversized` = 0 across all 36
  streams). ⇒ **Each shape exists in exactly one implementation and neither is proven for the other. No hardware run
  has been done.**
⇒ ★ **Together they mean on-device registration and team onboarding would be driven by TX outcomes whose truth on
metal is unestablished.** That is why they gate, not merely accompany, those features.

ⓘ **State at the stop:** B186a is IMPLEMENTED (four distinct internal TX identities; `mobile_reclaim = 6`; the
enum-switch guard proved to bite at two sites) · native **1515 / 81320 / 0** · `lus` **`43a7b6eb`** ·
`sizeof(Node)` **221880** (D2 negative) · [[B188]]'s non-corpus fixture at **39 verifier checks / 37 controls, all
mutation-proved** · the delivery floor remains **FROZEN / UNRATIFIED** · **`s07` untouched and un-re-anchored.**

### 1.22 [[B192]] — RELOAD is the three-way merge · ✅ **OWNER-RULED 2026-08-13**

★★★ **THE RULING, IN REPORTED FORM (⛔ no quotation — §3 rule 4). The owner approved [[B192]] as implemented:**
**RELOAD performs the three-way merge.**
- **fields UNCHANGED in the OLED draft ADOPT the current persisted values**;
- **fields EDITED in the OLED draft REMAIN UNSAVED in the draft**;
- **DISCARD remains the explicit FULL RESET.**

★★ **WHY IT MATTERS — this is the conflict escape hatch, and the alternative was forbidden.** §3.6.1 defines DISCARD
and NAMES RELOAD without defining it, so the semantics were genuinely open. ⇒ **Reinstating the whole draft over a
conflicting external write would have resurrected LAST-WRITER-WINS, which §3.6.1 explicitly forbids because it
silently overwrites companion changes.** The merge keeps the operator's deliberate edits while accepting everything
they did not touch — so a serial/BLE writer's changes survive unless the operator has explicitly overridden that
field.

ⓘ **Provenance, stated because this arc has five incidents:** the merge was **implemented first** (§UI-13) and
**recorded as a QG RECOMMENDATION, explicitly NOT an owner ruling**, through several review rounds. **It is now
ruled.** ⇒ ⛔ **Every in-source, register and spec label reading *"QG recommends"* / *"not ruled"* / *"awaiting an
owner decision"* for B192 is now FALSE and must be corrected in place** — §3 rule 3, and the class-4 defect this arc
has hit twelve-plus times.

⚠ **What this does NOT settle:** RELOAD's *behaviour* is ruled; its **NV/power-cut qualification is still deferred**
to the UI-14 device binding and [[B193]]. §UI-13 proves the logic against a counting/failing **fake** store — no NVS
or LittleFS write, no wear, no reset-during-write. **A green suite says the logic is right, never that the storage
is.**

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
| **§MH-S5b's delivery cost** — [[B178]] | ✅ **RULED AND LANDED 2026-08-11 (§1.15 + §MH-S5b-ii): option (ii). ⛔ This row previously read *"owner decision OWED … no approval of any figure or option is claimed"*; corrected in place (§3 rule 3), and the description below is kept as the record of what was priced.** Trigger 1 is **DEFERRED**; trigger 2 + items 2 and 3 landed at **737 / `s06` 110 / `s07` 83**, `s27` green, `lus` `1c0c63cb`. ⚠ **What is STILL OPEN is the REFINED trigger 1, sequenced AFTER [[B177]]** — and the interim limitation (a weak but consistently responding home is never canvassed) is **recorded, not repaired**. ⓘ **Two NEW measurements from the landing, recorded not acted on:** dropping **item 2** would score **739 with all 36 rows green** (⛔ refused — it is a correctness term the ruling preserves, and it would void the §0 hazard control), and **item 3 is NO LONGER byte-inert** (it moves `s27`), so §MH-S5b's *"footprint is zero"* was a property of that arm, not of item 3. ⓘ Historical description follows. (was: **NEW 2026-08-11, owner decision OWED. ⛔ No approval of any figure or option is claimed.** §8.3's trigger 1 (a searching probe when the home's reported quality is weak or critical — a trigger the spec itself permits) is measured to cost **6 unique deliveries, all inside `s07`**: 734 → **728** overall, `s06` **110** unchanged, total PHY airtime **−0.51 %**. Per-item in-tree A/B attributes the whole −6 to that trigger: trigger 2 alone measures **737** (above the anchor), item 2 alone **734**, and item 3 is **byte-inert on all 36 rows in both contexts tested**. ⚠ **The arm that scores highest (739) is INADMISSIBLE** — it disables trigger 1 while keeping the verified-echo requirement, and `s27` then returns **3 scenario assertion failures** because M5's re-home never happens. ⇒ five corpus rows moved and are attributed; **the `^### 36/36 corpus` table was NOT edited.**) ⓘ **END of the historical description.** | ✅ **Option (ii) TAKEN and landed.** ⛔ The *"a candidate is audible"* narrowing was **refused as too broad**; the refined form and its acceptance criteria live in [[B178]], **after [[B177]]**. ⚠ `≥733` is met but was already conditional on [[B163]]. |
| **The `≥733` delivery floor** — [[B179]] | ⛔⛔ **NEW 2026-08-11, owner decision OWED. ⛔ No approval of 732 is claimed.** §B177-FIX implements ruling §1.16 in full and delivery lands at **732 / `s06` 110 / `s07` 78** (raw 757, `lus` `316b9cb1`) — **below `≥733`**, which was already conditional on [[B163]]. In-tree A/B attributes the whole −5 and the single moved corpus row (`s07`, `e73be070`/107963 → `b3b7ce31`/107989) to the **beacon removal**; the selected-arm half is **byte-identical to the pre-slice tree on all 36 streams** and delivery-neutral. ⛔ **The beacon touch was NOT restored to recover the 5 — the brief named that an owner decision.** ★ What the loss is *not*: hosted-mobile service — **zero DM deliveries reach a mobile in `s07` in EITHER arm** — it is **15 changed static↔static pair rows netting −5** after the presence plane quiets (`presence_roster_tx` 185→148) and DM airtime rises (667 393→670 107 ms). ⓘ My churn-loop hypothesis was **refuted at the stream** (`presence_epoch_mismatch` = 0 in both arms). | Three options priced in [[B179]], ⛔ **none taken and none recommended**: accept-and-re-anchor · treat the 5 as a `s07` artefact pending [[B163]] · recover it *without* a hash-only registry refresh (undesigned, unmeasured, a new slice). |
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
4. ★★★ **NEVER PUT AN OWNER RULING IN QUOTATION MARKS UNLESS THE EXACT CHARACTERS ARE HELD — record it in REPORTED
   form** (*"the owner ruled that X"*). **Added 2026-08-10** after §1.12's wording was disowned and §1.13 repeated the
   same defect **in the very next entry written**. ⚠ The lesson is not carelessness: the QA-gate **could not tell its
   own paraphrase from a transcript**. A reconstruction in quotes is indistinguishable from an invention, *including to
   its author*.
5. ★★★ **A QA RECOMMENDATION RELAYED BY THE OWNER IS STILL A RECOMMENDATION. The channel it arrives on does not
   promote it.** **Added 2026-08-10**, the fifth provenance incident and a new failure mode — the opposite of rule 1's:
   not *"claiming an approval nobody gave"* but **silently upgrading a relayed review recommendation into an owner
   ruling**, because it arrived inside an owner message. It hit **five** sites in one micro-slice (the `[[maybe_unused]]`
   fix form, its no-move-into-`MR_EMIT` constraint, the five gate conditions, [[B157]]'s wording, and the N=2
   acceptance), all corrected in place. ⇒ **Label the source, not the messenger: `QA-recommended` / `QG acceptance
   condition` when a reviewer proposed it; `owner-ruled` ONLY when the owner decided it themselves.** ⓘ When both are
   in play, say so separately — e.g. the **name** `local_admitted` is genuinely owner-ruled (§1.12) while the
   `[[maybe_unused]]` **form** that carries it is QA-recommended.

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
