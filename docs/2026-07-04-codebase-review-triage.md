# Codebase review — verified triage + wave plan (2026-07-04)

Companion to `docs/2026-07-04-codebase-review-findings.md`. Every finding was independently re-verified against the CURRENT source (34-agent verification workflow, 2026-07-04). This doc is the implementation tracker.

## Verified counts
**30 confirmed · 1 false-positive · 0 already-fixed.** Corrections worth noting:
- **M5 — FALSE POSITIVE** (was marked `[verified]`). `pack_data`'s empty-mac zero-fill is unreachable for CRYPTED (the sole caller `node_mac.cpp:1003` always passes an 8-byte seed) *and* the seed has an all-zero `bad_rng` reject at seal (`node_hashlocate.cpp:285-289`). No nonce reuse. Only the `frame_codec.h:439` comment is stale.
- **M7a** — the `nonce_seed` half was already fixed post-review (`node_join.cpp:393`); only the `type` drop remains.
- **S1** verification surfaced **2 uncited** `nonce_seed`-drop sites: `node_mac_rx.cpp:861` (`drain_xl_handoffs_for_leaf`) + `:1004` (long-busy requeue). The helper closes these too.

## Product decisions (2026-07-04, user)
- **H1 (OTA auth / signature): WON'T IMPLEMENT.** Matches MeshCore's honest-node model (no OTA auth there either). Accepted risk.
- **H2 (OTA partial-image commit): FIX** — ensure the image is COMPLETE before `Update.end(true)` (the completeness guard; NOT the auth rewrite).
- **M8 (remote `reboot`/`prep-restart` auth): KEEP AS-IS** for now — address when remote-admin is implemented.
- **S4 (control-plane signing): KEEP** — accepted honest-node design; the signing layer is a future initiative (the MeshCore signed-advert PUSH is the blueprint).
- **S3 (decode strictness): NOTE only** — not a priority now.

## Wave plan
| Wave | Items | Testable | Status |
|---|---|---|---|
| **1 — S1 helper** | H4, M7(type), +2 uncited nonce_seed drops | native | ✅ DONE (587, uncommitted) |
| **2 — flash validation (S2)** | M1, M2, L4, H3 | native | ✅ DONE (589, uncommitted) |
| **3 — untrusted-wire** | M4, M6, M9, L7 | native | ✅ DONE (594, uncommitted) |
| **4 — config validation** | M10, L2, L3, L6 | native | ✅ DONE (597, uncommitted) |
| **5 — duty/crypto** | H5, M3, L10, L13 | native | ✅ DONE (601, uncommitted) |
| **6 — radio (bench-verify)** | H6, M11, L5 | flash | ⚙️ CODE DONE + xiao-compiles (601 native unaffected), ⚠ **BENCH-VERIFY-PENDING** (M11 dual-SF `routing_sf=7`/`sf_list={12}` delivery A/B; H6 stress-harness delivery+rxbad under load; L5 watch `status rxarm=`); uncommitted |
| **7 — OTA completeness** | H2 (completeness guard only; H1 skipped) | flash | — |
| cleanup | L11, L12, M5 comment, `protocol_constants.h:362` comment | — | — |
| deferred | L9 (4-bit ctr_lo → flight_gen migration, `risk medium`, sim-parity re-run) | — | — |
| not-fixing | M8, S3, S4 (see decisions above); **L1** deferred with S3 (decode-strictness class, benign today) | — | recorded |
