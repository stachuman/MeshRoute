# B250 — caller-aware return context for roster grants

**Status:** QG-PASSED 2026-08-27 / OWNER METAL RECHECK PENDING / UNCOMMITTED. The owner-approved review corrections
are incorporated and the independent software gate is complete.

**Scope:** one UI-model navigation fix. It changes neither the grant decision nor the grant transport.

**Governing context:** §UI-16 K7 in
`docs/superpowers/specs/2026-08-22-ui16-nearby-onboarding-spec.md`, §UI-17's entered TEAM list and [[B250]].

## 1. Verified current state

This audit is against `13decfb` (`B249`). The defect is structural:

- `run_roster_grant()` (`src/firmware_ui_model.h:4950`) freezes the member hash/id, closes the compose view, moves
  from TEAM to SETTINGS, and deliberately retires the entered TEAM list and its `_team_sel_id` pick.
- The reused N5/N6 chain has only invitation-window landings:
  - NEED-PUBKEY BACK returns to `Provision::invite` (`:4861`);
  - either press on WAITING returns to `Provision::invite` (`:4240`);
  - REJECT returns to `Provision::invite` (`:4884-4886`);
  - `WINDOW CLOSED` and the grant result acknowledge to `Provision::menu` (`:4243`, `:4249`);
  - a matching `peer_key_cached` push returns to `Provision::invite` (`:2901-2908`);
  - blanking a ready/need-pubkey confirmation also returns to `Provision::invite` (`:2860-2862`).
- `sync_team_cursor()` (`:4990-5004`) already supplies the required safe identity behaviour: follow the selected
  TEAM-plane id when its row moves; if it disappeared, clear the pick and raise `team_pick_gone` rather than select
  a neighbour.

Therefore the send path is sound, but its shared UI chain has no authority saying which parent opened it. A landing
chosen from `InviteWindow::taken`, a zero field, or the current screen would only encode the same ambiguity indirectly.

## 2. Required behaviour

### 2.1 Two callers, one grant implementation

The existing preflight, explicit pubkey request, safe-default confirmation, frozen hash, one grant forward, outcome
mapping and `{dst, ctr}` TxDone correlation remain one shared implementation.

Only navigation becomes caller-aware:

- **Invitation-window origin:** preserve every current landing and every current invitation-window side effect.
- **TEAM-roster origin:** every ordinary cancel, BACK, REJECT, expiry acknowledgement and completed-result
  acknowledgement returns to the **entered TEAM roster**, not PROVISION and never the invitation candidate list.

No return gesture may issue a pubkey request, grant, DM, channel post or any other transmission.

### 2.2 Explicit typed context

Add one model-private carrier, conceptually:

```cpp
enum class GrantOrigin : uint8_t { none = 0, invite_window, team_roster };

struct GrantReturn {
    GrantOrigin origin = GrantOrigin::none;
    uint8_t team_local_id = 0;
};
```

The exact spelling may follow the file's surrounding idiom, but these properties are mandatory:

- `origin` is the sole caller authority. Do not infer it from `InviteWindow::taken`, `sel_id == 0`, `sel_hash == 0`,
  `Screen::settings`, or a `Provision` arm.
- `team_local_id` is the selected member's one-byte TEAM-plane local id and is meaningful only with `team_roster`.
  It is not the 32-bit team identity. Zero is not a sentinel; the typed origin carries validity.
- The carrier stays private to `UiModel`. The renderer does not consume it, so it does not belong in `UiState` or a
  frozen frame and must not be duplicated there.
- Selecting an invitation-window candidate establishes `invite_window` before entering the first shared grant-chain
  state. `run_roster_grant()` establishes `team_roster` and copies the already-frozen `compose_peer` before
  `close_compose()` retires that field.
- Origin assignment must not change the relative order or count of B249's fresh-open sequence:
  `load_invite(snapshot)` → `enter_provision(invite)` → exactly one announcement request.
- The context survives the shared grant arms, including `invite_closed` and `invite_result`, because those terminal
  screens still owe one caller-aware acknowledgement.
- A named exhaustive classifier defines the context lifetime exactly; §2.3 lists it. Explicit provisioning close,
  screen leave and emergency pre-emption also retire the context. A later grant must never inherit an earlier caller.

The invitation window's `InviteWindow` lifetime remains unchanged. In particular, do not keep that 104-byte carrier
alive on `invite_result` merely to recover a one-byte TEAM-local member id.

### 2.3 One landing authority

Define one named, exhaustive classifier for the shared grant chain. It preserves the return context for exactly these
five arms and retires it for every other `Provision` value:

```cpp
inline bool provision_is_grant_chain(Provision p) {
    switch (p) {
        case Provision::invite_confirm:
        case Provision::invite_need_pubkey:
        case Provision::invite_wait_pubkey:
        case Provision::invite_closed:
        case Provision::invite_result:
            return true;
        case Provision::closed:
        case Provision::menu:
        case Provision::create_confirm:
        case Provision::create_result:
        case Provision::join_select:
        case Provision::join_confirm:
        case Provision::join_waiting:
        case Provision::join_result:
        case Provision::nearby:
        case Provision::nearby_confirm:
        case Provision::invite:
        case Provision::saved_key:
        case Provision::saved_keys:
        case Provision::saved_keys_confirm:
        case Provision::saved_keys_active:
        case Provision::saved_keys_result:
            return false;
    }
    return false;  // compiler-required tail only; no `default`, so `-Wswitch` reports a new enum value
}
```

`enter_provision()` consults this classifier. No call site may decide context lifetime from a range, ordinal, or a
different list of arms.

Use one caller-aware landing decision for the shared chain, with two exit meanings:

- **resume/cancel:** invite origin → `Provision::invite`; roster origin → entered TEAM roster;
- **terminal acknowledgement:** invite origin → `Provision::menu`; roster origin → entered TEAM roster.

This decision must be called by all relevant paths rather than repeated as origin checks at each call site. It first
copies and consumes the complete `GrantReturn`, then invokes any transition primitive that would clear it. Its
`GrantOrigin` switch is exhaustive and has no `default`:

- `invite_window` and `team_roster` take the two landings above;
- `none` fails closed by clearing the grant chain and returning to `Provision::menu`, with zero device calls. It never
  guesses a parent from another field.

| Event | Invitation-window origin | TEAM-roster origin |
|---|---|---|
| NEED PUBKEY: BACK | invitation list | entered TEAM roster |
| WAITING FOR PUBKEY: either press | invitation list | entered TEAM roster |
| ready confirmation: REJECT | add to the window's handled set, then invitation list | entered TEAM roster |
| ready/need confirmation blanks | invitation list, current behaviour | entered TEAM roster |
| five-minute expiry | `WINDOW CLOSED`; acknowledgement → PROVISION menu | `WINDOW CLOSED`; acknowledgement → entered TEAM roster |
| any grant result | result remains visible; acknowledgement → PROVISION menu | result remains visible; acknowledgement → entered TEAM roster |
| matching pubkey push while waiting | invitation list, current behaviour | existing `GRANT KEY` confirmation, still REJECT-default |
| wrong/unusable pubkey push | no change | no change |
| synchronous request refusal | remain on NEED PUBKEY | remain on NEED PUBKEY |
| grant perform returns “nothing ran” | remain on confirmation | remain on confirmation |

**Owner ruling, 2026-08-26:** a matching pubkey arrival during a roster-origin flow opens the existing `GRANT KEY`
confirmation with `REJECT` selected. This completes the operation the operator deliberately started while still
requiring the existing `short` then `double` before a key can be sent. It adds no screen, and the push itself cannot
send or grant anything.

Emergency pre-emption is not an ordinary BACK. Preserve its existing higher-priority landing and merely retire the
grant return context; do not make an alarm return to TEAM as part of B250.

## 3. Returning to TEAM without selecting the wrong member

The roster return must reuse §B64's existing identity authority, not invent a second matching rule:

1. Capture the return `team_local_id` from `compose_peer` before the compose view is closed.
2. On a roster landing, close SETTINGS/provisioning through the existing transition primitive.
3. Restore `Screen::team` with `ListView::interactive`, `Compose::none`, and the saved TEAM-plane id as the valid pick.
4. Run `sync_team_cursor(snapshot)` before a frame can freeze.

The observable results are:

- same row still present → it is highlighted;
- same id at another row → the highlight follows it;
- id absent → `team_pick_gone` is raised, no row is highlighted as the selection, and the next activation refuses;
- no path clamps to row zero, uses the old cursor index, or calls `note_team_cursor()` in a way that silently records
  whichever member now occupies row zero.

The grant target remains the frozen public-key hash. The saved TEAM-plane id is navigation state only and must never
become the send destination or TxDone correlation input.

## 4. Parent-spec correction

The implementation slice must amend UI-16 K7 as follows:

- replace “entry point only / chain verbatim” with: **the grant decisions, screens, words and send path are reused;
  parent navigation is selected by an explicit caller context**;
- replace “invite window byte-identical” with: **invite-origin behaviour is byte-identical**;
- add the complete two-origin transition table from §2.3;
- retain all existing K7 pins for P-12, full-hash identity, self/keyless vetoes, the one send forward and the
  invitation window's F-11/F-13 semantics.

No old rationale should continue to claim that every grant result belongs to the invitation window.

## 5. Tests and controls

### 5.1 Native model cases

Drive both origins through:

- NEED-PUBKEY BACK;
- issued WAITING and its explicit exit;
- confirmation REJECT;
- blank of ready and need-pubkey confirmations;
- expiry and `WINDOW CLOSED` acknowledgement;
- all eleven existing terminal grant outcomes and acknowledgement;
- matching, wrong-hash and unusable `peer_key_cached` pushes;
- synchronous request refusal and “nothing ran” grant refusal;
- emergency pre-emption from an active invitation-origin grant chain and independently from an active roster-origin
  grant chain. Both must retain the existing emergency landing, close provisioning, clear the return context and make
  zero device calls. A fresh flow opened afterwards must use only its own newly assigned origin.

Also drive `GrantOrigin::none` through the natural no-selection expiry path: acknowledging `WINDOW CLOSED` must clear
the chain, land on `Provision::menu`, and make zero device calls.

For roster returns, independently drive the selected member unchanged, moved to another row, and absent. The absent
case must then attempt activation and prove no compose view, command or grant is produced. Every terminal
acknowledgement must prove the grant/query counters stay unchanged and a second press cannot repeat the act.

Invitation-origin cases remain unchanged and must continue to assert their current list/menu landings and handled-set
behaviour.

### 5.2 Mutation battery

At minimum, each of these controls must turn RED independently:

- delete the roster-origin binding while retaining the rest of `run_roster_grant()`;
- derive the origin from `InviteWindow::taken` instead of the explicit binding;
- collapse both origins to the invitation landing;
- collapse both origins to the TEAM landing;
- make `GrantOrigin::none` infer either parent instead of failing closed to `Provision::menu`;
- restore TEAM as passive or leave SETTINGS/provisioning open;
- restore row zero / the old cursor instead of the saved TEAM-plane id;
- remove the `sync_team_cursor()` identity-follow step;
- make the gone-member path select a neighbour;
- route a roster-origin matching pubkey push to `Provision::invite`;
- route an invite-origin matching pubkey push to the roster confirmation;
- retain the context across an unrelated provisioning close and prove it contaminates the next grant;
- retain either origin across emergency pre-emption;
- repeat either the pubkey request or grant on a terminal acknowledgement.

Every mutation must have a non-zero match count and the battery must report zero unusable controls.

### 5.3 Feature/wiring proof

Extend the existing K7 feature-probe arm through the real model/device adapters. It must prove the roster result lands
on TEAM while invite-origin results still land in PROVISION, and that neither landing adds a device call. No renderer,
board-canvas or HAL change is expected.

## 6. Resource and regression boundary

- `sizeof(UiState)` and every frozen-frame offset remain unchanged.
- Measure and report `sizeof(UiModel)` before/after on native and the 32-bit board ABI; do not infer board cost from
  host padding.
- Measure RAM/flash on the two essential environments only, in order: `heltec_v4_mobile`, then
  `gateway_heltec_v4`.
- No `lib/core`, `lib/hal`, wire, routing, key material, NV record, timer or simulator physics change.
- The simulator binary must remain byte-identical. Rebuild `lus` and reproduce the current s18 keystone from
  `simulation/BASELINE.md`; do not edit the corpus anchor table.
- Run native (wrapper plus the real binary), the complete model battery, the firmware-UI feature probe,
  `tools/warning_census.sh`, the two essential builds, s18, and `git diff --check`.
- No new warning and zero `-Wswitch` diagnostics.

## 7. Done

B250 is ready to close only when the caller authority is explicit, every exit in §2.3 is pinned for both origins,
the roster return follows/refuses by TEAM-plane identity, the invite-origin transcript is unchanged, all mutations
are RED, and the bounded gate in §6 is green. Independent QG is complete; closure still requires the owner's metal
recheck described below. Leave the work uncommitted.

## 8. Implementation record — 2026-08-26

The bounded slice is implemented in `UiModel` with one private typed `GrantReturn`, the exhaustive five-arm
`provision_is_grant_chain()` lifetime classifier, and one caller-aware landing authority. The roster landing copies
and consumes the context before closing provisioning, then reuses `sync_team_cursor()` to follow or refuse by the
saved TEAM-local identity. Invitation-origin behaviour and the shared grant request/send/outcome machinery remain
unchanged. No renderer, device adapter, HAL, core, wire, routing, key, NV, timer or simulator-outcome path changed.

Coder gate:

- native wrapper plus real binary: **2218 cases / 95587 assertions / 0 failures**;
- complete UI-model mutation battery: **239 RED / 0 unusable**, including independent wrong-caller, stale-context,
  emergency-pre-emption, crossed-push, identity-follow and repeated-act controls;
- firmware-UI feature probe: layered **426/426**, child/mobile **858/858**, BLE **426/426**; **221 controls RED / 0
  unusable**;
- `sizeof(UiState)`: host **504 → 504**; `sizeof(UiModel)`: host **928 → 928**, ESP32-S3 ILP32 **912 → 912**;
- essential builds, in the required order: `heltec_v4_mobile` **219188 RAM / 1323684 flash** (**0 / +160** versus
  B249), then `gateway_heltec_v4` **244452 / 1273956** (**0 / +164**);
- warning census: `gateway_heltec` **174**, `gateway_heltec_v4` **179**, `heltec_mobile` **178**, `heltec_v3`
  **178**, `heltec_v4` **183**, `heltec_v4_mobile` **183**; zero `-Wswitch`, no new warning;
- rebuilt `lus` remained byte-identical at **`0876d3e528abf23b2e6b3f69155e179b`**; s18 reproduced exactly
  **`9868cad3` / 269905 events / 0 failures**; the corpus anchor table was not edited;
- `git diff --check`: clean.

**Independent QG, 2026-08-27: PASS.** QG independently reproduced the native, mutation, feature-probe, essential
board, simulator and s18 results above and found no correctness blocker. The sole remaining closure check is the
original metal reproduction: complete a roster-origin grant to M2, acknowledge the result, and confirm the entered
TEAM roster returns with M2 still selected. If M2 disappeared, the panel must show `TEAMMATE GONE` without selecting
another member. Work remains uncommitted pending that owner check.
