<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# DESIGN SPEC — encrypted channel posts (`send_channel -e`) + the location-requires-encryption rule

*2026-07-30. Owner-requested. Line references code-verified today against HEAD `3431e30`; **re-verify before acting
(V1/V2)** — this tree moves several times a day.*

**Status: SPEC'D, NOT DISPATCHED.** No file contention with the address book (`2026-07-29-…`), but see §3 for the
ordering that matters.

> **Owner's words (2026-07-30):** *"We already have teams priv key - but channel messages are lacking -e option at
> all - channel message should accept both -t and -e - then - if location is attached to DM, such message would
> refuse to send if not encrypted."*

---

## 0. Why — two leaks, one of them live today

**(1) A channel post cannot be encrypted at all.** The team content key shipped (`§team-ch-key` T-K1/T-K1b/T-K3)
and nothing can use it: `send_channel` has no `-e`. The **contract says so in its own words** —
*"`send_channel <ch> "<text>"` # **no ack/enc**"*. So every team channel post is in clear, on a shared PHY, to
anyone in range. This is the console half of **T-K2** (`2026-07-26-team-encrypted-channel-design.md` §2.2).

**(2) ★★ `loc_in_dm` leaks coordinates in the clear — this is live.** `node_mac.cpp:149-152`:

```cpp
if (app_dm && _cfg.loc_in_dm && (_cfg.lat_e7 != 0 || _cfg.lon_e7 != 0)) {
    const size_t with_loc = …;
    if (with_loc <= protocol::max_payload_bytes_hard_cap) item.flags |= DATA_FLAG_LOCATION;
}
```

The gate is `app_dm && loc_in_dm && has-a-fix && it-fits`. **There is no crypt check**, and the seal decision
happens *after* (`:154` onward). So on a node with `loc_in_dm = 1`, a **plaintext** DM — `e2e_dm` off, no `-e`, or
simply no peer key — flies with a 6-byte location **in the clear** (`frame_codec.cpp:953`, the unsealed pack path).
⚠ **And `node_carriers.h:233` claims the opposite**: *"piggyback location on originated DMs (DATA_FLAG_LOCATION,
**sealed inner**)"* — true only of a CRYPTED DM. **V1 drift, and it is why the leak reads as intended behaviour.**

## 1. Present state (code-verified 2026-07-30)

| | |
|---|---|
| `send_channel` flags | **`-t`** (TEAM plane) and **`-g`** accepted; plain ⇒ GLOBAL, `-t` ⇒ TEAM only, `-t -g` ⇒ BOTH (§S7 T-B). ★ **No `-e`.** |
| ⚠ contract drift | `INBOX_SYNC_CONTRACT.md:28` says *"`send_channel`/`send_layer` **REJECT** -t"* — **false for `send_channel`**. `console_parse.cpp:121` shows only `send_layer` rejects it. **Fix while here (V1).** |
| location on a DM | set on a **size** check only; **no crypt gate**; unsealed pack path live at `frame_codec.cpp:953` |
| location, sealed case | rides the **origin-onward sealed region** (`frame_codec.cpp:1018`, `node_hashlocate.cpp:414`) — correct today |
| the team content key | `team_ch_pub`/`team_ch_priv` + `team_channel_key_present()`, canonical RFC-7748 clamped (T-K1) |
| `team_channel_crypt` | **does not exist yet** — T-K2 §2.5 specifies it, default ON when a key is held |

## 2. Design

### 2.1 `send_channel -e` — the console half of T-K2

**The crypto is already specified: `2026-07-26-team-encrypted-channel-design.md` §2.2. Do not re-design it here.**
That section owns the `channel_flavor_crypted` bit, sealing to `team_ch_pub` via the existing sealed-sender path
(U2 — the team is "a recipient"), the `[inner_type u8][payload]` inner, the un-keyed-receiver drop + the
`team_channel_no_key` push, and `record_channel(…, enc=1)`.

★★ **Its #1 review point stands and is this slice's main risk: the NONCE.** `dm_nonce` binds a **ctr**, but a
channel post's natural identity is the 32-bit `channel_msg_id`. The nonce must be derived from material that is
**unique per post AND available to every reader**. If `channel_msg_id + origin hash` does not fit `dm_nonce`'s
shape, **carry a `seal_ctr` in the body exactly as `SEALED_RELAY` does** — do not improvise a third scheme.
⚠ **Nonce reuse under a static per-pair key is catastrophic** (see `node_hashlocate.cpp`'s R7 all-zero-seed
refusal for the precedent: it refuses rather than seals under a degenerate nonce). **Refuse, never reuse.**

### 2.2 ★ The flag matrix — and the combination that must REFUSE

`-e` interacts with an existing plane select, so spell out all four cases:

| invocation | behaviour |
|---|---|
| `send_channel <ch> "…"` | GLOBAL, plaintext — **unchanged** |
| `send_channel <ch> "…" -t` | TEAM, plaintext — **unchanged** (a keyless member still posts, and keyholders read it: plaintext is always openable) |
| `send_channel <ch> "…" -t -e` | ★ **the new capability** — TEAM, sealed to `team_ch_pub` |
| `send_channel <ch> "…" -e` (no `-t`) | ❌ **REFUSE loud.** There is no key for a global channel — the only content key is the *team* key. T-K2 §2.2 already requires rejecting `crypted && !team` at origination |
| ★★ `send_channel <ch> "…" -t -g -e` | ❌ **REFUSE loud.** This is the trap: BOTH means one copy to the team and one **global in clear**. Sealing the team copy while airing identical content globally **defeats the encryption completely** — an eavesdropper reads the global copy. **The refusal must say why**, or an operator will read it as an arbitrary limitation |

★ **`team_channel_crypt` (T-K2 §2.5) default-ON is what makes `-e` mostly unnecessary in practice:** when the node
holds a key, `-t` seals by default and a *plaintext* team post needs an explicit opt-out. `-e` then exists to be
**explicit** and to fail loud when sealing is impossible. ⇒ decide whether the opt-out is a new flag or
`cfg set team_channel_crypt 0`; **QA recommends the config toggle only** — a per-send "send this one in clear"
flag is a footgun on a privacy feature.

### 2.3 ★★ Location on a DM requires encryption — REFUSE, do not degrade

**Rule (owner-ruled 2026-07-30):** if location would be attached to a DM and the DM will **not** be sealed, the
send is **REFUSED**, loudly, with its own reason.

Three candidate behaviours; the owner chose the first, and it is the right one:
1. ✅ **refuse the send** — the app learns nothing was sent, and the user's privacy intent is honoured;
2. ❌ silently omit the location — the app believes it shared a position it did not;
3. ❌ send it in clear — **today's behaviour**.

**Implementation shape.** The crypt intent is **already a parameter of `enqueue_data`**, so the check is local:
compute `want_crypt` (the same expression `:154` onward uses — extract it once, U1, do not duplicate) **before**
the `:149` gate, and if `loc_in_dm && has-a-fix && !want_crypt` ⇒ refuse. **Do not** set `DATA_FLAG_LOCATION` and
hope the seal happens.

⚠ **A seal can still FAIL after the intent is set** (`no_pubkey`, `no_identity`, `bad_rng`). Those already refuse
loud today, so a location-bearing send inherits that correctly — **verify it, do not assume it**, and make sure the
reported reason is the *seal* failure rather than the location rule, or the operator will chase the wrong thing.

★★ **STATE THE CONSEQUENCE PLAINLY, because it is large: this makes `loc_in_dm = 1` mean "encrypted DMs only".**
Every plaintext DM from a node with a position fix will refuse. That is defensible — it is a privacy toggle, and
refusing is the honest reading of the owner's rule — but it must be **documented as such**, and the operator escape
(`cfg set loc_dm 0`) must appear **in the refusal text itself**. ⇒ **§4/O1: confirm this is intended, or scope the
rule to sends where location was requested per-send rather than globally.**

★ **Bonus: the fix makes an existing comment true.** `node_carriers.h:233`'s *"sealed inner"* claim becomes
unconditionally correct. Update it from a claim to a guarantee (V1).

### 2.4 The same rule for a channel post's location inner-type

T-K2 §2.2 defines `inner_type = 1` as location. **A location in a plaintext channel post is the same leak, broadcast
wider** — a channel post goes to every member, and in clear to everyone in range. ⇒ **`inner_type = 1` requires the
crypted flavour; refuse otherwise**, with the same reason shape as §2.3. This is cheap to include now and awkward to
retrofit once the app starts sending positions.

### 2.5 Follow-on the slice should NOT take (C1)

Once §2.3 lands, `frame_codec.cpp:953` — the **unsealed** LOCATION pack path — becomes unreachable for DMs.
**Leave it.** Deleting dead-but-reachable-looking codec code is its own slice, and `pack_data` is shared. **Mark it
`✖ MISSING` in-source with the reason** so the next reader knows it is deliberate, per the mark-done-vs-missing rule.

## 3. Slices, and the ordering that matters

- **CL1** — `send_channel` gains `-e` **parse + the four-case refusal matrix (§2.2)**, with `-t -e` wired to a
  **stub** that refuses `unsealable` until CL2 lands. Contract drift on `-t` fixed. **Gate: corpus byte-identical**
  (`lib/console` + `src/` are outside the sim build), native for all four cases.
- **CL2** — the T-K2 crypto: `channel_flavor_crypted`, seal/open, **the nonce design (§2.1 — the review point)**,
  the un-keyed-receiver drop + `team_channel_no_key`, `record_channel(enc=1)`, `team_channel_crypt` default-ON.
  ⚠ **Expect a re-anchor of every team-channel scenario** (s28/s29 hold keys); QA owns the scenario edits.
- **CL3** — §2.3 the DM location rule (+ §2.4's channel case if CL2 has landed).

★ **CL3 is independent of CL1/CL2 and is the only one closing a LIVE leak** — take it first if the owner wants the
privacy hole shut before the feature. CL1 alone is inert scaffolding; CL2 is the substantial slice.

## 4. Open decisions

| | decision |
|---|---|
| **O1** | ★ **Does `loc_in_dm = 1` becoming "encrypted-only DMs" match the intent** (§2.3), or should the rule apply only to a per-send location request? The owner's wording says refuse; this asks whether the *blast radius* is accepted |
| **O2** | The plaintext-team-post opt-out: **`cfg set team_channel_crypt 0` only** (QA's recommendation) or also a per-send flag? |
| **O3** | Should `send_channel -a` exist too? The contract's *"no ack/enc"* covers both, and a channel post has no single recipient to ack — **QA recommends NO**, recorded so it is not re-asked |

## 5. Gate expectations

Standard gate — ★ **read `docs/2026-07-26-slice-gate-method.md`, and §0 of `docs/2026-07-30-open-bug-register.md`
for the full dispatch contract** (QA-owned files, the git prohibition, `rm`-before-build, the three-board rule, the
flash noise floor). Specifics here:
- ★★ **`s18` keystone `1cd21235` / 271629 must NOT move**, and **no static-only scenario may move** — `loc_in_dm`
  and the team channel are both off by default in the static corpus. If one moves, **stop and report**.
- ★★ **The four detector probes are a hard item** — table in the bug register §0.
- **CL2 will re-anchor team-channel scenarios.** Attribute every mover; `dm_delivery_breakdown` should stay
  `diff`-identical (this changes *content*, not delivery).
- ★ **The location rule needs a NEGATIVE test:** a `loc_in_dm` DM that would go plaintext must **refuse**, and the
  test must assert **no frame was aired at all** — not merely that the location was absent. Show the before-arm
  (revert the guard) airing a frame with the 6 location bytes **in the clear**.
- ★ **If an enum is extended, APPEND** — the sim bridges `PushKind`/`SendFailReason` on their raw `uint8_t` with a
  `static_assert` twinned in two sim files. Prefer **reusing `SendFailReason::unsealable`** for the refusals here.
  And note `-Wswitch` fires for these **only on the board build** (`fw_main.cpp` is outside native).

## 6. Contract additions owed (QA writes them)

The `send_channel` grammar with `-e` and the four-case matrix; the corrected `-t` claim at line 28; the location
refusal reason and **the `cfg set loc_dm 0` escape in the user-facing text**; `enc:true` on `channel_recv` /
`inbox_channel` (T-K2 already reserves it); and the `team_channel_no_key` push.
★ **And the app-facing consequence of §2.3, stated plainly**: with `loc_in_dm` on, a DM to a peer whose key is not
held **will refuse** — so the app must surface `reqpubkey`/QR rather than retrying.
