<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile v1 — Slice 1: wire marks (codec only) — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-07). **Slice 1 of the mobile-node v1** (design: `2026-07-07-mobile-node-handling-assumptions.md` §14/§17). The user commits; I quality-gate. **Codec-only — pack/parse three mobile marks; NO behaviour change** (no `node_mac_rx` / routing logic this slice). Backward-compatible.

## Why
The mobile last-mile reuses the normal id-addressed handshake, but a mobile's **local id** can collide with a normal node's global id, so three frames must self-describe (§17):
- **`addr_len = 1`** on **RTS** and **DATA** — the `next` is a mobile local-id (receiver mark).
- a **`MOBILE` bit** on **RTS** (byte-5 b1) — the `src`/originator is a mobile (sender mark; also the team M_BROADCAST mark).
- a **`MOBILE` bit** on **ACK** (byte-1 b1) — the `to` is a mobile local-id.

This slice only makes the codec **carry and round-trip** these bits. Nothing reads them yet (that's Slice 2+). `addr_len` today is "hierarchy-deferred" and `parse_*` **rejects** any non-zero — we widen that to accept `1` (mobile), still rejecting `2..7`.

## Scope / non-goals
- **In:** `frame_codec.h` struct fields + `frame_codec.cpp` pack/parse for RTS / DATA / ACK, + codec round-trip tests.
- **OUT (later slices):** any use of the marks in `node_mac_rx` / routing / anti-spam; the local-id itself; registration. **Do not touch behaviour files this slice.**
- **Backward-compat:** existing senders emit `addr_len=0` / marks-clear, so existing frames parse exactly as before; accepting `addr_len=1` only affects frames a future mobile will send.

## Fix 1 — RTS (`addr_len` receiver mark + `MOBILE` sender bit)

**`lib/core/frame_codec.h`** — `rts_in` (line 215) gains two fields; `rts_out` (line 222) gains one (`addr_len` already exists there):
```cpp
struct rts_in {
    uint8_t  leaf_id; uint8_t src; uint8_t next; uint8_t ctr_lo;
    uint8_t  dst; uint8_t sf_index; uint8_t rts_flags; uint8_t payload_len;
    uint8_t  addr_len = 0;                 // NEW: 0=normal, 1=mobile-next (local id); 2..7 reserved (hierarchy)
    bool     mobile_src = false;           // NEW: MOBILE mark — src is a mobile local-id (byte-5 b1)
    uint16_t m_payload_id_lo16 = 0;
    uint32_t flood_channel_msg_id = 0;
    std::span<const uint8_t> flood_bitmap = {};
};
struct rts_out {
    uint8_t  leaf_id; uint8_t src; uint8_t next; uint8_t ctr_lo; uint8_t addr_len;
    bool     mobile_src = false;           // NEW: MOBILE mark (byte-5 b1)
    uint8_t  dst; uint8_t sf_index; uint8_t rts_flags; uint8_t payload_len;
    bool     m_broadcast; uint16_t m_payload_id_lo16;
    bool     flood = false;
    uint32_t flood_channel_msg_id = 0;
    size_t   flood_bitmap_off = 0;
};
```

**`lib/core/frame_codec.cpp` `pack_rts`** — byte-3 carries `addr_len`, byte-5 b1 carries `MOBILE`:
- Line 399 (byte 3):
```cpp
    w.u8(static_cast<uint8_t>(((in.ctr_lo & 0x0F) << 4) | ((in.addr_len & 0x07) << 1)));  // ctr_lo hi | addr_len b3..1 (1=mobile-next) | rsv b0
```
- Lines 401-402 (byte 5) — add the `MOBILE` bit at b1:
```cpp
    w.u8(static_cast<uint8_t>(((in.sf_index & 0x03) << 6) |
                              ((in.rts_flags & 0x0F) << 2) |
                              (in.mobile_src ? 0x02 : 0)));            // sf_index 7..6 | rts_flags 5..2 | MOBILE b1 | rsv b0
```
- (Optional guard, keep the pack honest:) right after the size check at the top of `pack_rts`, add `if (in.addr_len > 1) return 0;`.

**`lib/core/frame_codec.cpp` `parse_rts`** — accept `addr_len ∈ {0,1}`, read the `MOBILE` bit:
- Line 425 — widen the reject:
```cpp
    if (o.addr_len > 1) return std::nullopt;                        // 0=normal, 1=mobile-next (§17); 2..7 hierarchy-deferred → reject
```
- After line 427 (`o.rts_flags = …`) add:
```cpp
    o.mobile_src = (b5 & 0x02) != 0;                               // MOBILE mark (byte-5 b1)
```

## Fix 2 — DATA (`addr_len` receiver mark) — reject-widen only
`data_in`/`data_out` **already carry `addr_len`** and pack/parse it (byte-0 b3..1); only the two rejects widen. **No struct change.**
- **`pack_data`** line 719:
```cpp
    if (in.addr_len > 1) return 0;                                  // 0=normal, 1=mobile-next (§17); 2..7 deferred
```
- **`parse_data`** line 755:
```cpp
    if (o.addr_len > 1) return std::nullopt;                        // 0=normal, 1=mobile-next (§17); 2..7 deferred
```

## Fix 3 — ACK (`MOBILE` bit — `to` is a mobile local-id)

**`lib/core/frame_codec.h`** — `ack_in` (182) and `ack_out` (183) each gain `bool mobile_to = false;`:
```cpp
struct ack_in  { uint8_t ctr_lo; uint8_t budget_hint; uint8_t snr_bucket; uint8_t to; bool warn = false; bool mobile_to = false; };
struct ack_out { uint8_t ctr_lo; uint8_t budget_hint; uint8_t snr_bucket; uint8_t to; bool warn = false; bool mobile_to = false; };
```

**`lib/core/frame_codec.cpp` `pack_ack`** — byte-1 b1 carries `MOBILE` (line 364):
```cpp
    w.u8(static_cast<uint8_t>((bh << 6) | ((in.snr_bucket & 0x03) << 4) | (in.mobile_to ? 0x02 : 0) | (in.warn ? 0x01 : 0)));  // b1 MOBILE (to is a mobile local-id) · b0 AIRTIME_WARN
```

**`lib/core/frame_codec.cpp` `parse_ack`** — after line 382 (`o.warn = …`):
```cpp
    o.mobile_to   = (b1 & 0x02) != 0;   // MOBILE mark (byte-1 b1): the `to` is a mobile local-id (§17)
```

## Fix 4 — documentation (`docs/frames.md` + `docs/protocol.md`)
The marks are now real on the wire, so pin the exact bits and drop the "PLANNED / not yet in `parse_*`" hedging. **`frames.md` is structure-only** (bits + field-meaning, not behaviour) so this belongs in the codec slice; **`protocol.md`** gets a one-paragraph forward-reference (the behaviour itself lands in later slices).

**`docs/frames.md`** (edit these existing spots):
1. **Conventions — "Mobile last-mile addressing" bullet:** drop "Not yet in `parse_*`"; pin both `MOBILE` bits + add the RTS sender bit:
   > … `addr_len = 1` on **RTS** & **DATA** (`next` is a local id), a **`MOBILE` bit** on **RTS byte-5 b1** (`src` is a mobile / mobile-originated) and on **ACK byte-1 b1** (`to` is a local id), and **CTS by context**. The codec round-trips these (marks default 0 → backward-compatible); how a node *acts* on them is in the mobile-node behaviour slices.
2. **RTS byte-3 row:** `b3..1 = addr_len (`0` = normal; `1` = mobile-next — `next` is a LOCAL id) · b0 rsv` — drop "PLANNED".
3. **RTS "★ Flag space" note:** pin **byte 5 b1 = the `MOBILE` bit** (the `src` is a mobile local-id / the frame is mobile-originated, §17); byte 5 b0 + byte 3 b0 remain `rsv`. Keep the `addr_len=1` sentence, present-tense (drop "claims").
4. **RTS "Mobile-originated M_BROADCAST" note:** it self-marks via **the same `MOBILE` bit (byte-5 b1)** — unify (one bit means "src is a mobile", unicast or broadcast). Drop "PLANNED".
5. **DATA byte-0 row:** `b3..1 = addr_len (`0` = normal; `1` = mobile-next)` — drop "PLANNED".
6. **ACK byte-1 row:** `… b3..1 rsv (**b1 = `MOBILE`** — the `to` is a mobile local-id) · b0 = `AIRTIME_WARN`` — pin b1, drop "PLANNED".
7. **CTS "no mark, by design" note:** drop "PLANNED" (the by-context decision is final; no CTS codec change this slice).

**`docs/protocol.md`** — add a short note near the RTS→CTS→DATA→ACK handshake description:
   > **Mobile marks.** A mobile uses a home-assigned LOCAL id that can collide with a global id, so **RTS/DATA carry `addr_len=1`** (`next` is a mobile local-id), **RTS a `MOBILE` bit** (byte-5 b1 — the `src`/originator is a mobile), and **ACK a `MOBILE` bit** (byte-1 b1 — the `to` is a mobile local-id); **CTS relies on the marked-RTS context**. They keep the mobile plane's local-ids distinct from global ids. Wire layout = `frames.md`; the behaviour (registration, last-mile, the mobile plane) is in the mobile-node slices — design `docs/superpowers/specs/2026-07-07-mobile-node-handling-assumptions.md`.

**Doc gate:** review-only (no build impact), but the bit positions in both docs MUST match the codec edits above (RTS `MOBILE`=byte-5 b1, ACK `MOBILE`=byte-1 b1, `addr_len=1`=mobile-next).

## Tests — `test/test_frame_codec.cpp` (doctest; `CHECK` only, guard optional derefs)
Add one `TEST_CASE`:
```cpp
TEST_CASE("mobile marks — RTS/DATA addr_len + RTS/ACK MOBILE round-trip (Slice 1)") {
    uint8_t buf[43];

    // RTS: addr_len=1 (mobile-next) + mobile_src round-trips
    rts_in ri{}; ri.leaf_id=4; ri.src=17; ri.next=42; ri.ctr_lo=3;
    ri.dst=42; ri.sf_index=0; ri.rts_flags=0; ri.payload_len=10;
    ri.addr_len=1; ri.mobile_src=true;
    size_t n = pack_rts(ri, buf); CHECK(n == 7);
    auto ro = parse_rts({buf, n});
    CHECK(ro.has_value());
    if (ro) { CHECK(ro->addr_len == 1); CHECK(ro->mobile_src == true);
              CHECK(ro->src == 17); CHECK(ro->next == 42); }

    // RTS marks default clear (backward-compat)
    rts_in ri0{}; ri0.leaf_id=4; ri0.src=17; ri0.next=42; ri0.ctr_lo=3; ri0.dst=42; ri0.payload_len=1;
    n = pack_rts(ri0, buf); CHECK(n == 7);
    auto ro0 = parse_rts({buf, n});
    CHECK(ro0.has_value());
    if (ro0) { CHECK(ro0->addr_len == 0); CHECK(ro0->mobile_src == false); }

    // addr_len=2 rejected: craft byte-3 with addr_len=2 in an otherwise-valid RTS -> parse nullopt
    buf[0] = wire::cmd_byte(wire::Cmd::R, 4);
    buf[1] = 17; buf[2] = 42;
    buf[3] = static_cast<uint8_t>((3u << 4) | (2u << 1));   // ctr_lo=3 | addr_len=2
    buf[4] = 42; buf[5] = 0; buf[6] = 1;
    CHECK_FALSE(parse_rts({buf, 7}).has_value());

    // ACK: mobile_to round-trips, and warn stays independent
    ack_in ai{}; ai.ctr_lo=3; ai.budget_hint=1; ai.snr_bucket=2; ai.to=42; ai.warn=true; ai.mobile_to=true;
    n = pack_ack(ai, buf); CHECK(n == 3);
    auto ao = parse_ack({buf, n});
    CHECK(ao.has_value());
    if (ao) { CHECK(ao->mobile_to == true); CHECK(ao->warn == true); CHECK(ao->to == 42); }
    // ACK mobile_to defaults clear
    ack_in ai0{}; ai0.ctr_lo=3; ai0.to=42;
    n = pack_ack(ai0, buf); CHECK(n == 3);
    auto ao0 = parse_ack({buf, n});
    CHECK(ao0.has_value());
    if (ao0) { CHECK(ao0->mobile_to == false); }
}
```
Add an equivalent DATA check (build a minimal valid `data_in` with `addr_len=1`, `pack_data` → `parse_data`, `CHECK(out.addr_len == 1)`; then `addr_len=2` → `CHECK(pack_data(...) == 0)`).

## Build gate
- `pio test -e native` — the new case + **all existing codec tests pass** (backward-compat: existing round-trips unchanged, since they use `addr_len=0`/marks-clear).
- `pio run -e xiao_sx1262` (+ `heltec_v3`, `xiao_esp32s3`, `gateway`) compile — `frame_codec` is shared; the new struct fields must not break any pack/parse call-site (all default-init, so existing call-sites are unaffected).

## Sites
`lib/core/frame_codec.h` — `rts_in`(215), `rts_out`(222), `ack_in`(182), `ack_out`(183). · `lib/core/frame_codec.cpp` — `pack_rts`(399,401-402 + top guard), `parse_rts`(425 + after 427), `pack_data`(719), `parse_data`(755), `pack_ack`(364), `parse_ack`(after 382). · `test/test_frame_codec.cpp` — one new `TEST_CASE`. · `docs/frames.md` — 7 spots (Conventions bullet + RTS byte-3/Flag-space/M_BROADCAST + DATA byte-0 + ACK byte-1 + CTS note). · `docs/protocol.md` — one "Mobile marks" note. **No other file changes.**
