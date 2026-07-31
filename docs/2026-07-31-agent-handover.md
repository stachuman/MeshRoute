<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# HANDOVER — 2026-07-31

*Written at end-of-context by the QA-gate coordinator, at the owner's request, focused on **open topics**.
Supersedes `2026-07-28-agent-handover.md` (still valid for the team-routing arc that preceded this).*

> **My role, as the owner set it:** *"start separate coding agents by preparing instructions, monitor, then check
> quality."* I do **not** write feature code. Coding agents implement; I gate; **the owner commits and rules.**
> ⚠ A commit exception was granted for the overnight team-routing arc and has been used since — **re-confirm it
> rather than assuming it.**

## ★★ READ FIRST — two files, in this order

1. **`docs/2026-07-30-open-bug-register.md`** — the bug index **and `§0` is the full dispatch contract** (QA-owned
   files, the git prohibition, `rm`-before-build, the three-board rule, the flash noise floor, the four detector
   probes, the poison-probe rules). ★ **Point a coder at §0 instead of restating the gate** — it halves a brief.
   ⚠ It scored **zero on all ten** dispatch essentials when first written; §0 exists because of that test.
2. **`simulation/BASELINE.md`** — the **anchor authority and the evidence store.** Every register entry names the
   note that holds its measurement. ★ **Newest note wins**; a scenario name greps to the *oldest* match first,
   which has already misled one coder.

## State

| | |
|---|---|
| MeshRoute HEAD | `0aea27b`, tree **clean** |
| Simulator HEAD | `7abe86e`, tree **clean** |
| Native | **1006 / 70360 / 0** |
| Corpus | **36** scenarios · `sizeof(Node)` **220656** · `wire_version` **1** · NV `kVersion` **22** |
| Keystone | `s18` = **`1cd21235` / 271629** — ★ **has never moved through this entire arc** |

**Detector probes** (hard gate item, table in register §0): P-T7/s38 **474 ev, 8 of 16** · P-T1/s35a **1892 ev, 20
FAIL** (site **`node.cpp:1309`**) · P-T6A/s37 **851 ev, 12 of 36** · both **917 ev, 16 of 36**. All four verified
exact as of `3453def`.

---

## 1. Bug fixing — the live queue

**The register is the index; work from it, tier order, `fails-silently` jumps the queue.** Closed today: **B1**
(`team 88A672BA` joined team 88) and **B2** (a team-scoped H answer wrote the static `_id_bind`) — commit `3453def`,
five team re-anchors, s34's `no_route` **8 → 0**.

**Next up, in order:**
- **B0** — ★ **the only LIVE leak: `loc_in_dm` airs coordinates in the clear.** Its fix is **CL3** in the
  channel-crypt spec (§5 below), which is bigger than a guard: it adds `-l`, removes `loc_dm` across **eleven
  surfaces incl. an app-facing binary TLV**, and bumps `kVersion` 22 → 23.
- **B3** — the `reqpubkey` plane divergence. ⚠ **s22 is GREEN today and the fix REDDENS it** until the scenario
  gains `-t`. That is expected, not a regression — the register row was wrong about this and is corrected.
- **B4, B5** — `schedule_sync_response`'s static `_rt_count`; `channel_pull`'s missing `team_id`.
- Then Tier 3, and **D1/D2** (the deferred DV-cap flip, with its trigger condition; and the **read-path plane
  audit**, which **B18** now belongs to).

★ **Two findings from today worth carrying, both methodological:**
- **B17 — an out-of-range `team <id>` diverges BY TARGET WIDTH.** `team 4294967296` *leaves* on 64-bit native but
  **joins garbage `0xFFFFFFFF`** on the 32-bit boards. ⇒ **a native test can never catch that class.**
- **B19 — `deleg_ack_put` is inlined at 8 sites (≈4 KB).** ★ **Fold into B12, never take alone** — `noinline`
  re-codegens all eight sites and destroys the object-level attribution that proves inertness on `gateway`.

## 2. Address book — spec'd, **nothing open**, ready to dispatch

`docs/superpowers/specs/2026-07-29-peer-address-book-design.md`. **All decisions ruled.** Four slices, in order:

- **AB1** — NV: `PeerRec` gains `confidence` + name; `kPeersVersion` 1 → 2; boot restore honours both; ⚠ **eviction
  policy: pinned must beat authoritative** (the cap stays 16). **Independent — start here.**
- **AB2** — `peername` (a **synchronous ack**, owner-ruled ⇒ **no new `PushKind`**, no sim coordination) + `conf` on
  `peer_key_cached`.
- **AB3** — the generated view + **rewire `hashof`/`nameof` onto it**.
- **AB4** — retained location (§5 below). ★ **The one AB slice that moves `sizeof(Node)`.**

⚠ **AB2 and AB3 share `console_parse.cpp`/`console_json.cpp` — never run them concurrently.**

**The three things that make this spec worth reading rather than skimming:**
1. ★ **The view is GENERATED, not stored** — a join on `key_hash32` over three existing tables. A fourth table
   would need syncing on every set and eviction, which is how every ledger this arc found came to drift.
2. ★ **`is_key_available` is a LEVEL, not a boolean.** `peer_key_cached` currently emits a **hardcoded literal**
   `"pinned":false`, so the app cannot tell `overheard` (**cannot seal**) from `authoritative` (**can**) — which is
   exactly how you offer "send encrypted" and then fail `no_pubkey`. **The app must gate on `conf >= authoritative`.**
3. ★★ **§2.5 FORBIDS the obvious `hashof` fix.** `hashof` reads `_id_bind` (static ids) while `reqpubkey` reads the
   team key cache — bench-proven divergence. **Do not "fix" it by writing the team hash into `_id_bind`: that IS
   register B2**, just closed. Fix it with the **view**.

## 3. Remote-admin — **mid-redesign; I did not work it this session**

⚠ **This is absorbed context, not built work — verify before acting.** What is true as of today:

- **Spec: `docs/superpowers/specs/2026-07-26-remote-admin-challenge-response-design.md`.** Its §0: *replace the
  monotonic replay counter — **known-broken over a lossy link**, see the `§admin-replay-REDESIGN-OWED` note at
  **`src/firmware_remote.cpp:72`** — with a **node-issued single-use challenge**.* ★ **The standing owner ruling is
  REDESIGN, not patch.**
- **Live in code:** `_admin_pubkey[32]`, `_admin_counter_floor`, `_admin_provisioned` (`node.h:1229-1233`, gated
  `MR_FEAT_REMOTE_MGMT`; accessors `:61-63`) · dispatch in `src/firmware_remote.{h,cpp}` (`rcmd` + password /
  unlock / lock) · `rcmd <dst> <verb>` where **`status`/`routes` are OPEN cleartext and everything else is SEALED
  behind `unlock`** · `DATA_TYPE_REMOTE_CMD = 6`, `DATA_TYPE_REMOTE_RESP = 7` · a **single** `RemoteInbound` slot,
  deliberately **NOT** feature-gated, drained by `fw_main`.
- **Contract** (`ios-companion/INBOX_SYNC_CONTRACT.md`, "Remote admin — challenge–response"): the companion becomes
  the **primary** driver in v1; the `floor=N` counter hint is **replaced** by a node-issued challenge with an
  *invisible* lifecycle — the app caches the per-node challenge from sealed responses.
- ★ **It already constrains other work, twice:** register **B15** (`enc_cfg`'s TLV was **not** extended with
  `team_ch_key`) was left alone *because* this path is mid-redesign. And **`MR_FEAT_REMOTE_MGMT` is the THIRD AXIS
  of the six-env board grid** (only the three `*_mobile` envs clear it) — that is why the escalation set is six
  envs and not four.

## 4. Channel crypt + location — spec'd, **two open decisions**

`docs/superpowers/specs/2026-07-30-channel-crypt-and-location-privacy-design.md`. Three slices:

- **CL3** — ★ **take this first: it closes register B0, the only live leak.** Adds `-l` (per-send location) to
  `send`/`send_layer`, **removes `cfg set loc_dm`** across eleven surfaces including the app-facing binary TLV, and
  bumps `kVersion` 22 → 23. Three loud refusals: not-sealed · no-fix · does-not-fit (the last converts today's
  **silent** best-effort drop into a failure — that is the point of an explicit flag).
- **CL1** — `send_channel -e` parse + the refusal matrix (inert scaffolding until CL2).
- **CL2** — the substantial one: T-K2's crypto. ★ **Its #1 risk is the NONCE** — `dm_nonce` binds a *ctr* while a
  channel post's identity is a `channel_msg_id`; carry a `seal_ctr` like `SEALED_RELAY` rather than improvising.
  **Refuse, never reuse.**

★★ **Two flag combinations MUST refuse, and the second is the subtle one:** `-e` without `-t` (the only content key
is the *team* key), and **`-t -g -e`** — BOTH would air an identical copy **in clear** globally and **defeat the
encryption entirely**. The refusal has to say why or it reads as an arbitrary limit.

⚠ **`-l` is deliberately NOT on `send_channel`** — a channel location is T-K2's `inner_type = 1`, an *alternative*
payload, and belongs to **T-K5**. Overloading one letter with two meanings is the ambiguity this arc keeps paying for.

**Retained location (AB4, spec'd in the address-book doc §2.7):** the receive side **already** parses, crypt-checks
and pushes it (`node_mac_rx.cpp:1189-1194`, with `sender_hash` in scope) — **only retention is missing.** Storage is a
**dedicated 16 × 16 B ring keyed by `key_hash32`** (owner-ruled; `PeerKey` and `_team_keys` were both ruled out, the
latter for the same three reasons B2 established). **RAM only, deliberately** — a stale position is worse than none,
and a captured node must not yield everyone's last position.
★★ **The trust bound to carry:** the team content key is **SHARED**, so sealing a channel post proves **membership,
not identity** — **any keyholder can forge another member's position.** Accepted under the hiking-group model
(owner: *"we treat it as trusted"*), recorded like I9. ⇒ the row carries **which anchor** applied (`peer` = pairwise,
`team` = group) because the two claims are not equally strong.

## 5. Owner decisions pending

| | |
|---|---|
| **O2** | The plaintext-team-post opt-out: `cfg set team_channel_crypt 0` only (QA recommends) or also a per-send flag? |
| **O4** | The retired `TAG_CFG_LOC_DM` number — mark RETIRED in-source only, or reserve it in the contract too? |
| ★ **BLE** | *"The BLE fallback exposes the full console"* is **no longer a watch-item.** Under the `team exportkey` ruling it is the **only** control over the team content key — and once AB4 lands, over **every teammate's last known position**. Closing it (pairing / auth gate / console allow-list) is its own slice. |
| **T-K2** | ⚠ **Must rule before it seals:** `set_team_id` deliberately does **not** clear the team channel key, so a `team <other>` switch leaves the **previous team's key**. Inert today; the moment T-K2 seals, a switched member seals for its new team under the old key. Marked in-source, pinned by a test. |

## 6. The method lessons that keep earning

Full detail in register §0 and the BASELINE notes. The five that changed outcomes:

1. ★★ **A brief's premises are hypotheses.** Every brief this arc contained at least one wrong premise; one had
   four. **Say so in the brief** — "disproving one is the most valuable thing you can return". It has caught a
   2-bit field treated as an open enum, a drifted comment I passed on as fact, a wrong site count, and a
   recommended destination that would have upgraded unauthenticated data to trusted.
2. ★★ **V1 applies to comments.** Verifying a comment *exists* is not verifying it is *true*.
3. ★★ **A 0/N poison result means "unreachable", never "inert"** — prove reachability by tracing the line
   *immediately above*. And for a **comparison-only** score, a **uniform-offset** poison is an invalid control; use
   a **differential** one.
4. ★★ **A detector's clean-run anchor being unmoved is not evidence its discrimination survived.** Re-run the
   probe. This cost a whole slice, twice.
5. ★ **Durable output goes in the repo, never an agent scratchpad** — that is how a proven 33-assert scenario was
   lost.

## 7. Where everything lives

`docs/2026-07-30-open-bug-register.md` (bugs **+ the dispatch contract**) · `simulation/BASELINE.md` (anchors +
evidence) · `docs/2026-07-26-slice-gate-method.md` (the gate) · `ios-companion/INBOX_SYNC_CONTRACT.md` (app surface,
**opens with a PENDING CHANGES box** — ★ `loc_dm` is being removed, so an app reading it must stop **now**) ·
`docs/superpowers/specs/` — the address book, channel-crypt/location, remote-admin, team-encrypted-channel (T-K2/T-K5
still unbuilt) and the team-routing spec (**complete**).
