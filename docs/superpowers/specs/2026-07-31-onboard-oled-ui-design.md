# On-device OLED + one-button UI for a team mobile — design spec

*2026-07-31. From the owner's screen draft, refined in design dialogue. Fills the `mr_ui` seam that `2026-07-12-firmware-feature-split.md` slice 4 left empty.*

*Status: DRAFT, not implemented. Line references verified against the working tree at time of writing; board pins recovered from MeshCore's working V3 and V4 ports.*

*Phased: **Phase A ships the UI on Heltec V3**; **Phase B ports to V4** and adds GPS. Phase B's radio port and GPS driver are named here but specified elsewhere — see §10.2, §10.3, §13.*

---

## 0. Purpose

A team mobile with a display and one button must let a hiker **raise an alarm and know it was heard** without a phone. That ruling (owner, 2026-07-31) orders every trade-off below: the emergency path gets a dedicated gesture, an explicit confirm, honest delivery reporting and bounded auto-retry; browsing screens are deliberately thin.

The device remains fully usable with the companion app — this UI adds a degraded-mode surface, it does not replace the app or the console.

## 1. Scope

**In:** screen framework, one-button gesture input, status bar, team roster, merged inbox, two canned non-emergency messages, the emergency send path, battery indicator (incl. a new ESP32-S3 battery reader), screen blanking.

**Out (owner-scoped):**

| deferred | why |
|---|---|
| Editable settings screen (BLE on/off, OTA, GPS enable) | the companion app and console already cover it; one-button editing is expensive to build and rarely used. Screen-off timeout becomes a build constant. |
| Separate channel-message screen | merged with the DM inbox; two inbox screens double the cycle for the same interaction. |

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
set_power_save(bool) · button_pressed() · battery_sample_mv()
```

`board_ui.h` **must not include `firmware_ui_model.h`**. `firmware_ui.cpp` owns every decision about *what* is drawn; `board_ui.cpp` owns only *how pixels reach the panel*. That keeps U3 honest and is what makes the Phase B port a pin table rather than a rewrite — the V4 has the same panel, so a correct boundary means zero render changes.

### 2.1 Send attribution — the contract that makes outcomes trustworthy

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
   so three expiries end in sticky `NOT HEARD`. `CmdCode::queued` alone is not proof of transmission — a blocked or seal-failed channel post returns `queued` with `ctr == 0` (B39). `next_ctr` never yields 0, so zero is the sentinel. A zero-ctr result means **no LOCAL HANDLE exists and the transmission status is UNKNOWN** — it is *not* proof that nothing was sent (the third producer is a delegated **success**). The tracker therefore does **not** wait for a matching `send_blocked` / `send_failed`: an unattributable failure is **ignored**, and the slot is closed by **bounded expiry**, which supplies `channel_remote_mint` **and consumes one attempt** (§B84).
3. `channel_sent` completes an emergency **only** when its `ctr` matches the tracked handle (needs B40 for the full width).
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

**Why two pure headers.** `[env:native]` sets `test_build_src = no` but adds `-I src` (`platformio.ini:83`), so a pure header in `src/` is reachable from native tests — the established precedent is `firmware_config_parse.h` driven by `test_firmware_config_parse.cpp`. Gesture timing and emergency-state transitions are miserable to debug on hardware and trivial to test on host; both belong on the right side of that line.

`UiSnapshot` is plain data — unread counts, ages in ms, a bounded array of teammate rows, `batt_mv`, `ble_connected`, last send outcome. The model never sees `g_node`, so a native test can drive "3 teammates, one heard 4 h ago, emergency armed, `no_relay` returned" with no radio and no Arduino.

## 3. Screens and gestures

### 3.1 Cycle

| slot | screen | gate |
|---|---|---|
| 1 | STATUS | `MR_FEAT_OLED` |
| 2 | TEAM (peer list) | `MR_FEAT_OLED && MR_FEAT_TEAM` |
| 3 | INBOX (DM + CH merged) | `MR_FEAT_OLED` |
| 4 | SEND (team channel) | `MR_FEAT_OLED && MR_FEAT_TEAM` |

On a non-team build the cycle is STATUS → INBOX.

**Revised 2026-07-31 (owner).** An earlier draft gave every canned channel message its own slot to avoid a nested mode. Once the DM feature needed a compose step anyway, that reasoning inverted: **one compose sub-view, used by both send paths, is simpler than N slots** and keeps the cycle at four regardless of how many canned texts exist. Adding a fifth message now costs nothing instead of costing every user a press on every lap.

**"I'm in danger" appears in no list.** It is reachable only by long-press (§4). Two routes to the same dangerous action invite accidental alarms, and the navigational one would be the route nobody can use under stress.

### 3.2 Gestures

| gesture | meaning |
|---|---|
| short | **advance within the current list; at the end, move to the next screen** |
| double | activate the highlighted item — TEAM: open the DM compose sub-view for that peer · SEND: open the channel compose sub-view · sub-view: send the highlighted text (or leave, on `back`) · STATUS/INBOX: none |
| long | arm emergency, from **any** screen, sub-views included |

`short` is **list-aware**: on STATUS and SEND (single-item screens) it simply moves on, so the common case still reads as "next screen". On TEAM it walks the peer list and only leaves at the end. This is what frees `double` to mean "act on what is highlighted" everywhere, which is what makes peer selection possible with two gestures.

### 3.2.1 Compose sub-view

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
- The cursor starts on the **first message**, not on `back` — the peer was chosen deliberately one gesture ago, and these texts are benign.
- **Auto-exit after `MR_UI_BLANK_MS` of no input**, returning to the parent screen **without sending**. A modal that can outlive the user's attention is a modal that eventually sends the wrong thing.
- `long` still arms the emergency from inside it.

### 3.2.2 Canned text lists

| sub-view | texts |
|---|---|
| DM (from TEAM) | `Are you OK?` · `I'm OK` · `back without sending` |
| channel (from SEND) | `Got your message` · `All good` · `back without sending` |

`Delayed` was considered and left out of v1: it is a status broadcast, which is what the channel path is for, and every extra line costs a press for everyone on every visit. Adding it later is a one-line change precisely because the sub-view replaced per-message slots.

### 3.3 Layout

A persistent 8 px status bar on every screen, so "is anything wrong" never requires cycling:

```
+---------------------------------------+
| DM3 CH12  T4/5  3.9V                  |  <- 6x8, always
+---------------------------------------+
|  screen body: 4 lines @ 6x8,          |
|  or 2 lines @ 8x16 (emergency)        |
+---------------------------------------+
```

**No BLE indicator on V3.** `mrble::connected()` is `inline bool connected() { return false; }` on ESP32 (`device_ble.h:47`) — the whole module is nRF52-only. An indicator that is structurally always "disconnected" reads as a broken service rather than an absent one, so V3 omits it entirely. It returns in Phase B only if an ESP32 BLE transport exists by then.

**Battery is shown as volts, not a percentage.** A percentage requires a chemistry and discharge-curve policy nobody has approved; `3.9V` is honest with zero assumptions. `--` when unavailable, per the `console_json.h:126` rule.

STATUS becomes the detail view: ages spelled out ("DM 3, newest 1h05"), **our own team local id** (so the wearer can tell a teammate how to address them), the configured `team_id`, registration state, and battery mV.

TEAM shows one row per teammate: a display label resolved through `team_key_of_id()` → `peer_name_find()`, falling back to `0x<hash>` and then the bare team id; plus last-heard age, signal quality and hops. When `rt_team_count()` exceeds `kMaxTeamRows`, the screen shows the true total and a truncation marker (`3/12`) — it must never present the cap as the team size. **Phase B adds a distance column on V4**, rendered only when both our fix and the peer's location are known and fresh — omitted, never estimated (§10.3).

### 3.4 Direct messages to a teammate

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

### 3.4.1 DM outcome states

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

## 4. Emergency state machine

```
IDLE --hold 800ms--> ARMING
  ARMING: "RELEASE TO CANCEL / EMERGENCY IN 3..2..1" drawn while held
    release < 3.5s ------> CANCELLED (brief toast) -> IDLE
    held through 3.5s ---> FIRING
FIRING: post the encrypted team channel message "I'm in danger" (+ location if a fix exists)
    send_blocked{next_ms}          -> BLOCKED   (show "retry in Ns", auto-retry at next_ms)
    channel_sent{relayed=true}     -> PICKED UP
    channel_sent{relayed=false}    -> NOT HEARD (auto-retry x3 w/ backoff) -> NOT HEARD (sticky)
any state: inbound channel msg from a teammate -> REPLY: <name> <text>
sticky until acknowledged (double)
```

Two constraints the draft could not have anticipated, both verified:

- **`send_blocked` must be handled.** `channel_min_interval_ms = 10000` (`protocol_constants.h:377`) means a second emergency post inside 10 s is refused **pre-TX** with `send_blocked{min_interval, next_ms}` (`command.h:103, 177-180`). A safety UI that displayed "sent" there would be lying. Show the countdown; auto-retry at `next_ms`.
- **Delivery evidence is weaker than "delivered", and the wording must say so.** Team channel messages carry no end-to-end ack — there is no `DATA_FLAG_E2E_ACK_REQ` anywhere in `node_channel.cpp`. The only signal is `PushKind::channel_sent` (`command.h:105`, `node_channel.cpp:403-416`): `relayed=true` means a neighbour was overheard re-flooding the post. Render that as **`PICKED UP`**, never `DELIVERED`. True human confirmation arrives only as a teammate's reply — which is precisely why "Got your message" earns slot 4.

**Auto-retry bound: exactly 3 accepted transmissions**, then a sticky `NOT HEARD` the user can re-fire with `double`. Unbounded retry is not acceptable — it would burn the duty budget the rest of the team needs to answer.

★ **"3" means three *transmissions*, not three retries after the first.** Both readings appeared in an earlier draft; this is the binding one and the native tests pin it.

★★ **An attempt is counted on ACCEPTANCE, never on request.** A parser refusal or a pre-TX `send_blocked` puts nothing on the air, so it must not consume one of the three alarms. The counter increments when `CmdResult` comes back accepted with a `ctr`.

**Retry timing, corrected after review:**

- The deadline is `now_ms + next_ms`, computed from **the moment the block outcome arrived** — not from the originating gesture. An earlier draft based it on the gesture timestamp, which is stale by the time an automatic retry blocks.
- Comparisons are wrap-safe unsigned (`now - deadline` sign trick), not `now >= deadline`.
- **`next_ms == 0` is legal and means "the floor has passed but a cap or duty limit still blocks"** (`command.h:203`). It must not be treated as "retry immediately": doing so spins the retry every tick and burns all three alarms in milliseconds. Phase A policy: a UI-side recheck backoff starting at 2 s, doubling, capped at 30 s, staying in `BLOCKED` and **consuming no attempt** until a send is actually accepted.

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

★ **`emergency_hold_until_ms` must actually be READ by the blanking rule.** It is a deadline, not a duration: the panel stays on until `now >= emergency_hold_until_ms`, compared wrap-safely like the retry deadline. An earlier plan draft wrote the field on fire and on reply but then measured the hold from *last input* instead — so a reply that arrived while the user's hands were elsewhere never extended the window it had just set, and `picked_up` fell back to the ordinary 15 s blanking.

The model marks itself dirty **only when the visible countdown digit changes**, not every tick — otherwise the emergency repaints at the full tick rate for no visual difference.

### 4.4 Human confirmation — the `REPLY` state

`PICKED UP` is relay evidence. The only *human* confirmation is a teammate answering, so an inbound team channel post arriving while the emergency is live or sticky transitions to a sticky `reply` state carrying a bounded sender label and a display-clamped body.

**Scope: a post qualifies only if it is on `MR_UI_TEAM_CHANNEL_ID` *and* comes from OUR OWN TEAM.** Accepting any traffic on that channel would let unrelated chatter read as "someone answered my distress call", which is the same false-confirmation class as §2.1.

> ⚠ **FACTUAL CORRECTION 2026-08-05 (§B103 / register B103 — shipped behaviour, narrow correction only, no redesign).** This paragraph originally said the channel id **alone** qualified a reply, and the code shipped that way. It was a live safety defect: `Node::ingest_channel_m` (`lib/core/node_channel.cpp:211-212`) drops a *foreign team's* post, but a normal leaf post (`team_id == 0`) **falls through and is ingested by everyone** — so with `MR_UI_TEAM_CHANNEL_ID == 0` any node in radio range posting plaintext on channel 0 rendered as a distress REPLY. The shipped guard is now `pu.channel_id == MR_UI_TEAM_CHANNEL_ID && g_node.same_team(pu.team_id)`, and the clause carrying the safety weight is `same_team`'s implicit `team_id != 0` — not the channel equality, which ingest already guarantees for team traffic. ⓘ Consequence, deliberate: on a node with **no** team the REPLY indication is unreachable, because without a team there is no key and no membership and so nothing that could make a reply trustworthy.

★ **And only after an alarm was actually transmitted.** The state whitelist is `firing` · `blocked` · `picked_up` · `not_heard` · `reply`, and **at least one emergency transmission must have been accepted**. `arming`, `cancelled` and `failed` are excluded: in all three, nothing went out, so a coincident channel-0 post becoming `REPLY` would manufacture a confirmation of a message that was never sent — including during the 3.5 s hold *before* the user has even committed.

### 4.1 ★ Location on the distress call (owner ruling 2026-07-31)

Per `2026-07-30-channel-crypt-and-location-privacy-design.md` §2.2.1, an encrypted channel post carries **text and location together** — the sealed inner becomes a flags byte (`bit0` text, `bit1` location) with `pack_loc6`, not the either/or the earlier T-K2 draft had. **The distress call includes location when one is available.** This supersedes the recommendation in §14 Q3 to omit it.

The exact invocation is `send_channel <ch> "I'm in danger" -t -l -e`.

⚠ **The UI must attach `-l` conditionally, and this is a safety-critical detail, not a nicety.** That spec's matrix refuses `-t -l` outright when there is no fix (`lat_e7 == 0 && lon_e7 == 0`) with `no_location`. A UI that always sent `-l` would therefore turn "no fix" into **no alarm at all** — the worst possible failure for this feature. So:

| condition | command |
|---|---|
| `lat_e7 != 0 \|\| lon_e7 != 0` | `send_channel <ch> "I'm in danger" -t -l -e` |
| no fix | `send_channel <ch> "I'm in danger" -t -e` |

Both forms are explicitly `-e`, which also satisfies the "must actually be sealed" half of the O6 ruling without depending on `team_channel_crypt`'s default.

**Phase A caveat, stated once and then accepted.** On V3 there is no GPS, so the only coordinate is whatever was typed via `cfg set lat`/`lon` — potentially hours stale for a walking hiker, and a stale position in a rescue context can send help to the wrong place. I raised this and the owner ruled to include it when available; the ruling stands and the design follows it. Phase B's live fix removes the concern entirely. The canned non-emergency messages ("Got your message", "All good") do **not** carry location — only the distress call does.

## 5. Paint and power policy

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

The panel blanks after `MR_UI_BLANK_MS` (build constant, proposed 15000) of no input. Any press wakes it and **the waking press is consumed** — except a long press, which wakes *and* arms (§4.2). Emergency states hold the panel on for at most **`kEmgHoldMs`** (owner-re-ruled 2026-08-04: 120000 → **30000**; ★ read the CONSTANT — this clause said "120 s" and went stale the day it was re-ruled), after which it blanks with state retained; **the next press then restores the emergency screen, not the cycle** — ⓘ that half is the BLANKED case and stays correct under **B71**, whose ruling governs the *awake-with-an-outcome* case instead (next short press → back to the cycle).

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

## 6. Data sources

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
| BLE | **not shown on V3** — `mrble::connected()` is inert on ESP32 (`device_ble.h:47`). See §3.3 |
| battery | **new, and cached** — see §7 |
| send outcomes | correlated by the tracker in `firmware_ui.cpp`, never fed raw to the model — see §2.1 |

### 6.1 Inbox adapter

No new inbox subsystem is needed; `Inbox::pull()` already visits both stores and every `InboxEntry` carries its `InboxKind`, sender/channel metadata and body.

- Call `g_node.inbox().pull()` **directly**. Do **not** dispatch a textual `pull_inbox` into the 512 B `BufferSink` — the NDJSON is unbounded and would truncate.
- `pull()` returns the **DM block oldest-first, then the channel block oldest-first** (`inbox.h:107-109`) — the two seq spaces are independent and there is no interleaved chronological stream.
- Phase A renders **one screen with labelled rows** (`DM` / `CH <n>`) in that block order. Chronological interleaving by `rx_time_ms` is explicitly *not* implied; adopting it later needs a stated rule about reboot/uptime semantics first.
- Retain only the bounded number of rows the panel can browse; clamp sender and body text to the display width. ⚠ **Allocate the bound PER KIND** (e.g. 4 DM + 4 channel), not as one shared pool filled in visit order — `pull()` visits the channel block second, so a shared "keep the newest N" would let a chatty channel evict every DM row, on a screen whose whole point is showing both.
- `short` walks the retained rows and leaves at the end, like TEAM. When more rows exist than are retained, show that rather than implying the list is complete.
- Viewing on the panel does **not** advance the durable `mark_read` cursor. The UI's unread counters stay session-local (§6 above); moving the durable cursor from a button press would desynchronise the companion app, which is the cursor's real owner.

## 7. Battery reader (new work)

The only battery reader in the tree is nRF52-only: `#if defined(NRF52_PLATFORM) && defined(PIN_VBAT) && !defined(MR_NO_BATT)` (`firmware_commands.cpp:299-304`, method at `:709`). Both Heltec boards are ESP32-S3, so `batt_mv` is unavailable on either today, and `console_json.h:126` records the project rule: an unavailable reading is **omitted, never faked**. The status bar must render `--` rather than a plausible wrong percentage.

★ **Sampling is cached and slow, not per-tick.** The board function performs one sample: a divider toggle plus eight `analogRead()` calls. `firmware_ui.cpp` calls it **at boot and then every 30 s**, only under the §5 rule 1 MAC-idle predicate, and keeps the last good value between samples. An earlier draft sampled inside `build_snapshot()` on every service pass — ADC work on the radio hot path for a value that changes over minutes. Until the first successful sample the field is unavailable and renders `--`.

⚠ **The cadence must gate on "attempted", not on "succeeded".** A reader that returns the documented unavailable value (`<0`) on a board without a battery would otherwise be retried every idle pass forever — eight ADC reads per tick for a value that will never arrive. Advance the 30 s deadline after **every** attempt; keep the last good value separately.

Add an ESP32-S3 reader behind the same shape (a board-gated function returning millivolts, `<0` = unavailable). Both methods come from MeshCore — the same provenance `firmware_commands.cpp:709` used for the nRF52 reader ("the authoritative MeshCore XiaoNrf52Board method").

Pins and formula are **identical** on V3 and V4: `ADC_CTRL` 37, `VBAT_READ` 1, 10-bit resolution, mean of 8 samples, `mv = 5.42 * (3.3/1024.0) * raw * 1000`. Two things differ:

**V3 — polarity is auto-detected** (`HeltecV3Board::begin()`), because boards past rev 3.2 inverted it:

```
pinMode(PIN_ADC_CTRL, INPUT);
adc_active_state = !digitalRead(PIN_ADC_CTRL);   // probe the idle level, then invert
pinMode(PIN_ADC_CTRL, OUTPUT);
digitalWrite(PIN_ADC_CTRL, !adc_active_state);   // park inactive
```
Read: drive `adc_active_state`, sample, drive `!adc_active_state`. **No settling delay.** Do not hardcode LOW — the auto-detect exists because the hardware genuinely varies within "V3".

**V4 — fixed ACTIVE=HIGH, plus a settling delay:**

```
digitalWrite(PIN_ADC_CTRL, HIGH);
delay(10);                        // ⚠ see below
... sample ...
digitalWrite(PIN_ADC_CTRL, LOW);
```

⚠ **V4's `delay(10)` is the same hazard class as a full-frame repaint.** Ten milliseconds of blocking wait against `cts_to_data_gap_ms = 5` will break an in-flight exchange. The Phase B port must not copy it verbatim: either sample only under the §5 rule 1 MAC-idle predicate, or restructure as a small state machine across ticks (enable → return; sample on a later tick → disable). Battery is a once-per-N-seconds reading; there is no reason for it to ever block. Phase A (V3) has no delay to remove, which is one more reason it lands first.

Verify against a multimeter on the cell before trusting the constant on either board — the discipline the nRF52 comment demands, and the divider is a per-revision property.

## 8. Dependencies

The tree has **no display library** — `board_ui.cpp` is deliberately driver-free so the Heltec build links today. Phase A adds one, and V4 reuses it unchanged (same panel, same pins).

**Recommended: U8g2 in page-buffer mode** (`U8G2_SSD1306_128X64_NONAME_1_HW_I2C`). Two reasons, both structural rather than aesthetic: the 1-page mode uses a **128 B** buffer instead of 1024 B, and its natural draw loop is exactly the 8-page chunking §5 rule 3 requires — the constraint and the library's grain agree. Alternative is Adafruit_SSD1306 + GFX + BusIO (three deps, full 1024 B buffer, no page mode).

**Pin the version exactly, never a caret.** The RadioLib ruling (`platformio.ini:234` and its note, on this very env) exists because a caret let different checkouts resolve different versions and silently skewed board RAM/Flash baselines.

## 9. Feature gating

- `MR_FEAT_OLED` (board capability) gates the UI TUs. Default 0 (`mr_features.h`); set to 1 on `heltec_v3` (`platformio.ini:215`), inherited by `heltec_mobile`.
- `MR_FEAT_OLED && MR_FEAT_TEAM` gates the TEAM and SEND slots, both compose sub-views, and the team fields of the status bar. A non-team build cycles STATUS → INBOX only.

**Deviation from the owner's draft, approved 2026-07-31:** the draft said "gate it behind `MR_FEAT_TEAM`". Composing the two gates instead means a static OLED board still gets a status screen and an inbox rather than a blank panel, at no cost. `MR_FEAT_TEAM` alone would have conflated a board capability with a protocol plane.

**Phase A target env: `heltec_mobile`** (`platformio.ini:372`) — `heltec_v3` plus `MR_PROFILE_MOBILE`, which sets `MR_FEAT_REMOTE_MGMT=0` and leaves `MR_FEAT_TEAM=1`. It already inherits `MR_FEAT_OLED=1` from `heltec_v3`, so no env change is needed to start.

Phase B adds a `heltec_v4` env (and a `heltec_v4_mobile` extending it) once the radio port of §10.2 exists. GPS gets its own feature flag rather than riding `MR_FEAT_OLED` — a GPS is not a display, and a future non-display tracker would want one without the other.

## 10. Board port table

All values recovered from MeshCore's working ports — `~/MeshCore/variants/heltec_v3/` and `~/MeshCore/variants/heltec_v4/` — not from datasheet reading.

### 10.1 V3 vs V4

| item | Heltec V3 (Phase A) | Heltec V4 (Phase B) | same? |
|---|---|---|---|
| MCU | ESP32-S3 (`esp32-s3-devkitc-1`) | ESP32-S3, 16 MB flash, 2 MB PSRAM | ~ |
| panel | SSD1306 @ 0x3C, SDA **17**, SCL **18** | SSD1306 @ 0x3C, SDA **17**, SCL **18** | ✅ |
| panel reset | **21** per our own `board_ui.cpp:14` note — MeshCore's V3 variant defines no `PIN_OLED_RESET`; **confirm on hardware** | **21** (`PIN_OLED_RESET`, explicit) | ✅ (pending V3 confirmation) |
| **user button** | **GPIO 0** | **GPIO 0** | ✅ |
| battery pins | `ADC_CTRL` **37**, `VBAT_READ` **1** | `ADC_CTRL` **37**, `VBAT_READ` **1** | ✅ |
| battery formula | `5.42 * (3.3/1024) * mean8(raw)` | identical | ✅ |
| **battery ctrl polarity** | **auto-detected at boot** (`adc_active_state = !digitalRead(pin)` as INPUT) — boards >3.2 differ; nominal ACTIVE=LOW | **fixed ACTIVE=HIGH** | ❌ |
| **battery settling delay** | none | **`delay(10)`** | ❌ |
| peripheral power | `VEXT_EN` **36**, polarity implicit (default) | `VEXT_EN` **36**, explicitly **ACTIVE=HIGH** | ❌ |
| LoRa SPI | NSS 8, DIO1 14, BUSY 13, SCLK 9, MISO 11, MOSI 10 | identical | ✅ |
| **LoRa reset** | **`RADIOLIB_NC`** (matches our `platformio.ini:218`) | **GPIO 12** | ❌ |
| TX LED | 35 | 35 | ✅ |
| **front-end module** | **none** | **PA + LNA, switched every TX** | ❌ |
| **`LORA_TX_POWER`** | **22** = 22 dBm at the SX1262 | **10** = **22 dBm output** | ❌ |
| RX register patch | not set | `SX126X_REGISTER_PATCH=1` (reg 0x8B5) | ❌ |
| GPS pins | RX 47, TX 48, EN 26 — defined, **GPS not enabled** | RX 38, TX 39, RESET 42, EN 34 — **`ENV_INCLUDE_GPS=1`** | ❌ |

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
| `test_firmware_ui_model.cpp` | screen cycle incl. the non-team compile-out, cursor advance and wrap on TEAM/INBOX, the full emergency machine (arm → cancel, arm → fire → `blocked` → retry-at-`next_ms` → `picked up`; and → `no_relay` → 3 retries → sticky), dirty-flag correctness, blanking and emergency-hold timing |

Both are pure and table-driven; no Arduino, no radio, no display.

**On-target checklist** (Phase A: `heltec_mobile` = Heltec **V3**, bench):

1. Emergency from each screen, in the dark, with gloves — reaches FIRING without reading the panel.
2. Release at 3.0 s cancels; release at 3.6 s fires.
3. Two nodes: fire with the second powered off → `NOT HEARD` after 3 retries. Power the second on → fire again → `PICKED UP`. Reply from the second → `REPLY` shown.
4. Fire twice inside 10 s → second shows `BLOCKED` with a live countdown, then auto-fires.
5. **Paint-vs-radio:** run the s18-style DM load while cycling screens continuously; confirm no CTS timeout regression. This is the check that §5 rule 1 exists for, and the one most likely to fail.
6. Blank/wake: waking press does not change screen.

**Additional acceptance cases (from the 2026-08-01 review, all adopted):**

- a canned or console channel post completing while the emergency is `firing` **cannot** alter the emergency state
- a blocked **DM** cannot put the emergency screen into `BLOCKED`
- a parser refusal leaves `SENDING...` and shows an actionable failure. ⚠ **§B84: `send_failed{unsealable}` does NOT** —
  it is unattributable (six non-channel operations emit `dst == 0`) and is therefore **ignored**; that path terminates
  via bounded expiry in `NOT HEARD`, losing its precise reason (owner-accepted)
- `next_ms == 0` neither deadlocks nor spins — the backoff applies and no attempt is consumed
- exactly **three accepted transmissions** occur even when preceding requests were blocked
- long gestures work from both compose sub-views and from a blanked panel
- the arming countdown digit visibly changes; `CANCELLED` auto-returns to the parent
- a matching **same-team** reply on `MR_UI_TEAM_CHANNEL_ID` becomes sticky confirmation; other traffic on that channel — including a `team_id == 0` leaf post from any node in range — does not (factual correction 2026-08-05, see §4.4)
- a DM ack/failure is matched by `ctr` **and** peer; unrelated acks are ignored
- the inbox shows bounded labelled rows via `Inbox::pull()`
- a blanked panel produces **no repeated I²C traffic** (instrument or trace-count it)
- battery sampling happens only at its slow cadence and never starts while the MAC is busy
- `heltec_v3` and `heltec_mobile` compile with the final TU ownership and `mrfw::dispatch` qualified

**Added by the second review (2026-08-01), all adopted:**

- a team relay produces exactly one truthful `channel_sent{relayed=true}` while coverage retries remain valid *(needs B38)*
- a blocked emergency returns **no** accepted counter and consumes none of the three transmissions *(needs B39)*
- a channel seal failure leaves `SENDING...` and shows its exact reason
- channel counters **255 · 256 · 257 · 65535→1** correlate correctly, with a low-byte-colliding unrelated outcome interleaved *(needs B40)*
- an outstanding DM or canned channel transaction **cannot delay** emergency command execution — proven at the firmware integration boundary, not only in the pure model
- delayed outcomes from an abandoned normal transaction cannot move the emergency
- one frame redraws the scene **once per page** and performs one page transfer per MAC-idle tick
- `PICKED UP` and a late reply obey the **`kEmgHoldMs`** hold deadline across `millis()` wrap (owner-re-ruled
  2026-08-04: 120000 → **30000**. ★ Read the CONSTANT — this line said "120 s" and went stale the day it was re-ruled)
- `e2e_ack_timeout` shows `NO CONFIRM`, and a late ack upgrades it to `DELIVERED`
- an unavailable battery reader is retried at 30 s cadence, not loop cadence
- channel traffic during `arming`, `cancelled` or `failed` cannot become a distress `REPLY`
- the bounded inbox retains labelled DM **and** CH rows — a chatty channel cannot evict every DM row

**Gate:** the standing D1 gate applies — native, s18 md5 exact (this work is `src/`-only, so it is inert by construction and the md5 must not move), and the board envs per §11.

## 13. Slices

Slices are named `UI-n` deliberately: bare `U1`/`U3` would collide with the CLAUDE.md working-rule IDs (U1 = reuse-before-writing, U3 = feature logic in a `firmware_*` module), both of which this spec cites.

### Phase A — Heltec V3 (this spec)

| # | slice | gate |
|---|---|---|
| UI-1 | `firmware_ui_input.h` + native test | native |
| UI-2 | `firmware_ui_model.h`: screens, list-aware cursor, compose modal + native test | native |
| UI-3 | emergency + DM outcome machines in the model (§4, §3.4.1) + native test | native |
| UI-4 | **send tracker + typed send result** (§2.1) — the attribution layer | native + on-target |
| UI-5 | board canvas port: U8g2 behind `board_ui.h`, page-chunked paint, `set_power_save` blanking | on-target (V3) |
| UI-6 | button GPIO 0 into the classifier; `firmware_ui.cpp` render policy; the cycle becomes live | on-target (V3) |
| UI-7 | TEAM peer list + compose sub-views + inbox adapter over `Inbox::pull()` (`MR_FEAT_TEAM`) | on-target (V3) |
| UI-8 | emergency end-to-end on hardware incl. blocked/retry/reply | on-target |
| UI-9 | V3 battery reader (auto-detected ADC_CTRL polarity, no delay) + the 30 s cache | on-target, multimeter-verified |

Renumbered after the 2026-08-01 review. **UI-4 is new and is the review's first ordering item**: without send attribution, UI-3's emergency machine can be completed by an unrelated push, so the tracker must exist before the emergency is trusted on hardware.

UI-1 through UI-4 are pure or near-pure and can start immediately. UI-5 is blocked only on the display-library choice (§8). Every V3 pin UI-5/UI-6/UI-9 needs is known (§10.1) except the panel reset pin, which wants a hardware confirmation.

**Prerequisites — one discharged, one OUTSTANDING.**

✅ `send_channel … -t -l -e` is built and honoured — the parser accepts `-e`/`-l` (`console_parse.cpp:250,267`) and `on_command` enforces the refusal matrix including `no_fix` (`node.cpp:1402-1526`), with `team_channel_crypt` defaulting true (`node_carriers.h:184`).

⛔ **`B38` / `B39` / `B40` must land first** (`docs/2026-07-30-open-bug-register.md`, registered 2026-08-01 from the second review). An earlier revision of this spec claimed no core prerequisite remained; **that was wrong** — the claim was made without checking the channel-origination outcome contract:

| # | why Phase A cannot ship without it |
|---|---|
| **B38** | `channel_reoffer_confirm` returns on `rp.team` before `emit_channel_sent(true, …)`, and exhaustion emits `relayed=false`. ⇒ **`PICKED UP` is unreachable on the team plane**, and worse: §4's retry fires on `channel_no_relay`, so a distress call would always spend its full 3-attempt budget and always display `NOT HEARD` — *even when every teammate received it*. A safety feature reporting failure on success. |
| **B39** | ⚠ **SUPERSEDED by §B84:** `ctr == 0` means **no local handle exists / status UNKNOWN**, not "not sent" — the third producer is a delegated success. Historical text: `CmdCode::queued` with `ctr == 0` means **not sent** (blocked, or seal failure). §4's "count on acceptance" rule is unimplementable until the result distinguishes accepted / blocked / refused. |
| **B40** | `channel_sent.ctr` carries only `id & 0xff` while the origination handle is the full 16-bit `next_ctr`. §2.1's exact-`ctr` correlation breaks permanently after 255 channel posts. |

Until they land, §2.1's attribution degrades to "accepted with a non-zero ctr" and `PICKED UP` must not be claimed. **Do not implement the emergency outcome path against the current core contract.**

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
2. **§4 — is 3.5 s the right arm time?** Long enough to prevent pocket-fires, short enough not to feel broken. It is a guess and wants a bench opinion, not a code review.
3. ~~Should the emergency message include location?~~ **RESOLVED 2026-07-31 by owner ruling: yes, when available** — see §4.1. I had recommended omitting it in Phase A on staleness grounds; the owner ruled otherwise and the design now follows that. The residual implementation risk is the conditional `-l` (§4.1): getting it wrong converts "no fix" into "no alarm", so it belongs in the Task 8 bench matrix, not in a code review.
4. **§5 — is the idle-paint rule sufficient?** It prevents a paint from *starting* mid-exchange, but a paint already in progress when an RTS lands still holds the bus for one page (~3 ms). That should be inside the RX window slop, but it is an assumption a reviewer familiar with the metal RX path should confirm.
5. **§3.2.2 — is the canned-text list right?** Largely moot now: the compose sub-view means an extra text costs one list row, not a cycle slot, so adding "moving on" or "waiting here" later is nearly free. The only real question is whether either DM list should differ per direction — currently both ends see the same two texts.
