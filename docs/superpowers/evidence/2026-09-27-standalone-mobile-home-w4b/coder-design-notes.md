# W4b coder design notes (durable; survives context compaction)

## Retained state (§2.2, owner-ruled) — nothing else
- model.h, after UiSnapshot / before UiState: `HomeItem {none,inbox,send,team,invite,my_device,menu,join,create,key_help}`,
  `HomeView {list,my_device,key_help,setup_block}`, `SetupOrigin {none,home,settings}`,
  `HomeCapture { HomeItem items[6]; uint8_t count; HomeItem selected; bool changed; }` (9/1).
- UiState: `HomeCapture home{}; HomeView home_view = HomeView::list;` after `InviteGrantResult grant{};` → 520.
- UiModel: `HomeItem _home_return; SetupOrigin _setup_origin;` after `GrantReturn _grant_return{};` → 944/936/936.
- UiSnapshot: `char own_name[32]; uint8_t own_name_len;` appended → 1368.
- UiChrome: `bool menu_cue` appended in padding → 20.
- ProvBlock gains `unavailable` (no byte growth).

## Focus model
- `list_view` = the ONE focus authority on every top-level screen: interactive = list focus, passive = menu mode.
  Default `ListView::interactive` at boot (Home, list focus). `screen_is_entered`: TEAM/INBOX/STATUS/SEND read the view;
  SETTINGS keeps reading `Settings` (invariant: on SETTINGS, list focus ⇔ settings != closed).
- Every screen change sets focus explicitly (no implicit leave-reset): retire `list_follow_screen` (M106 re-anchored onto
  the menu-mode advance line, meaning kept). Keep `list_view_reset_on_leave` (pure) — used by the MENU transition
  (`go_menu_home`): M111 keeps its meaning (an entered list must not outlive the MENU exit).
- Menu mode: short → `next_screen` (unchanged skipping), focus stays passive; double → open the previewed screen in list
  focus, arrow on its first row (TEAM/INBOX through `list_activate`'s `enter` arm = today's passive double; SEND →
  `open_send_list`; SETTINGS → `open_settings_menu` over an open service only, else stays in menu mode; STATUS → Home
  list, `selected = none` so `sync_home` resolves item 1).
- List focus: short wraps (Settings walk-off line removed: M101 revised); double chooses; the exit row says MENU and
  goes to `go_menu_home` (TEAM/INBOX leave arm: M109 revised; Settings `CfgRow::back`: `on_back()` then
  `close_settings_menu()` then `go_menu_home`: M102 revised; Send back row: `close_compose()` then `go_menu_home`).
- Home list focus owns the press (`home_gesture`, before `advance_or_next`); sub-views: My device (double → return,
  short stays), key help (either press → return), setup block (either press → clear prov_block, return).
- `home_return()` (no snapshot needed): screen status, list focus, view list, `selected = none`, `changed = false`;
  `sync_home` resolves `selected` against the FRESH list: `_home_return` if present, else item 1 (no note), then clears
  `_home_return`. Boot and menu-mode double are the same path with `_home_return == none`.
- `sync_home(s)` runs in `on_gesture` (with the other syncs) and in `on_tick` (lit or dark). The pure
  `home_capture_refresh(c, s, list_focus_home, preferred)`: recompute items; if `selected == none` → preferred-or-first,
  no note; in Home list focus: latch set → keep item 1 of the newest list; selected vanished → item 1 + latch; outside
  Home list focus nothing but the items move. Returns whether anything changed (dirty).
- OPTIONS CHANGED truth table (design §6.4): note up + short/double → clear, run nothing; wake press → blanked arm (only
  wakes); list changes again → item 1, note stays; emergency → untouched (handled above). A press on the SAME tick the
  item vanished (note raised by that press's own sync) → consumed, note stays (the operator has not seen it yet) —
  ruling.
- `HomeCapture::changed` is the ONE list-changed note of the FOCUSED top-level list: Home's OPTIONS CHANGED or the Send
  list's PRESET CHANGED (only one top-level list is focused; every press that leaves it clears the note first; a
  committed alarm closing compose leaves it, so "afterwards the note is still there" holds for Send too) — ruling.

## Send list = the channel compose selection phase (no second send path)
- `open_send_list(s)`: screen send, list focus, `Compose::channel`, cursor 0, `compose_gen = s.preset_generation`.
- Result ack (either press) → `close_compose()` then reopen the Send list at item 1 (channel only; DM unchanged).
- Catalog change (`preset_generation_moved`): DM → close as today; channel → re-read (`compose_gen` = live, cursor 0),
  raise the note; the next non-wake press clears it and sends nothing.
- `send_list_follow(s)` in `on_tick`: SEND + list focus + compose none + emergency idle → reopen (after a committed alarm).
- Row labels: pure `send_list_row_line` — `PRESET CHANGED` on item 1 while noted, `MENU` for the back row, else
  `compose_row_line` verbatim.

## Setup origin
- Set: Home JOIN/CREATE/INVITE activation → home (and `_home_return` = the item); SETTINGS PROVISION row → settings
  (`_home_return` = none). Survives `enter_provision` (not touched there). Retired in `settings_follow_screen` (leaving
  SETTINGS), `close_provisioning` (PROVISION BACK / pre-emption / closed arm).
- `provision_menu_exit()`: origin home → `home_return()`, else `enter_provision(menu)`; used at the pre-check §6 sites
  that return to the PROVISION menu today: join_waiting, join_result (after the two special landings), create_confirm
  BACK, join_select BACK, nearby BACK, create_result terminal, saved_key decline, saved_keys BACK, invite list BACK,
  `leave_grant_chain` terminal arms (none/invite_window). Resume arms never re-home (they share the verbatim OQ-3 call).
- PROVISION BACK: origin home → `home_return()`; else `close_provisioning()` (Settings browsing).
- Kept verbatim: OQ-3 blank cancellations; `on_invite_push`'s none → menu (a push never navigates Home);
  SETTINGS-entry `enter_provision(menu)`; `team_key_note_ack_landed` lands on Home via `home_return()` (list focus).
- Home JOIN/CREATE: `provision_admit()` at activation; refusal → `prov_block` (unavailable when no block was set) +
  setup-block note (screen stays STATUS, nav slot SETTINGS). Admission → screen SETTINGS, list focus, CREATE →
  `enter_provision(create_confirm)`; JOIN → `load_nearby(s)` + `enter_provision(nearby)`.
- Home INVITE: no admission; screen SETTINGS, list focus; `load_invite(s); enter_provision(invite);
  request_team_announcement()`.

## Renderer
- Home body at x12 via `body_text` (no mark, no x40): row 0 `ME ` + identity(16) (`ME` alone on none); row 1 team
  line; row 2 `RESTART NEEDED` when `c.reboot`; list window rows 2–4 (3–4 under restart) through `list_first`;
  marker only in Home list focus; `OPTIONS CHANGED` on item 1 while noted.
- My device: name rows 0–1 split raw 0–18 / 19–31 sanitized, or `NO NAME SET`; `ID 0x<HASH8>`; `ui_status_location(false)`;
  `>BACK`. Key help: 5 fixed rows. Setup block: reason row 1, `IN SETTINGS` row 2 (blank for unavailable).
- Settings preview drops the `>`; Send preview unchanged; TEAM/INBOX exit row `MENU`.
- Rail: W41's box statement verbatim + a separate cue statement (x10, w2, one slot high) when `c.menu_cue`.
- `build_snapshot`: `own_name_len = g_node.effective_name(own_name, 32)`.
- Tick: `ui_home_invalidate(s_model, s, s_frame_snap)` (status.h) beside the team invalidation.
