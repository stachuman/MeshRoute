# MeshRoute codebase review — findings & proposed fixes

**Date:** 2026-07-04
**Scope:** full firmware review (`lib/core`, `lib/hal`, `lib/console`, `src/`), with emphasis on firmware-class errors — buffer overruns, USB/BLE stability, ISR races, crypto misuse, and flash robustness — plus overall quality, tests, and repo hygiene.
**Audience:** implementer (Opus). Each item gives location, defect, concrete failure scenario, and a fix direction. Findings marked **[verified]** were re-read and confirmed against the current source by the reviewer; others are reported by subsystem review and should be confirmed at the cited line before fixing.

## TL;DR

The wire/codec and console hot paths are genuinely strong: bounded `Reader`/`Writer` cursors make out-of-bounds access a structural impossibility, wire-derived node ids index full 256-element arrays (no id-as-index overflow), and the USB-CDC anti-wedge design (`console_sink.h` drop-never-block) directly and correctly addresses the classic nRF52 TinyUSB wedge the user worried about. **No classic stack/heap buffer overrun was found on the radio-input path.**

The real risks cluster in five places:

1. **The ESP32 WiFi OTA path** — open AP, no auth, no image validation, and an upload handler that can flash a *partial* image. Least-mature subsystem by a wide margin.
2. **Field-by-field struct rebuilding** — `TxItem`/`PendingTx`/`PostAck` are re-materialized by hand at ~10 sites; three real bugs all trace to a recently-added field (`type`, `nonce_seed`) being forgotten on a requeue path. This breaks encrypted-DM delivery precisely in the congested case.
3. **Trusting persisted/config state that crossed a flash boundary** — `fault_log` head and segmented-inbox meta are used as indices/divisors without range validation, giving OOB-write and boot-hang paths from a torn flash blob.
4. **Regulatory duty-cycle under-counting** — the 64-slot airtime ledger silently drops in-window records for a busy relay, so the node can transmit past its legal 1% duty.
5. **A radio ISR flag race** — RxDone landing mid-arm is consumed as TxDone, which can truncate a frame on air under dense traffic.

Beyond correctness, the entire control plane (join/deny, leaf-config, beacon id-binding) trusts unauthenticated wire input. That is the stated honest-node threat model, but the single-frame leverage points are worth recording for the eventual signing layer.

---

## Critical / High severity

### H1 — ESP32 OTA: open SoftAP, no auth, no image validation **[verified]**
**`src/device_ota.cpp:105`, `:52`** — `WiFi.softAP("MeshRoute-OTA")` starts an *open* network and `/update` accepts a POST with no token/password; `Update` validates only the ESP image magic byte — no signature or SHA-256.
**Failure:** anyone in WiFi range while `ota` mode is active joins the AP and POSTs arbitrary firmware, which is flashed and booted.
**Fix:** require a passphrase (`WiFi.softAP(ssid, psk)` using at least the device PIN), require a token on `/update`, and verify an expected image hash (or a signed image) before `Update.end()`.

### H2 — ESP32 OTA: upload handler mis-derives image size and can commit a partial image **[verified]**
**`src/device_ota.cpp:55-96`** — `total` comes from `arg("plain").length()` or `client().available()` (currently-buffered TCP bytes), never Content-Length. The read loop does `if (r == 0) break;` (treats a momentary stall as EOF), and `Update.end(true)` finalizes even when `written < image`.
**Failure:** (a) the `"plain"` String buffers the whole firmware body → OOM on a multi-hundred-KB image; (b) a short/stalled upload commits a truncated image and reboots → brick.
**Fix:** use the standard streaming `HTTPUpload` callback, take size from the multipart/Content-Length header, and only `Update.end(true)` when `Update.isFinished()`. This subsystem reads like a bring-up placeholder and should not ship as-is.

### H3 — `cfg set` rewrites the whole NV blob on every call — flash wear + corruption window **[verified]**
**`src/fw_main.cpp:655`, save path `src/fw_main.cpp:138-166` / `device_nv.h`** — every `cfg set` does `remove()` + `open(WRITE)` + whole-blob `write()` on InternalFS with no change detection; setting a key to its current value still rewrites. A reset during that window is exactly the FS-metadata corruption the code elsewhere works hard to self-heal (`mount_or_repair`).
**Failure:** a companion-app UI slider bound to `cfg set` (10–50 writes/s) drives rapid wear and multiplies the reset-during-write brick probability.
**Fix:** `memcmp` the serialized blob against the loaded one and skip the write when identical; debounce/coalesce app-driven `cfg set`.

### H4 — Cascade requeue drops `type` and `nonce_seed` → encrypted DMs undeliverable, typed frames misrouted **[verified]**
**`lib/core/node_cascade.cpp:203-215`** — `try_cascade_requeue` rebuilds the `TxItem` field-by-field but omits `it.type` and the 8-byte `nonce_seed`, while `do_data_tx` (`node_mac.cpp:1003`) reads both. (Confirmed: the copy sets `flags`, `ctr`, `inner`, hop budget, etc. — no `type`, no `nonce_seed`.)
**Failure:** (a) a CRYPTED DM that exhausts its candidates and requeues is re-flown with an all-zero nonce seed → recipient `dm_open` tag-fails → hard drop, *exactly in the congested case that triggers requeues*; (b) a typed frame (H_ANSWER, E2E_ACK, REMOTE_CMD) requeues as type 0 → delivered as a junk plain DM, ack lost.
**Fix:** copy both fields. Better: introduce one `txitem_from_pending(pt)` helper and use it at all rebuild sites (see S1) so the field-drop class cannot recur.

### H5 — Airtime duty-cycle ledger under-counts a busy relay → transmits past legal duty **[verified]**
**`lib/hal/airtime_ledger.cpp:7-15`, cap `airtime_ledger.h:21` (`kCap = 64`)** — `record()` drops the oldest entry when the 64-slot ring is full even if it is still inside the 1-hour duty window, so `used_in_window()` under-reports.
**Failure:** at 1% duty with ~80 ms frames a relay may legally send ~450 frames/hour ≫ 64; every TX past 64 evicts an in-window record, `duty_over_budget` reads low, and the node keeps transmitting **beyond its EU868 regulatory duty cycle**.
**Fix:** on overflow, coalesce the two oldest records (sum airtime, keep newer `end_ms`) instead of dropping; or size `kCap` from `window / min_airtime`; or carry a `_dropped_airtime_ms` aggregate credited until its end passes the cutoff.

### H6 — Radio ISR: RxDone between flag-clear and TX-arm is consumed as TxDone **[verified at cited lines by review]**
**`lib/hal/device_radio.h:144-145`, `:158-169`** — `start_transmit()` clears `g_dio1_fired` *then* calls `startTransmit()`; an RxDone edge in that gap (radio still in RX until the mode switch completes) re-sets the flag, and `poll_tx_done()` consumes it as TxDone.
**Failure:** a neighbor's packet finishing reception in that window makes the next `service_tx()` declare TX done and force the radio back to RX — truncating the outgoing frame on air. Recurring, hard-to-diagnose TX corruption under dense traffic.
**Fix:** on the "TxDone" path, read the SX126x IRQ status register and confirm `IRQ_TX_DONE` is actually set (else treat as RX); or clear the flag *after* `startTransmit()` returns (safe — min LoRa airtime ≫ arm time).

---

## Medium severity

### M1 — `fault_log_valid` doesn't range-check `head`/`count` → OOB write in `fault_log_push` **[verified]**
**`lib/core/fault_log.cpp:21-30`** — validation checks only `magic` and `version` (the first, most-likely-to-survive bytes); `fault_log_push` writes `f.ring[f.head]` with the raw loaded `head` before wrapping.
**Failure:** a torn `/mrfault` blob with `head = 0x3FFF` passes validation; the next fault pushes a 28-byte record hundreds of KB past the struct — the fault log becomes the wild-write corruptor the radio canary exists to chase. `format_fault_summary` similarly spins up to 65 k iterations on an unvalidated `count`.
**Fix:** require `head < kFaultRingN && count <= kFaultRingN` in `fault_log_valid`, and mask `f.head % kFaultRingN` in `push`.

### M2 — Corrupt segmented-inbox meta → divide-by-zero or infinite loop at boot
**`lib/core/segmented_inbox_store.h:83`, `:125-129`, `:141-146`** — `load_meta` validates only magic+version, not `seg_count`/`head_seg`/`tail_seg`. `seg_count == 0` → `% 0` fault in the recount/append loops; `head_seg >= seg_count` → the mod-reduced loop index never equals `head_seg` → infinite loop at boot (device hang).
**Fix:** additionally require `seg_count == ring_segs() && head_seg < seg_count && tail_seg < seg_count` (treat failure as fresh meta), and/or add a CRC over the Meta blob.

### M3 — Parked send-by-hash loses the per-message crypt intent → silent plaintext downgrade
**`lib/core/node_hashlocate.cpp:601-609` (park), `:728`/`:770` (drain); `ParkedSend` in `node.h:1108`** — `send_by_hash(..., CryptIntent crypt)` threads `crypt` only on the immediate path; `ParkedSend` has no crypt field, and both drains call `do_send(...)` with the default intent, which resolves to `_cfg.e2e_dm` (default false).
**Failure:** with `e2e_dm=false`, a `sendhashx` (crypt=on) to an unresolved hash parks; when the binding arrives the DM flies **in cleartext** — contradicting the codebase's own "never silently falls back to cleartext" rule (`node.h:376`).
**Fix:** add a `crypt` byte to `ParkedSend`, stamp it in `park_send`, thread it through both drains.

### M4 — RREQ hop count taken from wire unclamped → `hops+1` wraps to a false 0-hop route
**`lib/core/node_route_discovery.cpp:143`** — `learn_route_via(f.origin, prev, static_cast<uint8_t>(f.hops + 1), ...)` runs before any TTL/cap check; `f.hops == 255` wraps to 0. The beacon-ingest path gates on `dv_hop_cap` but the RREQ branch has none. A 0-hop candidate outranks every real route (sorted hops-ascending) and gets the long neighbor TTL; the re-flooded RREQ re-grows from 0 at each relay.
**Fix:** reject `f.hops >= _cfg.dv_hop_cap` (or clamp) at the top of the RREQ branch, mirroring the beacon gate, before `learn_route_via` and re-flood.

### M5 — `pack_data` silently zero-fills the 8-byte nonce seed under CRYPTED **[verified]**
**`lib/core/frame_codec.cpp:723-745`** — when `DATA_FLAG_CRYPTED` is set the trailer is an 8-byte nonce seed, but an empty `in.mac` is accepted and zero-filled (`if (mac_zero) { ... w.u8(0); }`), producing a constant all-zero seed. The nonce then derives only from `(ctr, dst_key_hash32)`, so it repeats on 16-bit ctr wrap or any reboot that resets ctr → XChaCha20 keystream + Poly1305 key reuse against the shared pair key.
**Fix:** in `pack_data`, `return 0` on an empty `mac` when `CRYPTED` is set (the zero-fill convention is only meaningful for the placeholder 4-B MAC). Update the stale header comment at `frame_codec.h:439`.

### M6 — NAV self-silencing driven by attacker-controlled `payload_len`
**`lib/core/node_mac_rx.cpp:142-146`, `node_mac.cpp:623-628`** — `nav_duration_rts(nav_sf, r.payload_len)` feeds the unauthenticated wire byte (0..255) into airtime at `max_data_sf()` (SF12), so a single cheap 8-byte overheard RTS with `payload_len=255` arms NAV for multiple seconds; NAV is on by default.
**Failure:** an attacker broadcasts cheap RTS frames (next≠victim, `payload_len=255`) to continuously silence a victim's transmitter. Same amplification in `start_pending_rx_expiry` and the overheard-CTS path.
**Fix:** clamp the NAV/expiry payload estimate to `max_payload_bytes_hard_cap` (or a fixed conservative reserve) rather than trusting the wire byte at max SF.

### M7 — `l2c_enqueue_forward` / `gateway_doorstep_hold` drop fields on redirect/requeue
**`lib/core/node_join.cpp:382-395`** (drops `type`) and **`lib/core/node_cascade.cpp:386-396`** (drops `nonce_seed`) — same field-by-field-rebuild class as H4. A misdelivered E2E_ACK/H_ANSWER redirect is forwarded as type 0; a CRYPTED DM held across a gateway off-window is requeued undecryptable.
**Fix:** thread `type`/`nonce_seed` through both; fold into the S1 shared helper.

### M8 — Remote `reboot`/`prep-restart` reachable by any mesh peer
**`src/fw_main.cpp:1222-1274` (`remote_exec`, called from `loop()` at `:2170`)** — the query buffer trim is bounds-safe (verified), but there is no sender allow-list, and `reboot`/`prep-restart` are executable by any node that DMs the string. E2E DM auth is opt-in (off by default).
**Failure:** a malicious/malfunctioning node broadcasts `rcmd`-style DMs → fleet-wide reboot DoS.
**Fix:** gate `reboot`/`prep-restart` on an authenticated/pinned sender or require the DM be E2E-authenticated.

### M9 — Non-UTF-8 radio payload bytes pass through `console_json` unescaped → invalid NDJSON
**`lib/console/console_json.cpp:15-31`** — `JsonBuf::str()` escapes quotes/backslash/C0 controls but emits any byte ≥ 0x20 verbatim, including 0x7F and arbitrary 0x80–0xFF. DM/channel bodies are attacker-controlled.
**Failure:** a body with a lone `0xC3` yields a line that strict decoders (iOS `JSONDecoder`) reject — a hostile peer can make its messages (or a fragile surrounding stream) undecodable in the companion app.
**Fix:** validate UTF-8 in `str()` and emit `�`/`\u00xx` for invalid sequences (or escape all bytes ≥ 0x80). Quote/backslash/control handling itself is correct.

### M10 — Unvalidated numeric `cfg set` values persist a bricking RF/timer config
**`src/fw_main.cpp:503,511,522`** — `freq` (`atof`, no band check), `bw` (`atol`), `cr`, `beacon_ms` (`atol`, 0 accepted) are persisted without range checks, unlike `join`/`create` which validate.
**Failure:** `cfg set beacon_ms 0` → 0 ms beacon period → airtime storm after reboot; `cfg set freq 9999` drives the SX1262 out of band and persists it → RF dead until re-provisioned.
**Fix:** range-validate freq/bw/cr/beacon_ms the same way `join`/`create` do, before persisting.

### M11 — Modulation params written mid-RX in `start_transmit` (documented-elsewhere silicon bug)
**`lib/hal/device_radio.h:139-145` vs. the fix comment at `:189`** — `start_transmit()` issues `setSpreadingFactor`/`setBandwidth`/`setCodingRate` while in continuous RX with no `standby()`, though `set_rx_sf()` documents that SF only latches in STANDBY on the SX1262.
**Failure:** a dual-SF DATA leg arms while RX active; the SF write is dropped, the frame flies on the old SF (receiver misses it), and the airtime ledger debits the *requested* params — duty accounting diverges from reality.
**Fix:** `standby()` before the per-frame param writes (mirror `set_rx_sf`), or confirm RadioLib's `startTransmit` re-latches params and document that.

---

## Low severity (worth fixing, low blast radius)

- **L1 — `parse_data`/`parse_rts` decode strictness gaps.** `parse_data` accepts `CRYPTED` without `DST_HASH` though `pack_data` forbids it (`frame_codec.cpp:749`); `parse_rts` accepts a truncated M_BROADCAST frame and returns `id_lo16 = 0`, aliasing dedup (`frame_codec.cpp:430-437`). Benign today (downstream tag/length checks), but they violate documented invariants. Fix: mirror the encode invariants on decode; make M_BROADCAST strict like FLOOD.
- **L2 — `parse_u32_tok(max=0xFFFFFFFF)` wraps mod 2³²** (`console_parse.cpp:28-33`) — `beacon_period_ms 4294967296` parses as 0 instead of erroring. Fix: check `v > max/10` before the multiply.
- **L3 — `send 00000000` aliases a hash-send to id-send node 0** (`console_parse.cpp:212-220`) — `send_layer` rejects `h==0`; `send` doesn't. Fix: mirror the guard.
- **L4 — `rx_window_slop_ms` / `configure()` divide by unvalidated `bw_hz`** (`device_hal.h:37-39,76-80`) — a corrupt `radio_bw_hz == 0` → div-by-zero hard fault. Fix: clamp `bw_hz <= 0` to the 125000 default in `configure()`.
- **L5 — `startReceive()` failures ignored on every re-arm path** (`device_radio.h:166,182,192,204,271`) — one SPI glitch leaves the node permanently deaf with no counter/recovery. Fix: count failures like `_tx_timeouts`, retry next loop, surface in `status`.
- **L6 — `config_epoch` u16 wraparound de-syncs a leaf permanently** (`node_query.cpp:221-223`) — at 65535 the next write wraps to 0 and `leaf_config_synced()` goes false forever. Fix: saturate at 65534 or handle wrap.
- **L7 — FLOOD RTS-M / H `hop_left` taken from wire unclamped** (`node_channel.cpp:746`, `node_hashlocate.cpp:467-476`) — a forged `dst=255` gives a 255-hop TTL (dedup-bounded per node). Fix: clamp to `flood_hop_max`.
- **L8 — `_last_acked_from` refuses inserts when full instead of roll-evicting** (`node_mac_rx.cpp:448-451`) — defeats the `already_received` dedup under sustained distinct-flight load. Fix: roll-evict oldest like `record_seen_origin`.
- **L9 — DATA re-issue / NACK matching still keyed on 4-bit `ctr_lo`** (`node_mac.cpp:917-949`, `node_mac_rx.cpp:936-938`) — 1/16 aliasing can re-arm `awaiting_ack`/`awaiting_cts` against the wrong flight; unknown-reason NACK re-arms with no retry accounting. Fix: complete the `flight_gen` migration to these sibling paths.
- **L10 — `ecdh_shared` has no low-order-point / all-zero check** (`identity.cpp:47-49`) — a peer advertising a low-order X25519 point makes "sealed" DMs to it decryptable by any observer. Inside the TOFU threat model but the sender believes it's confidential. Fix: constant-time reject an all-zero shared secret before `dm_kdf`.
- **L11 — `JsonBuf::f64` uses `%.4g` despite the no-float-printf rule** (`console_json.cpp:43`) — latent: emits malformed JSON (`"key":`) the first time an `f64` field is sent on nRF52 (newlib-nano has no `%f`). No device call site today. Fix: hand-roll a fixed-point formatter or remove `f64` from the device path.
- **L12 — Header-local `static volatile` ISR flags are an ODR trap** (`device_radio.h:48-50`) — internal-linkage globals referenced from header-inline members; safe only because one TU includes it today. Fix: move flags+ISR to a .cpp, or make them C++17 `inline` variables (as `frame_trace.h` already does correctly).
- **L13 — `bridge_cross_layer` can push a handoff a single-layer node never drains** (`node_mac_rx.cpp:776-818`) — leaks the cap-1 `_xl_handoffs` slot. Fix: also refuse when `_n_layers < 2`.

---

## Systemic patterns (fix the class, not just the instance)

### S1 — Hand-rebuilt `TxItem`/`PendingTx`/`PostAck` structs
Three confirmed bugs (H4, M7×2) and two low ones share one root cause: these structs are re-materialized field-by-field at ~10 sites, and each time a field was added later (`type`, `nonce_seed`, `flight_gen`) some rebuild sites were missed. **Introduce a single `txitem_from_pending(const PendingTx&)` (and the inverse) helper, or group the identity+crypto fields into an aggregate sub-struct that copies as a unit, and route every rebuild site through it.** This closes H4, M7, and prevents recurrence. This is the highest-leverage structural fix in the report.

### S2 — Trusting state that crossed a flash boundary
The wire-input paths are exemplary at validation; persisted/config state is trusted unvalidated (M1 fault-log head, M2 inbox meta, L4 bw_hz, and the write-amplification of H3). **Adopt one rule: any struct loaded from flash gets a field-level range/CRC check before its fields are used as indices, divisors, or loop bounds** — the same posture already applied to wire input.

### S3 — Decode strictness is inconsistent
`parse_beacon` rejects trailing bytes while `parse_rts`/`parse_q`/`parse_h`/`parse_m` accept oversized frames, and several encode invariants (CRYPTED⇒DST_HASH, M_BROADCAST⇒9B) aren't mirrored on decode (L1, M5). Pick one policy — strict everywhere, as `parse_beacon` already argues — and encode the invariants as shared checks.

### S4 — Unauthenticated control plane (design-level, for the roadmap)
Every control plane trusts unauthenticated wire input — forged J_DENY can force a victim to renumber (`node_join.cpp:226-240`); any C frame can take over a leaf's config, e.g. `allowed_sf_bitmap=0` to mute it (`node_query.cpp:175-211`); any beacon is an authoritative id-binding (`node_beacon.cpp:503`). This is the stated honest-node model, but these are the single-frame leverage points an eventual signing/authentication layer should cover first. Record them; don't fix piecemeal.

---

## Quality, tests, CI, and repo hygiene

### Repo hygiene
- **97 AppleDouble `._*` files are tracked in git** despite `.gitignore` containing `._*` (committed before the rule; ignore doesn't untrack). Includes `docs/._.DS_Store` and editor temp doubles like `._ContactsView.swift.tmp.28540.*`. Fix in one commit: `git rm --cached $(git ls-files | grep -e '/\._' -e '^\._')`. The `git status` "everything modified" churn is likely volume metadata on `/Volumes/MeshRoute` — confirm with `git diff` before committing. Otherwise `.gitignore` is correct (no `.pio`, `.DS_Store`, `firmware.map`, or binaries tracked).

### Test coverage gaps
- ~585 doctest cases / ~3,300 assertions across 24 files — strong for the protocol core. But **`fw_main.cpp` (~2,200 lines, the single largest source file) is entirely untested**, as are `device_ota.cpp`, `device_ble.h`, `device_nv.h`. `test_build_src = no` means **none of `src/` is even compiled by the CI test job**. `node_mac.cpp`/`node_mac_rx.cpp` have only integration coverage, no isolated unit tests.
- **Action:** extract `fw_main.cpp`'s pure logic (command dispatch, formatting, config validation) into header-only units testable like `sched_send.h` already is, and add regression tests for the confirmed bugs above (H4 nonce-seed round-trip, M1/M2 corrupt-blob rejection, H5 ledger overflow).

### CI gaps (`.github/workflows/firmware.yml`)
- Solid: native tests + 6-env firmware matrix with caching. Gaps: **the `production` env (`MR_CONSOLE=0`, the shipping build) is not in the matrix** and can silently rot; **`sim_main.cpp` is never compiled** (no `pio run -e native` step); no `-Werror`, no static-analysis/lint job.
- **Action (cheap):** add `production` to the matrix and a `pio run -e native` step; add a CI guard that fails if `git ls-files | grep '/\._'` is non-empty (this repo regenerates AppleDouble files); add a `pio check` (cppcheck) job scoped to `lib/` + `src/`, and `-Werror` in CI only (not in `[common]`, to avoid breaking vendored/framework builds locally).

### General
- Vendoring is exemplary — `lib/monocypher` (v4.0.2, verbatim, with re-fetch instructions) and `lib/meshcore` (pinned commit, byte-identical policy, re-sync script) both carry clean `NOTICE` files. No action.
- Header hygiene is fully consistent (`#pragma once` everywhere, zero `#ifndef` guards). `platformio.ini` is exceptionally well-annotated.
- Missing: `.clang-format`, `.editorconfig`, any static-analysis config. `tools/dm_delivery_breakdown.py` is 2,516 lines and untested — a maintenance smell.
- **Load-bearing comments have drifted from code** in several spots the project itself declares "wire authority" (`frame_codec.h:439` mac trailer, the BCN leaf-header size comments, `protocol_constants.h` record-header size). Encode the load-bearing invariants as `static_assert`s / unit tests so they can't drift silently.

---

## Suggested implementation order

1. **H1/H2 (OTA)** — security-critical and self-contained; rewrite the ESP32 OTA handler or gate the feature off until it's done.
2. **S1 helper + H4/M7** — one refactor closes three confirmed delivery bugs and prevents recurrence.
3. **H5 (duty ledger)** — regulatory; small, localized change.
4. **M1/M2 + S2 rule (flash validation)** — closes an OOB-write and a boot-hang; apply the validation rule uniformly.
5. **H3, H6, M5, M6, M8** — wear/race/crypto/DoS; each localized.
6. **M-rest, then L-batch and S3** — decode strictness and remaining robustness.
7. **CI/hygiene** — untrack `._*`, add `production` + `native` to CI, add the AppleDouble guard.
8. **S4** — record as roadmap; don't patch piecemeal.
