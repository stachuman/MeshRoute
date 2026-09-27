<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com>; r1 draft: OpenAI Codex (2026-09-06/07); r2/r2.1: Claude, specification author (2026-09-22/23) -->
# Standalone mobile — identity, dynamic Home and team messaging

**Revision 2.21 · 2026-09-27 · REVIEWED — independent review PASS with fold-ins (2026-09-24); packages W1, W3, W4a and W4b scoped by their QA pre-checks; the W4b allocation owner-ruled (§11.1, §16).** Every decision in
§12 is ruled by the owner (2026-09-23/24): navigation (D1), Home rows and lists (D2, D2a–D2c), the boot splash (D3),
the editor alphabet (D4), review before every send or save (D5), written-message size and location (D6), phrase
size (D7), the phrase-record reset (D8), default phrases (D9), names (D10), setup from Home (D11), the Home card of
a received team post (D12) and Inbox order with its restart rule (D13, D13b). The owner also confirmed the author's
choices disclosed during review (§7.4.1). The review took three rounds — [review](../evidence/2026-09-24-standalone-mobile-home-design-review.md)
(HOLD, DR-1–DR-8), [re-review](../evidence/2026-09-24-standalone-mobile-home-design-rereview.md) (HOLD on DR-4, minor
DR-9–DR-11), [second re-review](../evidence/2026-09-24-standalone-mobile-home-design-rereview-2.md) (PASS with the
fold-in DR-12) — and every finding is closed or folded in (§16). Sections marked [PROPOSED] (§6.4 details, §6.5,
§7.2, §7.4, §7.5) are the author's proposals, reviewed with the design. This verdict authorizes no implementation:
each §13 package still needs its Quality-Agent pre-check, measured allocation and brief. Source-validated at `2035757` plus the owner's uncommitted B439 register/tracker refresh; the
evidence is the [source audit](../evidence/2026-09-22-standalone-mobile-home-source-audit.md). This is a product
design, not an implementation brief, dispatch authorization, QA verdict, resource allocation or measured board
result. The owner rules and commits; a separate context reviews this document; QA pre-checks and gates each later
brief; a coder source-validates and implements. Wireframes are proposals whose column counts are computed, not
current output.

**Legend.** **[AGREED]** owner agreement, quoted · **[FACT]** verified in source at `2035757` · **[PROPOSED]** author
recommendation awaiting the numbered decision · **[HISTORY]** superseded text kept for provenance.

## 1. Purpose and agreement boundary

A Heltec mobile is a handheld group communicator usable without the iOS companion. Primary scenarios: a group
hiking together, and an event/festival team coordinating work. "Home" means the **OLED landing screen** (today's
STATUS screen), not the mobile's routing home node. This design covers identity, setup discoverability, everyday
team/person messaging and one shared text editor. It is not a mobile-home attachment or routing redesign.

**[AGREED] owner direction, 2026-09-06:** Home is dynamic — without a team it offers setup; in a team it emphasises
everyday communication. The wearer can see who this device is (its name, not only a team-local number). Joining,
creating and naming are discoverable without provisioning knowledge. Ordinary whole-team messages are in scope.

**[AGREED] owner correction, 2026-09-06:** *"This limit of 17 chars is wrong - messages should use multilines."*
The 17-byte preset cap is a current implementation fact, not the target. `Return to base now` (18 ASCII bytes)
must survive intact; shortening it to `Return to base` is not the remedy (the r1 Author recommendation to keep
17 bytes is withdrawn).

**[AGREED] owner agreement, 2026-09-07:** *"Proposal makes sense, grouping should be equal (we can't consider
english specific statistic of letter use) and limited to bare minimum (letter, numbers, limited punctaction)."*
One shared editor serves naming and one-off ordinary team/person messages: fixed, equally sized character groups;
a minimal repertoire; short advances, double chooses; long holds stay emergency gestures; no frequency layout,
prediction or learned ordering.

**[AGREED] requirements carried by the handoff (2026-09-22):** explicit review before Save/Send; bounded RAM
drafts, never an NV write per character; cancellation cannot save or send; one-off composition never rewrites a
preset nor uses the emergency repeat path; full message bytes survive review and submission; changes to names,
membership or the roster cannot change the action or destination under the wearer's finger.

**[AGREED] owner navigation direction, 2026-09-23:** *"Maybe right choice will be - by default we are in home
screen - moving between items there. One of item will be 'menu' - existing on each screen - that will move us to
left side menu"*; after the evaluation, *"Assuming refinements you propose (including 6)"* and *"yes, fold it into
the design"*. The resulting model is §6.1; the Home lists (§6.3) were folded in at the same request, and their
three sub-choices (D2a–D2c) were ruled later with D2. The owner then corrected two points: *"When panel goes dark - we should stay at the
current screen, no re-homing, inbox - we should sort with newest at top - so sorting to get newest messages from
top"* — refinement 6 (return to Home after a blank in menu mode) is withdrawn (§6.1 rule 7), and the Inbox becomes
one newest-first list (§6.8). Asked whether to adopt the lighter restart rule for that list, the owner replied
*"yes, mark D13b decided"*.

**[AGREED] owner ruling on the review, 2026-09-23:** offered three options for saved phrases — send on the double
press, review opening on SEND, or review opening on the safe action — the owner chose the last for everything the
device sends or saves: *"C everywhere - it makes it consistent."* (§7.3, D5).

**[AGREED] owner ruling on phrase size, 2026-09-23:** offered 163 bytes (the largest size every slot can always
send, about +7.4 KB RAM) or 64 bytes (about +2.4 KB), the owner chose *"163, mark D7 decided"* (§7.7, D7).

**[AGREED] owner ruling on the phrase record, 2026-09-23:** *"B, mark D8 decided. For your record - MeshRoute is
NOT yet deployed - we do NOT NEED back-compatibility."* An old record is not converted: the device runs on the defaults and says
so at every boot until the first successful change replaces it; no upgrade reader is written (§7.8, D8). The same principle applies throughout this design: no
backward-compatibility machinery for earlier firmware or stored formats.

**[AGREED] owner ruling on default phrases, 2026-09-23:** offered the recommended set (keep the five existing
defaults; add team `Return to base now` and `On my way`, personal `Where are you?`), the earlier set, no change or
own wording, the owner chose *"this set, mark D9 decided"* (§7.7, D9).

**[AGREED] owner ruling on written messages, 2026-09-23:** offered the largest limits without location (team 173,
DM 214), one 163-byte limit without location, or one 163-byte limit with a per-message location switch, the owner
chose *"B, mark D6 decided"*: every message the panel sends — saved phrase or written, to the team or to a
person — holds up to 163 bytes, and written messages carry no location (§7.1, §7.4, D6).

**[AGREED] owner ruling on names, 2026-09-23:** offered five recommendations — an unnamed device shown by its ID
(never a clipped hash), unnamed devices advertising no name, a 32-byte name, rename starting from the current name
when the editor can type it, and a name prompt before JOIN/CREATE only when unnamed with SKIP preselected — the owner
chose *"as recommended, mark D10 decided"* (§4, D10).

**[AGREED] owner ruling on Home, 2026-09-23:** offered the Home rows and per-state lists of §6.2–§6.3 (the mark
leaves Home, counts ride in item labels, the position moves to My device) with `INBOX` first (D2a), `INVITE MEMBER`
on Home for key holders (D2b) and Settings through `MENU` (D2c), the owner chose *"as recommended, mark D2
decided"* (§6.2–§6.3, D2).

**[AGREED] owner ruling on setup from Home, 2026-09-23:** offered the §6.6 proposal with one refinement from the
source check — `JOIN TEAM` and `CREATE TEAM` pass the settings gate, `INVITE MEMBER` opens without it because it
changes no settings — the owner chose *"as proposed, mark D11 decided"* (§6.6, D11).

**[AGREED] owner ruling on the editor alphabet, 2026-09-23:** offered uppercase letters, digits, space and
`. , ? ! -` in seven groups of six, controls under `EDIT` and a return to group 1 after each character — against
returning to the group just used, six groups of seven, lowercase, or a different five marks — the owner chose *"as
proposed, mark D4 decided"* (§5.2, D4).

**[AGREED] owner ruling on the boot splash, 2026-09-24:** offered a splash after start-up (about 1 s, dismissible,
never on wake) with or without the build's Git ID under the mark, a splash drawn during start-up, or none, the owner
chose *"A with the Git ID line, mark D3 decided"* (§9, D3).

**[AGREED] owner ruling on the Home preview, 2026-09-24:** offered deferring the r2 card, building it now, dropping
it, or a display-only card — the newest unread sealed team post shown in Home's rows 1–2, no rows added, cleared by
the existing unread rule — the owner chose *"D, mark D12 decided"* (§8, D12).

### 1.1 Correction: ordinary channel sending already exists [HISTORY, FACT]

The first discussion assumed only emergency channel messages could be sent. The owner clarified there was no
device report behind that; no hardware failure was reported. The source has an ordinary path: SEND → double →
channel compose → a preset queues `channel_canned` → `send_channel <ch> "<text>" -t -e` through the shared executor
(`UiModel::compose_gesture`, `ui_compose_send_line`). B334/HOME-A1 is closed as a premise correction. This design
improves discovery and composition and reuses that path.

### 1.2 Scope

**In:** own-name display and rename; the navigation model; Home states and setup entry; the shared editor; manual
DM and team messages; multiline presets with review; the `/mrui` version-2 record; the Home card of a received team
post (D12); the boot splash (D3).
**Out:** mobile-home attachment or routing; an application acknowledgement/provenance protocol (B118);
direct-message preview (separate decision); on-device preset editing; typed team-ID entry; draft persistence
across reboot; companion/BLE editing surfaces; any wire change; any `lib/core` change inside a feature package
(§11.3 — B444 and D10's no-default-name change are separate core packages; B241 is fixed in the UI's label adapter, W1).

## 2. Current source baseline [FACT]

Full anchors and method: [audit §3](../evidence/2026-09-22-standalone-mobile-home-source-audit.md#3-source-facts-by-area).

| Area | Today | Anchor |
| --- | --- | --- |
| Input | debounce 25 / double gap 350 / arm 800 / fire 3500 ms; short is emitted 350 ms after release; a press held 800 ms arms emergency | `firmware_ui_input.h`: `InputCfg`, `InputFsm::update` |
| Dispatch | emergency, then blank-wake consume, overlay absorb, detail, compose, provisioning, settings, cycle | `UiModel::on_gesture` |
| Navigation | screens land passive; short passes a screen; double enters; Team/Inbox lists end with BACK to the passive screen; the Settings menu is left by BACK or by walking off its end | UI-17 §1; `advance_or_next`, `next_screen` |
| Home body | `TEAM %08lX`, `ME T<id>`, `<n> KNOWN`/`NO TEAM KEY`, `<n> NEW / HOME <age>`, `RESTART NEEDED`/position/`NO LOCATION`; 24×24 mark; rows 0–2 at 14 cols | `firmware_ui_status.h`; `draw_status_screen` |
| Own name | not in the snapshot; unnamed nodes transmit `MeshRoute node: 0x<HASH8>` (26 B) as their name | `UiSnapshot`; `Node::effective_name`; `node_hashlocate.cpp` INTRO/answer/WANT_PUBKEY |
| Peer labels | raw cached bytes, 15-byte buffers without termination (B241); TEAM rows give the name 6 columns and no other identity, so an unnamed teammate reads `MeshRo`, or `0x12ab` before its name arrives (B441) | `label_from_hash`, `peer_name_find`, S-11 |
| Setup | PROVISION is a SETTINGS row, gated by an open, unconflicted, saved config draft; NEARBY is 11 presses from boot | `activate` CfgRow::provision; `provision_rows` |
| Compose | a double on a preset queues it immediately; no review; result phase acknowledged by either press | `compose_gesture` |
| Request | `SendReq{kind, peer_id, slot, generation}`; kinds `emergency`, `dm`, `channel_canned`; no owned text | `SendReq` |
| Line | DM `send <id> "<t>" -t -a [-l]`, channel `send_channel <ch> "<t>" -t -e [-l]`; `kSendLineCap` 96 on the stack | `ui_compose_send_line`, `ui_perform_send` |
| Team post guard | `-t` admitted on `is_mobile && team_id != 0` without a team-local ID (DMs require one) | `Node::on_command` send_channel arm (B444) |
| Presets | `/mrui` v1: 17 slots × 17 bytes, blob 372 B, exact-version load, "no migration arm … must not be one"; three blobs resident | `UiPresetBlob`, `ui_preset_blob_state`, `PresetCatalog` |
| Inbox list | four newest rows per kind; DM block, then team block, newest-first within each (B231); never interleaved because receive time is uptime, restarting at boot | `InboxRowBudget`; `fill_inbox_rows`; `ArduinoClock::now_ms` |
| Inbox detail | 19×2 byte slices, 2 s cycle, opens by `(kind, seq)`; a complete, visible frame of the Inbox screen, entered or passive, advances the session-unread watermarks | `kDetailCols`…, `ui_open_inbox_detail`, `FrameGate::on_page` |
| Channel records | `sender_hash = 0`; `origin` = team-local ID; msg-id keeps 16 bits of the sender hash | `Inbox::record_channel`, `channel_msg_id_mint` |
| Wake | DM wakes; team post wakes only when opened sealed; distress reply further scoped | `ui_route_recv_push` |
| Profiles | six ESP32-S3 OLED envs, no OLED+BLE image; gateways have no team plane | `platformio.ini`, `device_ble.h` |

**Dependencies found by the audit:** open **B241** (label termination — every unnamed peer takes its over-length
path), **B418** (board-UI readers W49/W51/W54 anchored to moved code; W54 guards the `/mrui` sites), **B191**
(four browsable rows per kind), **B236** (normal send slot held until a result is acknowledged). New:
**B440** (`/mrid` field drop when `cfg set name|lat|lon` cannot load the record), **B441** (names drawn without
the display sanitizer), **B442** (owner-ruled `KEYRING FULL` never rendered — adjacent, not this design's work),
**B443** (comment drift in files this work touches), **B444** (a team post before team-DAD originates under the
static ID for a registered member — core, not Home work; Home hides SEND TO TEAM meanwhile), **B445** (Inbox ages
fabricated for messages kept across a restart once uptime passes their stamp).

## 3. Product model

Home answers, in order: **which device is this; which group is it with; what needs attention; what can I do.**
Identity stays visible in every Home state. Membership, possession of the team content key and a team-local ID
are separate facts. A known route is not a person proven online. The UI distinguishes setup readiness, radio
transmission, delivery evidence and human acknowledgement. Dynamic content never changes the action under the
button: Home's arrow follows its item (§6.4), and every other action list is captured when it is entered.

## 4. Identity [AGREED owner 2026-09-23, D10]

### 4.1 One identity-label formatter

A pure formatter `ui_fmt_identity(bytes, len, key_hash32, cols)` renders every device on the panel — the own
name on Home and in My device, TEAM rows, compose, review and result lines, invite rows:

- **Bytes are cells.** Each byte occupies one column through `ui_display_byte` (0x20–0x7E as-is, anything else
  `.`), fixing B441. Byte length, not character count, is the width; a UTF-8 `ł` shows as `..`.
- **No name means the ID.** Unnamed devices advertise no name (§4.6), so "unnamed" simply means no stored name
  for the own device and no cached name for a peer. An unnamed device is shown by its ID: `0x<HASH8>` (uppercase,
  `ui_fmt_member_hash_full`) where the cell has at least 10 columns — `ME 0x12AB34CD` on Home — otherwise the
  six-digit member fingerprint (`ui_fmt_member_fingerprint`, UI-16 S-13, the spelling the invite list already
  shows): `AB34CD` on a TEAM row. A hash is never clipped — UI-16 (candidate row, F-15)
  bans a clipped `0x` form as a third spelling of the hash. Where the row already carries the hash (the invite
  row's fingerprint column, the DM review header `TO 12AB34CD`), the name part is left out. No hash known ⇒ the
  caller's existing `id <n>`/`T<n>` fallback.
- **Abbreviation is visible:** a name wider than its budget shows `cols − 1` bytes plus `»` (Latin-1 0xBB, a
  glyph the 6×10 font has; it can occur in neither the editor alphabet, the preset grammar nor sanitised
  names, so it is unambiguous). No silent clipping. It applies to the invite row as well as the TEAM row, so one
  name is shortened the same way on both screens (`Wolfg»`; UI-16's `%-6.6s` clip is revised, §10).
- **Budgets at today's sites** (W4a; set from the [W4a pre-check](../evidence/2026-09-26-standalone-mobile-home-w4a-precheck.md), zero resident growth):
  TEAM row name 6; compose header `to: ` + 15; the DELIVERED result's own peer row 19 (so an unnamed peer shows
  `0x12AB34CD` there); the emergency REPLY sender 14; invite candidate name 6 beside its separate fingerprint (an
  unnamed member's name field stays blank); NEW MEMBER confirmation name 14, always beside the full `0x<HASH8>`.
  The formatter reads the full counted name (up to 32 bytes) before abbreviating, never a pre-clipped copy.
- It depends on B241's fix (termination in the UI's label adapter `label_from_hash`, W1; the raw `peer_name_find`
  keeps its full 32-byte contract) and on the no-default-name package (§4.6); without
  the latter an unnamed peer's advertised default would render as a name.

### 4.2 Where the own name appears

- **Home row 0:** `ME ` + label in 16 columns: `ME STAN`, `ME 0x12AB34CD`, `ME ABCDEFGHIJKLMNO»` (19).
- **My device:** the full name over two rows (32 bytes ≤ 38 cells), then `ID 0x<HASH8>` (the stable device
  identity), then position (§6.7). An unnamed device shows `NO NAME SET` and its `ID` row.
- The snapshot gains the own name (32 bytes + length) published once per tick, so a console rename between OLED
  pages cannot tear a frame.

### 4.3 Rename

**Flow:** My device → `CHANGE NAME` → editor (caller *name*, cap 32) → review `SAVE NAME?` (new name in full, `WAS`
+ old label) → `SAVE` → result `NAME SAVED` or `NAME NOT SAVED` + reason → either press returns to My device
(saved) or to the editor with the draft intact (not saved). `EDIT` returns to the editor; `DISCARD` (confirmed)
leaves the old name untouched. That is the My device origin; a name started at the setup prompt returns into setup
instead (§4.4). The name editor records its origin — `my_device`, `setup_join` or `setup_create` — as an explicit
typed value, as the provisioning flow does (§6.6); every return in §7.4.1 follows it.

**Capacity [AGREED D10]:** 32 bytes — the `/mrid` field and the wire name field (`peer_name_max`); no new bound.
Narrow surfaces abbreviate with `»`; the review shows the full name.

**Preload [AGREED D10]:** the editor opens with the current name only if every byte is in the editor
repertoire; otherwise it opens empty and the review's `WAS` line shows the old label. Nothing is uppercased,
transliterated or dropped. The old name stays until an explicit `SAVE`.

**Identity-record contract** (a firmware service, not a copy of the console arm — B440):
1. Build the candidate `/mrid` record through **one** live→record conversion carrying the running seed, the new
   name and the running position (U2). A record unreadable at save time does not reset position or seed.
2. Exactly one `save_id`; the live name changes (`Node::set_name`) only after it succeeds (the console's order).
3. Failure: live identity unchanged, result `NAME NOT SAVED` + `NV WRITE FAILED`, draft retained. Identical bytes:
   `NAME SAVED` with zero writes (coalescing, measured by a counter).
4. Team membership, keys and routing are untouched. A peer whose cache row is not pinned learns the new name at its
   next key exchange with this device; a peer that pinned it (QR) keeps the old name until it re-pins it or
   relabels it with `peername` (`peer_key_set` returns early for a pinned row). The UI never claims a teammate
   already sees it.
5. Emergency: the save is a short bounded call; an alarm fired meanwhile pre-empts the panel afterwards.

### 4.4 Name prompt before setup [AGREED D10]

When the device is unnamed, JOIN TEAM and CREATE TEAM from Home show `NO NAME SET` / ` SET NAME` / `>SKIP` once
the settings gate has passed (§6.6).
SKIP is preselected, so a double continues setup exactly as before and naming costs one short more. `SET NAME`
opens the rename flow (§4.3); acknowledging `NAME SAVED` continues into the chosen setup step after asking the
settings gate again (§6.6 — a gate that now refuses shows its note, and acknowledging that note returns to Home),
and leaving the editor without saving returns to this prompt. A named device goes straight on. Setting a name never blocks setup.
This revises UI-16 R-1 (JOIN TEAM opens NEARBY directly) for unnamed devices only (§10).

### 4.5 Identity at the moment of sending

A DM review names the recipient by label **and** full hash (`TO STAN 3F2A91BC`; the label takes at most 7 columns,
`»` included, so the row fits 19), UI-16 R-13's rule that a name
may describe but never alone identify at an irreversible act. The destination is bound as `(team-local ID,
key_hash32)` when entered and re-checked at review and at submission (§7.4).

### 4.6 Unnamed devices advertise no name [AGREED D10; core package W1c]

- **Before W1c:** `Node::effective_name` synthesized `MeshRoute node: 0x<HASH8>` (26 bytes) for an empty name in
  INTRO, the key answer and the key request; peers cached it as the name.
- **Implemented; independent software QA PASS 2026-09-25:** those frames carry the stored name as it is, which may be empty — INTRO and the key answer with
  `name_len` 0, the key request without its optional name trailer (the codec's existing nameless form,
  `frame_codec.cpp` :718). No wire change and no `wire_version` bump.
- **Receivers are unchanged:** an empty advertised name is already ignored (`peer_key_set` :346), so it never
  erases a cached name. No path clears a name while keeping the key — the console refuses an empty name, the
  editor refuses an empty `DONE`, and a factory reset or `regen` makes a new identity — so, on a cache filled after
  this change, an ignored empty name cannot leave a stale one behind. A cache filled before it keeps its old default
  names (the precondition below). If clearing is ever added, receivers must treat an empty name as a clear.
- **Visible effects:** `whoami` prints `name=""` for an unnamed device (the lab parsers accept an empty name).
  The companion's `ready` already omits the own name when unset, and peer rows already omit `name` for a peer
  that sent none (`console_json.cpp` :835), so the app shows its own label for unnamed peers.
- **Gains:** INTRO and the key answer lose 26 bytes (their `name_len` byte stays, as 0 — an INTRO drops from 59 to
  33 bytes, so it fits beside a longer DM) and the key request 27 (the whole optional trailer, length byte
  included, is omitted — `pack_h`); the formatter needs no special string; a `peername` label on an unnamed peer
  is no longer overwritten by the default — the unnamed half of B447 (precedence for named peers stays open there).
- **Precondition for proving it:** a peer cache that already holds an old default name keeps it, because an empty
  advertised name is ignored and nothing recognises the default string (D10; no compatibility machinery, M3). The
  unnamed-display proof therefore starts from a fresh peer cache; a bench device that cached default names before
  W1c shows them until a factory reset clears `/mrpeers` with the rest of the `mr` namespace (`factory_erase`).
- **Corpus:** every simulated node has a name — the simulator requires the field, and all 783 nodes across the 36
  scenarios carry one. The full independent `lib/core` gate reproduced **36/36 streams byte-identical**, including
  s18; Node size and RAM are unchanged. Flash falls **304 B gateway / 140 B heltec_mobile**.
  [W1c QA evidence](../evidence/2026-09-25-standalone-mobile-home-w1c-qa.md).
- W1c replaced the default-name test with the counted stored-name contract and corrected the default-name comments
  and address-book/companion documentation (V1). B447's unnamed half is closed; named-peer precedence stays open.

## 5. Shared one-button editor

### 5.1 Contract [AGREED]

One editor supplies bytes to explicit callers; it never saves, edits presets or sends by itself. Two selection
levels (group, then character) with the same grammar: short advances, double chooses. Equal-sized groups in fixed
order with no dummy cells. Controls are separate from the alphabet. Drafts survive blank/wake and notifications;
the waking press only wakes; emergency holds keep precedence and nothing is submitted automatically afterwards.
RAM only, no NV write per character. Unsupported existing text is never silently changed.

### 5.2 Repertoire, groups and rings [AGREED owner 2026-09-23, D4]

Uppercase Latin letters, digits, space and five marks — 42 characters, each once, seven groups of six:

| Group | Characters, in order |
| --- | --- |
| 1 | `A B C D E F` |
| 2 | `G H I J K L` |
| 3 | `M N O P Q R` |
| 4 | `S T U V W X` |
| 5 | `Y Z 0 1 2 3` |
| 6 | `4 5 6 7 8 9` |
| 7 | `SPACE . , ? ! -` |

The repertoire is a subset of the preset grammar (printable ASCII, no `"` or `\`), so every draft is a valid
console quoted body. **Rings:** the group ring is the seven groups plus `EDIT`; a character ring is its six
characters plus `BACK`; the control ring is `DEL LEFT RIGHT DONE DISCARD BACK`. Entering any ring highlights its
first item. **After inserting a character the highlight returns to group 1.**

**Cost** (the audit's press-count model, re-derived 2026-09-23 with identical results; classified gestures, not
time): `STAN` 30; `RETURN TO BASE NOW` 126 — 88 shorts and 38 doubles, so at least 31 s of waiting from the 350 ms
short classification alone; a 33-character message 217. Deleting the character just typed costs 9 gestures
(7 shorts to `EDIT`, a double, a double on `DEL`); each further `DEL` is one double. No typing rate is claimed
before metal measurement. Accidental doubles are possible when tapping quickly (a second tap within the 350 ms
window chooses); `DEL` corrects them at that cost.

**Not chosen** (owner, 2026-09-23). Language-neutral figures assume every character equally likely:
- *Return to the group just used* (the r1 candidate): with a forward-only button it costs a near-full ring walk to
  reach any earlier group — 7.93 gestures per character on language-neutral text against 7.50. It wins only where
  consecutive characters share or follow a group (`ON MY WAY` 61 vs 66) and loses on the audit's four samples
  (`RETURN TO BASE NOW` 137 vs 126; `END OF SHIFT` 108 vs 84).
- *Six groups of seven*: ties on language-neutral text (7.50), wins some phrases and loses others (`WHERE ARE YOU?`
  103 vs 109; `RETURN TO BASE NOW` 134 vs 126), and spreads the digits over three groups instead of two.
- *Lowercase*: outside the owner's bare-minimum repertoire; 68 characters form no equal groups of six or seven, so
  its cheapest form would be a case switch among the controls (about 12 gestures per switch). A name typed over USB
  in lowercase stays editable over USB only (§4.3's preload rule).
- *Another five marks* (for example `:` for clock times or `'` for contractions): any five keep seven groups of
  six; the owner kept `. , ? ! -`.

### 5.3 Layout (5 rows × 19 columns)

Header (row 0): caller + used/cap right-aligned — `NAME           4/32`, `TO TEAM      45/163`,
`TO STAN      45/163`. Transient notes replace the caller part until the next press: `FULL`, `EMPTY`,
`TEAM CHANGED`, `RECIPIENT CHANGED`.
Rows 1–2: the draft on a fixed 19-column grid (no word wrap while editing, so a cursor column is predictable);
the two lines around the cursor; the cursor is a 6×1 underline below its cell (at the end: the next empty cell).
Rows 3–4: the current ring; `>` marks the highlight; items never clip:

```
group ring                 character ring (group 1)   control ring
row 3  >ABCDEF GHIJKL      row 3   A B C>D E F BACK    row 3  >DEL LEFT RIGHT
row 4   MNOPQR STUVWX      row 4  ADD D                row 4   DONE DISCARD BACK
```

The group ring shows the highlighted item and the next three (`>_.,?!- EDIT` / ` ABCDEF GHIJKL` wraps; SPACE
is drawn `_` only in the ring rows and announced `ADD SPACE`; with `BACK` highlighted row 4 reads
`BACK TO GROUPS`). Widths: 14, 17 and 18 columns worst case. A DM header's label has 8 columns (`»` included).

### 5.4 States

| State | short | double |
| --- | --- | --- |
| E1 group ring | next item (wraps) | group → E2 on its first character · `EDIT` → E3 on `DEL` |
| E2 character ring | next item (wraps) | character → insert at the cursor if `len < cap` (else note `FULL`, nothing inserted), return to E1 on group 1 · `BACK` → E1 on the same group |
| E3 control ring | next item (wraps) | `DEL` removes the byte before the cursor (stays on `DEL`) · `LEFT`/`RIGHT` move within 0..len (stay) · `DONE` → review if the draft is not empty or all spaces (else note `EMPTY`) · `DISCARD` → leaves at once when empty, else E4 · `BACK` → E1 on group 1 |
| E4 discard confirm (`DISCARD DRAFT?`, first 19 bytes shown, `>BACK` / ` DISCARD`, BACK first) | toggle | `BACK` → E3 · `DISCARD` → draft cleared, return to the opener |
| R review (§7.3) | toggle between its two actions | primary action (`SAVE`/`SEND`) or `EDIT`/`BACK` |

Cursor bounds are 0..len; `DEL` at 0 and moves past either end do nothing. The rail follows the body (R-4): a
message editor boxes SEND, a name editor boxes STATUS.

### 5.5 Event priority

| Event | Editor (E1–E4) | Review | Result |
| --- | --- | --- | --- |
| Any press while blanked | consumed, only wakes (R-1) | same | same |
| Blank (15 s idle) | kept, ring position kept | kept; **selection resets to `EDIT`/`BACK`** | kept (R-1) |
| Receive DM / team post | counters and R-7 wake only; no navigation; draft untouched | same | same |
| `long_arm` | kept under the overlay | kept; selection resets to `EDIT`/`BACK` | kept |
| `long_cancel` | returns after `CANCELLED` | returns with the safe selection | returns |
| `long_fire` | kept (a draft survives) | **closed**; a fresh review is required (§3.6.5); where it returns depends on the caller (§7.4.1) | per caller and request state (§7.4.1) |
| Short/double under the overlay | absorbed (existing R2/F3 rules) | absorbed | absorbed |
| Team changed / left | team caller: note `TEAM CHANGED`, draft kept | invalidated, per caller (§7.4.1) | a request not yet executed is refused at execution (§7.4); one already executed is unaffected |
| Recipient binding broken | DM caller: note `RECIPIENT CHANGED` | invalidated, per caller (§7.4.1) | as for a team change |
| Preset generation moved | — | preset review closes with `PRESET CHANGED` (existing rule) | may finish |
| Save/executor refusal | — | — | shown; acknowledgement returns per caller (§7.4.1) |
| Emergency pending | — | — | a saved phrase's confirmed request waits behind the alarm (existing slot order); a written message's unexecuted request was withdrawn at `long_fire` (§7.4.1) |
| Reboot / power loss | draft lost (RAM only) | lost | lost |

### 5.6 Draft ownership, lifetime and capacity

`UiModel` owns **one** draft: `bytes[kDraftMax]`, `len`, `cursor`, caller, cap, `draft_id`, `locked`.
`kDraftMax` is derived as the largest caller cap (163, §7.1). Caller caps: name 32; team and DM messages 163
(owner-ruled D6). The draft is created empty or preloaded (§4.3) and frozen with a new `draft_id` when a review
opens. Two separate things then hold it; the per-caller rules are §7.4.1:

- **The content lock** (`locked`) makes the bytes immutable from `SEND` until the request has been **executed** —
  the command line composed from them and the executor returned — because until then they are still to be read. A
  request withdrawn or refused before execution unlocks it.
- **Result ownership:** from `SEND` until its result is acknowledged, the result view owns the panel (B236's
  existing bound), so no editor can open and the draft cannot be replaced. At acknowledgement the draft is retained —
  unlocked, back in the editor — when the outcome shows nothing left the device, and released otherwise.

`DISCARD` clears it. Only the visible window (two grid lines or three review lines) is frozen per frame in
`UiState`; the whole draft is never copied into the doubled state, the snapshot or the stack (the detail modal's
rule).

### 5.7 Existing text outside the repertoire

Existing names or presets may contain lowercase, apostrophes or non-ASCII bytes. The editor never loads such
text (§4.3); it is shown read-only through the sanitising formatter. Presets are not editable on the panel.

## 6. Home and navigation

### 6.1 Navigation model [AGREED owner direction 2026-09-23; D1 resolved]

Two focus states:

- **List focus** — the body shows the `>` arrow; short moves it and wraps; double chooses.
- **Menu mode** — the left rail has focus; short moves to the next screen in the rail's order (Home, Team, Inbox,
  Send, Settings; team-gated slots skipped as today) and the body shows that screen's existing preview (the former
  passive form); double opens the previewed screen in list focus, arrow on its first row.

Rules (refinements 1–5 as accepted; refinement 6 withdrawn by the owner — rule 7):

1. Boot lands on Home in list focus, arrow on item 1 (after the optional splash, §9).
2. Every top-level screen's list ends with `MENU`: Home, Team (people), Inbox, Send (team phrases) and the
   Settings menu. `MENU` enters menu mode **on the Home slot** (refinement 1), so `MENU` then double returns Home
   from anywhere.
3. Sub-views keep their own exits to their opener: a person's compose list (`back, don't send`), message detail
   (`back`), editor, review, setup flows, My device and notes. Results keep either-press acknowledgement (R-5).
4. **Menu-mode cue** (refinement 3): a 2-pixel bar in the gutter between rail and text (x = 10–11, one rail slot
   high) beside the highlighted slot, drawn with the existing rectangle call; no `>` in the body. List focus draws
   no bar.
5. Home's arrow follows its item, not its row (refinement 4) — §6.4.
6. The panel never calls the landing screen `HOME` (refinement 5): `HOME` already means the mobile's routing home
   (`HOME 42s`, the strip's home slot). The rail slot keeps its icon; "Home" is design vocabulary only.
7. **The panel going dark never re-homes** (owner, 2026-09-23; refinement 6 withdrawn): a blank keeps the current
   screen, the focus state (list focus or menu mode) and the arrow; the next wake — a consumed press or an R-7
   message wake — shows exactly what was there (R-1/§3.3, S1). Neither a blank nor a push changes the screen
   (S8). The only exceptions are the two existing unfinished-confirmation cancellations (UI-16 OQ-3, owner-ruled),
   which stay as they are: at the blank an uncommitted key-grant confirmation falls back to its invitation list —
   or, for a TEAM-roster grant, to the TEAM list — and a saved-key offer closes to the PROVISION menu. Neither
   lands on Home, and a setup's typed origin (§6.6) survives them.
8. Emergency holds work in both states; the overlay rules are unchanged; afterwards the previous focus returns.
9. The rail names the body in both states (R-4); in menu mode its box follows the previewed screen.
10. Session-unread moves only as it does today: `FrameGate::on_page` advances each read watermark to the arrival
    serial a complete, visible frame froze, whenever that frame showed the Inbox screen with no emergency overlay,
    compose sub-view or detail modal over it. Today that includes the passive Inbox screen; under this design it is
    the Inbox list and the Inbox's menu-mode preview (its former passive form). Such a frame marks read every arrival
    it froze, not only the rows it drew. This design does not change that rule.

**Settings:** its menu-mode preview is today's closed view without the body arrow (`ENTER SETTINGS`, rule 4); double
opens the settings menu in list focus. The menu's last row becomes `MENU` (was `BACK`): it keeps `BACK`'s
draft-preserving `on_back()`, then enters menu mode on the Home slot; short wraps within the menu instead of walking off the end —
a named revision of B232/§UI-14. An unsaved draft survives leaving (badge; R-3).

### 6.2 Home rows [AGREED owner 2026-09-23, D2]

- **Row 0:** identity, `ME <label>` (§4.2).
- **Row 1:** team line — `TEAM 12A1B2C3 T220` (18 columns), `TEAM 12A1B2C3 NO ID` (19) before team-DAD assigns
  the local ID, `NO TEAM`, or blank on a gateway. The eight-digit ID ends with the six-digit nearby/invite
  fingerprint (`A1B2C3`), so the displays agree without a new format.
- **Row 2:** `RESTART NEEDED` while set (not selectable; R-3/§3.6.5); otherwise the list's first visible row.
- **Rows 2–4** (3–4 while `RESTART NEEDED`): the list window, scrolling to keep the arrow visible.
- **While an unread team post is on the card** (§8, D12), rows 1–2 show it in place of the team line and the list
  window is rows 3–4; no item is added or removed and the arrow keeps its item.
- Status rides in the labels: unread in `INBOX 3 NEW` (the strip's `99+` token, omitted at zero), route count in
  `TEAM 4 KNOWN` (the strip's `9+` token, omitted at zero), key state in `NO TEAM KEY - HELP`. Home age stays in the strip; the own
  position moves to My device; the 24×24 mark leaves Home (D2, splash D3).

### 6.3 Home lists by state [AGREED owner 2026-09-23, D2 with D2a–D2c]

| State | Items, in order |
| --- | --- |
| No team (team build) | `JOIN TEAM` · `CREATE TEAM` · `INBOX` · `MY DEVICE` · `MENU` |
| In a team, no team key | `NO TEAM KEY - HELP` · `INBOX` · `TEAM` · `MY DEVICE` · `MENU` |
| In a team, key held, local ID pending | `INBOX` · `TEAM` · `MY DEVICE` · `MENU` |
| In a team, ready (key and local ID) | `INBOX` · `SEND TO TEAM` · `TEAM` · `INVITE MEMBER` · `MY DEVICE` · `MENU` |
| Gateway (no team plane) | `INBOX` · `MY DEVICE` · `MENU` |

A missing key takes precedence over a pending ID. Static-role team-build nodes use the no-team list (JOIN/CREATE
promote them as today). Only actions that can work appear: UI team posts are sealed-only, so `SEND TO TEAM` needs
the key; while the local ID is pending, DMs are refused (`err_no_binding`) and a registered member's team post would
originate under its static ID (B444), so `SEND TO TEAM` and `INVITE MEMBER` wait for the ID. Each action item also
needs its capability (`prov_join_team`, `prov_create_team`, `prov_invite`): production OLED team builds have them, so
the lists are as shown; a build without one omits that item rather than offering an action that cannot run. The order
is fixed per state; counts never reorder items.

```
ready                       no team                     no team key
ME STAN                     ME STAN                     ME STAN
TEAM 12A1B2C3 T220          NO TEAM                     TEAM 12A1B2C3 T220
>INBOX 3 NEW                >JOIN TEAM                  >NO TEAM KEY - HELP
 SEND TO TEAM                CREATE TEAM                 INBOX 2 NEW
 TEAM 4 KNOWN                INBOX                       TEAM 4 KNOWN
```

**Gesture counts** in the ready state from the arrow on item 1 (review step excluded): open the newest message 2 ·
send a team phrase 3 · a phrase to teammate #2 6 · invite 4 · My device 5 · back to Home from a screen list: walk
to `MENU`, then 2 · open Settings 11 (walk 5, `MENU`, four rail steps, double).

**Sub-choices, owner-ruled with D2:** **D2a** `INBOX` first — a group member receives far more than they send.
**D2b** `INVITE MEMBER` on Home for key holders — the organiser's onboarding step becomes findable; Settings →
PROVISION keeps offering it to every member, as today. **D2c** Settings through `MENU` — rare for wearers.
[Not chosen: `SEND TO TEAM` first (send 2, read 3); invite only in Settings; Settings as a Home item (Settings 5,
every later item one step further).]

**Left off Home deliberately:**

| Item | Why |
| --- | --- |
| SETTINGS | rare for wearers; `MENU` reaches it (D2c) |
| `WRITE MESSAGE` shortcut | lives in SEND TO TEAM and in each person; saves about 2 of 100+ presses |
| JOIN NETWORK, SAVED KEYS | expert and rare; Settings → PROVISION unchanged |
| USE SAVED KEY | checking for it means a flash read on every refresh; the join flow already offers it |
| LEAVE TEAM | destructive and rare; the console, unchanged |
| Emergency | stays the long hold; as a list item one stray double could fire it |
| Reply / acknowledge | out of scope (B118) |

### 6.4 Arrow rules [AGREED refinement 4; details PROPOSED]

- The arrow lands on item 1 at boot and after `MENU` then double on Home.
- Returning from something Home opened (My device, a note, a setup flow) lands on the item that opened it; if that
  item no longer exists (for example CREATE TEAM once a team exists), on item 1.
- A blank never moves the arrow or changes the screen (§6.1 rule 7).
- The list follows state changes while shown (a join completes, the key arrives, the team is left from the
  console). The arrow keeps its item by identity (the action kind), never by row number; a count changing inside a
  label is not a change of item. If the arrow's item disappears while Home is in list focus (lit or dark), the arrow
  moves to item 1 and `OPTIONS CHANGED` (15 columns) replaces item 1's label until the next press — the
  `TEAMMATE GONE, pick` idiom, pinned exactly (W4b):

  | While the note shows | Result |
  | --- | --- |
  | a short or a double (not a wake press) | clears the note and nothing else: the arrow stays on item 1, a double runs nothing |
  | the wake press on a dark panel | only wakes (S1/R-7); the note stays |
  | the list changes again (items leave or return) | the arrow stays on item 1 of the newest list; the note stays |
  | an emergency hold | works as ever; afterwards the note is still there |

  Menu mode raises no note: its preview has no arrow, and a later double opens Home on item 1.

### 6.5 What Home items open [PROPOSED]

| Item | Opens | Rail | Way back |
| --- | --- | --- | --- |
| `INBOX` | Inbox in list focus, arrow on the first row — the newest message (§6.8) | INBOX | its `MENU` row, then double |
| `SEND TO TEAM` | Send in list focus: team phrases, `WRITE MESSAGE`, `MENU` (§7.5) | SEND | `MENU`, then double |
| `TEAM` | Team in list focus: people, `MENU`; a person opens their compose list | TEAM | `MENU`, then double |
| `INVITE MEMBER` | the existing invitation window, opened directly with no settings gate (it also announces the team, B249; D11) | SETTINGS | its BACK → Home |
| `JOIN TEAM` / `CREATE TEAM` | the settings gate (§6.6) → [name prompt when unnamed, §4.4] → NEARBY / `CREATE NEW TEAM` confirmation (BACK first) | SETTINGS | exits that return to the PROVISION menu today return to Home; an acknowledged result is not undone (D11) |
| `NO TEAM KEY - HELP` | note `NO TEAM KEY` / `A MEMBER WHO HAS IT` / `MUST GRANT IT TO` / `THIS DEVICE` / `press = back` — the existing procedure (a key holder uses INVITE MEMBER or TEAM → GRANT KEY); no automatic key request | STATUS | either press → Home |
| `MY DEVICE` | §6.7 | STATUS | its BACK → Home |
| `MENU` | menu mode on the Home slot | STATUS | double → Home |

**Interim content until later packages (W4b):** `INBOX` opens today's Inbox order (W4c/W4d later make it
newest-first) and `SEND TO TEAM` opens the team phrases alone (`WRITE MESSAGE` arrives with W8). The Send list is
the existing channel compose list promoted to a top-level list — its phrases, then `MENU` in place of `back, don't
send`; a phrase's send, result and acknowledgement are today's, and acknowledging a result returns to the Send list
on item 1. A catalog change while the Send list is open re-reads it like Home's: the arrow goes to item 1 and
`PRESET CHANGED` replaces item 1's label until the next press, which sends nothing (today's rule that a catalog
change never lets a press send, kept for a list that no longer closes).

### 6.6 Setup from Home [AGREED owner 2026-09-23, D11]

Today the three actions live under SETTINGS → PROVISION, whose entry gate (`settings_activate`, `CfgRow::provision`)
needs the open settings service and refuses a conflicted draft (`RELOAD OR DISCARD`) before an unsaved one (`SAVE OR
DISCARD`), never saving on the operator's behalf; every exit returns to the PROVISION menu, and leaving SETTINGS
closes provisioning. `JOIN` and `CREATE` change the stored settings (membership, the team-key binding, radio);
`INVITE` changes none — it announces the team once, reads the key cache, may ask for a public key and grants the key
by sealed message (`DeviceInvite`).

1. **`JOIN TEAM` and `CREATE TEAM` pass the same gate** as SETTINGS → PROVISION — opening the settings service as
   arrival on SETTINGS does, then the conflict and unsaved checks in that order — factored once (§13 W3). Same
   words; it never saves on the operator's behalf.
2. **`INVITE MEMBER` opens the invitation window directly, with no gate,** because it changes no settings. SETTINGS
   → PROVISION keeps gating its whole menu, as today.
3. **The flow records its origin** (`home` or `settings`) as an explicit typed value, B250's return-context
   precedent — never inferred from the screen or the arm. Every exit that returns to the PROVISION menu today
   returns to Home when the origin is `home`; a missing origin falls back to the PROVISION menu. An acknowledged
   result is never undone. The flow still lives in the SETTINGS sub-view (rail on SETTINGS, R-4), so the existing
   rule that leaving SETTINGS closes provisioning stays true.
4. **A blocked gate shows a note in the SETTINGS slot**, never configuration text on the Home body (UI-17 R-3):
   `SAVE OR DISCARD` / `IN SETTINGS`, `RELOAD OR DISCARD` / `IN SETTINGS`, or `CFG UNAVAILABLE` alone when the
   service cannot open — the reason on body row 1, `IN SETTINGS` on row 2, no arrow. The reason is frozen when the
   note opens: a later recovery neither changes it nor resumes the setup. Either press dismisses it and returns to
   Home with the arrow on the item that opened it.
5. **The gate runs before the name prompt** (§4.4), so a blocked device is not asked for a name first.

No scanning, PHY change or authorization shortcut is added; NEARBY stays passive and frozen per entry. Not chosen:
hiding `JOIN`/`CREATE` while blocked — the items would vanish without explanation and every change to a settings
draft would fire the `OPTIONS CHANGED` guard.

### 6.7 My device

```
STANISLAW KOZICKI       name, bytes 0–18 (sanitised)
                        name, bytes 19–31
ID 0x12AB34CD           own key_hash32
52.123,21.456           position, or NO LOCATION (UI-17 S-9/S-10 formats)
 CHANGE NAME >BACK      one action row, BACK first (18 columns)
```

Before the editor package lands the action row is `>BACK`.

### 6.8 Inbox order [AGREED owner 2026-09-23, including the restart rule (D13b)]

The owner: *"inbox - we should sort with newest at top - so sorting to get newest messages from top"*. The Inbox is
one list, newest at the top, DMs and team posts interleaved. Each row keeps its `DM` / `CH <n>` label.

- **What is listed does not change:** the four newest DMs and the four newest team posts (the parent spec's
  anti-eviction rule, B191), so a burst of team posts cannot push every DM off the panel. Only their order
  changes.
- **The restart rule** the parent spec §6.1 asked for before interleaving: receive time (`rx_time_ms`) is uptime and
  restarts at boot, so it orders only messages received since the last restart. When the UI starts at boot it
  records the Inbox's newest sequence number per kind (`Inbox::dm_newest_seq()` / `chan_newest_seq()`, existing
  accessors); the Inbox is restored earlier in setup and nothing is stored before the main loop runs, so the
  boundary is exact, costs no flash scan and needs no core change. `Inbox::clear()` keeps the high-water, so later
  messages stay above it after a wipe. Rows above the boundary arrived this session, the rest before the restart.
  1. This session's rows come first, by full-precision receive time (`rx_time_ms`), newest first; equal times put
     the DM first, then the higher sequence.
  2. Rows from before the restart follow — DMs newest-first, then team posts newest-first (today's order within
     each kind) — because no clock orders them against each other. Their age shows `--` (this also fixes B445).
  Placing every earlier row below every current one is always chronologically right; the only order this rule
  cannot give is DMs against team posts that both arrived before the last restart. No rule can show those rows'
  true age: the device has no real-time clock. The persistent arrival serial that would fix only that residual order
  was considered and not chosen (D13b).
- **The ordering key reaches the merge at full precision.** A published row carries only its whole-second age
  (`InboxRow::rx_age_s`), which cannot order two posts inside one second: with `now` = 2000 ms, a DM at 1100 ms and
  a team post at 1900 ms both show `0s`. The Inbox pull callback (`inbox_row_cb`) therefore hands `InboxRowBudget`
  each row together with its `InboxEntry::rx_time_ms` (64-bit uptime), kept beside the staged row and never
  published; `publish` merges on it. The four-per-kind budget and the restart split are unchanged; `UiSnapshot` does
  not grow.
- Row identity stays `(kind, seq)`: the arrow follows its message when rows move (the B64/B231 rules), and after a
  delete the arrow moves to the next row in the displayed order.
- The Home `INBOX` item therefore opens on the newest message received since the last restart.
- Unread counting, the detail view and deletion are unchanged.

## 7. Messages

### 7.1 Capacity derivation [FACT arithmetic; message cap AGREED owner 2026-09-23, D7 for phrases and D6 for written messages]

| Quantity | Equation | Bytes | Anchor |
| --- | --- | --- | --- |
| LoRa frame | — | 255 | `lora_max_frame_bytes` |
| DATA inner (plain) | 255 − header 8 − MAC/overhead 6 | 241 | `max_payload_bytes_hard_cap` |
| App-DM body, admission | 241 − (dst hash 4 + origin 1 + source hash 4) | **232** | `dm_max_body_bytes`; `on_command` send |
| Sealed DM inner | 255 − header 8 − nonce seed 8 | 239 | `data_inner_cap(CRYPTED, 0)` |
| Sealed DM body | 239 − dst hash 4 − origin 1 − source hash 4 − tag 16 | **214** | `enqueue_data` seal cap |
| Sealed DM body with `-l` | 214 − location 6 | **208** | same |
| Channel payload | — | 200 | `channel_msg_max_payload_bytes` |
| Sealed channel plaintext | 200 − seal 26 | 174 | `channel_seal_max_plaintext_bytes` |
| Sealed team text | 174 − flags 1 | **173** | `on_command` send_channel |
| Sealed team text with `-l` | 174 − flags 1 − source hash 4 − location 6 | **163** | same |
| Parser quoted body | ends at `"`; > 241 clamped then refused by every consumer | 241 | `parse_send_tail` |
| Local command line | — | 1023 | `local_command_max_bytes` |
| Name | `/mrid` field = wire name field | 32 | `IdBlob::name`, `peer_name_max` |
| Inbox record body | — | 241 | `inbox_max_body` |

**Caps:** name **32**. **Every message the panel sends holds up to 163 bytes** — saved phrases (T = 163 per slot,
owner-ruled D7) and written messages to the team or to a person (owner-ruled D6). 163 is the smallest admission of
any destination with location on (a sealed team post with `-l`), so every message fits in every shape and location
state: team 173 (163 with `-l`); DM plain 232 or sealed 214 (208 with `-l`); INTRO never constrains it. The UI
always posts to the team sealed (`-t -e`). Written messages never request location (D6, §7.4); the shared limit
leaves room for a later per-message location switch without changing it. The emergency slot must stay ≤ 163 or an
alarm with a fix would be refused. **Line buffer:** the longest composed form is a located team phrase,
`send_channel ` + channel ID (10 digits for the promoted `%u`) + ` "` + 163 + `"` + ` -t -l -e` + NUL = 199 bytes;
`kSendLineCap` (96 today) is re-derived from the named constants by `static_assert` and moves off the loop-task
stack to a static buffer, because `exec_command` → `on_command` → `enqueue_data` is a deep chain (the
`do_post_ack` stack lesson).

### 7.2 Presentation: grid while editing, word wrap in review [PROPOSED]

The editor uses a fixed 19-column grid (§5.3). Review uses **presentation-only word wrap** over the exact bytes.
From a line start `s`, in this order: (1) the remainder fits in 19 → it is the last line; (2) byte `s+19` is a
space or byte `s+18` is a space → the line is the 19 bytes `s..s+18` (a following space begins the next line);
(3) the window `s..s+18` contains a space → the line ends after the last such space (the space stays at the line's
end); (4) otherwise the word is split after 19 bytes. Every byte occupies one cell exactly once,
in order: concatenating the lines reproduces the payload, and no newline enters the payload. Authored paragraph
breaks are **not** added in this phase (no CR/LF in the grammar or the editor). Pages hold three lines; page count
is computed from the wrap (≤ 6 for 163 bytes, since any two consecutive lines hold ≥ 20 bytes). Pages advance on
the detail modal's `kDetailPageMs` cadence and cycle; a page change is time-driven, never a press, so it can
never send; blanking suspends the cadence and wake restarts it on the same page (UI-17 S2's rule). The detail
modal's own byte-slice paging is unchanged in this design.

### 7.3 One review screen [AGREED owner 2026-09-23, D5]

The same review serves saved phrases, manual messages and names:

```
TO TEAM 12A1B2C3        destination: team (full ID) · DM: TO STAN 3F2A91BC (label ≤ 7 columns) · unverified: TO T7 UNVERIFIED
RETURN TO BASE NOW.     page line 1
MEET AT THE NORTH       page line 2
GATE.                   page line 3
 SEND >EDIT     1/2     two actions + page (19 columns); preset: SEND/BACK, LOC when located; name: SAVE/EDIT (no page)
```

**Location is shown before it is sent** (D6): the review of a phrase whose location switch is on shows `LOC` in
columns 12–14 of the action row (` SEND >BACK LOC 1/2`, 19 columns). `LOC` states the phrase's request, not a
position: without a position the core refuses the send exactly as today — the result reads `NO FIX` for a DM
(asynchronous `send_failed`) and `REFUSED` with its code for a team post, whose synchronous refusal does not say
why (`refuse_reason_of`). Written messages and names never show it, because they never carry location.

A double on a preset row opens this review instead of sending (a behaviour change to UI-10/11 P3). Review opens
on the non-sending action (`EDIT` or `BACK`); sending costs short + double. Review is a confirmation, not a
result: R-5's either-press acknowledgement stays with results. It binds destination, exact bytes, location policy
and — for presets — slot and generation; it is invalidated by the events in §5.5. With Home resting on its
list, this review is also what stops two stray doubles from sending a phrase.

### 7.4 Submission [PROPOSED]

- `SendKind` gains `dm_text` and `channel_text`; `SendReq` gains `draft_id`, `team_id` and `peer_hash`
  (≈ 20 B host). Manual text is never a fake slot or generation.
- `send_gate_of` asks each kind only the questions that apply, each its own refusal with zero core submission:
  - written messages (`dm_text`, `channel_text`): the draft is still locked with the same `draft_id`;
  - saved phrases (`dm`, `channel_canned`): today's slot, generation, enabled and kind checks, unchanged;
  - all four ordinary kinds: `team_id` is still the live team (for phrases this closes the same gap — a named
    change);
  - both DM kinds, when a `peer_hash` was known at selection: the ID still resolves to that hash.

  The emergency kind stays ungated.
- The composer uses the existing forms (U1): `send <id> "<draft>" -t -a` and
  `send_channel <ch> "<draft>" -t -e`, bytes read from the locked draft with `%.*s`. Written messages never add
  `-l` (owner-ruled D6). The GPS design requires every new on-device sender to be classified for location (its
  §4.5): written messages are **not location-eligible**. A later GPS package may add a per-message location switch;
  the 163-byte limit already leaves it room. Phrases keep UI-10/11 R-2 — the slot's switch alone decides, the GPS
  supplies only availability — which that design's text does not yet reflect (B446).
- Tracking, outcomes, `ctr == 0` handling and the emergency/normal slot separation are the existing
  `ui_perform_send`/tracker path; manual text uses the normal slot only.

#### 7.4.1 Review, submission and result by caller

Saved phrases have no draft: their review returns to the list the phrase was chosen from and never creates an
editable preset. The name editor returns by its recorded origin (§4.3).

**Request states** after `SEND` (written messages and saved phrases):

- **Queued** — confirmed and waiting in the normal slot (`_req_pending`), not yet drained.
- **Known refused** — known not to have aired: refused before the core accepted it — after draining by
  `send_gate_of` (zero submission; `take_send_request` has already cleared the pending flag) or by the composer, or
  synchronously by the executor (parser or `err_*`) — or accepted and then answered by a typed failure that
  establishes the message never aired. `send_failed` by itself is not that proof: `no_ack` and `e2e_ack_timeout` arrive the same way, and such a
  message may have been received. The W8 brief classifies every `SendFailReason` from the core's code paths; a
  reason it cannot classify counts as accepted, final.
- **Accepted, open** — the core accepted it (`queued`, with or without a handle) and no final outcome has arrived.
  `SENT, waiting` belongs here: it reports the air, not a result (`match_aired` does not consume the tracker), so
  tracking continues until an outcome or the view closes.
- **Accepted, final** — any other final outcome: `PICKED UP`, `DELIVERED`, `NO RELAY HEARD`, `NO CONFIRM`,
  `NOT CONFIRMED`, or a failure that may have aired. While the view stays open, `NO CONFIRM` can still upgrade to
  `DELIVERED` through the existing late-ACK path.

Review phase:

| Event | Name (`SAVE NAME?`) | Written message (team or DM) | Saved phrase (team or DM) |
| --- | --- | --- | --- |
| Review opens | from the editor's `DONE`: draft frozen with a new `draft_id`; selection on `EDIT` | same | a double on a phrase row: binds slot, generation, team and (DM) the peer hash; no draft; selection on `BACK` |
| `EDIT` / `BACK` | editor on group 1, draft unfrozen | same | the phrase list, arrow on that phrase |
| Blank, `long_arm` | review kept; selection resets to `EDIT` | same | same, reset to `BACK` |
| `long_fire` | review closed; after the alarm the editor shows the draft | same | review closed; after the alarm the phrase list |
| Team changed or recipient binding broken | — | review closed → editor with `TEAM CHANGED` / `RECIPIENT CHANGED`, draft kept | review closed → the phrase list with the same note; with no team left, Home; with the teammate gone, the Team list with the existing `TEAMMATE GONE, pick` row |
| Preset generation moved | — | — | review closed → the phrase list, `PRESET CHANGED` (existing rule) |
| Primary action | `SAVE`: one bounded save (§4.3) → `NAME SAVED` / `NAME NOT SAVED` | `SEND`: request queued, draft content-locked, result view `SENDING...` | `SEND`: request queued (existing path), result view |

After `SEND`:

| Request state | Event | Written message | Saved phrase |
| --- | --- | --- | --- |
| Queued | a press | consumed and ignored: the request executes on the next service pass; the draft stays locked | the view closes; the request still executes (today) |
| Queued | `long_fire` | **withdrawn**: only the pending ordinary request is cleared (the emergency request is untouched) and the draft is unlocked; it is never sent after the alarm, when the editor shows it for a fresh review | waits in today's slot order and is re-gated at execution; its view closes as compose closes today (§B101) |
| Known refused | a press (acknowledgement) | editor, draft unlocked | the phrase list |
| Known refused | `long_fire` | the view closes (§B101); after the alarm the editor shows the draft, unlocked, for a fresh review; nothing is re-submitted | the view closes; after the alarm the phrase list |
| Accepted, open | a press | the view closes and tracking ends (today's rule); draft released — **the declared residual:** a later outcome is not shown and the text is not kept; nothing claims the message failed or never aired | the view closes and tracking ends (today) |
| Accepted, open | `long_fire` | the same residual; the view closes (§B101) | the view closes (§B101) |
| Accepted, final | a press | the list `WRITE MESSAGE` came from; draft released | the phrase list |
| Accepted, final | `long_fire` | the view closes; draft released | the view closes |
| any | reboot | draft and request lost (RAM only) | request lost |

Name results:

| Result | Acknowledgement | `long_fire` |
| --- | --- | --- |
| `NAME SAVED` | draft released; origin `my_device` → My device; origin `setup_join` / `setup_create` → the chosen setup step after the settings gate is asked again (a refusing gate shows its note; acknowledging it → Home) | draft released; after the alarm My device, or Home for a setup origin — setup never resumes by itself after an alarm |
| `NAME NOT SAVED` + reason | the editor with the draft | after the alarm the editor with the draft |

The withdrawal applies only to written messages, because only they carry typed text that a refusal nobody sees
would lose; saved phrases keep today's behaviour (the UI-10/11 path, unchanged). "Waits in today's slot order" is
the existing drain priority, not a new timer or a wait for the overlay to close.

### 7.5 Entry points [PROPOSED]

- **Team:** Home `SEND TO TEAM` or menu mode → the Send screen's list: enabled team phrases → `WRITE MESSAGE` →
  `MENU`. It replaces today's channel-compose sub-view, whose exit row was `back, don't send`; as a screen list it
  now ends with `MENU` (§6.1 rule 2). An empty catalog still offers `WRITE MESSAGE` (the empty-state note stays).
- **Person:** Home `TEAM` or menu mode → Team list → a person → their compose sub-view: enabled DM phrases →
  `WRITE MESSAGE` → K7's `GRANT KEY` when offered → `back, don't send` (a sub-view keeps its exit; K7's semantics
  unchanged; its position moves by one — named revision of preset spec R-1).
- `WRITE MESSAGE` opens the editor with the destination already bound; a manual message is never retargeted.

### 7.6 Outcome wording [FACT reused]

Results reuse the existing states: `SENDING...`, `QUEUED`, `SENT, waiting`, `DELIVERED to <label>` (DM with a
matching E2E ACK only), `NO KEY`, `NO CONFIRM`, `PICKED UP` (a relay heard it), `NO RELAY HEARD`,
`NOT CONFIRMED`, refusals with their code. A team post never claims whole-team delivery or a human reply.

### 7.7 Preset catalog v2 [size and default phrases AGREED owner 2026-09-23, D7/D9]

- `/mrui` version 2: 17 fixed slots `{enabled, loc, len, text[T+1]}`, `T = 163`: slot 167 B, record 2852 B
  (12-B header, one named tail byte). Canonical-byte, generation, busy, coalescing and four-state rules unchanged.
- `validate_preset_text` accepts 1..T bytes of the same grammar. The `ui preset` NDJSON keeps
  `ui_presets_end.capacity` as the **slot count** (`kUiPresets`, 17) and gains a separate `text_max` field — the
  byte limit, from `kUiPresetTextMax` (163) — so the companion sizes its editor from the record's own constants;
  the overlength arm of `bad_text` then uses `text_max`, while absent, empty or all-space text and forbidden bytes
  still answer `bad_text` as today. The companion contract's `ui preset` section
  (`ios-companion/INBOX_SYNC_CONTRACT.md`, which still states 1..17 bytes) changes in the same package.
- **The reply line** `kPresetLineMax` (a literal 160 today) is re-derived by `static_assert` from the widest record —
  `ui_preset` for `emergency`, enabled, 163 bytes, location off: 243 bytes with its newline, 244 with the NUL. At
  160, `JsonBuf::finish` would return 0 and a successful `set` or `list` would emit nothing. All four users that
  size a buffer from it are in W6's fence: `preset_emit_record`, `preset_emit_list` and `preset_emit_err` on the
  console path, and `preset_boot_restore` on the boot-diagnostic path. Each widened buffer grows by 84 B of stack;
  the peak stack on either path is measured at the brief, not inferred (§11.1). A brief may instead derive a
  separate, smaller bound for the boot diagnostic, if it says so.
- The snapshot projection is **decoupled from T**: each compose row keeps 17 text columns — first 16 bytes plus
  `»` when longer — and a length, so `UiSnapshot` does not grow with T (without this it would grow 2336 B static
  and again on the per-tick stack). The full text is read from the live catalog only when a review opens.
- Default phrases [owner-ruled D9]: the five existing defaults stay (emergency `I'm in danger`, location on;
  personal `Are you OK?`, `I'm OK`; team `Got your message`, `All good`). Added, all location off: team 3
  `Return to base now`, team 4 `On my way`, personal 3 `Where are you?`; the remaining slots stay empty and disabled.
  A list row shows 17 columns, so `Return to base now` appears as `Return to base n»`; the review shows it whole
  and it is always sent intact. Compiled defaults apply to absent, invalid and old-format stores (§7.8); a valid
  v2 record keeps every custom, disabled and location field.

### 7.8 `/mrui` version 2: defaults until replaced, no upgrade reader [AGREED owner 2026-09-23, D8]

MeshRoute is not deployed and needs no backward compatibility (owner, 2026-09-23). The record keeps the rule every
store except `/mrcfg` follows: the version must match exactly, and there is no migration arm
(`ui_preset_blob_state`, retained). A board carrying an old record runs on the defaults and says so at every boot
until the first successful change replaces the record (or an erase removes it); nothing is written at boot to make
that happen sooner. Its custom phrases are re-entered over USB; `ui preset list` before flashing shows them. Boards that never changed
a phrase have no record and simply get the new defaults.

| Stored record | Boot | Diagnostic | First successful change |
| --- | --- | --- | --- |
| absent | compiled defaults | none | writes v2 |
| exact canonical v2 | loaded | none | writes v2 |
| old v1 record (372 B, `'MRU1'`, version 1) | compiled defaults; nothing converted; **zero boot writes** | `ui presets = DEFAULTS (old v1 record — re-enter custom phrases)`, at every boot until replaced | writes v2 over it |
| any other size, magic or version, or non-canonical | compiled defaults | existing `record invalid` line | writes v2 (repair) |
| `io_failed` | compiled defaults | existing `store unreadable` line | every mutation refuses `store`, zero writes |
| v2 save fails | live catalog unchanged | verb error `store` | retry allowed |
| power cut during a v2 write | NVS keeps the old or the new value (physical check NV-06) | — | — |

Recognising the old record needs only its magic, version and size — a distinct message, no reader of its
contents. Same key; factory reset erases it.

### 7.9 Emergency slot

Unchanged behaviour: mandatory, never cleared, long hold only, location on + fix adds `-l`, no fix sends without
it. Its text is bounded by T ≤ 163 so the located alarm is admitted. The overlay never draws the phrase; the
tracker correlates by counter, so longer text changes neither rendering nor tracking.

## 8. Home card of a received team post [AGREED owner 2026-09-24, D12]

**Today** a sealed team post wakes the panel on whatever screen is showing (UI-17 R-7), raises the unread count and
never changes the screen (S8); a cleartext post only counts (`ui_route_recv_push`). With the Home of §6, reading it
takes 2 gestures from `INBOX n NEW`.

**Ruled: a display-only card.**

```
ME STAN                 row 0 unchanged
FROM T7 20s +2          sender at posting time · age · other unread team posts (widest FROM T254 59m +9+, 17)
RETURN TO BASE NOW      the post's first 19 bytes, `»` after 18 when longer
>INBOX 3 NEW            the list window, rows 3–4
 SEND TO TEAM
```

- **Eligibility:** a live `channel_recv` push after boot with `enc == true`, `team_id` = the current nonzero team,
  `channel_id` = `MR_UI_TEAM_CHANNEL_ID`, same-team post, nonzero Inbox `seq`. Cleartext, foreign-team,
  other-channel and internal records never qualify. Stored history is never scanned; nothing is shown after a
  reboot. DM preview is not included (§1.2).
- **Provenance:** `FROM T7` is the team-local ID at posting time, plus the record's age. A shared team key
  authenticates membership, not an individual or organiser; no name is reconstructed from today's roster; the
  retained 16-bit hash fragment is not shown as an identity.
- **State:** one card in `UiModel` — the post's **session arrival serial** (the `UiInboxCounters::arr_ch` value its
  push produced), the storage epoch, team, origin, receive time, the first 19 sanitised bytes (copied at push time,
  the push body being borrowed) and a `more` flag. The Inbox record sequence is deliberately not used: it is
  persistent and continues from restored records, while the read watermark counts this session's arrivals — a store
  restored at sequence 100 gives the first new post sequence 101 but arrival serial 1. A newer eligible arrival
  replaces the card (the newest wins) and raises `+<n>`, the count of other eligible arrivals since the card was
  last cleared (capped `9+`).
- **Display:** whenever Home's body is drawn and the card has not cleared, rows 1–2 show the card in place of the
  team line and the list window is rows 3–4. No item is added or removed and the arrow keeps its item (§6.4), so a
  card appearing under the finger never changes what a double does. While `RESTART NEEDED` holds row 2, the card
  keeps only its header row.
- **Clearing:** the card clears when the channel read watermark reaches its arrival serial — `int32_t(read_ch −
  serial) >= 0`, the counters' own unsigned modular arithmetic — that is, when a complete, visible Inbox frame (the
  list or its menu-mode preview, §6.1 rule 10) froze an arrival serial at or past it. A post that arrives while such
  a frame pages out is not covered by that frame and keeps its card. A team change or an Inbox epoch change also
  clears it. The card keeps a separate present flag, because after a wrap an arrival serial of 0 is an ordinary
  value; the comparison assumes fewer than 2^31 arrivals between the two serials, unreachable as for the counters
  themselves. Nothing here persists; `Inbox::mark_read` is not used.
- **No actions of its own:** `INBOX` (item 1) opens the Inbox with the arrow on the newest message (§6.8). Nothing
  on the card marks read, deletes, acknowledges or sends.
- **Interruption:** no wake of its own (R-6/R-7 unchanged — the existing sealed-post wake lights the panel and the
  card is simply what Home then shows); never drawn under the emergency overlay; not distress confirmation; no
  keyword privilege — `Return to base now` is ordinary text.
- Not chosen: the r2 card with `OPEN`/`HIDE` rows at the top of the list, shown only when Home's list was rebuilt
  (after `MENU` then double, or a return from a sub-screen) and therefore absent when a post wakes a panel already
  on Home; deferring; dropping the preview.

## 9. Boot splash [AGREED owner 2026-09-24, D3]

**Today** there is none: the panel is brought up at the very end of `setup()` (`mr_ui_init`), after the
unconditional 2 s settle for the USB serial monitor, the NV loads and the radio start, so the panel is dark for at
least 2 s after power-on or reset and then shows Home. The UI-5 bring-up splash was a canvas test and is removed
(metal plan EXCL-01); `mr_ui_init`'s note says so.

**Ruled:** with the mark gone from Home, show it once after start-up. Geometry (the whole 128×64 panel; no rail or
strip during the splash): the mark at x 52–75, y 8–31; the ID line in the 6×10 font centred on y 44–53 —
`2035757-dirty` is 78 px wide, so x 25–102.

- **Content:** the 24×24 mark centred, and one line under it with the build's Git ID — `kGitRevision`, the same
  string the `version` command prints (`<short-sha>[-dirty]`, 13 columns at most today). It is formatted by a pure
  function with a native case; a longer override string is shortened with `»` at 19 columns.
- **Timing:** `kSplashMs` ≈ 1000 ms from the first UI tick, then Home in list focus with the arrow on item 1.
- The splash is a view state, not a delay: the loop, radio and push drain continue. Any short or double dismisses it
  and is consumed; a long hold goes to emergency as everywhere; it never appears on wake from blank; B65's first-tick
  blank seed is unaffected (1 s < 15 s). Boot faults remain on the console; `RESTART NEEDED` shows on Home after the
  splash.
- Not chosen: the mark alone; a splash drawn during start-up (it must follow the watchdog start, so the first 2 s
  stay dark anyway, its visible time is unmeasured, and it would move the panel bring-up inside a boot order that
  carries B200, B91 and the watchdog placement); no splash.

## 10. Earlier authorities: retained or revised

| Authority | Treatment in this design |
| --- | --- |
| Parent OLED design §1 "Out: free-form one-button entry of arbitrary text" | **Revised** by the owner's 2026-09-07 agreement for names and ordinary DM/team text only; RF numbers, OTA/GPS enable and timeout editing stay out |
| Parent §3.2 gesture contract: short walks a list and at its end moves to the next screen | **Revised** (owner, 2026-09-23): lists wrap; screens change only in menu mode |
| Parent §3.6.4 / UI-16 §10: no one-button character entry | **Revised** likewise; typed team-ID (Crockford) entry stays out |
| Parent §3.6.5: a draft survives, an unconfirmed destructive action does not | Applied: editor survives `long_fire`; review does not; review selection resets at blank and `long_arm` |
| UI-17 §1 navigation contract: screens land passive, short passes them, double enters | **Revised** (owner, 2026-09-23; D1): Home lands in list focus; the passive previews remain as menu mode behind `MENU` |
| UI-17 S1: a list's last row is BACK, returning to that screen's passive form, never to the next screen | **Revised**: the last row is `MENU`, returning to menu mode on the Home slot |
| UI-17 §2 STATUS body, 24×24 slot, S3/S6 | **Revised** (owner, 2026-09-23, D2): identity, team line, three-row list; unread and KNOWN counts in item labels; position to My device; mark to the splash. P14a/P14f/P17a re-pointed, never weakened |
| UI-17 R-1 / §3.3 retention | Retained everywhere, menu mode included (owner, 2026-09-23: the panel going dark never re-homes) |
| UI-17 S8: a push never navigates | Retained: neither a push nor a blank changes the screen |
| Parent §6.1 / B231: DM block then team block, newest-first within each; interleaving needs a reboot rule first | **Revised** (owner, 2026-09-23): one newest-first list; §6.8 states the reboot rule; the per-kind budget stays |
| UI-17 R-2 | Retained: no TEAM re-ordering |
| UI-17 R-3 | Retained on the Home body (only `RESTART NEEDED`); blocked setup shows Settings words on a SETTINGS-slot note (owner, 2026-09-23, D11) |
| UI-15 §3.6.3 / plan §4: PROVISION requires SAVE or DISCARD of an unsaved draft; conflict before unsaved; never saves on the operator's behalf | Retained for `JOIN`/`CREATE` from Home through the same function; `INVITE MEMBER` from Home opens without it because it changes no settings (D11); SETTINGS → PROVISION unchanged |
| UI-15 slice 4 plan §5: provisioning closes whenever SETTINGS is left | Retained: a flow started from Home runs in the SETTINGS sub-view and returns to Home through that close (D11) |
| UI-17 R-4 | Retained: the rail names the body in both focus states; menu mode adds the gutter bar |
| UI-17 R-5 | Retained: results acknowledged by either press; review is a confirmation with two actions |
| UI-17 R-6/R-7 | Retained: no new wake source, no rate limiter change; the Home card never holds the panel (D12) |
| B232 / §UI-14: the Settings menu is left by BACK or by walking off its end, to the closed view | **Revised**: its last row is `MENU` (menu mode on Home) and short wraps within the menu |
| UI-10/11 OQ-A (17 bytes, one row) | **Superseded** by the owner's 2026-09-06 correction; T per D7; the other catalog invariants stay |
| UI-10/11 P3: a double on a preset queues it | **Revised** (owner, 2026-09-23, D5): opens the review on `BACK` |
| UI-10/11 R-1: preset rows → K7 row → back | **Revised position** only: presets → `WRITE MESSAGE` → K7 → back |
| UI-10/11 pin 6: empty catalog = back row only | **Revised**: `WRITE MESSAGE` + the exit row |
| UI-10/11 R-2: the per-slot location switch is authoritative; the GPS supplies availability only | Retained; the review now shows the request (`LOC`, §7.3) |
| GPS design (2026-08-25) §4.5: every new on-device sender must be classified for location | **Applied** (owner, 2026-09-23, D6): written messages are not location-eligible. That design's ruling 8 and §4.5 still locate every phrase whenever a fix is fresh, contrary to R-2 (B446) |
| `mr_ui_init`: "No boot splash" (UI-5's existed only to prove the canvas) | **Revised** (owner, 2026-09-24, D3): a product splash — the mark and the build's Git ID, about 1 s after start-up |
| `ui_preset_blob_state`: "no migration arm" | **Retained** (owner, 2026-09-23, D8): an old v1 record boots as defaults with a distinct `old v1 record` line, at every boot until the first change |
| UI-16 R-1: JOIN TEAM opens NEARBY directly | Retained and reused from Home; an unnamed device first sees the name prompt, SKIP preselected (D10, §4.4) |
| UI-16 OQ-3: unfinished confirmation does not survive blank | Applied through the review's safe-selection reset |
| UI-16 R-13: a name describes, never identifies | Applied: DM review shows the full hash |
| UI-16 S-35 / F-15 invite row: `%-6.6s` name clip; no clipped `0x` form | **Revised** (D10): a name shortens with `»` on the invite and TEAM rows alike, one truncation per name; the clipped-hash ban is retained and extended to the TEAM row, where an unnamed member shows the S-13 fingerprint |
| `Node::effective_name`: an empty name is advertised as `MeshRoute node: 0x<HASH8>` | **Revised** (D10, §4.6): an empty name is advertised as empty; package W1c |
| Address-book design §2.3: `peername` labels a peer that advertises the default | Unnamed devices advertise no name (D10), so such a label survives; precedence over a named peer's own name stays open (B447) |
| B118 / B191 / B236 | Unchanged; the Home card has no actions of its own (D12); manual draft lifetime follows B236's bound |
| §B115: strings built in `firmware_ui.cpp` are ungated | Applied: every new string and format lives in a pure header with a native case |

## 11. Resources, profiles and compatibility

### 11.1 Resource model — estimates, host ABI; board figures to be measured

| Item | Lives in | Instances | Estimate | Note |
| --- | --- | --- | --- | --- |
| Own name | `UiSnapshot` | 1 static + 1 transient stack | ≈ +36 B each | 32 + length + padding |
| Navigation: menu-mode flag, rail index, Home item identity and list capture | `UiState` | 2 | ≈ +12 B each | frozen per frame |
| Home return item, setup origin | `UiModel` | 1 | ≈ +2 B | |
| Inbox boot boundary (newest sequence per kind, read at UI start) | UI inbox context | 1 | ≈ +8 B | |
| Inbox ordering key (`rx_time_ms` beside each staged row) | `InboxRowBudget` (one static instance) | 8 rows | ≈ +64 B static | the merge's full-precision key (§6.8); nothing added to `UiSnapshot` |
| Draft | `UiModel` | 1 | ≈ +170 B | `kDraftMax` 163 + fields |
| Editor window / review page | `UiState` | 2 | ≈ +48 / +64 B each | sharing a 3-row page with `detail_line` is the review package's choice (W6/W8); W3 keeps `detail_line` at 2 rows |
| `SendReq` | `UiModel::_req` | 1 | 8 → ≈ 20 B | |
| Command line | static | 1 | ≈ +200 B static (199 derived, §7.1), −96 B stack | today a 96-B stack local |
| Preset reply line | stack in `preset_emit_*` (console path) and `preset_boot_restore` (boot-diagnostic path) | one per active call | 160 → 244 B (+84 per widened buffer) | derived from the widest record (§7.7); peak stack measured at the brief, not inferred |
| Preset catalog v2 (T = 163, owner-ruled D7) | `PresetCatalog` (3 blobs) | 1 | **+7440 B** | the owner accepted this estimate with D7; the brief reports the measured figure against it, and a material excess returns to the owner |
| Compose rows | `UiSnapshot` | 1 + stack | ≈ +16–32 B | text stays 17 columns |
| `/mrui` record | NVS | 1 | 372 → **2852 B** | 20 KB partition; a mobile image holds ≈ 4.7 KB today (estimate) |
| Home card (D12) | `UiModel` + `UiState` | 1 + 2 | ≈ +40 + 2×30 B | first 19 bytes, session arrival serial, present flag, epoch, team, origin, time, count |
| Splash | `UiModel` | 1 | ≈ +5 B | the Git ID is the existing `kGitRevision` string, read, not copied |

**W4b allocation, owner-ruled 2026-09-27:** +72 B of static UI structures on the OLED boards (+64 on the host) for
the QA-measured full six-item Home capture ([W4b pre-check](../evidence/2026-09-27-standalone-mobile-home-w4b-precheck.md) §3): `UiState` 504→520 (six Home item
identities, their count, the selected item and the changed latch, plus the Home view; two instances), `UiModel`
912→936 on the boards (928→944 host; the return item and the setup origin), `UiSnapshot` 1336→1368 (the own name,
32 bytes plus length; one static and one per-tick stack copy), `UiChrome` unchanged (the menu cue fits its padding).
Any further retained state returns to the owner.

Without the catalog growth the static estimate stays near 1 KB. The stack changes on two separate paths, so no
neutrality is claimed: the UI send path loses its 96-B line (now static), and each widened preset buffer gains
84 B on the console and boot-diagnostic paths; the peaks are measured at the brief. With T = 163 about +8.4 KB static on the six OLED images (`heltec_mobile` last recorded at 211724 B). Board padding differs (`UiModel`
912 board vs 928 host; B246): every figure is re-derived with `tools/probe_board_abi.py` pins and a
`tools/measure_board.py` pair at each brief. **Apart from the catalog estimate the owner accepted with D7 and the
W4b structures the owner approved on 2026-09-27 (above), no allocation is granted by this document.**

### 11.2 Profiles

All six OLED envs are ESP32-S3 and compile the same UI (the mobile role is runtime). Gateways (`MR_FEAT_TEAM 0`)
get identity, My device, rename, the navigation model and the splash; their menu mode skips the empty Team and Send
slots and team actions are absent (UI-19 shape preserved). nRF52 and XIAO images have no OLED and are unaffected
except by shared `lib/core`/`src` fixes (W0, W1b, W1c; W1 touches only the OLED UI); with W1c an unnamed static node also advertises no name.

### 11.3 lib/core, wire, NV and corpus

No feature package needs `lib/core` or a wire change: names already ride the existing frames, admission caps are
existing constants, and the UI composes existing console forms. The core changes are separate packages, each with
the full corpus gate: B444's team-post guard and D10's no-default-name change (W1c, §4.6 — no wire change; predicted
corpus-inert because every simulated node is named). B241 is a `src`-only fix in the UI's label adapter (W1). `/mrui` v2 is an NV record version (not
wire, not `/mrcfg` v26). Feature packages confined to `src/` are predicted corpus-inert by construction; the
prediction is re-checked at each gate, never assumed.

## 12. Decisions for the owner

| # | Decision | Recommendation | Alternative | Consequence / cost |
| --- | --- | --- | --- | --- |
| D1 | Navigation | **Resolved — owner direction 2026-09-23:** Home by default in list focus; `MENU` on every top-level list opens menu mode on the Home slot; refinements 1–5; the panel going dark never re-homes (§6.1) | — | Revises UI-17 §1/S1, the parent §3.2 gesture contract and B232's Settings exits; navigation tests, probe navigation checks and metal UI-01 re-pinned |
| D2 | Home rows and lists | **Resolved — owner 2026-09-23 (as recommended):** mark removed; identity, team line, three-row list, `RESTART NEEDED` line; counts in labels; the position moves to My device; lists per state (§6.3). **D2a** `INBOX` first · **D2b** `INVITE MEMBER` on Home for key holders · **D2c** Settings via `MENU` | Not chosen: keep the mark (rows 0–2 at 14 columns); `SEND TO TEAM` first; invite only in Settings; Settings on Home | Revises UI-17 S3/S6 and the Home probe pins; gesture counts in §6.3 |
| D3 | Splash | **Resolved — owner 2026-09-24 (A with the Git ID line):** boot-only ≈ 1 s after start-up, nonblocking, dismissible with the press consumed, never on wake; the mark centred with the build's Git ID under it | Not chosen: the mark alone; a splash drawn during start-up; no splash | +≈5 B; separately attributable package; shows which build a device runs without a USB console |
| D4 | Editor alphabet | **Resolved — owner 2026-09-23 (as proposed):** uppercase letters, digits, space and `. , ? ! -` in seven groups of six; controls under `EDIT`; return to group 1 after each character | Not chosen: return to the group just used (7.93 vs 7.50 gestures per character on language-neutral text); six groups of seven (tie, digits over three groups); lowercase (a case switch, about 12 gestures each); other marks | Gesture counts in §5.2 and audit §4; `RETURN TO BASE NOW` 126 gestures, deleting the last character 9 |
| D5 | Review | **Resolved — owner 2026-09-23 (option C everywhere):** one review for phrases, written text and names; a double on a phrase opens it; the safe action (`BACK` / `EDIT`) is preselected | Not chosen: phrases send on the double (A); review opening on SEND (B) | +2 gestures per phrase send; only double, short, double on a review can send; revises UI-10/11 P3 and the UI-15/UI-17 metal expectations |
| D6 | Written messages | **Resolved — owner 2026-09-23 (B):** one limit, 163 bytes, for every message the panel sends — phrase or written, team or DM; written messages carry no location (not location-eligible under the GPS design's §4.5); the review shows `LOC` for a located phrase | Not chosen: A — team 173 / DM 214 without location; C — 163 with a per-message location switch now | One number everywhere, the same as D7; location can never make a written message too long; gives up 10 team and 51 DM bytes nobody types with one button; ≈ 90 B less RAM than A (draft and line buffer); a later GPS package can add a location switch without changing the limit |
| D7 | Preset size | **Resolved — owner 2026-09-23: T = 163** per slot (smallest location-on admission; long instructions can be prepared over USB) | Not chosen: T = 64 | ≈ +7440 B RAM on the six OLED images (estimate accepted, to be measured); `/mrui` 2852 B |
| D8 | `/mrui` version change | **Resolved — owner 2026-09-23 (B):** no upgrade reader; an old v1 record boots as defaults with a distinct message at every boot until the first change; custom phrases re-entered over USB | Not chosen: converting the v1 record (A) | Keeps the in-source "no migration" rule and the project's reset-on-change practice; MeshRoute is not deployed, so no backward compatibility is needed |
| D9 | Default phrases | **Resolved — owner 2026-09-23:** keep the five; add team `Return to base now`, `On my way` and personal `Where are you?`, location off | Not chosen: `Return to base now` + `End of shift`; no change | Applies to absent, invalid and old-format stores; each added phrase lengthens its list by one row |
| D10 | Names | **Resolved — owner 2026-09-23 (as recommended):** an unnamed device is shown by its ID — `0x<HASH8>` where 10 columns fit, otherwise the six-digit member fingerprint, never a clipped hash; unnamed devices advertise no name (core package W1c); names up to 32 bytes, shortened with `»` (the invite row too); rename starts from the current name when the editor can type it; a name prompt before JOIN/CREATE only when unnamed, SKIP preselected | Not chosen: keep advertising the default and recognise it on the panel; a 16-byte panel limit; always start empty; no prompt | TEAM rows stop showing `MeshRo` / `0x12ab`; 26 bytes less on INTRO and key answers and 27 on key requests from an unnamed device; revises UI-16 R-1 (unnamed devices only) and S-35's clip; W1c closes B447's unnamed half |
| D11 | Setup from Home | **Resolved — owner 2026-09-23 (as proposed):** `JOIN`/`CREATE` pass the same settings gate, factored once; `INVITE MEMBER` opens without it (changes no settings); an explicit `home`/`settings` origin returns exits to Home (unknown → the PROVISION menu); a blocked gate shows a SETTINGS-slot note; the gate runs before the name prompt | Not chosen: hide `JOIN`/`CREATE` while blocked; gate `INVITE` too (r2–r2.10) | Keeps R-3's Home body rule; no settings text on Home; SETTINGS → PROVISION unchanged |
| D12 | Preview | **Resolved — owner 2026-09-24 (D):** a display-only card — the newest unread sealed team post in Home's rows 1–2, no rows added, cleared by the existing unread rule, a team change or an epoch change; no DM preview | Not chosen: the r2 card with `OPEN`/`HIDE` (absent when a post wakes a panel already on Home); deferring; dropping | ≈ 100 B RAM; no wake, read or navigation change |
| D13 | Inbox order | **Resolved — owner 2026-09-23:** one newest-first list across DMs and team posts (§6.8). **D13b resolved — owner 2026-09-23 (the lighter rule):** this session's messages merged by receive time; earlier ones below, each kind newest-first, ages `--` | Not chosen: a persistent arrival serial in every Inbox record, exact across restarts | `src`-only — ≈ 8 B for the boot boundary plus an estimated 64 B payload of full-precision ordering keys (§11.1), before padding; existing accessors — and closes B445; the accepted residue is the DM-versus-team order among rows from before the last restart. The rejected alternative needed a `lib/core` Inbox change plus a store-format migration on both backends and still could not show pre-restart ages |

## 13. Proposed implementation packages (for QA briefs — not a frozen slice list)

| # | Kind | Content | Depends | Gate obligation |
| --- | --- | --- | --- | --- |
| W0 | fix, `src` | B440: one live→`/mrid` conversion for `cfg set name/lat/lon` (and `regen`); B448: a name over 32 bytes is refused (`too_long`), never shortened | — | native (the conversion as a pure helper; 32 accepted, 33 refused), corpus, 2 boards, touched batteries; QA names the probe that drives `cfg set` |
| W1 | fix, `src` | B241: `label_from_hash` terminates at the copied length (one byte reserved; the `0x%08lx` fallback and zero capacity handled); `peer_name_find` keeps its raw full-32-byte contract for the push body and `/mrpeers` persistence ([pre-check](../evidence/2026-09-24-standalone-mobile-home-w1-precheck.md)) | — | `src`-only gate (P6): native (plus an exact-capacity guard on the raw API), corpus (predicted 36/36 identical), the two board envs, the firmware-UI probe driving the real adapter with poisoned destinations and assertion-failing controls **Status 2026-09-25: INDEPENDENT QA PASS; B241 closed**, owner commit `8360802`: native 2951/195777/0; corpus 36/36 identical; firmware-UI 467/902/467 with 225 verified controls / 0 unusable; gateway unchanged, mobile RAM unchanged / flash +28 B. [QA receipt](../evidence/2026-09-25-standalone-mobile-home-w1-qa.md). Product decisions and the remaining packages are unchanged. |
| W1b | fix, `lib/core` | B444: refuse a `-t` post while the team-local ID is 0 (or the owner rules the intended pre-DAD behaviour) | — | full gate; independent of the Home work, which hides SEND TO TEAM regardless |
| W1c | change, `lib/core` | D10 (§4.6): unnamed devices advertise no name; `whoami` prints `name=""`; the default-name test and comments rewritten | — | full gate: native from a fresh peer cache (exact INTRO, key-answer and key-request bytes from an unnamed node — 26, 26 and 27 bytes shorter; a cached name survives an empty one), corpus keystone per `simulation/BASELINE.md` (predicted unchanged), boards; every `effective_name` user grepped (P7). **Status 2026-09-25:** **INDEPENDENT SOFTWARE QA PASS**, uncommitted on `8360802`. [Receipt](../evidence/2026-09-25-standalone-mobile-home-w1c-qa.md): native 2962/195904/0; 36/36 corpus identical; ABI/RAM unchanged; flash −304/−140 B; 34/34 mutations RED. B447 unnamed half closed; named-peer precedence and B450 remain open. |
| W2 | tool | B418: re-anchor W49/W51/W54 and their controls; rerun the supplemental probe | — | tools discovery; all board-UI controls RED |
| W3 | refactor, `src` (C1) | a pure fixed-byte pager extracted from the detail modal — the page count and one page's row slices, with rows and columns as parameters (the review's word wrap, §7.2, stays with W6/W8); the compose-row display width derived from the body width minus its two marker columns, no longer from the record limit `kUiPresetTextMax` (unchanged; W6 raises it); one provisioning admission function sharing the arrival opener (no Home caller yet). No new state, layout or behaviour ([pre-check](../evidence/2026-09-25-standalone-mobile-home-w3-precheck.md)) | — | byte-identical renders in `probe_firmware_ui`, proved by real-render fixtures added before the extraction (detail bodies of 0/38/39/76/241 bytes, 17-byte compose rows, the blocked PROVISION notes); native pager, display-versus-record and counted-opening cases; batteries re-anchored with working-rule D6 care, never weakened; corpus; the two board envs. **Status 2026-09-25: INDEPENDENT SOFTWARE QA PASS**, uncommitted on `8360802` plus W1c. [QA receipt](../evidence/2026-09-25-standalone-mobile-home-w3-qa.md); the [approved brief](../plans/2026-09-25-standalone-mobile-home-w3-ui-model-seams.md) and coder freeze remain unchanged. No resident-state growth or render change in the characterized cases; W1/W1c/W3 prerequisites for W4a are satisfied. |
| W4a | fix, `src` | B441 + the identity formatter for every device label (TEAM, compose, review, result and invite rows), including D10's unnamed rule, at the §4.1 budgets; B449 (the invite probe control O8) and B455 (O6) with it | W1, W1c, W3 | native, `chrome`/`model` batteries, firmware-UI probe; **Status 2026-09-27: INDEPENDENT SOFTWARE QA PASS**, uncommitted on `8360802` plus W1c/W3. [QA receipt](../evidence/2026-09-27-standalone-mobile-home-w4a-qa.md): B441/B449/B455 closed; current device labels use the §4.1 budgets with zero resident growth. Own Home/My device labels remain W4b, review labels W6/W8. [Metal UI-20](../../2026-09-20-metal-test-plan.md#ui-20) OWED. B456 records the runner’s separate missing-control false-PASS defect; this gate independently accounts for all 236 controls. The [approved brief](../plans/2026-09-26-standalone-mobile-home-w4a-identity-labels.md) and coder freeze stay unchanged. W4b prerequisites satisfied; next is its QA pre-check and author brief. |
| W4b | feature, `src` | Home and navigation, paired under P6: menu mode, `MENU` rows on the five top-level lists, the gutter bar, Home rows/lists/arrow rules, My device (read-only), setup origin, mark removal; B456 (the firmware-UI probe's missing-control false PASS) as its instrument-first stage; allocation owner-ruled (§11.1) | W4a, W3 | native navigation matrix, `model`/`chrome`/`uistatus`, navigation tests and probe checks re-pinned (never weakened), ABI pins, RAM pair; metal UI-01. **Status 2026-09-27:** QA [pre-check](../evidence/2026-09-27-standalone-mobile-home-w4b-precheck.md) complete; the [brief](../plans/2026-09-27-standalone-mobile-home-w4b-home-navigation.md) awaits QA review. |
| W4c | fix, `src` | B445: the Inbox boot boundary (`dm_newest_seq()`/`chan_newest_seq()` read once at UI start); rows from before the restart show age `--` | — | native (both sides of the stamp), `model` battery, firmware-UI probe |
| W4d | feature, `src` | Inbox newest-first merge (§6.8) over the unchanged per-kind budget, keyed on the full receive time kept beside each staged row; the identity cursor and the after-delete neighbour follow the displayed order | W4c | native (merge, ties, restart split, sub-second order), `model` battery (B231's M92/M93 re-anchored, never weakened), firmware-UI probe; metal UI-02 |
| W5 | feature, `src` | boot splash: the mark and the build's Git ID (D3) | W4b | native (the pure line formatter, including an over-long ID), probe render, POWER metal |
| W6 | feature, `src` + NV | `/mrui` v2 (an old v1 record: defaults and a distinct message at every boot until the first change), T, validation, `text_max` beside the unchanged slot `capacity` and the companion contract's `ui preset` section, the reply line `kPresetLineMax` and its four users (three console emitters and `preset_boot_restore`), row projection with `»`, review for phrases (with `LOC` when located), team-binding gate, static line buffer, defaults | W2, W3, W4b | native, `uipresets`/`uipresetverbs`/`devicenv`/`uisend`/`model` (full 163-byte `set` and `list` replies through `PresetPrintLines`, with a control restoring the 160-byte line), W54 green, probe exact send lines, ABI, RAM pair; closes B335 with QA |
| W7 | feature, `src` | editor + rename (CHANGE NAME, name prompt; the name origins `my_device`/`setup_join`/`setup_create`, with the settings gate re-asked on continuing into setup) | W0, W4b, W6 | new `uieditor` target, `model`, probe |
| W8 | feature, `src` | written DM/team messages: WRITE MESSAGE rows, locked draft (163 bytes), new kinds and kind-scoped gates, the §7.4.1 caller table, never `-l` (D6) | W6, W7 (candidate pairing under P6) | `uisend`/`model`/`uieditor`, probe exact lines, ABI, RAM pair |
| W9 | feature, `src` | the Home card of the newest unread team post (§8, D12) | W4b | native (eligibility matrix, newest wins with `+n`, clearing on read, team and epoch change, header-only under `RESTART NEEDED`, list items and the arrow's item unchanged), `model`, probe render, UI-14/POWER metal |

Rules carried into every brief: C1 (W3 separate), C4 (no wire change anywhere), P6 (a `src`-only package's
independent gate = native + corpus + boards + touched batteries + affected probes), P7 (grep every user of a
changed or removed symbol — `kUiPresetTextMax`, `ComposeSlot`, `SendReq`, `SendKind`, `effective_name`, the navigation helpers
`advance_or_next`/`next_screen`/`list_follow_screen`, the Inbox row publisher `InboxRowBudget::publish` — across
`src`, `test` and `tools`, including mutation patterns), working rules D5/D6 from `AGENTS.md` (strict pin readers, comment-only returns — not this design's decisions), and the two-env board gate. B286's staging workaround
must preserve every tracked and untracked input and deletion.

## 14. Acceptance matrix

| Contract | Existing proof | New automated proof | Metal residue |
| --- | --- | --- | --- |
| Input boundaries | `test_firmware_ui_input.cpp` (short/double/long, wrap, sparse polling) | editor consumes classified gestures only; a probe feeds raw samples through `InputFsm` into the editor | EDIT-01 |
| Navigation | ui17 navigation cases (`test_firmware_ui_model.cpp`), chrome-nav cases, probe P14 series | boot → Home list focus; `MENU` on each top-level list → menu mode on Home; menu-mode short/double over the slots (team slots skipped on gateways); previews; gutter-bar render; sub-view exits unchanged; Settings menu wraps; a blank never changes the screen, focus or arrow (menu mode included); no navigation on a push; emergency from both states; unread watermarks unchanged | UI-01, UI-14, UI-19 |
| Repertoire and rings | — | exact 42 bytes, 7 × 6, each once, fixed order; ring sizes; return-to-group-1; mutation per rule | — |
| Editor transitions | — | every state × gesture; wrap; `BACK`; cursor bounds; `DEL` at 0; `FULL` at cap and cap+1 refused; `EMPTY`/all-space `DONE` refused; discard confirm; zero writes and zero sends on cancel | EDIT-01 |
| Interruption | UI-17 R-1/S8 cases (`test_firmware_ui_model.cpp` ui17-*) | blank/wake in every editor and review state; receive without navigation; `long_arm`/`cancel`/`fire` per §5.5; no request ever queued by overlay dismissal | UI-16/UI-17 |
| Name save | none native: the arm lives in the device TU `firmware_config.cpp` (no battery target) | exact bytes, 32-byte max, fake-NV failure keeps the live name, seed/position/membership preserved (W0), identical save = 0 writes; each origin's returns (My device, or on into setup with the gate re-asked; a refusing gate's note → Home); console `cfg set name` of 33 bytes refused (B448) | UI-13 |
| Identity labels | label cases (`test_firmware_ui_team.cpp`), invite-row cases (S-35) | sanitisation; unnamed → `0x<HASH8>` at ≥ 10 columns and the six-digit fingerprint below, never a clipped hash; the name part left out where the row carries the hash; `»` on names only, identical on TEAM and invite rows; B241: the real label adapter terminates at the copied length with a poisoned, canary-guarded destination (firmware-UI probe, W1) | UI-13 |
| No default name (W1c) | the §1.3 `effective_name` case (`test_node_r3.cpp`) | an unnamed node's INTRO, key answer and key request carry an empty name (exact bytes); a receiver keeps its cached name on an empty one; `whoami` prints `name=""`; corpus keystone reproduces | UI-13 |
| Home rows and lists | `test_firmware_ui_status.cpp`, P14a/P14f/P17a | each state's list and order; key-before-ID precedence; conditional items; counts in labels; arrow landing and return rules; identity tracking; `OPTIONS CHANGED` refusal; `RESTART NEEDED` line; return to Home from every setup exit (unknown origin → PROVISION menu); `JOIN`/`CREATE` refused by an unsaved or conflicted draft with zero writes and the note, `INVITE MEMBER` opening with the same draft; the gate before the name prompt; name prompt only when unnamed, SKIP preselected, `NAME SAVED` continuing into setup and an unsaved exit returning to the prompt; blocked-gate note; gateway arm; real renderer | UI-01, UI-04, UI-06, UI-19 |
| Inbox order | B231 per-kind newest-first cases (mutations M92/M93); the §B64 cursor cases | one list newest-first across kinds for this session's rows; rows under one second apart in both kind orders (never ordered by the published age); genuinely equal times; receive times above 2^32 ms; rows from before the restart below, each kind newest-first, ages `--` on both sides of the stamp (B445); per-kind budget of four unchanged; cursor follows `(kind, seq)` through reordering; after-delete neighbour in displayed order; Home `INBOX` lands on the newest | UI-02 |
| Multiline and send | ui7-line exact strings (`test_firmware_ui_send.cpp`) | 17/18 and cap/cap+1 (163/164) for team, DM and phrases — one limit (D6/D7); wrap concatenation property; long words; page count (≤ 6); page change sends nothing; reviewed bytes = composed quoted body; location variants for phrases, with `LOC` on the review exactly when the slot requests it; a written message's composed line never contains `-l`; a DM review header with a long label fits 19 columns | UI-15, UI-12 |
| Context and lifetime | P3 generation cases, `PRESET CHANGED` | team change, recipient hash change, duplicate names (review shows hash), locked draft drained after the editor closes, ordinary path never touches `/mrui` or the emergency tracker; every §7.4.1 row for each caller and request state — gates scoped by kind (a phrase request is never refused for lacking a draft), the saved-phrase return to its list, presses ignored while a written request is still queued, withdrawal of a queued written message at `long_fire` with the emergency request untouched, the known-refused `long_fire` path (draft kept for a fresh review, nothing re-submitted), the accepted-open residual, the typed retain/release classification, and both name origins | UI-15 |
| Catalog version 2 | P1 four-state cases | a v1 fixture boots as compiled defaults with the distinct old-record line and zero writes; fresh → exactly the eight defaults of §7.7; invalid/truncated/`io_failed` unchanged; first change writes canonical v2; save failure keeps live; emergency slot rules; `capacity` 17 and `text_max` 163 asserted independently; full 163-byte `set` and `list` replies through the real output path and the boot diagnostic with a full v2 record, with a control restoring the 160-byte line | NV-06, UI-12 |
| Home card (D12) | wake cases | eligibility matrix (sealed, same team, team channel, nonzero seq; cleartext, foreign, other-channel and internal never); no card from history after boot; newest wins and `+n` capped `9+`; `T<n>` kept after ID reuse; clears when the read watermark reaches the card's arrival serial, on a team change and on an epoch change — including a restored store whose sequences start above zero (never compared with the record sequence), a sequence gap after a delete, clearing by the Inbox's menu-mode preview, an arrival while an Inbox frame pages out (the card stays) and the arrival serial wrapping at 2^32; header-only under `RESTART NEEDED`; list items and the arrow's item unchanged when it appears; widest header `FROM T254 59m +9+`, 17 columns; first 19 bytes with `»` | UI-14, POWER-02 |
| Splash | — | boot only, deadline, dismiss consumed, long hold → emergency, never on wake, then Home in list focus; the ID line equals `kGitRevision`, centred, `»` past 19 columns | UI-01, POWER-01 |
| Production reachability | `probe_firmware_ui` drives the real `firmware_ui.cpp` (asserts no composed line today) | the probe asserts exact composed lines through the real composer into the faked executor; Home, menu mode, editor and review renders | UI-15 |
| Resources | ABI pins for 29 structs | re-pin changed structs; add the draft type; RAM/flash pair on the ruled envs | — |

Controls must compile, match their intended boundary once and fail for the intended reason; VACUOUS or unusable
controls are not counted RED. Changed-source and dependency selectors are derived separately and their union is
gated where the brief requires arc coverage.

## 15. Physical obligations — pending, mapped to the current plan

Nothing here is added to the [metal plan](../../2026-09-20-metal-test-plan.md) until an implementation contract
is approved; each brief then freezes the exact panel and console strings.

| Scenario | New observation |
| --- | --- |
| UI-01 | Home rows and `»` legible; an uncoached wearer finds `MENU` and moves between screens (replaces the cycle step); the gutter bar is visible; a blank keeps the screen, focus and arrow; splash timing, dismissal and coexistence with radio service; the splash's Git ID matches the `version` command |
| UI-02 | one newest-first Inbox across DMs and team posts; after a restart, earlier messages sit below this session's with `--` ages |
| UI-04 / UI-06 | create and join started from Home; an unnamed device sees the name prompt first; every exit returns to Home; an acknowledged result is not undone |
| UI-12 | 163-byte phrases over USB without `CONSOLE_DROP`, and their full `ui_preset` replies; a device carrying a custom v1 record boots on defaults with the old-record line at every boot until a phrase is re-entered, and re-entered phrases persist across reboot |
| UI-13 | own and peer labels; an unnamed teammate as its six-digit fingerprint on the TEAM row and `0x<HASH8>` where it fits; a non-ASCII name as `.` cells |
| UI-14 | wake behaviour unchanged; a message wake lights the current screen, menu mode included; on Home a waking team post's first line is readable without a press, and the card clears once an Inbox frame — the list or its menu-mode preview — has been shown after the post arrived |
| UI-15 | phrase review then send, `LOC` shown for a located phrase; written team and DM messages arrive byte-exact and without location; refusal returns to the editor |
| UI-16 / UI-17 | emergency while editing, reviewing and in menu mode: draft survives, review closes, nothing sends later |
| UI-19 | gateway Home: identity, My device and a menu mode that skips Team and Send |
| NV-06 | power cuts during v2 writes, including the first write over an old v1 record; NVS free-entry reading |
| POWER-01 / 02 | lit time and sleep during long typing sessions and chatty receive |
| EDIT-01 (new, with the W7 brief) | an uncoached wearer enters a name and `RETURN TO BASE NOW` with one correction (the D9 saved default stays mixed case); record gestures, time, accidental doubles and emergency arms |

## 16. Revision history and preparation record

**r1, 2026-09-06/07 (Codex) [HISTORY]:** captured the owner's direction, the 17-byte correction, the shared-editor
agreement, a seven-by-six candidate and HOME-A1/A2 intake. Its source table was read at `a0ff994`/`1677b44`
beside an active remote-admin coder; those snapshots and that coordination note are historical — remote-admin
software is complete and its residue is metal-only. r1's Home wireframes (`ME: STAN`, `TEAM 12A1B2C3`,
`> INBOX (3 NEW)`) and the return-to-last-group editor rule are superseded by §6 and §5.2.

**r2, 2026-09-22 (Claude, specification author):** re-verified every source claim at `2035757` (audit linked
above); added the capacity derivation, editor state and priority tables, Home states and navigation, review and
submission contracts, the `/mrui` upgrade table, the resource model, the authority map, twelve owner decisions,
implementation packages, the acceptance matrix and the pending physical mapping. Registered B440–B443 and added
dated notes to B241, B418 and B286; B335 stays open. Ran only two disposable host `sizeof` programs and a
press-count model; no build, test, probe, battery, simulator, board or hardware run; no production, test, tool
or metal-plan edit; nothing staged or committed.

**r2.1, 2026-09-23 (Claude, at the owner's request):** folded in the owner's navigation direction — Home by default
in list focus, `MENU` on every top-level list opening menu mode on the rail at the Home slot, refinements 1–6 —
resolving D1 [HISTORY: r2 recommended keeping UI-17's passive Home with a double to enter its actions]. Rewrote §6
(navigation model, Home rows, per-state lists with D2a–D2c, arrow rules, what each item opens) and carried the
consequences into §3, §7.5 (the Send list now ends with `MENU`), §8 (preview placement), §9–§15. Registered B444
(team post before team-DAD) and added package W1b. Still documentation only: nothing built, run, staged or
committed.

**r2.2, 2026-09-23 (Claude, owner corrections):** withdrew refinement 6 — the panel going dark never re-homes;
screen, focus and arrow stay [HISTORY: r2.1 moved menu mode to Home at the blank transition]. Added §6.8: one
newest-first Inbox across DMs and team posts, with the restart rule the parent spec required (this session's rows
by receive time; earlier rows below, ages `--`); D13 recorded as resolved with D13b open; packages W4c/W4d;
registered B445 (fabricated ages across a restart) and noted the ruling on closed B231. Documentation only.

**r2.3, 2026-09-23:** the boot boundary now reads the Inbox's existing `dm_newest_seq()`/`chan_newest_seq()` at UI
start, exact and scan-free [HISTORY: r2.2 said "at the first Inbox read after boot", which could misfile a
message stored in the first loop pass]. The owner ruled D13b: the lighter restart rule, residue accepted.

**r2.4, 2026-09-23:** the owner ruled D5 — option C everywhere: every phrase, written message and name goes
through the review, which opens on the safe action. §7.3 and D5 are now agreed. Documentation only.

**r2.5, 2026-09-23:** the owner ruled D7 — saved phrases hold up to 163 bytes; the ≈ +7.4 KB RAM estimate is
accepted with the ruling and will be measured at the brief. Documentation only.

**r2.6, 2026-09-23:** the owner ruled D8 — no upgrade reader: an old v1 phrase record boots as defaults with a
distinct message and custom phrases are re-entered over USB — and restated that MeshRoute is not deployed and
needs no backward compatibility [HISTORY: r2–r2.5 recommended converting the v1 record]. §7.8 rewritten; §7.7,
§10 and §12–§15 follow. Documentation only.

**r2.7, 2026-09-23:** the owner ruled D9 — the default phrases keep the existing five and add team `Return to base
now`, `On my way` and personal `Where are you?`, all location off [HISTORY: r2 proposed `Return to base now` +
`End of shift`]. Documentation only.

**r2.8, 2026-09-23:** the owner ruled D6 — one 163-byte limit for every message the panel sends, and no location on
written messages [HISTORY: r2 recommended team 173 / DM 214 without location]. The source check before the ruling
found that the GPS design requires every new on-device sender to be classified for location, and that its text
still contradicts the owner's ruling R-2 on phrases (registered B446). Consequences: §5.3/§5.6 caps, §7.1 caps and
the re-derived line buffer (199 bytes), §7.2 page bound (≤ 6), `LOC` on a located phrase's review (§7.3), the
classification (§7.4), §10–§15. Two corrections to this draft: the DM review header's label is capped at 7 columns
(8 made the row 20 wide), and W4b no longer lists a "blank-time return to Home", a leftover of the withdrawn
refinement 6. Documentation only.

**r2.9, 2026-09-23:** the owner ruled D10 as recommended. §4 is agreed: an unnamed device is shown by its ID —
`0x<HASH8>` where 10 columns fit, otherwise the six-digit member fingerprint — and unnamed devices advertise no name
(new §4.6, core package W1c) [HISTORY: r2–r2.8 kept the advertised default and had the formatter recognise it,
which would have drawn a clipped `0x12A»` on the six-column TEAM row, the form UI-16 bans]; names up to 32 bytes;
rename preload; the name prompt with SKIP preselected [HISTORY: the r2 wireframe put the arrow on SET NAME while
its text said SKIP]. The source check registered B447 (a `peername` label is overwritten by the peer's next
advertised name) and B448 (`cfg set name` silently shortens), and added the `MeshRo` / `0x12ab` TEAM-row fact to
B441. §1, §2, §10, §11 and §13–§15 follow. Documentation only.

**r2.10, 2026-09-23:** the owner ruled D2 as recommended — the Home rows and per-state lists of §6.2–§6.3, `INBOX`
first (D2a), `INVITE MEMBER` on Home for key holders (D2b), Settings through `MENU` (D2c); the own position moves to
My device. The arrow-rule details (§6.4) and what each item opens (§6.5) stay proposals for the independent
review. Documentation only.

**r2.11, 2026-09-23:** the owner ruled D11 as proposed — `JOIN TEAM` and `CREATE TEAM` from Home pass the same
settings gate (factored once), `INVITE MEMBER` opens without it because it changes no settings [HISTORY: r2–r2.10
gated `INVITE` too], exits return to the recorded origin, a blocked gate shows a SETTINGS-slot note, and the gate
runs before the name prompt. §4.4, §6.5, §6.6, §10, §12 and §14 follow. Documentation only.

**r2.12, 2026-09-23:** the owner ruled D4 as proposed — uppercase letters, digits, space and `. , ? ! -` in seven
groups of six, controls under `EDIT`, return to group 1 after each character. The press-count model was re-derived
with identical results; two statements were corrected [HISTORY: r2–r2.11 said return-to-last-group was worse "on
every sample" — true only for the audit's four — and that six groups of seven "splits the digits", which seven
groups of six also do, over two groups instead of three]. Added the language-neutral figures, the shorts/doubles
split and the deletion cost. Documentation only.

**r2.13, 2026-09-24:** the owner ruled D3 — option A with the Git ID line: the mark centred for about 1 s after
start-up with the build's Git ID (`kGitRevision`) under it, dismissible with the press consumed, never on wake.
§9 now states today's dark boot and the ruling; §10–§15 follow. Documentation only.

**r2.14, 2026-09-24:** the owner ruled D12 — a display-only Home card: while this session's newest sealed team
post is unread, Home's rows 1–2 show `FROM T<n> <age> +<n>` and its first 19 bytes, with no rows added and the
existing unread rule clearing it [HISTORY: r2 proposed a card with `OPEN`/`HIDE` rows, shown only when Home's list
was rebuilt]. §8 rewritten; §1.2, §6.2, §6.3, §10–§15 follow. Every decision in §12 is now ruled; the design goes
to independent review with its remaining [PROPOSED] sections (§6.4 details, §6.5, §7.2, §7.4, §7.5). Documentation
only.

**r2.15, 2026-09-24 — review fold-in.** The [independent review](../evidence/2026-09-24-standalone-mobile-home-design-review.md)
returned HOLD with eight findings. Each was re-checked against the source before folding in; none is disputed and
none reopens an owner ruling.

| Finding | Disposition | Where |
| --- | --- | --- |
| DR-1 (BLOCKER) card clearing compared the Inbox sequence with a session watermark | **Fixed:** the card binds to its session arrival serial and clears by modular comparison with `read_ch`; §6.1 rule 10 now states the existing read rule exactly, the menu-mode Inbox preview included [HISTORY: r2.14 compared the record sequence and described the rule as "a completed, visible Inbox list frame"] | §6.1, §8, §14, §15 |
| DR-2 (MAJOR) `capacity` confused with the text bound | **Fixed:** `capacity` stays the 17-slot count; a separate `text_max` field; the companion contract section joins W6 | §7.7, §13, §14 |
| DR-3 (MAJOR) preset reply buffer too small | **Fixed:** `kPresetLineMax` re-derived (244 with NUL), its three emitters fenced, the stack cost priced, no stack neutrality claimed | §7.7, §11.1, §13, §14 |
| DR-4 (MAJOR) one review without caller-specific transitions | **Fixed:** the §7.4.1 caller table; gates scoped by kind; the content lock separated from result ownership; an unexecuted written message is withdrawn at `long_fire` | §5.5, §5.6, §7.4, §13, §14 |
| DR-5 (MAJOR) merge had no full-precision key | **Fixed:** `rx_time_ms` travels beside each staged row to the merge; +64 B static priced | §6.8, §11.1, §13, §14 |
| DR-6 (MINOR) "boots on the defaults once" | **Fixed:** defaults and the diagnostic at every boot until the first change; still zero boot writes | §1, §7.8, §10, §12, §13, §15 |
| DR-7 (MINOR) rename propagation too broad; old default names persist | **Fixed:** propagation qualified for unpinned rows; the fresh-peer-cache precondition stated | §4.3, §4.6, §13 |
| DR-8 (MINOR) three exact examples | **Fixed:** 26 bytes on INTRO/answer, 27 on the key request; the card header's widest form is 17 columns; EDIT-01 types `RETURN TO BASE NOW` | §4.6, §8, §12, §14, §15 |

Documentation only.

**r2.16, 2026-09-24 — re-review fold-in.** The [scoped re-review](../evidence/2026-09-24-standalone-mobile-home-design-rereview.md)
closed DR-1–DR-3 and DR-5–DR-8, kept DR-4 open and added three minor findings. Each was re-checked against the
source; none is disputed.

| Finding | Disposition | Where |
| --- | --- | --- |
| DR-4 (MAJOR), remaining | **Fixed:** the name editor returns by a typed origin, and a setup origin continues into setup with the settings gate re-asked; explicit request states (queued, known refused, accepted open, accepted final) give the drained-and-refused request its own `long_fire` transition and separate a known refusal (draft kept) from the accepted-open residual (draft released, nothing claimed); the retain predicate is typed, and `send_failed` alone is not proof [HISTORY: r2.15 sent every `NAME SAVED` to My device and released the draft of every executed request at `long_fire`] | §4.3, §4.4, §5.5, §7.4.1, §13 W7, §14 |
| DR-9 (MINOR) `bad_text` narrowed to length | **Fixed:** only the overlength arm uses `text_max`; the other causes stay | §7.7 |
| DR-10 (MINOR) fourth `kPresetLineMax` user | **Fixed:** `preset_boot_restore` joins the fence and the stack accounting; +84 B is per widened buffer, peaks measured [HISTORY: r2.15's DR-3 disposition said three emitters] | §7.7, §11.1, §13, §14 |
| DR-11 (MINOR) summaries lagging their bodies | **Fixed:** §4.6's stale-name sentence qualified; D13's cost separates the +8 B boundary from the ~64 B keys; the card's field is its session arrival serial, with a present flag (serial 0 is valid after a wrap) | §4.6, §8, §11.1, §12 |

Documentation only.

**r2.17, 2026-09-24 — review verdict.** The [second re-review](../evidence/2026-09-24-standalone-mobile-home-design-rereview-2.md)
closed DR-4 and DR-9–DR-11 and returned **PASS with fold-ins**, recording the owner's confirmation of the choices
disclosed in r2.16 (a press while a written request is queued is ignored; a name saved from the setup prompt and
interrupted by an alarm lands on Home; the accepted-open residual). Its one fold-in, DR-12 (MINOR), is **fixed**:
`SENT, waiting` moves from *Accepted, final* to *Accepted, open*, *Known refused* now starts from "known not to have
aired", and the late-ACK upgrade of `NO CONFIRM` is stated — no retain/release rule, owner decision or tracker
behaviour changes (§7.4.1). The status line now records the verdict. Documentation only.

**r2.18, 2026-09-24 — W1 re-scoped by its QA pre-check.** The [W1 pre-check](../evidence/2026-09-24-standalone-mobile-home-w1-precheck.md)
found that `peer_name_find`'s full 32-byte count is payload for two raw consumers (the push body and `/mrpeers`
persistence), so terminating inside it would clip a 32-byte name. B241 is fixed instead in `label_from_hash`, the one
C-string adapter — the register's listed alternative. The package becomes `src`-only and corpus-inert by construction
[HISTORY: r2–r2.17 planned W1 as a `lib/core` change terminating inside `peer_name_find`]. §1.2, §4.1, §11.2, §11.3,
§13 W1 and §14 follow; no owner ruling or product behaviour changes. Documentation only.

**r2.19, 2026-09-25 — W3 scoped by its QA pre-check.** The [W3 pre-check](../evidence/2026-09-25-standalone-mobile-home-w3-precheck.md)
confirmed one C1 refactor with no new state, layout or behaviour, and found that the pager W3 extracts is the detail
modal's fixed-byte slicer — §7.2's word-wrapped review lines stay with the package that adds the review page.
§11.1 no longer offers W3 a 3-row `detail_line` [HISTORY: r2–r2.18 listed "or share `detail_line` widened to 3 rows
(W3)"], and §13 W3 names the three seams, the real-render proof added before the extraction and the re-anchoring
duty. No owner ruling or product behaviour changes. Documentation only.

**r2.20, 2026-09-26 — W4a budgets from its QA pre-check.** The [W4a pre-check](../evidence/2026-09-26-standalone-mobile-home-w4a-precheck.md) measured the
columns at every existing device-label site; §4.1 now states the budgets (TEAM 6, compose header 15, DELIVERED peer
row 19, REPLY sender 14, invite name 6 with its fingerprint, NEW MEMBER name 14) with zero resident growth, and
requires the full counted name before abbreviation. The `DELIVERED to AB34CD` example is corrected: the result
draws the peer on its own 19-cell row, so an unnamed peer shows `0x<HASH8>` there [HISTORY: r2–r2.19]. §13 W4a names
B449 and B455. Author choices within D10; no owner ruling. Documentation only.

**r2.21, 2026-09-27 — W4b scoped by its QA pre-check; allocation owner-ruled.** The [W4b pre-check](../evidence/2026-09-27-standalone-mobile-home-w4b-precheck.md)
asked the author to settle the transitions the design left open, and these are now stated: §6.1 rule 7 names the two
existing unfinished-confirmation cancellations (UI-16 OQ-3) as its only exceptions; the Settings preview drops its
body arrow and the menu's `MENU` row keeps `on_back()`; §6.3 gates each action on its capability; §6.4 pins the
`OPTIONS CHANGED` press table; §6.5 states W4b's interim Inbox and Send content and the Send list's result and
catalog-change returns; §6.2 omits a zero route count as it does a zero unread count; §6.6 pins the blocked-gate note rows and freezes its reason. The owner approved +72 B of static
UI structures for W4b (§11.1, 2026-09-27; §11.1's closing sentence now names that grant beside D7's). §13 W4b adds
B456. Documentation only.
