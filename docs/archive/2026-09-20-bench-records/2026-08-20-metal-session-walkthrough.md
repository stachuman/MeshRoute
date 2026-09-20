<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Metal session WALKTHROUGH — follow top to bottom · 2026-08-20

**One document. Every step has its commands and expected output inline — no jumping to Part numbers.**
ⓘ Assembled from `docs/2026-07-31-bench-test-script.md` (Parts 22, 24, 25, 28, 29, 30), which **remains the
authority (M2)** — if anything here disagrees with it, the script wins and this walkthrough gets fixed. The small
`(P xx.x)` tags are for the record sheet only; you never need to open them.
**Hardware:** 2 × Heltec (ESP32-S3, OLED) = **H1, H2** · 1 × XIAO ESP32-S3 = **X**. Optional extras at the end.
**Record per checkbox: PASS / FAIL / NOT-RUN + reason. Failures ship with their console lines verbatim (D3).**

---
## PHASE 0 — build, flash, archive

1. ☐ Working tree clean where it matters: `git status --short -- src/ lib/ variants/ platformio.ini tools/` ⇒ empty.
2. ☐ ★★ **Pick the fresh-chip node (suggest X) and FULL-ERASE it first: `esptool.py erase_flash`** (or the
   PlatformIO erase target). ⛔ `factory_reset confirm` does NOT reopen this window — only a full erase does.
3. ☐ Build + flash: `pio run -e heltec_mobile -t upload` (H1, H2) · `pio run -e xiao_esp32s3 -t upload` (X).
4. ☐ Archive per node: `firmware.elf` + `firmware.map` + the `.bin` actually written, plus `COMMIT.txt`,
   `DIRT.txt`, `SHA256SUMS` → `~/MeshRoute-artifacts/soak-<stamp>/`.
5. ☐ On each node `version` ⇒ ⛔ **STOP if any banner is `nogit` or names a different commit.**

## PHASE 1 — the ONE-SHOT fresh-chip check, on X, FIRST (P 28.4) - confirmed OK

⛔ The first save of ANY record (`cfg set`, `regen`, a join…) closes this window forever.

6. ☐ On the freshly-erased X, **the very first command**: `joinprofile list`
   ⇒ **`> joinprofile NO PROFILES`** — ⛔ never the `STORAGE FAILURE` line. (A fresh chip must read as fresh,
   not broken — this is the only check that reaches the ESP32 "namespace never written" classifier.)

## PHASE 2 — baseline (all three nodes) - confirmed OK

7. ☐ `version` · `cfg` · `status` · `routes` on H1, H2, X — record them.
8. ☐ `debug on` on H1 and H2 (several later checks read `»tx BCN` / DAD traces, invisible otherwise).

## PHASE 3 — the `/mrjoin` store over real flash (on H1) (P 28.1-28.5)

9.  ☐OK `joinprofile list` ⇒ `> joinprofile NO PROFILES` (ordinary absent state).
10. ☐OK `joinprofile set 1 layer=4 freq=869.4625 bw=125 sf=9 name="hut"` ⇒ `> joinprofile set 1 ok`, then
    `joinprofile list` ⇒ `> joinprofile 1 layer=4 freq=869.4625 bw=125.00 sf=9 name="hut"`.
    ★★ **The four decimals ARE the check**: a store that kept kHz would answer `869.462` or `869.463`.
11. ☐OK Repeat the identical `set` ⇒ **`> joinprofile set 1 unchanged`** (the no-rewrite coalescing guard).
12. ☐OK **Power-cycle H1**, then `joinprofile list` ⇒ slot 1 still exactly as above (the only step that proves the
    real backend).
13. ☐OK Strict index: `joinprofile clear 2junk` and `joinprofile set 1x layer=4 freq=868 bw=125 sf=9`
    ⇒ **`> joinprofile err bad_index`** both times, and `joinprofile list` shows slots UNCHANGED.
    `joinprofile list extra` ⇒ the usage line, ⛔ not silent acceptance.
14. ☐OK `joinprofile clear 1` ⇒ `ok`. `joinprofile reset` (no confirm) ⇒ `> joinprofile err needs_confirm …`,
    nothing written. `joinprofile reset confirm` ⇒ `ok`/`unchanged`.
15. ☐OK `joinprofile set 1 …` (any valid), then `factory_reset confirm`, then `joinprofile list`
    ⇒ **`NO PROFILES`** — factory reset takes `/mrjoin` with it (owner-ruled; unlike `/mrfault`).

## PHASE 4 — team up, then everything that NEEDS the team

16. ☐ Provision H1+H2 into one team (exported key). Confirm two-way `rx BCN` and a team route each way.
    ⚠ **[[B230]] (found live 2026-08-20): after step 15's `factory_reset` the node's `sf_list` is EMPTY and
    `team new` is a DEAD-END** — it refuses `incomplete PHY`, its suggested inline command fails the same check,
    and `sf_list=` is not a `team new` key. **First run `cfg set sf_list 6,7`** (also the old §5a precondition
    value), then `team new freq=… sf=… bw=…` works. The refusal itself is correct (C2); the remedy text is the
    registered bug.

### 4a — the status strip on glass (P 24.1-24.2)

17. ☐OK Cycle all five screens on H1. Top row, left to right: **envelope+count · house+age · people+count · key ·
    battery outline+volts**, above the full-width rule, body untouched below.
    ⛔ Fail on: overlapping glyphs · a token off the right edge · icons that MOVE when the mail count passes 9
    and 99 · a smeared battery outline.
18. ☐OK Slot truth-check against the console:
    - envelope: one DM + one channel post in ⇒ `0 → 1 → 2`; opening one message does NOT clear it; one fully
      drawn INBOX list returns it to `0`;
    - house+age = `mobile status`'s home-link state/age (⛔ a team message or foreign beacon must not refresh it);
    - people = `routes` / TEAM row count (no team ⇒ `--`, team but nobody heard ⇒ `0`);
    - key = `team_ch_key` present/absent;
    - battery vs multimeter (one-sided window, no percentage anywhere).

### 4b — the rail, badge and 19-column body (P 25.1-25.5) - confirmed OK

19. ☐OK Cycle all five screens: a one-pixel box around exactly ONE rail icon — the current screen's
    (top→bottom **STATUS · TEAM · INBOX · SEND · SETTINGS**). ⓘ Screens no longer print `STATUS`/`SETTINGS`
    headings — that is the design, not a fault.
20. ☐OK Modal mapping: open an inbox message ⇒ **INBOX stays boxed** through detail + `MESSAGE GONE`. From TEAM,
    send a canned DM ⇒ **SEND is boxed** for the pick-list AND the result — ⛔ not TEAM.
21. ☐ ★★ **Emergency (safety check):** long-press arm, fire an alarm; let one reach `NOT RELAYED`; cancel one.
    On EVERY emergency screen: **no rail at all**, strip still present, and each headline (`RELEASE!` /
    `SENDING...` / `BLOCKED` / `PICKED UP` / `NOT RELAYED` / `REPLY` / `CANCELLED` / `FAILED`) rendered
    **complete**, hard against the left edge.
22. ☐ SETTINGS badge (read at arm's length; states must be distinguishable):
    | do this | badge | SETTINGS must ALSO print |
    |---|---|---|
    | fresh boot, never opened SETTINGS | plain gear | — |
    | edit a value, don't save | gear + dot | `CFG* UNSAVED` |
    | with that draft open: `cfg set e2e_dm 1` over serial | gear + exclamation | `CFG! RELOAD` |
    | conflict AND unsaved together | the exclamation (conflict outranks) | `CFG! RELOAD` |
    ⛔ Fail on an icon-only error (badge changes but SETTINGS states no remedy in words).
    ⓘ The `RESTART NEEDED` arm needs `-DMR_UI_BLE_ROW=1`, which no env sets ⇒ **not-run with that reason.**
23. ☐OK 19-column body: open a ≥120-byte inbox message — two 19-char rows, `n/N` counts every page, the LAST page
    really is the tail (no bytes missing between N-1 and N). TEAM: a long teammate name stays clear of the rail;
    a departed teammate reads `TEAMMATE GONE, pick` **complete**.

### 4c — physical TX-completion (P 22.1-22.2, all three nodes) - confirmed OK

24. ☐OK USB serial on H1, H2 in range: `send <H2-id> "aired probe"` ⇒ in order:
    ```
    AIRED ctr=<n> dst=<id>
    ACKED ctr=<n>
    ```
    (ⓘ `AIRED` may interleave/repeat — fine. ⛔ Never an `ACKED`/`FAILED` for a ctr with NO `AIRED` at all.)
25. ☐OK `send_channel 0 "aired probe" -t` ⇒ **`AIRED ctr=<n> dst=0`**, `<n>` = the FULL handle from
    `ack:queued ctr=<n>`, ⛔ not its low byte.
26. ☐ ★ Negative: leave **X relaying only** for a minute ⇒ its console stays **silent of `AIRED`** (a relay owns
    no origination).
27. ☐OK Panel: SEND → `double` → `double` on `Got your message` ⇒ `SENDING...` → (maybe brief `QUEUED`) →
    `SENT, waiting` → within ~36 s `PICKED UP` / `NO RELAY HEARD`. ⛔ Fail shapes: STUCK on `QUEUED` (new) or
    stuck on `SENDING...` (old). Repeat for a DM ⇒ … → `DELIVERED to <label>`.
28. ☐ ★ Power H2 OFF, post again ⇒ `QUEUED` → `SENT, waiting` → `NO RELAY HEARD` (⛔ never stuck on `QUEUED`
    with no peer). Power H2 back on.

## PHASE 5 — OLED team create (on H1) (P 29)

⓵ ⚠ This phase REPLACES team membership — 4a-4c are done, so that is now safe. Re-export the current key first
if you want the old team back afterwards.

29. ☐OK SETTINGS → `>PROVISION` → `double` ⇒ child menu `>CREATE TEAM` / `JOIN NETWORK` / `BACK`.
    `double` on **JOIN NETWORK does NOTHING** (that flow is exercised in PHASE 6's build state — here it just
    must not act).
30. ☐OK `double` on CREATE TEAM ⇒ `CREATE NEW TEAM`, `REPLACES <6 hex>` **only because H1 is in a team**, cursor
    on `>BACK`. `double` (BACK) ⇒ back to the child menu; `cfg` ⇒ ` team=0x…` **unchanged**, and ⛔ **no
    `»tx BCN` burst** on the console.
31. ☐OK Enter again, `short` (→ CREATE), `double` ⇒ panel **`TEAM CREATED` / `0x<8 uppercase hex>` /
    `<6 uppercase hex>` / `press = back`**. ★ The 6-hex row must be the LAST SIX of the 8-hex row. Console `cfg`
    ⇒ ` team=0x<same 8 hex> team_local_id=<n>` (`(team-DAD pending)` until DAD lands).
32. ☐OK ★★ **PHY divergence (metal-only):** `mobile register freq=869.100 sf=7 bw=125` (retunes LIVE, persists
    nothing), then OLED CREATE again ⇒ panel **`PHY DIFFERS` / `USE SERIAL`**; `cfg` unchanged, no key change,
    ⛔ no DAD burst. **Then REBOOT H1** — the radio is live-retuned off the persisted PHY until you do.
33. ☐OK Durability: after a successful create, power-cycle ⇒ `cfg` shows the same ` team=0x…`, `team exportkey`
    the same keypair, SETTINGS shows **no** `CFG! RELOAD`. Note `team_local_id=<n>`; power-cycle again ⇒ same
    `<n>`, no re-DAD burst.
34. ☐OK Restore/confirm the H1+H2 team you want going forward (create fresh or import the exported key), two-way
    `rx BCN` again.

## PHASE 6 — OLED static join (P 30) — H2 = joiner, X = static peer

35. ☐OK **Make X static: `factory_reset confirm`** (`team 0` does not demote — erase is the only path), then on X:
    `create layer=17 freq=869.4625 bw=125 sf=7 sf_list=6,7 duty=1 name="Layer 17"`. Wait for adoption; record
    its non-zero id as `<PEER_ID>`.
36. ☐OK On H2 build a sparse profile list:
    `joinprofile reset confirm` · `joinprofile set 1 layer=4 freq=868.5 bw=125 sf=9 name="old"` ·
    `joinprofile set 3 layer=17 freq=869.4625 bw=125 sf=7` · `joinprofile list` ⇒ only slots 1 and 3; slot 3
    exactly `layer=17 freq=869.4625 bw=125.00 sf=7`.
37. ☐OK Gate first: edit one SETTINGS value on H2, don't save, activate PROVISION ⇒ panel `SAVE OR DISCARD`,
    provisioning does not open, no J frame. `DISCARD`, then PROVISION opens. (Optional conflict arm: unsaved
    edit + a serial `cfg set` ⇒ `RELOAD OR DISCARD`, ⛔ never a SAVE suggestion.)
38. ☐OK PROVISION → JOIN NETWORK ⇒ list shows `old`, **`PROFILE 3`** (★ the stored slot number, not its list
    position), `BACK` — slots 2/4 absent. Open PROFILE 3 ⇒ the panel shows ALL of:
    ```
    PROFILE 3
    L17 SF7 BW125.00
    869.4625 MHz
    >BACK
     JOIN
    ```
39. ☐OK With BACK selected, `double` ⇒ back to the list; `cfg`, `whoami`, live PHY, `joinprofile list` all
    unchanged; **no outbound J CLAIM** (the discriminator is a J claim, not the aggregate TX counter).
40. ☐OK Open PROFILE 3, `short` (→ JOIN), `double` ⇒ immediately: panel **`JOINING`** (⛔ never `JOINED`/`ADOPTED`
    yet); console: record saved → retune to 869.4625/SF7/BW125 → a J CLAIM after the listen window.
    ⓘ **EXPECTED, NOT A FAULT (confirmed on metal 2026-08-20): after ADOPTED the joiner "storms" `»tx BCN` at
    ~5 s ±20% for ~60 s** — the deliberate post-reprovision DISCOVERY burst (`restart_discovery`,
    `node.cpp:822`; exit at `discovery_ms` = 60 s, checked on every timer fire; no console line marks the exit —
    that emit is sim-only). It then settles to ONE beacon per ~5 min — the TEAM steady cadence
    (`team_beacon_period_ms` = 300 000, spec Change A), ⛔ **not** `beacon_ms` = 15 min, which only a
    team-less static node uses. ⛔ Fast beacons persisting past ~2 min = a real fault, register it.
    ⓘ A stale `m[0] hash=… DIRECT age=…` hosted-mobile row on the static peer is step-32 residue
    (`mobile register` from the PHY-divergence check) — it ages out at 1500 s.
41. ☐OK **Blank/wake:** hands off for 16-18 s (panel blanks at 15 s while DAD continues), then `short` ONCE ⇒ the
    press is consumed as wake only — still `JOINING`, not back at the menu.
42. ☐OK Wait for real adoption (~23 s normally) ⇒ panel **`ADOPTED` / `node <N>` / `press = back`**; console
    `join_adopted` with the same non-zero `<N>`; `whoami` agrees.
43. ☐ ★★ **Layer-17 discriminator:** `cfg` retains **full layer 17** while `whoami`/wire filtering uses
    **leaf 1** — and the screen still completed. ⛔ A console-adopted node whose panel stays `JOINING` forever is
    the full-byte-vs-nibble regression.
44. ☐OK Start the same join again; as soon as `JOINING` shows, `short` **before the blank** ⇒ child menu returns
    instantly, nothing cancelled, no second write. Watch the console: DAD continues and adopts. ⓘ The panel does
    NOT jump to `ADOPTED` on that late adopt (declared behaviour — a push never navigates); `whoami`/`cfg` show
    the join completed.
45. ☐OK Durability + service: power-cycle H2 ⇒ same layer 17 / leaf 1 / PHY / id. **While that boot settles, the
    boot's own DAD re-adopt must change NOTHING on the panel** (no uninvited `ADOPTED`). Then DM H2 → `<PEER_ID>`
    and back — application delivery both ways, not just an intermediate ACK. Re-open JOIN NETWORK ⇒ slot 3 still
    renders its four decimals.
46. ☐ Conditional only — ⛔ do not manufacture any of these: `STILL JOINING` appears only if a real
    collision/retry keeps DAD past 60 s (then: let it blank, `short` once ⇒ `STILL JOINING`, never a failure;
    otherwise record **not-run: adoption completed <60 s** — the host probe is the mandatory control).
    Unreadable `/mrjoin` and forced save-failure: not safely reachable — record not-run.

**STOP RULES for this phase — stop and preserve logs+ELF if:** BACK emits a J claim or changes config · the panel
says `JOINED`/`ADOPTED` before the console adoption · layer 17 adopts on console but the screen never correlates ·
one wake press EXITS the waiting screen · leaving `JOINING` cancels DAD or a late adopt steals the screen · the
node reboots into a different layer/PHY/id · either DM direction fails.

## PHASE 7 — ⚠⚠ the `/mrjoin` POWER-CUT — LAST, the only destructive step (slice-7 gate)

47. ☐ On H2: `joinprofile reset confirm`, then `joinprofile set 1 …` with recorded values.
48. ☐ Issue a DIFFERENT `joinprofile set 1 …` and **cut power at varying delays, ~5 attempts** (immediately →
    ~1 s after enter).
49. ☐ Every boot: `joinprofile list` ⇒ the **complete OLD or complete NEW** slot. ⛔ **`PROFILE STORE INVALID`
    after a cut = the torn-record FAIL** (honest detection is not a pass).
50. ☐ ⓘ These nodes are ESP32/NVS (the robust arm). The nRF52/LittleFS arm (the risky remove-then-write path):
    **not-run with the hardware reason** unless an nRF52 node is on the bench.
51. ☐ Finish: `joinprofile reset confirm` + re-seed if profiles are still wanted.

## PHASE 8 — separate / optional (record not-run with reasons if skipped)

- ☐ **`gateway_heltec` image** (a separate flash of H1): rail shows STATUS/INBOX/SETTINGS only, TEAM+SEND slots
  EMPTY at unchanged heights (⛔ not packed up) · SETTINGS lists **no PROVISION row at all** (walk the whole
  menu) · `joinprofile list` ⇒ `> err gateway_build (joinprofile is normal-node only)` · the house strip-slot is
  BLANK, not a crossed house.
- ☐ **nRF52 node, if you bring one:** the `stackhw` figure (its previous pass predates B209-B212 and does not
  carry over) · the `/mrjoin` power-cut on LittleFS (the risky arm of PHASE 7).
