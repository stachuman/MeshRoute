<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 7a — the persisted activation-delay configuration and its source-derived bounds · brief · 2026-09-07

**Status: REVISION 2 (2026-09-07) — the coder's preflight STOP-1 (`docs/superpowers/evidence/2026-09-07-radmin-slice7a.md`,
B353–B358) is FOLDED IN (§0.1). DISPATCHABLE at the owner's commit that ADDS this file (rev 1 was left untracked by
`dd593a2`, which committed the register only): the coder's preflight records `git rev-parse HEAD` (its parent chain
must contain `eb6d46b`, "Slice 6"), this file's SHA-256, and an EMPTY status in both repositories. ⛔ No self-referential
hash pin — the binding is by content + clean status.**
Roles: the Quality Agent (Claude) authors, gates independently and lands documentation; **the coder is Codex**; the
owner rules and commits. The coder VALIDATES every `file:line` below against the source before editing and reports
any disagreement as a STOP-1 preflight report (the Slice 6 pattern, which caught six real gaps — keep doing it).
The pre-check and the brief are ONE document under the new roles: §3 is the verified ledger.

Authority: design `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md` §13 (:1370-1482, the
activation contract), §19 item 7a (:2037-2040), §19.1 row 7a (:2097); rulings R-RA-9, **R-RA-16**, **R-RA-20**,
**R-RA-23** in `docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md`; the Slice 0e characterization
`test/test_radmin_characterization_0e.cpp` (0e-D, the KAT INPUT — "Slice 7a owns `remote_scheduled_reply_path_budget_ms(cfg)`").

## 0. What Slice 7a is, in one line

Add the persisted target `cfg.remote_action_activation_ms` (NV v25; `0` = the derived default), the production
`remote_scheduled_reply_path_budget_ms` authority with its floor / default / ceiling, the `cfg set` key that refuses
an out-of-bounds or impossible value, the `cfg` text + JSON read-out, and the boot restore — **and change no action
timing**: nothing consumes the value yet (7b does). Two commits: **7a-0**, a pure refactor that gives the MAC's two
wait-window formulas one named home so the budget cannot fork them; **7a**, the feature.

## 0.1 The coder's preflight findings, verified by QA against source and folded in (revision 2)

| finding | verified fact | where the brief changed |
| --- | --- | --- |
| **B353** — the append consumes tail padding; v24 is NOT rejected by size | `mrnv::Blob` is 280 B / align 8 on native, ARM and Xtensa; `team_key_team_id` @268, `team_key_active` @272; a `uint32_t` append lands @276 inside the seven padding bytes ⇒ `sizeof` stays **280**, so `slot_size_ok` accepts a v24 record and `load()`'s range (2..kVersion) would read the old padding as the new field | §4.3: `load()` REJECTS `version < 25` explicitly (the range floor moves 2 → 25 with its derivation; the RANGE call stays for a future deliberate widening); the size pin stays 280 with the new `offsetof` 276 pinned; **RAM prediction +4 only** (the global; `static Blob cur` does not grow); §6 the v24-same-size rejection is an executed native case |
| **B354** — the key buffer is 19 chars; the new key is 27; `gw_announce_interval` (20) has been UNREACHABLE since v11 | `src/firmware_config.cpp:251` `char key[20]` truncates the token, so `cfg set gw_announce_interval …` answers `unknown_key gw_announce_interva` (`:396` never matches) — a pre-existing latent defect the preflight found | §4.5: the buffer is sized from the longest key literal by a named constant + a generator-backed test that the buffer exceeds every inventory `cfg set` sub-verb; the newly reachable `gw_announce_interval` arm gets a structural row + a Part 57a bench line; B354 registered as a bug fixed in-slice |
| **B355** — `max_data_sf()` is private and `node.h` was out of fence | `lib/core/node.h:2322` declares it in the private section; `src/` cannot call it | §4.4/§5: `node.h` enters the fence for ONE declaration move (`max_data_sf()` becomes public; no member, no ABI change; ⛔ no logic duplicated in `src/`) |
| **B356** — the inbox probe compiles `firmware_commands.cpp`, which will reference the new global | `tools/probe_inbox_verbs/probe_main.cpp:241-242` defines the `g_ble_*` fakes for exactly this reason | §5: the probe's fake definition of `g_remote_action_activation_ms` enters the fence |
| **B357** — the union omitted `sliceDack` and `b134ack`; the inventory prediction was undated | both are `lib/console` batteries in the map | §7: added; **203 after 7a-0, 204 after 7a** |
| **B358** — §4.7's rationale overstated operator privilege and paraphrased the proposal's rule | `regen`, `factory_reset`, `ota`, `crashtest` are OWNER-class disruptive rows | §4.7 rewritten; the rule quoted verbatim |

The six are registered (B353–B358; B354 as a pre-existing defect fixed in-slice; the coder's proposals start at
**B359**). The preflight report stays at the head of the evidence file; the implementation sections are appended.

## 1. Closure bindings — the Slice 6 QA PASS pins (QA's independent gate, brief §10 of Slice 6)

**MeshRoute base: the owner's commit of this brief on `main`, on top of `eb6d46b` ("Slice 6", clean).**
**Simulator base: `06746a97de5764415d6fcef10b97bca90569b9c7` ("Slice 5"), clean.** 7a-0 rebuilds `lus` (a `lib/core`
edit) and predicts **36/36 byte-identical streams**; 7a rebuilds it again (`lib/console/console_json.cpp` is compiled
by the simulator) with the same prediction. ⛔ Any stream delta is a STOP.

| Required binding | Slice 6 closure entry |
| --- | --- |
| Native | **2839 / 121831 / 0** (`tools/probe_ui_model_mutations.py` `PIN_CASES, PIN_ASSERTS = 2839, 121831`) |
| Corpus | 36/36, anchors 36/36, s18 `32afbf11` / 269517 / 0 (`simulation/BASELINE.md` is the authority; NEVER edited) |
| `lus` | md5 `db6582a171a6d785720e324c2cfe43f1` |
| Node ABI | `meshroute::Node` native 224136/8 · heltec_mobile 117912/8 · gateway 150504/8 — **UNMOVED** by 7a (no `Node` member, no `NodeConfig` field) |
| Console sink | `profiles=6 checks=720 structural=62 ble_guard=905 ownership=6 ownership_controls=3 controls=114 unusable_controls=0` |
| Inbox verbs | ACCEPT 362 / 39 · CLIENT 360 / 42 |
| Firmware UI · custody · BLE line · features | 223 · 27/10 · 40/8 · 9/120/59 + ownership 40 (nine-file census) |
| Tools sweep · inventory · authority checker | 342 OK · **203 rows** · PASS + 6/6 selftests |
| Warning census | 173 / 178 / 177 / 177 / 182 / 182, `-Wswitch` 0 |
| Boards | gateway 197180 / 550204 / 285 · heltec_mobile 207748 / 1371260 / 329 |
| Register | next free **B359** (B352 = this brief's own finding, §3; B353–B358 = the preflight fold-ins, §0.1) |

## 2. Verbatim authority pins

R-RA-16 — **Owner:** *"yes, we will accept worst but I'd propose to make it configurable so user can tune"*.
*Settled:* `remote_action_activation_delay_ms` defaults to a DERIVED worst-case value … a target-side `cfg` tunable
with a named minimum and maximum … *"Adding the cfg key is an NV field (own attribution, C4 idiom). The 300 s outer
bound (`e2e_ack_deadline_xl_ms`) is unchanged."*

R-RA-20 — **Owner:** *"agree - we go with 2x."*

> budget(cfg) = airtime(RTS + CTS + terminal DATA + ACK at cfg PHY) + cts_to_data_gap_ms
>             + rts_max_retries * rts_busy_retry_ms + the MAC's CTS-wait and ACK-wait windows + cascade_requeue_base_ms
> floor = budget(cfg); default = 2 * budget(cfg); ceiling = e2e_ack_deadline_xl_ms - 1 (299 999 ms)

R-RA-23: *"the CTS-wait term … is the sum of the wait windows for attempts 0, 1 and 2, not merely the largest
final-attempt window … **7,006/14,012 ms** … mutating away any one attempt window, counting only attempt 2, omitting
a slop occurrence, or hard-coding the reference value is RED. The floor remains one budget, the default exactly twice
it, and the ceiling 299,999 ms; an impossible configured interval refuses rather than clamps."*

Design §13: *"target `cfg.remote_action_activation_ms` is runtime-configurable only inside
`[remote_action_activation_min_ms(cfg), remote_action_activation_max_ms]`. Adding and persisting this field is its
own attributable NV/schema change before the behaviour consumes it."* … *"A target configuration whose derived
floor/default cannot fit this interval must refuse disruptive remote scheduling loudly; it may not clamp an impossible
promise."* … *"Codec/config/scheduler tests pin both sides of the floor and ceiling, prove the default changes with its
source timing inputs, refuse an impossible interval, and distinguish the legacy 3-second behaviour from v2."*

Design §19 item 7a: *"add only the persisted target `cfg.remote_action_activation_ms`, its source-derived
default/floor/ceiling validation, schema migration, refusal of an impossible interval, and boundary controls. This is
the R-RA-16 NV change; it changes no action timing yet."* §19.1 row 7a: *"zero remote events, 36/36 unchanged; ruled
pair with isolated NV attribution | **Part 57a:** cfg migration/reboot; exact `cfg remote_action_activation_ms=<N>`
value persists and invalid bounds refuse"*.

## 3. Verified source state at `eb6d46b` (V1) and corrections (V2)

| Source verified | Consequence |
| --- | --- |
| `src/device_nv.h:38-135` `mrnv::Blob` (`/mrcfg`), `kVersion = 24` (`:135`), last fields `team_key_team_id` / `team_key_active` (v24); `load()` `:1419-1421` = `blob_valid_range(v_min 2, v_max kVersion)` over `slot_size_ok(n, sizeof(Blob))` (`:709-711`) — an EXACT size check | **B353 correction:** the record is 280 B / align 8 with SEVEN tail-padding bytes after `team_key_active` @272, so the appended `uint32_t` lands @276 and `sizeof` does NOT move — the size check cannot reject a v24 record. **V2 of the design's word "schema migration": the repository's ruled policy is REPROVISION-ON-REFLASH (stated at v22, v23, v24). 7a follows it by an EXPLICIT version floor: `load()` rejects `version < 25` (§4.3), the node re-seeds from live defaults (`nv_load_stamped`, `src/firmware_config.cpp:94-97`), no migration arm.** Part 57a's "migration/reboot" = the reprovision after reflash plus persistence across reboot |
| `test/test_device_nv.cpp:64` "v2..kVersion load, v1 and kVersion+1 are rejected"; `:497-530` pins `kVersion == 24`; sizes are asserted for the sibling records | the range case is REWRITTEN as its new obligation (v25 loads; v24 — same size — and v26 are rejected; the old range kept visible), `kVersion == 25`, `sizeof(Blob) == 280` and `offsetof(remote_action_activation_ms) == 276` pinned beside the siblings |
| **B354:** `src/firmware_config.cpp:251` `char key[20]` — a 19-char token cap; `gw_announce_interval` (20) at `:396` has been unreachable since v11 (`> cfg err unknown_key gw_announce_interva`), and the 27-char new key would be too | §4.5: the buffer is derived from the longest key; the latent arm becomes reachable and is covered (structural row + bench line); B354 registered |
| `src/firmware_config.cpp:250-612` `handle_cfg_set`: per-key `else if (!strcmp(key, …))` arms, `live`/`persist` flags (`:291`), refusal idiom `> cfg err bad_value (<key> <range>)` (e.g. `:500`, `:505`), unknown key `:586`, persist + `mr_ui_on_config_saved()` `:607`, the trailing `ok (reboot to apply)` / `ok (live + saved)` (`:610-611`); `seed_blob_from_live` (`:82`, defined below) is the ONE record-seeding path (U2); `handle_gateway`'s load-failure seed `:690-694` is a deliberate subset | the new arm follows the idiom; the new field is seeded in `seed_blob_from_live` (0 = default); the gateway subset stays a subset (it seeds nothing for this field — 0 is the default) |
| `src/fw_main.cpp:820-845` the boot restore `nv → cfg / globals` (`g_ble_mode` … `:832-833`; `if (nv.x != 0) cfg.x = nv.x` for the "0 = default" fields); `src/fw_context.h:99-101` the `extern` globals | the persisted raw value restores into a new firmware global; the EFFECTIVE value is computed on demand (§4.4), never latched |
| `src/firmware_commands.cpp:761-763` the `cfg` text dump's `ble` line; `:1722-1740` `make_cfg_extras`; `lib/console/console_json.h:215-230` `CfgExtras` + `write_cfg`; `console_json.cpp:900-902` the JSON twins | the text line and the JSON twin are added in the same idiom; **`lib/console/console_json.cpp` IS compiled by the simulator** (`CMakeLists.txt:122-136`) ⇒ `lus` changes in 7a; streams must stay identical |
| `lib/core/node_mac.cpp:2417-2462` `Node::start_rts_timeout`: `base = airtime_routing_ms(unicast_rts_wire_len(c)) + airtime_routing_ms(terminal_cts_wire_len(c))`, `shift = min(attempt, 2)`, `delay = (base << shift) + 2*slop + 1` with `slop = _hal.rx_window_slop_ms(_cfg.routing_sf)`; `:2463-…` `start_ack_timeout`: `base = airtime_ms(data_sf, …, 18 + inner_len) + airtime_routing_ms(3) + slop(data_sf) + slop(routing_sf)`, armed at `base + 2` | the two window formulas are inline arithmetic on flight state. **7a-0 extracts them into two pure helpers the MAC calls with the same operands** (§4.1), so the production budget reuses them (U1) instead of forking them as the 0e test had to |
| `test/test_radmin_characterization_0e.cpp:216-300, :560-636` 0e-D `compute_budget(attempt = rts_max_retries)` uses ONE CTS-wait window (attempt 2 = 665 ms); QA ran it (`MR_RADMIN0E_TABLE=1`): total **6506 ms**, default **13012** — the interpretation R-RA-23 WITHDREW (attempts 0/1/2 = 167/333/665 ⇒ sum 1165 ⇒ **7006 / 14012**) | **B352 (registered):** the 0e KAT fixture is at the withdrawn figure. 7a updates it to the ruled sum with the old number kept visible, and binds the production authority to 7006/14012 at the characterized PHY with slop 0 |
| `lib/core/airtime.h:37` `airtime_ms(sf, bw_hz, cr, preamble_sym, len)`; `lib/core/node.h:2322` `max_data_sf()` (**PRIVATE — B355**), `:2945` `airtime_routing_ms(len)`; `IHal::rx_window_slop_ms(sf)` (a HAL method, 0 on host/sim, ≈53 ms per turnaround on metal) | the budget takes its PHY and slop as explicit inputs; the firmware supplies the live ones, the tests the characterized ones |
| `lib/core/protocol_constants.h:133` `cts_to_data_gap_ms` 5, `:134-135` `rts_busy_retry_ms` 30 / `rts_max_retries` 2, `:273` `cascade_requeue_base_ms` 5000, `:780` `e2e_ack_deadline_xl_ms` 300000 | every constant term is read from these names; ⛔ no literal |
| `tools/probe_inbox_verbs/run.sh:93-94, :258-274` compiles `firmware_commands.cpp` + `firmware_inbox.cpp` + all `lib/core` + `lib/console`, ⛔ NOT `src/firmware_config.cpp` nor `fw_main.cpp`; `probe_main.cpp:241-242` FAKES the `g_ble_*` globals (**B356**: the new global needs the same fake); native compiles no `src/*.cpp` (§B115) | `handle_cfg_set`'s new arm cannot be EXECUTED by any host instrument: the validation is a PURE header (§4.4) executed natively; the arm's wiring is proven structurally by the console-sink probe (which already parses `firmware_config.cpp`, `structural.py:122` `config_cpp_path`) plus bench Part 57a |
| `tools/gen_command_inventory.py` `handle_cfg_set` is a pinned `sub` surface (`:153`); a new `!strcmp(key, …)` arm becomes a `cfg set | <key>` row; the ruled table + header + checker must carry it or the checker REFUSES | inventory **203 → 204**; one new table/header row (§4.7) |
| `tools/probe_board_abi.py` PIN_TABLE has NO `mrnv::Blob` row (the NV record is size-pinned by `test_device_nv.cpp` only) | no ABI probe re-pin; the +4 is attributed through the test pin and the board RAM |

## 4. Author decisions

### 4.1 Step 7a-0 — ONE home for the MAC's wait-window arithmetic (pure refactor, own commit)

New header-only `lib/core/mac_wait_windows.h` (`namespace MESHROUTE_NS`, no HAL, no Node):
```cpp
inline constexpr uint32_t cts_wait_delay_ms(uint32_t base_air_ms, uint8_t attempt, uint32_t slop_ms)
    { const uint32_t shift = attempt < 2 ? attempt : 2; return (base_air_ms << shift) + 2u * slop_ms + 1u; }
inline constexpr uint32_t ack_wait_delay_ms(uint32_t data_air_ms, uint32_t ack_air_ms, uint32_t slop_data_ms, uint32_t slop_routing_ms)
    { return data_air_ms + ack_air_ms + slop_data_ms + slop_routing_ms + 2u; }
```
`start_rts_timeout` and `start_ack_timeout` compute their operands exactly as today and call these; ⛔ no operand,
order or rounding changes (the M-broadcast arm and every comment stay). Proof: **36/36 byte-identical streams**
after the `lus` rebuild (the MAC arms the same delays), native unchanged, a native case asserting the helpers
reproduce the 0e attempt windows 167/333/665 and the ACK window 311 at the characterized PHY, and the existing
`node_mac.cpp` batteries + a new `macwait` battery (the shift cap, the `+1`, the `+2`, each slop occurrence) all RED.
This commit changes NO behaviour and carries NO feature; it is separately attributable (C1).

### 4.2 The production budget authority — `lib/core/remote_activation.h` (header-only, pure)

```cpp
struct ActivationBudgetInputs {   // explicit, so the same function serves the live radio and the KAT
    uint8_t routing_sf; uint8_t data_sf; uint32_t bw_hz; uint8_t cr; uint16_t preamble_sym;
    uint32_t slop_routing_ms; uint32_t slop_data_ms; uint16_t terminal_inner_len; bool crypted_flight;   // false: §8.11 outer RPC DATA is plaintext
};
uint32_t remote_scheduled_reply_path_budget_ms(const ActivationBudgetInputs&);   // R-RA-20 + R-RA-23, term by term
inline constexpr uint32_t remote_action_activation_max_ms = protocol::e2e_ack_deadline_xl_ms - 1;   // 299 999
uint32_t remote_action_activation_min_ms(const ActivationBudgetInputs&);        // = budget
uint32_t remote_action_activation_default_ms(const ActivationBudgetInputs&);    // = 2 * budget
```
* Terms, each from its named authority: RTS/CTS/ACK airtime at `routing_sf` (`unicast_rts_wire_len(crypted)`, 3, 3);
  the terminal DATA at `data_sf` = the SLOWEST admitted data SF (`max_data_sf()` — the worst case is the floor's
  purpose) with the frame length from `data_frame_len(SOURCE_HASH|DST_HASH|CROSS_LAYER, remote type, terminal_inner_len)`
  where `terminal_inner_len` is 0e-C's largest authenticated TERMINAL{scheduled} inner (the coder derives it from the
  same `kCarriers` logic as 0e, ⛔ not typed); `cts_to_data_gap_ms`; `rts_max_retries * rts_busy_retry_ms`;
  **`cts_wait_delay_ms` summed over attempts 0, 1, 2** (R-RA-23) with `slop_routing_ms` at every occurrence;
  `ack_wait_delay_ms` (`18 + inner`, the MAC's own length model, kept as 0e kept it) with both slops;
  `cascade_requeue_base_ms`. Overflow-checked (saturating) — a PHY that overflows is "impossible", never wrapped.
* **The reference KAT** (`test/test_remote_activation.cpp`): at SF8 / 125 kHz / CR5 / slop 0 the budget is **7006**
  and the default **14012**, the ceiling 299999; the withdrawn 6506/13012 is asserted NOT equal; every R-RA-23
  control (drop attempt 0, drop attempt 1, count only attempt 2, drop one slop occurrence, hard-code 7006) is a
  mutation entry that turns RED; the default is exactly 2× the floor for three different PHYs; every input moves the
  result in the right direction (0e's idiom); the `impossible` case is a real PHY the coder FINDS by search over
  legal (sf, bw, cr) tuples where `2 * budget > ceiling` — reported with its inputs, not invented.
* The header is included by the simulator's `lib/core` compile only if a `.cpp` needs it — it is header-only and
  `node_mac.cpp` does NOT include it, so 7a's `lus` movement is `console_json.cpp`'s alone.

### 4.3 The NV field — `mrnv::Blob` v25

`uint32_t remote_action_activation_ms;   // v25: R-RA-16 — 0 = use the derived default at read time` appended
AFTER `team_key_active`. **B353: it lands at offset 276 inside the record's tail padding, so `sizeof(Blob)` STAYS 280
on all three ABIs and the size check is blind to the bump.** Therefore `load()` (`src/device_nv.h:1419-1421`) takes
`v_min = kVersionMinLoad = 25` — an explicit, derived floor with its comment stating exactly this (the RANGE call is
kept so a future same-layout bump can widen it deliberately); a v24 record is REJECTED and the node re-seeds
(the documented reprovision-on-reflash). The coder measures `sizeof` and `offsetof` on native and both boards and
pins both in `test_device_nv.cpp` beside `kVersion == 25`; the `kVersion` comment gains its v25 entry with the
REPROVISION-ON-REFLASH warning in the established wording, plus "the field consumed padding — the version floor,
not the size, is the guard". ⛔ NOT a `NodeConfig` field (Node ABI
unmoved; 7b may promote it with its own re-pin when core consumes it). `seed_blob_from_live` seeds it from the live
global; `handle_gateway`'s subset seed leaves it 0 (= default) and says so.

### 4.4 The effective-value policy — `src/firmware_remote_activation.h` (pure header)

```cpp
enum class ActivationState : uint8_t { default_derived, configured, below_floor, above_ceiling, impossible_phy };
struct ActivationResolved { uint32_t effective_ms; uint32_t floor_ms; uint32_t default_ms; ActivationState state; };
ActivationResolved remote_activation_resolve(uint32_t persisted_ms, const ActivationBudgetInputs&);
const char* activation_state_name(ActivationState);
```
* `persisted == 0` → `default_derived` with `effective = default`; a persisted value inside `[floor, ceiling]` →
  `configured`; outside → `below_floor` / `above_ceiling` with `effective = 0` (⛔ never clamped — the PHY may have
  changed since the value was set, and an out-of-bounds promise is refused, not rounded); `default > ceiling` →
  `impossible_phy`, `effective = 0`. 7b refuses disruptive scheduling for every non-usable state.
* Computed ON DEMAND from `(g_remote_action_activation_ms, live PHY inputs)` — ⛔ no second resident copy, no latch,
  so a later `cfg set sf_list` cannot leave a stale effective value. The firmware's one binding
  (`firmware_commands.cpp`) builds the inputs from `g_node.config()` / `max_data_sf()` / `g_hal.rx_window_slop_ms`.
  **B355:** `Node::max_data_sf()` is private (`node.h:2322`); AUTHORIZED: move that ONE declaration to the public
  section (no new member, no logic change, Node ABI unmoved) — ⛔ never re-derive the SF from the bitmap in `src/`.

### 4.5 The `cfg set remote_action_activation_ms <N|0>` arm

**B354 first:** `handle_cfg_set`'s `char key[20]` (`:251`) cannot hold this 27-char key — nor the existing 20-char
`gw_announce_interval`, whose arm (`:396`) has been unreachable since v11. The buffer becomes
`char key[kCfgKeyMaxBytes]` with `kCfgKeyMaxBytes` a named constant derived from the longest key literal
(`sizeof("remote_action_activation_ms")`), and a tools unit test (in `test_gen_command_inventory.py`, which already
enumerates every `cfg set` sub-verb) asserts the constant exceeds the longest inventory key — so a future longer key
cannot silently truncate again. The newly reachable `gw_announce_interval` arm changes NO code; it is covered by a
console-sink structural row (the arm exists and assigns both the live and persisted fields) and a bench line in
Part 57a; its previous unreachability is B354's record.
Validates through `remote_activation_resolve(N, live inputs)`: `0` and any `configured` result persist (`live = true`
— nothing consumes it, so no reboot is required; the trailing line is `ok (live + saved)`); `below_floor` /
`above_ceiling` refuse with `> cfg err bad_value (remote_action_activation_ms <floor>..299999 ms or 0=default)` where
`<floor>` is the LIVE floor; `impossible_phy` refuses with `> cfg err impossible_activation (floor <f> default <d>
exceed 299999 ms at this PHY)`. ⛔ Nothing else in the handler moves.

### 4.6 The read-out (text + JSON twin, the `ble_period` idiom)

Text `cfg` dump gains one line: `  radmin: activation_ms=<effective> state=<name> cfg=<persisted> floor=<f> default=<d> ceiling=299999`.
JSON `cfg` gains `remote_action_activation_ms` (effective, 0 when unusable) and `remote_action_activation_state`
(the name) in `CfgExtras` + `write_cfg`. The boot report gains ONE line `> remote-activation state=<name> ms=<effective>`
printed beside the admin-session boot line (ACCEPT builds only; a CLIENT never schedules).

### 4.7 Classification of the new row

`cfg set | remote_action_activation_ms` = **operator**, not disruptive. Rationale (B358-corrected): it is ordinary
configuration of WHEN an already-accepted disruptive action fires, never WHETHER; the floor is derived from the PHY,
so no value can fire an action before the scheduled terminal has cleared the first hop, and the ceiling is fixed —
the setting cannot widen any authority (owner-class disruptive rows such as `regen`, `factory_reset`, `ota` and
`crashtest` stay owner-class regardless of it). Added to the ruled table Markdown + `kCommandPolicy` under the
proposal's closing rule, quoted verbatim: *"Rows that Slice 4/5/6 add later are classified at their closure under
the same policy and appended here by the Author; the checker refuses an unclassified row."* **OWNER VETO REQUESTED
at the gate** — if the owner reads it as security policy (owner class), the row moves; nothing else changes.

### 4.8 Numbering

B352 is this brief's (the 0e drift); B353–B358 are the preflight fold-ins. The coder's proposals start at **B359**.

## 5. Exact implementation/instrument fence

**7a-0 (refactor commit):** NEW `lib/core/mac_wait_windows.h`; `lib/core/node_mac.cpp` (the two call sites only);
NEW `test/test_mac_wait_windows.cpp`; `tools/probe_ui_model_mutations.py` (`macwait` battery + PIN). Nothing else.

**7a (feature commit):** NEW `lib/core/remote_activation.h`; `src/device_nv.h` (the v25 field + version + comment);
`src/firmware_config.cpp` (the new arm; `seed_blob_from_live`); `src/fw_main.cpp` (the boot restore of the raw value;
the boot line); `src/fw_context.h` (the extern); NEW `src/firmware_remote_activation.h`; `src/firmware_commands.cpp`
(the text line, `make_cfg_extras`, the one inputs binding); `lib/console/console_json.{h,cpp}` (the two JSON keys);
`test/test_device_nv.cpp` (v25 + size/offset pins); `test/test_radmin_characterization_0e.cpp` (the CTS-wait SUM per
R-RA-23, the old figure kept visible); NEW `test/test_remote_activation.cpp`, NEW `test/test_firmware_remote_activation.cpp`;
`docs/superpowers/evidence/2026-09-07-radmin-command-authority-table.md` + `src/firmware_command_authority.h` (the one
row) + the regenerated inventory; `tools/probe_console_sink/{structural.py,negctl.py,run.sh}` (rows: the arm calls the
pure resolver, refuses on the three non-usable states, persists through the seven-site rule; the dump/JSON twins exist;
the restore reads the field; controls for each); `tools/probe_ui_model_mutations.py` (`remoteactivation` +
`fwactivation` batteries, PIN); `lib/core/node.h` (**B355: the ONE declaration move of `max_data_sf()` to public**);
`tools/probe_inbox_verbs/probe_main.cpp` (**B356: the fake definition of `g_remote_action_activation_ms` beside the
`g_ble_*` fakes**); `tools/test_gen_command_inventory.py` (**B354: the key-buffer bound test**); evidence
`docs/superpowers/evidence/2026-09-07-radmin-slice7a.md` (the preflight stays at its head).
⛔ OUT OF FENCE: `lib/core/node.h` beyond that one declaration (no member, no `NodeConfig` field), any scheduler/deferred-action code (7b), the
seam/dispatcher, the simulator, `simulation/BASELINE.md`, the register/bench/design/rulings (QA lands).

## 6. Wiring proof

1. Native: the budget KAT and controls (§4.2); the resolver's five states with boundary values floor−1 / floor /
   ceiling / ceiling+1 / 0; the default moves when any timing input moves; the `impossible` PHY found by search;
   the NV record's new size/offset and v25; `write_cfg` renders both keys; the helpers of 7a-0 reproduce the 0e windows.
2. Console-sink structural rows + controls: the `cfg set` arm resolves through the pure header and refuses on the
   three non-usable states (a control that clamps instead of refusing is RED); the arm persists through
   `mrnv::save` and notifies (`§notify-every-save` site 1); the text dump and `make_cfg_extras` carry the twins;
   `fw_main`'s restore reads `nv.remote_action_activation_ms`; the boot line exists under ACCEPT only.
3. Corpus: 7a-0 and 7a each **36/36 byte-identical**, `lus` changed both times (state the build actions), zero
   `radmin*`/activation events in every stream.
4. Boards: RAM predicted **+4 on both** (the new global only — B353: the record did not grow, so `mrnv::save`'s
   `static Blob cur` is unchanged) — attribute by symbol; flash + attributed; Node ABI unmoved; census at pins.
   Native: a v24-versioned record of the SAME 280 bytes is REJECTED by `load()` and a v25 one accepted (B353).
5. Bench Part 57a (QA lands the text): `cfg set gw_announce_interval 7200000` → accepted (B354: it answered
   `unknown_key gw_announce_interva` before); `cfg set remote_action_activation_ms 20000` → `ok (live + saved)`; reboot;
   `cfg` shows `activation_ms=20000 state=configured`; `cfg set … 6000` (below the live floor) refuses with the live
   floor named; `… 300000` refuses; `… 0` → `state=default_derived ms=<2×floor>`; reflash from v24 → the node
   re-provisions (documented policy) and the boot line reads `state=default_derived`.

## 7. Gates and predictions (prediction-first, every figure derived)

Full chain twice (after 7a-0, after 7a): native; simulator rebuild with actions counted; corpus 36/36; both ABI probes
(unmoved); the six probes with controls (console-sink structural/controls +N derived; inbox-verbs unchanged — it does
not compile `firmware_config.cpp`); `--no-neg`; tools sweep; inventory `--write`/bare/`--check` **204**; the authority
checker + selftest; census at pins; a0 / literals; both `git diff --check`; the ruled pair. Inventory: **203 after
7a-0 (unchanged), 204 after 7a.** Union: changed-source = `macwait`, `remoteactivation`, `fwactivation`, every
`node_mac.cpp` battery (`b159mac`, `b161mac`, `b20mac`, `grantadmit`, `sliceBmac` — its M04 stays B342's),
`devicenv`, every `lib/console` battery (`sliceAjson`, `sliceDack`, `sliceGjson`, `b134ack`); dependency =
`radmin2codec` (the terminal length), `cmdauthority` (the new row). Evidence carries
`PIN re-synced? YES — <derivation>`.

## 8. STOP conditions

1. Dirty base, a §1 mismatch, concurrent input, out-of-fence path — STOP to QA.
2. Any stream delta after 7a-0 (the refactor changed an armed delay) — STOP; ⛔ no re-anchor.
3. A clamp anywhere; a literal budget term; a single-attempt CTS-wait; a resident effective copy; a `NodeConfig` or
   `Node` member; a consumer of the value (scheduling, deferral, timer) — STOP.
4. A migration arm for v24 records (the repository's policy is reprovision-on-reflash) — STOP.
5. A failed gate, an unusable control, a lost pin — STOP; no count laundering.

## 9. QA landing after the software PASS

Close B352; land bench Part 57a with the exact lines from the evidence; design §19.1 row 7a measured; the manual's
`cfg` key list; register the coder's proposals from B353; record the owner's veto (or not) on §4.7. Then the 7b
brief — the transcript, scheduler and deferred-action owner, which consumes this value.


## 10a. QA gate — 7a-0 (the pure wait-window refactor) — PASS 2026-09-07 (independent re-run at `d51d62b` + the coder's five-file package)

**Verdict: PASS for the refactor commit.** Fence exact: `lib/core/node_mac.cpp` (the two arming expressions call the
helpers with identical operands; the M-broadcast arm and every comment untouched), NEW `lib/core/mac_wait_windows.h`
(the two pure functions only), NEW `test/test_mac_wait_windows.cpp` (4 cases: the 0e windows 167/333/665 and 311,
the backoff cap with two turnarounds, every ACK term, unsigned-overflow parity), `tools/probe_ui_model_mutations.py`
(`macwait` battery, 10 entries; PIN re-synced 2843/121859), the evidence. QA figures (`scratchpad/s7a0gate/`):

| instrument | QA figure |
| --- | --- |
| native | **2843 / 121859 / 0** |
| simulator | `lus` **`10b649123db83486adfdedd5c5a6ca2c`** (moved from `db6582a1…`: `node_mac.cpp` recompiled, both variants) |
| corpus | **36/36, anchors 36/36**, s18 `32afbf11` / 269517 / 0 — the refactor armed the same delays |
| ABI probes | Node 224136/8 · 117912/8 · 150504/8 unmoved; B278 42/6 |
| six probes | console-sink 62/114 · inbox 362/39 + 360/42 · fw-ui 223 · custody 27/10 · BLE line 40/8 · features 120/59 + 40 — all at the Slice 6 pins |
| tools sweep · inventory · authority checker | 342 OK · 203 rows byte-identical · PASS + 6/6 |
| warning census | 173 / 178 / 177 / 177 / 182 / 182, `-Wswitch` 0 |
| boards | gateway **197180 / 550220 / 285** (RAM 0, flash +16 = `start_ack_timeout` +12 + 4 alignment, per the coder's symbol attribution) · heltec_mobile **207748 / 1371256 / 329** (RAM 0, flash −4) |
| batteries (6) | `macwait` 10, `b159mac` 2, `b161mac` 1, `b20mac` 11, `grantadmit` 1 all RED / 0 unusable; `sliceBmac` 3 RED / 1 unusable (M04 = B342, pre-existing) |

Coder proposal **B359** (the optional `tools/radmin_0e_abi_pins.json` overlay still pins `TimerWheel` at 824, the
pre-Slice-5 size; the default ABI probe does not read it) is registered as a small follow-up. The owner commits
7a-0; the coder records that hash as the 7a base and implements the feature under §5's second fence.
