# Codebase review — verified triage + wave plan (2026-07-04)

Companion to `docs/2026-07-04-codebase-review-findings.md`. Every finding was independently re-verified against the CURRENT source (34-agent verification workflow, 2026-07-04). This doc is the implementation tracker.

## ★ Status update (2026-07-14) — reconciled vs `HEAD`; this doc is the plan for the NEXT (cleanup) task
- **Waves 1–5 + cleanup + L9: DONE + COMMITTED** (native 587→601, since folded into the mobile / plane-sep / feature-split / command-sink commits; native now ~692+). Closed.
- **Waves 6 (radio H6/M11/L5) + 7 (OTA H2): still `BENCH-VERIFY-PENDING`** — code is in, but the on-metal A/B is not confirmable from source. The lingering open item from the original triage.
- **M8: NO LONGER "keep-as-is" — IN PROGRESS / CLOSING.** Remote-admin is being built: designs `2026-07-13-remote-management-auth-design.md` / `-command-sink-consolidation-design.md` / `-remote-binary-response-encoders-design.md` close M8, and commit **`4a99db0`** is a committed **first version with authentication (not yet fully tested).** Supersedes the line-14 decision below.
- **Other not-fixing calls stand** (H1 OTA-auth, S3, S4, L1 — accepted honest-node risk). The sealed-DM remote-auth is a first concrete step toward S4's deferred signing initiative (admin surface only).
- **Cleanup-prep numbers re-verified 2026-07-14:** `node.h` **1614** (↑ from 1595), `fw_main.cpp` **2755** (↑ from 2698 — growing *during* the consolidation churn), tracked `._*`/metadata **89** (exact), CI still **6 board envs (no `production`, no `lus` sim**, and behind the ~8-env feature-split set). The growth reinforces the split.
- **★ CLEANUP GATE CLEARED (2026-07-14):** the command-sink consolidation (`4a99db0`) is now **tested** (user-confirmed — behaviour/output-parity verified), so the structural cleanup PROCEEDS. **Focus = fully code cleanup.** Front of the sequence: the two cheap guardrails (`._*` untrack + CI `production`/`native`/`lus`/profile-envs), then the `fw_main` responsibility split → `Node` state legibility → data-carrier hygiene → docs. Behavior-preserving; keep s18 + native gates.

## Verified counts
**30 confirmed · 1 false-positive · 0 already-fixed.** Corrections worth noting:
- **M5 — FALSE POSITIVE** (was marked `[verified]`). `pack_data`'s empty-mac zero-fill is unreachable for CRYPTED (the sole caller `node_mac.cpp:1003` always passes an 8-byte seed) *and* the seed has an all-zero `bad_rng` reject at seal (`node_hashlocate.cpp:285-289`). No nonce reuse. Only the `frame_codec.h:439` comment is stale.
- **M7a** — the `nonce_seed` half was already fixed post-review (`node_join.cpp:393`); only the `type` drop remains.
- **S1** verification surfaced **2 uncited** `nonce_seed`-drop sites: `node_mac_rx.cpp:861` (`drain_xl_handoffs_for_leaf`) + `:1004` (long-busy requeue). The helper closes these too.

## Product decisions (2026-07-04, user)
- **H1 (OTA auth / signature): WON'T IMPLEMENT.** Matches MeshCore's honest-node model (no OTA auth there either). Accepted risk.
- **H2 (OTA partial-image commit): FIX** — ensure the image is COMPLETE before `Update.end(true)` (the completeness guard; NOT the auth rewrite).
- **M8 (remote `reboot`/`prep-restart` auth): ~~KEEP AS-IS~~ → NOW IN PROGRESS (2026-07-14).** The "address when remote-admin is implemented" condition is now met — remote-admin is being built (2026-07-13 designs + committed first version `4a99db0`), which closes M8. See the status update above.
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
| **7 — OTA completeness** | H2 (completeness guard only; H1 skipped) | flash | ✅ CODE DONE + esp32-compiles, ⚠ **BENCH-VERIFY** (OTA valid .bin flashes+boots; a truncated .bin is REJECTED not flashed). Kept raw-body (multipart broken on this fork); OOM for very large images = pre-existing, streaming-rewrite follow-up. uncommitted |
| cleanup | L11 (f64 hand-roll, device-safe), L12 (inline ISR flags), M5 comment; `protocol_constants.h:362` fixed in Wave 5 | native+xiao | ✅ DONE (601, xiao builds, uncommitted) |
| **L9 (was deferred)** | 4-bit ctr_lo → flight_gen migration: 6 LOCAL flight-match sites (TxStashSlot re-arm ×4 incl. the uncited node.cpp:939 giveup + _nack_wait re-RTS) exact-matched; the WIRE NACK match (node_mac_rx.cpp:945) stays ctr_lo (wire-bounded, documented) | native + s18 sim-parity | ✅ DONE (601; s18 BYTE-IDENTICAL pre/post — 240119 events, 0 asserts). ⚠ needed a cross-repo sim-wrapper fix (`NodeRuntimeWrapper.cpp` dropped the removed `channel_origin_max_per_window`, wired the new anti-spam knobs) — lus was broken since the anti-spam removal. uncommitted (both repos) |
| not-fixing | M8, S3, S4 (see decisions above); **L1** deferred with S3 (decode-strictness class, benign today) | — | recorded |

## Cleanup preparation review (2026-07-13)

This is a maintainability-focused follow-up, not a new correctness/security finding pass. It was read against the current worktree and is deliberately separate from the verified waves above. Do not mix these changes with a radio/protocol behaviour change.

### Current boundary

- The worktree contains an in-progress command/remote-management feature set, including the command-sink consolidation work. `src/fw_main.cpp` has a large uncommitted refactor (about 1,100 changed lines) that is already consolidating serial, BLE, and remote command dispatch through `dispatch(line, len, Print&)`.
- **Finish, output-parity-test, and commit that refactor before starting the structural cleanup below.** Do not overlap a broad file move or handler rewrite with it.
- The feature-profile split is also current work. Retain its rule: feature state is compiled out, while the public API supplies inert stubs, so callers remain stable.

### Confirmed structural hotspots

| Area | Evidence | Cleanup implication |
|---|---|---|
| `Node` ownership | `lib/core/node.h` is ~1,595 lines and owns routing, MAC flights, timers, channel flood, join, identity/hash lookup, mobile/team, gateway scheduling, and their state | The existing `.cpp` partition is useful, but it is not a module boundary while every plane reaches the same private state. Keep `Node` as the façade; introduce explicit state ownership before extracting classes. |
| Timer dispatch | `Node::on_timer()` owns 77 timer ids/ranges and their slot arithmetic | Replace raw numeric ids/ranges incrementally with typed, plane-owned timer declarations. Preserve the numeric allocation and add collision/range checks before changing behaviour. |
| Firmware integration | `src/fw_main.cpp` is ~2,698 lines: hardware lifecycle, persistence, config mutation, console commands, BLE, remote management, serialization, and mesh loop | After command-sink consolidation, split by responsibility: command/config logic, transports, and board/runtime orchestration. Do not create a generic framework. |
| Firmware test seam | Native tests intentionally set `test_build_src = no`; thus `fw_main.cpp` is not compiled by the unit suite | Extract pure parsing/config-validation/formatting units first and add native tests. Device-dependent handlers remain board-tested. |
| Test coupling | `test/test_dual_layer.cpp` uses a broad `DualLayerTestAccess` friend seam into `Node`/`LayerRuntime` | For new tests, prefer observable protocol outcomes or small purpose-built seams. Reduce the broad friend seam only as affected state gains stable accessors. |
| CI coverage | `.github/workflows/firmware.yml` builds six board environments but excludes `production`; the simulation driver is also not built in CI | Add `production` and a `pio run -e native` build step before broad cleanup. Add static analysis/format enforcement separately from protocol changes. |
| Repo hygiene | 89 tracked AppleDouble/metadata files (`._*`/`.DS_Store`) remain despite ignore rules | Remove them in one hygiene-only change, then add a CI guard that prevents reintroduction. |
| Tooling | `tools/dm_delivery_breakdown.py` is ~2,516 lines and untested | Defer its split until firmware/core cleanup is stable; it is isolated from the embedded critical path. |

### Recommended cleanup sequence

1. **Land the active command-sink consolidation.** Capture the documented serial/BLE output-parity battery first; verify all supported board profiles. This removes active duplication and creates the safe command boundary.
2. **Improve the guardrails.** Add the missing CI builds (`production`, native simulation driver), a formatter/static-analysis configuration, and focused native tests for any newly extracted pure firmware logic.
3. **Split firmware by responsibility.** Extract `firmware_config` (parse/validate/apply/persist), `firmware_commands` (dispatch and human-facing handlers), and transport adapters. Leave startup, radio polling, sleep scheduling, and board glue in a deliberately small `fw_main.cpp`.
4. **Make `Node`'s private state legible.** First move state records/`LayerRuntime` into dedicated private headers; then group state and helpers by routing, MAC, channel, identity, join, mobile/team, and gateway planes. Maintain the `Node` public façade and fixed-memory layout.
5. **Clean recurring data-transfer hazards.** Continue using a single conversion path for `TxItem`, `PendingTx`, and `PostAck`; never hand-rebuild these data carriers at a new call site. This is the structural prevention for the already-fixed field-drop class (S1/L9).
6. **Perform isolated hygiene/documentation work.** Untrack AppleDouble files, add a short current architecture/index document, and label historical plans/specs as archival rather than deleting design history.

### Constraints for every cleanup wave

- A cleanup must be behavior-preserving unless it is explicitly tracked as a protocol/robustness fix above.
- Keep the native all-features build and simulator byte-identity gates used by the feature-split plan; run device/profile builds for firmware-facing work.
- Avoid combining file moves, formatting churn, and semantic edits in one change.
