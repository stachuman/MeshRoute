<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# REFINED DRAFT — shared DM/channel APP envelopes and the B118 "requires answer" binding · 2026-08-05

**Status: REFINED DESIGN PROPOSAL. ⛔ NOT YET APPROVED TO IMPLEMENT.** The codec, encrypted-channel, inbox and
companion seams have now been audited. Two §6 items are settled by owner ruling; the remaining product choices carry
explicit recommendations rather than placeholders. Origin: owner ruling 2026-08-05 parking **B116**
(`HAVE`-digest-as-delivery-evidence) in favour of an explicit application-level marker.

> ★★ **THE REFINEMENT'S CENTRAL RULE:** `DataType`, channel framing and `app_code` are three different layers.
> **Do not spend one namespace as if it were another.** A transport-specific marker says *"an application envelope
> follows"*; the envelope itself is the SAME on DM and channel paths:
> ```text
> [app_code u8][meta_len u8][metadata meta_len bytes][UTF-8 text: all remaining bytes]
> ```
> `meta_len` is what makes information-before-text safe: a receiver can locate the text even when it does not know
> the code's metadata schema. Metadata and text remain together in this canonical raw envelope for live and durable
> storage; JSON and the OLED expose only the decoded text as `body`.

> ★ **ONE SHARED CODE REGISTRY:** `0` is reserved/invalid; `1..127` are DM application codes; `128..255` are
> channel application codes. The high bit classifies the code **only after an explicit transport marker established
> that an envelope exists**. It is never a presence test against arbitrary user text.

> ★★ **OWNER RULING 2026-08-05 — §6 DECISION 2 IS SETTLED: the binding references the CHANNEL MESSAGE, not the sender.**
> *"Agree — referring to channel msg makes more sense; we will refine that separately."*
> ⇒ **§4's recommendation is ADOPTED.** A reply carries the **original post's `channel_msg_id`**; ⛔ **the
> "matching sender" formulation from the original sketch is WITHDRAWN and must not be reintroduced** — it has no carrier
> on a plaintext M frame (§4), and it cannot distinguish two alarms from the same hiker.
> ⓘ **§6 decision 3 is settled too** — `128` is the first code and the audit confirms 0–127 is free — **but its
> `≥128`-classifies-it corollary is NOT** (§3). Both rows are marked SETTLED **in §6 itself**, not only here.

> ⚠ **EVERYTHING ELSE REMAINS UNRULED, AND THIS REFINEMENT IS NOT APPROVAL TO BUILD.** Sections 3.1–3.4 and 5.4–5.6
> now give one coherent recommendation for transport markers, the shared DM/channel envelope, code allocation,
> responder display identity, durable storage, first-slice scope, request lifetime and plaintext handling. They are
> concrete so the owner can rule on a buildable shape, not so a coder may treat them as settled.
> ⛔⛔ **AND THE MATCHING RULE IS NOT RULED EITHER.** §5.1's six-condition floor is **independent QA's proposal of
> 2026-08-06, recorded as PROPOSED, pending the owner.** The three-clause rule it replaces was **forgeable** and is
> withdrawn in place — read §5.1 before quoting any matcher out of this document or out of the ledger's §1.7.

⚠ **Revision note, kept rather than smoothed over (§3's in-place rule).** An earlier revision carried the "settled"
banner above while **§6's body still asked the same questions** — the *"prepended a correction instead of replacing
it"* defect, and a reader acts on the body. The body is corrected as of 2026-08-06; the banner is no longer the only
place a decision is recorded.

★ **Why this is a spec and not a bug-log entry.** The idea reads as one byte, but the codec audit found **two facts that
break it as stated** — the `≥128` discriminator is ambiguous against UTF-8, and **the plaintext M frame has no sender
field at all**, so the matching half has no carrier. Either one changes the design; together they need a format
decision, an identity decision and a matching rule. That is spec work. **The register keeps the one-line pointer.**

---
## §1 — The problem, stated as a measurement

`§4.4`'s distress-reply scope has **no positive carrier**. It currently infers *"someone answered"* from **"a channel
post arrived on our team channel"**. Two consequences, both measured this week:
- **A stranger nearly counted.** Any plaintext channel-0 post from any node in range read as a distress REPLY until F4
  added `same_team()` (§B103). The predicate is now safe but still **inferential** — it recognises *traffic*, not *an
  answer*.
- **A real answer did not count.** On the owner's bench the team received all three emergency posts and replied — by
  **DM**. Owner ruled 2026-08-05 that **a DM must never confirm an emergency** (§B114 ②), so that path is closed *by
  design*. ⇒ **the only remaining honest confirmation is a channel post that says, unambiguously, "this answers that".**

⇒ **Goal: a channel message can declare it wants an answer, and a later authenticated team-channel message can be
bound to that exact request rather than inferred from unrelated traffic.**
★ **Trust bound:** a valid sealed answer proves that a producer holding the shared team content key emitted an
explicit answer frame. It does **not** cryptographically prove a particular person pressed a button; every keyholder
can impersonate another member. The honest UI claim is `TEAM ANSWER RECEIVED` / `REPLY`, not an attested human identity.
⛔ **Non-goal:** delivery/receipt evidence. `HAVE` proves a node *holds* a post; it never proves an explicit answer was
emitted. That is why B116 is parked, and this design must not quietly re-acquire that ambiguity.

---
## §2 — Wire cost is settled

**Owner-confirmed three times (2026-07-31, 2026-08-01, 2026-08-05): MeshRoute is NOT DEPLOYED. Wire changes are FREE.**
⇒ **Do not contort this design to fit a spare bit** — that is exactly how the DATA flags byte (`0xFF`) and `q_opcode`
(2 bits) both reached exhaustion. Pick the right shape.
★ The one residual cost is **attribution**: a `wire_version` bump re-anchors all 36 corpus streams at once, so **if** one
is needed it takes **its own slice/commit** (C4). Never conflate that with reflash cost.

---
## §3 — Audit finding ①: `≥ 128` alone is NOT a valid discriminator

**Measured against the codec, not read from docs.** The M payload has **no app-code byte today** — on a plaintext post
`payload[0]` is the first byte of **user text**. And *"a code is ≥ 128"* discriminates only against **7-bit ASCII**:
**UTF-8 lead bytes `0xC2`–`0xF4` are all ≥ 128**, so any post beginning with a non-ASCII character (`Ó`, `€`, an emoji)
would be misread as an app code.

**Proposed resolution — an explicit presence bit, with the owner's 128+ range kept as a second line of defence:**
- **A presence bit** (the audit reports **four free `flavor` bits** and **five free sealed-inner bits**) declares
  *"this payload begins with an app-code byte"*. **The bit is the authority — never a value test on `payload[0]`.**
- **The code itself stays in the owner's `128`+ range.** With a presence bit the code could use the full 0–255, but
  keeping `≥128` is worth it: a receiver seeing the bit set and a code `<128` knows the frame is **malformed** and can
  refuse loudly (C2) instead of guessing. ⓘ Two things the range still buys, both unaffected by the finding above:
  0–127 stays available for any future non-channel app codes, and the space has **128 codes of headroom**.
  ⛔ **What it does NOT buy, corrected in place** — an earlier revision of this bullet ended *"`128 = 0x80` also means
  the high bit alone classifies a code"*, i.e. **this section asserted the very claim its own heading refutes.**
  **WITHDRAWN.** The high bit classifies nothing on the plaintext path: `payload[0]` is user text and UTF-8 lead bytes
  are ≥ `0x80`. **The presence bit is the authority; `≥128` is a malformed-frame tripwire and nothing more.**
- ★ **Recommended placement:** keep one APPLICATION envelope but use the marker that is trustworthy on each transport.
  A plaintext M uses a free outer `flavor` bit; a sealed M uses a free bit inside the authenticated sealed-inner flags.
  The difference is deliberate. The present channel AAD binds team-key hash, message id and channel id — **not the
  outer `flavor` byte** (`node_channel.cpp:504-519`) — so using only an outer app bit on a sealed post would let a
  non-keyholder flip the semantic marker. Consistency at the application boundary must not weaken authenticity.

### §3.1 — One envelope, three explicit transport carriers

| transport | envelope-presence carrier | where the envelope sits | v1 validation |
|---|---|---|---|
| ordinary DM | `DATA_TYPE_APP_MESSAGE = 0x05` (RESERVED by §CUSTODY-A Slice A, 2026-08-29 — the enum member exists with no behaviour; was "new … = 21") | DM body | code must be `1..127` |
| plaintext channel M | new `channel_flavor_app = 0x20` | M body | code must be `128..255` |
| sealed channel M | new `channel_inner_flag_app = 0x08` | after the existing sealed-inner fixed fields, in the current text/body region | code must be `128..255`; outer `channel_flavor_app` must be clear |

⛔ **CORRECTED 2026-08-29 (§CUSTODY-A) — the withdrawn paragraph read:** *"`21` is proposed deliberately: live
`DataType` values occupy `1..19`, and the parked B59 notice spec reserves `20` for `DATA_TYPE_CUSTODY_FAILURE`."*
**`0x05` is ALLOCATED, not proposed.** The namespace transition put application-bearing types in `0x01..0x7F` and
reserved `0x05` for this design, so the app-code work appends behaviour to an existing member rather than claiming
a number. `DATA_TYPE_CUSTODY_FAILURE` is likewise reserved at `0x81`, not 20. **`DATA_TYPE_APP_MESSAGE` is only a
transport wrapper** — it never means app code 5, and it is not exposed to users as the application operation.
Until this design lands it is `known = false` in `data_type_traits()`, so it takes the application range's unknown
behaviour; implementing it means adding it to the known-application set in the same slice. `DATA_FLAG_APP` remains
derived from a non-zero `DataType`, exactly as today. (`DATA_TYPE_SEALED_RELAY` is now `0x03`; the enclosed-type
field it must carry per the section below is `0` or `0x05`.)

★ The sealed channel's inner bit is authenticated because it is in the AEAD plaintext. The outer bit is correct for
plaintext because there is no stronger authenticity to preserve there. A sealed frame with the outer app bit set, or
with outer/inner markers in any other contradictory combination, is malformed content: log/drop the content but keep
the existing content-blind relay behaviour.

### §3.2 — The common application envelope

```text
offset  size       field
0       1          app_code
1       1          meta_len
2       meta_len   code-specific metadata
2+N     remainder  UTF-8 display text (may be empty only when that code permits it)
```

All multi-byte integers inside metadata are **little-endian**, matching DATA inner fields and the inbox record.
`meta_len` covers metadata only; no text length is needed because text is the remaining body. The generic parser
performs four checks before any code-specific handler runs:

1. at least two envelope bytes exist;
2. `2 + meta_len <= payload_len`;
3. the code belongs to the transport's half of the registry;
4. the selected code's minimum/exact v1 metadata shape and text rule are satisfied.

Malformed envelopes are fail-loud and are never inboxed as raw binary text. An **unknown but structurally valid** code
is different: retain and surface its numeric code, raw metadata and decoded text, but trigger no firmware/UI action.
The sealed-inner unknown-bit precedent cannot be copied blindly here: an unknown inner bit prevents locating later
fields, whereas `meta_len` lets an older application-envelope reader locate text safely.

### §3.3 — DMs use the same model; they do not use the channel code range

A structured DM sets `type = DATA_TYPE_APP_MESSAGE` and places the same envelope in its body. A normal DM remains
`type = 0` with bare text and is byte-identical. On a sealed DM the generic type byte remains visible, but the exact
`app_code`, metadata and text are inside the seal; on a plaintext DM all are visible.

Existing plain/same-layer mobile delegation and plaintext cross-layer paths already thread an enclosed `DataType`.
Encrypted cross-layer DMs are the exception: `DATA_TYPE_SEALED_RELAY` currently spends the only outer type and
delivers only the opened body, so an enclosed application type would be lost. Before claiming full transport parity,
the sealed-relay plaintext must carry an authenticated `enclosed_type` (0 or `DATA_TYPE_APP_MESSAGE`) and return it on
open. Until that codec change lands, **an encrypted cross-layer app DM must refuse `unsealable`**, never arrive as an
ordinary text DM. This is the same structural rule already applied to typed team-key grants.

DM codes `1..127` are deliberately unable to confirm B118. A DM carrying a channel code (`>=128`) is malformed, which
pins the owner's §1.1 ruling in the format itself: a direct DM can never become an emergency answer through a shared
decoder accident.

### §3.4 — Cost and limits

The envelope costs two bytes plus its metadata. A same-layer structured DM additionally spends the existing one-byte
`DataType` header; a channel post spends a previously free bit, not a body byte. No global `dm_max_body_bytes` or
`channel_msg_max_payload_bytes` constant shrinks: the structured-send builder checks the actual envelope against the
existing packer limit and refuses `too_large`. Ordinary text limits and ordinary wire bytes remain unchanged.

---
## §4 — Audit finding ②: the plaintext M frame has NO SENDER, so "matching sender" is unimplementable

**This is the finding that changes the design.** The owner's sketch matches a reply by **"the original sender + the
flag"**. But a stable sender identity exists **only on the SEALED path**. On a plaintext post there is only `origin` —
a node/local id that is **not stable** across DAD or rejoin, and that is **plane-dependent** (a team local id is not a
static `node_id`; conflating them is the C3 violation this project has fixed repeatedly).

### ★★ Recommendation: bind to the ORIGINAL POST, not to its sender
Carry the **original post's `channel_msg_id`** in the reply instead of a sender identity.

| | sender-matching (as sketched) | **`channel_msg_id`-matching (proposed)** |
|---|---|---|
| carrier exists today | ⛔ sealed path only | ✅ **already on the wire** — 32-bit, the app's dedup identity (`pu.channel_msg_id`) |
| stable across DAD/rejoin | ⛔ no | ✅ yes — minted per post |
| plane-safe (C3) | ⚠ needs care | ✅ no id-space question at all |
| answers *which* post? | ⛔ no — only *who* sent something | ✅ **yes** |

★ **It is strictly stronger than what was asked for.** "Sender + code" cannot distinguish *two* emergencies from the
same hiker; `channel_msg_id` answers **which** alarm was acknowledged — precisely what a panel claiming `PICKED UP`
must know. It also needs **no new sender field**, so the wire delta is one code byte plus one 32-bit reference.
★ **Recommendation:** carry the existing sealed-inner `source_hash` on every structured sealed post and retain it
for display/lookup. The **binding** does not need it, and the shared team key authenticates membership rather than a
particular human, so the matcher must remain independent of this display identity.

---
## §5 — Sketch of the mechanism (subject to §3/§4 rulings)

1. **Request — proposed allocation `0x80 CHANNEL_REQUEST_ANSWER`.** The v1 metadata is exactly:
   ```text
   [schema=1 u8][request_kind u8][request_token u64 LE]
   ```
   with `request_kind=1` meaning `DISTRESS`. `request_token` is fresh, non-zero CSPRNG output minted once per alarm
   generation. The envelope text is the human-readable post. Keeping the action (`requires answer`) in the code and
   the reason (`distress`) in metadata avoids allocating a new request/answer pair for every future catalog message.
2. **Answer — proposed allocation `0x81 CHANNEL_ANSWER`.** The v1 metadata is exactly:
   ```text
   [schema=1 u8][request_code=0x80 u8][referenced_channel_msg_id u32 LE]
   [request_token u64 LE][answer_kind u8]
   ```
   with `answer_kind=0` meaning `ACKNOWLEDGED`. The text is optional free-form detail. The explicit request code
   prevents a future answer family from aliasing this one; the id implements the owner-settled reference and the
   echoed token binds it to the alarm generation even after the 8-bit id component wraps.
   An unknown or unallocated `answer_kind` is retained/displayed but never credits the emergency state; only a
   registered semantic value may drive an action.
3. **Retries.** Each of the emergency tracker's three channel originations mints a different `channel_msg_id`, while
   all three carry the same per-alarm `request_token`. The accepted ids are aliases of the same alarm generation and
   remain in one bounded request record. A valid answer to any live `(id, token)` pair answers that alarm; it does not
   matter which retry the teammate heard.
4. **Responder identity.** Every sealed structured channel post sets the existing authenticated-inner
   `channel_inner_flag_source`, even without location. The live push and durable record retain that `sender_hash` for
   display/lookup. This is useful attribution, not individual authentication: the team content key is shared, so any
   keyholder can claim another member's hash. The binding and matcher never depend on this display field.
5. **Match — see §5.1. The three-clause rule this step used to state is WITHDRAWN as forgeable.**
6. **UI.** Only then may the panel show `REPLY` / `TEAM ANSWER RECEIVED`. ⇒ §4.4 stops inferring from traffic.
   ★ The measured fact is *"a holder of the team content key emitted an explicit answer bound to this request"*.
   It is not proof of which person acted. This keeps §2.1 honest: a state asserting a physical/protocol fact must be
   reachable only from a path that established that exact fact. See §5.2 for unsealed messages.

### §5.1 ★★ THE MATCHER — SECURITY-CRITICAL. **PROPOSED (independent QA, 2026-08-06); ⛔ NOT RULED — the owner has not seen it.**

⛔⛔ **THE EARLIER THREE-CLAUSE RULE IS WITHDRAWN, quoted here only so the correction is auditable:**
> ⛔ **WITHDRAWN:** *"A receiver credits an answer only when all hold: the reply is on the **same team**
> (`same_team()`, §B103's guard — unchanged), its code is an **answer** code, and its referenced `channel_msg_id`
> **equals a post this node originated and is still tracking**."*

**Why it is withdrawn — it is FORGEABLE, and it reintroduces one layer up the exact class F4 had already closed.**
Verified in source, not read from comments (V1):
- `same_team(t)` is `lib/core/node.h:274` — `return _cfg.team_id != 0 && t == _cfg.team_id;` — **a plain comparison of
  the CLEAR `team_id` carried on the M frame. It authenticates NOTHING.** It is a *scoping* predicate, exactly as
  §B103/F4 shipped it, and it was never load-bearing for authenticity.
- `channel_msg_id` is minted at `lib/core/node_channel.cpp:53-58` as
  `origin<<24 | (key_hash32 & 0xffff)<<8 | (ctr & 0xff)` — **the counter component is only 8 BITS.**
⇒ **any radio peer in range can transmit a plaintext team frame carrying an OBSERVED team id and an OBSERVED message
reference**, and the panel would render it as an authenticated team answer. Both ingredients are visible on the air
to anyone who heard the original post. **That is the §2.1 false-confirmation class the whole UI arc exists to prevent.**

**★ THE PROPOSED FLOOR — ALL SIX must hold before an answer is credited:**

| # | condition | why it is not droppable |
|---|---|---|
| ① | the post arrived **SEALED and was successfully OPENED** | the **only** condition in this list that authenticates anything: opening proves the frame producer held the shared team content key. It does not authenticate a particular person. Everything else is observable to a passive eavesdropper. |
| ② | **matching team** (`same_team()`) | scoping, as F4 shipped it — keeps a foreign team's post out. **Not** authentication. |
| ③ | **matching channel id** | scoping — the team channel, not some other channel the node also hears. |
| ④ | the code is an **ANSWER** code (§6 item 3) | a request must never credit itself, and a reserved/unknown code must refuse loudly (C2), not be treated as an answer. |
| ⑤ | the referenced `channel_msg_id` and echoed `request_token` match one pair **THIS node originated and TRACKS** | the id honours the §4 ruling; the random token binds the id to this alarm generation after the 8-bit component wraps. |
| ⑥ | the tracked request is **LIVE and UNEXPIRED** (§5.3) | bounds retained state and the replay window; expiry remains mandatory even with a generation token. |

ⓘ **① has a carrier ALREADY, measured — do not design a new one, and do not trust the header comment.** The
`channel_recv` push sets `pu.enc = (enc != 0)` at `lib/core/node_channel.cpp:415`, meaning *"arrived CRYPTED and we
OPENED it"* (§chan-crypt CL2a), and only a **readable** post produces a push at all. ⚠ `lib/core/command.h:302` still
documents `enc` as *"channel_recv -> false (cleartext today)"* — **that comment is STALE and states the reverse of the
live code**; registered as **[[B119]]**, not fixed here (this slice is documentation-only).

### §5.2 ★ IF PLAINTEXT ANSWERS ARE SUPPORTED AT ALL, THE UI MAY NOT CALL THEM CERTAIN

An unsealed answer satisfies ②–⑥ and **fails ①**, so it is exactly as trustworthy as the frame it rode in on: **not at
all.** ⇒ **a plaintext answer may INFORM, never CONFIRM.** It may be stored as *"a plaintext post references your
alarm"*, but it must **never** reach a state claiming an authenticated team answer.
★ **State the obligation the way §2.1 states it:** the display-shaped claim `REPLY` / `TEAM ANSWER RECEIVED` — today
`Emergency::reply` (`src/firmware_ui_model.h:247`, entered only via `on_reply`) — **must be reachable ONLY from the
authenticated path, i.e. only when ① holds.** Even there the trust bound is a team-key holder, not a particular
person. ⓘ Do not conflate this with `PICKED UP`, which is a **different**
measurement: `Emergency::picked_up` is entered from `channel_relayed` (a relay of our own post was overheard, §B69), and
it is a LOCAL send outcome, not a reply. Two states, two evidence sources — keep them apart.
★ **Recommendation:** the generic envelope codec accepts both plaintext and sealed channel posts, because future
low-trust application codes may legitimately use plaintext. B118's `CHANNEL_REQUEST_ANSWER` / `CHANNEL_ANSWER`
handlers, however, have **sealed-only effects**: an unsealed instance is retained/displayed as an untrusted structured
message and triggers no emergency transition. Do not add a second hedged emergency state in the first slice.

### §5.3 ⛔ EXPIRY / REPLAY IS A HARD REQUIREMENT — *"still tracking"* is too soft to implement against

**Measured, not assumed:** `channel_msg_id_mint` (`lib/core/node_channel.cpp:53-58`) packs
`origin<<24 | (key_hash32 & 0xffff)<<8 | (ctr & 0xff)` ⇒ the only per-post varying component is an **8-bit counter**,
so **the id REPEATS after 256 posts from the same origin/key-hash pair.**

★ **Frame it correctly, because this is what the earlier wording got wrong: `channel_msg_id` is a CORRELATION HANDLE,
not a NONCE.** It is not unique enough to identify an alarm generation or authenticate on its own. The proposed
`request_token` closes the generation-alias problem; condition ① (sealed) independently authenticates the frame to
the shared-team-key trust domain. Neither substitutes for the other.

**⇒ MANDATORY, not advisory:**
1. **A tracked request has a BOUNDED LIFETIME — proposed as 30 minutes in §5.5.** It is independent of the shorter
   emergency modal hold; an unbounded tracking table is not acceptable.
2. **A match against an EXPIRED/UNTRACKED `(id, token)` pair is REFUSED, never credited** — refused loudly where a
   refusal is observable (C2), never silently downgraded into a partial credit.
3. **A second answer is idempotent for emergency state.** It remains a normal stored/displayable message but causes
   no second state transition, wake, retry cancellation or alert (§5.5).
⚠ **Do not "solve" the 256-wrap only by widening the counter.** Widening reduces accidental collision but is still
not an alarm-generation nonce. The random token is the generation binder; ① is the authentication bound. Per M3 the
metadata bytes are free to change, so keeping both properties explicit is preferable to overloading the message id.

### §5.4 — Live push, durable inbox and companion JSON use one decoded view

Do **not** split metadata into another maximum-sized buffer. Keep the canonical envelope bytes in the existing
`Push::body` / inbox body slot and parse them through one bounds-checked `AppEnvelopeView` returning three spans:
`code`, `metadata`, and `text`. A small `Push::app_code` discriminator (0 = ordinary body) may be added so consumers
do not re-infer presence; its layout cost must be measured against `sizeof(Push)` rather than assumed. The duplicated
value must equal envelope byte 0.

The durable representation needs no second payload and no larger record cap:

- set the existing inbox record `type` to `DATA_TYPE_APP_MESSAGE` for a structured DM **or channel record**;
- store the complete canonical envelope in the existing variable body;
- on pull, parse that envelope and expose the remaining bytes as the user-visible text;
- retain the channel responder hash in the record's **existing** `sender_hash` field (currently hard-coded to 0 for
  channel records), and retain `enc` in the existing field.

This deliberately broadens `InboxEntry::type` from *"the received DATA frame type"* to a **normalized stored-body
type**. For a channel record, `DATA_TYPE_APP_MESSAGE` describes the reconstructed logical envelope; it is not
pretending that the M frame carried a DATA type. The field comment and serializers must adopt that contract together.

★ **A pre-existing contract seam is load-bearing here:** `Inbox::record_channel` already stores `enc`, but
`firmware_inbox.cpp` does not pass it to `write_inbox_channel`, whose signature/JSON have no `enc` argument. The live
`channel_recv` correctly emits `enc:true`; the pulled twin silently loses it. B118 cannot make a durable answer
trustworthy unless this is corrected in the storage/JSON slice. The same slice should emit the retained channel
`sender_hash` omit-when-zero. Neither change requires an inbox record-format bump because both fields already exist.

Both live and pulled JSON use the same additive shape:

```json
{"ev":"channel_recv","app_code":129,"app_meta":"018004030201080706050403020100","body":"On my way",
 "enc":true,"sender_hash":2065214882,"channel_msg_id":123456789}
```

`app_meta` is lowercase hex of the exact metadata bytes; known-code convenience fields may be added, but never instead
of the raw metadata. Ordinary messages omit `app_code`/`app_meta`, retain bare-text `body`, and remain byte-identical.
The OLED likewise receives only the `text` span for wrapping/rendering; it must never print metadata bytes as text.

### §5.5 — Proposed request lifecycle

Use a **30-minute live-request lifetime** and a bounded emergency record holding one random `request_token` plus the
three accepted `channel_msg_id` aliases. The token makes correctness independent of the configurable channel-rate
gate: a wrapped id from an old alarm cannot match the new alarm's token. Thirty minutes is therefore a product/state
retention proposal, not a hidden assumption about how quickly ids can be minted.

A valid first answer marks the alarm answered. Further valid answers to any alias are retained/displayable as normal
messages but are **idempotent for the emergency state**: no second wake, sound, retry cancellation or state transition.
Expired/untracked references are stored as ordinary structured posts and may be displayed, but never credit an alarm.
Unsigned elapsed-time comparisons must be wrap-safe.

### §5.6 — Receipt must never auto-generate an answer

Firmware must **never auto-generate `CHANNEL_ANSWER` merely because it received or decrypted a request**. That would
turn the mechanism back into delivery evidence. MeshRoute's companion/OLED/console producers originate an answer only
from an explicit operator action on the selected request. A future convenience command should accept the channel id
plus referenced `channel_msg_id`, build the fixed v1 metadata internally and require `-t -e`; callers must not
hand-assemble binary metadata in a quoted text command.

⚠ The receiver cannot prove that third-party software did not automate the command. Therefore the protocol claim
remains *"an authenticated team application emitted an explicit answer frame"*, never *"a particular human acted"*.


Recommended implementation slices:

1. shared envelope pack/parse, the three transport carriers, sealed-relay enclosed-type preservation, and live/durable
   JSON parity — no emergency behavior;
2. B118 request/answer allocation, bounded tracker and six-condition matcher;
3. companion/OLED actions and presentation. Until slice 3, a console-only explicit answer command is an adequate
   protocol probe but not the finished feature.

---
## §6 — Owner decisions this draft needs

⚠ **Items 2 and 3 are kept in place as SETTLED rows rather than deleted, so the diff shows the decision** (§3's
in-place-correction rule). ⛔ **Do not re-ask them.** ★ An earlier revision of this section carried a "settled" banner at
the top while the body below still asked the same questions — the *"prepended a correction instead of replacing it"*
defect. The body is what a reader acts on; it is fixed here.

1. **Transport marker placement — OPEN; recommendation in §3.1.** Use `DATA_TYPE_APP_MESSAGE=21` for DMs,
   `channel_flavor_app=0x20` for plaintext M, and authenticated `channel_inner_flag_app=0x08` for sealed M. Do not
   use the unauthenticated outer flavor bit as the sole sealed marker.
2. ✅ **SETTLED — bind to `channel_msg_id`.** Owner ruling 2026-08-05: the answer references the exact channel post,
   never merely its sender. This settles the reference, not §5.1's six-condition matcher.
3. ✅ **SETTLED — the channel range starts at `128`; contents remain owner-unruled.** The refined allocation proposes
   `0x80 CHANNEL_REQUEST_ANSWER` and `0x81 CHANNEL_ANSWER`, with distress and answer detail carried in versioned
   metadata (§5). `0` is invalid and `1..127` are reserved for DM app codes. Unknown structurally valid codes are
   retained/displayed but inert; malformed envelopes are rejected.
4. **Responder display identity — OPEN; recommendation in §5.** Set the existing sealed-inner source flag on every
   structured sealed post and retain `sender_hash` in live and durable records. It is display attribution only;
   the shared team key does not authenticate a particular human and the matcher must not use it.
5. **First-slice scope — OPEN; recommendation in §5.6.** Land the generic envelope/transport/storage layer first,
   the B118 tracker/matcher second, and companion/OLED presentation third. Do not bury the wire and inbox changes
   inside an emergency-UI task.
6. **Request lifetime/token — OPEN; recommendation in §5.5.** Thirty minutes, one random token plus all three
   emergency ids per alarm, duplicate answers idempotent, expired/untracked answers stored but never credited. The
   token removes correctness dependence on the configurable channel-rate gate. The ordinary emergency timeout
   wording remains governed by B117; expiry does not invent another failure claim.
7. **Plaintext policy — OPEN; recommendation in §5.2.** The generic codec may carry low-trust plaintext app messages,
   but B118 request/answer codes have sealed-only effects. A plaintext instance is visible/inert and does not get a
   second hedged emergency state in v1.
8. **DM consistency — OPEN as a new scope ruling; recommendation in §3.3.** Reserve `1..127` for DMs and use the same
   envelope/parser/storage contract. This does not reverse §1.1: transport-family validation makes a channel answer
   code in a DM malformed. Encrypted cross-layer app DMs remain a loud refusal until sealed relay preserves the
   enclosed type.

---
## §7 — Out of scope / attribution

⛔ **B116 stays parked**, not reopened — this design replaces it; if it ships, B116 closes as superseded.
⛔ Not in this slice: **B105**, **B112**, the **REPLY-only wake** widening, Tasks 8–9.
★ **C1:** the wire/codec change and the UI consumption are **separate slices**. If a `wire_version` bump is taken, it is
its **own commit** so the 36-stream re-anchor stays attributable (§2).
⚠ **Corpus consequence to plan for, not discover:** a new channel payload shape will move scenario md5s. Expect a
deliberate re-anchor, and land it **alone** — `simulation/BASELINE.md` is the authority for which streams move.

---
## §8 — Required implementation gates

1. **Legacy/UTF-8:** ordinary DM and M frames remain byte-identical; bare text beginning with `Ó`, `€`, emoji,
   `0x80`, `0x81`, or any other high byte is never parsed as an envelope without its transport marker.
2. **Common codec:** one packer/parser round-trips empty/max metadata, empty/permitted text, maximum fitting text,
   every length boundary, and little-endian `channel_msg_id`; `2 + meta_len > len` is red.
3. **Carrier matrix:** direct plaintext/sealed DM, plaintext M and sealed M expose identical envelope views. Flipping
   the unauthenticated outer app bit on a sealed M never creates a trusted app message; contradictory markers are red.
4. **Namespace:** DM code `>=128`, channel code `<128`, and code 0 reject. An unknown in-range code preserves
   code/metadata/text but calls no registered handler.
5. **Routing:** same-layer, team, mobile-delegated and plaintext cross-layer app DMs preserve type. Encrypted
   cross-layer either preserves authenticated enclosed type end-to-end or refuses `unsealable`; a raw metadata-as-text
   delivery is a gate failure.
6. **Inbox parity:** live and pulled DM/channel events carry the same `app_code`, exact `app_meta`, clean `body`,
   `enc`, stable identities and message ids. Specifically pin the current missing `enc:true` on pulled sealed-channel
   history and `sender_hash` persistence. Ordinary JSON remains byte-identical by omit-when-absent.
7. **B118 matcher:** each of the six §5.1 conditions has an independent negative. A forged plaintext answer with the
   observed team/id, a sealed wrong-channel answer, wrong code, wrong id, wrong token and expired pair all fail; only
   the complete sealed case reaches `Emergency::reply`.
8. **Three attempts / expiry:** answer to attempt 1, 2 or 3 with the shared alarm token reaches the same alarm exactly
   once; duplicates are idempotent; 30-minute and unsigned-time wrap are exercised; a wrapped-id answer carrying an
   old token cannot credit a new alarm. All-zero/dead-RNG token generation refuses `bad_rng` and emits no request.
9. **No auto-answer:** receipt/decryption of a request emits no answer. Only the explicit answer command/UI action
   originates `0x81`; a control that auto-answers must turn the test red.
10. **Resource attribution:** measure `sizeof(Push)`, `sizeof(Node)`, inbox record maximum, board RAM/flash and warning
    census. Do not assume the proposed discriminator fits padding, and do not reduce ordinary-message limits globally.
