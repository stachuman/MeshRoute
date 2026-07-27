<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# DESIGN SPEC — CTS byte 3 becomes `[len6][cr2]`: closing the NAV-from-CTS under-reservation

**Status:** proposed, owner-requested 2026-07-27. **Not implemented.** Review before code (rule P2).
**Owner ruling this rests on:** *"we do not bump wire version, meshroute is not yet deployed"* (2026-07-27) — see
[[meshroute-not-yet-deployed]]. A CTS wire change is therefore cheap **now** and expensive later.

---

## 1. The problem

NAV (virtual carrier sense) exists for the hidden terminal: node **C** overhears a CTS from **B**, learns *"**A**
is about to send DATA to **B**"*, and stays silent for that long even though C may not hear A at all.

The reservation is `DATA airtime + ACK + gaps`, and DATA airtime depends on **A's coding rate** — CR multiplies
the payload-symbol count directly. `nav_duration_cts` cannot know it, so it passes `active_cr()` — **C's own** CR
as a stand-in for a third party's.

★ **This is the only site of its class that fails in the DANGEROUS direction.** Peer CR heavier than ours ⇒ we
**under**-reserve and release NAV while the DATA is still on the air ⇒ we transmit into it. That is precisely the
collision NAV exists to prevent, failing in the one case it is for. Measured (cr5 overhearer, cr8 peer, BW 250 kHz):

| SF / `payload_len` | reserved | needed | shortfall |
|---|---|---|---|
| SF11 / 38 B | 690 ms | 935 ms | **−245 ms** |
| SF11 / 120 B | 1304 ms | 1918 ms | −614 ms |
| SF12 / 120 B | 2723 ms | 4050 ms | **−1327 ms** |

The −245 ms is the same order as the DATA-wait hole that was killing cross-layer delivery before `§rts-cr`.

**Exposure is the DEFAULT configuration, not a corner.** `nav_enabled = true` (`node_carriers.h:200`), runtime-only
(`cfg set nav`, `persist = false`) so it reverts to ON every boot. Two independent nav gates matter: the **CTSer's**
decides whether byte 3 exists at all (`cin.payload_len = nav_enabled ? r.payload_len : 0`), and the **overhearer's**
decides whether to arm. Both default on. ⚠ **Counter-intuitively, correct NAV configuration is what exposes this**:
the `payload_len == 0` branch reserves for a full 255 B frame and would mask the CR error, but it only fires when
the CTS *sender* has NAV off. With shipped defaults it never fires — the size is exact and the CR is wrong.

**Scope:** only bites when two nodes on one channel run **different** CRs. Today that means a dual-layer gateway
with per-layer CR (`s32_dual_cr_gateway`'s shape). In a uniform-CR mesh the fallback is exactly correct.

## 2. Why this was previously "unfixable", and what changed

The CTS has **zero free bits** — verified at `pack_cts` (`frame_codec.cpp:341-353`):

| byte | content | slack |
|---|---|---|
| 0 | `cmd \| (sf−5)<<1 \| already_received` | none — 3 bits cover SF 5..12, all 8 values used |
| 1 | `tx_id` | full u8 |
| 2 | `rx_id` | full u8 |
| 3 | `payload_len` | full u8, present iff NAV |

`parse_cts` accepts **exactly** 3 or 4 bytes. The rejected options were a 5th conditional byte (a permanent tax on
every CTS in every mesh) or reclaiming `already_received` (load-bearing for lost-ACK recovery).

★★ **What changed: the CTSer already HAS the CR and merely lacks somewhere to put it.** `§rts-cr` put `cr_adv` in
the RTS, and **all three CTS producers hold the parsed RTS `r`** — verified: `node_mac_rx.cpp:279`, `:291`, `:384`
all read `r.payload_len` on the adjacent line. ⇒ this proposal is **pure forwarding of a datum already in hand**,
not the invention of new knowledge. Nothing needs to learn anything it does not already know.

## 3. The encoding

```
producer (CTSer, RTS in scope):
    len6 = ceil(r.payload_len / 4)            // 6 bits; clamp 63
    cr2  = rts_cr_decode(r.cr_adv) - 5        // 2 bits; total over cr 5..8
    byte3 = (len6 << 2) | cr2                 // 0 still means "no NAV hint"

consumer (overhearer):
    if byte3 absent      -> today's fallback: 255 B frame, active_cr()
    len6 = byte3 >> 2;  cr = (byte3 & 3) + 5
    bytes = min(255, len6 * 4 + 9)            // 9 = the TRUE max header (see §4)
```

**Why uniform 4-byte steps and not a log/piecewise scale.** Airtime error scales with **absolute** bytes, so a log
scale puts its coarsest steps on the largest frames — exactly where a byte costs the most milliseconds. Uniform
gives a constant ≤3-byte error everywhere. **Rounding UP is the load-bearing choice**: it keeps every length error
in the *safe* (over-reserve) direction, which is the only reason quantization is acceptable at all.

**The 6-bit field never saturates for a legal frame:** `max_payload_bytes_hard_cap = 241`
(`protocol_constants.h:614`), and `ceil(241/4) = 61 < 63`. The clamp is belt-and-braces.

⚠ **`byte3 == 0` must keep meaning "no NAV hint".** With `len6 = 0, cr2 = 0` a real frame could encode to 0 only
if `payload_len == 0` and cr == 5 — and a DATA frame always has a non-empty inner, so `payload_len ≥ 1 + MAC ≥ 5`
⇒ `len6 ≥ 2`. **Verify this at implementation time**; if any path can produce `payload_len == 0` with NAV on, the
sentinel must move.

## 4. Airtime-neutrality — the reason this costs nothing

`payload_len` is **inner + MAC** (`node_mac.cpp:800`, `data_mac_len` = 4 B, or **8 B under `CRYPTED`**). So the
consumer must add only the **header**: bytes 0..7 = **8**, plus **+1 iff `APP`** (the TYPE byte) ⇒ **8 or 9**.
The code today adds a flat **`+13`**, i.e. it already **over**-reserves by **4–5 bytes**.

★ Spend that existing fudge on the quantization and the change is free:

| | over-reservation |
|---|---|
| today | `payload_len + 13` → **4–5 B** |
| proposed | `4·ceil(len/4) + 9` → 0–3 (quantization) + 0–1 (header) = **0–4 B** |

**Same margin, zero net airtime cost — and the CR becomes exact.** This is strictly better than the 5th-byte
option: it costs no bytes and it retires the `+13` fudge in the same move.

## 5. Exact producer / consumer set

**Producers (3)** — `lib/core/node_mac_rx.cpp:279`, `:291`, `:384`. All have `r` in scope. Today all three do
`cin.payload_len = _cfg.nav_enabled ? r.payload_len : 0`.
**Codec** — `pack_cts` / `parse_cts` (`frame_codec.cpp:341-357`), `cts_in` / `cts_out` (`frame_codec.h`).
**Consumers (2)** — `node_mac_rx.cpp:431` `nav_arm(nav_duration_cts(c.chosen_data_sf, c.payload_len, active_cr()))`
and `:439` `reserve_yield(...)`. Both currently pass `active_cr()` with the limitation stated in-source; both would
pass the decoded CR.
**Helper** — `nav_duration_cts` (`node_mac.cpp:877`) already takes `data_cr` as a parameter (added by
`§rts-cr-overhear`), so **its signature does not change** — only what the callers pass, and the `+13` → `+9`.

⚠ **Do NOT touch the RTS.** `nav_duration_rts` already receives the exact CR from `cr_adv`; the RTS's byte-6
`payload_len` stays an exact 8-bit value. This spec changes the **CTS only**.

## 6. The invariant the whole scheme rests on

> **DECODE ≥ TRUE FRAME LENGTH, always.**

A test must sweep **all four CRs (5..8) × the full legal `payload_len` range (0..241) × both header shapes
(`APP` on/off) × both MAC sizes (`CRYPTED` on/off)** and assert the decoded byte count is **never less** than the
real frame length, and never more than 4 over. If that holds, every residual error is a bounded over-reservation
and the dangerous direction is gone by construction.
Also pin: the `byte3 == 0` sentinel still means "no hint"; a 3-byte CTS still parses; a same-CR mesh reserves
within ±4 bytes of today (⇒ airtime-neutral).

## 7. Risks and open points

1. ★ **It permanently spends the CTS's last byte of flexibility.** After this, a new CTS field means widening the
   frame. Accept deliberately or not at all.
2. **`payload_len` already has two producer meanings** — the M_BROADCAST path sets `inner_len − 6`
   (`node_mac.cpp:627`), not `inner + MAC`. Confirm which reach a CTS and whether `+9` is right for both, or
   whether the M path needs its own constant.
3. **Coverage will be thin.** Only `s32`/`s33` run mixed CR, and NAV-from-CTS needs a *third* node overhearing a
   CTS for a heavier-CR sender. **A scenario is owed**; without it the sweep test in §6 is the sole detector.
4. **Wire change without a `wire_version` bump** — correct per the standing ruling, but every producer and
   consumer must land together. Not deployed, so no flag day.
5. **Not addressed here (deliberate):** the CTS/ACK *terms* of the NAV estimate still use our own CR — 3 B on the
   routing SF, tens of ms against a DATA term of hundreds. And the anti-spam ledger's CTS term likewise. Both are
   documented in-source as noise-level residuals.

## 8. Gate (beyond the standard one)

Standard gate applies (`docs/2026-07-26-slice-gate-method.md`): native EXACT-or-declared, **all 30 scenarios
`cmp`-identical** — expected, since no corpus scenario has a third node overhearing a mixed-CR CTS, so state that
byte-identity proves **inertness only** — `sizeof(Node)` unchanged, boards 10/10 with ΔRAM 0, `-Werror=switch`
clean, sim 27 binaries.
Plus: the §6 exhaustive sweep; a **before/after NAV-duration table** for the three shapes in §1 showing the
shortfall reaching **0**; and confirmation that a uniform-CR mesh moves by **≤4 bytes** of reservation.
Docs owed (QA-authored): `frames.md`'s CTS byte-3 row + its "flags nibble is full" note, and `protocol.md` §2's
NAV-from-CTS deferral paragraph, which this retires.
