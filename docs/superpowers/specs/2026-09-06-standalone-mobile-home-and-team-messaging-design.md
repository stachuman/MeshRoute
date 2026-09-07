<!-- Author: OpenAI Codex -->
# Standalone mobile — identity, dynamic Home and team messaging

**2026-09-06 · updated 2026-09-07 · DRAFT product design — owner agreements captured; detailed review pending.**
Not an implementation brief, QA PASS, dispatch authorization, measured resource result or change to the
remote-admin sequence. Author writes documentation only. The owner commits; QA pre-checks and gates
each eventual implementation brief. Illustrative screen strings below are proposals, not frozen literals.

## 1. Purpose and agreement boundary

A Heltec mobile is a handheld group communicator that can be used without an iOS companion. Primary
scenarios are a group hiking together and an event/festival team coordinating work. Cycling, camps,
group outings and volunteer crews are adjacent use cases, not coverage or safety-certification claims.

The owner agrees with the direction:

- Home should be dynamic: a device without a team offers the most useful setup actions; a device in a
  team emphasizes everyday communication.
- The wearer must be able to answer who this device is. Show its name, not only a team-local number.
- Joining/creating a team and naming the device should be discoverable without knowledge of provisioning.
- Preserve these discussions in a dedicated design, including ordinary whole-team messages.

**Owner correction, 2026-09-06:** "This limit of 17 chars is wrong - messages should use multilines."
For this redesign, message length must not be constrained to a single menu row. Multiline display and
paging are required; the old 17-byte preset limit is a current implementation fact, not the new target.
The longer example Return to base now must remain intact. This supersedes the Author's initial
recommendation to keep 17 bytes and shorten the wording; that recommendation is withdrawn.

**Owner agreement, 2026-09-07:** "Proposal makes sense, grouping should be equal (we can't consider english specific statistic of letter use) and limited to bare minimum (letter, numbers, limited punctaction)."
The accepted direction is one shared standalone text editor for naming and manually composing ordinary
team/person messages: short advances, double chooses, with equal-sized fixed character groups and a
minimal letters/digits/punctuation repertoire. No English letter-frequency layout, predictive selection
or learned reordering. Long holds remain the existing emergency gestures. §4.1 records the interaction
contract and an explicitly proposed equal-group layout; §6.2 records manual sending.

Detailed mechanisms remain proposals: the exact alphabet/case/group size and editor geometry, Home
action/rail gestures, boot splash duration, incoming-message preview policy, new stored-body capacity
and preset vocabulary. Moving the logo from Home to a new boot splash is
under consideration. The owner's question about showing incoming messages on Home is answered by the
recommendation in §7, not treated as a final owner ruling.

### 1.1 Correction: ordinary channel sending already exists

The discussion initially assumed that only emergency channel messages could be sent from the mobile.
The owner subsequently clarified: **there is no device user-experience report behind that assumption**;
they may have been mistaken. The Author's intermediate wording about a mismatch with device experience
is withdrawn too. No failure on hardware has been reported or reproduced.

Source inspection establishes an ordinary channel-preset path (§2). Therefore this design improves its
discoverability, scenario vocabulary and presentation; it does not invent a second send mechanism or
declare the existing one broken. A real-device walkthrough remains future validation, not a prerequisite
for accepting the source fact. Maintained intake HOME-A1 records this documentation correction.

## 2. Current source baseline (V1/V2/P1)

Read-only inspection of the shared checkout at HEAD a0ff994, 2026-09-06. Remote-admin Slice 4 has
concurrent uncommitted production/test/tool edits. These are observations of that checkout, not a clean
dispatch base or a claim about what firmware is on the owner's device. Re-resolve symbols and line
anchors at QA's pre-check; no production, test, tool or QA evidence file is edited by this preparation.

| Fact | Current executable source anchor |
| --- | --- |
| Mobile Heltec V3/V4 environments select the mobile profile | platformio.ini:524 and :530 |
| ESP32 companion BLE transport is inert; the implemented arm is nRF52-gated | src/device_ble.h:24, :41 |
| Default UI screen is Status; ordinary screen cycle is Status, Team, Inbox, Send, Settings | src/firmware_ui_model.h:2358, :244, UiModel::next_screen |
| Current body is team ID, team-local ID, known route count, unread/home age, location/restart | src/firmware_ui.cpp:1368; src/firmware_ui_status.h:83, :96, :125, :155, :204 |
| Logo is 24x24; upper three rows allow 14 columns, bottom two 19 | src/firmware_ui.cpp:1033; src/firmware_ui_status.h:60 |
| Name persists in the identity record and updates the live core after a successful save | src/firmware_config.cpp:271, handle_cfg_set name arm |
| Stored core name is up to 32 bytes; unset name uses the existing identity-derived default | lib/core/node.h:525; lib/core/node.cpp:41, Node::effective_name |
| OLED Settings has no name editor | src/firmware_ui_model.h:640, settings_rows |
| Peer labels use cached names, with hash or local-ID fallbacks | src/firmware_ui.cpp:440, label_from_hash / label_for_team_id; lib/core/node_hashlocate.cpp:450 |
| Create, nearby Join, Invite and Saved Keys already have provisioning rows | src/firmware_ui_model.h:784, provision_rows; :914, provision_row_label |
| Ordinary Send page explicitly offers team sending; double enters channel composition | src/firmware_ui.cpp:2149; src/firmware_ui_model.h:3895 |
| Selecting a channel preset queues channel_canned, separate from emergency | src/firmware_ui_model.h:5401 and :5421 |
| Real composer emits send_channel with team and encryption options, then uses the shared executor | src/firmware_ui_send.h:547, :573; src/firmware_ui.cpp:514, :539 |
| There are eight channel slots; defaults enable Got your message and All good | src/firmware_ui_presets.h:137, :376; channel defaults at :386 |
| Every preset is limited to 17 printable ASCII bytes | src/device_nv.h:355; src/firmware_ui_presets.h:332, validate_preset_text |
| A completed visible Inbox list frame advances session-unread watermarks, not individual reading | src/firmware_ui_model.h:5670, FrameGate::on_page |
| Channel Inbox entries retain team, channel, receive time and opened-encryption metadata, but no full stable sender hash | lib/core/inbox.cpp:174, Inbox::record_channel; lib/core/inbox.h:27 |
| Receive wake and distress-reply attribution are different guards | src/firmware_ui_send.h:664–695; src/firmware_ui.cpp:2726 |
| Default blank interval is 15 seconds; a short waking gesture is consumed; emergency holds have their own path | src/firmware_ui_model.h:221, :2668, :5322; src/firmware_ui_input.h:18 |

Existing test source explicitly walks Status → Team → Inbox → Send, double-opens channel composition,
selects the second preset and obtains a channel_canned request:
test/test_firmware_ui_model.cpp:263. The byte-level composer case at
test/test_firmware_ui_send.cpp:1299 expects an ordinary encrypted team-channel line.
These tests were inspected, **not run in this documentation turn**.

**Text-editor follow-up inspection, 2026-09-07 at 1677b44 (V1/V2):** the active Slice 5 coder has
untracked remote_session files; they are outside this documentation task. src/firmware_ui_input.h:18–41
already classifies short/double and emergency holds, with double_gap_ms=350 and short emitted only after
that window. UiModel::on_gesture at :2668–2675 gives emergency holds precedence and consumes the waking
press. The name mutation at firmware_config.cpp:272–279 updates the live name only after save succeeds.
SendReq at firmware_ui_model.h:1672–1677 carries a preset slot/generation, not an owned manual text body;
firmware_ui_send.h's send_gate_of validates that catalog binding for ordinary sends. A shared keyboard
therefore needs an explicitly designed manual-body submission path, not a fictitious existing free-text
UI path or a bypass of the preset gate. No implementation, timing change or new gate result is claimed.

## 3. Product model: identity, membership and attention are separate

Home answers, in order: **which device is this; which group is it with; what needs attention; what can I do?**

- Identity stays visible across Home variants. A person's name (Stan) or an assigned role (Gate 3) is valid.
- Team membership changes available actions, not the device's identity.
- Membership, possession of the team content key and acquisition of a team-local ID are distinct facts.
- A known route is not a person proven online. Home confirmation is not a guarantee of team connectivity.
- No configured location need not occupy an everyday festival Home. An action that requires location
  must still explain a missing fix at the point of use. Never substitute a location or bypass a refusal.
- The UI must distinguish setup readiness, radio transmission, delivery evidence and human acknowledgement.
- Dynamic content must not cause an action under the button to change identity.

## 4. Identity and first-use flow

Proposed normal Home heading: **ME: STAN**. The full name remains accessible in My device. A long name
must have a declared abbreviated Home rendering and a full-name view; never silently truncate identity
in the only place the wearer can read it. No extra stored short-name identity is proposed.

With no configured name, render a compact, explicitly declared presentation of the existing default
identity, for example MR-12AB34CD. It is not a new address, a v2 administration fingerprint or a promise
that a short code is collision-free. QA must verify consistency with the peer-side fallback. Preserve a
full identity/details path for ambiguous names; duplicate display names must not select the wrong peer.

Joining or creating offers a name check, with **Change name** and **Continue**. A usable unnamed device
must not be blocked by a keyboard wizard. Setting the name must preserve keys, coordinates and membership;
failed save leaves the old live name intact. Do not assume the current console mutation can be copied
into a new UI service without checking its persistence/error contract.

USB preparation already supports a name and is useful for an organizer distributing devices. The
standalone editor is new software, but its shared name/manual-message purpose and equal-group direction
are now agreed (§4.1). Naming uses My device → Change name → editor → full-name review → Save;
Cancel leaves the old name intact. Alphabet/case, non-ASCII handling, geometry and the editor's practical
length remain detailed review decisions, not a reason to reintroduce frequency-based or unequal groups.
The existing 32-byte storage is not permission to treat 32 arbitrary characters as fitting.

A peer name is a cached label, not a unique or instantly synchronized identity. A rename must not promise
that John's device already calls this device Stan. Recipient selection keeps its actual identity bound
through compose/confirmation despite name changes, roster movement or team-local-ID reassignment.

### 4.1 Shared one-button text editor — agreed direction, 2026-09-07

One editor supplies text to two different callers: naming and ordinary message composition. It does
not itself save identity, modify the saved-phrase catalog or send a packet. Those are explicit caller
actions after review. Keep the first editor small and predictable:

- Two selection levels: group, then character. Short advances one selection; double chooses it.
  Both levels use the same grammar. Selecting a character inserts exactly that character and returns
  to group selection; the proposed return highlight is the group just used, without changing its order.
- Every character group has the same number of selectable characters. Use a fixed alphabetical/numeric
  order, independent of language statistics, message content, usage history or a predicted next letter.
  Do not hide an undersized final group, duplicate letters or add dummy selectable cells to claim equality.
- The repertoire is letters, digits, space and a small explicitly enumerated punctuation set. No emoji,
  symbol library, word prediction or dictionary in this first editor. Alphabet/case support is a separate
  explicit choice; equal grouping is not a claim that every language or existing name is representable.
- Editor controls are separate from character groups: Back returns from characters without insertion;
  Edit exposes Backspace, Move cursor, Done and Cancel. Control labels are not transmitted characters
  or padding used to balance the alphabet. No long-press editing shortcut or automatic timed scanning.
- The text remains visible, wraps over multiple lines and follows the cursor across longer drafts.
  Show the selected character and remaining capacity without interpreting row width as the body cap.
  Name and message callers apply their own source-derived byte bounds; neither inherits the old
  17-byte preset limit just because the editor is shared. Refuse a character that cannot fit, never clip.
- Preserve the draft through ordinary display blank/wake and incoming notifications. A waking press
  only wakes; it must not also insert/select. Incoming content cannot replace the editor or move its
  selection. Emergency holds retain immediate precedence; no draft or pending Send may be submitted
  automatically when the emergency UI ends. Revalidate context and require fresh send confirmation.
- Draft state is bounded RAM, not an NV write per character. Reboot/power-loss draft recovery and saving
  a draft as a reusable preset are not included by this agreement. Cancel/discard is explicit. Unsupported
  characters in an existing name/message must not be silently uppercased, transliterated or removed to
  fit a smaller editor alphabet; the non-lossy edit/refusal behavior is part of detailed review.

**Author layout candidate, not an owner-frozen alphabet:** seven groups of six characters, using one
uppercase Latin alphabet, ten digits, a space and five punctuation marks. All 42 characters occur once:

| Group | Six selectable characters, in order |
| --- | --- |
| 1 | A B C D E F |
| 2 | G H I J K L |
| 3 | M N O P Q R |
| 4 | S T U V W X |
| 5 | Y Z 0 1 2 3 |
| 6 | 4 5 6 7 8 9 |
| 7 | SPACE . , ? ! - |

SPACE inserts one ordinary space. The mixed Y/Z/digit group preserves equal size without assuming
which letters are common. This is a selection map, not a claim that all groups fit simultaneously on
the OLED. Exact glyphs/case, six-character geometry and control placement await review; additions must
keep the minimal repertoire and equal-group rule rather than silently expanding an exceptional group.
The earlier A–F / G–L / M–R / S–Z sketch is superseded because its last letter group had eight entries.

The current classifier waits 350 ms to distinguish short from double; rapid repeated taps can become
a selection rather than repeated navigation. Reuse the classified gestures, not raw button edges, and
measure the actual button interaction before claiming a typing rate or changing timing. Saved phrases
remain the fast route; manual text is useful even when slow, without borrowing emergency-repeat behavior
or implying guaranteed delivery of an urgent message.

## 5. Home without a team, and provisioning

Illustrative five-row body; top strip and navigation chrome omitted:

    ME: STAN
    NO TEAM
    > JOIN TEAM
      CREATE TEAM
      MY DEVICE

Join is first because most participants join a group that one organizer creates. This is an action
surface, not a fault dashboard. Additional menu rows, including Back where required, use a defined
scrolling/return rule; a sixth row is never assumed to fit.

Participant flow:

1. Join team → optional name check → existing nearby-team list.
2. Select a team code that can be compared with the organizer's display.
3. Confirm using the existing provisioning and radio-compatibility checks; do not invent automatic
   cross-frequency discovery or change radio settings just to make this screen look successful.
4. Present the real membership result. If a saved-key offer applies, preserve that deliberate choice.
5. If a team key or team-local ID is still missing, go to a specific setup-attention Home, not Ready.
   Explain the existing invitation/key-sharing procedure; do not imply an automatic key-request path.

No team observed → explain that observation and offer retry/back. It is not proof that no team exists.
Owner-side invitation, grant selection and confirmation remain explicit.

Organizer flow:

1. Create team → optional name check → existing creation/configuration/confirmation flow.
2. Show the actual durable result, not success before a write.
3. Offer Invite members or Go to Home after acknowledging the result.
4. Invite shows the team's comparison code and candidates through the existing grant safeguards.

Do not silently replace the established nearby-team fingerprint format with Home's full team ID.
Both endpoints of any revised comparison display must agree; reconciling the formats is a review item.
An acknowledged create/join is not undone merely by navigating Back.

## 6. Home in a team and ordinary whole-team sending

Illustrative normal body:

    ME: STAN
    TEAM 12A1B2C3
    > INBOX (3 NEW)
      SEND MESSAGE
      MY TEAM

Send message offers **To team** and **To a person**. My team leads to the existing people/invitation
functions. My device/Settings stays reachable by the reviewed navigation/continuation, not hidden
because the three visible communication actions fill this illustration.

Whole-team flow:

**Send message → To team → Saved phrase / Write message → review full text and team → explicit send → real result.**

The Saved phrase branch reuses the existing channel preset catalog, team channel constant, shared command executor and
ordinary-send tracker. No emergency repeat/retry behavior is borrowed. An explicit pre-send review is
a proposed change: today a double on a preset queues it directly (src/firmware_ui_model.h:5421).
Bind the selected preset slot, catalog generation and team context so a changed catalog or team cannot turn
the confirmation into a different transmission. No send is inferred merely from opening the preview.

Example vocabulary:

| Scenario | Candidate text | ASCII bytes | Current preset fit |
| --- | --- | --- | --- |
| Event/festival | End of shift | 12 | yes |
| Hike/event recall | Return to base now | 18 | no |
| Earlier Author shortening, now withdrawn as a remedy | Return to base | 14 | yes, but not the requested remedy |

The old 17-byte cap came from owner-ruled OQ-A in the preset-catalog spec: it priced a message as one
19-column row minus selection/location markers. The owner now rejects that design premise and requires
multiline messages (§1). The table records today's behavior, not acceptance limits for this redesign.
Do not drop now or change the user's wording to satisfy the old format.

HOME-A2 tracks the replacement and its implementation obligations. The larger persistent preset record,
validation, compose/send buffers and any migration need an explicitly fenced change after QA derives
the real bounds. These examples never authorize overwriting customized catalogs or silently enabling
previously empty slots in an existing valid record.

### 6.1 Multiline message contract — owner-directed

- Stored/transmitted message length and visible row width are independent quantities. Screen columns
  determine wrapping, never the accepted body length.
- Display the full message across multiple rows and, when necessary, multiple pages. A selection list
  may show a clearly abbreviated preview, but a full-text review is available before explicit send.
- Bind review pages to the same recipient/team, slot and generation that will be submitted. Page
  navigation cannot itself transmit. A changed message/context invalidates review rather than sending
  different bytes. Send and Cancel remain explicit, distinct from Next page.
- Prefer word boundaries where possible; split an overlong word predictably. Preserve byte order and
  content across page boundaries: no missing suffix, duplicated phrase or implicit text replacement.
  Show page position and a continuation indication, so a first page never looks like the whole message.
- Display wrapping does not insert newlines into the command transport or rewrite the payload.
  Support for author-entered paragraph breaks would need an explicit parser/encoding contract; ordinary
  long text must already wrap without one.
- Derive the bounded maximum from the existing DM/team-channel transport admission limits, location
  overhead and storage/resource constraints. Do not reuse remote-admin DM caps as channel authority,
  choose a new arbitrary screen-sized cap, or assume every optional form has the same payload allowance.
  Exact capacities are pending QA's source-derived pre-check, not permission for unbounded allocation.
- Update the versioned catalog/layout and migration policy deliberately. Preserve existing custom
  messages on valid-record upgrade; unsupported/unreadable records must not silently become defaults.
  Existing validated short messages remain exact. No automatic wire-version bump is presumed.
- Apply the same full-message presentation principle to normal DM and team-channel composition and
  received-message detail. Audit the shared emergency preset/storage path without changing emergency
  gestures, repeating behavior, location fallback or proven send outcomes as an incidental consequence.

This is now more than moving the existing Send entry: it includes a bounded preset-capacity and
multiline-review feature. C1 still forbids bundling unrelated refactors or file moves with that change.

Preset selection is not text authoring. Current defaults do not contain either new example. USB catalog
editing can prepare a deployment without iOS. The earlier statement leaving all on-device text authoring
as a separate possible capability is superseded by the shared-editor agreement: Write message is now
part of this design (§6.2). Editing or saving the persistent preset catalog from the panel remains separate;
one-off manual composition must not rewrite a preset slot. Keep empty/disabled catalogs honest, with
Write message available through its own admission checks rather than pretending a preset exists.

Person flow:
**Send message → To a person → bound recipient → Saved phrase / Write message → recipient/text review → explicit send → result.**
Expose ambiguity rather than invent a name or claim a short identifier proves who is holding the radio.

An ordinary channel post does not have an all-recipients-delivered result. Preserve queued, aired,
relay-observed, blocked and unconfirmed distinctions from the real tracker. Never show Everyone received
or infer a human acknowledgement from radio traffic. Received instruction-like text never executes
a device command; this feature is unrelated to remote-admin RPC.

### 6.2 Manual message flow — shared editor, separate explicit send

Choose the recipient/team first, then Write message opens the §4.1 editor. Keep that destination visible
and bound while typing. Done leaves the keyboard for full-text/recipient review; it never transmits.
Review offers Edit, Send and Cancel. Paging never sends, and returning to edit invalidates the previous
review. The reviewed byte sequence is the one submitted, not a later mutable draft or a catalog lookup.

Use one owned bounded draft and an explicit handoff/lifetime contract through any queued submission;
no dangling editor buffer, temporary NV preset, fabricated slot/generation or exemption through the
emergency kind. Extend the existing normal DM/team-channel submission and outcome path under a reviewed
implementation fence; keep saved-phrase generation validation intact. Apply the real destination,
team/key, optional-location and body-capacity checks to manual text too. Revalidate a changed team or
recipient before sending; never silently retarget. Display wrapping adds no transport newline (§6.1).

Saved phrases remain the everyday fast option. Write message covers unanticipated information such as
an injury, a changed meeting point or a name/location absent from the catalog; it remains an ordinary
message. It creates no emergency priority/retries, guaranteed reachability or all-recipients receipt.
The existing emergency hold remains usable while composing, independently of typing progress.

## 7. Should a received team message appear on Home?

**Author recommendation: yes, as a bounded, persistent-through-sleep Home preview, not an emergency overlay.**
It is especially useful when a festival worker checks for an instruction or a hiker wakes the display.
The preview is a received message, not the sender's local send result and not a permanent team directive.

Illustrative body, using a sender-ID fallback the retained channel record can actually support:

    ME: STAN
    TEAM / T7     20s
    Return to base now
    2 MORE NEW
    > OPEN       HIDE

This is a content sketch, not two physical soft keys. Open and Hide require explicit selectable actions
under the one-button grammar. Keep access to Send/My team through the reviewed action/menu path.
An instruction longer than the preview window is visibly abbreviated and opens into the full message;
do not let a clipped qualifier turn a message into a misleading complete instruction.

### 7.1 Eligibility, provenance and record lifetime

- First scope: readable, successfully opened encrypted posts for the current nonzero team on the
  existing UI team channel. Reuse the real channel constant and core team-scope semantics.
- Reject other-team/history, leaf/global, other-channel and internal records from this team preview.
  Reuse the existing internal-record/text admission helper, not a second private list of DATA types.
- A received team post can be authenticated under a shared team key without proving which individual
  authored it. It is not an organizer-only announcement. No new sender-role or urgent-message privilege.
- Retained channel records have sender_hash=0 (Inbox::record_channel). Do not reconstruct a permanent
  sender identity from today's mapping of a historical team-local ID. Initially use a truthful T7-style
  origin label; showing a stable sender name requires a separately verified retained binding.
- Select from the existing Inbox, bound to its storage epoch/kind/sequence and recorded team context.
  No dangling callback body pointer, unbounded second inbox or persistent write on every repaint.
  QA must verify the lookup/epoch seams and measured storage behavior before a brief.
- Freeze metadata and bounded text for the page-buffer frame. Revalidate existence/context before Open.
  Eviction/deletion cannot open a different record; a missing record gets an honest unavailable result.
- Leaving/switching teams clears the old team's Home offer, without deleting or relabelling Inbox history.
  A pending old send confirmation is invalidated, not silently retargeted to the new team.

### 7.2 Selection, sleep and interruption

Proposed v1 policy:

1. On entering an idle Home, offer the newest eligible not-yet-retired preview record. This is a local
   preview-offer policy, not a new definition of global unread. Show additional arrivals as a count.
2. Once shown, retain that record through ordinary blank/wake. New arrivals do not replace its text
   or move its action under the user's finger; provide an explicit route to other messages.
3. Entering Home with multiple messages does not auto-cycle them. An older instruction is visibly
   aged; neither a newer post nor elapsed time silently declares it fulfilled or obsolete.
4. A receive while elsewhere may update existing badges and use existing wake policy, but never navigates
   out of compose, a confirmation, an editor, Inbox detail or an operation result.
5. Emergency UI keeps its established precedence. A Home preview never delays or masks it.
6. On reboot, do not claim a RAM-only preview survived or replay old storage as newly received. QA must
   bind startup selection to actual Inbox retention/session semantics; no new durable preview store.

### 7.3 Open, Hide, unread and acknowledgement

- Drawing/waking onto a preview does not mark a message read, advance Inbox watermarks, delete it,
  emit an acknowledgement or send a response.
- Open addresses that exact message in the existing detail flow and retires the Home offer only after
  the valid transition. Hide retires only this local offer; it is not a read receipt or deletion.
- Retiring an offer prevents the same card immediately reappearing. The bookkeeping is bounded,
  session-scoped and separately measured; it is not a second delivery/read ledger.
- Preserve the existing completed-Inbox-list unread contract unless the owner explicitly selects a
  separate redesign. Opening detail directly from Home must not pretend the whole Inbox list was shown.
- The screen should not label the local action Acknowledge: an actual human reply is an explicit send,
  through the normal send path, and a group acknowledgement protocol is outside this proposal.

No keyword detection: Return to base now, End of shift and any other readable team text are ordinary
messages. Do not infer emergency priority, official authority or a dismiss-proof notice from wording.
If a persistent organizer bulletin board or acknowledged task system is wanted, design it separately.
Direct-message Home previews and their privacy/priority policy remain a follow-up decision.

## 8. Splash, chrome and interaction

Propose moving the current logo from Home to a short boot-only splash (approximately one second as a
discussion value, not a timing pin). A press dismisses and is consumed; radio servicing and emergency
handling never wait for a blocking splash delay. Boot faults must not be hidden behind branding.
No splash on display wake; restore the previous screen, including retained compose/detail context.

Keep battery and useful status facts accessible. Reassess the crowded strip/rail and duplicated counts
with measured pixel layouts rather than assuming logo removal creates unlimited room. No low-battery
percentage/runtime model, signal-strength promise or home=connected shorthand is introduced here.

Proposed action grammar: short advances a selection, double opens/chooses, explicit Back for menus;
existing emergency holds remain global. This changes short-to-next-screen on Home and needs an explicit
reviewed navigation map, including a discoverable way to leave Home and reach Settings.
Do not accidentally apply selectable Back to terminal results, which have their own acknowledgement rule.
Name/phrase editing, splash dismissal and wake must not consume or reinterpret emergency holds.

## 9. Existing decisions retained or requiring explicit revision

The following are not silently superseded by this draft:

| Existing authority / executable anchor | Proposed relationship |
| --- | --- |
| UI-17 spec §2 / §4 S3/S6; firmware_ui.cpp:1033, :1368 | Replaces fixed five-fact Status content and moves its logo; requires approved geometry/strings/probe changes |
| UI-17 §1 navigation; UiModel::advance_or_next at firmware_ui_model.h:3818 | Action-first Home changes its gesture behavior; full navigation map required before implementation |
| UI-17 §9 R-1 | Preserve compose/detail across blank, consume waking press, keep emergency exception |
| UI-17 §9 R-2 | Keep existing Team ordering; no sorting hidden in Home work |
| UI-17 §9 R-3 | Preserve configuration badge vs Settings text separation and visible restart requirement; no generic unsaved/conflict text reintroduced on Home without a ruling |
| UI-17 §9 R-4/R-5 | Existing body-based rail mapping and terminal acknowledgement survive unless explicitly revised |
| UI-17 §9 R-6/R-7; firmware_ui_send.h:637, :681 | Preserve existing receive wake scope and no-rate-limiter ruling; preview retention does not keep the panel lit |
| UI-10/11 preset spec OQ-A; device_nv.h:355 | Owner's 2026-09-06 correction supersedes the single-row/17-byte design for this redesign; derive new bounded storage/admission and preserve generation-bound full-text review |
| UI-16 provisioning; provision_rows / existing adapters | Reuse join/create/invite/key services and their confirmations; change entry/landing, not authorization implicitly |
| FrameGate::on_page at firmware_ui_model.h:5670 | Preview never counts as an Inbox-list frame; unread redesign is separate |

References:
docs/superpowers/specs/2026-08-20-ui17-navigation-status-team-redesign-spec.md;
docs/superpowers/specs/2026-08-25-ui10-11-preset-catalog-spec.md;
docs/superpowers/specs/2026-08-22-ui16-nearby-onboarding-spec.md.
The earlier documents remain historical/current-implementation references. This draft explicitly records
the new multiline ruling; it does not claim that the running code or old catalog format already changed.

## 10. Review decisions and eventual verification

Owner/design review still selects:

1. Exact Home states, strip/rail geometry, gesture map and full-name/identity-detail path.
2. Exact minimal alphabet/case, equal group size/layout, edit controls, non-lossy handling of existing
   unsupported text, and caller-specific byte capacities. Shared naming/manual composition, equal fixed
   groups and no frequency-based ordering are agreed; the seven-by-six map in §4.1 is a candidate.
   Persistent on-device preset editing remains a separate possible increment, not implicit in Write message.
3. Scenario preset vocabulary/default policy and the source-derived bounded capacity/migration design.
   Multiline presentation and removal of the single-row 17-byte limit are settled direction, not an
   outstanding keep-versus-expand question.
4. Incoming Home preview policy, its exact Open/Hide/count semantics and origin-label limitation.
5. Whether direct-message previews or any additional interruption policy are wanted; neither is assumed.
6. Splash timing and the precise changes to earlier geometry/navigation rulings.

Suggested work packages, not dispatch slices: identity/Home/setup entry points; shared standalone text
editor with name save/review; manual DM/team-message draft ownership, review and normal-send integration;
bounded catalog-capacity/migration and multiline composition/review; ordinary-send discoverability and
scenario vocabulary; incoming Home preview; boot splash/artwork
relocation as a separately attributable visual change if useful. Dependencies and actual fences come
from QA pre-checks. No implementation overlap with the active remote-admin coder is authorized.

Before any brief, QA must verify the real UI wiring and tests, both mutation selectors
(changed-source and historical/dependency), preset-generation invalidation, team-change handling,
record identity/provenance, retained context, glyph budgets, button timing and boot/service scheduling.
Gate the required union; pure formatter tests alone do not prove production rendering/dispatch.

Expected direction, not measured pins: UI-only changes should be corpus-inert if core is untouched;
snapshot copies, preview metadata/text, editor scratch and board stack/RAM/flash still need attribution.
No assumed zero board cost. Any core/NV/wire expansion needs its own explicit authority and predictions;
wire changes are not forbidden by deployment concerns (M3), but attribution stays separate (C4).

Native/model/real-renderer instruments should cover all states and longest strings; negative controls
must distinguish Home from Inbox read-marking, ordinary channel from emergency, wrong team/channel,
cleartext/internal records, renamed/reassigned senders, stale catalog/context, deletion/eviction,
multiple arrivals while selecting, and blank/wake during every confirmation or edit. Add exact-byte
multiline/page-boundary cases, 17 versus 18 versus actual capacity/cap-plus-one, location-dependent
admission, no silent clipping, full-review/send identity, valid-record upgrade/custom-text preservation,
failed migration and shared emergency-catalog regressions. Page advancement must never score as send.
For the editor, assert equal group sizes, unique/reachable exact repertoire and fixed order; drive
character/group wrap, Back, Backspace, cursor movement, Done versus Save/Send, full-buffer refusal and
cancel without mutation. Exercise the short/double timing boundary, blank/wake, notification arrival,
emergency pre-emption, changed recipient/team and queued draft lifetime. Manual sending must prove the
reviewed bytes reach the normal DM/channel path without touching NV presets or bypassing preset guards.

Future metal residue belongs in the maintained bench script (M2), under the eventual brief:
readability and one-button discovery without coaching; setup of two devices; John identifies/sends to
Stan; enter a name and a new message using only short/double gestures, with errors corrected and no
timing coaching; whole-team ordinary send and truthful outcome; message arrival while another task is active;
screen sleep/receive wake and power cost under event traffic; splash/radio/emergency coexistence.
No new bench part is numbered or claimed run here. Exact console/panel expectations are frozen with
the implementation brief, not inferred from these illustrative wireframes.

## 11. Preparation record

Dedicated discussion capture plus MEMORY/tracker pointers and maintained HOME-A1/HOME-A2 intake only.
No code, test, tool, generated inventory, bench script, QA ledger, remote-admin brief/evidence or ruling
was edited for this design. No build, test suite, simulator, board gate, regeneration or commit was run.
Concurrent-input auditing remains the active coder/QA's responsibility; this note waives no STOP.

**2026-09-07 editor update:** captured the owner's shared-editor/equal-group/minimal-repertoire agreement,
the Author's seven-by-six candidate, manual-message flow and verification obligations; updated MEMORY
and tracker pointers. No new defect finding or register number: this is proposed feature scope, not a
measured bug. No production/test/tool/QA-ledger/evidence/bench edit, regeneration, software gate or commit.
Active Slice 5 files are untouched; this design still authorizes no overlapping implementation.
