<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# NOTE — Heltec STATUS, TEAM and one-button navigation redesign · 2026-08-20

**Status: OWNER-APPROVED DESIGN DIRECTION, not yet an implementation spec.** The interaction model, STATUS
contents, teammate-row semantics and the 24x24 logo reservation below were approved by the owner on 2026-08-20.
A bounded implementation spec and plan must still inventory affected states, tests, mutations, metal checks and
resource costs before code changes start.

## 1. Objective

Make the Heltec mobile UI predictable with one button and make its first two screens useful at a glance while
hiking:

- a single click changes the top-level screen;
- a double-click enters interaction with the selected screen;
- every interaction has an explicit `BACK` path and cannot accidentally walk into another top-level screen;
- STATUS expands the compact chrome into readable, honest node/team information and carries a MeshRoute mark;
- TEAM uses the best peer identity already known and, when authenticated locations permit it, shows useful
  distance and geographic bearing.

The existing top status strip and left navigation rail remain persistent chrome. This note redesigns the normal
body and its interaction, not the strip's established meanings.

## 2. As-built facts that bound the design (verified 2026-08-20)

- Panel geometry is 128x64 pixels. The top strip occupies `y=0..8`, its rule is at `y=9`, and the navigation rail
  occupies `x=0..9`, `y=10..59`.
- The normal body begins at `x=12`, is 116 pixels wide and has five small-font baselines at `y=19,29,39,49,59`.
  The fixed 6x10 font therefore permits 19 columns in a full-width body row.
- The strip already carries mail count, home-confirmation age, route-derived team count, team-key presence and
  battery voltage. Those compact facts remain visible on every ordinary screen.
- The TEAM snapshot already carries a team-local id, a resolved label, route age and hops. Its label resolver is
  `team_key_of_id -> peer_name_find -> 0x<hash> -> bare id` (`src/firmware_ui.cpp`,
  `label_for_team_id`).
- A peer name is optional. It may accompany a verified public-key exchange and is stored beside that key
  (`Node::peer_key_set`), but possession of a key does not guarantee that a name exists. A locally assigned
  `peername` is not, merely by being typed, a network broadcast.
- **Correction to the original discussion:** an authenticated, hash-keyed, RAM-only peer-location cache already
  exists (`Node::peer_loc_set` / `peer_loc_find`, `lib/core/node_hashlocate.cpp`). It is populated from an
  authenticated location-bearing sealed DM (`lib/core/node_mac_rx.cpp`) and from an attributed encrypted team
  channel post (`lib/core/node_channel.cpp`). Opportunistic distance display therefore needs no new wire type.
- Location is not continuously broadcast. A UI that requires continuously fresh teammate positions would be a
  separate airtime, privacy and wire-policy feature. This redesign must not create such traffic implicitly.
- The current gesture model is not uniform: a short press walks TEAM and INBOX rows, while SETTINGS already has a
  passive closed view, a double-click entry and a `BACK` row. TEAM and INBOX are the principal migrations.
- Emergency long-press behavior and the consumed wake press are safety/power overlays and remain exceptions to
  the normal navigation rules.

## 3. Owner-approved navigation contract

### 3.1 Passive top-level screen

- **Single click:** move to the next available top-level screen.
- **Double-click:** enter interaction with the current screen, if it has an interaction.
- A single click never moves a cursor within a passive list preview.
- STATUS is non-interactive in this version. A double-click on STATUS is a no-op until a useful status-detail
  interaction is deliberately designed.

The canonical top-level cycle remains the rail order, skipping unavailable features without moving the remaining
rail slots:

```text
STATUS -> TEAM -> INBOX -> SEND -> SETTINGS -> STATUS
```

### 3.2 Interactive screen

- **Single click:** advance the selection, page or choice inside the current interaction.
- **Double-click:** activate the selected item.
- Every interactive list or modal exposes an explicit `BACK` action.
- `BACK` returns to the passive form of the **same** top-level screen.
- Reaching the last row never exits to the next screen and never wraps into an unrelated action.
- Selection remains keyed by stable identity, not row index: team-local/hash identity for a teammate and
  `(kind, seq)` for an inbox record. A disappearing item is refused loudly, never rebound to its replacement.

TEAM therefore gains an interactive member selector with a `BACK` row. INBOX gains the same passive/interactive
split before its existing detail view. SEND already has `back, don't send`; SETTINGS and provisioning already
have contained `BACK` paths; these existing paths must be reconciled with the same parent-screen rule.

### 3.3 Inactivity and panel blanking

Panel blanking is a power action, not navigation:

- blanking preserves the current interaction and its stable selection;
- the consumed wake press restores the same interaction;
- only explicit `BACK`, a completed deliberately terminal operation, or another separately specified safety
  transition may retire it;
- no attention timeout silently discards a draft, detail selection or compose choice.

The implementation spec must audit existing compose/detail timeout behavior against this rule rather than assume
that all current screens already comply.

## 4. STATUS body — own node and team at a glance

STATUS describes **our node and our current evidence**. TEAM is the separate teammate overview. Repeating selected
strip facts in readable words is intentional: the strip is compact chrome; STATUS explains it.

### 4.1 Reserved MeshRoute mark

Reserve a permanent **24x24-pixel logo slot**:

- slot: `x=12..35`, `y=12..35`;
- placeholder: `draw_rect(12, 12, 24, 24)`;
- preferred final asset: native 24x24 monochrome XBM (72 raw bitmap bytes);
- minimum accepted asset: native 16x16 monochrome XBM (32 raw bitmap bytes), centred at `x=16..31`,
  `y=16..31` inside the reserved slot;
- do not scale a bitmap at runtime;
- use the established repository-owned XBM bit order and `draw_bitmap` seam;
- draw the mark only in the STATUS body; it does not displace the top strip or rail.

The 24x24 reservation is permanent even if the first artwork is 16x16. Replacing the asset must not move text.

### 4.2 Text geometry and contents

Text beside the logo starts at `x=40`, leaving 88 pixels, or 14 small-font columns. The first three baselines use
that narrowed area. The final two baselines use the full body origin `x=12` and all 19 columns.

```text
top strip (unchanged)

rail  +------------------------+
      | [24x24] TEAM 3D9348A5  |
      | [ LOGO] ME T220        |
      | [     ] 4 HEARD        |
      | 3 NEW / HOME 42s       |
      | 52.123,21.456          |
      +------------------------+
```

Normal substitutions state facts rather than manufacture a global `READY` or `CONNECTED` state:

```text
NO TEAM
NO TEAM KEY
HOME UNKNOWN
NO LOCATION
CFG UNSAVED
RESTART NEEDED
```

Semantics:

- `TEAM 3D9348A5` is the full eight-hex-digit team id and fits beside the logo.
- `ME T220` is our team-local id. With no team, show `NO TEAM` rather than a plausible zero id.
- `4 HEARD` is the route-derived `team_total` known to this runtime. It must not say `4 MEMBERS`: the route
  table is not an authoritative membership roster.
- `3 NEW` is the combined capped unread count. The implementation spec defines its saturation token and prevents
  wrap or clipping.
- `HOME 42s` is the age of the last bidirectional home confirmation. Use the established age formatter and its
  unknown token; do not rename route or beacon evidence as confirmation.
- Show configured own coordinates compactly to approximately three decimal places. With no fix, show
  `NO LOCATION`. Account for signs and never turn `(0,0) = unset` into a plausible fix.
- A persistent actionable condition such as `RESTART NEEDED` may own the final row. The implementation spec
  defines deterministic priority when it competes with coordinates; no existing fact disappears accidentally.
- The SETTINGS rail badge remains the persistent unsaved/conflict indicator on all screens. STATUS may spell out
  an actionable condition but must not create a second configuration-state authority.

## 5. TEAM body — passive overview and interactive member selection

### 5.1 Identity label

For each teammate use the existing resolution order:

1. peer name, when present beside the resolved hash/key;
2. shortened hash;
3. team-local id.

Names are shortened only for presentation. Selection and sending continue to use stable stored identity, never
the shortened text or visible row index.

### 5.2 Fixed row format

Reserve the 19 columns as:

```text
marker  NAME6  AGE3  DIST4  DIR2
```

Examples:

```text
 ANNA    2m 1.2k NE
>BOB    18m  850m  W
 BACK
```

A practical formatter is:

```text
%c%-6.6s %3s %4s %2s
```

A passive preview uses a blank marker; interactive selection uses `>`. Prove all expansions fit. If a field is
too wide, shorten it at its semantic formatter—never through a blanket renderer clamp that makes width tests unable
to fail.

### 5.3 Age semantics and ordering

The available teammate age derives from the selected route candidate's `last_seen_ms`. On a multihop path it is
route-evidence age, **not necessarily when this device directly heard that teammate**. Documentation must use
`route age`, `known age` or equally honest wording, never promise direct RF contact or general connectivity.

The original worst-first idea remains a candidate but is **not yet an owner ruling**. Sorting is deferred until
the implementation spec establishes unknown/stale semantics and cursor stability. The navigation slice must not
silently re-sort as a side effect.

### 5.4 Distance and direction

Show distance only when:

- our own location is configured;
- team-local id resolves to a non-zero peer hash;
- `peer_loc_find(hash, ...)` returns an authenticated cached location;
- cached location age is no greater than **10 minutes**.

Ten minutes is the approved initial freshness limit and becomes one named UI policy constant with boundary tests.
Beyond it, blank both distance and direction; do not retain an old coordinate with current-looking units.

Distance tokens remain at most four columns, for example `850m`, `1.2k`, `12k`, plus a deliberate far/overflow
token. The implementation spec chooses and tests rounding and saturation. Compute the approximation in a pure,
host-tested helper.

Direction is an eight-way geographic bearing (`N`, `NE`, `E`, `SE`, `S`, `SW`, `W`, `NW`) from our
coordinate to the peer's last reported coordinate. It is not movement direction and is not relative to how the
Heltec is facing. Use cardinal text, not an arrow implying unavailable compass/heading hardware.

No cache hit means blank distance/direction, not zero metres. Rendering TEAM must not cause any location request,
message or periodic broadcast.

## 6. Recommended implementation slices

1. **Navigation consistency:** passive/interactive TEAM and INBOX; single-click top-level navigation;
   double-click entry; contained `BACK`; stable identity and interaction retention over blanking. Audit SEND,
   SETTINGS, detail, result and provisioning against the same rule.
2. **STATUS geometry/content:** reserve and probe the 24x24 slot, draw the placeholder, and migrate the five body
   facts with explicit priority and width proofs. Do not wait for final artwork.
3. **TEAM identity/row format:** retain the name/hash/id resolver and add the bounded row presentation.
4. **Location projection:** expose the existing peer-location cache through the frozen UI snapshot; add pure
   distance, bearing and freshness helpers; render only authenticated fresh values. No wire change.
5. **Artwork replacement:** replace the placeholder with the owner-provided native bitmap and repeat visual,
   flash and timing gates. This is an asset change, not a geometry redesign.

Any continuously refreshed teammate-location mechanism is a separate future specification covering airtime,
privacy, authentication, freshness and user control.

## 7. Gates the implementation spec must make concrete

- Gesture matrix for every top-level screen in passive and interactive mode, including blank/wake and `BACK`.
- Negative controls proving a passive short press cannot move a row and an interactive short cannot leave its
  parent screen.
- Stable-identity mutations for TEAM and INBOX: reorder/disappearance never activates a replacement.
- Pixel/column probes for the 24x24 slot, `x=40` narrowed text and two full-width lower rows.
- Placeholder/final XBM linkage through the real page-buffer board canvas.
- Name/hash/id fallback matrix, including key-without-name and id-without-key.
- Location matrix: either fix missing, exact ten-minute boundary, one tick beyond it, negative coordinates,
  identical points, long distance and all eight bearings.
- Proof that rendering TEAM emits no radio traffic.
- Bounded clock-driven repaint without waking a blank panel merely because a token changed.
- Per-OLED-board RAM/flash attribution and board-canvas probe update; non-OLED builds remain feature-neutral.
- Metal checks for logo legibility/alignment, coordinate readability, member rows at arm's length, and interaction
  retention across blank/wake.

## 8. Remaining decisions before implementation-spec approval

1. TEAM order: existing, worst/stalest first, or another measured rule.
2. Exact distance rounding/saturation tokens.
3. STATUS priority when an actionable condition competes with coordinates.
4. Whether a terminal result contains selectable `BACK` or returns to its passive parent after a separately
   defined acknowledgement gesture. It may not silently enter another top-level screen.
