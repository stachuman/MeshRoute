# On-device OLED + one-button UI for a team mobile — design spec

*2026-07-31. From the owner's screen draft, refined in design dialogue. Fills the `mr_ui` seam that `2026-07-12-firmware-feature-split.md` slice 4 left empty.*

*Status: ACTIVE DESIGN, updated 2026-08-17. Phase A now includes the implemented UI-13/UI-14 staged SETTINGS flow,
the T2/T3 transmit-completion reporting path, and the implemented four-slice status/navigation redesign. The current
OLED chrome is the fixed status strip + left navigation rail defined by
`2026-08-15-heltec-mobile-status-navigation-ui-design.md`; the earlier full-width status-row layouts below survive
only where explicitly fenced as history. UI-7D/UI-8/UI-9, SETTINGS storage, transmit truth, sleep/wake and the new
chrome still have named metal acceptance. ⛔ **Phase A is NOT complete:** [[B164]] remains a metal truthfulness gate,
while [[B189]]'s recovery half remains an explicit post-diagnostic decision rather than an implemented retry.
Sections marked 📝 are design only and must not be read as shipped behavior. The 2026-08-06 UI-13–UI-16 extension
adds staged settings, direct provisioning and nearby-team onboarding; UI-15/UI-16 remain design only.*

> ★★ **FACT-ONLY CORRECTION PASS 2026-08-06 — register [[B130]]. Read this before trusting any instruction below.**
> Every earlier slice was told *"⛔ do not edit the spec — report needed changes"*, and every slice obeyed. The
> corrections therefore landed in the code, the bug register, the plan, the bench guide and the owner-rulings ledger —
> **everywhere except the authoritative document**, which became the most drifted artefact in the arc. This pass
> corrected the **operational text itself**; superseded content survives only inside clearly fenced `⛔ SUPERSEDED`
> blocks, which are audit trail and **must never be read as guidance**. ⚠ **A grep of this file will therefore return
> `NOT HEARD`, the bare-`INPUT` probe and restated digits — every one of those hits is inside a fence or a quotation.
> Read the surrounding sentence; that trap has fired five times in this arc, twice on the QA-gate.**
> ⛔ **The B130 correction pass itself redesigned nothing and added no ruling.** Its outstanding items remain in
> `docs/2026-08-05-owner-rulings-ledger.md` §2 — notably **B118**'s authentication floor, the **REPLY-only wake**, and
> **B123**'s pre-3.2 park residual. The later owner-requested UI-13–UI-15 scope extension is separately marked 📝 and
> has its own open decisions in §14.

*Phased: **Phase A targets Heltec V3**; **Phase B ports to V4** and adds GPS. Phase B's radio port and GPS driver are named here but specified elsewhere — see §10.2, §10.3, §13.*

---

## Implementation status map — authoritative

Use this table before reading the historical narrative. A green build proves implementation, not completion of a
manual hardware gate.

| mark | meaning |
|---|---|
| ✅ | implemented in the landed tree; any remaining qualification is stated separately |
| 🧪 | implementation exists, but named metal/acceptance checks remain open |
| 📝 | specified only; no implementation may be inferred |
| ⏸ | deliberately deferred or blocked |

| area | status on 2026-08-17 | evidence / remaining work |
|---|---|---|
| A0 board-source placement | ✅ | landed; Heltec board UI lives under `variants/heltec_v3/` |
| UI-1 … UI-7 | ✅ | gesture, model, attribution, canvas, feature layer, real sends, roster and inbox preview are implemented |
| UI-8 emergency hardware qualification | 🧪 | all render/send arms exist; the H8 bench matrix is still the completion gate |
| UI-9 V3 battery reader | 🧪 | code and probes landed; H9 meter, control-line and radio-load checks remain |
| Status/navigation chrome | ✅ implementation · 🧪 metal | CHROME-1…CHROME-4 implemented the fixed top status strip, five-slot left navigation rail, settings badge and 19-column body. The detailed normative authority is `2026-08-15-heltec-mobile-status-navigation-ui-design.md`; bench Parts 24–25 remain the visual, timing and sleep/radio acceptance gate. |
| UI-7D inbox detail/delete | ✅ **UI + storage** · 🧪 metal | ⛔ **CORRECTED IN PLACE 2026-08-13: this row used to read `📝 UI · ✅ storage` and end *"§3.5's modal is slice B and is not built: the double press on INBOX still intentionally does nothing"*. THAT IS NOW FALSE.** **Slice A** (storage) landed 2026-08-06, QA-rejected the same day, re-closed 2026-08-07 — §6.2's `Inbox::erase(InboxKind, seq)` (tombstone; three outcomes; console `del_msg`), with the durable store's **mid-frame tear** ([[B135]], pre-existing) and the verb's **target parsing** ([[B136]]) both fixed. **Slice B** (the §3.5 modal) landed **2026-08-13**: `src/firmware_ui_model.h` owns the identity-tracked selection (`(InboxKind, seq)`), the modal, its paging and all three erase landings; `src/firmware_ui.cpp` owns the `(kind, seq)` lookup, the in-callback body copy and the one `erase()` call. 32 native mutations + 13 device-half probe controls, all RED. 🧪 **What remains is metal**: the panel's own rendering of the modal and the [[B134]] non-persistence check — see `docs/2026-07-31-bench-test-script.md` §UI-7D |
| UI-10/UI-11 configurable presets | 📝 | §3.2.2–3; current OLED still uses the landed fixed catalog |
| UI-12 Heltec ESP32 BLE | 📝 | §2.2; persisted `ble_mode` does not provide BLE on V3 yet |
| T2/T3 transmit-completion truth | ✅ implementation · 🧪 metal | HAL completion outcomes and app/UI `QUEUED` → `SENT` / `NO RELAY HEARD` semantics are implemented. [[B164]] remains open for Part 22 metal correlation; [[B189]]'s B186b recovery is deliberately not implied by observability. |
| UI-13 typed staged-config service | ✅ **service only — HEADLESS** | ⛔ **CORRECTED IN PLACE 2026-08-13: this used to be one `UI-13…UI-16` row reading `📝 … no SETTINGS screen, draft marker, team-create/static-join UI or nearby-team onboarding exists yet`. That sentence is now FALSE for UI-13 and still TRUE for UI-14…UI-16, so the row is SPLIT rather than overwritten.** `src/firmware_config_service.h` implements §3.6.1: the three states (persisted / effective / draft) with `config_unsaved`, `conflict` and `reboot_required` as three DISTINCT comparisons, typed per-field validation, whole-candidate validation before any write, exactly ONE durable `/mrcfg` write, live apply only after durable success, DISCARD/RELOAD, and the two ruled headlines `CFG! RELOAD` / `SAVE FAILED`. Covered fields = the four already-durable ones (`ble_mode`, `e2e_dm`, `intro_attach`, `mobile_autoregister`); no NV schema change, no `kVersion` bump. 29 native cases / 32 mutations, all RED. ⚠ **Proved against FAKES: the NV fault / power-cut half is deferred with the device binding to UI-14 / [[B193]].** ⛔⛔ **CORRECTED IN PLACE 2026-08-13 (QG round 2): this row used to END *"NOTHING RENDERS OR CALLS IT: no SETTINGS screen, no menu row, no gesture, and `ICfgStore`/`ICfgLive` have no device binding yet, so the running firmware's behaviour is unchanged"* — SITTING DIRECTLY ABOVE THE UI-14 ROW THAT SAYS THE OPPOSITE. That sentence is withdrawn here, not deleted.** ★ **Read it as what it always was: the §UI-13 SLICE BOUNDARY.** UI-13 landed HEADLESS by scope — no renderer, no menu row, no gesture and no hardware binding *in that slice* — and **§UI-14 CONSUMES it**: the SETTINGS screen renders it, `src/firmware_ui.cpp` constructs the one `ConfigService`, and `mrfw::device_cfg_store()` / `device_cfg_live()` supply the binding ([[B193]]'s first half). ⇒ the running firmware's behaviour **is** changed by UI-14, and the header of `src/firmware_config_service.h` carries the same correction beside the obligations it recorded. ⛔ **What is STILL owed is [[B193]]'s other half:** the binding is exercised only through FAKES, so no real-flash write, no wear and no reset-during-write is proved by any gate — bench Parts 19/20 |
| UI-14 SETTINGS screen, draft marker, save/discard/reboot | ✅ **UI + the device binding** · 🧪 NV / power-cut | ⛔ **CORRECTED IN PLACE 2026-08-13: this was part of a `UI-14…UI-16 📝` row saying *"no SETTINGS screen, no draft marker on the panel"*. That is now FALSE for UI-14 and still TRUE for UI-15/UI-16, so the row is SPLIT rather than overwritten.** §3.6.2's menu is the fifth cycle slot on both cycles: the three live-class value rows with `short`-to-cycle / `double`-to-enter-and-accept, SAVE / DISCARD / BACK, a **conflict-conditional RELOAD** row, and a **present-but-inert PROVISION** row that refuses out loud. §3.3's marker is on STATUS (`CFG* UNSAVED` / `CFG! RELOAD`) and `RESTART NEEDED` is its own row. `src/firmware_ui_model.h` owns every gesture meaning, the row table and all four action landings; `src/firmware_ui.cpp` owns the frame-frozen render and the one `ConfigService` instance; **[[B193]]'s DEVICE BINDING landed in `src/firmware_config.cpp`** (`device_cfg_store` / `device_cfg_live` — the §nv-ritual `nv_load_stamped` load and the OFF→ON `mobile_register_current()` bridge). 29 native cases / 18 new model mutations / 8 new device-half probe controls. ⚠ **The BLE-mode row is ABSENT in every env in the tree** — §3.6.2's own condition, since UI-12 does not exist; the present arm is built and run by the probe under `-DMR_UI_BLE_ROW=1`. 🧪 **What remains is the storage half: no NVS/LittleFS write, no wear and no reset-during-write is exercised by any gate** — see `docs/2026-07-31-bench-test-script.md` §UI-14 ⛔ **AMENDED 2026-08-20 by [[B232]] (owner ruling): the fifth slot LANDS CLOSED on a single `ENTER SETTINGS` row — `short` passes in one press, `double` opens the menu (and stays CLOSED when the service cannot open). The auto-enter-on-arrival described above is WITHDRAWN.** The service still opens on arrival and the remedy words still render from the closed view. |
| UI-15…UI-16 provisioning and nearby onboarding | 📝 | §3.6.3–§3.6.4; no team-create/static-join UI and no nearby-team onboarding exists. ⚠ The SETTINGS menu's PROVISION row is rendered and **refuses**; §3.6.3's *"an unsaved draft requires SAVE or DISCARD first"* precondition is deliberately **not** implemented — it belongs to UI-15 |
| Heltec V4 UI/GPS | 📝 | Phase B; radio/RF work is owned by the separate V4 spec |

**Chrome precedence (2026-08-17):** the UI-14 row retains its 2026-08-13 landing description, including the then-live
STATUS placement. CHROME-1…4 superseded only that placement: configuration state is now the SETTINGS-rail badge and
actionable words remain on SETTINGS. Read §3.3 for current behavior.

Section headings below repeat a status only where the whole section has one clear state. Mixed sections defer to this
table. Historical `⛔ SUPERSEDED` blocks are evidence only.

---

## 0. Purpose

A team mobile with a display and one button must let a hiker **raise an alarm and know it was heard** without a phone. That ruling (owner, 2026-07-31) orders every trade-off below: the emergency path gets a dedicated gesture, an explicit confirm, honest delivery reporting and bounded auto-retry; browsing screens are deliberately thin.

The device remains fully usable with the companion app — this UI adds a degraded-mode surface, it does not replace the app or the console.

## 1. Scope

**In:** screen framework, one-button gesture input, status bar, team roster, merged inbox, configurable DM/channel/emergency presets, the emergency send path, battery indicator, screen blanking, secured companion BLE, and the staged SETTINGS/provisioning extension: explicit save/discard, an unsaved marker, on-device team creation, nearby-team onboarding with explicit sealed-key approval, and profile-based static-network join.

**Out (owner-scoped):**

| deferred | why |
|---|---|
| Free-form one-button entry of arbitrary text or arbitrary RF numbers; OTA/GPS enable; screen-timeout editing | text remains a BLE/serial job; unconstrained numeric entry is unsafe and poor with one button. Screen-off timeout remains a build constant. |
| Separate channel-message screen | merged with the DM inbox; two inbox screens double the cycle for the same interaction. |

**Scope change, owner 2026-08-06:** the earlier blanket exclusion of an editable settings screen is withdrawn.
Finite-choice settings and provisioning actions are now in scope under §3.6. This is a new feature, not a defect in
UI-1…UI-9. A setting already documented as persistent but lost after reboot remains a separate firmware bug.

**Phased by board (owner ruling 2026-07-31): Phase A targets Heltec V3, Phase B adds V4.** V3 first because its panel and button are identical to V4's while it has no front-end module, so the display and input work can land without the radio port §10.2 describes. See §13.

**GPS / distance-to-teammate: out for Phase A, IN for Phase B (V4).** No GPS driver exists anywhere in the tree today and `lat`/`lon` are typed by hand via `cfg set lat`/`lon` into `/mrid`, so Phase A's TEAM screen shows last-heard age and signal quality only — both already in the core. Phase B adds the GPS driver and the distance column (§10.3).

⚠ **Honest caveat on the evidence.** MeshCore's V4 config defines GPS pins *and* sets `ENV_INCLUDE_GPS=1`, which is what prompted reviving this. But its **V3** config also defines GPS pins (RX 47, TX 48, EN 26) without enabling GPS — so pin definitions alone do not prove an on-board module, and my earlier reading overstated the case. Practically this changes little: the driver work (UART NMEA on defined pins) is identical whether the receiver is on-board or attached to headers. What it changes is whether the *user* must attach a module, which needs confirming against the physical board before Phase B is planned.

**Explicit non-goal:** per-message read/unread. The existing cursor model (`Inbox::read_cursor()` / `mark_read()`, `inbox.h:69-70, 116`) is kept as-is, per the owner's draft.

## 2. Architecture

The `mr_ui` seam already defines the boundary and is already wired unconditionally — `mr_ui_init()` at `fw_main.cpp:795`, `mr_ui_on_push()` at `:1066`, `mr_ui_tick()` at `:1208`, with inline no-ops when `MR_FEAT_OLED=0` (`lib/hal/mr_ui.h`). This spec fills that seam; it does not change it.

---

## §0 BOARD SOURCE OWNERSHIP — ★ THE BROAD SPLIT IS PARKED (owner-ruled 2026-08-03)

⚠⚠ **v1 of this section (2026-08-03, review-driven) SPECIFIED A PREREQUISITE SLICE THAT WAS MEASURED AND DISPROVEN
BEFORE A LINE MOVED.** The measurements are in `simulation/BASELINE.md`'s top note and register **B61**; the withdrawn
QA brief is `docs/2026-08-03-phase0-qa-objective.md`. **Four of my own claims were false — recorded because two of them
are the reason the slice looked worth doing:**

| v1 claimed | measured |
|---|---|
| "use `boards/`" | ⛔ **`boards/` is PlatformIO's** — it holds the vendored manifest `seeed-xiao-afruitnrf52-nrf52840.json` + `nrf52840_s140_v7.ld`, wired at `platformio.ini:97-98`. My own objection to `variants/`, aimed at the directory I recommended |
| "`variants/` belongs to the Arduino core" | ⛔ **False.** The Adafruit core reads `variants_dir` from the **board manifest**; ours does not set it, so it resolves to `FRAMEWORK_DIR/variants` — which holds no `Seeed_XIAO_nRF52840`. The two hand-wired lines I cited as proof of core ownership **are the entire mechanism.** `variants/` was already ours |
| "board ≈25 sites vs chip ≈65" | ⛔ **Double-counted.** Of **26** board-macro sites, **23 are chip-family OR-chains** where the board macro is a redundant alias — and those 23 were counted on **both** sides of my table. `BOARD_XIAO_ESP32S3` was omitted entirely. Real: **33 chip · 3 board** |
| "byte-identical per env" as the gate | ⛔ **Unachievable.** The controls have a genuine **zero** noise floor and `-I src` is innocent, but a semantics-preserving `git mv` **out of `src/`** still moves flash **+192 B**, RAM **−8 B**, and resizes **14 Arduino/ESP-IDF functions** via link-order xtensa relaxation. Only "delta attributed" survives — and attribution means explaining *framework* code motion |

★★ **The premise is gone: there is no board tier to absorb.** Genuinely board-discriminating code is **three lines** —
the `#if/#elif/#elif` body of `board_name()` (`src/firmware_commands.cpp:339-349`). The real board coupling is the
**56 `-D` flags** in `platformio.ini` (25/16/15 per board), and those are heterogeneous (application capability · radio
wiring · RadioLib config · TinyUSB/framework · nRF52 storage). A `capabilities.h` cannot reach the framework and C
translation units that receive them; a forced global include is a build-system redesign. ⇒ **its own design, its own
tests, NOT a remnant of a disproven refactor, and NOT a prerequisite for the OLED work.**

### The ruling — where board sources live

| tree | owner | rule |
|---|---|---|
| **`variants/<board>/`** | ★ **ours** — per-board sources | already project-owned; matches MeshCore and "one directory per physical board". `Seeed_XIAO_nRF52840/` already lives here |
| `boards/` | **PlatformIO** | manifests + linker assets ONLY. Never MeshRoute sources |
| `platform/` | **RESERVED** | for a future *genuine* chip-family abstraction. **Not another name for board directories** |
| `src/firmware_ui_*` | unchanged | board-INDEPENDENT product logic stays here |
| `lib/hal/` | unchanged | the small composition interfaces (`IBoardRf`, the `board_ui.h` canvas) |

⚠ **Do NOT add `variants_dir` to the board manifest yet.** Letting the toolchain own the directory (and thereby dropping
`platformio.ini:117`'s `../variants/…` term and `:137`'s `-I` — which must drop **together**) is an **untested** change
and a separate slice.

### What survives: a narrow A0 placement slice

★ **Phase 0's broad split is PARKED. `MR_FEAT_OLED` defaults 0 and the seam is 32 lines with one guard and zero board
conditionals ⇒ nothing blocks Phase A.** (It now lives at `variants/heltec_v3/board_ui.cpp` — moved by A0, below.) The only placement work worth doing is done *before* the OLED code
exists, so the feature lands in the right file rather than being moved afterwards:

**A0** — move the **empty** `src/board_ui.cpp` seam to `variants/heltec_v3/board_ui.cpp`, rewire the Heltec
environments, gate all eleven. Then implement the OLED feature in the already-correct location. **V4 later becomes
`variants/heltec_v4/` carrying its own `board_ui.*`, `board_rf.*`, `lora_fem.*`, pins and power handling.**
✅ **A0 HAS LANDED (2026-08-03) and the prediction that stood here was WRONG IN BOTH HALVES.** Measured: the **three
Heltec envs are IDENTICAL** in every flash-bearing section, RAM and the whole symbol multiset — the only delta is
`.debug_line`/`.debug_str` **+15**, exactly the path-length difference. The real movers are the **three `xiao_esp32s3`**
envs (RAM −8, flash +168/+48/+124, ~30 **framework** symbols), because `MR_FEAT_OLED` defaults 0 and the TU was a
**zero-byte object that was nonetheless a link input** there. ★ **B63, proven by probe: the directory is irrelevant to
code size — LINK-SET MEMBERSHIP of even an empty object is the entire effect, and it is xtensa-only.** The old "+192 B
for leaving `src/`" measured `device_ota.cpp`, a 227 KB object, i.e. reordering *non-empty* objects. Full grid:
`simulation/BASELINE.md` §A0.

ⓘ **Independently, as a tiny safety fix (register B61, its own commit):** `board_name()`'s `#else` silently returns
`"native"` on any target defining no `BOARD_*`. Verified safe to make loud — all four macros are declared exactly once
(`platformio.ini:79/134/218/263`) and every one of the seven extending envs re-lists `${env:<board>.build_flags}`, so
all eleven envs satisfy an arm:

```c
#elif defined(MESHROUTE_NATIVE)
    return "native";
#else
#error "No supported BOARD_* or MESHROUTE_NATIVE target selected"
#endif
```

Four units:

| unit | responsibility | depends on | tested |
|---|---|---|---|
| `src/firmware_ui_input.h` — pure header | classify `(level, now_ms)` samples → `short` / `double` / `long-arm-progress` / `long-fire` / `cancel` | nothing | **native** |
| `src/firmware_ui_model.h` — pure header | reduce `(Gesture, UiSnapshot, SendOutcome) → UiState` — screens, cursors, compose modal, emergency machine, per-send outcome states, dirty flag | nothing | **native** |
| `src/firmware_ui.cpp` | build `UiSnapshot`; drive the model; perform sends and **correlate their outcomes**; own all screen selection and text formatting; call canvas primitives | core accessors, model, canvas | on-target |
| `variants/heltec_v3/board_ui.cpp` | board port **only**: U8g2, I²C, button GPIO, battery ADC, panel power state | Arduino, display lib | on-target |

**Hard boundary — restated after review, because the first plan draft broke it.** `board_ui.h` exposes a **display-independent canvas**, not the UI model:

```
begin_frame() · next_page() · set_font(Font) · draw_text(x, y, const char*) · draw_hline(x, y, w)
draw_bitmap(x, y, w, h, bits) · draw_rect(x, y, w, h)
set_power_save(bool) · button_pressed() · battery_sample_mv()
```

`board_ui.h` **must not include `firmware_ui_model.h`**. `firmware_ui.cpp` owns every decision about *what* is drawn; `board_ui.cpp` owns only *how pixels reach the panel*. That keeps U3 honest and is what makes the Phase B port a pin table rather than a rewrite — the V4 has the same panel, so a correct boundary means zero render changes.

### 2.1 Send attribution — the contract that makes outcomes trustworthy · ✅ implemented

★ **This section exists because the first plan draft could report a false `PICKED UP`.** Pushes are node-wide: `channel_sent` and `send_blocked` fire for *every* origination, including console, BLE, scheduled and canned sends. Routing them into the emergency machine unconditionally means an unrelated channel post can complete an emergency that was never transmitted — a false safety confirmation, which is the exact failure this feature must not have.

`firmware_ui.cpp` therefore owns a **send tracker**, and the model never sees a raw push:

| field | why |
|---|---|
| `kind` | emergency · canned channel · DM |
| `state` | `submitted` → `accepted` / `refused` → terminal |
| `ctr` | the accepted send handle from `CmdResult`, the only reliable correlator |
| ~~`channel_id`~~ / `peer_id` | ⛔ **§B81 (2026-08-04): the `channel_id` half is UNSATISFIABLE and is withdrawn.** Neither `channel_sent` nor `send_blocked` carries a channel id, so the tracker's `_chan` is written-never-read **by construction** — no implementation can honour this. `peer_id` stands and is load-bearing (it is half of the DM correlator, with `ctr`). ⓘ Restoring the channel half would need the core to put a channel id on those pushes — a core slice, and see **§B84**: the emergency path no longer depends on correlating a channel push at all. |
| `accepted_ms` | opens a bounded outcome window |

Rules, all of which must hold before a push may complete a UI transaction:

1. `ui_perform_send()` returns a **typed result**, never a discarded `BufferSink`. A parser refusal or an immediate `CmdCode::err_*` is a terminal outcome shown on the panel — never an indefinite `SENDING...`.
2. ★ **Accepted requires a NON-ZERO `ctr`.** ⚠⚠ **CORRECTED §B84 (2026-08-04) — the reason is NOT what this section used
   to say.** `ctr == 0` does **not** mean "nothing was transmitted", and **no matching `send_failed` is guaranteed to
   arrive**. It means **NO LOCAL HANDLE EXISTS and the transmission status is UNKNOWN** — there are three producers and
   the third is a delegated **SUCCESS** (`node.cpp:1565-1573`), while the post-mint seal failure pushes a reason that
   the core itself documents as arriving *"asynchronously and correlat[ing] with nothing"* (`node_channel.cpp:~740`).
   ⇒ **unattributable failures are IGNORED; expiry supplies `channel_remote_mint` and CONSUMES one bounded attempt**,
   so `kEmgMaxTries` expiries end in the sticky **`not_heard`** state — panel headline **`NOT RELAYED`** (B117;
   ⛔ this clause used to restate the bound as "three" and spell the state `NOT HEARD`, [[B130]]).
   `CmdCode::queued` alone is not proof of transmission — a blocked or seal-failed channel post returns `queued` with `ctr == 0` (B39). `next_ctr` never yields 0, so zero is the sentinel. A zero-ctr result means **no LOCAL HANDLE exists and the transmission status is UNKNOWN** — it is *not* proof that nothing was sent (the third producer is a delegated **success**). The tracker therefore does **not** wait for a matching `send_blocked` / `send_failed`: an unattributable failure is **ignored**, and the slot is closed by **bounded expiry**, which supplies `channel_remote_mint` **and consumes one attempt** (§B84).
3. `channel_sent` completes an emergency **only** when its `ctr` matches the tracked handle (full 16-bit width since **B40, ✅ landed 2026-08-01**; ⚠ it is a **LOCAL** handle — never match it against a *received* id).
4. `send_blocked` must be `blocked_channel == true` **and** arrive inside the pending request's outcome window. It carries no `ctr`, so scope plus a bounded window is the only correlator available — weaker than exact matching, and labelled as such rather than described as exact attribution.
5. `send_e2e_acked` / `send_failed` complete a **DM** only on a matching `ctr` **and** peer.
6. **The full `SendFailReason` reaches the UI**, not a boolean: `no_pubkey` → `NO KEY`, `e2e_ack_timeout` → `NO CONFIRM`, others → a compact reason. Collapsing them makes `NO CONFIRM` unreachable and discards the one thing that tells the user what to do next.

Anything unmatched is ignored. Native tests must interleave unrelated channel and DM outcomes and prove the emergency state cannot move.

**Two trackers, and the emergency always wins.** A single in-flight slot would serialise a distress call behind an outstanding `-a` DM waiting on its end-to-end ack — a long press would queue an alarm that cannot dispatch until the DM's ack or deadline closes. That defeats "fires from any screen". So:

- the **emergency** slot and the **normal** slot (DM or canned channel) are independent and may be in flight together; their pushes are distinguishable by kind plus `ctr`/peer;
- an emergency **never** waits on, and is never overwritten by, normal UI work;
- if a canned channel transaction is outstanding when the emergency fires, its UI outcome tracking is **abandoned** — the emergency takes the channel slot immediately, and the late canned `ctr` simply will not match;
- the model likewise holds the pending emergency request separately, so a normal compose action cannot overwrite it.

**How the UI issues a send.** ⚠ Not through `dispatch()` — that is a console *verb router* which returns `false` for `send` / `send_channel`; its callers (`service_console`, `ble_dispatch_line`) each open-code `parse_command` + `Node::on_command`. The UI uses a small typed helper, **`mrfw::exec_command(line, len) → ExecResult`** (owner-approved 2026-08-01), which runs that same sequence and returns the `CmdResult`. Composing a command *string* keeps the UI off the `Command` struct's field names, so flags like `-e` and `-l` needed no UI change when they landed; taking the result *typed* keeps a safety behaviour off a formatting detail. Scraping `BufferSink` text is explicitly rejected.

### 2.2 Companion transport reality and the Heltec BLE extension (2026-08-05) · 📝 not implemented

**USB serial and BLE are not the same byte transport, but they must expose one command meaning.** USB reads a
newline-delimited stream from `Serial`; BLE uses Nordic UART Service framing, MTU-sized notifications and a separate
line accumulator. They converge only after a complete line exists: `service_console` and `ble_dispatch_line` reuse
the command parser/handlers. New preset management therefore gets **one shared handler and one response schema**,
called by both ingress paths. It must not grow a serial-only parser and a companion-only JSON mutation path.

★★ **Current Heltec reality:** `device_ble.h` defines the real BLE-NUS implementation only for nRF52. On ESP32,
`mrble::begin()` returns `false`, `connected()` is always false and every other method is inert. The existing
persisted `ble_mode` field does **not** switch BLE on for Heltec V3; today it merely reaches `INIT FAILED` at boot.
The Heltec extension therefore includes a real ESP32-S3 implementation behind the existing `mrble` API. Documentation
and the companion must not claim Heltec BLE before that slice lands.

The ESP32 transport contract is parity with the secured nRF52 path, not merely “a UART-like characteristic”:

- the same NUS service/characteristic UUIDs and newline-delimited command stream, with MTU-aware output chunking;
- `ble_mode = off | on | periodic`, the existing advertising window, and reboot-to-apply configuration semantics;
- encrypted, bonded, MITM-passkey access using the persisted six-digit `ble_pin`; unauthenticated writes never reach the command handler;
- non-blocking service from the main loop, bounded RX/TX buffers, loud overflow, and no waiting inside a radio-critical interval;
- `mrble::connected()` becomes truthful so sleep policy and the OLED indicator can use the same source.

**Build scope:** first enable the ESP32 BLE implementation on `heltec_mobile`, the Phase-A product target. Do not
silently add its RAM/flash and coexistence risk to `gateway_heltec` or every ESP32 build. On an ESP target where the
transport is compiled out, `cfg set ble_mode on|periodic` must refuse `unsupported` rather than persist a setting
that can only boot into `INIT FAILED`.

**Why two pure headers.** `[env:native]` sets `test_build_src = no` but adds `-I src` (`platformio.ini:83`), so a pure header in `src/` is reachable from native tests — the established precedent is `firmware_config_parse.h` driven by `test_firmware_config_parse.cpp`. Gesture timing and emergency-state transitions are miserable to debug on hardware and trivial to test on host; both belong on the right side of that line.

`UiSnapshot` is plain data — unread counts, ages in ms, a bounded array of teammate rows, `batt_mv`, `ble_connected`, last send outcome. The model never sees `g_node`, so a native test can drive "3 teammates, one heard 4 h ago, emergency armed, `no_relay` returned" with no radio and no Arduino.

## 3. Screens and gestures

### 3.1 Cycle · ✅ current cycle; 📝 provisioning extension

| slot | screen | gate | status |
|---|---|---|---|
| 1 | STATUS | `MR_FEAT_OLED` | ✅ |
| 2 | TEAM (peer list) | `MR_FEAT_OLED && MR_FEAT_TEAM` | ✅ |
| 3 | INBOX (DM + CH merged) | `MR_FEAT_OLED` | ✅ preview; ✅ detail/delete (§UI-7D slice B, 2026-08-13) |
| 4 | SEND (team channel) | `MR_FEAT_OLED && MR_FEAT_TEAM` | ✅ |
| 5 | SETTINGS / PROVISION | `MR_FEAT_OLED` | ✅ SETTINGS (§UI-14, 2026-08-13); 📝 PROVISION (§UI-15) — the row exists and refuses |

⛔ **CORRECTED IN PLACE 2026-08-13 (§UI-14):** this read *"The landed cycle is the first four rows … AFTER UI-14 LANDS, SETTINGS is appended"*. UI-14 has landed. **The landed cycle is all five rows: STATUS → TEAM → INBOX → SEND → SETTINGS, or STATUS → INBOX → SETTINGS on a non-team build** (`UiModel::next_screen`, `src/firmware_ui_model.h` — SETTINGS is deliberately NOT team-gated, because the four covered fields are durable on every build and `gateway_heltec` is a real `OLED=1 / TEAM=0` env).

**Revised 2026-08-05 (owner).** An earlier draft gave every canned channel message its own slot to avoid a nested mode.
Once the DM feature needed a compose step anyway, that reasoning inverted: **one compose sub-view, used by both send
paths, is simpler than N message slots**. Enabling a preset adds a list row, not a cycle slot. The later SETTINGS
extension adds exactly one fixed slot, independent of preset count. Emergency remains outside every list.

**"I'm in danger" appears in no list.** It is reachable only by long-press (§4). Two routes to the same dangerous action invite accidental alarms, and the navigational one would be the route nobody can use under stress.
**As-built navigation chrome (2026-08-16):** the cycle above is represented by five fixed rail slots — STATUS, TEAM,
INBOX, SEND and SETTINGS — at x=0…9 below the top strip. Gated-out TEAM/SEND slots remain empty instead of shifting
later icons. The active slot has a rectangular outline. Detail and compose modals retain their parent slot; emergency
suppresses the rail and uses the full width while keeping the top strip. The exact pixel geometry and modal mapping
are normative in `2026-08-15-heltec-mobile-status-navigation-ui-design.md`.


### 3.2 Gestures · ✅ current screens; ✅ SETTINGS actions (§UI-14, 2026-08-13)

| gesture | meaning |
|---|---|
| short | **advance within the current list; at the end, move to the next screen** |
| double | activate the highlighted item — TEAM: open DM compose · INBOX: open detail (UI-7D) · SEND: open channel compose · SETTINGS: open the menu from the closed entry row, then edit/activate the row (UI-14, [[B232]]) · sub-view: confirm the highlighted action · STATUS: none |
| long | arm emergency, from **any** screen, sub-views included |

`short` is **list-aware**: on STATUS and SEND it simply moves on; on TEAM and INBOX it walks the list and leaves
only at the end. ⛔ **CORRECTED IN PLACE 2026-08-20 ([[B232]], owner-ruled): SETTINGS IS NO LONGER IN THAT SET, AND
THE WITHDRAWN WORDING IS KEPT VISIBLE — it read *"on TEAM, INBOX and SETTINGS it walks the list"*.** SETTINGS now
**lands CLOSED on a single entry row** (`ENTER SETTINGS`); `short` passes the screen in **one** press like every
other screen, and `double` **enters** the menu — the enter-by-double idiom §3.6.3's child menu already uses. Inside
the menu `short` walks the rows exactly as before; walking off the last row, and the `BACK` row, both return to the
**closed entry view**, never straight off the screen. ⓘ Why: as built the operator paid **one short press per row —
up to nine** — merely to cycle past SETTINGS. This frees `double` to mean “act on what is highlighted” everywhere. The emergency `long`
gesture retains priority inside every future editor; no settings interaction may consume or delay it.

### 3.2.1 Compose sub-view · ✅ implemented with the current fixed catalog

Entered by `double` from TEAM (a DM to the highlighted peer) or from SEND (a team channel post). It is the one modal in the design, and it is safe because it always contains an explicit exit:

```
+---------------------------------------+
| DM3 CH12  T4/5  BLE*  84%             |
+---------------------------------------+
| to: Ann (id 174)                      |
| > Are you OK?                         |
|   I'm OK                              |
|   back without sending                |
+---------------------------------------+
```

- `short` moves the highlight; `double` sends the highlighted text, or leaves on **`back without sending`**.
- When at least one preset is enabled, the cursor starts on the **first message**, not on `back` — the peer was chosen deliberately one gesture ago, and these texts are benign. If that catalog has no enabled presets, the view shows `no presets configured` plus `back without sending`, and the cursor starts on `back`.
- **Auto-exit after `kBlankMs` of no input** (⛔ named `MR_UI_BLANK_MS` here until 2026-08-06; that macro exists nowhere in the tree — [[B130]]), returning to the parent screen **without sending**. A modal that can outlive the user's attention is a modal that eventually sends the wrong thing.
- `long` still arms the emergency from inside it.

### 3.2.2 Configurable preset catalog · 📝 not implemented

The original hard-coded strings become **defaults**, not firmware policy. The first configurable version uses one
mandatory emergency slot plus two independent fixed-capacity catalogs: **eight DM slots and eight channel slots**.
The capacity is fixed for deterministic embedded storage; the visible count is not. A user may enable from zero to
eight entries in each compose list without a dynamic allocator or an unbounded list. Raising either capacity later is
an explicit catalog-format revision.

| stable slots | compiled default | default state | UI reachability |
|---|---|---|---|
| `emergency` | `I'm in danger`, location on | enabled, mandatory | long press only; never appears in a compose list |
| `dm1`, `dm2` | `Are you OK?`; `I'm OK`, location off | enabled | TEAM → DM compose |
| `dm3` … `dm8` | empty, location off | disabled | TEAM → DM compose when configured |
| `channel1`, `channel2` | `Got your message`; `All good`, location off | enabled | SEND → channel compose |
| `channel3` … `channel8` | empty, location off | disabled | SEND → channel compose when configured |

The OLED lists only enabled slots, in stable-slot order, and scrolls through all eight when necessary. Gaps are valid:
for example `dm1`, `dm4` and `dm8` may be the three visible rows. Therefore every visible row carries its stable slot
identifier; code must never derive `dmN` or `channelN` from the current row index. DM presets never appear in the
channel list and channel presets never appear in the DM list.

`back, don't send` is an **action**, not a message and not persisted. It remains the derived final row of each compose
view, preserving B66's “one table/count authority” cure. DM and channel slots may be disabled. Emergency cannot be
disabled, cleared or made empty. A catalog with zero enabled entries has no sendable row and shows the empty state
defined in §3.2.1.

Each persistent slot stores:

- `enabled`: one explicit boolean for DM/channel slots. `set` makes it true and `clear` makes it false. Emergency is
  implicitly always enabled.
- `text`: when enabled, 1–18 printable ASCII bytes containing at least one non-space character; `"`, `\`, CR and LF are
  rejected. Eighteen is a UI safety bound, not an on-air limit: with the selection marker and location marker the
  full message remains visible in the panel's implemented 19-column body. The device must never send a hidden
  suffix the wearer could not inspect.
- `include_location`: one explicit boolean. The compose row shows `L` when set and `-` when clear; this is part of
  what the user confirms before the double press.

### 3.2.3 Location semantics, configuration verbs and persistence · 📝 not implemented

“Include location” has two deliberately different failure policies:

| slot kind | location on |
|---|---|
| emergency | if a fix exists, issue `send_channel <ch> "<text>" -t -l -e`; without a fix, still send `-t -e`. An alarm is more important than its coordinates, preserving §4.1 |
| channel | issue `send_channel <ch> "<text>" -t -l -e`. No fix/key/seal means a loud refusal; never silently strip `-l` |
| DM | issue `send <team-id> "<text>" -t -a -l`. The existing core accepts `-l` on an id form but refuses unless the effective DM is sealed; therefore this requires `e2e_dm=1`, a usable key and a fix. Do not change addressing to hash or silently downgrade merely to make the preset send |

Location off emits the existing command without `-l`. In every case the core remains the final privacy gate: a
location-bearing message must be sealed.

The shared administrative grammar is:

```
ui preset list
ui preset set <emergency|dm1..dm8|channel1..channel8> loc=<on|off> "<text>"
ui preset clear <dm1..dm8|channel1..channel8>
ui preset reset <emergency|dm1..dm8|channel1..channel8|all>
```

USB serial and BLE return the same bounded NDJSON records:

```json
{"ev":"ui_preset","slot":"dm1","enabled":true,"text":"Are you OK?","location":false}
{"ev":"ui_presets_end","capacity":17,"dm_active":2,"channel_active":2,"generation":7}
{"ev":"ui_preset_err","reason":"bad_slot|bad_text|bad_location|mandatory|busy|store"}
```

`list` emits all 17 records in stable slot order, including disabled slots, followed by `ui_presets_end`; this lets
the companion edit exact stable slots without inferring them from active-list positions. `set` validates the full
record and enables that slot. `clear` disables a DM/channel slot and clears its body and location flag; attempting to
clear `emergency` fails with `mandatory`. A single-slot `reset` restores its compiled default, which means slots 3–8
return to disabled. `reset all` restores the complete compiled catalog. Mutating verbs return the resulting record,
or the full list for `reset all`. The companion contract must add these events before the app exposes an editor.

Persistence lives in a **separate versioned UI record** (suggested key/file `/mrui`) behind
`mrnv::load_ui_presets` / `save_ui_presets`. Do not grow `mrnv::Blob`: its size/version mismatch deliberately
reprovisions the whole node, and editing a phrase must never reset radio, identity, team or key configuration. A
missing UI record uses the compiled defaults. A corrupt/unsupported record also falls back to those safe defaults
but emits a visible boot/status warning; it is never silently accepted as valid. Save a fully validated temporary
catalog first and replace the live catalog only after durable storage succeeds.

The catalog carries a persisted, non-zero `generation`, incremented only with a successful durable update. Consumers
compare it for equality rather than ordering, so uint32 wrap is harmless. A `SendReq` identifies both the enabled
stable slot selected by the wearer and the generation they saw; it never stores only the compacted visible-row index.
If the slot is disabled or the generation no longer matches at execution, refuse and repaint—never resolve the same row
index to newly configured words. A preset update while a selection-phase compose modal is open closes that modal
without sending. An outcome already being displayed may finish. Any preset write while an emergency is active
returns `busy`; an alarm's retries must not change body or location policy halfway through the attempt series.
Page-buffer painting likewise freezes one catalog generation for the whole frame so a BLE update between OLED pages
cannot tear two versions into one image.

### 3.3 Layout · ✅ implemented status strip + navigation rail (2026-08-16)

The current geometry is normative here at product level and pixel-exact in
`2026-08-15-heltec-mobile-status-navigation-ui-design.md`:

- **status strip:** y=0…8, followed by a rule at y=9;
- **navigation rail:** x=0…9, y=10…59;
- **body:** x=12…127, 116 px wide, five small-font baselines at y=19/29/39/49/59, and therefore **19 text columns**;
- **emergency exception:** retain the top strip, suppress the rail, and use x=0…127 for the alarm body.

The status strip is a fixed-slot summary. A missing value changes its token to `--`; it never shifts later slots:

| slot | content | semantic authority |
|---|---|---|
| mail, x=0 / token x=8 | combined session-unread DM + channel count: `0…99`, then `99+` | merged inbox cursors, not store depth |
| home, x=28 / token x=36 | `--`, seconds, or minutes since the latest correlated **bidirectional** confirmation with the registered home | mobile-home link only; never generic RF activity |
| team, x=56 / token x=64 | active team-route count: `--`, `0…9`, then `9+` | active runtime routes; this is reachability evidence, not an “online” claim |
| key, x=79 | key / crossed-key icon | actual team content-key presence |
| battery, x=91 / token x=104 | fixed battery outline plus one-decimal volts, or `--` | cached board reading |

Battery remains volts, not percentage: a percentage requires an unapproved chemistry/discharge-curve policy.

The rail has five stable semantic slots — STATUS, TEAM, INBOX, SEND, SETTINGS. TEAM/SEND may be absent in a build,
but their slots do not collapse. The selected slot is outlined with `draw_rect`; icons use `draw_bitmap`. Inbox
detail retains INBOX, compose/outcome retains SEND (or TEAM for a peer compose), and settings editing retains
SETTINGS. A frame freezes one `UiChrome` snapshot so the strip, rail and body cannot describe different moments.

Configuration state belongs on the SETTINGS rail icon, not in the status body. Its four visual states are clean,
unsaved, conflict and restart-needed, with priority **conflict > unsaved > restart > clean**. The distinct service
predicates remain `config_unsaved`, `conflict` and `reboot_required`; `UiState::dirty` still means only
“repaint owed”. Actionable text such as `RELOAD`, `SAVE FAILED` and `RESTART NEEDED` remains in SETTINGS.

STATUS remains the detail view: expanded home/team ages and identities, our own team local id, configured `team_id`,
registration state and battery detail may live there without duplicating the compact strip.

TEAM shows one row per teammate: a display label resolved through `team_key_of_id()` → `peer_name_find()`, falling
back to `0x<hash>` and then the bare team id; plus last-heard age, signal quality and hops. When `rt_team_count()`
exceeds `kMaxTeamRows`, the screen shows the true total and a truncation marker (`3/12`) — it must never present
the cap as the team size. **Phase B adds a distance column on V4**, rendered only when both our fix and the peer's
location are known and fresh — omitted, never estimated (§10.3).

⛔ **SUPERSEDED:** the pre-CHROME implementation used a full-width 21-column body, a textual
`DM… CH… B* …V` top row, and `CFG* UNSAVED` / `CFG! RELOAD` on STATUS. Those placements are historical and must
not be reintroduced. UI-12 may add BLE status later only through a separately approved slot/layout change.

### 3.4 Direct messages to a teammate · ✅ implemented

Sent as `send <team_local_id> "<text>" -t -a`. Every part of that already exists — `console_parse.cpp:305-329`: a bare decimal ≤254 is an id, `-t` selects `Plane::TEAM`, and `-a` is accepted on the id form (`allow_a=true`). No new firmware surface.

**`-a` buys something the channel path cannot offer.** A DM yields `PushKind::send_e2e_acked` — *the destination received it*, per peer, as distinct from the link ack. So "Are you OK?" can report **delivered to that person**, where a channel post can only ever report `PICKED UP`. The compose sub-view surfaces that outcome the same way the emergency screen does.

**Cleartext, by owner ruling 2026-07-31 — with one caveat that must be stated rather than assumed.** These DMs carry no `-e`; the flag is gated `allow_e = by_hash` (`console_parse.cpp:313`) and is not even accepted on an id target. The command therefore leaves `crypt = CryptIntent::def`, which follows the node's `e2e_dm` setting:

| node state | result |
|---|---|
| `e2e_dm = 0` (the shipped default, and what the bench nodes run) | plaintext — the intended behaviour |
| `e2e_dm = 1` **and** the peer's full Ed25519 pubkey is held | sealed |
| `e2e_dm = 1` **and** no pubkey | **fails loud** `no_pubkey` — nothing is sent |

⚠ **The UI cannot force plaintext**, and should not try: `CryptIntent::off` was deliberately removed from the console ("force-plain dropped", `console_parse.cpp:328`). A node its owner configured for encryption must not be silently downgraded by a button press. So "cleartext" here means *"we do not ask for encryption"*, not *"we guarantee no encryption"* — the accurate framing, and it is the correct behaviour.

The `no_pubkey` case is a genuine dead end on-device: the 2026-07-29 ruling forbids the node ever auto-issuing `reqpubkey`, so the user cannot resolve it from the panel — it needs a QR ceremony or a typed `reqpubkey`. The sub-view must therefore report that failure plainly (`NO KEY`) rather than showing a generic failure the user cannot act on.

### 3.4.1 DM outcome states · ✅ implemented

A DM needs its own small outcome machine — separate from the emergency one, correlated by `ctr` **and** peer per §2.1, and never advanced by an unrelated push:

| state | entered when | shown |
|---|---|---|
| `submitting` | the command was handed to `dispatch()` | `SENDING...` |
| `refused` | the synchronous `CmdResult` was an `err_*`, or the parser rejected it | the reason, terminal |
| `waiting_ack` | accepted, `ctr` recorded (`-a` was set) | `SENT, waiting` |
| `delivered` | `send_e2e_acked` with matching `ctr` **and** `dst` | `DELIVERED to <label>` |
| `no_key` | `send_failed{no_pubkey}` matching | `NO KEY` |
| `not_confirmed` | `send_failed{e2e_ack_timeout}` matching | `NO CONFIRM` |
| `failed` | any other matching `send_failed` | a compact reason |

The tracker must therefore carry the **whole `SendFailReason`** into the model, not an `acked`/`no_pubkey` pair of booleans — with only those two, `not_confirmed` is unreachable and every other reason collapses to a `failed` the user cannot act on.

**Late acks:** the core deliberately permits `send_e2e_acked` to arrive after `e2e_ack_timeout` (`command.h:160`). Phase A **upgrades** `not_confirmed` → `delivered` if a matching late ack arrives while the sub-view is still showing — the truth is worth more than the tidiness, and the correlation data is already retained.

The sub-view closes to its parent on an explicit `double`, or after a bounded display window. `delivered` is the one place in this design where the word **DELIVERED is accurate** — it is a genuine end-to-end ack, unlike a channel post's `PICKED UP`.

### 3.5 Inbox message detail and delete (owner extension 2026-08-05) · ✅ implemented 2026-08-13 (§UI-7D slice B)

> **AS BUILT — slice B, 2026-08-13.** Everything below is implemented as written; the paragraphs are kept in the
> requirement's own words. ⛔ **CORRECTED IN PLACE: the opening sentence used to say the extension was NOT satisfied and
> that "a double press has no inbox action" — both are now false.** The identity rule is enforced at three sites (the
> preview snapshot's `(InboxKind, seq)`, the activation, and the `erase()` call), `InboxRow::text` remains a 20-character
> RENDERING field, and the model asks while `src/firmware_ui.cpp` performs. ⚠ **[[B134]] BOUNDS WHAT "DURABLE" MEANS ON
> THIS BOARD:** on every ESP32 target, `heltec_v3` included, the inbox is a **volatile RAM ring**, so `erased` means the
> tombstone was appended and the record is gone from every future `pull()` **within that runtime**. ⛔ No power-loss
> durability is claimed by the code, the tests or the panel — and ⛔⛔ **note which way that cuts: a reboot destroys the
> record, its tombstone and the ENTIRE inbox together, so a deleted message CANNOT reappear and *"it survived the reboot"*
> would be a VACUOUS pass on this board.** Cross-reboot delete durability is owed by a **durable** backend and is
> qualified as such at §6.2's criterion; the volatility itself is pinned by bench Part 11.4.
> ⓘ **Two measured consequences, recorded rather than smoothed over:** (1) the panel retains only
> `kInboxRowsPerKind` = 4 rows **per kind** (§6.1's bound), so a record outside that window cannot be selected — and
> therefore cannot be deleted — from the panel; the console verb `del_msg` remains the way to reach it ([[B191]]).
> (2) After the terminal `MESSAGE GONE` modal closes, the rebuilt LIST also shows `MESSAGE GONE`, because the selection
> it was tracking is likewise gone from the store. That is honest, not a leak.

The landed UI-7 inbox was a bounded preview list: a double press had no inbox action and `InboxRow::text` retains only
20 display characters. That did **not** satisfy this extension. A `double` on a highlighted DM or channel row opens
one detail modal containing the complete stored body and exactly two selectable actions:

```
+---------------------------------------+
| DM from 48                     1/6    |
| first wrapped body line               |
| second wrapped body line              |
| > back                                |
|   delete                              |
+---------------------------------------+
```

For a channel row the header is `CH<n> from <origin>`. The body is copied from the selected `InboxEntry`, wrapped
without dropping bytes, and held in a fixed `inbox_max_body + 1` buffer for the modal's lifetime; it must never render
or dereference the callback-owned `InboxEntry::body` after `pull()` returns. Unsupported display bytes are replaced
visibly, never treated as control characters.

Two body rows expose **38 characters per page** in the implemented 19-column body, so the maximum 241-byte inbox
body needs at most **seven pages**. Long bodies
advance automatically every 2 s and cycle while the modal remains open. A page change marks the model dirty but does
not reset the user-inactivity deadline, and every resulting repaint still obeys §5's MAC-idle/page-buffer gate. Short
press toggles the action selection; double activates it. `back` is selected initially, so deletion requires the
deliberate sequence **short → double** after opening the message.

- `back`: close the modal and return to INBOX without changing storage.
- `delete`: request durable deletion of this exact record. On success close the modal, rebuild the list and preserve
  the neighbouring selection where possible. On `not_found`, show `MESSAGE GONE`—the bounded store may have evicted
  it meanwhile—and never affect another row. On storage failure stay in the modal and show `DELETE FAILED`; a visual
  disappearance without durable success is forbidden.
- long press closes the detail modal before arming emergency; the hidden Delete action cannot survive underneath an
  emergency overlay. Ordinary modal timeout returns to INBOX without deleting.

Selection identity is **`(InboxKind, seq)`**, not the visible row index, origin, message counter or body. DM and
channel sequence spaces are independent, so `seq` alone is insufficient. The preview snapshot must carry this pair;
activation re-finds the exact record and copies it. If a refresh moves rows, the highlight follows the identity; if
the record disappears, activation refuses with `MESSAGE GONE` rather than opening or deleting its replacement.

### 3.6 On-device settings and provisioning (owner extension 2026-08-06) · 📝 not implemented, EXCEPT §3.6.1's service

This extension changes the product scope: a Heltec mobile should be able to create a team without a phone, and should
be able to join a static network from a prepared profile. It also introduces finite-choice settings on the panel.
It does **not** retroactively make UI-1…UI-9 incomplete.

#### 3.6.1 Persistence model: persisted, effective and draft are three different states · ✅ implemented 2026-08-13 — the SERVICE (§UI-13) **and its screen + device binding (§UI-14)**
⛔ **CORRECTED IN PLACE 2026-08-13 (QG round 2): this heading read *"implemented … (§UI-13) — the SERVICE only, headless"*, which was true for one slice and is now misleading — §UI-14 landed the renderer AND the `ICfgStore`/`ICfgLive` device binding. ⚠ Still true and NOT corrected away: [[B193]]'s real-flash / power-cut qualification is OPEN.**

★ **Where it landed:** `src/firmware_config_service.h` (`ConfigService`, `CfgValues`, `ICfgStore`, `ICfgLive`,
`CfgOpen`/`CfgSet`/`CfgSave`/`CfgRefresh`), tests in `test/test_firmware_config_service.cpp`. The paragraphs below are
the normative text and are unchanged; two points they leave open are recorded rather than invented:
**(a)** `RELOAD` is named but not defined in the paragraphs below. ✅ **OWNER-RULED 2026-08-13** ([[B192]], ledger
§1.22), in reported form: **RELOAD performs the THREE-WAY MERGE — fields UNCHANGED in the OLED draft adopt the current
persisted values, fields EDITED in the draft remain unsaved in the draft, and DISCARD remains the explicit full
reset.** That is what `ConfigService::reload` implements, so no code changed; ⛔ do not re-open it. The reason it is
not "reinstate the whole draft" is why it was ruled so: that would be the last-writer-wins this same paragraph
forbids. ⛔ **CORRECTED IN PLACE 2026-08-13: this item read *"That shape is a design choice, not a ruling — registered
as an owner decision"*, which was true when written and is now false.** ⚠ **The ruling settles BEHAVIOUR only — the
NV / power-cut qualification remains deferred to the UI-14 device binding and [[B193]].**
**(b)** the covered set is the four fields §3.6.2 lists that are already durable; ⛔ no live-only field was promoted.

The existing console contract is mixed by design: most `cfg set` keys write `/mrcfg` immediately, some are live-only
and revert at reboot, identity fields write `/mrid`, and `join`/`create`/`team` are provisioning operations. A single
boolean inferred from `NodeConfig` cannot describe “unsaved”. The OLED therefore owns an explicit `ConfigDraft`:

- opening SETTINGS snapshots only the supported persisted fields and records a baseline fingerprint;
- changing a row changes the RAM draft only—no radio retune, live mutation or flash write occurs;
- `config_unsaved` is true iff the draft differs from that baseline. Do not call it `dirty`: `UiState::dirty` already
  means “a repaint is owed”;
- `SAVE` validates the whole candidate, writes it once, and only then applies live-capable fields. A save that needs a
  reboot sets `reboot_required`; it is still durably saved and no longer unsaved;
- `DISCARD` reloads the persisted values and clears the marker. `BACK` and blanking preserve the draft; silently
  discarding because attention timed out is forbidden. A power loss intentionally loses an unsaved RAM draft;
- a no-op save performs zero NV writes. A failed write keeps the old effective/persisted state, keeps the draft and
  marker, and shows `SAVE FAILED`.

Serial/BLE retain their existing immediate-write behavior for compatibility. If either changes a covered field while
an OLED draft is open, the baseline fingerprint no longer matches: show `CFG! RELOAD`, refuse SAVE, and require
`RELOAD` or `DISCARD`. Last-writer-wins would silently overwrite companion changes. Runtime changes such as routes,
registration, battery and unread counts never set the marker.

✅ **THE IMMEDIATE HALF LANDED 2026-08-13 (§UI-14 follow-up), and it was genuinely missing until then.** §UI-13 built
`note_external_write` and nothing called it, so a companion write was discovered only at the panel's next SAVE — which
covers a *standing* change (`save()`'s gate 2b re-reads `/mrcfg`) but ⛔ **not `change → external REVERT → SAVE`**: by
save time the bytes match the baseline again and, unnotified, the latch was never raised. ⇒ a fourth **feature-neutral**
hook, `mr_ui_on_config_saved()` (`lib/hal/mr_ui.h`, inline no-op off the OLED profile), called by `handle_cfg_set`
**after a write that both happened and succeeded**; the OLED half re-reads the record, notifies the service, and asks
for a repaint **only when the latch changes** — required, because `FrameGate` skips a clean model, so a latch raised
without it would be true and invisible.

✅ **AND THE RULE COMPLETED 2026-08-13 (§notify-every-save).** ⛔ **CORRECTED IN PLACE: the paragraph above used to end
*"Still not wired … the provisioning verbs and the channel-ctr lease also write `/mrcfg` and do not notify"*. THAT IS
NOW FALSE for the user-initiated verbs and is withdrawn here, not deleted.** **SEVEN user-initiated verbs now notify** —
`cfg set` · `gateway` · `join` · `create` · `team` · `leave` · `password` — after a write that both happened and
succeeded. ★ The one that made it a blocker rather than polish is **`leave`**: it rebuilds the record from a zeroed
`mrnv::Blob`, so it **resets all four covered fields to 0** and persists them, which is the largest covered-field
change any verb makes. ★★ It is a RULE rather than a per-field judgement because the alternative — notify only where a
covered field provably moves — makes every future writer re-derive the field table, and one that forgets is silently
non-compliant; it is safe because the OLED side compares **only** the four covered fields, so a save that moved nothing
covered raises nothing. ⛔ **The INTERNAL writers stay silent, measured rather than assumed:** `fw_main`'s ctr-lease /
join persist and leaf-config adopt, and `firmware_remote`'s admin counter-floor / pubkey-rotate writes assign **none**
of the four, are not user-initiated, and the lease fires on a timer. The rule, the seven sites and that measurement are
recorded at `§notify-every-save` in `src/firmware_config.cpp`.

Implement this behind a typed configuration service shared by serial, BLE and OLED. The OLED must not loop through
`handle_cfg_set` or manufacture command strings: that would apply/save fields one at a time, expose partial success,
and make atomic validation impossible. Only fields already represented durably may appear in the first SETTINGS
editor; promoting a live-only field requires its own NV-schema slice.

#### 3.6.2 First SETTINGS menu · ✅ implemented 2026-08-13 (§UI-14)

Only finite-choice, recoverable values belong in the one-button editor:

| row                          | values                           | commit behavior                                                                                                                               |
| ---------------------------- | -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| BLE mode                     | `off` / `on`                     | persisted, reboot required; row absent when UI-12 transport is not compiled. note - periodic will be obsolete and to be removed from firmware |
| DM encryption default        | off / on (`e2e_dm`)              | persisted and live                                                                                                                            |
| first-contact key attachment | off / on (`intro_attach`)        | persisted and live                                                                                                                            |
| mobile auto-registration     | off / on (`mobile_autoregister`) | persisted and live                                                                                                                            |
| PROVISION                    | opens §3.6.3                     | immediate operation, never a draft field                                                                                                      |
| SAVE / DISCARD / BACK        | actions                          | as §3.6.1                                                                                                                                     |

★ **Where it landed, and the three points the table left open, recorded rather than invented:**
**(a)** the **BLE mode row is ABSENT in every env in the tree** — its condition is the UI-12 transport, which does not
exist; it is a build flag (`MR_UI_BLE_ROW`, `src/firmware_ui.cpp`) defaulting to the absent arm, and the model takes it
as a snapshot field so both arms are tested. **(b)** the **PROVISION row is PRESENT and INERT**: it is a row of this
menu, so it is rendered, and activating it refuses out loud (`PROVISION: UI-15`). ⛔ §3.6.3's *"if a settings draft is
unsaved, PROVISION first requires SAVE or DISCARD"* is **not** implemented — it is a rule about a flow that does not
exist. **(c)** a **RELOAD row appears exactly while `conflict()` stands**: §3.6.1 requires a conflict to be resolved by
*"RELOAD or DISCARD"* and this table lists neither, so without it the only escape would be DISCARD — which throws away
the operator's edits, the cost [[B192]]'s ruling explicitly declines to charge them. ⓘ The menu's `ble_mode` cycle
offers **off/on only**, per the table's own note; a persisted `periodic` is still RENDERED honestly, and the service's
0..2 domain is **not** narrowed (serial/BLE still write it).

⛔ **CORRECTED IN PLACE 2026-08-20 ([[B232]]): the screen LANDS CLOSED.** Arriving on SETTINGS shows ONE row,
`ENTER SETTINGS`; a `double` opens the table above and a `short` passes the screen. ★ **The `ConfigService` still
OPENS ON ARRIVAL** — §3.6.1's baseline, the `CFG! RELOAD` latch and §6's rail badge are all comparisons against it,
so deferring the open to the menu would leave a node whose operator merely glanced at SETTINGS with no baseline at
all. ★ And §6's rule holds **from the closed view**: `CFG* UNSAVED` / `CFG! RELOAD` / `RESTART NEEDED` are printed
there, not only inside the menu. ⛔ **When the service CANNOT open (`CFG UNAVAILABLE`), a `double` leaves the screen
CLOSED** — there is no menu to enter, and opening an all-hidden row walk would re-create the multi-press trap one
double deep (the QG-caught arm). `BACK` leaves the **menu**, not the screen; it still preserves an unsaved draft.
`short` advances rows or cycles a finite value while editing; `double` enters/accepts the highlighted row. `BACK` is
safe and preserves an unsaved draft; `DISCARD` is a separate deliberate action. The long gesture always leaves the
editor and arms emergency. Arbitrary text, frequency digits, transmit power, duty, OTA/GPS controls, volatile debug
knobs and privacy-unsafe `team_channel_crypt=off` are excluded from this first menu.

Preset **text** remains configurable through authenticated BLE or serial (§3.2.2–3); the OLED selects configured text
but does not type it. That boundary is why “configurable presets” and “SETTINGS screen” are separate slices.

#### 3.6.3 Provisioning operations

Provisioning is not part of SAVE/DISCARD because it changes identity/membership and begins network activity. If a
settings draft is unsaved, PROVISION first requires SAVE or DISCARD.

**Create team — primary path.** `CREATE NEW TEAM` opens a confirmation with `BACK` selected initially; reaching CREATE
requires `short` then `double`. If already in a team, the screen says the current membership will be replaced. Generate
the candidate team id and keys in temporary state, durably save the complete candidate, then apply it live. On NV
failure the old team remains live and stored; “team is LIVE but NOT persisted” is unacceptable on this UI path.
Success shows the full new team id plus the same short fingerprint used by nearby onboarding. The primary no-phone
distribution path is the explicit nearby-member approval in §3.6.4. Authenticated BLE/serial export and the existing
companion QR remain alternative provisioning paths; the first OLED version does not need to render a private-key QR.

**Join static network — secondary path.** Free-form entry of `layer/freq/bw/sf` is not credible with one button, so the
first version selects one of four fixed-capacity join profiles. Each profile contains the exact existing `join`
inputs: full `layer` byte, frequency in kHz, bandwidth in Hz and routing SF. Profiles are configured and listed through
one shared serial/BLE handler and stored in a separate versioned `/mrjoin` record; corruption cannot reset `/mrcfg`,
identity, team keys or presets. OLED shows the complete values before confirmation, with BACK selected initially.

After confirmation, use the existing join validation and DAD semantics: persist the join candidate before applying it
live, then show `JOINING`, adopted/refused, and the resulting node id. B132—gateways must never host mobiles—is a
prerequisite for trusting the resulting registration, not for rendering the screen. A completely fresh device with no
profile can still create a team, but cannot join an arbitrary RF domain from the button alone; §14 asks whether a
future numeric editor or QR/profile import is required.

#### 3.6.4 Join a nearby team without typing its id

**Owner ruling 2026-08-06:** the random 32-bit `team_id` remains the authoritative identity. It must not be derived
from a human name. A name-derived id would make duplicate names and spelling changes identity operations, would make
renaming unsafe, and still would not distribute the independent team content key. A team label is optional metadata;
duplicate labels are allowed and changing one never changes membership, keys, routes or `team_id`.

The primary no-phone join path is `PROVISION → JOIN TEAM → NEARBY`. Its first version reuses the team id already
carried by team beacons and therefore adds no new team identity, routing rule or key format:

1. On an existing member, `INVITE MEMBER` opens a bounded invitation window and displays the team label when one is
   configured, plus a short fingerprint of the full random `team_id`. It does not transmit the content key.
2. The joiner listens on its **current effective PHY**, collects recently observed non-zero team ids and shows a
   de-duplicated list with the id fingerprint, signal strength and age. The observation path is read-only: scanning a
   foreign team must not write team routes, peer bindings, membership, keys or NV. A label may be shown only if it came
   from an authenticated local profile or a future specified invitation carrier; it must never be guessed from an id.
3. `double` on a candidate opens `JOIN <fingerprint>?` with **BACK selected initially**. Confirmation selects the exact
   full `team_id`, not the visible list index or truncated fingerprint, and uses the same role, PHY, team-DAD and
   persistence validation as the existing guarded `team <id>` operation. The candidate is committed atomically before
   live apply; failure leaves the previous membership and key intact.
4. The joiner is now a **keyless team member**. The team id is already public in beacons and is not an access secret;
   joining it must not imply possession of the team content key or the ability to read encrypted channel posts.
5. Opening invitation mode snapshots the creator's currently known team-member identities. While it remains open, the
   existing member lists candidates first observed after that snapshot by team-local id plus a short identity-hash
   fingerprint. The current wire does **not** attest whether another member holds the team content key, so the creator
   must call these `NEW MEMBER` candidates, never `KEYLESS`. The wearer selects `GRANT KEY` or `REJECT`, again with the
   safe action selected by default. The fingerprint is a human selection aid, not cryptographic authentication and
   never substitutes for a verified public key.
6. `GRANT KEY` first resolves the selected member's authoritative public key, then reuses the existing sealed
   `team grantkey` payload and send path. No private team key is broadcast or rendered. Until the public key is usable,
   show `WAITING FOR KEY`; a timeout or resolution conflict grants nothing. The creator may honestly show `KEY SENT`
   when the existing send contract accepts it, but must not claim `JOIN COMPLETE` without a receiver acknowledgement.
   The joiner shows `TEAM KEY RECEIVED` only after durable adoption succeeds.

Invitation mode expires automatically and emergency pre-empts it. Expiry closes the approval UI but does not expel an
already joined keyless member; team-id membership is as public and permissive as the existing typed-id path. Closing or
timing out an invitation never grants, revokes or rewrites a key. A newly seen member outside an active invitation must
not produce an unsolicited one-button grant prompt.

**PHY boundary:** the beacon-only first version discovers teams already audible on the current PHY. It must say
`CURRENT PHY ONLY`, rather than claim a general radio scan. Cross-PHY onboarding uses an authenticated BLE/serial
profile import until a separate bounded scan/invitation-carrier design exists. A future invitation carrier may add a
team label, PHY profile, expiry and nonce, but it remains an onboarding envelope around the same random `team_id` and
sealed key grant—not a replacement identity mechanism.

For spoken/serial fallback, the UI may render a lossless Crockford-Base32 representation of the full random id plus a
checksum. This is an encoding of `team_id`, not a shorter identity. Manual one-button character entry is not the
primary flow and remains out of scope for the first version.

#### 3.6.5 Interruption and safety

- Emergency pre-empts SETTINGS, confirmation and provisioning-result screens. A draft survives; an unconfirmed
  destructive action does not.
- While a durable save/provision commit is executing, button input may update emergency intent but the storage call
  must itself be bounded and outside a radio-critical interval. No screen may claim success before the save returns.
- Reset/power-cut fault injection must leave either the complete old record or complete new record; never a half team,
  half profile, or a live configuration that cannot survive reboot.
- Team creation and join produce explicit success/failure states and are never triggered by the waking press.
- Nearby scan, invitation expiry and candidate-list refresh never select, join or grant by themselves; every state-
  changing action has an explicit confirmation with the safe action selected initially.
- A saved-but-reboot-required state stays visible until reboot; it is not the same as an unsaved draft.

## 4. Emergency state machine · ✅ implemented; 🧪 metal qualification in progress

★★ **DERIVED FROM THE SHIPPED CODE 2026-08-06 ([[B130]]), not from the earlier diagram** — the model
(`src/firmware_ui_model.h`: `emergency_gesture` · `on_outcome` · `on_send_refused` · `on_reply` · `tick_emergency`)
and the headlines (`src/firmware_ui.cpp`'s `draw_emergency`, all eight non-`idle` arms). ⚠ **Every threshold is
NAMED, never restated** — the constants' own declarations are the only place the digits belong ([[B120]], §B78).

```
idle --long_arm (InputCfg::arm_ms)--> arming        panel: "RELEASE!"  + "EMERGENCY IN <n>"
  arming: release before kArmToFireMs   -> cancelled  "CANCELLED"     -> idle after kCancelledMs
          held through kArmToFireMs     -> firing     (long_fire: budget, backoff and evidence all RESET)
firing: send_channel <MR_UI_TEAM_CHANNEL_ID> "<emergency preset>" -t -e [-l only when a fix exists — §4.1]
    synchronous refusal (parser / err_*) -> failed     "FAILED"      + compact reason [+ CmdCode]   [terminal, retained]
    blocked{next_ms}                     -> blocked    "BLOCKED"     + "retry in <n>s"   [re-fires; consumes NO attempt]
    channel_relayed                      -> picked_up  "PICKED UP"   + "a relay heard it"          [sticky]
    channel_failed{reason}               -> failed     "FAILED"      + the reason        [terminal, NEVER retried]
    channel_no_relay | channel_remote_mint:
        _tries <  kEmgMaxTries           -> firing     (the next attempt)
        _tries >= kEmgMaxTries           -> not_heard  "NOT RELAYED" + "no relay after <n>"  (evidence local_tx)
                                                                     / "unconfirmed x<n>"    (evidence no_handle)
                                                                                               [sticky]
blocked: at retry_at_ms                  -> firing
reply — ⛔ NOT "any state". Only from firing · blocked · picked_up · not_heard · reply, AND only with at least one
        ACCEPTED transmission (`_tries != 0`), AND only for a post that passes §4.4's team scope
                                         -> reply      "REPLY"       + "<who>: <text>"   [sticky; UN-BLANKS, §R1]
exit: sticky until a SHORT press, and only once the outcome has actually been PRESENTED (B71 + §B102);
      `long` re-fires a NEW alarm; `double` does NOTHING (R2).
```

> ⛔ **SUPERSEDED — the original diagram, kept as audit trail (§3 rule 3 of the rulings ledger), NOT as guidance.**
> Four things in it were wrong by the time it was read, and [[B130]] is that it stood as live instruction for a day:
> `NOT HEARD` is no longer the headline (**`NOT RELAYED`**, B117/ledger §1.2); a reply is **not** accepted from "any
> state"; the sticky exit is a **short** press, not `double` (B71); and `failed` — a whole terminal arm — was missing.
> ```
> IDLE --hold 800ms--> ARMING
>   ARMING: "RELEASE TO CANCEL / EMERGENCY IN 3..2..1" drawn while held
>     release < 3.5s ------> CANCELLED (brief toast) -> IDLE
>     held through 3.5s ---> FIRING
> FIRING: post the encrypted team channel message "I'm in danger" (+ location if a fix exists)
>     send_blocked{next_ms}          -> BLOCKED   (show "retry in Ns", auto-retry at next_ms)
>     channel_sent{relayed=true}     -> PICKED UP
>     channel_sent{relayed=false}    -> NOT HEARD (auto-retry x3 w/ backoff) -> NOT HEARD (sticky)
> any state: inbound channel msg from a teammate -> REPLY: <name> <text>
> sticky until acknowledged (double)
> ```

Two constraints the draft could not have anticipated, both verified:

- **`send_blocked` must be handled.** `channel_min_interval_ms` (`lib/core/protocol_constants.h`, declaration only — ⚠ **do not restate its value here**, [[B120]]) means a second emergency post inside that floor is refused **pre-TX** with `send_blocked{min_interval, next_ms}` (`command.h:103, 177-180`). A safety UI that displayed "sent" there would be lying. Show the countdown; auto-retry at `next_ms`.
- **Delivery evidence is weaker than "delivered", and the wording must say so.** Team channel messages carry no end-to-end ack — there is no `DATA_FLAG_E2E_ACK_REQ` anywhere in `node_channel.cpp`. The only signal is `PushKind::channel_sent` (`command.h:105`, `node_channel.cpp:403-416`): `relayed=true` means a neighbour was overheard re-flooding the post. Render that as **`PICKED UP`**, never `DELIVERED`. True human confirmation arrives only as a teammate's reply — which is precisely why "Got your message" earns slot 4.

**Auto-retry bound: exactly `kEmgMaxTries` accepted transmissions**, then the sticky `not_heard` state — headline **`NOT RELAYED`** — which the user re-fires with a **`long`** press. Unbounded retry is not acceptable — it would burn the duty budget the rest of the team needs to answer.
⛔ **This sentence used to say "3 … a sticky `NOT HEARD` … re-fire with `double`" and all three halves were wrong as live guidance** ([[B130]]): the bound is the named constant, the headline is `NOT RELAYED` (B117), and B71 **withdrew both of `double`'s emergency duties** — under the overlay `double` does nothing at all (R2).

★ **`kEmgMaxTries` counts *transmissions*, not retries after the first.** Both readings appeared in an earlier draft; this is the binding one and the native tests pin it. ⓘ The constant's declaration (`src/firmware_ui_model.h`) is the only place its value belongs ([[B120]]).

★★ **An attempt is counted on ACCEPTANCE, never on request.** A parser refusal or a pre-TX `send_blocked` puts nothing on the air, so it must not consume one of the three alarms. The counter increments when `CmdResult` comes back accepted with a `ctr`.

> ⚠ **FACTUAL CORRECTIONS 2026-08-05 (two owner rulings + one measured defect; shipped behaviour, no redesign).**
> ① **The terminal panel headline is `NOT RELAYED`, not `NOT HEARD`** (register B117, owner-ruled 2026-08-05).
> `NOT HEARD` overstated its evidence: what is measured is that no relay transmission was overheard, but a user in
> distress reads it as "nobody received it", and on the B114 bench run those readings diverged and the misleading one was
> wrong. `NOT RELAYED` states the measurement and implies nothing about receipt. ⓘ **Width, measured:** `Font::large` is
> `u8g2_font_10x20_tf` = 10 px/char on a 128 px panel = **12 columns**; `NOT RELAYED` is 11 chars = 110 px, one column
> spare. The FIRST ruled wording was `NO RELAY HEARD` — 14 chars = **140 px** — and would have been clipped to
> `NO RELAY HEAR`, so it was not shipped. ⛔⛔ **CORRECTED IN PLACE 2026-08-05: this paragraph used to end "so the owner
> approved the shorter form". THAT APPROVAL DID NOT EXIST** — the 8-char `NO RELAY` that briefly shipped was substituted
> by the implementing slice, which reported an approval it had invented; refusing to truncate was right, choosing a
> different string was the owner's call and was taken without them. `NO RELAY` is superseded and was never sanctioned.
> ⛔⛔ **CORRECTED IN PLACE 2026-08-06 ([[B130]]) — THIS PARAGRAPH USED TO CLAIM SOMETHING UNTRUE ABOUT ITS OWN
> DOCUMENT.** It said: *"Every `NOT HEARD` elsewhere in this spec names the model STATE (`Emergency::not_heard`), which
> is unchanged — only the rendered string moved."* **That claim was FALSE and was never verified.** Counted at the time
> of the correction, the spec carried **nine** occurrences of the display-form string `NOT HEARD`: three inside this
> correction block (legitimate — they name the superseded string in order to supersede it) and **six in live guidance**
> — §2.1 rule 2, the §4 state diagram, the §4 retry-bound sentence, the §12 on-target checklist, the §12 `unsealable`
> acceptance case and the §13 **B38** prerequisite row — **not one of which spelled the enum.** All six are now either
> the enum `not_heard` (where the model state is meant) or **`NOT RELAYED`** (where the panel headline is meant).
> ★ The correct statement, which is the one this note should always have made: **the RENDERED headline is
> `NOT RELAYED`; the MODEL STATE is `Emergency::not_heard` and is unchanged** — so a sentence about the panel must
> never spell it `NOT HEARD`, and a sentence about the machine must never spell it in display case at all.
> ② **The counter above is displayed through a SEPARATE presentation ordinal** (register B115, measured on metal). The
> counted value `_tries` — the single source of truth for this bound, still moved only on acceptance — is *not* what the
> panel shows: a `ctr == 0` attempt is in flight while deliberately uncounted (§2.1 rule 2), so the panel's "attempt N
> of 3" is `_tries` once accepted and `_tries + 1` while uncounted. The shipped renderer added `+1` unconditionally and
> displayed `2 of 3` → `3 of 3` → `4 of 3` against three posts. ⛔ The ordinal must never gate a send.
> ③ **A direct DM does NOT confirm an alarm** (register B114 matter ②, owner-ruled). §4.4's channel scope is the ruled
> design, not a gap: widening it would re-open the surface §4.4's own §B103 correction narrowed, and a DM's `team_id` is
> not the channel-post team tag. The `HAVE`-digest evidence source is register B116, unimplemented.

**Retry timing, corrected after review:**

- The deadline is `now_ms + next_ms`, computed from **the moment the block outcome arrived** — not from the originating gesture. An earlier draft based it on the gesture timestamp, which is stale by the time an automatic retry blocks.
- Comparisons are wrap-safe unsigned (`now - deadline` sign trick), not `now >= deadline`.
- **`next_ms == 0` is legal and means "the floor has passed but a cap or duty limit still blocks"** (`command.h:203`). It must not be treated as "retry immediately": doing so spins the retry every tick and burns the whole `kEmgMaxTries` budget in milliseconds. Phase A policy: a UI-side recheck backoff starting at `kBlockedBackoffMinMs`, doubling, capped at `kBlockedBackoffMaxMs`, staying in `blocked` and **consuming no attempt** until a send is actually accepted. (⛔ the digits used to be restated here — [[B120]]; `src/firmware_ui_model.h` owns them.)

### 4.2 Emergency gestures pre-empt everything

`long_arm` / `long_fire` / `long_cancel` are handled **before** blank-wake consumption and **before** compose-modal dispatch. An earlier draft checked the modal first, which silently swallowed all three inside both sub-views — directly contradicting "from any screen, sub-views included". The order is: long gestures → blank wake → modal → normal navigation.

### 4.3 Time-based transitions are explicit model state

Every timed transition is a field, so the renderer never computes deadlines and the native tests can drive them:

| field | drives | reset by |
|---|---|---|
| `arming_fire_at_ms` | the countdown digit, derived as `(fire_at - now)/1000` | `long_arm` |
| `cancelled_until_ms` | `CANCELLED` auto-returns to the parent after ~1 s | `long_cancel` |
| `emergency_hold_until_ms` | the capped panel-on window (§5) | `long_fire`, and **every retained outcome** — `picked_up`, `not_heard`, `blocked`, `reply` |
| `retry_at_ms` | the blocked-retry deadline | a `blocked` outcome |

★ **`emergency_hold_until_ms` must actually be READ by the blanking rule.** It is a deadline, not a duration: the panel stays on until `now >= emergency_hold_until_ms`, compared wrap-safely like the retry deadline. An earlier plan draft wrote the field on fire and on reply but then measured the hold from *last input* instead — so a reply that arrived while the user's hands were elsewhere never extended the window it had just set, and `picked_up` fell back to the ordinary `kBlankMs` blanking.

The model marks itself dirty **only when the visible countdown digit changes**, not every tick — otherwise the emergency repaints at the full tick rate for no visual difference.

### 4.4 Human confirmation — the `REPLY` state

`PICKED UP` is relay evidence. The only *human* confirmation is a teammate answering, so an inbound team channel post arriving while the emergency is live or sticky transitions to a sticky `reply` state carrying a bounded sender label and a display-clamped body.

**Scope: a post qualifies only if it is on `MR_UI_TEAM_CHANNEL_ID` *and* comes from OUR OWN TEAM.** Accepting any traffic on that channel would let unrelated chatter read as "someone answered my distress call", which is the same false-confirmation class as §2.1.

> ⚠ **FACTUAL CORRECTION 2026-08-05 (§B103 / register B103 — shipped behaviour, narrow correction only, no redesign).** This paragraph originally said the channel id **alone** qualified a reply, and the code shipped that way. It was a live safety defect: `Node::ingest_channel_m` (`lib/core/node_channel.cpp:211-212`) drops a *foreign team's* post, but a normal leaf post (`team_id == 0`) **falls through and is ingested by everyone** — so with `MR_UI_TEAM_CHANNEL_ID == 0` any node in radio range posting plaintext on channel 0 rendered as a distress REPLY. The shipped guard is now `pu.channel_id == MR_UI_TEAM_CHANNEL_ID && g_node.same_team(pu.team_id)`, and the clause carrying the safety weight is `same_team`'s implicit `team_id != 0` — not the channel equality, which ingest already guarantees for team traffic. ⓘ Consequence, deliberate: on a node with **no** team the REPLY indication is unreachable, because without a team there is no key and no membership and so nothing that could make a reply trustworthy.

★ **And only after an alarm was actually transmitted.** The state whitelist is `firing` · `blocked` · `picked_up` · `not_heard` · `reply`, and **at least one emergency transmission must have been accepted**. `arming`, `cancelled` and `failed` are excluded: in all three, nothing went out, so a coincident channel-0 post becoming `REPLY` would manufacture a confirmation of a message that was never sent — including during the `kArmToFireMs` hold *before* the user has even committed.

### 4.1 ★ Location on the distress call (owner ruling 2026-07-31)

Per `2026-07-30-channel-crypt-and-location-privacy-design.md` §2.2.1, an encrypted channel post carries **text and location together** — the sealed inner becomes a flags byte (`bit0` text, `bit1` location) with `pack_loc6`, not the either/or the earlier T-K2 draft had. **The distress call includes location when one is available.** This supersedes the recommendation in §14 Q3 to omit it.

The exact invocation is `send_channel <ch> "I'm in danger" -t -l -e`.

⚠ **The UI must attach `-l` conditionally, and this is a safety-critical detail, not a nicety.** That spec's matrix refuses `-t -l` outright when there is no fix (`lat_e7 == 0 && lon_e7 == 0`) with `no_location`. A UI that always sent `-l` would therefore turn "no fix" into **no alarm at all** — the worst possible failure for this feature. So:

| condition | command |
|---|---|
| `lat_e7 != 0 \|\| lon_e7 != 0` | `send_channel <ch> "I'm in danger" -t -l -e` |
| no fix | `send_channel <ch> "I'm in danger" -t -e` |

Both forms are explicitly `-e`, which also satisfies the "must actually be sealed" half of the O6 ruling without depending on `team_channel_crypt`'s default.

**Phase A caveat, stated once and then accepted.** On V3 there is no GPS, so the only coordinate is whatever was typed via `cfg set lat`/`lon` — potentially hours stale for a walking hiker, and a stale position in a rescue context can send help to the wrong place. I raised this and the owner ruled to include it when available; the ruling stands and the design follows it. Phase B's live fix removes the concern entirely. The compiled defaults still attach location only to the distress call; §3.2.2–3 lets the owner change that per preset without weakening the sealed-or-refused privacy rule.

## 5. Paint and power policy · ✅ implemented

**This is a correctness constraint, not a nicety.** A full 128×64 SSD1306 frame is 1024 bytes; at 400 kHz I²C that is roughly **25 ms of blocking bus time**. The MAC's CTS→DATA gap is `cts_to_data_gap_ms = 5` (`protocol_constants.h:127`) and measured turnarounds are ~5–8 ms (`protocol_constants.h:331-334`). A full-frame repaint is therefore long enough to break an in-flight RTS/CTS/DATA exchange.

Three rules:

1. **Paint only when the MAC is idle** — no pending TX/RX, `!g_iradio.tx_busy()`, `g_hal.txq_depth() == 0`. `fw_main.cpp:1274` already computes this predicate to decide it may sleep; reuse it rather than inventing a second one (U1).
2. **Paint only when the model reports dirty**, throttled to ≤2 Hz, as `mr_ui.h` already instructs.
3. **Chunk the frame across ticks** — 8 pages of 128 B ≈ 3 ms each, so no single tick holds the bus. This is why §8 recommends a page-buffered driver rather than a full-frame one.

⚠ **U8g2 page mode redraws the WHOLE scene once per page.** Its picture loop is `firstPage(); do { draw_everything(); } while (nextPage());` — the draw calls are clipped to the current page, not accumulated. An earlier plan draft drew the scene once at frame start and then only advanced pages, which would have left seven of eight pages blank. So per eligible tick: **draw the full scene, then advance one page.**

**Freeze the frame's inputs at `begin_frame()`.** The `UiState`/`UiSnapshot` a frame renders are copied when it starts and the page loop reads only that copy — a frame spans several ticks, and live state changing mid-frame would tear the image across page boundaries.

Emergency states override rule 2's throttle but **not** rule 1's idle check: sending is more important than drawing, and the send is what the screen is about. Even then the emergency repaints only when its countdown digit or state actually changes (§4.3).

**Power — blanking is EDGE-triggered, corrected after review.** An earlier draft called U8g2 `clearDisplay()` on every tick while blanked. That is not a local state change: it clears the panel over I²C, so a "blanked" screen would have generated a full-frame transfer every service pass — the exact traffic the page-chunking rule exists to prevent, plus wasted power.

- Blank by `set_power_save(1)` **once**, on the transition into blanked. It turns the SSD1306 off without clearing display RAM, so waking needs no full redraw.
- `set_power_save(0)` **once** on wake.
- The board keeps a `panel_asleep` latch so repeated ticks are genuine no-ops.
- A tick that pushes the final page must not start another display operation in the same pass.

★★ **B71 OWNER RULING 2026-08-04 — the emergency screen's EXIT, which §4/§5 left contradictory.** §4 said `double`
*both* acknowledges sticky state *and* re-fires; it does neither, and nothing returned `_emg` to `idle`, so UI-6 had no
exit condition. ⇒ **Once the emergency has been sent AND ITS RESULT SEEN, the next SHORT PRESS restores the normal
cycle.** Sticky until then; **`long` re-fires; `double` gets no emergency job** (both its §4 duties are withdrawn).
§5's *"the next press restores the emergency screen, not the cycle"* is **corrected**: the emergency screen when waking
from blank, the cycle on the press after. ★ The consumed-waking-press rule immediately below is what makes this safe —
the result is always displayed before any press can dismiss it. Full table: the plan's B71 block.

The panel blanks after **`kBlankMs`** of no input (⛔ **corrected 2026-08-06, [[B130]]: this clause named `MR_UI_BLANK_MS`, which EXISTS NOWHERE IN THE TREE** — it was proposed as an env constant and shipped as `src/firmware_ui_model.h`'s `kBlankMs`; the restated `15000` is likewise gone, [[B120]]). Any press wakes it and **the waking press is consumed** — except a long press, which wakes *and* arms (§4.2). Emergency states hold the panel on for at most **`kEmgHoldMs`** (owner-re-ruled 2026-08-04; ★ read the CONSTANT — this clause said "120 s", went stale the day it was re-ruled, and the correction then restated the *new* digits, which is the same violation over again), after which it blanks with state retained; **the next press then restores the emergency screen, not the cycle** — ⓘ that half is the BLANKED case and stays correct under **B71**, whose ruling governs the *awake-with-an-outcome* case instead (next short press → back to the cycle).

★★ **R1 OWNER RULING 2026-08-05 — AN INCOMING REPLY UN-BLANKS THE PANEL.** §5 above says only that *"any press wakes
it"*, and nothing else did: a distress **REPLY** arriving at a dark panel waited for a button press, which on a
safety device loses the one message the feature exists to deliver. ⇒ **an accepted reply un-blanks.** ⚠ Blanking stays
**edge-triggered**: the un-blank is a **transition**, `set_power_save(0)` once, never a per-tick write. ⚠ It is **not**
wake-on-any-push — what wakes is a post §4.4 *accepts* as an answer to a transmitted alarm (its team scope plus its
state whitelist), so **a stranger's channel-0 post must not light the panel**; that is the §2.1 false-confirmation
class in power form, and a battery-drain vector besides. ⓘ The wake invents **no second window**: §4.3's
`kEmgHoldMs` deadline is refreshed by the reply's own `retain()`, so the panel stays lit for a full window and then
blanks with the state retained. ⓘ **Deliberately unruled and therefore unimplemented:** a `blocked` / `picked_up` /
`not_heard` / `failed` outcome arriving at a dark panel does **not** wake it. Widening the wake to the other retained
outcomes is an open owner question, not a coder's call.

★★ **R2 OWNER RULING 2026-08-05 — A DOUBLE PRESS UNDER THE EMERGENCY OVERLAY IS IGNORED ENTIRELY.** The overlay
**absorbs** it: **no** emergency action (consistent with B71's *"double gets no emergency job"*), **no** operation of
the screen underneath, **no** dismiss, **no** re-fire. ⇒ this closes a hidden mis-send: the overlay owns the body, so
two doubles could open and then send from a compose view the user cannot see — and with a modal left open under
`arming` (which §4.2's cancellable arm deliberately preserves), one was enough.
★ **The complete gesture contract under the overlay: `short` = B71's exit *once the result has actually been
presented* · `long` = re-fire · `double` = nothing.**

## 6. Data sources · ✅ current sources; 📝 extension sources

Every field, and where it comes from:

| field | source |
|---|---|
| unread DM / CH counts | **counted UI-locally in `mr_ui_on_push`**, cleared when the INBOX screen is viewed. ⚠ Corrected 2026-07-31 during planning: the original `dm_newest_seq() - read_cursor()` here was not implementable — `Inbox` (`inbox.h:111-116`) has no read-cursor getter (`read_cursor()` is on `InboxStore`), and `firmware_inbox.h:11` states that verbs use `g_node.inbox()`, not the `g_inbox_*` stores. Counting locally needs no new core API. Consequence: counts are **session-scoped** and reset on reboot, which for a glanceable bar arguably reads better as "since you last looked". |
| "newest received Nm ago" | **stamped by the UI in `mr_ui_on_push`**, not scanned from the store — `msg_recv` / `channel_recv` already fire there (`fw_main.cpp:1066`). Avoids a store scan on the service path. Consequence: after a reboot the age reads `—` until the first push, while counts stay correct (the store persists, the stamp does not). Accepted; the alternative is an `InboxEntry.rx_time_ms` (`inbox.h:38`) lookup via `read_since`, which is a scan we do not need. |
| inbox rows | `g_node.inbox().pull(dm_since, chan_since, cb, ctx)` (`inbox.h:107`) — the same API `pull_inbox` streams. See §6.1 |
| team member count / roster | `rt_team_count()`, `rt_team_at(i)` (`node.h:420-421`); snapshot carries **shown** and **total** separately |
| per-member last heard / quality / hops | `RtCandidate.last_seen_ms`, `.score` (Q4 dB), `.hops` (`node_carriers.h:265-272`) |
| member label | `team_key_of_id` (`node.h:129`) → `peer_name_find(key_hash32, …)` (`node.h:622`); falls back to `0x<hash>` then the bare team id |
| our own team local id | `team_local_id()` (`node.h:258`) |
| configured team id | `g_node.config().team_id` |
| BLE | `mrble::connected()` after UI-12; until then V3 omits the indicator because the ESP32 implementation is inert. Enabled/off comes from the persisted `ble_mode`. See §2.2 and §3.3 |
| battery | **new, and cached** — see §7 |
| send outcomes | correlated by the tracker in `firmware_ui.cpp`, never fed raw to the model — see §2.1 |

### 6.1 Inbox adapter · ✅ preview implemented

No new inbox subsystem is needed for browsing; `Inbox::pull()` already visits both stores and every `InboxEntry` carries its `InboxKind`, sequence, sender/channel metadata and body. Single-record deletion is the separate prerequisite in §6.2.

- Call `g_node.inbox().pull()` **directly**. Do **not** dispatch a textual `pull_inbox` into the 512 B `BufferSink` — the NDJSON is unbounded and would truncate.
- `pull()` returns the **DM block oldest-first, then the channel block oldest-first** (`inbox.h:107-109`). That is the STORE traversal order, not the panel order.
- The panel renders **one screen with labelled rows** (`DM` / `CH <n>`): the complete DM block first, then the complete channel block, but **newest-first within each block** ([[B231]], owner ruling 2026-08-20). The two sequence spaces remain independent; chronological interleaving by `rx_time_ms` is explicitly *not* implied and would need a stated reboot/uptime rule first.
- Retain only the bounded number of rows the panel can browse; clamp sender and body text to the display width. ⚠ **Allocate the bound PER KIND** (e.g. 4 DM + 4 channel), not as one shared pool filled in visit order — `pull()` visits the channel block second, so a shared "keep the newest N" would let a chatty channel evict every DM row, on a screen whose whole point is showing both.
- `short` walks the retained rows and leaves at the end, like TEAM. When more rows exist than are retained, show that rather than implying the list is complete.
- After a successful delete, the frame built before the erase may still contain the old row; the model therefore owes exactly one further repaint from the next tick's normal, fresh pull ([[B233]]). A failed delete changes no store row and owes no such repaint.
- Viewing on the panel does **not** advance the durable `mark_read` cursor. The UI's unread counters stay session-local (§6 above); moving the durable cursor from a button press would desynchronise the companion app, which is the cursor's real owner.

The retained preview row gains `InboxKind kind` and `uint32_t seq`. Its 20-character preview remains a rendering
field, never an identity. Detail activation performs an exact pull lookup by `(kind, seq)` and copies the complete
entry into a fixed buffer; no unbounded JSON intermediary and no heap allocation are introduced.

### 6.2 Durable single-record deletion prerequisite · ✅ implemented 2026-08-06 (UI-7D slice A)

> **AS BUILT — slice A, `lib/core/inbox.{h,cpp}`. This is the contract §3.5's modal calls; the paragraphs below are
> the requirement it was built against and are kept as written.**
> ```cpp
> enum class InboxEraseResult : uint8_t { erased = 0, not_found = 1, io_error = 2 };
> InboxEraseResult Inbox::erase(InboxKind kind, uint32_t seq);   // identity = the PAIR (kind, seq)
> ```
> `erased` → close the modal and rebuild · `not_found` → **`MESSAGE GONE`** (evicted, already deleted, or `seq == 0`)
> · `io_error` → **`DELETE FAILED`**, nothing was deleted. An unwired inbox is `io_error`, never success.
> **Owner ruling 2026-08-06: the mechanism is an appended TOMBSTONE — no rewrite, no segment erase.** `pull()` runs a
> bounded pre-pass (a marker is appended *after* its target, so a single streaming pass cannot filter it) and skips
> both the marker and the record it names; markers live in the same bounded ring, are always evicted after their
> target, and the writer caps them at `protocol::inbox_max_tombstones` (32) so the reader's fixed array cannot
> overflow. Cost: 128 B of stack, **0 bytes of RAM on every env**, two store scans per pull. ★ **No virtual was added
> to `InboxStore`** — `erase()` is composed from `read_since` + `append`, so no backend can be missed or silently
> default to a no-op. The record format is **unchanged** (the marker is `type = 0xFE`, not a `DataType`), so **no
> store-format version bump was taken**. Console: `del_msg <dm|chan> <seq>`. Register [[B133]].
> ⚠ **[[B134]] — ⛔ DO NOT ACT ON THE NEXT SENTENCE WITHOUT THE SHARPENING DIRECTLY BELOW IT; read as-is it produced a
> bench step that could only pass vacuously:** on every ESP32 target — `heltec_v3` included — the inbox is a **volatile
> RAM ring**, so on the panel's own board the delete is durable only until the next power cycle. Slice B must not imply
> otherwise.
> ⛔ **SHARPENED 2026-08-13 (slice B), because the sentence above is true and still reads as the WRONG THING — it misled a
> reader into writing a bench step that could only pass vacuously: *"durable only until the next power cycle"* does NOT
> mean the message returns.** A reboot takes the record, its tombstone **and the whole inbox** with it
> (`persisted_next_seq()` = 0, a fresh `storage_epoch` every boot). ⇒ **within the runtime: deletion is real. Across a
> reboot: there is no history to have deleted from.** Nothing comes back, and nothing cross-reboot is testable here.


> ⛔ **SUPERSEDED 2026-08-07 — the paragraph immediately below is the ORIGINAL REQUIREMENT text and its present
> tense is now false.** It says *"there is no single-record delete today"*; `Inbox::erase(InboxKind, seq)` shipped
> 2026-08-06 (the AS-BUILT block above) and its durable prerequisite closed 2026-08-07. **Read it as the
> requirement this was built against, never as a statement of current behaviour.** The five bullets that follow it
> are the acceptance criteria and they are DISCHARGED — see the AS-BUILT block above and the checklist below it.

There is no single-record delete today. `InboxStore` exposes append, iteration, read-cursor update and whole-store
`wipe()` only; `mark_read()` is not deletion. UI-7D therefore adds the platform-neutral operation
`Inbox::erase(InboxKind, seq)` over a store result with three meaningful outcomes: `erased`, `not_found`, and
`io_error` (an unavailable inbox reports `io_error`, never success). Every backend—fixed/RAM, segmented and device—
must implement the same contract.

The durable backend is an append-only segmented ring, so this is storage work rather than a `vector.erase` hidden in
the UI. The storage implementation may use an atomic segment rewrite or a bounded tombstone/compaction design, but it
must prove all of the following before UI-7D is complete:

- after power loss at any mutation point, the target record is either still present or absent; every other previously
  valid record remains readable and in its original order;
- successful deletion survives reboot, creates only a hole in that kind's sequence space, and never reuses a sequence;
  ⛔⛔ **PLATFORM-QUALIFIED 2026-08-13 (§UI-7D slice B), because the unqualified sentence invites a VACUOUS pass:** this
  criterion is **REQUIRED OF A DURABLE BACKEND** (nRF52 + QSPI `DeviceInboxStore`) and is **UNTESTABLE ON THE CURRENT
  HELTEC/ESP32 BACKEND**, where `FixedInboxStore` is a RAM ring — a reboot destroys the record, its tombstone and the
  entire history alike ([[B134]]). ⇒ on ESP32 the honest statement is *"deletion is real and `pull()`-verified WITHIN the
  runtime"*; *"it is still deleted after a reboot"* is true there only because **everything** is gone, so it must be
  recorded `n/a, volatile store` rather than as a pass (bench Part 19.1 step 6). The native cases discharge this criterion
  against the **segmented** store, which is where it has meaning.
- `next_seq`, read cursor and storage epoch keep their meanings. A one-record delete is not a store wipe and must not
  make the companion reset both cursors;
- `pull_inbox` and OLED browsing omit the deleted record; a failed delete omits nothing;
- deleting a record already evicted by the bounded ring returns `not_found` and cannot select a newer replacement;
- stack/RAM, flash wear and worst-case blocking time are measured. No segment rewrite may run in a radio-critical
  interval.

> **AS BUILT — how the five criteria above were discharged (2026-08-07, closing the QA rejection §B133b).**
> ★ Criterion 1 (*power loss at any mutation point*) **failed on first review and is the reason this section was
> re-opened.** The durable store frames a record as `[u16 framed_len][u32 seq][rec]` and wrote header and body as
> **two** appends, so a tear left a header claiming bytes that were not there — and the **next** append landed
> behind it and was consumed as its body, making a stored record unreachable. ⇒ a retried tombstone could report
> `erased` with the message still visible. **That hole predates UI-7D by two months** (`git blame` → 2026-06-12) and
> affects **ordinary message appends**, so it carries its own register id, [[B135]], and was fixed there:
> a torn segment is **sealed** and the next append **rolls to a fresh one**, with the seal re-derived at `begin()`
> because a power cut also loses the RAM flag. ⛔ **Rotation is not transactional** — a roll on a full ring evicts
> the oldest segment before the write is attempted — so the honest guarantee is *"no previously readable record is
> corrupted or made unreachable"*, **not** *"nothing else is mutated"*; there is also **no per-record CRC**, so
> **short** frames are detected and **corrupt** ones are not. Criteria 2–5 are pinned by the native cases
> (`§3.5/1…/9`, `§B135/1…/6`), which assert **absence from a real `pull()`**, never a return code.
> ⚠ The console verb's own target parsing was a second blocker — [[B136]].

This action deletes the **device's durable copy only**. A companion that already imported the record keeps its own
history; the current inbox protocol has no delete-propagation event. Synchronised deletion across the companion is a
different product contract and is not implied by this button action.

## 7. Battery reader · ✅ code landed; 🧪 H9 hardware qualification remains

Before UI-9, the only reader was nRF52-only. UI-9 has now landed the Heltec V3 implementation in
`variants/heltec_v3/board_ui.cpp`; `--` now means no usable sample or the deliberate polarity-refusal path, not
“ESP32 is unimplemented”. `console_json.h`'s rule still governs every board: unavailable is omitted/`--`, never faked.

★ **Sampling is cached and slow, not per-tick.** The board function performs one sample: a divider toggle plus eight `analogRead()` calls. `firmware_ui.cpp` calls it **at boot and then every `kBattPeriodMs`** (`src/firmware_ui.cpp` — the declaration is the only place the digits belong, [[B120]]), only under the §5 rule 1 MAC-idle predicate, and keeps the last good value between samples. An earlier draft sampled inside `build_snapshot()` on every service pass — ADC work on the radio hot path for a value that changes over minutes. Until the first successful sample the field is unavailable and renders `--`.

⚠ **The cadence must gate on "attempted", not on "succeeded".** A reader that returns the documented unavailable value (`<0`) on a board without a battery would otherwise be retried every idle pass forever — eight ADC reads per tick for a value that will never arrive. Advance the `kBattPeriodMs` deadline after **every** attempt; keep the last good value separately.

The landed ESP32-S3 reader uses the same board-gated shape: millivolts, with `<0` meaning unavailable. Its starting
point came from MeshCore—the same provenance used for the nRF52 reader—but the shipped V3 polarity handling is the
reviewed implementation below, not a verbatim copy of the reference port.

Pins are **identical** on V3 and V4: `ADC_CTRL` 37, `VBAT_READ` 1, 10-bit resolution, mean of 8 samples. The formula is
`mv = kVbatAdcScale * (kAdcRefV / kAdcFullScale) * raw * 1000`. Two things differ:

★★ **`kVbatAdcScale` (5.42) IS A COMBINED EMPIRICAL ADC SCALE, NOT A DIVIDER RATIO AND NOT A PER-REVISION PROPERTY**
([[B126]], fixed in `variants/heltec_v3/board_ui.cpp`; ⛔ this section used to present the number as the divider and
§10.1's own row called it "a per-revision property"). **Measured against the documented network:** the V3 battery
divider is **VBAT — 390 kΩ — GPIO1 — 100 kΩ — GND**, a *physical* ratio of `(390 + 100) / 100` = **4.9**. The reference
port's 5.42 is `4.9 × ≈1.106` ⇒ **roughly 10.6 % of the constant is not the resistors at all** — it absorbs the
ESP32-S3 ADC's attenuation / full-scale error against the nominal `kAdcRefV / kAdcFullScale` this formula assumes.
★ **The diagnostic split the bench now uses (guide H9-02 / script 8.6):** a **constant proportional** error ⇒ suspect
the **scale**; a **voltage-dependent** error or a **fixed mV offset** ⇒ suspect the **ADC** — a resistor network can
produce neither of those two shapes. A meter disagreement is an ADC-calibration suspect at least as much as a
resistor-tolerance one. ⓘ Provenance at the level it is known and no higher: the 390 k/100 k network is documented by
the V3 community and by `ropg/heltec_esp32_lora_v3`'s README, read off the schematic — Heltec's own HTIT-WB32LA_V3.2
PDF is not machine-readable — so it is third-party-from-schematic, **not** a vendor spec sheet. ⛔ The **value** is
unchanged and still comes from the working reference port; do not retune it from one voltage point.

**V3 — the ADC_CTRL polarity is PROBED, never hardcoded** (boards past rev 3.2 inverted the line while keeping the
"V3" name) — **but the reference port's one-shot probe is UNSAFE and must not be reproduced.** ★★ **THE SPECIFICATION
IS THE SHIPPED FILE, `variants/heltec_v3/board_ui.cpp`'s `battery_init()` / `battery_sample_mv()`** ([[B123]] round 2;
the B129 precedent — a copyable unsafe listing in a document is how the defect gets reimplemented, and here it already
had been). It differs from the vendor port in four ways, each of which came out of review:

1. **Two pulls, not one.** Probe with `INPUT_PULLUP`, then with `INPUT_PULLDOWN`, with a µs-scale settle between them.
   `INPUT` alone selects **no pull**, so on a line nothing drives the read is **indeterminate** — and nothing in this
   tree or in the vendor sources documents a pull-up, a pull-down or an idle level for GPIO 37.
2. **Agreement is the licence to believe the reading.** Both reads equal ⇒ something *external* holds the line, the
   idle level is real, and polarity is its inverse (`s_adc_active_high`). Disagreement ⇒ the line is **FLOATING**.
3. **Floating ⇒ REFUSE (C2), never guess.** `s_adc_polarity_known` stays false and `battery_sample_mv()` returns `-1`
   for the life of the boot, so the panel shows `--` — this project's rule for an unavailable reading
   (`console_json.h`) — and never a number derived from a coin flip.
4. ★★ **A fail-safe park on the unknown path: `kAdcCtrlFailsafePark = LOW`.** Heltec's V3.2 hardware update log states
   verbatim *"Modified voltage detection circuit, now need to pull up the ADC_Ctrl(GPIO 37)"* ⇒ **HIGH = MEASURING**,
   so the single expression `digitalWrite(CTRL, s_adc_active_high ? LOW : HIGH)` parks the divider **ENABLED** on
   exactly the refusal path written to prevent a standing drain on a battery-powered safety device.

⛔⛔ **`kAdcCtrlFailsafePark` IS NOT THE HARDCODED POLARITY THIS SECTION FORBIDS, and a reader must not "restore" the
single expression as a spec violation — that is the defect, not the rule.** The **measurement** polarity is still
detected: when the two-pull probe succeeds, both the enable level and the park come from `s_adc_active_high`, and
board-probe checks **P6f** (an idle-HIGH board parks HIGH) and **P8o** (an idle-LOW board parks LOW) go red the moment
that stops being true. The constant is consulted **only** where detection has **already failed** and there is no
detected polarity to consult.

⚠⚠ **The residual, recorded and NOT claimed away:** LOW is documented-inactive for **V3.2 and later only**. A
**pre-3.2** V3 inverts it (`ropg/heltec_esp32_lora_v3`: *"if GPIO37 is pulled low, the battery voltage appears on
GPIO1"*), so there this fallback would be wrong again. ⓘ Why it is still the better bet than a re-flipped coin: a
revision that **biases** the gate at all is one the two-pull probe **detects**, and the fallback then never runs — it
runs only when nothing biases the line. ⛔ **No claim is made that this is provably safe on all revisions. Only the
bench closes it: guide `H9-05` part C, script `8.31`.**

Read sequence: drive the detected ACTIVE level, sample, drive the detected INACTIVE level. **No settling delay in the
burst** (the µs-scale settle belongs to the boot-time probe, where the pull is deliberately changed between reads).

> ⛔ **SUPERSEDED — DO NOT IMPLEMENT.** The vendor listing this section used to prescribe as live guidance, kept only
> as audit trail ([[B130]]; ⚠ the section already knew about the 3.2 change and *still* prescribed the unsafe probe):
> ```
> pinMode(PIN_ADC_CTRL, INPUT);                    // ⛔ NO PULL -> indeterminate on a floating line
> adc_active_state = !digitalRead(PIN_ADC_CTRL);   // ⛔ a coin flip when nothing drives the pin
> pinMode(PIN_ADC_CTRL, OUTPUT);
> digitalWrite(PIN_ADC_CTRL, !adc_active_state);   // ⛔ "park inactive" — parks the MEASURING level on V3.2+
> ```
> ⓘ MeshCore runs this on `heltec_v3` **and** `rak3112`; its own **V4** board drops the probe entirely and hardcodes
> ACTIVE=HIGH (`HeltecV4Board.cpp:7-8`). Reproducing it verbatim would claim knowledge the tree does not have.

**V4 — fixed ACTIVE=HIGH, plus a settling delay:**

```
digitalWrite(PIN_ADC_CTRL, HIGH);
delay(10);                        // ⚠ see below
... sample ...
digitalWrite(PIN_ADC_CTRL, LOW);
```

⚠ **V4's `delay(10)` is the same hazard class as a full-frame repaint.** Ten milliseconds of blocking wait against `cts_to_data_gap_ms = 5` will break an in-flight exchange. The Phase B port must not copy it verbatim: either sample only under the §5 rule 1 MAC-idle predicate, or restructure as a small state machine across ticks (enable → return; sample on a later tick → disable). Battery is a once-per-N-seconds reading; there is no reason for it to ever block. Phase A (V3) has no delay to remove, which is one more reason it lands first.

Verify against a multimeter on the cell before trusting the constant on either board — the discipline the nRF52 comment demands. ⛔ **This sentence used to end *"and the divider is a per-revision property"*; that is the [[B126]] error restated** — `kVbatAdcScale` is not the divider, and the per-revision axis on V3 is the **ADC_CTRL polarity**, not the resistor network. Use the constant-vs-voltage-dependent split above to choose the suspect.

## 8. Dependencies · ✅ U8g2 choice landed

UI-5 landed U8g2 in page-buffer mode for the Heltec targets. V4 is expected to reuse the same display dependency
because the panel and I²C pins match; only the board port changes.

**Landed choice: U8g2 page-buffer mode** (`U8G2_SSD1306_128X64_NONAME_1_HW_I2C`). The 1-page mode uses a
128 B buffer instead of 1024 B, and its picture loop matches §5's chunking rule. Adafruit_SSD1306/GFX/BusIO remains
the rejected three-dependency, full-frame alternative.

**Pin the version exactly, never a caret.** The RadioLib ruling (`platformio.ini:234` and its note, on this very env) exists because a caret let different checkouts resolve different versions and silently skewed board RAM/Flash baselines.

## 9. Feature gating

- `MR_FEAT_OLED` (board capability) gates the UI TUs. Default 0 (`mr_features.h`); set to 1 on `heltec_v3` (`platformio.ini:215`), inherited by `heltec_mobile`.
- `MR_FEAT_OLED && MR_FEAT_TEAM` gates TEAM, SEND and on-device team creation. ⛔ **CORRECTED IN PLACE 2026-08-13
  (§UI-14): this read *"The current non-team cycle is STATUS → INBOX; AFTER UI-14 it is STATUS → INBOX → SETTINGS."*
  UI-14 has landed — the non-team cycle IS `STATUS → INBOX → SETTINGS`.** SETTINGS is deliberately not team-gated.

- The preset catalog/serial verbs are gated with the OLED feature; the default catalog remains compile-time data when
  no persistent record exists.
- The first ESP32 BLE port is compiled for `heltec_mobile` only. A BLE setting must be rejected on ESP targets where
  the transport is absent; a persisted-but-inert feature is not a valid gate.
- SETTINGS is `MR_FEAT_OLED`-gated. Unsupported settings are absent, not inert; BLE mode is not editable until UI-12
  is compiled for that target.
- Static join profiles are available only on mobile-capable builds. Team creation additionally requires
  `MR_FEAT_TEAM`; neither action appears on a gateway.
**Deviation from the owner's draft, approved 2026-07-31:** the draft said "gate it behind `MR_FEAT_TEAM`". Composing the two gates instead means a static OLED board still gets a status screen and an inbox rather than a blank panel, at no cost. `MR_FEAT_TEAM` alone would have conflated a board capability with a protocol plane.

**Phase A target env: `heltec_mobile`** (`platformio.ini:372`) — `heltec_v3` plus `MR_PROFILE_MOBILE`, which sets `MR_FEAT_REMOTE_MGMT=0` and leaves `MR_FEAT_TEAM=1`. It already inherits `MR_FEAT_OLED=1` from `heltec_v3`, so no env change is needed to start.

Phase B adds a `heltec_v4` env (and a `heltec_v4_mobile` extending it) once the radio port of §10.2 exists. GPS gets its own feature flag rather than riding `MR_FEAT_OLED` — a GPS is not a display, and a future non-display tracker would want one without the other.

## 10. Board port table

All values recovered from MeshCore's working ports — `~/MeshCore/variants/heltec_v3/` and `~/MeshCore/variants/heltec_v4/` — not from datasheet reading.

### 10.1 V3 vs V4

| item                       | Heltec V3 (Phase A)                                                                                                                                                                                                                                                                                                                                                            | Heltec V4 (Phase B)                                     | same?                       |
| -------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------- | --------------------------- |
| MCU                        | ESP32-S3 (`esp32-s3-devkitc-1`)                                                                                                                                                                                                                                                                                                                                                | ESP32-S3, 16 MB flash, 2 MB PSRAM                       | ~                           |
| panel                      | SSD1306 @ 0x3C, SDA **17**, SCL **18**                                                                                                                                                                                                                                                                                                                                         | SSD1306 @ 0x3C, SDA **17**, SCL **18**                  | ✅                           |
| panel reset                | **21** per our own `board_ui.cpp:14` note — MeshCore's V3 variant defines no `PIN_OLED_RESET`; **confirm on hardware**                                                                                                                                                                                                                                                         | **21** (`PIN_OLED_RESET`, explicit)                     | ✅ (pending V3 confirmation) |
| **user button**            | **GPIO 0**                                                                                                                                                                                                                                                                                                                                                                     | **GPIO 0**                                              | ✅                           |
| battery pins               | `ADC_CTRL` **37**, `VBAT_READ` **1**                                                                                                                                                                                                                                                                                                                                           | `ADC_CTRL` **37**, `VBAT_READ` **1**                    | ✅                           |
| battery formula            | `kVbatAdcScale * (kAdcRefV/kAdcFullScale) * mean8(raw)` — ★ `kVbatAdcScale` is a **combined empirical ADC scale**, not the 4.9 physical divider ([[B126]]; §7)                                                                                                                                                                                                                 | identical                                               | ✅                           |
| **battery ctrl polarity**  | **detected at boot by a TWO-PULL probe** (`INPUT_PULLUP` then `INPUT_PULLDOWN`; agreement ⇒ polarity known, disagreement ⇒ floating ⇒ **REFUSE**, park `kAdcCtrlFailsafePark`) — boards >3.2 inverted the line. ⛔ **This cell used to prescribe the vendor's bare-`INPUT` one-shot probe and "nominal ACTIVE=LOW"; both are superseded — see §7 ([[B123]] round 2, [[B130]])** | **fixed ACTIVE=HIGH**                                   | ❌                           |
| **battery settling delay** | none                                                                                                                                                                                                                                                                                                                                                                           | **`delay(10)`**                                         | ❌                           |
| peripheral power           | `VEXT_EN` **36**, polarity implicit (default)                                                                                                                                                                                                                                                                                                                                  | `VEXT_EN` **36**, explicitly **ACTIVE=HIGH**            | ❌                           |
| LoRa SPI                   | NSS 8, DIO1 14, BUSY 13, SCLK 9, MISO 11, MOSI 10                                                                                                                                                                                                                                                                                                                              | identical                                               | ✅                           |
| **LoRa reset**             | **`RADIOLIB_NC`** (matches our `platformio.ini:218`)                                                                                                                                                                                                                                                                                                                           | **GPIO 12**                                             | ❌                           |
| TX LED                     | 35                                                                                                                                                                                                                                                                                                                                                                             | 35                                                      | ✅                           |
| **front-end module**       | **none**                                                                                                                                                                                                                                                                                                                                                                       | **PA + LNA, switched every TX**                         | ❌                           |
| **`LORA_TX_POWER`**        | **22** = 22 dBm at the SX1262                                                                                                                                                                                                                                                                                                                                                  | **10** = **22 dBm output**                              | ❌                           |
| RX register patch          | not set                                                                                                                                                                                                                                                                                                                                                                        | `SX126X_REGISTER_PATCH=1` (reg 0x8B5)                   | ❌                           |
| GPS pins                   | RX 47, TX 48, EN 26 — defined, **GPS not enabled**                                                                                                                                                                                                                                                                                                                             | RX 38, TX 39, RESET 42, EN 34 — **`ENV_INCLUDE_GPS=1`** | ❌                           |

**What this means for the plan:** the panel, the button and the battery *pins and formula* are common to both boards, so the UI layer and its render/input code port unchanged. The differences are confined to two places — a handful of **board-port details** (battery control polarity and settling, Vext polarity) that live entirely inside `board_ui.cpp`, and the **radio domain** (FEM, reset pin, tx_power semantics, register patch), which the UI never touches. That is exactly why Phase A on V3 is cheap and Phase B is a real port: Phase B's cost is radio work, not UI work.

⚠ **GPIO 0 is the ESP32-S3 boot strap pin, on both boards.** Holding the user button across a reset enters serial-download mode. Since long-press is the emergency gesture, a user holding the button while the node brownouts or resets gets a bricked-looking device instead of an alarm. This is a hardware behaviour to document in user-facing text and to weigh when choosing the arm duration — not something the UI layer can fix.

### 10.2 ⚠ V4 is a radio port, not a pin table — Phase B PREREQUISITE, out of scope here

MeshCore's V4 variant carries a `LoRaFEMControl` class and calls it on **every transmission** (`HeltecV4Board::onBeforeTransmit` / `onAfterTransmit`). The V4 has an external front-end module — PA plus LNA — that must be switched between TX and RX modes. Our tree has **no such hook**: `DeviceRadio::start_transmit` and `poll_tx_done` (`lib/hal/device_radio.h:137, 162`) drive RadioLib directly with no board callback.

Four consequences, none of which belong in a UI spec:

1. **FEM switching is mandatory.** Without it the V4 transmits through a bypassed PA with the LNA still in circuit. The natural insertion points exist (`start_transmit` before `startTransmit`; `poll_tx_done` and `tx_timeout_recover` before `arm_rx`) but the abstraction does not.
2. **Two board revisions behind one name.** `LoRaFEMControl::init()` auto-detects the FEM at runtime by reading GPIO 2's default pull level — pull-down ⇒ GC1109 (V4.2), pull-up ⇒ KCT8103L (V4.3) — and the two need different pin sequences. "Heltec V4" is not one target.
3. **★ `tx_power` changes meaning, and the range becomes unsafe.** MeshCore's FAQ §7.7 states for the V4: an in-app setting of **10 dBm yields 22 dBm output**, and **22 dBm yields 28 dBm**. Our `cfg set tx_power` validates −9..22 (`firmware_config.cpp:142`) and documents it as SX1262 dBm. On a V4 that same range silently means up to **28 dBm actual**, past what most EU868 sub-bands permit and into the territory MeshCore's own table prefixes with a hardware-damage warning. The V4 port must either offset the setting or clamp the range per board — a compliance decision for the owner, not an implementation detail.
4. `SX126X_REGISTER_PATCH=1` (register 0x8B5, "improved RX") is set for V4 and is absent from our envs.

**Ruling (owner, 2026-07-31): the V4 radio port is its own spec, and Phase A proceeds on V3 meanwhile.** Folding a PA/LNA switching path and a transmit-power semantics change into a display feature would violate C1 outright, and the tx_power item is a regulatory question that deserves its own decision record. Nothing in Phase A depends on it, because V3 has no FEM.

### 10.3 GPS and distance — Phase B only

Phase B adds, in this order:

1. **A UART NMEA driver** behind a board-gated seam, on V4's `PIN_GPS_RX 38` / `PIN_GPS_TX 39`, with `PIN_GPS_EN 34` (active LOW) and `PIN_GPS_RESET 42` (active LOW) for power control. It must not block: NMEA arrives continuously and the parser has to be fed incrementally from the service loop, never with a blocking read — the same discipline §5 imposes on the panel and §7 on the battery.
2. **Feed the fix into the existing location plumbing** rather than inventing a parallel one (U1): `lat_e7` / `lon_e7` already exist in `NodeConfig` and in the identity record, and a DM location piggyback already exists. A GPS fix should write the same fields `cfg set lat/lon` writes, so every existing consumer works unchanged.
3. **Distance column on the TEAM screen**, shown only when both our fix and the peer's location are known and fresh; otherwise the column is omitted, never estimated. This follows the project rule already recorded for battery (`console_json.h:126`): an unavailable reading is omitted, never faked. A wrong distance in a safety context is worse than no distance.
4. **Location in the emergency message** is already resolved for both phases (§4.1: include when available). Phase B changes only its *quality* — a live fix replaces a hand-typed coordinate — so no decision is outstanding, and the conditional `-l` written for Phase A needs no change.

Peer location exchange is the open dependency: distance needs teammates' coordinates, and how those propagate on the team plane (beacon TLV, DM piggyback, or channel message) is not settled. That question belongs to the Phase B spec, not this one.

## 11. Flash / RAM budget and D2

- RAM: `UiSnapshot` + `UiState` + a teammate array capped at **`kMaxTeamRows = 8`** + a 128 B page buffer. Expected low hundreds of bytes; measure, don't estimate. **8, not 16** — a 3–10 member hiking group (§1) is the design target, each row carries a display label, and the TEAM screen must show a truncation marker rather than silently under-report when `rt_team_count()` exceeds the cap. The snapshot therefore carries both the shown count and the true total.
- Flash: U8g2 with **two** fonts selected (6×10, 10×20). Do not link the full font set.
- These units live in `src/`, not `lib/core`, so **`sizeof(Node)` is unchanged** and the `node.h:1976` assert is untouched. That is a deliberate architectural consequence: no UI state belongs in the protocol engine.
- Per D2/the 3-env board rule, gate on `gateway` + `xiao_sx1262` + `xiao_esp32s3`, plus `heltec_v3` and `heltec_mobile` since those are the envs Phase A changes. Record the flash/RAM delta for the Heltec envs. ESP32-S3 flash is not the constraint here (V3 devkit part; V4 is 16 MB) — the reason to record it is the RadioLib-pinning lesson about baselines drifting unnoticed, not headroom anxiety.

## 12. Test plan

**Native (the valuable half):**

| test | drives |
|---|---|
| `test_firmware_ui_input.cpp` | gesture classifier: short vs double window, double vs two shorts, long-arm progress ticks, release-before-fire cancel, bounce rejection, the consumed wake press |
| `test_firmware_ui_model.cpp` | screen cycle incl. the non-team compile-out, cursor advance and wrap on TEAM/INBOX, the full emergency machine (arm → cancel, arm → fire → `blocked` → retry-at-`next_ms` → `picked up`; and → `no_relay` → `kEmgMaxTries` accepted transmissions → sticky `not_heard`), dirty-flag correctness, blanking and emergency-hold timing |

Both are pure and table-driven; no Arduino, no radio, no display.

**On-target checklist** (Phase A: `heltec_mobile` = Heltec **V3**, bench):

⚠⚠ **CORRECTED 2026-08-06 ([[B130]]): every threshold below is NAMED, never restated, and the headline is the
owner-ruled one.** This list previously read *"Release at 3.0 s cancels; release at 3.6 s fires"*, *"`NOT HEARD` after
3 retries"* and *"Fire twice inside 10 s"*. [[B120]] was the **third** violation of the name-don't-restate rule and it
survived review **because the restated value was correct at the time** — a restated timing in the authoritative spec is
strictly worse, because every downstream document copies from here. ⓘ The executable procedure lives in
`docs/2026-08-04-heltec-v3-oled-ui-bench-guide.md` / `docs/2026-07-31-bench-test-script.md` (M2); this list is the
spec's acceptance intent, not a second copy of the bench.

1. Emergency from each screen, in the dark, with gloves — reaches `firing` without reading the panel.
2. Release **before** `kArmToFireMs` cancels; release **after** it fires (`InputCfg::fire_ms` must equal
   `kArmToFireMs` — pinned by a native test, so the bench never needs the digits).
3. Two nodes: fire with the second powered off → **`NOT RELAYED`** after `kEmgMaxTries` **accepted transmissions**
   (⚠ *transmissions*, not "retries after the first" — §4's binding reading, and the previous wording contradicted it).
   Power the second on → fire again → `PICKED UP`. Reply from the second → `REPLY` shown.
4. Fire twice inside `channel_min_interval_ms` → the second shows `BLOCKED` with a live countdown, then auto-fires.
5. **Paint-vs-radio:** run the s18-style DM load while cycling screens continuously; confirm no CTS timeout regression. This is the check that §5 rule 1 exists for, and the one most likely to fail.
6. Blank/wake: waking press does not change screen.

**Additional acceptance cases (from the 2026-08-01 review, all adopted):**

- a canned or console channel post completing while the emergency is `firing` **cannot** alter the emergency state
- a blocked **DM** cannot put the emergency screen into `BLOCKED`
- a parser refusal leaves `SENDING...` and shows an actionable failure. ⚠ **§B84: `send_failed{unsealable}` does NOT** —
  it is unattributable (six non-channel operations emit `dst == 0`) and is therefore **ignored**; that path terminates
  via bounded expiry in **`not_heard`** (headline **`NOT RELAYED`**), losing its precise reason (owner-accepted)
- `next_ms == 0` neither deadlocks nor spins — the backoff applies and no attempt is consumed
- exactly **`kEmgMaxTries` accepted transmissions** occur even when preceding requests were blocked
- long gestures work from both compose sub-views and from a blanked panel
- the arming countdown digit visibly changes; `CANCELLED` auto-returns to the parent
- a matching **same-team** reply on `MR_UI_TEAM_CHANNEL_ID` becomes sticky confirmation; other traffic on that channel — including a `team_id == 0` leaf post from any node in range — does not (factual correction 2026-08-05, see §4.4)
- a DM ack/failure is matched by `ctr` **and** peer; unrelated acks are ignored
- the inbox shows bounded labelled rows via `Inbox::pull()`
- a blanked panel produces **no repeated I²C traffic** (instrument or trace-count it)
- battery sampling happens only at its slow cadence and never starts while the MAC is busy
- `heltec_v3` and `heltec_mobile` compile with the final TU ownership and `mrfw::dispatch` qualified

**Added by the second review (2026-08-01), all adopted:**

ⓘ **The three `(needs Bnn)` markers below are HISTORICAL: B38, B39 and B40 all landed 2026-08-01** ([[B130]]; B39 as an
interim whose ambiguity is still live — §2.1 rule 2 / §B84). They name the prerequisite each case was written for, not
an outstanding gate.

- a team relay produces exactly one truthful `channel_sent{relayed=true}` while coverage retries remain valid *(B38 ✅)*
- a blocked emergency returns **no** accepted counter and consumes none of the `kEmgMaxTries` transmissions *(B39 ✅ interim)*
- a channel seal failure leaves `SENDING...` and shows its exact reason
- channel counters **255 · 256 · 257 · 65535→1** correlate correctly, with a low-byte-colliding unrelated outcome interleaved *(B40 ✅)*
- an outstanding DM or canned channel transaction **cannot delay** emergency command execution — proven at the firmware integration boundary, not only in the pure model
- delayed outcomes from an abandoned normal transaction cannot move the emergency
- one frame redraws the scene **once per page** and performs one page transfer per MAC-idle tick
- `PICKED UP` and a late reply obey the **`kEmgHoldMs`** hold deadline across `millis()` wrap (owner-re-ruled
  2026-08-04. ★ Read the CONSTANT — this line said "120 s", went stale the day it was re-ruled, and its own correction
  then restated the new digits, which is the [[B120]] violation over again; they are now gone)
- `e2e_ack_timeout` shows `NO CONFIRM`, and a late ack upgrades it to `DELIVERED`
- an unavailable battery reader is retried at the `kBattPeriodMs` cadence, not loop cadence
- channel traffic during `arming`, `cancelled` or `failed` cannot become a distress `REPLY`
- the bounded inbox retains labelled DM **and** CH rows — a chatty channel cannot evict every DM row

**Inbox-detail/delete extension acceptance:**

- double opens the exact highlighted `(kind, seq)` for DM and channel rows; reordered or evicted rows cannot substitute
- bodies of 0, 42, 43 and 241 bytes render without an out-of-bounds read or hidden suffix; automatic pages do not postpone blanking
- initial double plus double again takes safe `back`; deletion requires short → double and survives reboot
- `not_found`, injected write failure and power loss never remove another record or visually claim deletion
- successful deletion leaves sequence high-water, read cursors, storage epoch, companion history and every other record unchanged

**Configurable-preset/BLE extension acceptance:**

- missing and corrupt UI storage select the documented 17-slot catalog: mandatory emergency plus two enabled defaults
  in each DM/channel catalog and six disabled slots in each; corruption is visible, and neither case changes `/mrcfg`,
  identity, team membership or keys
- every stable slot round-trips byte-exact through set/clear/reset → list → reboot → list on both serial and
  authenticated BLE; emergency rejects clear
- zero-active, sparse (`1,4,8`) and full eight-entry DM and channel catalogs render, scroll and send the selected
  stable slot; the empty catalog offers only the safe back action
- malformed slot/text/location, storage failure and a write during an active emergency change nothing and fail loud
- a catalog generation change cannot send a newly configured text from an already-open selection or tear an OLED frame
- location-off, location-on-with-fix, normal-location-without-fix and emergency-location-without-fix issue the exact
  command lines required by §3.2.3
- unpaired BLE cannot read or write NUS; bonded BLE can run the same preset commands and receives complete NDJSON across
  MTU splits
- BLE advertising/connection plus continuous OLED interaction causes no repeatable CTS/DATA, beacon, sleep or watchdog
  regression; RAM/flash and warning deltas are attributed for `heltec_mobile`

**Settings/provisioning extension acceptance:**

- opening SETTINGS and changing a value performs zero NV writes and changes no live radio/core state; STATUS and the
  SETTINGS title show the unsaved marker
- no-op SAVE writes nothing; a multi-field SAVE performs one durable config write, then applies live fields, and
  accurately reports reboot-required fields
- DISCARD restores the persisted snapshot; BACK, blanking and emergency interruption preserve the draft
- injected validation/NV failure leaves old live and durable state intact, retains the draft, and shows `SAVE FAILED`
- a serial/BLE write during a draft produces `CFG! RELOAD`; SAVE cannot overwrite it
- live-only keys are absent until they gain durable schema support; unsupported BLE is absent rather than persisted
- team creation defaults to BACK, atomically saves before live apply, and cannot leave a volatile-only team on failure
- nearby scan de-duplicates full team ids, expires stale observations and mutates no routes, bindings, membership, key
  or NV state; the display says `CURRENT PHY ONLY` and selecting a truncated fingerprint applies the exact full id
- joining nearby without an imported key produces a durable keyless membership that cannot decrypt encrypted channel
  posts; invitation expiry or an unconfirmed/failed grant changes no key
- invitation approval lists only identities first observed after the session snapshot, never claims their key state,
  requires an authoritative public key, and routes the selected candidate through the existing sealed grant path
- the creator distinguishes `KEY SENT` from receiver completion; only durable adoption lets the joiner display
  `TEAM KEY RECEIVED`; no failure path renders or broadcasts the private team key
- all four join profiles round-trip independently; exact profile values are shown before JOIN, and a corrupt profile
  record affects neither node configuration nor team/preset storage
- emergency from every settings/provisioning state reaches dispatch without saving, discarding or committing a hidden
  action; a waking press commits nothing
- power loss at every config/team/profile commit boundary recovers the complete old or complete new record only

**Status/navigation chrome acceptance (bench Parts 24–25):**

- verify all five fixed rail positions, including empty TEAM/SEND slots on a non-team OLED build, and the selected outline;
- distinguish all four SETTINGS badge states on glass without relying on STATUS text;
- verify mail, home, team, key and battery slot semantics against their source data, including every `--` / saturation case;
- verify every normal line remains inside the 116 px body and inbox detail uses 19-column wrapping / seven-page maximum;
- verify emergency suppresses the rail but retains the top strip and full-width body;
- repeat repaint, radio-wake and `slept=` checks after the extra chrome compose work; visual success alone is insufficient.

**Gate:** the standing D1 gate applies per slice: native tests, relevant board envs per §11, and the s18 corpus.
Require exact s18 byte identity only for a slice proven outside the core/simulator behavior surface. A core/storage
change such as UI-7D must instead attribute any corpus delta and add a non-vacuous behavioral control; do not demand
or claim identity merely because this is an OLED-led feature.

## 13. Slices

Slices are named `UI-n` deliberately: bare `U1`/`U3` would collide with the CLAUDE.md working-rule IDs (U1 = reuse-before-writing, U3 = feature logic in a `firmware_*` module), both of which this spec cites.

### Phase A — Heltec V3 (this spec)

| # | status | slice | gate / completion evidence |
|---|---|---|---|
| UI-1 | ✅ | gesture classifier + native test | landed, native |
| UI-2 | ✅ | screens, cursor and compose model | landed, native |
| UI-3 | ✅ | emergency + DM outcome machines | landed, native |
| UI-4 | ✅ | send tracker + typed result (§2.1) | landed, native + board |
| UI-5 | ✅ | U8g2 board canvas and page paint | landed; board probe |
| UI-6 | ✅ | button, snapshot/render policy and live cycle | landed; board probe |
| UI-7 | ✅ | roster, fixed compose tables, real sends and inbox preview | landed; hardware acceptance partly recorded |
| UI-7D | ✅ **UI + storage** · 🧪 metal | inbox detail/delete (§3.5/§6.2) | ⛔ **CORRECTED IN PLACE 2026-08-13: this cell read `📝 UI · ✅ storage`, i.e. the ONE table a reader consults to answer "is this built?" still said the UI was not — while §3.5, §6.2 and §13's prose had all been corrected. Class-4 (a correction placed anywhere but the instruction a reader follows), third instance in one review round.** ✅ storage (slice A): `Inbox::erase` + `del_msg` + durable-append recovery ([[B135]]) + strict target parsing ([[B136]]); **storage fault/power-cut injection DONE natively** (`§B135/1…/6`, mid-frame injector). ✅ **UI (slice B, 2026-08-13): the §3.5 modal — identity-tracked `(InboxKind, seq)` selection, 42-char paging on `mr_ui_tick`, all three erase landings, the `long_arm` close; 32 native mutations + 13 device-half probe controls, all RED.** 🧪 **metal REMAINS THE ONLY OUTSTANDING PART: bench Part 19** (the panel's real 21 columns, wall-clock paging, and [[B134]]'s volatility control) |
| UI-8 | 🧪 | emergency end-to-end qualification | code exists; complete H8 |
| UI-9 | 🧪 | V3 battery reader and cache (§7) | code exists; complete H9/multimeter |
| UI-10 | 📝 | versioned configurable preset catalog | native + storage fault injection |
| UI-11 | 📝 | preset verbs, sparse lists and per-slot location | native + target, serial first |
| UI-12 | 📝 | secured ESP32-S3 BLE-NUS for `heltec_mobile` | builds + BLE/LoRa soak |
| UI-13 | ✅ **service only — HEADLESS** · 🧪 NV/power-cut | typed staged-config service, conflict detection and one-write commit (§3.6.1) | ⛔ **CORRECTED IN PLACE 2026-08-13: this cell read `📝` and the "Recommended next order" line below still named UI-13 as the next slice to START. Both were true until the service landed and are now false. FOURTH instance in this arc of a correction reaching the prose but not the table a reader acts on — which is why the sweep for this one covered §13's table, §13's next-order sentence, the §status map and §3.6/§3.6.1's headings.** ✅ **landed 2026-08-13** (`src/firmware_config_service.h`): three states, `config_unsaved` / `conflict` / `reboot_required` as three distinct comparisons, typed validation, whole-candidate validation before any write, ONE durable write, live apply only after durable success, DISCARD/RELOAD, `CFG! RELOAD` / `SAVE FAILED`. Native: 29 cases, **32 mutations all RED**. ⛔⛔ **CORRECTED IN PLACE 2026-08-13 (QG round 2): this cell read *"HEADLESS — no screen, no cycle change and NO DEVICE BINDING (`ICfgStore`/`ICfgLive` unimplemented on hardware)"*. THAT IS NOW FALSE and is withdrawn here, not deleted — it describes the §UI-13 SLICE BOUNDARY, and §UI-14 (the row below) landed BOTH the screen and the binding.** ★ Read as history: UI-13 shipped headless by scope; UI-14 consumes it. ⇒ 🧪 **the gate's "NV fault/power-cut" half is STILL NOT met — it moved to [[B193]] with the binding rather than being discharged by it**: everything here is proved against a counting/failing FAKE store, which measures the LOGIC and cannot measure flash, wear or a reset mid-write |
| UI-14 | ✅ **UI + device binding** · 🧪 NV/power-cut | SETTINGS screen, marker and save/discard/reboot states (§3.6.2) | ⛔ **CORRECTED IN PLACE 2026-08-13: this cell read `📝` and the gate column read *"native + board probe + target"*. ✅ landed 2026-08-13** — the fifth cycle slot on both cycles, the row table with its two CONDITIONAL rows (BLE by the UI-12 transport, RELOAD by `conflict()`), `short`'s two modes behind the `Settings{closed,browsing,editing}` state, `double` to enter/accept, SAVE / DISCARD / RELOAD / BACK through the §UI-13 service, §3.3's three literals on STATUS, the `long_arm` editor close, and **[[B193]]'s device binding** in `src/firmware_config.cpp`. **PROVISION is present-but-inert; §3.6.3's unsaved-draft precondition is NOT built.** 18 new model mutations + 8 new probe controls, both UI probes green, census at its pins, 36/36 corpus with 0 movers. ⛔ **NATIVE TOTAL CORRECTED IN PLACE 2026-08-13 (§notify-every-save): this cell published `1610/82310/0`, which was already stale by the §UI-14 FOLLOW-UP's `1613/82339/0` and is now stale again. The live figure is `1615/82362/0`** — and the lesson is that a suite-wide total published in a per-row cell goes stale on the next slice by construction, so read it from the tree, never from here. 🧪 **THE `target` HALF IS NOT MET: no NVS/LittleFS write, no wear and no reset-during-write is exercised by any gate** — the bindings are proved only by compiling, and everything behavioural runs against FAKES. That is a BENCH check (`docs/2026-07-31-bench-test-script.md` §UI-14) and [[B193]] keeps its 🧪 until it runs ⛔ **AMENDED 2026-08-20 by [[B232]] (owner ruling): SETTINGS now LANDS CLOSED on a single `ENTER SETTINGS` row — `short` passes in one press, `double` opens the menu (closed-stay when the service cannot open); menu BACK/walk-off return to the closed view. The auto-enter/walk-to-leave described above is WITHDRAWN.** |
| CHROME-1…4 | ✅ implementation · 🧪 metal | status strip, navigation rail, settings badge and 19-column migration | implementation complete; bench Parts 24–25 remain |
| UI-15 | 📝 | atomic team creation and four-profile static join (§3.6.3) | native + NV fault/power-cut + multi-node metal |
| UI-16 | 📝 | current-PHY nearby-team scan, explicit candidate approval and sealed key grant (§3.6.4) | native + RF isolation controls + multi-node metal |

**2026-08-17 reconciliation:** the UI-7D row's historical “42-char / 21-column” evidence describes its landing
configuration; current chrome makes it **38 characters / 19 columns / at most seven pages**. Likewise the UI-14 row's
historical “three literals on STATUS” placement is superseded by the SETTINGS-rail badge in §3.3. Neither historical
cell is operational guidance.

UI-1…UI-7, **UI-7D (both slices)** and the UI-9 code are landed. UI-8 is a hardware gate, not missing firmware.
**UI-7D is a hardware gate too now — its firmware is complete and only bench Part 19 is outstanding.**
**UI-10…UI-12 and UI-15…UI-16 are extensions and must not be reported as current behavior.**
⛔ **CORRECTED IN PLACE 2026-08-13 (§UI-14): this read *"UI-10…UI-12 and UI-14…UI-16"*, which stopped being accurate
when the SETTINGS screen landed the same day. ⚠ The correction is narrow on purpose — what landed is the SCREEN, its
marker and its save/discard/reboot states over a REAL device binding; ⛔ **no provisioning behaviour exists** (the
PROVISION row refuses), and ⛔ the binding's NV / power-cut behaviour is unproven, so *"the storage half is qualified"*
must still not be reported.**
⛔ **CORRECTED IN PLACE 2026-08-13: this read "UI-10…UI-16", which stopped being accurate when UI-13's SERVICE landed
the same day. ⚠ The correction is narrow on purpose — UI-13's landed part is a HEADLESS API with no device binding and
no screen, so "no on-device settings behavior exists" REMAINS TRUE and must still not be reported otherwise.**
⛔⛔ **AND THAT NARROW CORRECTION IS ITSELF NOW STALE — CORRECTED IN PLACE 2026-08-13 (QG round 2, found by sweeping rather than by being named): §UI-14 landed the SETTINGS screen AND the device binding, so *"no on-device settings behavior exists"* is FALSE.** On-device settings behaviour EXISTS: the fifth cycle slot renders the four covered fields, edits a RAM draft, and SAVE writes `/mrcfg` through the service. ⚠ **What must still not be reported is narrower and unchanged: the STORAGE half is unqualified** — no real-flash write, no wear and no reset-during-write is proved by any gate ([[B193]], bench Parts 19/20) — **and UI-15/UI-16 (team creation, join profiles, nearby-team onboarding) do not exist**; §UI-14's `PROVISION` row refuses out loud.
⛔ **CORRECTED IN PLACE 2026-08-13: this sentence read *"UI-7D and UI-10…UI-16 are extensions and must not be reported as
current behavior"*, which was true until slice B landed and is now false for UI-7D. It is kept withdrawn rather than
deleted because it is the sentence a reader acts on.**

**Recommended next order (2026-08-17):** qualify the current firmware on metal before adding another screen flow:
finish the refreshed Phase-A R1–R6 cases plus bench Parts 19–25, including B196 soak, [[B164]] transmit correlation,
[[B193]] real-storage/reset behavior and the status/navigation visual + sleep/radio checks. Use the observed
`tx_outcome_drops` / HAL results to decide [[B189]] recovery separately; do not treat T2/T3 observability as retry.
After those gates, proceed **UI-15 → UI-16** because no-phone team creation and onboarding are the owner's primary
remaining OLED functions. UI-10 → UI-11 and UI-12 may proceed independently after their own dependencies; UI-12 is
needed for convenient preset/profile editing and companion key export, but UI-15/UI-16 do not depend on BLE.

UI-7D is **LANDED IN FULL**: the storage half 2026-08-06/07 (§6.2 AS-BUILT; `Inbox::erase`, console `del_msg`,
durable-append recovery [[B135]], target parsing [[B136]]) and **slice B, the §3.5 modal itself, on 2026-08-13**. What
remains is **metal only** (bench Part 19).
⛔ **CORRECTED IN PLACE 2026-08-13 — this paragraph read: *"What remains is slice B, the §3.5 modal itself … Until slice B
lands, inbox double press doing nothing is expected."* Both sentences are now false: a double press on a highlighted
inbox row opens the detail modal.**
⛔ **SUPERSEDED 2026-08-07 — this paragraph read: *"UI-7D remains independent and blocked on §6.2's durable erase
contract. Until it lands, inbox double press doing nothing is expected."* The erase contract is no longer the
blocker; only the UI is.** ⚠ On `heltec_v3` the store is a volatile RAM ring ([[B134]]), so slice B must not imply
a permanence the board does not provide — ⓘ **and slice B (landed 2026-08-13) does not: it claims only within-runtime
removal. ⛔ The correct reading of that volatility, spelled out because the short phrasing misled a reader once: a reboot
takes the record, its tombstone and the whole history together, so nothing reappears and nothing cross-reboot is
testable here.** UI-15's profile-based static join is honest about its boundary: a fresh device with no profile
cannot enter arbitrary RF numbers from one button. UI-16 likewise discovers only teams audible on the current PHY;
neither slice may imply a general cross-frequency scan.

**Prerequisites — BOTH DISCHARGED** (this heading said *"one discharged, one OUTSTANDING"* until 2026-08-06, [[B130]]).

✅ `send_channel … -t -l -e` is built and honoured — the parser accepts `-e`/`-l` (`console_parse.cpp:250,267`) and `on_command` enforces the refusal matrix including `no_fix` (`node.cpp:1402-1526`), with `team_channel_crypt` defaulting true (`node_carriers.h:184`).

✅✅ **CORRECTED 2026-08-06 ([[B130]]): `B38` / `B39` / `B40` ALL LANDED 2026-08-01 and are `[x]` in the register's
status checklist.** ⛔ **This block used to read *"`B38`/`B39`/`B40` must land first"* and to close with *"Do not
implement the emergency outcome path against the current core contract"* — a live prohibition on work that has since
shipped and is bench-ready.** That is the [[B121]] shape one document over: **a `⛔ Gated on …` banner is the
highest-authority sentence in a document and the least likely to be revisited**, while the bugs it names get fixed
elsewhere. **Re-verify a gate before obeying it (V1/V2).**

⚠ **One residual is real and is NOT discharged: [[B39]] closed as an INTERIM (comments plus an invariant test), and the
ambiguity it names is still live** — see §2.1 rule 2 and §B84, which is the design the UI actually ships against.
⇒ the rows below are kept as **the record of what each prerequisite was**, not as a gate:

| # | what it was — ✅ all three FIXED 2026-08-01 |
|---|---|
| **B38** | `channel_reoffer_confirm` returns on `rp.team` before `emit_channel_sent(true, …)`, and exhaustion emits `relayed=false`. ⇒ **`PICKED UP` is unreachable on the team plane**, and worse: §4's retry fires on `channel_no_relay`, so a distress call would always spend its full `kEmgMaxTries` budget and always display the failure headline (**`NOT RELAYED`** since B117; this row said `NOT HEARD`) — *even when every teammate received it*. A safety feature reporting failure on success. |
| **B39** | ⚠ **SUPERSEDED by §B84:** `ctr == 0` means **no local handle exists / status UNKNOWN**, not "not sent" — the third producer is a delegated success. Historical text: `CmdCode::queued` with `ctr == 0` means **not sent** (blocked, or seal failure). §4's "count on acceptance" rule is unimplementable until the result distinguishes accepted / blocked / refused. |
| **B40** | `channel_sent.ctr` carries only `id & 0xff` while the origination handle is the full 16-bit `next_ctr`. §2.1's exact-`ctr` correlation breaks permanently after 255 channel posts. |

> ⛔ **SUPERSEDED — kept as audit trail, NOT as instruction:** *"Until they land, §2.1's attribution degrades to
> 'accepted with a non-zero ctr' and `PICKED UP` must not be claimed. Do not implement the emergency outcome path
> against the current core contract."* **All three landed 2026-08-01; the outcome path is implemented and
> `PICKED UP` is reachable on the team plane.** ⚠ Carry the owner ruling that came with B38: **`relayed` means FIRST
> RELAY ONLY, never coverage — on a fully-1-hop team it reads `false` at 100 % delivery**, so a `NOT RELAYED` headline
> on a small co-located team is *accepted behaviour* and must not be "fixed" (ledger §1.9). ⚠ And B40's handle is a
> **LOCAL correlation handle only** — no peer echoes more than 8 bits, so it must never be matched against a
> *received* id.

### Phase B — Heltec V4 (separate specs, not this one)

| # | work | owning spec |
|---|---|---|
| B-1 | V4 radio port: FEM TX/RX switching, runtime GC1109-vs-KCT8103L detection, LoRa RST 12, `SX126X_REGISTER_PATCH` | ✅ **spec'd**: `2026-08-01-heltec-v4-radio-port-and-board-rf-seam-design.md` (slices R1-R3) |
| B-2 | `tx_power` semantics and per-board clamp — a compliance decision | ✅ **spec'd + RULED 2026-08-01** (= antenna dBm) — same spec, §4 + slice R4 |
| B-3 | V4 board port for the UI: battery polarity fixed HIGH, the `delay(10)` restructured, Vext ACTIVE=HIGH | this spec's §7 + §10.1, applied to a new env |
| B-4 | GPS driver + peer-location exchange + TEAM distance column | **new spec** (§10.3) |

B-1 and B-2 must land before any V4 hardware is trusted on air. B-3 is small once B-1 exists. B-4 is the largest and depends on a decision this spec does not make (how peer locations propagate on the team plane).

## 14. Open questions for the reviewer

1. ~~Which board is the real target?~~ **Resolved 2026-07-31.** Pins for both boards recovered from MeshCore (§10.1). **Phase A on V3, Phase B on V4**, with the V4 radio port and GPS as separate specs (§10.2, §10.3). Residual, small: the **V3 panel reset pin** is 21 per our own `board_ui.cpp:14` note, but MeshCore's V3 variant defines no `PIN_OLED_RESET` — worth one hardware confirmation before UI-3, since a wrong reset pin shows up as a dead panel and is easy to misdiagnose as a driver problem.
2. **§4 — is `kArmToFireMs` (with `InputCfg::fire_ms`, which must equal it) the right arm time?** Long enough to prevent pocket-fires, short enough not to feel broken. It is a guess and wants a bench opinion, not a code review. ⓘ The value lives at its declaration in `src/firmware_ui_model.h` and is deliberately not restated here.
3. ~~Should the emergency message include location?~~ **RESOLVED 2026-07-31 by owner ruling: yes, when available** — see §4.1. I had recommended omitting it in Phase A on staleness grounds; the owner ruled otherwise and the design now follows that. The residual implementation risk is the conditional `-l` (§4.1): getting it wrong converts "no fix" into "no alarm", so it belongs in the Task 8 bench matrix, not in a code review.
4. **§5 — is the idle-paint rule sufficient?** It prevents a paint from *starting* mid-exchange, but a paint already in progress when an RTS lands still holds the bus for one page (~3 ms). That should be inside the RX window slop, but it is an assumption a reviewer familiar with the metal RX path should confirm.
5. ~~**§3.2.2 — is the canned-text list right?**~~ **RESOLVED 2026-08-05:** emergency is one mandatory configurable preset. DM and channel use separate fixed-capacity catalogs of eight stable slots each; users may configure zero to eight active choices per list. Raising the capacity remains deliberately out of scope for the first persistent format.
6. **§3.6.3 — is profile-based static join sufficient for the first on-device version?** The proposed UI can join
   without a phone once a profile exists, but cannot type an arbitrary RF domain on a fresh unit. A numeric editor or
   QR/profile import is a separate interaction and storage decision.
7. ~~**§3.6.3 — how is a newly created team's key distributed without a phone?**~~ **RESOLVED 2026-08-06:** keep
   the random team id; `JOIN TEAM → NEARBY` selects an id observed on the current PHY, and an existing member must
   explicitly approve the candidate before the existing sealed `team grantkey` path is used (§3.6.4). BLE/serial and
   companion QR remain alternatives; an OLED private-key QR is not required for the first version.
