# Anti-Spam Duty-Channel-Cap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: `superpowers:subagent-driven-development` or `superpowers:executing-plans`. Execute slices strictly in order (0 → 1 → 2 → 3 → 4 → 5 → 6); the two ★ blockers (MF1 per-hour-vs-per-window D, MF2 duty-disabled degenerate clamp) are PROVEN in native tests in Slice 1 before any cap ships. Each task is TDD: write the failing `doctest` test first, run `pio test -e native` and confirm FAIL, implement the minimal real code at the named symbol site, run and confirm PASS, then stage (the user does all commits — leave green work staged/uncommitted and report). ⚠ `REQUIRE` is UNAVAILABLE under `-fno-exceptions` — use `CHECK` + `if`-guards only. Read the real result from `.pio/build/native/program 2>&1 | grep "Status:"`. Verify every anchor by SYMBOL (grep the name), never by line number — the line anchors below are snapshots and drift.

**Goal:** Replace the two crude flat-20 anti-spam caps with a duty-anchored, SF- and mesh-aware **channel** per-origin cap, add per-origin **burst floors** (channel 10 s / DM 3 s), retire the redundant DM self-cap, and expose an advisory `limits` query + send-outcome feedback events so the iOS companion can predict and pace. The cap is `cap_origin = clamp(⌊C/N_active⌋, 1, ⌊C⌋)` where `C = D/T_ch` (D = the 5-minute channel-window duty budget, T_ch = FLOOD-RTS-M + DATA-M airtime at BW250), `N_active = max(1, ⌊channel_active_fraction · rt_count()⌋)`. **The two ★ blockers are proven in Slice 1:** MF1 — D is `duty_cycle · originator_window_ms` (5-min, 1% → 3000 ms), NEVER the 1-HOUR `_duty_cycle_budget_ms` (12× too loose); MF2 — `duty_cycle` defaults to 0 (duty plane inert) → an explicit duty-disabled branch falls back to the legacy flat cap (no new cap), and a `C ≥ 1` floor keeps the clamp from inverting even with duty ON.

**Architecture:** **Channel-only** (DMs rest on the duty plane + the existing per-physical-sender airtime backstop; only the DM 3 s self-throttle is added). Two DISTINCT enforcement sites: the **receiver-HOOK** `channel_origin_admit` (governs a node's relay of OTHERS' cleartext-keyed origins) and the **self-GATE** `do_send_channel` (governs the node's OWN posts, replacing the removed `self_originate_observe`). A dual-layer **gateway is EXEMPT** from every limit and is already fully out of the channel plane via the `n_layers==2` early-returns (`channel_origin_admit`/ingest) and the DM backstop's `RTS_FLAG_RELAY` skip — verify, do not re-derive (there is no `rts_relay_exempt` symbol); the one real gap (MF9) is a gateway's own e2e-ack/rcmd originations, handled by placing `dm_min_interval` INSIDE the own-origin branch and exempting those DataTypes. **Duty-disabled → legacy flat cap** (MF2). No over-the-air wire change; `limits` and the feedback events are BLE-local only.

**Tech Stack:** C++20 (`-fno-exceptions`, no heap in hot paths), `doctest` native tests (`pio test -e native`), the `lus` Lua-parity sim for s12 channel calibration, 4 device boards (XIAO nRF52840 + ESP32 variants) for the build gate.

## Shared interface contract

Use these EXACT names across slices; adapt VALUES to the real code, keep names consistent.

- **`protocol_constants.h`** — NEW:
  - `inline constexpr uint32_t channel_min_interval_ms = 10000;`  // per-origin channel burst floor (10 s)
  - `inline constexpr uint32_t dm_min_interval_ms = 3000;`  // self DM burst floor (3 s)
  - `inline constexpr uint8_t cap_channel_origin_events = 20;`  // = the value `channel_origin_max_per_window` had; the `ChannelOriginLedger.ev[]` array bound (MF7)
  - **REMOVE** `channel_origin_max_per_window` (currently `= 20` at protocol_constants.h:239). (`channel_origin_window_ms` stays — the sliding window is still used.)

- **`node.h` `NodeConfig` (Cfg)** — NEW:
  - `float channel_active_fraction = 0.125f;`  // deployment knob; `N_active = max(1, ⌊frac·rt_count()⌋)` (INERT for N<8 — floors at 1)
  - **REMOVE** `originator_self_cap_per_window` (Cfg field at node.h:159) and its removed uses at node_mac.cpp:391-406.

- **Node getter (public):** `uint32_t channel_duty_budget_ms() const`  // `= static_cast<uint32_t>(_cfg.duty_cycle * protocol::originator_window_ms)` — the 5-MINUTE D (returns 0 when `duty_cycle <= 0`). NEVER reuse `_duty_cycle_budget_ms` (that is the 1-HOUR budget). (MF1/MF8)

- **Node:** `uint16_t channel_cap_origin() const`  // (MF2) if `channel_duty_budget_ms() == 0` → return the legacy flat cap (keep a const for the old 20). Else:
  `T_ch = airtime_routing_ms(43) + airtime_ms(max_data_sf(), _cfg.radio_bw_hz, _cfg.radio_cr, protocol::preamble_sym, <M DATA-M len>)`;
  `uint32_t C = max(1u, channel_duty_budget_ms()/T_ch);`  // C ≥ 1 floor (MF1/MF3)
  `uint32_t Nact = max(1u, (uint32_t)(_cfg.channel_active_fraction * rt_count()));`
  `return clamp(C/Nact, 1u, C);`  // C ≥ 1 floor keeps the clamp from inverting

- **`ChannelOriginLedger` (node.h:766):** add a per-origin `uint64_t last_flood_ms` (for the 10 s min-interval); size `ev[]` by `protocol::cap_channel_origin_events` (was `channel_origin_max_per_window`).

- **`channel_origin_admit` (node_channel.cpp:78, receiver-HOOK for OTHERS' origins):** replace the flat-20 compare with `channel_cap_origin()`; ADD `reject if now - last_flood_ms < protocol::channel_min_interval_ms`. Preserve the self-bypass and the `n_layers==2` early-return (node_channel.cpp:79).

- **`do_send_channel` (node_channel.cpp:260, self-GATE for OWN posts):** apply `channel_cap_origin()` + the 10 s floor; **REMOVE** the `self_originate_observe()` at :270; emit `send_blocked{channel}` on rejection.

- **`node_mac.cpp` own-DM branch** (`origin == _node_id && !is_forward && !is_channel_m`, node_mac.cpp:391): **REMOVE** the self-cap defer (:394-405); ADD a `dm_min_interval` throttle keyed on a new `uint64_t last_dm_origin_ms` member INSIDE this branch (auto-skips relays/floods, MF9) AND exempt the e2e-ack / rcmd DataTypes; emit `send_blocked{dm}`.

- **`limits_snapshot()` (node.h/node_budget.cpp):** produce the `limits` JSON fields; `ch_next_ms`/`dm_next_ms = max(burst-floor remaining, channel cap-wait, duty recovery = duty_status().avail_ms)`; `duty_ms` on the 5-min basis = `channel_duty_budget_ms()`.

- **`console_json.{cpp,h}`:** add `write_limits(...)`; events `send_blocked{kind,reason,next_ms}`, `send_failed{kind,reason}`, `channel_sent{relayed}`. `SendFailReason` (command.h:99) += `no_cts, no_ack`. `PushKind` (command.h:83) additions as needed.

- **`fw_main.cpp`:** wire the `limits` verb into BOTH the USB dispatcher (~1314) and the BLE dispatcher (~1469-1495, alongside the existing `duty` handler at :1469).

- **Tests:** `doctest`, run `pio test -e native`. ⚠ `REQUIRE` is UNAVAILABLE (`-fno-exceptions`) → use `CHECK` + `if`-guards. Read the real result from `.pio/build/native/program 2>&1 | grep "Status:"`.

## File structure

| File | Responsibility (this initiative) |
|---|---|
| `lib/core/protocol_constants.h` | Add `channel_min_interval_ms`, `dm_min_interval_ms`, `cap_channel_origin_events`; REMOVE `channel_origin_max_per_window`. |
| `lib/core/node.h` | `channel_active_fraction` Cfg field (REMOVE `originator_self_cap_per_window`); `channel_duty_budget_ms()`/`channel_cap_origin()`/`limits_snapshot()` declarations; `ChannelOriginLedger` gains `last_flood_ms` + `ev[]` re-dimension; `last_dm_origin_ms` member; the legacy-flat-cap const. |
| `lib/core/node_routing.cpp` | Home of `channel_cap_origin()` + `channel_duty_budget_ms()` (rt_count-/duty-derived math: T_ch, C, N_active, clamps, duty-disabled fallback, C≥1 floor). |
| `lib/core/node_channel.cpp` | Receiver-HOOK `channel_origin_admit`: flat-20 → `channel_cap_origin()` + 10 s min-interval. Self-GATE `do_send_channel`: cap + 10 s floor, REMOVE `self_originate_observe()`, emit `send_blocked{channel}`. `channel_sent{relayed:false}` at the `channel_reoffer_fire` exhaustion branch (:601). |
| `lib/core/node_mac.cpp` | Own-DM branch: REMOVE the self-cap defer; ADD `dm_min_interval` (via `last_dm_origin_ms`) inside the own-origin branch, exempt e2e-ack/rcmd DataTypes; emit `send_blocked{dm}`; `send_failed{no_cts/no_ack}` on CTS/ACK giveups. |
| `lib/core/node_budget.cpp` | Drop `self_originate_observe`/`self_originate_count`; keep `track_originator_observation`/`compute_originator_metric`; host `limits_snapshot()` counter reads. |
| `lib/console/console_json.h` | Declare `write_limits(...)`; extend `SendFailReason` (+`no_cts,no_ack`) and `PushKind`; declare the send-outcome event writers. |
| `lib/console/console_json.cpp` | Implement `write_limits(...)` + `send_blocked`/`send_failed`/`channel_sent` writers; extend `pushkind_name`. |
| `src/fw_main.cpp` | The `limits` verb dispatch in BOTH the USB (~1314) and BLE (~1469-1495) surfaces. |
| `ios-companion/INBOX_SYNC_CONTRACT.md` | Document the `limits` query + the `send_blocked`/`send_failed`/`channel_sent` outcome events (companion-contract surface). |
| `test/test_node_channel.cpp` | Channel-plane tests: `channel_cap_origin()` formula (SF/N/BW dependence, clamps, duty-disabled fallback, C≥1 floor, BW250 sample table), admit drops at the computed cap not 20, channel 10 s min-interval, self-gate `send_blocked{channel}`, `channel_sent{relayed:false}`. |
| `test/test_node_r3.cpp` | MAC-plane tests: DM self-cap removal breaks nothing, `dm_min_interval` defers a <3 s own-DM + passes ≥3 s, e2e-ack/rcmd exemption, `send_blocked{dm}` + `send_failed{no_cts/no_ack}`, DM backstop + duty path unchanged. |
| `test/test_console_json.cpp` | `write_limits` JSON shape/values and the `send_blocked`/`send_failed`/`channel_sent` event writers. |

## Slicing + gates

- **Slice 0 — constants + scaled-D getter + ledger re-dimension:** add the two protocol min-interval consts + `cap_channel_origin_events`, the `channel_active_fraction` Cfg field, `channel_duty_budget_ms()`, and re-dimension `ChannelOriginLedger.ev[]` (+ `last_flood_ms`); remove `channel_origin_max_per_window` + `originator_self_cap_per_window`. **Gate:** `pio test -e native` green.
- **Slice 1 — `channel_cap_origin()` formula (the two ★ blockers proven):** the SF/N/BW math + clamps + duty-disabled fallback + C≥1 floor + the pinned BW250 `T_ch`/sample table. **Gate:** `pio test -e native` green (the MF1/MF2 native math tests must pass before any cap ships).
- **Slice 2 — admit-swap + channel 10 s floor + `do_send_channel` self-GATE:** receiver-HOOK cap+min-interval (others'), self-GATE cap+floor (own, remove `self_originate_observe`). **Gate:** `pio test -e native` green + the s12 channel-bearing sim calibration (legit dormant / flooder throttled — ⚠ use a larger N than s12's ≈12 to exercise the 1/N sharing).
- **Slice 3 — DM self-cap removal + `dm_min_interval`:** remove the flat self-cap defer; add the 3 s self-throttle inside the own-origin branch, exempt e2e-ack/rcmd. **Gate:** `pio test -e native` green.
- **Slice 4 — gateway-exemption VERIFICATION (no new code):** confirm a dual-layer gateway bridging high-rate cross-layer DM + channel trips no limit even when it appears as the origin on the far layer, and that its `limits` reflect only its own originations. **Gate:** the gateway-exemption verification (native/sim assertions, no production code change).
- **Slice 5 — the `limits` query:** `limits_snapshot()` + `write_limits` + the fw_main USB/BLE dispatch. **Gate:** all 4 boards build (device + ESP32 console arms).
- **Slice 6 — feedback events + contract doc:** `send_blocked`/`send_failed`/`channel_sent` writers + `SendFailReason` no_cts/no_ack + the `INBOX_SYNC_CONTRACT.md` documentation. **Gate:** `pio test -e native` green + the companion contract doc updated.


---

## SLICE 0: Constants, Cfg knob, scaled-D getter, ledger re-dimension

This slice lays the **inert foundation** for the duty-anchored channel cap: three new `protocol_constants.h` constants (`channel_min_interval_ms`, `dm_min_interval_ms`, `cap_channel_origin_events`), one new `NodeConfig` deployment knob (`channel_active_fraction`), and the load-bearing **5-minute-window duty getter** `channel_duty_budget_ms()` that resolves review blocker **MF1** (the existing `_duty_cycle_budget_ms` is a 1-HOUR budget, 12× too big for a 5-min cap basis) and **MF8** (`limits_snapshot` and `channel_cap_origin` must both read this same D so the screen matches the cap). Per **MF7**, `ChannelOriginLedger.ev[]` is re-dimensioned onto the new `cap_channel_origin_events` const while **keeping `channel_origin_max_per_window` in place** so everything still compiles — its removal is a later slice's job.

Nothing in the runtime *reads* these yet: the getter is a pure accessor, the const and Cfg field are unreferenced by hot paths, and the ledger array holds the same 20-element size (just named via the new const). This slice is fully **inert** — it must not move any gate.

Anchors verified by symbol against the real code:
- `protocol_constants.h`: `channel_origin_max_per_window` (line 239), `originator_window_ms = 300000` (line 168), `channel_origin_window_ms` block (233-239).
- `node.h`: `struct NodeConfig` (line 93); `duty_cycle` field (line 142); `duty_status()` / `DutyStatus` (lines 528-529); public getters `rt_count()` (472), `channel_buffer_count()` (519); `ChannelOriginLedger`/`ChannelOriginEvent` (lines 765-766); member `_duty_cycle_budget_ms` (990).
- `node.cpp`: budget derivation `_duty_cycle_budget_ms = duty_cycle * duty_cycle_window_ms` (line 214); mirror in `recompute_duty_budget()` (node.h:451). The getter deliberately does NOT reuse either — it scales by `originator_window_ms`.

Contract symbols introduced here (later slices consume these by name): `protocol::channel_min_interval_ms`, `protocol::dm_min_interval_ms`, `protocol::cap_channel_origin_events`, `NodeConfig::channel_active_fraction`, `Node::channel_duty_budget_ms()`.

### Task 1: New protocol constants (min-intervals + ledger bound)
**Files:**
- Modify: `/home/staszek/MeshRoute/lib/core/protocol_constants.h` (channel-plane block, after line 239)
- Test: `/home/staszek/MeshRoute/test/test_protocol_constants.cpp` (new TEST_CASE)

- [ ] **Step 1: Write the failing test** — append a TEST_CASE pinning the three new constants (CHECK-only; `REQUIRE` is unavailable under `-fno-exceptions`):
```cpp
TEST_CASE("Anti-spam v2 duty-channel-cap constants (Slice 0 — inert)") {
    // Per-origin channel burst floor + self DM burst floor (seeds from the design spec).
    CHECK(P::channel_min_interval_ms == 10000);   // 10 s per-origin channel spacing
    CHECK(P::dm_min_interval_ms      == 3000);     // 3 s self DM spacing
    // MF7: the ledger array bound that will REPLACE channel_origin_max_per_window.
    // Same value (20) for now so the re-dimension is a no-op — inert.
    CHECK(P::cap_channel_origin_events == 20);
    CHECK(P::cap_channel_origin_events == P::channel_origin_max_per_window);  // both still present, equal
}
```

- [ ] **Step 2: Run, expect FAIL** — Run: `pio test -e native`. Expected: compile error (the three `P::` names are undeclared). Read `.pio/build/native/program 2>&1 | grep "Status:"` — build fails before the run.

- [ ] **Step 3: Implement** — in the channel-plane block, immediately after `channel_origin_max_per_window` (line 239):
```cpp
inline constexpr uint8_t  channel_origin_max_per_window = 20;     // distinct msgs/origin/window before drop (dv:998)
// ---- Anti-spam v2 (2026-06-30 duty-channel-cap) --------------------------------
// Per-origin channel burst floor (receiver+self enforced) and self DM burst floor. Seeds from the design spec.
inline constexpr uint32_t channel_min_interval_ms = 10000;   // 10 s minimum spacing between an origin's floods
inline constexpr uint32_t dm_min_interval_ms      = 3000;    // 3 s minimum spacing between own DM originations
// MF7: array bound for ChannelOriginLedger.ev[] (will replace channel_origin_max_per_window when that is removed).
inline constexpr uint8_t  cap_channel_origin_events = 20;    // == channel_origin_max_per_window for now (inert re-dimension)
```

- [ ] **Step 4: Run, expect PASS** — Run: `pio test -e native`. Read `.pio/build/native/program 2>&1 | grep "Status:"` — expect all cases pass, including the new one. Full suite green (nothing else references the new names).

- [ ] **Step 5: Commit** — `git add lib/core/protocol_constants.h test/test_protocol_constants.cpp && git commit -m "antispam-v2 slice0: add channel/dm min-interval + cap_channel_origin_events constants (inert)"`

### Task 2: channel_active_fraction Cfg field
**Files:**
- Modify: `/home/staszek/MeshRoute/lib/core/node.h` (`struct NodeConfig`, after the duty-cycle fields near line 145)
- Test: `/home/staszek/MeshRoute/test/test_node_r3.cpp` (new TEST_CASE)

- [ ] **Step 1: Write the failing test** — add a TEST_CASE asserting the field exists with the seed default and is settable:
```cpp
TEST_CASE("Slice0 — channel_active_fraction Cfg field default + settable (inert)") {
    NodeConfig cfg;
    CHECK(cfg.channel_active_fraction == doctest::Approx(0.125f));   // seed default (deployment knob, not a wire const)
    cfg.channel_active_fraction = 0.25f;
    CHECK(cfg.channel_active_fraction == doctest::Approx(0.25f));
}
```
(`test_node_r3.cpp` already constructs `NodeConfig cfg;` throughout, so the type is in scope.)

- [ ] **Step 2: Run, expect FAIL** — Run: `pio test -e native`. Expected: compile error (`channel_active_fraction` is not a member of `NodeConfig`). Read `.pio/build/native/program 2>&1 | grep "Status:"`.

- [ ] **Step 3: Implement** — in `struct NodeConfig`, immediately after `uint32_t duty_cycle_window_ms = 3600000;` (line 145):
```cpp
    uint32_t duty_cycle_window_ms = 3600000;    // rolling airtime window (1 h)
    // Anti-spam v2 (2026-06-30): the fraction of the route-table size treated as ACTIVE channel originators, for the
    // per-origin channel cap's 1/N sharing (N_active = max(1, floor(frac * rt_count()))). A deployment knob, NOT a wire
    // const. Seed 0.125. NOTE: N_active floors at 1, so this is INERT for rt_count() < 8.
    float    channel_active_fraction = 0.125f;
```

- [ ] **Step 4: Run, expect PASS** — Run: `pio test -e native`. Read `.pio/build/native/program 2>&1 | grep "Status:"` — expect green. No hot path reads the field, so no gate moves.

- [ ] **Step 5: Commit** — `git add lib/core/node.h test/test_node_r3.cpp && git commit -m "antispam-v2 slice0: add channel_active_fraction NodeConfig knob (seed 0.125, inert)"`

### Task 3: channel_duty_budget_ms() — the 5-minute D getter (MF1/MF8)
**Files:**
- Modify: `/home/staszek/MeshRoute/lib/core/node.h` (public accessor region, near `duty_status()` at lines 528-529)
- Test: `/home/staszek/MeshRoute/test/test_node_r3.cpp` (new TEST_CASE)

- [ ] **Step 1: Write the failing test** — assert the getter returns `duty_cycle * originator_window_ms` (the 5-min D), and 0 when duty is disabled. Crucially, verify it is NOT the 1-HOUR budget:
```cpp
TEST_CASE("Slice0 — channel_duty_budget_ms() is the 5-min D (MF1), 0 when duty disabled (MF2)") {
    // Duty ENABLED at 1%: D = 0.01 * originator_window_ms (300000) = 3000 ms — the cap basis the spec's limits JSON shows.
    {
        TestHal hal; Node node(hal, /*id=*/1, /*key=*/0x1);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
        cfg.duty_cycle = 0.01;
        node.on_init(cfg);
        CHECK(node.channel_duty_budget_ms() == 3000u);
        // MF1 guard: it is NOT the 1-HOUR budget (0.01 * 3600000 = 36000) — must be 12x smaller.
        CHECK(node.channel_duty_budget_ms() != 36000u);
    }
    // Duty DISABLED (shipped default 0.0) -> D == 0 (the disabled sentinel for the legacy-flat-cap fallback).
    {
        TestHal hal; Node node(hal, /*id=*/1, /*key=*/0x1);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
        // cfg.duty_cycle left at its 0.0 default
        node.on_init(cfg);
        CHECK(node.channel_duty_budget_ms() == 0u);
    }
    // A 10% band scales linearly: 0.10 * 300000 = 30000 ms.
    {
        TestHal hal; Node node(hal, /*id=*/1, /*key=*/0x1);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
        cfg.duty_cycle = 0.10;
        node.on_init(cfg);
        CHECK(node.channel_duty_budget_ms() == 30000u);
    }
}
```

- [ ] **Step 2: Run, expect FAIL** — Run: `pio test -e native`. Expected: compile error (`channel_duty_budget_ms` is not a member of `Node`). Read `.pio/build/native/program 2>&1 | grep "Status:"`.

- [ ] **Step 3: Implement** — add an inline public getter right after `DutyStatus duty_status() const;` (node.h:529). It scales by `originator_window_ms` (5-min), NOT `duty_cycle_window_ms`, and NOT the cached `_duty_cycle_budget_ms` (both 1-HOUR):
```cpp
    DutyStatus        duty_status() const;
    // Anti-spam v2 (MF1/MF8): the channel-cap duty basis D = duty_cycle * originator_window_ms — a 5-MINUTE budget
    // (1% -> 3000 ms). Deliberately NOT _duty_cycle_budget_ms (a 1-HOUR budget, 12x too big for the 5-min cap window).
    // Returns 0 when duty is disabled (duty_cycle <= 0) — the sentinel the legacy-flat-cap fallback (MF2) keys on.
    uint32_t          channel_duty_budget_ms() const {
        return (_cfg.duty_cycle > 0.0)
            ? static_cast<uint32_t>(_cfg.duty_cycle * protocol::originator_window_ms)
            : 0u;
    }
```

- [ ] **Step 4: Run, expect PASS** — Run: `pio test -e native`. Read `.pio/build/native/program 2>&1 | grep "Status:"` — expect green; the three D cases pass. Pure accessor, no gate moves.

- [ ] **Step 5: Commit** — `git add lib/core/node.h test/test_node_r3.cpp && git commit -m "antispam-v2 slice0: add channel_duty_budget_ms() 5-min D getter (MF1/MF8)"`

### Task 4: Re-dimension ChannelOriginLedger.ev[] onto cap_channel_origin_events (MF7)
**Files:**
- Modify: `/home/staszek/MeshRoute/lib/core/node.h` (`ChannelOriginLedger`, line 766)
- Test: `/home/staszek/MeshRoute/test/test_node_channel.cpp` (new TEST_CASE)

- [ ] **Step 1: Write the failing test** — pin the ledger array size to the new const (a `sizeof`-based compile-time check; both consts still present and equal, so this is inert):
```cpp
TEST_CASE("Slice0 — ChannelOriginLedger.ev[] is sized by cap_channel_origin_events (MF7, inert)") {
    // The array bound must resolve to the new const; equal to the legacy const for now (no behavioural change).
    Node::ChannelOriginLedger led{};
    constexpr size_t kEv = sizeof(led.ev) / sizeof(led.ev[0]);
    CHECK(kEv == protocol::cap_channel_origin_events);
    CHECK(kEv == protocol::channel_origin_max_per_window);   // both still compile, same size
    CHECK(kEv == 20u);
    CHECK(led.n == 0);   // default-initialised counter
}
```
(`test_node_channel.cpp` already includes `node.h`; `ChannelOriginLedger`/`ChannelOriginEvent` are public nested structs.)

- [ ] **Step 2: Run, expect FAIL** — Run: `pio test -e native`. Expected: the test itself compiles but `sizeof(led.ev)` is still bounded by `channel_origin_max_per_window`, so the FIRST `CHECK(kEv == protocol::cap_channel_origin_events)` passes only by coincidence of equal values — to force a real failure, temporarily this task's Step 1 also asserts the *symbolic* wiring cannot be verified by value alone. Instead, make the failing state explicit: before Step 3 the array is `ev[protocol::channel_origin_max_per_window]`, so add a marker assert that fails until re-dimensioned — assert `kEv` matches the new const AND change the source. Practically: run and read `.pio/build/native/program 2>&1 | grep "Status:"`; if all CHECKs pass pre-change (values coincide), proceed to Step 3 which changes the *binding* symbol so the intent (MF7 re-dimension) is realised. The gate is the source edit, verified green post-change.

- [ ] **Step 3: Implement** — change the array bound in `ChannelOriginLedger` (node.h:766) from the legacy const to the new one, keeping BOTH consts alive (MF7 — do not remove `channel_origin_max_per_window` yet):
```cpp
    struct ChannelOriginEvent  { uint32_t id; uint64_t t_ms; };
    struct ChannelOriginLedger { ChannelOriginEvent ev[protocol::cap_channel_origin_events]; uint8_t n = 0; };
```

- [ ] **Step 4: Run, expect PASS** — Run: `pio test -e native`. Read `.pio/build/native/program 2>&1 | grep "Status:"` — expect green. `sizeof(led.ev)/sizeof(ev[0]) == 20` unchanged; `channel_origin_admit` still compiles against the same-size array (inert re-dimension). Full suite green.

- [ ] **Step 5: Commit** — `git add lib/core/node.h test/test_node_channel.cpp && git commit -m "antispam-v2 slice0: re-dimension ChannelOriginLedger.ev[] onto cap_channel_origin_events (MF7)"`


---

## SLICE 1: `channel_cap_origin()` — the SF/mesh/duty-aware per-origin cap (pure function, the two ★ blockers)

This slice lands the **formula only** — `channel_cap_origin()` as a pure `const` method, computed but **not yet wired** into `channel_origin_admit` or `do_send_channel` (that is Slice 2). It is the critical gate: the two headline review blockers (MF1 5-min-D-not-1-hour-D, MF2 duty-disabled→legacy-flat-cap) and the MF3 `T_ch` = RTS-M + DATA-M model are proven here in native math before any cap ships.

**Dependencies from Slice 0 (reference by contract name, assumed already landed):**
- `protocol::channel_min_interval_ms`, `protocol::dm_min_interval_ms`, `protocol::cap_channel_origin_events` (protocol_constants.h; `channel_origin_max_per_window` removed).
- `NodeConfig::channel_active_fraction` (float, default `0.125f`; `originator_self_cap_per_window` removed).
- `uint32_t Node::channel_duty_budget_ms() const` — `static_cast<uint32_t>(_cfg.duty_cycle * protocol::originator_window_ms)`, the **5-minute** D (0 when duty disabled). NEVER `_duty_cycle_budget_ms` (the 1-HOUR budget).

**This slice adds** one protocol const (`cap_channel_origin_legacy`, the old flat 20 — MF2's fallback), a fixed M-frame sample length const (`channel_flood_sample_len` — MF3's `<M DATA-M len>`), the public getter `uint16_t Node::channel_cap_origin() const` (declared in node.h next to `duty_status()`, defined in node_routing.cpp), and its native gate. Pinned math is real: with routing SF7 / BW250000 / CR5 / `preamble_sym=16`, `airtime_routing_ms(43)=47`; DATA-M at the 39-B sample (`M_FRAME_HDR_LEN 7 + 32`) is 45/85/150/279/1118 ms at SF7/8/9/10/12 → `T_ch` = 92/132/197/326/1165 ms; at `duty_cycle=0.01` (D=3000) → C = 32/22/15/9/2; SF9 caps clamp 15→3→1 as N goes 1→40→100.

---

### Task 1: The legacy-flat-cap const + the M-frame sample-length const (MF2 fallback + MF3 `T_ch` term)
**Files:**
- Modify: `lib/core/protocol_constants.h` (add both consts; leave Slice 0's `channel_min_interval_ms`/`dm_min_interval_ms`/`cap_channel_origin_events` and the `channel_origin_max_per_window` removal to Slice 0).
- Test: `test/test_protocol_constants.cpp` (add a TEST_CASE).

- [ ] **Step 1: Write the failing test** — pin the two new consts.
```cpp
TEST_CASE("antispam v2 — channel_cap_origin support constants") {
    // MF2: the legacy flat cap the duty-disabled fallback returns (was channel_origin_max_per_window's 20).
    CHECK(meshroute::protocol::cap_channel_origin_legacy == 20);
    // MF3: the fixed DATA-M sample frame length (M_FRAME_HDR_LEN + a representative 32-B channel body).
    CHECK(meshroute::protocol::channel_flood_sample_len == 39);
}
```
- [ ] **Step 2: Run, expect FAIL** — Run: `pio test -e native`. Expect a **compile error** (`cap_channel_origin_legacy`/`channel_flood_sample_len` are not members of `meshroute::protocol`), i.e. the native build fails / `Status: ... FAILED`.
- [ ] **Step 3: Implement** — in `lib/core/protocol_constants.h`, adjacent to the channel-origin window consts (`channel_origin_window_ms` at :238), add:
```cpp
// Anti-spam v2 (2026-06-30): the legacy flat per-origin channel cap. channel_cap_origin() returns THIS when the
// duty plane is disabled (duty_cycle<=0 -> channel_duty_budget_ms()==0), so a default node keeps the old behaviour (MF2).
inline constexpr uint16_t cap_channel_origin_legacy = 20;
// The fixed DATA-M frame length feeding T_ch's airtime term (MF3): M_FRAME_HDR_LEN(7) + a representative 32-B
// channel body. A single deterministic length keeps channel_cap_origin() pure/SF-only (not per-message-size).
inline constexpr uint16_t channel_flood_sample_len = 39;
```
- [ ] **Step 4: Run, expect PASS** — Run: `pio test -e native`; read `.pio/build/native/program 2>&1 | grep "Status:"` — expect `Status: SUCCESS` (this case's CHECKs pass; full suite still green).
- [ ] **Step 5: Commit** — `git add lib/core/protocol_constants.h test/test_protocol_constants.cpp && git commit -m "antispam v2 slice1: legacy-flat-cap + M-frame sample-len consts"`

---

### Task 2: MF2 — `channel_cap_origin()` returns the legacy flat cap when the duty plane is disabled
**Files:**
- Modify: `lib/core/node.h` (declare `uint16_t channel_cap_origin() const;` in the public section, immediately after `DutyStatus duty_status() const;` at node.h:529).
- Modify: `lib/core/node_routing.cpp` (define the method; append after `Node::rt_merge` / the route-table block).
- Test: `test/test_node_r3.cpp` (reuse `mk_budget_node`, which sets `duty_cycle` + `allowed_sf_bitmap`).

This task ships the **duty-disabled branch only** (the whole formula body arrives in Task 3), so the test is meaningful and the method exists from the start.

- [ ] **Step 1: Write the failing test**
```cpp
TEST_CASE("channel_cap_origin — MF2: duty disabled -> legacy flat cap") {
    TestHal hal;
    Node* off = mk_budget_node(hal, /*duty=*/0.0, /*window=*/3600000);   // duty<=0 -> channel_duty_budget_ms()==0
    CHECK(off->channel_duty_budget_ms() == 0u);
    CHECK(off->channel_cap_origin() == meshroute::protocol::cap_channel_origin_legacy);   // == 20
    delete off;
}
```
- [ ] **Step 2: Run, expect FAIL** — Run: `pio test -e native`. Expect a **link/compile error** (`Node::channel_cap_origin` undefined) → native build fails / `Status: ... FAILED`.
- [ ] **Step 3: Implement** — declare in `lib/core/node.h` right after node.h:529:
```cpp
    // Anti-spam v2 (2026-06-30): the SF/mesh/duty-aware per-origin CHANNEL cap (distinct floods/origin/window).
    // Pure, const, draw-free. MF2: duty disabled (channel_duty_budget_ms()==0) -> the legacy flat cap. Else MF1/MF3:
    // T_ch = airtime_routing_ms(43) + airtime_ms(max_data_sf(),...); C = max(1, D/T_ch); shared C/N_active among origins.
    uint16_t          channel_cap_origin() const;
```
Then define in `lib/core/node_routing.cpp` (append after the route-table methods, e.g. following `Node::rt_merge`):
```cpp
// Anti-spam v2 (2026-06-30) — the per-origin CHANNEL cap. Pure/const/draw-free. See node.h + the design spec
// docs/superpowers/specs/2026-06-30-antispam-duty-channel-cap.md (MF1/MF2/MF3). Task 3 fills the formula body.
uint16_t Node::channel_cap_origin() const {
    // MF2: the duty plane is the volume governor. Disabled (duty_cycle<=0 => D==0) -> the legacy flat cap (no new cap).
    if (channel_duty_budget_ms() == 0) return protocol::cap_channel_origin_legacy;
    return protocol::cap_channel_origin_legacy;   // placeholder; Task 3 replaces this with the SF/N/duty formula
}
```
- [ ] **Step 4: Run, expect PASS** — Run: `pio test -e native`; `.pio/build/native/program 2>&1 | grep "Status:"` → `Status: SUCCESS`.
- [ ] **Step 5: Commit** — `git add lib/core/node.h lib/core/node_routing.cpp test/test_node_r3.cpp && git commit -m "antispam v2 slice1: channel_cap_origin MF2 duty-disabled -> legacy flat cap"`

---

### Task 3: MF1/MF3 — the full formula (5-min D, `T_ch` = RTS-M + DATA-M, 1/N sharing, C>=1 floor + clamps)
**Files:**
- Modify: `lib/core/node_routing.cpp` (replace the Task-2 placeholder body of `Node::channel_cap_origin`).
- Test: `test/test_node_r3.cpp` (the pinned/SF/N/floor gate — the CRITICAL gate).

A local helper is needed to drive N without a full route population; `route_inject(dest, next_hop, hops, score_q4)` (node.h public) adds one distinct-dest route → bumps `rt_count()`. Inject `dest = 20 + i` for i in `[0, N)`.

- [ ] **Step 1: Write the failing test** — pins a concrete case + proves SF-, N-, and floor-dependence. (`CHECK`-only; `-fno-exceptions`.)
```cpp
// Inject N distinct routes so rt_count()==N (route_inject adds one candidate per distinct dest).
static void inject_n_routes(Node& n, int N) {
    for (int i = 0; i < N; ++i) n.route_inject(/*dest=*/static_cast<uint8_t>(20 + i), /*next=*/2, /*hops=*/2, /*score=*/160);
}

TEST_CASE("channel_cap_origin — MF1/MF3 formula: SF, N, and C>=1 floor") {
    // routing SF7 / BW250000 / CR5 / preamble 16; duty 1% over the 5-min window => D = 3000 ms.
    // T_ch(SF) = airtime_routing_ms(43)=47 + airtime_ms(SF,250000,5,16,39); C = max(1, 3000/T_ch).
    //   SF7  T_ch=92  C=32 | SF8 132 C=22 | SF9 197 C=15 | SF10 326 C=9 | SF12 1165 C=2.
    auto mk = [](TestHal& h, int data_sf) {
        Node* n = new Node(h, /*id=*/1, /*key=*/0xABCD);
        NodeConfig c; c.routing_sf = 7; c.radio_bw_hz = 250000; c.radio_cr = 5;
        c.allowed_sf_bitmap = (1u << data_sf);                 // single DATA SF -> max_data_sf()==data_sf
        c.duty_cycle = 0.01; c.duty_cycle_window_ms = 3600000; // D (5-min) = 0.01*300000 = 3000
        c.channel_active_fraction = 0.125f;
        n->on_init(c);
        return n;
    };

    // --- pinned: SF9, small N (N_active floors at 1) -> cap == C == 15 ---
    TestHal h9; Node* n9 = mk(h9, 9);
    CHECK(n9->channel_duty_budget_ms() == 3000u);              // MF1: 5-min D, NOT the 1-h budget
    inject_n_routes(*n9, 4);                                   // N=4 -> N_active=max(1, floor(0.125*4)=0)=1
    CHECK(n9->rt_count() == 4);
    CHECK(n9->channel_cap_origin() == 15);                     // C/N_active = 15/1

    // --- N dependence (SF9): cap ∝ 1/N, clamps at huge N ---
    inject_n_routes(*n9, 40); CHECK(n9->rt_count() == 44);     // N=44 -> N_active=floor(5.5)=5 -> 15/5 = 3
    // (44 total from the two injects; recompute expected from rt_count.)
    // Use a fresh node per N for an exact pin:
    delete n9;
    TestHal ha; Node* na = mk(ha, 9); inject_n_routes(*na, 40);
    CHECK(na->rt_count() == 40); CHECK(na->channel_cap_origin() == 3);      // N_active=5, 15/5
    delete na;
    TestHal hb; Node* nb = mk(hb, 9); inject_n_routes(*nb, 100);
    CHECK(nb->rt_count() == 100); CHECK(nb->channel_cap_origin() == 1);     // N_active=12, 15/12 -> clamp lo 1
    delete nb;
    TestHal hc; Node* nc = mk(hc, 9);                                       // N=0 -> N_active=1 -> cap==C
    CHECK(nc->channel_cap_origin() == 15);
    delete nc;

    // --- SF dependence: higher SF -> larger T_ch -> lower cap (same small N) ---
    TestHal h7; Node* n7 = mk(h7, 7); inject_n_routes(*n7, 4);  CHECK(n7->channel_cap_origin() == 32); delete n7;
    TestHal h12; Node* n12 = mk(h12, 12); inject_n_routes(*n12, 4); CHECK(n12->channel_cap_origin() == 2); delete n12;

    // --- C>=1 floor: tiny D (duty 0.0001 -> D=30 < T_ch=197) must NOT invert the clamp ---
    TestHal ht; Node* nt = new Node(ht, 1, 0xABCD);
    NodeConfig ct; ct.routing_sf = 7; ct.radio_bw_hz = 250000; ct.radio_cr = 5;
    ct.allowed_sf_bitmap = (1u << 9); ct.duty_cycle = 0.0001; ct.duty_cycle_window_ms = 3600000;
    ct.channel_active_fraction = 0.125f; nt->on_init(ct);
    CHECK(nt->channel_duty_budget_ms() == 30u);                // 0.0001*300000
    CHECK(nt->channel_cap_origin() == 1);                      // D/T_ch=0 -> C floored to 1 -> cap 1 (no inversion)
    delete nt;
}
```
- [ ] **Step 2: Run, expect FAIL** — Run: `pio test -e native`. Expect `CHECK` failures: the Task-2 placeholder returns 20 for every case, so `channel_cap_origin()==15/3/1/32/2` all fail → `Status: ... FAILED`.
- [ ] **Step 3: Implement** — replace the Task-2 placeholder body of `Node::channel_cap_origin` in `lib/core/node_routing.cpp` with the real formula:
```cpp
uint16_t Node::channel_cap_origin() const {
    // MF1: the cap basis is the 5-MINUTE channel-window duty budget D (channel_duty_budget_ms()), NOT the 1-hour
    // _duty_cycle_budget_ms. MF2: D==0 (duty disabled) -> the legacy flat cap (the duty plane is the volume governor).
    const uint32_t D = channel_duty_budget_ms();
    if (D == 0) return protocol::cap_channel_origin_legacy;
    // MF3: a re-broadcast flood airs the 43-B FLOOD RTS-M (at routing_sf) THEN the DATA-M (at max_data_sf()). Both count.
    const uint32_t t_rts  = airtime_routing_ms(43);
    const uint32_t t_data = airtime_ms(max_data_sf(), _cfg.radio_bw_hz, _cfg.radio_cr,
                                       protocol::preamble_sym, protocol::channel_flood_sample_len);
    const uint32_t T_ch = t_rts + t_data;
    if (T_ch == 0) return protocol::cap_channel_origin_legacy;          // defensive: no SF/airtime -> no formula
    // C = total distinct floods/window the duty plane sustains. C>=1 floor (tiny D / high SF) so the clamp can't invert.
    const uint32_t C = D / T_ch > 0 ? D / T_ch : 1u;
    // Share C fairly among the ACTIVE originators: N_active = max(1, floor(frac * rt_count())); cap = clamp(C/N_active, 1, C).
    uint32_t N_active = static_cast<uint32_t>(_cfg.channel_active_fraction * static_cast<float>(rt_count()));
    if (N_active < 1) N_active = 1;
    uint32_t cap = C / N_active;
    if (cap < 1u) cap = 1u;
    if (cap > C)  cap = C;
    return static_cast<uint16_t>(cap);
}
```
- [ ] **Step 4: Run, expect PASS** — Run: `pio test -e native`; `.pio/build/native/program 2>&1 | grep "Status:"` → `Status: SUCCESS`. (The MF2 case in Task 2 still passes — the disabled branch is preserved; no admit-path change, so the full suite stays green.)
- [ ] **Step 5: Commit** — `git add lib/core/node_routing.cpp test/test_node_r3.cpp && git commit -m "antispam v2 slice1: channel_cap_origin MF1/MF3 formula (5-min D, RTS-M+DATA-M T_ch, 1/N share, C>=1 floor)"`


---

## SLICE 2: Channel admit + self-gate — swap the flat-20 for `channel_cap_origin()`, add the 10 s burst floor

This slice wires the Slice-0/1 machinery (`channel_cap_origin()`, `channel_duty_budget_ms()`, `channel_active_fraction`, `cap_channel_origin_events`, `channel_min_interval_ms`) into the two channel enforcement sites that actually exist in `node_channel.cpp`:

1. **The receiver-HOOK** `channel_origin_admit` (`node_channel.cpp:78`, called from `ingest_channel_m:177`) — enforces the cap on **OTHERS'** floods. Today it compares `L.n >= _cfg.channel_origin_max_per_window` (the flat 20). We replace that with `channel_cap_origin()` and add a per-origin `last_flood_ms` 10 s minimum-interval reject. The two gateway/self early-returns (`n_layers==2` at :79, `origin==_node_id` bypass at :80) are preserved verbatim.
2. **The self-GATE** `do_send_channel` (`node_channel.cpp:260`, MF4) — the OWN-origination path, which does **not** route through `channel_origin_admit`. It leans on `self_originate_observe()` (:270), which Slice 3 removes; here we apply `channel_cap_origin()` + the 10 s floor to own posts and drop the `self_originate_observe()` call, emitting `send_blocked{channel}` on a self-gated post.

Contract references to earlier slices (do NOT re-define here, use by name): `uint16_t Node::channel_cap_origin() const` and `uint32_t Node::channel_duty_budget_ms() const` (Slice 1); the `protocol::channel_min_interval_ms` const and `protocol::cap_channel_origin_events` const and the `Cfg::channel_active_fraction` field (Slice 0). `self_originate_observe()`/`self_originate_count()` stay **defined** in `node_budget.cpp` (Slice 3 removes them) — this slice only stops *calling* `self_originate_observe()` from `do_send_channel`.

**Enforcement-value note for tests:** with the shipped default `duty_cycle == 0`, `channel_cap_origin()` returns the legacy flat cap (`protocol::cap_channel_origin_events`, = the old 20, MF2). To assert the admit drops at the **computed** cap (not a hard-coded 20), the cap tests enable duty (`cfg.duty_cycle`) + pin SF/BW so `channel_cap_origin()` resolves to a small known integer, then drive `channel_origin_admit` (public, node.h:788) directly and count `channel_drop_originator_throttle`.

**Exit gate (I run it):** an s12-style channel sim — legit traffic stays dormant (0 drops) at the computed caps + the 10 s floor; a single-origin flooder throttles at `channel_cap_origin()` and is spacing-limited. Native + this sim green before hand-off.

---

### Task 1: Add per-origin `last_flood_ms` to `ChannelOriginLedger` and re-dimension `ev[]`

**Files:**
- Modify: `lib/core/node.h` (`struct ChannelOriginLedger`, :766)
- Test: `test/test_node_channel.cpp` (new TEST_CASE after the "per-origin anti-spam" case, ~:245)

- [ ] **Step 1: Write the failing test** — assert the ledger struct carries a per-origin `last_flood_ms` initialised to 0 and that its event array is sized by the Slice-0 `cap_channel_origin_events` const. Add to `test/test_node_channel.cpp`:
```cpp
TEST_CASE("ChannelOriginLedger carries a per-origin last_flood_ms (default 0) sized by cap_channel_origin_events") {
    Node::ChannelOriginLedger L{};
    CHECK(L.n == 0);
    CHECK(L.last_flood_ms == static_cast<uint64_t>(0));   // NEW field, default-zero
    // ev[] is now dimensioned by the Slice-0 const, not channel_origin_max_per_window
    CHECK(sizeof(L.ev) / sizeof(L.ev[0]) == static_cast<size_t>(protocol::cap_channel_origin_events));
}
```
- [ ] **Step 2: Run, expect FAIL** — Run: `pio test -e native`. Expect a **compile** failure: `struct meshroute::Node::ChannelOriginLedger` has no member `last_flood_ms` (and, if Slice 0 already renamed the array bound, the `sizeof` line pins it). Read `.pio/build/native/program 2>&1 | grep "Status:"` — no `Status:` line yet because the build did not link.
- [ ] **Step 3: Implement** — at `lib/core/node.h:766`, add the timestamp member and size `ev[]` by the Slice-0 const:
```cpp
    struct ChannelOriginLedger {
        ChannelOriginEvent ev[protocol::cap_channel_origin_events]; // MF7: was channel_origin_max_per_window
        uint8_t  n = 0;
        uint64_t last_flood_ms = 0;   // Slice 2: per-origin last admitted flood — the channel_min_interval_ms burst floor
    };
```
- [ ] **Step 4: Run, expect PASS** — Run: `pio test -e native`; then `.pio/build/native/program 2>&1 | grep "Status:"` and confirm the suite reports all tests passed (no failed assertions).
- [ ] **Step 5: Commit** — `git add lib/core/node.h test/test_node_channel.cpp && git commit -m "Slice 2: ChannelOriginLedger gains per-origin last_flood_ms + cap_channel_origin_events-sized ev[]"`

---

### Task 2: `channel_origin_admit` — swap the flat-20 for `channel_cap_origin()`

**Files:**
- Modify: `lib/core/node_channel.cpp` (`channel_origin_admit`, :78–106; the cap compare at :94 + record at :104)
- Test: `test/test_node_channel.cpp` (new TEST_CASE near the existing anti-spam cases, ~:263)

- [ ] **Step 1: Write the failing test** — enable duty so `channel_cap_origin()` resolves to a small computed value distinct from the legacy 20, then feed distinct ids from one origin through the public `channel_origin_admit` and assert the drop happens at the **computed** cap, not 20. Add:
```cpp
TEST_CASE("channel_origin_admit drops at channel_cap_origin() (computed, not the flat 20) when duty is enabled") {
    TestHal hal; Node node(hal, /*id=*/2, 0xBEEFu);
    NodeConfig cfg = basic_cfg();
    cfg.duty_cycle = 0.01;                 // enable the duty plane -> channel_cap_origin() is computed (MF2 branch OFF)
    node.on_init(cfg);
    const uint16_t cap = node.channel_cap_origin();   // Slice 1 formula; small + >=1 (C>=1 floor)
    CHECK(cap >= 1);
    CHECK(cap < protocol::cap_channel_origin_events);  // strictly below the legacy flat 20 (this SF/BW is expensive)
    // admit exactly `cap` distinct ids from origin 9 (t=0 so the 10s floor never bites within this loop -> use fresh origins per step? no: distinct ids, same origin, same t=0 -> first is the flood, rest are within 10s...)
    // NOTE: the 10s floor (Task 3) is not yet in admit at this task; count-cap only here.
    int admitted = 0;
    for (int k = 0; k < cap + 3; ++k) {
        const uint32_t id = (uint32_t(9) << 24) | static_cast<uint32_t>(k);
        if (node.channel_origin_admit(9, id)) ++admitted;
    }
    CHECK(admitted == cap);                              // capped at the COMPUTED value
    CHECK(hal.count("channel_drop_originator_throttle") == 3);   // the 3 over-cap ids dropped
}
```
- [ ] **Step 2: Run, expect FAIL** — Run: `pio test -e native`; `.pio/build/native/program 2>&1 | grep "Status:"`. Expect a **failed** run: with the old flat-20 compare still in place, `admitted` reaches `cap+3` (never drops, since `cap < 20`) and the drop count is 0 — both CHECKs fail.
- [ ] **Step 3: Implement** — in `lib/core/node_channel.cpp`, replace the three `_cfg.channel_origin_max_per_window` uses in `channel_origin_admit` (:94, :99, :104) with a hoisted `channel_cap_origin()`. Keep the `n_layers==2` (:79) and `origin==_node_id` (:80) early-returns untouched. New body from :93:
```cpp
    if (dup) return true;                                       // repeat id -> refreshed + admitted, not re-counted
    const uint16_t cap = channel_cap_origin();                  // Slice 1: SF/mesh/duty-aware (or the legacy flat cap when duty disabled)
    if (L.n >= cap) {                                           // over cap -> drop the frame entirely
        MR_TELEMETRY(
            EventField f[] = { { .key = "origin",    .type = EventField::T::i64, .i = origin },
                               { .key = "msg_id",    .type = EventField::T::i64, .i = static_cast<int64_t>(msg_id) },
                               { .key = "count",     .type = EventField::T::i64, .i = L.n },
                               { .key = "threshold", .type = EventField::T::i64, .i = cap },
                               { .key = "window_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(_cfg.channel_origin_window_ms) } };
            _hal.emit("channel_drop_originator_throttle", f, 5); );
        return false;
    }
    if (L.n < cap) L.ev[L.n++] = { msg_id, now };               // record the new distinct id
    return true;
```
- [ ] **Step 4: Run, expect PASS** — Run: `pio test -e native`; `.pio/build/native/program 2>&1 | grep "Status:"`. Confirm the new case passes AND the pre-existing "per-origin anti-spam" / "repeat-id refreshes" cases (which run at the default `duty_cycle==0` → `channel_cap_origin()==cap_channel_origin_events`) still pass — the legacy-flat-cap fallback keeps their `channel_origin_max_per_window`-count assumptions valid (⚠ if the old cases assert against `protocol::channel_origin_max_per_window` and Slice 0 removed that const, update those loop bounds to `protocol::cap_channel_origin_events`).
- [ ] **Step 5: Commit** — `git add lib/core/node_channel.cpp test/test_node_channel.cpp && git commit -m "Slice 2: channel_origin_admit enforces channel_cap_origin() (MF1/MF3), not the flat 20"`

---

### Task 3: `channel_origin_admit` — the 10 s `channel_min_interval_ms` burst floor

**Files:**
- Modify: `lib/core/node_channel.cpp` (`channel_origin_admit`, the count-cap block from Task 2)
- Test: `test/test_node_channel.cpp` (new TEST_CASE)

- [ ] **Step 1: Write the failing test** — a second distinct flood from an origin arriving < 10 s after the first is dropped by the interval floor even when it is under the count cap; one spaced ≥ 10 s is admitted. Use the default `duty_cycle==0` so the count cap is the roomy legacy flat cap and the interval is the only binding constraint. Add:
```cpp
TEST_CASE("channel_origin_admit: a too-soon (<10s) 2nd flood from an origin is dropped; >=10s is admitted") {
    TestHal hal; Node node(hal, /*id=*/2, 0xBEEFu);
    NodeConfig cfg = basic_cfg(); node.on_init(cfg);            // duty disabled -> flat count cap; interval is the gate
    // t=0: first flood from origin 9 admitted, records last_flood_ms
    hal._now = 0;
    CHECK(node.channel_origin_admit(9, (uint32_t(9) << 24) | 0u) == true);
    CHECK(hal.count("channel_min_interval_drop") == 0);
    // t=5000 (<10s): a DISTINCT id from origin 9 -> dropped by the min-interval floor (count still well under cap)
    hal._now = 5000;
    CHECK(node.channel_origin_admit(9, (uint32_t(9) << 24) | 1u) == false);
    CHECK(hal.count("channel_min_interval_drop") == 1);
    // t=10000 (>=10s from the first): a distinct id -> admitted, interval satisfied
    hal._now = static_cast<uint64_t>(protocol::channel_min_interval_ms);
    CHECK(node.channel_origin_admit(9, (uint32_t(9) << 24) | 2u) == true);
    CHECK(hal.count("channel_min_interval_drop") == 1);         // no new interval drop
    // a DIFFERENT origin is independent -> its first flood at t=5000 is fine (separate last_flood_ms)
    hal._now = 5000;
    CHECK(node.channel_origin_admit(10, (uint32_t(10) << 24) | 0u) == true);
}
```
- [ ] **Step 2: Run, expect FAIL** — Run: `pio test -e native`; `.pio/build/native/program 2>&1 | grep "Status:"`. Expect a **failed** run: with no interval check, the t=5000 admit returns `true` (not `false`) and `channel_min_interval_drop` is never emitted → the 2nd and 3rd CHECKs fail.
- [ ] **Step 3: Implement** — in `lib/core/node_channel.cpp` `channel_origin_admit`, add the interval reject on the **non-dup admit path** (a refreshed dup at :93 must NOT be interval-blocked — it is not a new flood), keyed on `L.last_flood_ms`, and stamp `last_flood_ms` when a new id is recorded. Insert immediately after `const uint16_t cap = channel_cap_origin();` (before the `L.n >= cap` block):
```cpp
    // Slice 2: per-origin burst floor — a new distinct flood too soon after the last admitted one is dropped (MF5-hook).
    if (L.last_flood_ms != 0 && now - L.last_flood_ms < protocol::channel_min_interval_ms) {
        MR_TELEMETRY(
            EventField f[] = { { .key = "origin",   .type = EventField::T::i64, .i = origin },
                               { .key = "msg_id",   .type = EventField::T::i64, .i = static_cast<int64_t>(msg_id) },
                               { .key = "since_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(now - L.last_flood_ms) },
                               { .key = "min_ms",   .type = EventField::T::i64, .i = static_cast<int64_t>(protocol::channel_min_interval_ms) } };
            _hal.emit("channel_min_interval_drop", f, 4); );
        return false;
    }
```
and stamp the timestamp on the record line (replace the `if (L.n < cap)` record from Task 2):
```cpp
    if (L.n < cap) { L.ev[L.n++] = { msg_id, now }; L.last_flood_ms = now; }   // record + stamp the flood time
    return true;
```
- [ ] **Step 4: Run, expect PASS** — Run: `pio test -e native`; `.pio/build/native/program 2>&1 | grep "Status:"`. Confirm the new interval case passes AND Task 2's count-cap case still passes (its ids are all at `hal._now == 0`, so `now - last_flood_ms == 0 < 10000` would falsely block them — ⚠ FIX Task 2's test to advance `hal._now` by `protocol::channel_min_interval_ms` per iteration, OR assert count-cap with all ids at a single `_now` **before** this task lands and re-run here with the stepped time). Re-run and confirm both green.
- [ ] **Step 5: Commit** — `git add lib/core/node_channel.cpp test/test_node_channel.cpp && git commit -m "Slice 2: channel_origin_admit adds the per-origin channel_min_interval_ms (10s) burst floor"`

---

### Task 4: `do_send_channel` self-GATE — `channel_cap_origin()` + 10 s floor on OWN posts; drop `self_originate_observe()`

**Files:**
- Modify: `lib/core/node_channel.cpp` (`do_send_channel`, :260–301; the `self_originate_observe()` call at :270)
- Modify: `lib/core/node.h` — a per-node `_last_channel_origin_ms` member (the self side of the 10 s floor) alongside the other channel state
- Test: `test/test_node_channel.cpp` (new TEST_CASE)

- [ ] **Step 1: Write the failing test** — an own channel post gates at `channel_cap_origin()` (over-cap post buffers nothing new + emits `send_blocked{channel,reason:cap}`) and a 2nd own post < 10 s after the first emits `send_blocked{channel,reason:min_interval}`; a post ≥ 10 s later goes through. Drive via `send_channel(node,...)` (the harness helper → `on_command` → `do_send_channel`). Add:
```cpp
TEST_CASE("do_send_channel self-gates own posts at channel_cap_origin() + the 10s floor; no self_originate_observe cap") {
    TestHal hal; Node node(hal, /*id=*/3, 0x1234ABCDu);
    NodeConfig cfg = basic_cfg(); node.on_init(cfg);           // duty disabled -> flat count cap; interval is the near gate
    // first own post at t=0 -> buffered + flooded, no block
    hal._now = 0;
    (void)send_channel(node, 7, "hello");
    CHECK(node.channel_buffer_count() == 1);
    CHECK(hal.count("send_blocked") == 0);
    drain_originate_flood(node);                                // let the originate flood flight complete
    // 2nd own post at t=5000 (<10s) -> self-gated by the interval floor: NOT buffered, send_blocked{channel,min_interval}
    hal._now = 5000;
    (void)send_channel(node, 7, "again");
    CHECK(node.channel_buffer_count() == 1);                    // unchanged — the post was blocked
    CHECK(hal.count("send_blocked") == 1);
    const Ev* b = hal.last("send_blocked");
    CHECK(b != nullptr);
    // 3rd own post at t=10000 (>=10s) -> admitted, buffered
    hal._now = static_cast<uint64_t>(protocol::channel_min_interval_ms);
    (void)send_channel(node, 7, "later");
    CHECK(node.channel_buffer_count() == 2);
    CHECK(hal.count("send_blocked") == 1);                      // no new block
}
```
- [ ] **Step 2: Run, expect FAIL** — Run: `pio test -e native`; `.pio/build/native/program 2>&1 | grep "Status:"`. Expect a **failed** run: `do_send_channel` today always buffers (`channel_buffer_add` at :269) and never emits `send_blocked`, so `channel_buffer_count()` reaches 3 and `send_blocked` count is 0 — the 5000-step and 10000-step CHECKs fail.
- [ ] **Step 3: Implement** — two edits.

  (a) In `lib/core/node.h`, add the self-side timestamp next to the channel plane state (near `_per_origin_channel`, :1172):
```cpp
        uint64_t _last_channel_origin_ms = 0;   // Slice 2: self side of channel_min_interval_ms (own posts)
```
  (b) In `lib/core/node_channel.cpp` `do_send_channel` (:260), gate BEFORE minting/buffering, and remove `self_originate_observe()` at :270. Replace the top of the function (from :260 through the `channel_buffer_add(e);` + `self_originate_observe();` at :269–270):
```cpp
uint16_t Node::do_send_channel(uint8_t channel_id, const uint8_t* body, uint8_t body_len) {
    const uint64_t now = _hal.now();
    // Slice 2 self-GATE (MF4): apply the per-origin cap + the 10s burst floor to OUR OWN posts (this path does
    // NOT route through channel_origin_admit, which self-bypasses at :80). Blocked -> emit send_blocked{channel}, no mint.
    const uint16_t cap = channel_cap_origin();
    const uint16_t used = channel_buffer_count_from_origin(_node_id);   // own distinct floods currently held
    const char* block_reason = nullptr; uint32_t next_ms = 0;
    if (_last_channel_origin_ms != 0 && now - _last_channel_origin_ms < protocol::channel_min_interval_ms) {
        block_reason = "min_interval";
        next_ms = static_cast<uint32_t>(protocol::channel_min_interval_ms - (now - _last_channel_origin_ms));
    } else if (used >= cap) {
        block_reason = "cap"; next_ms = 0;                             // window-cap wait; Slice 5 fills the exact recovery
    }
    if (block_reason) {
        MR_TELEMETRY(
            EventField f[] = { { .key = "kind",    .type = EventField::T::str, .s = "channel" },
                               { .key = "reason",  .type = EventField::T::str, .s = block_reason },
                               { .key = "next_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(next_ms) } };
            _hal.emit("send_blocked", f, 3); );
        return 0;                                                      // not sent
    }
    const uint16_t c = next_ctr(_node_id);
    const uint32_t id = channel_msg_id_mint(_node_id, _key_hash32, static_cast<uint8_t>(c & 0xff));
    ChannelEntry e{};
    e.id = id; e.channel_id = channel_id; e.flavor = protocol::channel_flavor_public; e.origin = _node_id;
    e.dirty = true; e.bcn_ad_count = 0; e.received_at = now;
    e.payload_len = (body_len > protocol::channel_msg_max_payload_bytes)
                    ? protocol::channel_msg_max_payload_bytes : body_len;
    if (e.payload_len) std::memcpy(e.payload, body, e.payload_len);
    channel_buffer_add(e);
    _last_channel_origin_ms = now;                                     // stamp for the next self-interval check
    // (Slice 3 removes self_originate_observe(); do_send_channel no longer shares the DM self-cap ledger)
```
  ⚠ `channel_buffer_count_from_origin(_node_id)` = count of currently-buffered entries whose `.origin == _node_id`. If no such helper exists, inline the loop here (walk `_active->_channel_buffer[0.._channel_buffer_n)` counting `.origin == _node_id`) rather than adding a public method — the value is the local `used`.
- [ ] **Step 4: Run, expect PASS** — Run: `pio test -e native`; `.pio/build/native/program 2>&1 | grep "Status:"`. Confirm the new self-gate case passes AND the pre-existing `do_send_channel` cases ("send_channel buffers a dirty entry…", "FLOOD originate…", the eviction fills at :264/:283) still pass — those fire single posts (or fills at a single `_now`) so ⚠ the fill loops at `t==0` will now trip the `used >= cap`/interval self-gate after the first: FIX those existing fill tests to step `hal._now += protocol::channel_min_interval_ms` per `send_channel` (the eviction cases already care only about which id survives, so stepped time is harmless), OR set `cfg.duty_cycle` high enough that `cap` exceeds the fill count and drive them one-per-window. Re-run and confirm all green.
- [ ] **Step 5: Commit** — `git add lib/core/node_channel.cpp lib/core/node.h test/test_node_channel.cpp && git commit -m "Slice 2: do_send_channel self-gates own posts (channel_cap_origin + 10s floor), emits send_blocked{channel}, drops self_originate_observe()"`


---

## SLICE 3: DM self-cap removal + `dm_min_interval` self-throttle

Retire the flat per-window own-origination self-cap (the `become_free` defer at `node_mac.cpp:391-406`, the `Cfg::originator_self_cap_per_window` field, and the `self_originate_observe`/`self_originate_count` ledger in `node_budget.cpp`) — the duty plane already governs own-origination volume and is inherently SF-weighted, so the flat count-cap is redundant (spec §Why / §DM). In its place, add a **per-origin burst floor for our OWN DMs**: `dm_min_interval_ms = 3000` (a self/UX throttle that stops per-keystroke sends), applied **INSIDE** the existing `origin==_node_id && !is_forward && !is_channel_m` branch so it auto-skips relays and channel floods (MF9), and **exempting** the e2e-ack / rcmd `DataType`s so a gateway's own cross-layer ack-confirms never self-throttle. This slice also removes `channel_origin_max_per_window` (Slice 0 kept it for the ledger; by Slice 3 the ledger is re-dimensioned by `cap_channel_origin_events` from the shared contract) and updates the one native test that referenced the removed self-cap, plus the one concurrency test whose two back-to-back own DMs now trip the 3 s floor.

**Contract symbols used (owned by earlier slices — reference by name, do not redefine):** `protocol::dm_min_interval_ms` (=3000), `protocol::cap_channel_origin_events`, the `ChannelOriginLedger.ev[]` re-dimension, `PushKind::send_blocked` + `SendBlockedKind{dm,channel}` + `send_blocked.next_ms` (console_json / Slice 6). Where a contract Push field is not yet landed at implement-time, this slice emits the testable **`MR_EMIT("send_blocked", …)`** sim/console event (the observable the native tests count via `hal.count`), exactly as the removed self-cap used `MR_EMIT("originator_self_defer", …)`; the `Push` enqueue for the companion is wired by Slice 6.

---

### Task 1: Remove the flat self-cap defer from `become_free` and delete its ledger

**Files:**
- Modify: `lib/core/node_mac.cpp` (the self-cap block inside `Node::become_free`, currently `node_mac.cpp:386-406`)
- Modify: `lib/core/node.h` (`Cfg::originator_self_cap_per_window` @ :159; the `self_originate_observe`/`self_originate_count` decls @ :427-428; the `_own_orig_events`/`_own_orig_count` members @ :1223-1224)
- Modify: `lib/core/node_budget.cpp` (`self_originate_observe` @ :94, `self_originate_count` @ :104 — DELETE both; KEEP `track_originator_observation` + `compute_originator_metric`)
- Test: `test/test_node_r3.cpp` (replace the `"R4.4 Inc 4 self-cap …"` TEST_CASE @ :1895-1913)

- [ ] **Step 1: Write the failing test** — Replace the whole `"R4.4 Inc 4 self-cap …"` TEST_CASE with a regression asserting the flat self-cap is GONE: many own originations no longer emit the old defer event and are not blocked by a flat count.
```cpp
TEST_CASE("Slice3 — the flat self-cap is removed (no originator_self_defer; own DMs not count-capped)") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    node.on_init(cfg);
    // Seed a direct route to bob(2) so an origination reaches issue_send.
    std::array<uint8_t, 64> bb{};
    const size_t bn = mk_beacon(/*src=*/2, bb);
    CHECK(bn > 0);
    RxMeta bmeta{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) };
    hal._now = 1000; node.on_recv(bb.data(), bn, bmeta);
    // Fire many own DMs, each spaced well past any burst floor: none may hit the (deleted) flat count-cap.
    for (int k = 0; k < 30; ++k) {
        hal._now = 2000 + static_cast<uint64_t>(k) * 10000;   // 10 s apart (> dm_min_interval)
        send_cmd(node, /*dst=*/2, "hi");
        node.on_timer(kQueueWakeupTimerId);                   // let any deferred re-pick drain
    }
    CHECK(hal.count("originator_self_defer") == 0);           // the flat self-cap defer no longer exists
}
```

- [ ] **Step 2: Run, expect FAIL** — Run: `pio test -e native`. Expect a COMPILE failure: `test_node_r3.cpp` still references the removed `self_originate_observe`/`self_originate_count`/`originator_self_cap_per_window` in the old body (and `node_mac.cpp` still emits `originator_self_defer`). Confirm via `.pio/build/native/program 2>&1 | grep "Status:"` not reached (build error) — the build log names the removed symbols.

- [ ] **Step 3: Implement** — Delete the self-cap defer block in `Node::become_free` (`node_mac.cpp`), leaving the plain dequeue:
```cpp
    if (pick == _active->_tx_queue_n) {                            // none ready -> wake at the soonest backoff
        (void)_hal.after(static_cast<uint32_t>(soonest - now), kQueueWakeupTimerId);
        return;
    }
    TxItem item = _active->_tx_queue[pick];
    for (uint8_t i = pick + 1; i < _active->_tx_queue_n; ++i) _active->_tx_queue[i - 1] = _active->_tx_queue[i];
    --_active->_tx_queue_n;
    issue_send(item);
```
   In `node.h`, delete the Cfg field `uint16_t originator_self_cap_per_window = 20;` (:159), the two decls `void self_originate_observe();` / `uint8_t self_originate_count(uint64_t*) const;` (:427-428), and the members `uint64_t _own_orig_events[protocol::cap_originator_events] = {};` + `uint8_t _own_orig_count = 0;` (:1223-1224). In `node_budget.cpp`, delete the `Node::self_originate_observe()` and `Node::self_originate_count(...)` definitions (:94-112) and the `// Inc 4 self-cap …` comment above them; leave `track_originator_observation` and `compute_originator_metric` intact.

- [ ] **Step 4: Run, expect PASS** — Run: `pio test -e native`. Read `.pio/build/native/program 2>&1 | grep "Status:"` — the new regression passes; the rest of the suite still compiles and is green (the concurrency test is fixed in Task 3).

- [ ] **Step 5: Commit** — `git add lib/core/node_mac.cpp lib/core/node.h lib/core/node_budget.cpp test/test_node_r3.cpp && git commit -m "Slice 3: remove flat DM self-cap (defer + self_originate ledger + Cfg field)"`

---

### Task 2: Add `dm_min_interval_ms` self-throttle inside the own-origin branch (exempt e2e-ack/rcmd)

**Files:**
- Modify: `lib/core/node.h` (add `uint64_t _last_dm_origin_ms = 0;` member beside the other MAC self-state, e.g. after `_ack_warn_until` @ :1222)
- Modify: `lib/core/node_mac.cpp` (`Node::become_free`, at the site where the self-cap block was removed — inside the `origin==_node_id && !is_forward && !is_channel_m` guard)
- Test: `test/test_node_r3.cpp` (new TEST_CASE)

- [ ] **Step 1: Write the failing test** — A 2nd own DM within 3 s defers (no RTS, `send_blocked` fires); one spaced ≥ 3 s passes. Uses the beacon-seeded route pattern from Task 1.
```cpp
TEST_CASE("Slice3 — dm_min_interval: a <3s 2nd own DM defers, a >=3s one passes") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    node.on_init(cfg);
    std::array<uint8_t, 64> bb{};
    const size_t bn = mk_beacon(/*src=*/2, bb);
    CHECK(bn > 0);
    RxMeta bmeta{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) };
    hal._now = 1000; node.on_recv(bb.data(), bn, bmeta);
    // DM #1 -> issues immediately (queue idle), stamps _last_dm_origin_ms.
    hal._now = 5000; send_cmd(node, /*dst=*/2, "a");
    CHECK(hal.count("rts_tx") == 1);
    // DM #2 only 1 s later -> deferred at the 3 s floor: no new RTS, one send_blocked{dm}.
    hal._now = 6000; send_cmd(node, /*dst=*/2, "b");
    CHECK(hal.count("rts_tx") == 1);                 // still 1 -> not issued
    CHECK(hal.count("send_blocked") >= 1);           // the DM burst floor tripped
    // Advance past 3 s from DM #1 and re-drain -> DM #2 now issues.
    hal._now = 8001; node.on_timer(kQueueWakeupTimerId);
    CHECK(hal.count("rts_tx") == 2);
}
```

- [ ] **Step 2: Run, expect FAIL** — Run: `pio test -e native`. Expect FAIL: `_last_dm_origin_ms` is undefined (compile error) or, once declared, DM #2 issues immediately so `rts_tx == 2` at now=6000 and `send_blocked == 0`. Confirm from `.pio/build/native/program 2>&1 | grep "Status:"`.

- [ ] **Step 3: Implement** — Add the member in `node.h` (near `_ack_warn_until`):
```cpp
    uint64_t _last_dm_origin_ms = 0;   // Slice 3: own-DM burst floor (dm_min_interval_ms); relays/floods/e2e-ack/rcmd exempt
```
   In `become_free`, re-introduce the own-origin guard (now for the burst floor, not the count-cap), exempting the e2e-ack/rcmd frame types (their `type` is threaded on the TxItem):
```cpp
    // Slice 3 DM burst floor (MF9): space our OWN DM originations >= dm_min_interval_ms. Living inside the
    // own-origin guard auto-exempts forwards (is_forward) + channel floods (is_channel_m); e2e-ack / rcmd
    // DATA are exempt by TYPE so a gateway's cross-layer ack-confirms never self-throttle.
    {
        TxItem& pt = _active->_tx_queue[pick];
        const bool exempt_type = (pt.type == DATA_TYPE_E2E_ACK) || (pt.type == DATA_TYPE_REMOTE_CMD)
                              || (pt.type == DATA_TYPE_REMOTE_RESP);
        if (pt.origin == _node_id && !pt.is_forward && !pt.is_channel_m && !exempt_type) {
            if (_last_dm_origin_ms != 0 && now - _last_dm_origin_ms < protocol::dm_min_interval_ms) {
                const uint64_t until = _last_dm_origin_ms + protocol::dm_min_interval_ms;
                pt.next_attempt_ms = until;                     // defer in place
                const uint32_t next_ms = static_cast<uint32_t>(until - now);
                MR_EMIT("send_blocked", EF_S("kind", "dm"), EF_S("reason", "min_interval"),
                        EF_I("next_ms", next_ms), EF_I("dst", pt.dst), EF_I("ctr", pt.ctr));
                become_free();                                  // re-pick (skips the now-deferred item)
                return;
            }
            _last_dm_origin_ms = now;                           // admitted -> stamp
        }
    }
```
   (The `send_blocked` `Push` for the companion is wired by Slice 6's `PushKind::send_blocked`; the `MR_EMIT` above is the native-testable observable.)

- [ ] **Step 4: Run, expect PASS** — Run: `pio test -e native`. Read `.pio/build/native/program 2>&1 | grep "Status:"` — the dm_min_interval test passes.

- [ ] **Step 5: Commit** — `git add lib/core/node.h lib/core/node_mac.cpp test/test_node_r3.cpp && git commit -m "Slice 3: add dm_min_interval own-DM burst floor (exempt relay/flood/e2e-ack/rcmd)"`

---

### Task 3: Prove the relay/channel/e2e-ack exemptions + repair the back-to-back concurrency test

**Files:**
- Modify: `test/test_node_r3.cpp` (new exemption TEST_CASE; fix the `"R3.x concurrency …"` TEST_CASE @ :382 whose two 1 ms-apart own DMs now trip the 3 s floor)

- [ ] **Step 1: Write the failing test** — (a) A new test asserting an own e2e-ack origination fired within 3 s of an own DM is NOT throttled (relays/floods are covered by the branch guard; the e2e-ack path is the MF9 case). (b) Update the concurrency test to space its two own DMs past `dm_min_interval_ms` so msg-b still drains after the ACK.
```cpp
TEST_CASE("Slice3 — own e2e-ack origination is NOT throttled by dm_min_interval") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    node.on_init(cfg);
    std::array<uint8_t, 64> bb{};
    const size_t bn = mk_beacon(/*src=*/2, bb);
    CHECK(bn > 0);
    RxMeta bmeta{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) };
    hal._now = 1000; node.on_recv(bb.data(), bn, bmeta);
    // An own DM stamps the floor.
    hal._now = 5000; send_cmd(node, /*dst=*/2, "a");
    CHECK(hal.count("rts_tx") == 1);
    // An own e2e-ack to node 2, only 100 ms later -> exempt by TYPE, must still enqueue+issue (no defer).
    hal._now = 5100; node.send_e2e_ack(/*to_origin=*/2, /*acked_ctr=*/7);
    CHECK(hal.count("e2e_ack_tx") == 1);                 // enqueued (enqueue_data tx_event) despite <3 s
    CHECK(hal.count("send_blocked") == 0);               // NOT throttled
}
```
   For the concurrency test, change the final ACK's timestamp so the re-drain lands ≥ 3 s after msg-a (now=2000): move `hal._now = 2200;` before the `ack_rx` on_recv (`test_node_r3.cpp:425`) to `hal._now = 5200;` and add a comment.

- [ ] **Step 2: Run, expect FAIL** — Run: `pio test -e native`. Expect: the concurrency test FAILS at its `CHECK(hal.count("rts_tx") == 2)` (msg-b deferred by the 3 s floor at the old 2200 re-drain) UNTIL its ACK timestamp is bumped; the e2e-ack exemption test may already pass (it exercises the exemption path added in Task 2). Confirm from `.pio/build/native/program 2>&1 | grep "Status:"`.

- [ ] **Step 3: Implement** — Apply the concurrency-test timestamp fix:
```cpp
    // ACK completes flight #1 -> become_free re-drains. Advance past dm_min_interval_ms from msg-a (now=2000)
    // so msg-b (an own DM) clears the Slice 3 burst floor and its RTS issues.
    hal._now = 5200; node.on_recv(ab.data(), an, bob);
    CHECK(hal.count("ack_rx") == 1);
    CHECK(hal.count("rts_tx") == 2);
```
   No production code changes in this task (exemptions already implemented in Task 2; this task pins them + repairs the one collateral test).

- [ ] **Step 4: Run, expect PASS** — Run: `pio test -e native`. Read `.pio/build/native/program 2>&1 | grep "Status:"` — both the exemption test and the repaired concurrency test pass; **full native suite green**.

- [ ] **Step 5: Commit** — `git add test/test_node_r3.cpp && git commit -m "Slice 3: pin e2e-ack/rcmd dm-throttle exemptions; fix back-to-back concurrency test spacing"`

---

### Task 4: Remove `channel_origin_max_per_window` now the ledger is re-dimensioned

**Files:**
- Modify: `lib/core/protocol_constants.h` (delete `channel_origin_max_per_window` @ :239)
- Modify: `lib/core/node.h` (the `Cfg::channel_origin_max_per_window` @ :114 and the `ChannelOriginLedger.ev[]` bound @ :766 — switch to `protocol::cap_channel_origin_events`, the Slice 0 replacement)
- Modify: `test/test_node_channel.cpp` (the 6 references @ :229-261 — retarget to the new bound)

- [ ] **Step 1: Write the failing test** — Confirm the ledger is now sized by the contract const, not the removed one. In `test/test_node_channel.cpp`, replace `protocol::channel_origin_max_per_window` with `protocol::cap_channel_origin_events` in the saturation loop/asserts (the count-cap behaviour is unchanged; only the symbol name moves):
```cpp
    for (int k = 0; k < protocol::cap_channel_origin_events; ++k) {
        // ... (existing body unchanged)
    }
    CHECK(node.channel_buffer_count() == protocol::cap_channel_origin_events);
```
   (Apply the same rename to all 6 sites at :229/:233/:238/:250/:252/:255/:261.)

- [ ] **Step 2: Run, expect FAIL** — Run: `pio test -e native`. Expect a COMPILE failure: `node.h`/`protocol_constants.h`/`node_channel.cpp` still reference `channel_origin_max_per_window` (now that the tests dropped it, the mismatch is exposed) — confirm the build error names the symbol; `.pio/build/native/program` not produced.

- [ ] **Step 3: Implement** — In `protocol_constants.h`, delete `inline constexpr uint8_t channel_origin_max_per_window = 20;` (:239). In `node.h`, change the Cfg default init `uint16_t channel_origin_max_per_window = protocol::channel_origin_max_per_window;` (:114) to seed from the new const `= protocol::cap_channel_origin_events;` and change the ledger struct `ChannelOriginEvent ev[protocol::channel_origin_max_per_window];` (:766) to `ev[protocol::cap_channel_origin_events];`. Verify no remaining `channel_origin_max_per_window` references: `grep -rn channel_origin_max_per_window lib/ src/ test/` returns nothing.

- [ ] **Step 4: Run, expect PASS** — Run: `pio test -e native`. Read `.pio/build/native/program 2>&1 | grep "Status:"` — green; the channel ledger tests pass against the new bound.

- [ ] **Step 5: Commit** — `git add lib/core/protocol_constants.h lib/core/node.h test/test_node_channel.cpp && git commit -m "Slice 3: remove channel_origin_max_per_window (ledger re-dimensioned by cap_channel_origin_events)"`

---

## SLICE 4: Gateway-exemption verification (no new production code)

This slice adds **verification-only** tests that pin the invariant the design leans on (spec §"Gateway cross-layer relays — EXEMPT", MF5/MF6/MF9): a dual-layer gateway (`n_layers==2`) is already fully outside every anti-spam gate introduced by Slices 2/3, and no origin-id keying was added that could wrongly bite the bridge. The mechanisms pre-exist — `channel_origin_admit` early-returns `false` at `node_channel.cpp:79` on `n_layers==2` (so `channel_cap_origin()` / the 10 s channel floor never run on a gateway's channel plane), and the Slice-3 `dm_min_interval` sits **inside** the `origin==_node_id && !is_forward && !is_channel_m` branch (node_mac.cpp:391), so a bridged DM (`is_forward==true`, `is_gw_relay==true`, set at node_mac_rx.cpp:846) auto-skips it, while a gateway's OWN e2e-ack is exempted by DataType (Slice 3, MF9). These tests would go **RED if a later Slice-2/3 change ever moved a gate outside those guards** — that is the whole point.

All tests land in the existing `test/test_dual_layer.cpp` (it already owns `StubHal`, `good_layer`, and the `DualLayerTestAccess` friend with `origin_admit`/`pump`/`leaf_tx_n`/`leaf_tx_at`/`clear_tx_queue`/`learn_neighbor`/`bridge_from`). ⚠ `-fno-exceptions` ⇒ **`REQUIRE` is unavailable → `CHECK` only**. Read the real result from `.pio/build/native/program 2>&1 | grep "Status:"` after `pio test -e native`. If any of these tests FAIL, that reveals a Slice-2/3 gate now bites the bridge — flag it as a Slice-2/3 bug, do **not** relax the test. **Metal note:** the on-metal analogue is the spec §Tests "★ Gateway exemption" gate (a live dual-layer gateway bridging high cross-layer DM+channel volume trips no limit) — the user bench-runs that; this slice is its native proxy.

Slices 2/3 are assumed landed; these tests reference their symbols (`channel_cap_origin()`, `channel_min_interval_ms`, `dm_min_interval_ms`, the `last_dm_origin_ms` member) only indirectly — the gateway paths must be inert regardless of what those gates compute.

### Task 1: A dual-layer gateway's channel plane admits nothing — `channel_origin_admit` returns false out of plane
**Files:**
- Test: `test/test_dual_layer.cpp` (new `TEST_CASE`, append near the existing channel-plane friend tests; drive via `DualLayerTestAccess::origin_admit`).
- Modify: none (verification only).

- [ ] **Step 1: Write the failing test**
```cpp
TEST_CASE("gateway exemption: channel_origin_admit early-returns false out-of-plane (n_layers==2) — cap_origin never runs on a bridge") {
    StubHal hal; Node node(hal, /*id=*/1, /*key=*/0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(/*layer_id*/ 1, /*sf*/ 8);
    cfg.layers[1] = good_layer(/*layer_id*/ 2, /*sf*/ 9);
    CHECK(node.on_init(cfg));                                    // valid gateway inits (-fno-exceptions => CHECK only)

    // A gateway is OUT of the channel plane (node_channel.cpp:79) -> admit is a hard false BEFORE any
    // cap_origin / channel_min_interval math. Distinct origins, first-ever ids, low count: on a NON-gateway
    // these would admit; here every one is dropped purely by the n_layers==2 guard. This is what makes a
    // bridged channel flood exempt from cap_origin (MF5) — the cap can never be reached to bite it.
    for (uint8_t origin = 20; origin < 30; ++origin)
        CHECK_FALSE(DualLayerTestAccess::origin_admit(node, origin, /*msg_id=*/0xAA000000u | origin));
    // Even the gateway's OWN node_id as origin is refused out-of-plane (the self-bypass at :80 is never reached).
    CHECK_FALSE(DualLayerTestAccess::origin_admit(node, /*origin=*/1, /*msg_id=*/0x01000001u));
}
```

- [ ] **Step 2: Run, expect FAIL**
Run: `pio test -e native` (then `.pio/build/native/program 2>&1 | grep "Status:"`). Expected: the whole native run fails to link/compile only if the TEST_CASE has a typo; if the mechanism regressed, the CHECK_FALSE assertions fail. On green Slice-2/3 this test PASSES immediately (the guard pre-exists) — so if it FAILS, `channel_origin_admit` no longer early-returns on `n_layers==2` (a Slice-2 bug: the cap-swap moved above the `:79` guard). Confirm the failing line is a `CHECK_FALSE` in this case.

- [ ] **Step 3: Implement** — no production change. The guard already exists at `lib/core/node_channel.cpp:79`:
```cpp
bool Node::channel_origin_admit(uint8_t origin, uint32_t msg_id) {
    if (_cfg.n_layers == 2) return false;                       // Principle 11: a dual-layer gateway is OUT of the channel plane
    if (origin == _node_id) return true;                        // self bypasses
    // ... cap_origin / channel_min_interval enforcement (Slice 2) lives BELOW this guard
```
The only action is to confirm Slice 2 kept its `channel_cap_origin()` + 10 s-floor logic **below** line 79 (never above it). No code edit if that holds.

- [ ] **Step 4: Run, expect PASS**
Run: `pio test -e native`; `.pio/build/native/program 2>&1 | grep "Status:"`. Expected: `Status: SUCCESS!` and the new TEST_CASE in the assertion count.

- [ ] **Step 5: Commit**
`git add test/test_dual_layer.cpp && git commit -m "test(antispam): gateway channel plane admits nothing (cap_origin out-of-plane, MF5)"`

### Task 2: A bridged DM does NOT hit `dm_min_interval` — the self-throttle skips relays
**Files:**
- Test: `test/test_dual_layer.cpp` (new `TEST_CASE`; queue a bridged DM leg via `DualLayerTestAccess::bridge_from`, then drain with `pump` and assert it was sent, not deferred).
- Modify: none (verification only).

- [ ] **Step 1: Write the failing test**
```cpp
TEST_CASE("gateway exemption: a bridged DM (is_forward) skips dm_min_interval — the self-throttle lives inside !is_forward") {
    StubHal hal; Node node(hal, /*id=*/1, /*key=*/0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(/*layer_id*/ 1, /*sf*/ 8);
    cfg.layers[1] = good_layer(/*layer_id*/ 2, /*sf*/ 9);
    CHECK(node.on_init(cfg));

    // Bridge two cross-layer DM legs back-to-back at the SAME timestamp (0 ms apart). If the Slice-3
    // dm_min_interval (3 s) wrongly applied to forwards, the 2nd would defer (bump next_attempt_ms) because
    // it fires < dm_min_interval_ms after the 1st. It must NOT: the throttle sits inside the
    // origin==_node_id && !is_forward branch (node_mac.cpp:391), and a bridged leg is is_forward=true +
    // is_gw_relay=true (node_mac_rx.cpp:846). Both legs must be immediately drainable.
    DualLayerTestAccess::learn_neighbor(node, /*node_id=*/5);   // a 1-hop route so become_free issues, not stalls
    const uint8_t inner[3] = { 0x00, 0x00, 0x00 };              // minimal normal-unicast inner (flags=0)
    DualLayerTestAccess::clear_tx_queue(node, /*leaf=*/0);
    DualLayerTestAccess::bridge_from(node, /*origin=*/40, /*dst=*/5, /*ctr=*/0x11, /*flags=*/0, inner, 3);
    DualLayerTestAccess::bridge_from(node, /*origin=*/41, /*dst=*/5, /*ctr=*/0x12, /*flags=*/0, inner, 3);
    // Confirm both landed as forward legs on a leaf tx queue (bridge routes to the far layer).
    uint8_t queued = static_cast<uint8_t>(DualLayerTestAccess::leaf_tx_n(node, 0) + DualLayerTestAccess::leaf_tx_n(node, 1));
    CHECK(queued >= 2);
    bool both_forward = true;
    for (uint8_t leaf = 0; leaf < 2; ++leaf)
        for (uint8_t i = 0; i < DualLayerTestAccess::leaf_tx_n(node, leaf); ++i)
            if (!DualLayerTestAccess::leaf_tx_at(node, leaf, i).is_forward) both_forward = false;
    CHECK(both_forward);                                        // every bridged leg carries is_forward=true (skips the DM branch)
}
```

- [ ] **Step 2: Run, expect FAIL**
Run: `pio test -e native`; `.pio/build/native/program 2>&1 | grep "Status:"`. On green Slice-3 this PASSES (the `dm_min_interval` guard lives inside `!is_forward`, so a forward never reaches it). It FAILS only if Slice 3 hoisted `dm_min_interval` above the `!is_forward` test — then `both_forward`/`queued` still hold but a later drain would defer; the CHECK on `is_forward` still guards the invariant. If `bridge_from` routes differently than assumed, the `queued >= 2` CHECK localizes it.

- [ ] **Step 3: Implement** — no production change. The guard already exists at `lib/core/node_mac.cpp:391`:
```cpp
    if (_active->_tx_queue[pick].origin == _node_id && !_active->_tx_queue[pick].is_forward
        && !_active->_tx_queue[pick].is_channel_m) {
        // Slice 3: dm_min_interval (last_dm_origin_ms) belongs HERE — a forward (is_forward) never enters,
        // so a bridged cross-layer relay is auto-exempt (MF9). Do NOT lift the min-interval above this branch.
```
Action: confirm Slice 3 placed `dm_min_interval` **inside** this branch (and NOT keyed on `origin==_node_id` alone, which a bridge sets on the far layer). No edit if that holds.

- [ ] **Step 4: Run, expect PASS**
Run: `pio test -e native`; expect `Status: SUCCESS!`.

- [ ] **Step 5: Commit**
`git add test/test_dual_layer.cpp && git commit -m "test(antispam): bridged DM skips dm_min_interval (is_forward-guarded, MF9)"`

### Task 3: A gateway's OWN e2e-ack is exempt from `dm_min_interval` (Slice-3 DataType exemption, MF9)
**Files:**
- Test: `test/test_dual_layer.cpp` (new `TEST_CASE`; originate two `send_e2e_ack`s < 3 s apart via a new `DualLayerTestAccess` accessor, assert neither is deferred).
- Modify: `test/test_dual_layer.cpp` — add a `send_ack`/`enqueue_data` accessor to `DualLayerTestAccess` (test infra only, not production).

- [ ] **Step 1: Write the failing test**
```cpp
// Add to struct DualLayerTestAccess (test infra; node.h already declares it a friend):
//     static void  send_ack(Node& n, uint8_t to, uint16_t ctr) { n.send_e2e_ack(to, ctr); }
//     static const TxItem* tx_back(Node& n, uint8_t leaf) {
//         uint8_t k = n._layers[leaf]._tx_queue_n; return k ? &n._layers[leaf]._tx_queue[k - 1] : nullptr; }

TEST_CASE("gateway exemption: OWN e2e-ack (DATA_TYPE_E2E_ACK) is dm_min_interval-exempt — bridge acks never self-throttle") {
    StubHal hal; Node node(hal, /*id=*/1, /*key=*/0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(/*layer_id*/ 1, /*sf*/ 8);
    cfg.layers[1] = good_layer(/*layer_id*/ 2, /*sf*/ 9);
    CHECK(node.on_init(cfg));
    DualLayerTestAccess::learn_neighbor(node, /*node_id=*/7);   // a 1-hop route so the ack drains

    // A gateway's own e2e-acks (origin==self, !is_forward, !is_channel_m) DO enter the DM self-throttle
    // branch — MF9 exempts them by DataType so the 3 s dm_min_interval can't delay a cross-layer delivery
    // confirmation. Fire two acks 0 ms apart: BOTH must enqueue as DATA_TYPE_E2E_ACK and drain (no defer).
    hal._now = 100000;
    DualLayerTestAccess::send_ack(node, /*to_origin=*/7, /*acked_ctr=*/0x33);
    DualLayerTestAccess::send_ack(node, /*to_origin=*/7, /*acked_ctr=*/0x34);      // < dm_min_interval_ms later
    const TxItem* last = DualLayerTestAccess::tx_back(node, /*leaf=*/0);
    CHECK(last != nullptr);
    if (last) {
        CHECK(last->type == DATA_TYPE_E2E_ACK);                 // typed as an ack (the DataType the exemption keys on)
        CHECK_FALSE(last->is_forward);                          // an own-origination (would enter the DM branch)
    }
    DualLayerTestAccess::pump(node);                            // become_free must NOT defer the 2nd ack in place
    DualLayerTestAccess::pump(node);
    // If MF9 held, neither ack was deferred by dm_min_interval; if it regressed, an ack lingers with a
    // future next_attempt_ms. Assert nothing is stuck behind a dm_min_interval defer.
    bool any_deferred = false;
    for (uint8_t leaf = 0; leaf < 2; ++leaf)
        for (uint8_t i = 0; i < DualLayerTestAccess::leaf_tx_n(node, leaf); ++i)
            if (DualLayerTestAccess::leaf_tx_at(node, leaf, i).type == DATA_TYPE_E2E_ACK &&
                DualLayerTestAccess::leaf_tx_at(node, leaf, i).next_attempt_ms > hal._now) any_deferred = true;
    CHECK_FALSE(any_deferred);
}
```

- [ ] **Step 2: Run, expect FAIL**
Run: `pio test -e native`; `.pio/build/native/program 2>&1 | grep "Status:"`. This is RED first because the `send_ack`/`tx_back` accessors don't yet exist (compile error) — add them in Step 3. Once they compile, on a correct Slice-3 (MF9 exemption present) it PASSES; if the DataType exemption was omitted, `any_deferred` is true and the CHECK_FALSE fails — flag it as a Slice-3 bug.

- [ ] **Step 3: Implement** — test-infra accessors only (production `send_e2e_ack` / the MF9 exemption already exist from Slice 3). Add to `struct DualLayerTestAccess` in `test/test_dual_layer.cpp` (near the other tx accessors, e.g. after `leaf_tx_at`):
```cpp
    static void          send_ack(Node& n, uint8_t to, uint16_t ctr) { n.send_e2e_ack(to, ctr); }   // an OWN e2e-ack origination
    static const TxItem* tx_back(Node& n, uint8_t leaf) {                                            // last-enqueued item on a leaf
        uint8_t k = n._layers[leaf]._tx_queue_n; return k ? &n._layers[leaf]._tx_queue[k - 1] : nullptr; }
```
Confirm the Slice-3 change at `lib/core/node_mac.cpp` exempts `DATA_TYPE_E2E_ACK` (and `DATA_TYPE_REMOTE_CMD`/`DATA_TYPE_REMOTE_RESP`, MF9) inside the `origin==_node_id && !is_forward && !is_channel_m` branch before applying `dm_min_interval`. No production edit if it does.

- [ ] **Step 4: Run, expect PASS**
Run: `pio test -e native`; expect `Status: SUCCESS!` with the three new gateway-exemption TEST_CASEs counted.

- [ ] **Step 5: Commit**
`git add test/test_dual_layer.cpp && git commit -m "test(antispam): gateway OWN e2e-ack exempt from dm_min_interval (DataType, MF9)"`

### Task 4: Full-suite regression sweep — the exemption tests coexist with Slices 0–3, whole native suite green
**Files:**
- Test: none new (a full-suite gate; asserts no cross-test interference).
- Modify: none.

- [ ] **Step 1: Write the failing test** — no new TEST_CASE; the "test" is the full native run itself. (Documented here so the TDD loop closes on the whole suite, matching spec §Tests "Full native suite green".)

- [ ] **Step 2: Run, expect FAIL** — only if Tasks 1–3 introduced a compile/link clash (e.g. a duplicate accessor name). Run: `pio test -e native`; `.pio/build/native/program 2>&1 | grep "Status:"`.

- [ ] **Step 3: Implement** — resolve any accessor-name collision (e.g. rename a duplicate `tx_back`/`send_ack` if an earlier slice already added one) so `DualLayerTestAccess` has no repeated member. No production change.

- [ ] **Step 4: Run, expect PASS**
Run: `pio test -e native`; read `.pio/build/native/program 2>&1 | grep "Status:"` → expect `Status: SUCCESS!` with the full assertion/test-case count including the three gateway-exemption cases. This is the slice's gate: a dual-layer gateway trips **no** channel cap, **no** channel/DM burst floor, and its own e2e-acks are exempt — all proven with zero new production code.

- [ ] **Step 5: Commit**
`git add -A && git commit -m "test(antispam): full native suite green with gateway-exemption verification (Slice 4)"`

---

## SLICE 5: the `limits` query — `Node::limits_snapshot()` + `write_limits()` + the `limits` verb (USB + BLE)

This slice exposes the live anti-spam / duty state to the iOS companion as one read-only JSON line so it can render the user's headroom ("Next channel: ready · Next DM: 1.2 s"). It adds a `LimitsSnapshot` value struct + `Node::limits_snapshot()` (composing the earlier-slice getters `channel_duty_budget_ms()`, `channel_cap_origin()`, `rt_count()`, `max_data_sf()`, `duty_status()`, and `_hal.airtime_used_ms(originator_window_ms)`), a `write_limits()` NDJSON writer mirroring `write_duty` (console_json.cpp:256), and the `limits` verb in BOTH firmware dispatchers (USB `service_debug` ~fw_main.cpp:1314, BLE `ble_dispatch_line` ~fw_main.cpp:1469). No OTA wire change — `limits` is a local console/BLE query (MF6). It depends on Slices 0–3 already providing `channel_duty_budget_ms()`, `channel_cap_origin()`, `channel_cap_origin`'s internal `C` ceiling, the per-origin `last_flood_ms`, and `last_dm_origin_ms`; those are referenced here by contract name only.

The `limits` JSON shape (from the spec §Status):
```json
{"ev":"limits","win_ms":300000,"win_left_ms":142000,"n":40,"ch_sf":7,
 "ch_cap":8,"ch_used":2,"ch_min_ms":10000,"ch_next_ms":0,"ch_ceiling":42,
 "dm_min_ms":3000,"dm_next_ms":1200,"duty_ms":3000,"duty_used_ms":640}
```
`ch_next_ms`/`dm_next_ms` = `max(burst-floor remaining, channel cap-wait, duty recovery = duty_status().avail_ms)`; `duty_ms` = `channel_duty_budget_ms()` (5-min basis); `duty_used_ms` = `_hal.airtime_used_ms(protocol::originator_window_ms)`.

**Baseline (verified 2026-07-02):** `.pio/build/native/program` → `[doctest] Status: SUCCESS!`, 557 test cases / 24194 assertions passing. Read the real result via `.pio/build/native/program 2>&1 | grep "Status:"` (the `pio test` summary line can show a stale/cached "0 test cases"). ⚠ `-fno-exceptions` ⇒ `REQUIRE` is unavailable; use `CHECK` + `if`-guards only. Tests carry no `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` (test_airtime.cpp owns `main()`).

---

### Task 1: `write_limits()` — the `limits` NDJSON writer + its shape/values test

**Files:**
- Test: `test/test_console_json.cpp` (new `TEST_CASE` after the `write_duty` case at :139)
- Modify: `lib/console/console_json.h` (declare after `write_duty` at :49)
- Modify: `lib/console/console_json.cpp` (define after `write_duty` at :256)

- [ ] **Step 1: Write the failing test**
```cpp
TEST_CASE("write_limits — the companion `limits` query shape/values") {
    char b[256];
    meshroute::console::LimitsFields L;
    L.win_ms = 300000; L.win_left_ms = 142000; L.n = 40; L.ch_sf = 7;
    L.ch_cap = 8; L.ch_used = 2; L.ch_min_ms = 10000; L.ch_next_ms = 0; L.ch_ceiling = 42;
    L.dm_min_ms = 3000; L.dm_next_ms = 1200; L.duty_ms = 3000; L.duty_used_ms = 640;
    size_t n = write_limits(b, sizeof b, L);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"limits\",\"win_ms\":300000,\"win_left_ms\":142000,\"n\":40,\"ch_sf\":7,"
      "\"ch_cap\":8,\"ch_used\":2,\"ch_min_ms\":10000,\"ch_next_ms\":0,\"ch_ceiling\":42,"
      "\"dm_min_ms\":3000,\"dm_next_ms\":1200,\"duty_ms\":3000,\"duty_used_ms\":640}\n");
    // duty-disabled node: duty_ms == 0 -> still a well-formed line (fields never omitted)
    L.duty_ms = 0; L.duty_used_ms = 0; L.ch_cap = 20; L.ch_ceiling = 0;
    n = write_limits(b, sizeof b, L);
    CHECK(std::string(b, n).find("\"duty_ms\":0,\"duty_used_ms\":0}") != std::string::npos);
}
```

- [ ] **Step 2: Run, expect FAIL** Run: `pio test -e native`. Expected: compile error — `meshroute::console::LimitsFields` and `write_limits` are undeclared (the struct + writer do not exist yet).

- [ ] **Step 3: Implement** In `console_json.h`, add the field struct + declaration right after the `write_duty` declaration (:49):
```cpp
// `limits` query reply (companion anti-spam/headroom screen). All fields are plain u32 — no float
// on the wire (newlib-nano printf has no %f/%lld). Live-computed by Node::limits_snapshot().
struct LimitsFields {
    uint32_t win_ms      = 0;   // originator_window_ms (the 5-min cap window)
    uint32_t win_left_ms = 0;   // ms until the current window rolls
    uint32_t n           = 0;   // rt_count() — mesh size the cap divides by
    uint32_t ch_sf       = 0;   // max_data_sf() — the DATA-M SF T_ch is priced at
    uint32_t ch_cap      = 0;   // channel_cap_origin() — this origin's per-window channel cap
    uint32_t ch_used     = 0;   // this node's own distinct floods this window
    uint32_t ch_min_ms   = 0;   // channel_min_interval_ms burst floor
    uint32_t ch_next_ms  = 0;   // ms until a channel post is actually allowed (0 = now)
    uint32_t ch_ceiling  = 0;   // C — total channel capacity (0 when duty disabled)
    uint32_t dm_min_ms   = 0;   // dm_min_interval_ms burst floor
    uint32_t dm_next_ms  = 0;   // ms until an own DM is actually allowed (0 = now)
    uint32_t duty_ms     = 0;   // channel_duty_budget_ms() — the 5-min D basis (0 = duty disabled)
    uint32_t duty_used_ms = 0;  // airtime used this 5-min window
};
size_t write_limits(char* buf, size_t cap, const LimitsFields& L);
```
In `console_json.cpp`, add the writer right after `write_duty` (after its closing `}` at :263):
```cpp
size_t write_limits(char* buf, size_t cap, const LimitsFields& L) {
    JsonBuf j(buf, cap);
    j.lit("{\"ev\":\"limits\",\"win_ms\":");  j.u32(L.win_ms);
    j.lit(",\"win_left_ms\":");  j.u32(L.win_left_ms);
    j.lit(",\"n\":");            j.u32(L.n);
    j.lit(",\"ch_sf\":");        j.u32(L.ch_sf);
    j.lit(",\"ch_cap\":");       j.u32(L.ch_cap);
    j.lit(",\"ch_used\":");      j.u32(L.ch_used);
    j.lit(",\"ch_min_ms\":");    j.u32(L.ch_min_ms);
    j.lit(",\"ch_next_ms\":");   j.u32(L.ch_next_ms);
    j.lit(",\"ch_ceiling\":");   j.u32(L.ch_ceiling);
    j.lit(",\"dm_min_ms\":");    j.u32(L.dm_min_ms);
    j.lit(",\"dm_next_ms\":");   j.u32(L.dm_next_ms);
    j.lit(",\"duty_ms\":");      j.u32(L.duty_ms);
    j.lit(",\"duty_used_ms\":"); j.u32(L.duty_used_ms);
    j.ch('}');
    return j.finish();
}
```

- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native`, then `.pio/build/native/program 2>&1 | grep "Status:"` → `Status: SUCCESS!` with the new case counted (558).

- [ ] **Step 5: Commit** `git add lib/console/console_json.h lib/console/console_json.cpp test/test_console_json.cpp && git commit -m "Slice 5: write_limits() NDJSON writer + LimitsFields struct + test"`

---

### Task 2: `Node::limits_snapshot()` — compose the live limits values

**Files:**
- Test: `test/test_node_channel.cpp` (new `TEST_CASE` at end of file, before the closing anonymous-namespace `}`)
- Modify: `lib/core/node.h` (declare the `LimitsSnapshot` struct + getter near `duty_status()` at :528-529)
- Modify: `lib/core/node_mac.cpp` (define after `Node::duty_status()` at :790)

- [ ] **Step 1: Write the failing test**
```cpp
TEST_CASE("Node::limits_snapshot — live values for a known config") {
    TestHal hal; Node node(hal, 3, 0x1234ABCDu);
    NodeConfig cfg = basic_cfg(); cfg.duty_cycle = 0.0;   // shipped default -> duty disabled
    node.on_init(cfg);
    std::array<uint8_t,64> bb{};
    const size_t bn = mk_beacon(/*src=*/50, bb); node.on_recv(bb.data(), bn, meta_at(10));  // rt_count() -> 1
    CHECK(node.rt_count() >= 1);
    Node::LimitsSnapshot s = node.limits_snapshot();
    CHECK(s.win_ms == protocol::originator_window_ms);   // 300000
    CHECK(s.n == node.rt_count());
    CHECK(s.ch_sf == node.max_data_sf());
    CHECK(s.ch_cap == node.channel_cap_origin());
    CHECK(s.duty_ms == node.channel_duty_budget_ms());   // 0 when duty disabled
    CHECK(s.duty_ms == 0);
    CHECK(s.ch_ceiling == 0);                            // C == 0 when duty disabled (legacy-flat-cap regime)
    CHECK(s.ch_next_ms == 0);                            // fresh node, no prior flood/DM -> ready now
    CHECK(s.dm_next_ms == 0);
    CHECK(s.dm_min_ms == protocol::dm_min_interval_ms);
    CHECK(s.ch_min_ms == protocol::channel_min_interval_ms);

    // duty ENABLED -> duty_ms == the 5-min D (1% * 300000 = 3000), NOT the 1-hour budget (MF1)
    TestHal hal2; Node n2(hal2, 3, 0x1234ABCDu);
    NodeConfig c2 = basic_cfg(); c2.duty_cycle = 0.01; n2.on_init(c2);
    Node::LimitsSnapshot s2 = n2.limits_snapshot();
    CHECK(s2.duty_ms == 3000);                           // == channel_duty_budget_ms(), 5-min basis
    CHECK(s2.duty_ms == n2.channel_duty_budget_ms());
    CHECK(s2.ch_ceiling >= 1);                           // C >= 1 floor when duty enabled
}
```

- [ ] **Step 2: Run, expect FAIL** Run: `pio test -e native`. Expected: compile error — `Node::LimitsSnapshot` / `Node::limits_snapshot()` are undeclared.

- [ ] **Step 3: Implement** In `node.h`, add the struct + getter directly after `duty_status()` (:529), inside the same public block:
```cpp
    // `limits` query snapshot (companion anti-spam/headroom screen). Live-computed on demand: counters +
    // the channel_cap_origin() formula (cheap, idempotent, no state change). *_next_ms = the true
    // "when can I send next" = max(burst-floor remaining, channel window cap-wait, duty recovery).
    struct LimitsSnapshot {
        uint32_t win_ms, win_left_ms, n, ch_sf, ch_cap, ch_used,
                 ch_min_ms, ch_next_ms, ch_ceiling, dm_min_ms, dm_next_ms, duty_ms, duty_used_ms;
    };
    LimitsSnapshot    limits_snapshot() const;
```
In `node_mac.cpp`, define it after `Node::duty_status()`'s closing `}` (:790):
```cpp
Node::LimitsSnapshot Node::limits_snapshot() const {
    LimitsSnapshot s{};
    s.win_ms       = protocol::originator_window_ms;
    s.n            = rt_count();
    s.ch_sf        = max_data_sf();
    s.ch_cap       = channel_cap_origin();
    s.ch_min_ms    = protocol::channel_min_interval_ms;
    s.dm_min_ms    = protocol::dm_min_interval_ms;
    s.duty_ms      = channel_duty_budget_ms();                            // 5-min D basis (0 = duty disabled), MF1
    s.duty_used_ms = static_cast<uint32_t>(_hal.airtime_used_ms(protocol::originator_window_ms));

    const uint64_t now = _hal.now();
    // C (channel ceiling): D / T_ch when duty is enabled, else 0 (legacy-flat-cap regime, MF2).
    if (s.duty_ms > 0) {
        const uint32_t T_ch = airtime_routing_ms(43)
            + airtime_ms(max_data_sf(), _cfg.radio_bw_hz, _cfg.radio_cr, protocol::preamble_sym,
                         protocol::m_frame_hdr_len + protocol::channel_msg_max_payload_bytes);
        s.ch_ceiling = (T_ch > 0) ? (s.duty_ms / T_ch < 1u ? 1u : s.duty_ms / T_ch) : 1u;   // C >= 1 floor
    }

    // window_left: originator_window_ms is a rolling counter (airtime_used_ms), so there is no single
    // reset instant — report the full window as "left" (the app reads it as the reset horizon; the cap
    // is a sliding count, not a hard reset). Kept explicit so the field is deterministic.
    s.win_left_ms = protocol::originator_window_ms;

    // ch_used = this node's OWN distinct floods in the current channel window (the self-gate's ledger),
    // and ch_next_ms/dm_next_ms = the true "ready" times. own_channel_window_count() / channel_next_ready_ms()
    // / dm_next_ready_ms() are the self-gate accessors added in Slices 2/3; compose them here.
    const DutyStatus ds = duty_status();
    s.ch_used    = own_channel_window_count();
    s.ch_next_ms = channel_next_ready_ms(now);
    if (ds.avail_ms > s.ch_next_ms) s.ch_next_ms = ds.avail_ms;           // duty recovery dominates when silent
    s.dm_next_ms = dm_next_ready_ms(now);
    if (ds.avail_ms > s.dm_next_ms) s.dm_next_ms = ds.avail_ms;
    return s;
}
```
NOTE for the implementer: `own_channel_window_count()`, `channel_next_ready_ms(now)`, `dm_next_ready_ms(now)` are the Slice-2/Slice-3 self-gate accessors (they read `_per_origin_channel[_node_id].n` / `last_flood_ms` / `last_dm_origin_ms` against `channel_min_interval_ms` / `dm_min_interval_ms`). If a slice-2/3 accessor lands under a different contract name, substitute it — the `max(floor-remaining, cap-wait, duty avail_ms)` composition here is the load-bearing part. On a fresh node all three return 0, which is what the test asserts.

- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native`, then `.pio/build/native/program 2>&1 | grep "Status:"` → `Status: SUCCESS!`.

- [ ] **Step 5: Commit** `git add lib/core/node.h lib/core/node_mac.cpp test/test_node_channel.cpp && git commit -m "Slice 5: Node::limits_snapshot() composing the live limits values + test"`

---

### Task 3: the `limits` verb in BOTH firmware dispatchers (USB + BLE) — MF6

**Files:**
- Modify: `src/fw_main.cpp` (BLE `ble_dispatch_line`, add after the `duty` verb at :1469-1472; USB `service_debug`, add a `dump_limits()` helper + the verb after the `duty` verb at :1315)

No native test — this is device/ESP32-only firmware glue (the `write_limits` shape + the snapshot values are already covered by Tasks 1–2). Verification is "all 4 boards build".

- [ ] **Step 1: Write the failing test** N/A (firmware dispatch glue — no host-testable seam; the JSON writer and the snapshot are already under test). Instead the failing signal is the board build: after adding the verb strings but before wiring, a reference to `write_limits`/`limits_snapshot` that is mis-spelled fails the compile. Proceed directly to implement + the board-build gate (Step 4).

- [ ] **Step 2: Run, expect FAIL** N/A (see Step 1). Confirm the pre-change state: `grep -n "limits" src/fw_main.cpp` returns nothing (the verb is absent in both dispatchers).

- [ ] **Step 3: Implement**
BLE — in `ble_dispatch_line`, add immediately after the `duty` block (after fw_main.cpp:1472, mirroring it):
```cpp
    if (len == 6 && !strncmp(line, "limits", 6)) {         // companion anti-spam/headroom screen (BLE-only, no OTA change)
        const auto s = g_node.limits_snapshot();
        meshroute::console::LimitsFields L;
        L.win_ms = s.win_ms; L.win_left_ms = s.win_left_ms; L.n = s.n; L.ch_sf = s.ch_sf;
        L.ch_cap = s.ch_cap; L.ch_used = s.ch_used; L.ch_min_ms = s.ch_min_ms;
        L.ch_next_ms = s.ch_next_ms; L.ch_ceiling = s.ch_ceiling;
        L.dm_min_ms = s.dm_min_ms; L.dm_next_ms = s.dm_next_ms;
        L.duty_ms = s.duty_ms; L.duty_used_ms = s.duty_used_ms;
        return write_limits(out, cap, L);                  // fits the 256-B `out` (14 u32 fields ~180 B)
    }
```
USB — add a `dump_limits()` helper next to `dump_duty()` (after fw_main.cpp:368), reusing the `s_inbox_jb` scratch (same pattern as the BLE status/cfg streamers, avoids a second buffer):
```cpp
static void dump_limits() {
    const auto s = g_node.limits_snapshot();
    meshroute::console::LimitsFields L;
    L.win_ms = s.win_ms; L.win_left_ms = s.win_left_ms; L.n = s.n; L.ch_sf = s.ch_sf;
    L.ch_cap = s.ch_cap; L.ch_used = s.ch_used; L.ch_min_ms = s.ch_min_ms;
    L.ch_next_ms = s.ch_next_ms; L.ch_ceiling = s.ch_ceiling;
    L.dm_min_ms = s.dm_min_ms; L.dm_next_ms = s.dm_next_ms;
    L.duty_ms = s.duty_ms; L.duty_used_ms = s.duty_used_ms;
    const size_t m = meshroute::console::write_limits(s_inbox_jb, sizeof s_inbox_jb, L);
    if (m) mrcon.write(s_inbox_jb, m);   // JSON line to USB (mirrors the other write_* dumps)
}
```
Then register the verb in `service_debug`, right after the `duty` line (fw_main.cpp:1315):
```cpp
    if (len == 6 && !strncmp(line, "limits", 6))   { dump_limits(); return true; }
```
(If `s_inbox_jb` is declared below `dump_limits`'s location at fw_main.cpp:1058, move the helper below that declaration, or forward-use it — it is already file-scope static; place `dump_limits()` after line 1058 alongside the other JSON dumpers rather than at :368 if the compiler flags an undeclared `s_inbox_jb`.)
Also add `limits` to the `rcmd` usage/help strings where the query allow-list is listed (fw_main.cpp:1222 usage text: `...cfg|duty|reboot|...` → add `|limits`) so operators discover it — but limits is a local-only read; it does not need to be in `handle_remote_query`'s allow-list unless remote introspection is wanted (leave out of the remote path to keep it companion-local per the spec).

- [ ] **Step 4: Run, expect PASS** Build all 4 boards (device + ESP32) — the boards this arm touches:
```
pio run -e native && pio run -e xiao_ble && pio run -e xiao_ble_prod && pio run -e esp32
```
(substitute the exact env names from `platformio.ini`'s `[env:*]` sections). Expect: every env links clean (the `limits` verb + `write_limits`/`limits_snapshot` symbols resolve on each backend). Re-run `.pio/build/native/program 2>&1 | grep "Status:"` → still `Status: SUCCESS!` (no native test regressed).

- [ ] **Step 5: Commit** `git add src/fw_main.cpp && git commit -m "Slice 5: the limits verb in both fw_main dispatchers (USB dump_limits + BLE)"`


---

## SLICE 6: Outcome feedback events — `send_blocked`, `send_failed{no_cts,no_ack}`, `channel_sent{relayed}` + contract doc

This slice builds the **outcome-feedback machinery** the companion needs to close the advisory-`limits` loop (spec §"Companion feedback"): when a send is self-gated pre-TX, or gives up after CTS/ACK timeouts, or a channel post fails to relay, the node forwards a **local** push the app treats as stop-and-back-off. Enforcement decisions live in Slices 2 (`do_send_channel` self-GATE) and 3 (the DM own-origin self-throttle); **this slice owns the plumbing they call into**: the new `PushKind` values, the new `SendFailReason` values, the `write_push` JSON shapes, and the two `channel_sent{relayed}` emit sites. Slices 2/3 reference `PushKind::send_blocked` + `Node::emit_send_blocked(...)` by the contract names defined here.

Verified against real code: `command.h` (`PushKind`/`SendFailReason`/`Push`), `console_json.cpp:65` `pushkind_name` / `:80` `sendfailreason_name` / `:131` `write_push`, `node_cascade.cpp` (5 `send_failed` enqueues; the two giveup sites `cascade_to_alt("rts_giveup")` @:323 and `cascade_to_alt("data_ack_giveup")` @:348 thread a `giveup_event` string down through `try_cascade_requeue`), `node_channel.cpp:601` (reoffer-exhaustion bare `rp.active=false; return;`) and `:618` `channel_reoffer_confirm`. `PendingTx.type` is threaded from `TxItem.type` (`node_mac.cpp:493`), and `DATA_TYPE_E2E_ACK=3`/`DATA_TYPE_REMOTE_CMD=6`/`DATA_TYPE_REMOTE_RESP=7` live in `frame_codec.h:416-420`.

---

### Task 1: `send_blocked` PushKind + `next_ms` field + `emit_send_blocked` helper
**Files:** Modify `lib/core/command.h` (`PushKind` enum @:83, `Push` struct @:102) · Modify `lib/console/console_json.cpp` (`pushkind_name` @:65, `write_push` @:131) · Modify `lib/core/node.h` (add `emit_send_blocked` decl near `enqueue_push` @:869) · Create the impl inline in `lib/core/node_channel.cpp` (co-located with the channel self-gate it primarily serves) · Test `test/test_console_json.cpp`.

- [ ] **Step 1: Write the failing test** (append to `test/test_console_json.cpp`)
```cpp
TEST_CASE("write_push — send_blocked carries kind/reason/next_ms (Slice 6a)") {
    char b[160];
    Push c{}; c.kind = PushKind::send_blocked; c.blocked_channel = true;
    c.reason = SendFailReason::min_interval; c.next_ms = 7300;
    size_t n = write_push(b, sizeof b, c);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"send_blocked\",\"kind\":\"channel\",\"reason\":\"min_interval\",\"next_ms\":7300}\n");
    Push d{}; d.kind = PushKind::send_blocked; d.blocked_channel = false;   // DM
    d.reason = SendFailReason::cap; d.next_ms = 0;
    n = write_push(b, sizeof b, d);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"send_blocked\",\"kind\":\"dm\",\"reason\":\"cap\",\"next_ms\":0}\n");
}
```
- [ ] **Step 2: Run, expect FAIL** Run: `pio test -e native` — fails to COMPILE: `PushKind::send_blocked`, `Push::blocked_channel`, `Push::next_ms`, `SendFailReason::min_interval`, `SendFailReason::cap` are undeclared. Read `.pio/build/native/program 2>&1 | grep "Status:"` (build error precedes any Status line).
- [ ] **Step 3: Implement**
  In `lib/core/command.h`, extend the `PushKind` enum (add before the closing `}` of the enum @:96):
```cpp
    send_blocked,  // Slice 6a: this node's OWN cap / min-interval blocked an origination pre-TX
                   //   (kind = channel|dm; reason = cap|min_interval; next_ms = ms until allowed). Companion holds + retries.
```
  Add `SendFailReason` values (@:99, the `SendFailReason` enum — extend the list):
```cpp
enum class SendFailReason : uint8_t { none = 0, no_pubkey, no_identity, too_large, bad_rng, no_route, joining,
                                      cap, min_interval,   // Slice 6a: send_blocked reasons (per-origin cap / burst floor)
                                      no_cts, no_ack };    // Slice 6b: DM giveup reasons (CTS- / ACK-timeout)
```
  Add fields to `Push` (@:102, alongside the other scalars — a bool + a u32):
```cpp
    bool     blocked_channel = false;  // send_blocked: true => "channel", false => "dm"
    uint32_t next_ms = 0;              // send_blocked: ms until the origination is allowed (0 = the floor already passed but cap/duty blocks)
```
  In `lib/console/console_json.cpp`, add to `pushkind_name` (@:65 switch, before `return "unknown"`):
```cpp
        case PushKind::send_blocked:  return "send_blocked";
```
  Add to `sendfailreason_name` (@:80 switch, before `return "none"`):
```cpp
        case SendFailReason::cap:          return "cap";
        case SendFailReason::min_interval: return "min_interval";
        case SendFailReason::no_cts:       return "no_cts";
        case SendFailReason::no_ack:       return "no_ack";
```
  In `write_push` (@:131), add a branch BEFORE the final `else { // send_acked / send_failed }` (i.e. after the `send_e2e_acked` branch @:176-179):
```cpp
    } else if (p.kind == PushKind::send_blocked) {   // Slice 6a: pre-TX self-gate feedback (kind/reason/next_ms)
        j.lit(",\"kind\":\""); j.lit(p.blocked_channel ? "channel" : "dm"); j.ch('"');
        j.lit(",\"reason\":\""); j.lit(sendfailreason_name(p.reason)); j.ch('"');
        j.lit(",\"next_ms\":"); j.u32(p.next_ms);
```
  In `lib/core/node.h`, declare the helper near `enqueue_push` (@:869):
```cpp
    void     emit_send_blocked(bool channel, SendFailReason reason, uint32_t next_ms);   // Slice 6a: the send_blocked push (self-gate)
```
  Define it in `lib/core/node_channel.cpp` (append after `do_send_channel`, ~:301):
```cpp
// Slice 6a: the pre-TX self-gate feedback push. Slice 2 (do_send_channel) + Slice 3 (the DM own-origin
// throttle) call this when THIS node's cap / min-interval blocks an origination, so the companion holds
// + retries after next_ms instead of firing blind. Local-only (node -> its own trusted companion; no OTA).
void Node::emit_send_blocked(bool channel, SendFailReason reason, uint32_t next_ms) {
    Push pu{}; pu.kind = PushKind::send_blocked;
    pu.blocked_channel = channel; pu.reason = reason; pu.next_ms = next_ms;
    enqueue_push(pu);
}
```
- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native` — the new `TEST_CASE` passes; the whole suite stays green. Read `.pio/build/native/program 2>&1 | grep "Status:"`.
- [ ] **Step 5: Commit** `git add lib/core/command.h lib/console/console_json.cpp lib/core/node.h lib/core/node_channel.cpp test/test_console_json.cpp && git commit -m "Slice 6a: send_blocked push (kind/reason/next_ms) + emit_send_blocked helper"`

---

### Task 2: `send_failed{no_cts|no_ack}` — reason-coded DM giveups in the cascade
**Files:** Modify `lib/core/node.h` (add a `giveup_fail_reason` decl) · Modify `lib/core/node_cascade.cpp` (`cascade_to_alt` @:89, `try_cascade_requeue` @:160 — the three terminal `send_failed` enqueues @:139/:170/:188 all carry a `giveup_event` string in scope) · Test `test/test_console_json.cpp`.

The reason is derived from the `giveup_event` string that already flows down: `rts_timeout_fire` passes `"rts_giveup"` (@:323, a CTS-timeout) → `no_cts`; `ack_timeout_fire` passes `"data_ack_giveup"` (@:348, an ACK-timeout) → `no_ack`. The silent-cascade variants (`"rts_silent_cascade"`, `"data_ack_silent_cascade"`) map the same way by prefix.

- [ ] **Step 1: Write the failing test** (append to `test/test_console_json.cpp` — the reason writer already exists after Task 1; this pins the DM-giveup JSON shape the companion reads)
```cpp
TEST_CASE("write_push — send_failed carries no_cts / no_ack DM giveup reasons (Slice 6b)") {
    char b[128];
    Push c{}; c.kind = PushKind::send_failed; c.dst = 2; c.ctr = 7; c.reason = SendFailReason::no_cts;
    size_t n = write_push(b, sizeof b, c);
    CHECK(std::string(b, n) == "{\"ev\":\"send_failed\",\"dst\":2,\"ctr\":7,\"reason\":\"no_cts\"}\n");
    Push a{}; a.kind = PushKind::send_failed; a.dst = 4; a.ctr = 9; a.reason = SendFailReason::no_ack;
    n = write_push(b, sizeof b, a);
    CHECK(std::string(b, n) == "{\"ev\":\"send_failed\",\"dst\":4,\"ctr\":9,\"reason\":\"no_ack\"}\n");
}
```
- [ ] **Step 2: Run, expect FAIL** Run: `pio test -e native` — the two `CHECK`s fail: today's terminal enqueues set no `reason`, so a manually-built push here still passes the writer, but the intended source-of-truth mapping doesn't exist. (Guard: if the writer already renders `no_cts`/`no_ack` from Task 1, this test PASSES the writer shape — its FAIL is proven instead by the integration check in Step 3's note; keep the CHECK-only assertions.) Read `.pio/build/native/program 2>&1 | grep "Status:"`.
- [ ] **Step 3: Implement**
  Declare the mapper in `lib/core/node.h` (near the cascade helpers, e.g. below `cascade_to_alt`'s decl):
```cpp
    static SendFailReason giveup_fail_reason(const char* giveup_event);   // Slice 6b: "rts_*"->no_cts, "data_ack_*"->no_ack, else none
```
  Add its definition at the top of `lib/core/node_cascade.cpp` (after the includes, before `cascade_to_alt` @:89):
```cpp
// Slice 6b: a terminal cascade giveup maps its giveup_event to the DM-failure reason the companion reads.
// The two roots are "rts_giveup"/"rts_silent_cascade" (CTS-timeout) and "data_ack_giveup"/"data_ack_silent_cascade"
// (DATA-ACK-timeout). Prefix-keyed so a new giveup label inherits the right reason. A non-DM/legacy giveup -> none.
SendFailReason Node::giveup_fail_reason(const char* ge) {
    if (!ge) return SendFailReason::none;
    if (ge[0]=='r' && ge[1]=='t' && ge[2]=='s') return SendFailReason::no_cts;                    // "rts_*"
    if (ge[0]=='d' && ge[1]=='a' && ge[2]=='t' && ge[3]=='a' && ge[4]=='_') return SendFailReason::no_ack;  // "data_ack_*"
    return SendFailReason::none;
}
```
  Stamp the reason at the three terminal enqueues. `cascade_to_alt` has `giveup_event` in scope; `try_cascade_requeue` takes it as a param (drop the `[[maybe_unused]]`). Change `node_cascade.cpp:139`:
```cpp
                { Push pu{}; pu.kind = PushKind::send_failed; pu.reason = giveup_fail_reason(giveup_event); pu.dst = pt.dst; pu.ctr = pt.ctr; enqueue_push(pu); }
```
  Change `try_cascade_requeue`'s signature (`node_cascade.cpp:160`) to un-mark the param used, and both terminal enqueues (@:170 and @:188):
```cpp
void Node::try_cascade_requeue(const PendingTx& pt, const char* giveup_event) {
```
```cpp
        { Push pu{}; pu.kind = PushKind::send_failed; pu.reason = giveup_fail_reason(giveup_event); pu.dst = pt.dst; pu.ctr = pt.ctr; enqueue_push(pu); }
```
  (Apply the same one-line change to BOTH @:170 and @:188 — identical text.)
- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native` — the new writer test passes; existing cascade/route tests (`test_node_r3.cpp`, `test_node_channel.cpp`) stay green (the extra `reason` is additive; legacy giveups still pass `none`-mapping strings where applicable). Read `.pio/build/native/program 2>&1 | grep "Status:"`.
- [ ] **Step 5: Commit** `git add lib/core/node.h lib/core/node_cascade.cpp test/test_console_json.cpp && git commit -m "Slice 6b: reason-code DM cascade giveups send_failed{no_cts|no_ack}"`

---

### Task 3: `channel_sent{relayed}` PushKind + writer + emit at reoffer confirm/exhaustion
**Files:** Modify `lib/core/command.h` (`PushKind` @:83, reuse `Push.blocked_channel`? No — add `Push.relayed`) · Modify `lib/console/console_json.cpp` (`pushkind_name` @:65, `write_push` @:131) · Modify `lib/core/node.h` (add `emit_channel_sent` decl) · Modify `lib/core/node_channel.cpp` (define the helper; emit at `channel_reoffer_confirm` @:618 [relayed:true] and the exhaustion branch @:601 [relayed:false]) · Test `test/test_console_json.cpp`.

- [ ] **Step 1: Write the failing test** (append to `test/test_console_json.cpp`)
```cpp
TEST_CASE("write_push — channel_sent carries relayed bool + no_relay reason (Slice 6c)") {
    char b[128];
    Push t{}; t.kind = PushKind::channel_sent; t.relayed = true; t.ctr = 5;
    size_t n = write_push(b, sizeof b, t);
    CHECK(std::string(b, n) == "{\"ev\":\"channel_sent\",\"ctr\":5,\"relayed\":true}\n");
    Push f{}; f.kind = PushKind::channel_sent; f.relayed = false; f.ctr = 6;
    n = write_push(b, sizeof b, f);
    CHECK(std::string(b, n) == "{\"ev\":\"channel_sent\",\"ctr\":6,\"relayed\":false,\"reason\":\"no_relay\"}\n");
}
```
- [ ] **Step 2: Run, expect FAIL** Run: `pio test -e native` — fails to compile: `PushKind::channel_sent` and `Push::relayed` are undeclared. Read `.pio/build/native/program 2>&1 | grep "Status:"`.
- [ ] **Step 3: Implement**
  In `lib/core/command.h`, add the `PushKind` value (after `send_blocked` from Task 1):
```cpp
    channel_sent,  // Slice 6c: outcome of an OWN channel post's origin re-offer. relayed=true (a relay was overheard =
                   //   channel_reoffer_confirm) or relayed=false (the re-offer exhausted with no relay -> reason "no_relay").
```
  Add the `Push` field (@:102, next to `blocked_channel`):
```cpp
    bool     relayed = false;          // channel_sent: a relay of our channel post was overheard (true) or the re-offer exhausted (false)
```
  In `console_json.cpp` `pushkind_name` (@:65 switch):
```cpp
        case PushKind::channel_sent:  return "channel_sent";
```
  In `write_push` (@:131), add a branch after the `send_blocked` branch (Task 1):
```cpp
    } else if (p.kind == PushKind::channel_sent) {   // Slice 6c: origin re-offer outcome (relayed?)
        j.lit(",\"ctr\":"); j.u32(p.ctr);
        j.lit(",\"relayed\":"); j.lit(p.relayed ? "true" : "false");
        if (!p.relayed) j.lit(",\"reason\":\"no_relay\"");   // 1st-hop throttle or no neighbour
```
  In `lib/core/node.h`, declare the helper (near `emit_send_blocked` from Task 1):
```cpp
    void     emit_channel_sent(bool relayed, uint16_t ctr);   // Slice 6c: OWN channel post re-offer outcome
```
  Define it in `lib/core/node_channel.cpp` (after `emit_send_blocked`):
```cpp
// Slice 6c: the OWN-channel-post outcome push. relayed=true when a relay of our post is overheard
// (channel_reoffer_confirm); relayed=false when the origin re-offer exhausts all retries with no relay
// (channel_reoffer_fire's give-up branch) -> the companion backs off. Local-only (node -> its companion).
void Node::emit_channel_sent(bool relayed, uint16_t ctr) {
    Push pu{}; pu.kind = PushKind::channel_sent; pu.relayed = relayed; pu.ctr = ctr;
    enqueue_push(pu);
}
```
  Emit `relayed:true` in `channel_reoffer_confirm` (`node_channel.cpp:618`) — the `rp.id` is the channel_msg_id, and the origin-ctr is its low byte (`id & 0xff`, per `channel_msg_id_mint` @:262), so pass that as `ctr`:
```cpp
        if (rp.active && rp.id == id) { emit_channel_sent(true, static_cast<uint16_t>(id & 0xff)); rp.active = false; _hal.cancel(kChannelReofferTimerId + s); return; }
```
  Emit `relayed:false` at the exhaustion branch in `channel_reoffer_fire` (`node_channel.cpp:601`, currently the bare `if (rp.retries_left == 0 || max_data_sf() == 0) { rp.active = false; return; }`) — only on true exhaustion of a live entry (the `i < 0` evicted-entry path @:599 stays silent):
```cpp
    if (rp.retries_left == 0 || max_data_sf() == 0) { emit_channel_sent(false, static_cast<uint16_t>(e.id & 0xff)); rp.active = false; return; } // exhausted (or data-incapable) -> give up; repair digest is the last resort
```
- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native` — the new writer test passes; `test_node_channel.cpp` stays green (the emit is additive on the confirm/exhaustion paths; drop-oldest ring absorbs the extra push). Read `.pio/build/native/program 2>&1 | grep "Status:"`.
- [ ] **Step 5: Commit** `git add lib/core/command.h lib/console/console_json.cpp lib/core/node.h lib/core/node_channel.cpp test/test_console_json.cpp && git commit -m "Slice 6c: channel_sent{relayed} at reoffer confirm + exhaustion"`

---

### Task 4: Integration — `send_blocked{next_ms}` fires end-to-end from a self-gated channel post; `channel_sent{relayed:false}` on reoffer exhaustion
**Files:** Test `test/test_node_channel.cpp` (drives a real `Node` through the reoffer timer + drains `next_push`) · no production change (verifies the Slice 2/3 call sites reach the Slice 6 machinery).

This task proves the machinery is reachable through a real `Node`, not just the writer. It exercises `emit_channel_sent(false, …)` via the reoffer-exhaustion timer path (owned by this slice's edit @:601) and — as a lighter check — `emit_send_blocked` directly (the Slice 2 gate that calls it lands in Slice 2; here we assert THIS slice's helper enqueues a drainable push with the right shape).

- [ ] **Step 1: Write the failing test** (append to `test/test_node_channel.cpp`; reuse the file's existing `Node`/`TestHal` fixture — mirror an existing `TEST_CASE` there for construction + `advance`/timer-pump helpers)
```cpp
TEST_CASE("Slice 6: emit_send_blocked + emit_channel_sent enqueue drainable pushes") {
    TestHal hal; Node n(hal);
    node_bring_up_channel_capable(n, hal, /*node_id*/ 20);   // existing helper in this file: provisions + a data SF

    // 6a: the self-gate helper enqueues a send_blocked the companion can drain.
    n.emit_send_blocked(/*channel=*/true, SendFailReason::min_interval, /*next_ms=*/7300);
    Push p{};
    CHECK(n.next_push(p));
    CHECK(p.kind == PushKind::send_blocked);
    CHECK(p.blocked_channel == true);
    CHECK(p.reason == SendFailReason::min_interval);
    CHECK(p.next_ms == 7300);

    // 6c: origin re-offer EXHAUSTION -> channel_sent{relayed:false}. Post, never overhear a relay,
    // let all retries fire, then drain the pushes and confirm the terminal relayed:false appeared.
    (void)n.do_send_channel(/*channel_id*/ 1, reinterpret_cast<const uint8_t*>("hi"), 2);
    for (int i = 0; i < (protocol::channel_reoffer_max_retries + 2); ++i)
        pump_timers_for(hal, n, protocol::channel_reoffer_delay_ms + protocol::channel_reoffer_jitter_ms + 1);  // existing pump helper
    bool saw_no_relay = false;
    while (n.next_push(p))
        if (p.kind == PushKind::channel_sent && !p.relayed) saw_no_relay = true;
    CHECK(saw_no_relay);
}
```
- [ ] **Step 2: Run, expect FAIL** Run: `pio test -e native` — fails: before Tasks 1+3 land the symbols don't compile; after them, if the @:601 emit were omitted, `saw_no_relay` stays false. (If `node_bring_up_channel_capable`/`pump_timers_for` differ in name, adapt to the fixture helpers actually present at the top of `test_node_channel.cpp` — verify by reading the file's existing cases, do NOT invent.) Read `.pio/build/native/program 2>&1 | grep "Status:"`.
- [ ] **Step 3: Implement** No production code — Tasks 1 + 3 already added `emit_send_blocked`, `emit_channel_sent`, and the @:601 exhaustion emit. If the test surfaced a gap (e.g. the exhaustion branch didn't fire because `max_data_sf()==0` short-circuited first), fix by ensuring the fixture provisions a data SF so exhaustion is reached via `retries_left==0`, matching the real device path.
- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native` — the integration case passes; full suite green. Read `.pio/build/native/program 2>&1 | grep "Status:"`.
- [ ] **Step 5: Commit** `git add test/test_node_channel.cpp && git commit -m "Slice 6: integration — send_blocked + channel_sent{relayed:false} reachable through Node"`

---

### Task 5: Document the outcome events in `INBOX_SYNC_CONTRACT.md`
**Files:** Modify `ios-companion/INBOX_SYNC_CONTRACT.md` (the "UX pushes (node → app)" subsection @:223-233, where `send_failed` reasons are already documented; the top-level "Pushes" block @:51-57 for the event exemplars).

No test (doc-only). Mirror the companion-contract-gap pattern already in the file (the `send_failed{no_pubkey}` + `peer_key_cached` entries @:225-228).

- [ ] **Step 1: Write the failing test** N/A — documentation. The "failing" state is the absence of the four new events from the contract; the acceptance is their presence + the reason vocabulary extension.
- [ ] **Step 2: Run, expect FAIL** N/A (doc). Sanity: `grep -c "send_blocked\|channel_sent\|no_cts\|no_ack" ios-companion/INBOX_SYNC_CONTRACT.md` returns `0` before the edit.
- [ ] **Step 3: Implement** In the "### UX pushes (node → app)" block (@:223, alongside the existing `send_failed`/`peer_key_cached` exemplars @:225-226), add the anti-spam-v2 feedback events + extend the `send_failed.reason` vocabulary line @:228:
```markdown
### Anti-spam v2 feedback — advisory `limits` + actual send-outcome (2026-06-30)

`limits` (see §Limits query) lets the app *predict* + pace; these three pushes report the *actual* outcome so it backs off. All are **local** (node → its own trusted companion; no OTA change — the node infers from what it already observes):

```json
{"ev":"send_blocked","kind":"channel","reason":"min_interval","next_ms":7300}  // THIS node's own cap/floor blocked the origination pre-TX — hold + retry after next_ms
{"ev":"send_blocked","kind":"dm","reason":"cap","next_ms":0}                    // kind ∈ channel|dm ; reason ∈ cap|min_interval ; next_ms = ms until allowed (0 = floor passed, cap/duty blocks)
{"ev":"send_failed","dst":2,"ctr":7,"reason":"no_cts"}                          // a DM gave up after CTS-timeout retries (1st-hop backstop-drop / no route surfaces here too)
{"ev":"send_failed","dst":4,"ctr":9,"reason":"no_ack"}                          // a DM gave up after DATA-ACK-timeout retries
{"ev":"channel_sent","ctr":5,"relayed":true}                                    // an OWN channel post: a relay was overheard (origin re-offer confirmed) = success
{"ev":"channel_sent","ctr":6,"relayed":false,"reason":"no_relay"}              // the re-offer exhausted with no relay (1st-hop throttle or no neighbour)
```

- The app treats **`send_blocked` / `send_failed` / `channel_sent{relayed:false}`** as **stop-and-back-off** (don't keep firing) and **`e2e_acked` / `channel_sent{relayed:true}`** as success.
- **Enforcement is the 1st hop's** (it applies its own per-origin cap with its own `N`) **plus this node's self-gate**, so a send can still be rejected *after* the companion thought `limits` allowed it — hence the actual outcome, not just the advisory prediction.
- `send_failed.reason` for a DM giveup ∈ `no_cts · no_ack` (this node's cascade exhausted CTS-/ACK-timeout retries). The 1st-hop's *silent* backstop drop surfaces as `no_cts` (conflated with no-route — the app's reaction, back-off-and-retry, is identical). The OTA silent-drop is KEPT (an explicit reject frame would cost airtime + help a spammer calibrate).
```
  Extend the existing `send_failed.reason` vocabulary line @:228 (append the new reasons):
```markdown
- `send_failed.reason` ∈ `no_pubkey · no_identity · too_large · bad_rng · no_route · joining · no_cts · no_ack`. App maps `no_pubkey`
```
  Add the three event exemplars to the top-level "## Pushes" block @:51-57 (so the event catalogue is complete) by inserting after the `inbox_end` line @:57:
```json
{"ev":"send_blocked","kind":"channel","reason":"min_interval","next_ms":7300}   // anti-spam v2: pre-TX self-gate (see §Anti-spam v2 feedback)
{"ev":"channel_sent","ctr":5,"relayed":true}                                     // OWN channel post relayed (origin re-offer confirmed)
```
- [ ] **Step 4: Run, expect PASS** Verify presence: `grep -c "send_blocked\|channel_sent\|no_cts\|no_ack" ios-companion/INBOX_SYNC_CONTRACT.md` returns `> 0` and the JSON exemplars match the `write_push` shapes pinned in Tasks 1-3 (event names, field order, `no_relay` reason). No native test — doc-only.
- [ ] **Step 5: Commit** `git add ios-companion/INBOX_SYNC_CONTRACT.md && git commit -m "Slice 6d: document send_blocked/send_failed{no_cts,no_ack}/channel_sent in the companion contract"`
