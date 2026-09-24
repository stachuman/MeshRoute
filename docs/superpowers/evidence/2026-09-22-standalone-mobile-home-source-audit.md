<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com>; specification audit: Claude, 2026-09-22 -->
# Standalone mobile Home — source audit and measurements

**2026-09-22 · specification evidence, not a gate.** This record backs revision 2 of the
[standalone-mobile design](../specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md). It grants no
software PASS, runs no gate and changes no production, test, tool, simulator or board file. Line numbers are
navigation hints at the audited tree; the symbol is the anchor (P4).

## 1. Tree and inputs

| Item | Observed |
| --- | --- |
| MeshRoute HEAD | `20357578700270d7315dcf86bc3bb5725d8024cb` (`repository cleanup`) |
| Pre-existing dirty state | `B164.md` deleted; `docs/2026-07-30-open-bug-register.md` and `tracker.md` modified (owner's B439 refresh) — all three preserved |
| Handoff manifest | [`2026-09-22-standalone-mobile-home-handoff-inputs.json`](2026-09-22-standalone-mobile-home-handoff-inputs.json): **94/94 selected inputs hash-identical**; the three dirty paths match; the only new paths were the two handoff artefacts, which the manifest excludes by design |
| Simulator | `/home/staszek/lora-universal-simulator` at `6585649ea5a780f0542b2931853a667be56a5b2b`, porcelain empty; read only |

No subsequent owner work or unexplained input change was found, so every fact below was read from the snapshot the
handoff describes.

## 2. Method and limits

- The author read the contract-critical paths directly: input classification, the model gesture dispatch, the
  send composer and executor, console parsing, core admission for `send` / `send_channel`, the sealed-DM and
  sealed-channel packers, `/mrui` and `/mrid` records, name storage and wire use, Status rows, Inbox detail and
  records, receive routing and emergency drawing.
- Three read-only exploration agents surveyed provisioning flows, board profiles/fonts/loop cadence and the
  test/instrument inventory. Their reports were used only where spot-checked against the source (§3.6, §3.11,
  §5 B418); agent-only claims are marked.
- **Run:** two disposable host `sizeof` programs and one press-count model, all in the session scratch directory
  (§4). **Not run:** native suite, corpus, board builds, probes, mutation batteries, ABI probe, hardware.

## 3. Source facts by area

### 3.1 Input classification

| Fact | Anchor |
| --- | --- |
| Defaults: debounce 25, double gap 350, arm 800, fire 3500 ms | `src/firmware_ui_input.h`: `InputCfg` (:20) |
| A tap becomes `short_press` only after 350 ms without a second debounced press, measured from the release edge; a second press debounced inside the window becomes `double_press` on its release | `InputFsm::update` (:31), `InputFsm::on_release` (:65) |
| Any press held 800 ms arms emergency (`long_arm`); release before fire gives `long_cancel`; a slow second press of a double therefore becomes an emergency arm, not a double | `InputFsm::update` :38–39, `on_release` :67–68 |
| `active()` keeps the CPU awake while a gesture is undecided | `InputFsm::active` (:62) |

### 3.2 Model dispatch and navigation

| Fact | Anchor |
| --- | --- |
| Screens: status, team, inbox, send, settings; default status | `src/firmware_ui_model.h`: `Screen` (:244), `UiState::screen` |
| Order of `on_gesture`: every gesture stamps `_last_input_ms`; long gestures go to emergency first; a blanked panel consumes the waking press; the overlay dismisses a presented outcome on short and absorbs double; then detail modal, compose, provisioning, settings editor; finally short → `advance_or_next`, double → `activate` | `UiModel::on_gesture` (:2668) |
| Emergency closes the detail modal, provisioning sub-view and settings editing at `long_arm`; compose closes only at `long_fire`; a settings draft survives | `UiModel::emergency_gesture` (:5493) |
| Passive/entered screens: an un-entered screen is one row, so short passes it; entered lists end in BACK | `UiModel::list_len`, `next_screen` (:5476), UI-17 §1 |
| Compose list: short cycles, double on a text row **queues immediately** (no review); result phase closes on either press | `UiModel::compose_gesture` (:5363), queue at :5421 |
| A catalog generation change closes a selection-phase compose without sending | `preset_generation_moved` (:5455) |
| Blank after `kBlankMs` 15 s unless an emergency hold or message wake is active | `kBlankMs` (:221), `blank_due` (:5322) |
| On blank, only `invite_confirm`, `invite_need_pubkey` and `saved_key` are backed out; `create_confirm`, `join_confirm`, `nearby_confirm`, `saved_keys_confirm` survive with BACK default | `UiModel::on_tick` blank block (:2908–2926) |

### 3.3 Rendering, geometry and glyphs

| Fact | Anchor |
| --- | --- |
| Body origin x=12, 19 columns of 6 px, five rows at baselines 19/29/39/49/59 | `src/firmware_ui.cpp`: `kBodyX`/`kBodyCols` (:997–998), `kBodyY0`/`kBodyDy`/`kBodyRows` (:964–966) |
| Status mark 24×24 at 12,12; rows 0–2 at x=40 (14 cols), rows 3–4 at x=12 (19 cols) | `kStatusMarkX..kStatusTextX` (:1033–1038); `src/firmware_ui_status.h`: `kStatusNarrowCols`/`kStatusWideCols` (:60–61) |
| Frames paint from frozen copies `s_frame_state` (UiState) and `s_frame_snap` (UiSnapshot); the snapshot is also a per-tick stack local | `s_frame_state`/`s_frame_snap` (:390–391), `mr_ui_tick` (:2464) |
| Fonts: `u8g2_font_6x10_tf` body, `u8g2_font_10x20_tf` emergency headline; glyphs 0x20–0x7E and 0xA0–0xFF (191); lowercase present; no glyph for 0x80–0x9F; raw bytes are glyph codes | `variants/heltec_common/board_ui.cpp`: `set_font` (:303–307); U8g2 2.35.30 font header "Glyphs: 191/1597" (agent-decoded, header line checked) |
| Message bodies are sanitised to printable ASCII, other bytes as `.`; **peer names are not** — `label_from_hash` draws the cached bytes | `ui_display_byte` (model :1410); `label_from_hash` (`firmware_ui.cpp` :440) |
| No boot splash; the old UI-5 splash only proved the canvas survived `--gc-sections` | `mr_ui_init` comment (`firmware_ui.cpp` :2459) |

### 3.4 Current Home (Status) body

| Row | Content | Anchor |
| --- | --- | --- |
| 0 (x=40) | `TEAM %08lX` / `NO TEAM` | `ui_status_team` (status.h :83) |
| 1 (x=40) | `ME T%u` / `ME NO ID` / blank without team — **a team-local ID, not the name** | `ui_status_me` (:96) |
| 2 (x=40) | `%s KNOWN` / `NO TEAM KEY` / blank | `ui_status_known` (:125) |
| 3 (x=12) | `%s NEW / HOME %s` (≤18) or `%s NEW` on non-mobile builds | `ui_status_unread_home` (:155) |
| 4 (x=12) | `RESTART NEEDED` > coordinates > `NO LOCATION` | `ui_status_location` (:204) |

The snapshot carries `my_key_hash32` but no own name (`UiSnapshot::my_key_hash32`, model :1939; filled
`firmware_ui.cpp` :867).

### 3.5 Identity and name

| Fact | Anchor |
| --- | --- |
| `/mrid` = magic, version, `name_len`, seed[32], name[32], lat/lon; exact-version load | `src/device_nv.h`: `IdBlob` (:146), `load_id` (:1444), `save_id` (:1448) |
| Console `cfg set name`: load `/mrid`, **on load failure rebuild from the running seed only**, clamp the value to 32 bytes silently, save, then `set_name` live only on success | `src/firmware_config.cpp`: `handle_cfg_set` name arm (:274–282) |
| The lat/lon arm has the same fallback: on load failure the saved record drops the name and the other coordinate | `handle_cfg_set` lat/lon arm (:262–271) |
| Boot re-mints the identity when `/mrid` cannot be loaded | `setup` identity block (`fw_main.cpp` :915–923) |
| Core name: `_name[32]`, `set_name` clamps; `effective_name` returns the name or `MeshRoute node: 0x<HASH8>` (26 bytes), never NUL-terminated | `lib/core/node.h` :574–576, :2978; `lib/core/node.cpp`: `Node::effective_name` (:41) |
| `effective_name` — including the 26-byte default — is what INTRO, the pubkey answer and WANT_PUBKEY transmit; peers cache it as the name | `lib/core/node_hashlocate.cpp` :767, :1262, :2321 |
| Panel labels: `peer_name_find` copies `min(len, cap)` without a terminator (open B241); label buffers are 15 bytes; TEAM rows show 6 columns | `node_hashlocate.cpp` :450; `kLabelCap` (model :224); TEAM row format S-11 |
| (added 2026-09-23, D10) Receivers ignore an empty advertised name; on a non-pinned row every advertised name replaces the cached one, a `peername` label included (B447); a pinned row ignores on-air sets | `node_hashlocate.cpp` :344, :346; `peer_name_set` (:386) |
| The codec already has a nameless form: the key request's name trailer is optional, and INTRO and key answers parse `name_len` 0 | `frame_codec.cpp` :718; `on_hash_bind_pubkey` (`node_hashlocate.cpp` :1275–1276); `on_mobile_key_forward` (:1464) |
| Every simulated node is named: the simulator requires the field and all 783 nodes in the 36 scenarios carry one, so the default never runs in the corpus | `lora-universal-simulator/core/topology/JsonConfig.cpp` :269; scenario scan (disposable script, not retained) |
| `whoami` prints `effective_name`, the default when unset; the companion `ready` passes the stored name and omits it when empty; peer rows omit `name` when none is cached | `firmware_commands.cpp` :1399; `fw_main.cpp` :541–543; `console_json.cpp` :590, :835 |
| The TEAM row gives the name `%-6.6s` and has no other identity column; UI-16 bans a clipped `0x` form and shows members by the six-digit fingerprint (S-13) | `kTeamLabelCols` (`firmware_ui_team.h` :92); `ui_fmt_member_fingerprint` (`firmware_ui_invite.h` :379); UI-16 spec, candidate row (F-15) |
| The other name setters refuse an over-cap name (`too_long`) where `cfg set name` shortens it silently (B448) | `node.cpp` :2177–2188; `firmware_config.cpp` :277 |

Consequence: an unnamed peer's label is the 26-byte default, which clamps to `MeshRoute node` (14) or `MeshRo` (6)
and always takes B241's over-length path; a peer with no cached name shows the clipped `0x12ab` on a TEAM row (B441
addendum). The owner ruled D10 on 2026-09-23: unnamed devices advertise no name and are shown by their ID.

### 3.6 Setup and provisioning (agent-surveyed, spot-checked)

- PROVISION is a SETTINGS menu row; its double first requires an open config service, then refuses on a
  conflicting or unsaved settings draft (`RELOAD OR DISCARD` / `SAVE OR DISCARD`) — `activate` CfgRow::provision
  arm (model :4142–4147, checked). The config service opens only on arrival at SETTINGS (`sync_settings` :4022,
  checked).
- Children: CREATE TEAM, JOIN NETWORK, JOIN TEAM (opens NEARBY directly), INVITE MEMBER (in a team), SAVED KEYS,
  BACK (`provision_rows` :784). Confirmations open on BACK; results are acknowledged by either press.
- From boot the NEARBY list is 11 presses and CREATE TEAM's confirmation 11 (12 to create) — agent count from
  source, not a trace.
- After a nearby join the only team-DAD-pending signal is `my_team_id == 0`; there is no team join session.
- Create/join are durable before success (`ProvisioningService::apply_team`, one save then live apply).
- The owner-ruled lexeme S-30 `KEYRING FULL` is never rendered: a keyring-full create shows `CREATE REFUSED` with
  the token `keyring_full` (`prov_result_head` :1205–1210, `a.reason` :1288; `kKeyringFullText` referenced only by
  tests/tools — checked).
- The six-hex nearby fingerprint is `team_id & 0xFFFFFF` (`ui_fmt_team_fingerprint`), i.e. the last six digits
  of the eight-digit id Home shows.
- (added 2026-09-23, D11) The settings service, once opened, is never closed: leaving SETTINGS closes the editor
  and provisioning but keeps the draft (`settings_follow_screen` :3960, `provision_reset_on_leave`). INVITE MEMBER
  is offered to any member (`prov_invite` = team build and `team_id != 0`, `firmware_ui.cpp` :724) and changes no
  settings: its device operations are one triggered team beacon, a key-cache read, a `reqpubkey` line and the
  sealed key grant (`DeviceInvite`, `firmware_ui.cpp` :293–328). JOIN and CREATE change `/mrcfg` (team membership
  `team_id`, the team-key binding; `src/device_nv.h` :107, :127–128).

### 3.7 Send path and byte admission

| Stage | Fact | Anchor |
| --- | --- | --- |
| UI request | `SendReq{kind, peer_id, slot, generation}` (8 B); kinds `emergency`, `dm`, `channel_canned`; no owned text | `SendKind`/`SendReq` (model :1657/:1672) |
| Pending | One normal request slot and one emergency slot; the normal request is taken only when the normal tracker is idle, emergency first | `queue` (:3628), `take_send_request` (:3159); `mr_ui_tick` (`firmware_ui.cpp` :2486–2491) |
| Gate | Stale generation, disabled slot or kind mismatch → `PRESET CHANGED`, zero submission; emergency exempt | `send_gate_of` (`firmware_ui_send.h` :493) |
| Compose | DM `send <id> "<text>" -t -a [-l]`; channel `send_channel <ch> "<text>" -t -e [-l]`; emergency adds `-l` only with loc on **and** a fix; truncation is a refusal | `ui_compose_send_line` (:534) |
| Line buffer | `kSendLineCap = 96`, a stack local in `ui_perform_send` | `kSendLineCap` (:472), `ui_perform_send` (:573) |
| Executor | Validates the line against `local_command_max_bytes` (1023), parses, runs `on_command` synchronously with the body borrowing the line | `exec_command` (`firmware_commands.cpp` :1902); `lib/console/console_line.h` |
| Parser | Quoted body ends at the next `"`; no escaping; a body over 241 bytes is **clamped to 241**, then every current consumer refuses above its smaller cap | `parse_send_tail` (`console_parse.cpp` :96, clamp :110) |
| DM admission | body ≤ `dm_max_body_bytes` = 241 − 9 = **232** (shape-blind) | `on_command` send arm (`node.cpp` :1690); `protocol_constants.h` :1088 |
| Sealed DM | `data_inner_cap(CRYPTED, 0)` = 255 − 8 − 8 = 239; sealed inner 4 (dst hash) + 1 + 4 + body + 16 ≤ 239 ⇒ **214**, **208** with `-l`; loud `too_large` | `enqueue_data` seal cap (`node_mac.cpp` :376–378); `data_inner_cap` (`frame_codec.h` :743) |
| INTRO | Attached only on by-hash and `send_layer` paths; dropped (message still sent) when it does not fit — never constrains the UI's by-id DM | `intro_attach_prefix` (`node_hashlocate.cpp` :751) |
| Channel | plain ≤ 200; sealed: inner overhead + body ≤ 174 ⇒ text ≤ **173**, **163** with `-l` (flags 1 + source hash 4 + location 6) | `on_command` send_channel arm (`node.cpp` :1736, :1921); `protocol_constants.h` :418, :512 |
| Team-post guard (added 2026-09-23) | `-t` is admitted on `is_mobile && team_id != 0` alone, while the team-DM guard also requires `team_local_id() != 0`; before team-DAD, `do_send_channel` keeps `origin = _node_id`, i.e. a registered member's static ID (B444) | `node.cpp` :1778/:1782 vs :1726; `node_channel.cpp` :621/:623 |
| Location request (added 2026-09-23, D6) | `-l` is strict. A DM refuses `unsealable` when it will not be sealed and `no_location` when the position is (0,0); a sealed team post refuses `unsealable`, `no_identity`, `no_location`, and the whole send when location makes it too long. The position is never silently dropped. It is `_cfg.lat_e7/lon_e7`, configured over USB (no GNSS code exists). A received located DM or team post records the sender's position for the Team screen's distance and bearing | `node_mac.cpp` :300–310; `node.cpp` :1843–1927; `peer_loc_set` callers `node_channel.cpp` :373, `node_mac_rx.cpp` :2923 |
| Phrase location intent | The slot's `loc` flag alone adds `-l` (preset ruling R-2); a located team or DM phrase without a fix is refused, never stripped; only the emergency sends without `-l` when there is no fix | `ui_compose_send_line` (`firmware_ui_send.h` :515–556) |
| Refusal wording | A synchronous refusal maps `err_unsupported` to `REFUSED` plus the code (no fix, no key and unsealable are indistinguishable there); `NO FIX` appears only through the asynchronous `send_failed` path, i.e. for a DM | `refuse_reason_of` (`firmware_ui_send.h` :449), `refuse_text` (`firmware_ui.cpp` :494), `note_failure` (`firmware_ui_model.h` :3794) |

### 3.8 Presets

| Fact | Anchor |
| --- | --- |
| `/mrui` 'MRU1' v1: 17 fixed slots `{enabled, loc, len, text[18]}` = 21 B; blob 12 + 357 + 3 = 372 B; per-ABI asserted | `UiPresetSlot`/`UiPresetBlob` (`device_nv.h` :340/:357, asserts :383–389) |
| `kUiPresetTextMax = 17` (OQ-A: one 19-column row minus two markers) | `device_nv.h` :356 |
| Version is **equality**; the source states *"there is no migration arm for a phrase catalog and there must not be one"* | `ui_preset_blob_state` (:939) |
| Grammar: 1..17 bytes, 0x20–0x7E, no `"` or `\`, not all spaces | `validate_preset_text` (`firmware_ui_presets.h` :329) |
| Defaults: emergency `I'm in danger` (loc on); dm1 `Are you OK?`, dm2 `I'm OK`; channel1 `Got your message`, channel2 `All good`; others disabled | `kPresetDefaults` (:376) |
| `PresetCatalog` holds three blobs (live + two transaction scratch); boot load writes nothing | `PresetCatalog` (:501), `begin` |
| The snapshot projection copies each visible row's full text: `ComposeSlot` 20 B, `ComposeList` 161 B, two lists on `UiSnapshot` | `ComposeSlot`/`compose_project` (model :1469/:1495) |
| The emergency overlay never draws the phrase; the alarm composes `send_channel … -t -e [-l]`, so its text is bound by the sealed-channel cap (163 with a fix) | `draw_emergency` (`firmware_ui.cpp` :2301); `ui_compose_send_line` |
| (added 2026-09-24, review DR-2/DR-3) `ui_presets_end.capacity` is the slot count `kUiPresets` (17), not a text length; the reply buffer `kPresetLineMax` is a literal 160, so a 163-byte `ui_preset` record (243 bytes with its newline) would make `JsonBuf::finish` return 0; the companion contract publishes the 1..17-byte text rule | `write_ui_presets_end`, `kPresetLineMax`, `preset_emit_record` (`src/firmware_ui_preset_verbs.h`); `JsonBuf::finish` (`lib/console/console_json.cpp`); `ios-companion/INBOX_SYNC_CONTRACT.md` `ui preset` section |

### 3.9 Inbox, receive and wake

| Fact | Anchor |
| --- | --- |
| `InboxEntry` keeps seq, kind, origin, channel, msg_id, sender_hash, enc, type, team_id, rx time, borrowed body | `lib/core/inbox.h` :27 |
| Channel records store `sender_hash = 0`, `origin = msg_id >> 24` (team-local ID of the minter) | `Inbox::record_channel` (`inbox.cpp` :174) |
| `channel_msg_id = origin<<24 \| (sender key_hash32 & 0xFFFF)<<8 \| ctr8` — a 16-bit sender-hash fragment is retained, not a unique or authenticated identity | `Node::channel_msg_id_mint` (`node_channel.cpp` :54, layout note :485) |
| `Push` carries the Inbox per-store `seq`; nonzero seq is not proof of persistence | `lib/core/command.h` `Push::seq` |
| Receive router: DM counts + wakes; channel counts, marks dirty, wakes only when `pu.enc`; the distress reply further requires the team channel and same-team post, copying ≤ 20 bytes | `ui_route_recv_push` (`firmware_ui_send.h` :608) |
| (added 2026-09-24, review DR-1) Unread is session arrival serials: `arr_dm`/`arr_ch` count every arrival, uncapped, from 0 at boot; `read_dm`/`read_ch` move only in `FrameGate::on_page`, to the serials frozen by a complete, visible frame whose screen is the Inbox (entered or passive) with no emergency overlay, compose or detail modal. They are not Inbox record sequences, which continue from restored records | `UiInboxCounters`, `FrameGate::step`/`on_page` (`src/firmware_ui_model.h`) |
| (added 2026-09-24, review DR-5) A published Inbox row carries `rx_age_s` in whole seconds, not the receive time; `InboxRowBudget` stages four rows per kind and `publish` copies DM then channel blocks | `InboxRow`, `InboxRowBudget::add`/`publish`; `inbox_row_cb` (`src/firmware_ui.cpp`) |
| Detail opens by `(kind, seq)` through a full pull, so a record outside the four browsable rows per kind (B191) still opens; missing ⇒ `MESSAGE GONE` | `ui_open_inbox_detail` (`firmware_ui.cpp` :637); `kInboxRowsPerKind` (model :2099) |
| Detail pages: 19 × 2 = 38 bytes, pure byte slice (no word wrap), 2 s cadence, cycling; only the current page is frozen in `UiState` | `kDetailCols`…`kDetailPageMs` (model :1399–1404), `refresh_detail_page` (:5341) |
| A complete, visible frame of the Inbox screen — entered or passive, with no overlay, compose or detail modal — sets the read watermarks from the frozen arrival serials [corrected 2026-09-24, review DR-1: this row said "Inbox list frame"] | `FrameGate::on_page` (:5670) |
| Rows: four newest per kind (two rings), published DM block then team block, newest-first within each (B231); `rx_time_ms` is uptime (`ArduinoClock::now_ms`, `lib/hal/iclock.h` :44–46), so it restarts at boot while records persist; a row's age reads `--` only while current uptime is below its stamp (added 2026-09-23, B445). The restored high-water per store is public (`Inbox::dm_newest_seq`/`chan_newest_seq`, `inbox.h` :274–275), restored by `Inbox::on_init` in setup (`fw_main.cpp` :1048) before `mr_ui_init` (:1179); all Inbox writes come from loop-time receive handlers (`node_channel.cpp` :408, `node_mac_rx.cpp` :1793/:2684/:2889); `Inbox::clear` persists the high-water before wiping | `InboxRowBudget` (model :2099–2130); `fill_inbox_rows` age (`firmware_ui.cpp` :588–592) |

### 3.10 Profiles

Six envs compile the OLED UI, all ESP32-S3: `heltec_v3`, `heltec_v4`, `heltec_mobile`, `heltec_v4_mobile`,
`gateway_heltec`, `gateway_heltec_v4` (`platformio.ini`, `MR_FEAT_OLED=1` declared once in `[env:heltec_v3]`).
Real BLE exists only on nRF52 envs (`src/device_ble.h` gate :26–28); no image combines OLED and BLE.
`MR_PROFILE_MOBILE` changes only the remote-admin pair, so static and mobile Heltec images compile the same UI;
the mobile role is runtime `NodeConfig.is_mobile`. Gateways build with `MR_FEAT_TEAM 0`. One button (GPIO0),
polled once per UI tick; the loop runs flat out while the panel is lit.

## 4. Measurements (disposable, labelled)

**Host ABI `sizeof`** (x86-64, native env flags `-DMESHROUTE_NATIVE=1 -DMR_N_LAYERS=2`) beside the **board pins**
already committed in `tools/probe_board_abi.py` `PIN_TABLE` for `heltec_mobile` (read, not re-run):

| Type | Host | heltec_mobile pin |
| --- | --- | --- |
| `mrui::UiState` | 504 | 504 |
| `mrui::UiSnapshot` | 1336 | 1336 |
| `mrui::UiModel` | 928 | 912 |
| `mrui::ComposeSlot` / `ComposeList` | 20 / 161 | 20 / 161 |
| `mrui::SendReq` | 8 | 8 |
| `mrnv::UiPresetSlot` / `UiPresetBlob` | 21 / 372 | 21 / 372 |
| `mrfw::PresetCatalog` | 1144 | 1132 |

**NV record sizes** (host; each record is per-ABI asserted in `device_nv.h`): `/mrcfg` 240, `/mrid` 80,
`/mrpeers` 1160, `/mrjoin` 104, `/mrteams` 296, `/mrui` 372, `/mradmid` 40, `/mracl` 368, `/mrmkeys` 368,
`/mrtargets` 2056. A mobile (CLIENT) image holds about 4.7 KB of these in the 20 KB `nvs` partition of
`default_8MB.csv`; nothing in the tree reports NVS free entries (`nvs_get_stats` has no caller). Occupancy is an
estimate until measured.

**Latest recorded board RAM:** `heltec_mobile` 211724 B at Slice 10
([evidence](2026-09-19-radmin-slice10.md), row "heltec_mobile RAM /flash").

**Preset capacity arithmetic** (fixed-width record; `T` = text bytes per slot):

| T | slot B | `/mrui` B | catalog (3 blobs) Δ vs today | `UiSnapshot` Δ if rows still copy full text |
| --- | --- | --- | --- | --- |
| 17 (today) | 21 | 372 | 0 | 0 |
| 32 | 36 | 624 | +756 | +240 |
| 64 | 68 | 1168 | +2388 | +752 |
| 163 | 167 | 2852 | +7440 | +2336 (static, and again on the per-tick stack) |

**Editor press-count model** (disposable script, not retained; counts classified gestures, not time). Group ring =
groups + one EDIT item, entered on the first group; char ring = the group's characters + BACK, entered on its first
character; each character costs `(target group − highlighted group) mod ring` shorts + 1 double + its index shorts
+ 1 double; finishing costs the walk to EDIT + 1 double + 3 shorts + 1 double (DONE as the fourth control):

| Text (bytes) | 7×6, return to group just used | 7×6, return to first group | 6×7, return to first group |
| --- | --- | --- | --- |
| `STAN` (4) | 30 | 30 | 39 |
| `END OF SHIFT` (12) | 108 | 84 | 89 |
| `RETURN TO BASE NOW` (18) | 137 | 126 | 134 |
| `INJURED AT NORTH RIDGE, SEND HELP` (33) | 266 | 217 | 229 |
| `ON MY WAY` (9) — added 2026-09-23 | 61 | 66 | 72 |
| `WHERE ARE YOU?` (14) — added 2026-09-23 | 130 | 109 | 103 |
| language-neutral, per character (every character equally likely) — added 2026-09-23 | 7.93 | 7.50 | 7.50 |

With one forward-only button, returning to the group just used costs a near-full ring walk to reach any earlier
group; returning to the first group is cheaper on the first four samples and on language-neutral text, though not
on every phrase (`ON MY WAY`), and six groups of seven tie on language-neutral text [corrected 2026-09-23: this
paragraph said "cheaper on every sample"]. Re-derived 2026-09-23 from the model as stated above, with identical
counts for the four original samples (disposable script, not retained). `RETURN TO BASE NOW` under 7×6
return-to-first is 88 shorts and 38 doubles; deleting the character just typed costs 9 gestures. No typing rate is
claimed. The owner ruled seven groups of six with return to the first group (design D4, 2026-09-23).

## 5. Findings, dependencies and register actions

| Item | Action |
| --- | --- |
| `/mrid` field drop when `load_id` fails during `cfg set name|lat|lon` (§3.5) | Registered **B440** (source-audit finding, not reproduced by execution) |
| Peer and own names drawn without `ui_display_byte` (§3.3) | Registered **B441** |
| S-30 `KEYRING FULL` never rendered (§3.6) | Registered **B442** (adjacent UI-16 finding; not this design's work) |
| Stale UI comments listed in §6 | Registered **B443** (comment-only inventory for the slices that touch those files) |
| Team post before team-DAD originates under the static ID for a registered member (§3.7, found 2026-09-23 while specifying Home lists) | Registered **B444** (static, not reproduced; core package W1b); Home hides SEND TO TEAM while the ID is pending |
| Inbox ages fabricated for records kept across a restart once uptime passes their stamp (§3.9, found 2026-09-23 while specifying the owner's newest-first Inbox) | Registered **B445** (static, not reproduced; package W4c) |
| The V4 GPS design still locates every phrase whenever a fix is fresh (its rulings 8 and 12, §4.5, the §9.2 pins and §9.3 control 14) and never mentions the per-slot switch, while the owner's preset ruling R-2 and the code make the switch authoritative (§3.7; found 2026-09-23 while preparing D6) | Registered **B446** (documentation only; the GPS design is rewritten in place before its final review or any GPS brief) |
| A `peername` label on an unpinned peer is replaced by that peer's next advertised name — for an unnamed peer, the default (§3.5; found 2026-09-23 while preparing D10) | Registered **B447** (open: precedence for named peers needs a ruling; D10's W1c closes the unnamed half) |
| `cfg set name` shortens a name over 32 bytes silently, unlike the other name setters (§3.5, recorded 2026-09-22, registered 2026-09-23) | Registered **B448** (lands with W0) |
| The six-column TEAM row shows unnamed teammates as `MeshRo`, or `0x12ab` before a name arrives (§3.5) | Addendum to open **B441**; closed by the formatter's unnamed rule (D10) |
| Unnamed peers transmit the 26-byte default, so every unnamed peer takes B241's over-length path | Addendum to open **B241**; identity work depends on B241 |
| B418 W49/W51/W54: each reader keys on a stale anchor while the guarded property still holds — `dispatch` gained a `CommandTransport` parameter (`firmware_commands.cpp` :1548); the BLE fallback moved into `exec_console_line`; the `UI PRESETS` help literal left `firmware_commands.cpp` (help now in `firmware_help.h`). Several control anchors are stale too; W19 was retired, so the historical 57/60 is not today's denominator (agent analysis; the three anchors spot-checked) | Addendum to open **B418**; W54 guards the `/mrui` sites a catalog package edits, so repair precedes that package |
| B286's rsync exclusion is now at `tools/probe_ui_model_mutations.py` :11986, still `.git` and `.pio` only | Citation note on open **B286** |
| B335 | Stays **OPEN**; linked to design r2 |
| Parser clamp at 241 (§3.7) | Not a defect today (every consumer's cap is smaller); recorded here only |

## 6. Comment drift observed (for B443)

Agent-reported, two spot-checked (✓): `firmware_ui_chrome.h` :259 says `ui_fmt_team_fingerprint` is "CURRENTLY
UNCALLED" (it has seven production callers ✓); `firmware_ui_model.h` :55/:574 call `Provision` an eight-arm enum (it
has 21 ✓); model :974 cites the fingerprint helper at chrome :212 (now :269); chrome :21–24 says five snapshot fields
are unpublished (published at `firmware_ui.cpp` :848–862); model :1766–1769 says PROVISION offers only BACK on
device; model :2487 says only `run_create_team` writes `prov_answer`; model :4264–4265 says the grant act is not
here yet; `firmware_ui_nearby.h` :34–38 says a double on a team row does nothing; `platformio.ini` :217,
`firmware_ui.cpp` :175–176 and `firmware_ui_icons.h` :32–33 list three OLED envs (there are six);
`lib/core/node.cpp` :1680 still says `dm_max_body_bytes` is 239 (already B300).

## 7. Not verified

Hardware behaviour, button timing, panel legibility, NVS occupancy, board RAM after any proposal, whether the
physical V3/V4 has one user button, any probe or battery result, and the projected B418 denominator.
