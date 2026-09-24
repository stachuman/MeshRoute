# MeshRoute durable decisions

- **Evidence retention (2026-09-20):** receipts and reusable instruments stay in Git; new raw runs use ignored `artifacts/` or an explicit durable external archive. Historical run recovery and retention rules live in `docs/superpowers/evidence/README.md`; preserve flashed ELFs and uncommitted inputs before cleanup.

- **Metal qualification (2026-09-20):** `docs/2026-09-20-metal-test-plan.md` is the sole current bench procedure/result authority; the 69-Part library and seven companions are archived with compatibility stubs and a complete disposition map. Owner-approved rig: two Heltec V3s, XIAO ESP32-S3, Heltec V4, XIAO nRF52/SX1262; an identified dirty snapshot is valid, commits are not blockers.

- **Remote-admin v2 — status lives in TWO homes only (owner P5 ruling 2026-09-16):** the design's §19.1 table
  (`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md`) and the register §0 + rows
  (`docs/2026-07-30-open-bug-register.md`); rulings R-RA-1..50 in `docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md`;
  queue pointer in `tracker.md`. As of 2026-09-18: everything through **7b-3** is committed (`6086152`); the disruptive
  arc is complete (R-RA-41); **8a+8c (paired) is independent QA PASS 2026-09-18, owner commits `c07b77f` /
  simulator `6585649`**; **8b (the mobile controller carrier) is independent QA PASS 2026-09-18, owner commit `84edd3e`**
  (brief revisions 1–6, R-RA-46..49; the product path is software-complete end to end; Part 57c landed); **Slice 9
  (legacy deletion + durable protocol docs) is independent QA PASS 2026-09-19, owner commit `4ad9c34`** (R-RA-50; the
  legacy `rcmd`/password/unlock/lock protocol is gone; `frames.md`/`protocol.md` §15 document v2); **Slice 10 (standalone NV cleanup,
  R-RA-6; `/mrcfg` v26, 280 → 240; Node 235208/122176/157304) INDEPENDENT QA PASS 2026-09-20 — THE ARC IS
  SOFTWARE-COMPLETE**; residue = the owner's metal parts (57b–57f et al.) + nothing in software — the B434/B435/B436 harness tool dispatch is
  INDEPENDENT QA PASS 2026-09-20 (uncommitted on `e680271`); only the owner's metal backlog remains.
- **Remote-admin 8b durable rulings (2026-09-18):** R-RA-46 B112 does NOT gate 8b — the controller claims only its own
  local `SendDispatch` ("wrapper stored in my TX queue"), never the hop ACK / `send_aired` / the home's `deleg_fail`;
  B112 stays open as its separate core slice. R-RA-47 ONE automatic exact resend at
  `hop_count ? gateway_send_giveup_ms (150 s) : e2e_ack_deadline_ms (60 s)`, then `unknown` at 300 s. R-RA-48 ACK debt
  = cascade burst 5/10/20 s, then dormant (one re-attempt per new request to that target), wiped only by epoch change.
  R-RA-49 the target E2E-ACKs a `-a` request only for admitted / replay / already-acknowledged verdicts (B278 §9.2).
- **Remote-admin 8a+8c durable rulings:** R-RA-42 `remote`/`remote-retry`/`remote-result`/`remote-ack` are
  `controller_local`, USB + secured BLE, table rows landed at the freeze; R-RA-43 B292 closed by the ONE bounded BLE
  write (`tx_line` chunks, never truncates); R-RA-44 automatic retry timing belongs to 8b (8a = manual retry + one auto
  safe rollover on `session_full`); R-RA-45 pointer-free controller block 4512 B, Node 235248 native / 122176 mobile /
  157344 gateway unchanged (the control). Board carrier is a stub (`carrier_unavailable`) until 8b. Rule P7 (B405/B406):
  a brief that adds a `lib/core` TU fences the simulator source list; a brief that removes a symbol fences every user.
- **Remote-admin 7b-3 durable rulings:** R-RA-37 one outstanding disruptive promise per target, ONE row, typed TERMINAL
  `action_busy` 0x08 (codec landed `ac5f9a5`); R-RA-38 remote `prep-restart` stays schedulable, lockout documented
  (Part 57b, 8a warning); R-RA-39 named-family refusal fallback made permanent by R-RA-41 (36 rows refused remotely by design; B395–B398 closed as not required);
  R-RA-40 allocation +80 B ACCEPT-only, Node 230976 native / 157344 gateway, mobile unchanged.

- **Standalone mobile Home redesign (agreements 2026-09-06/07/23/24; revision 2.17 2026-09-24; REVIEWED):**
  `docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md` is a DRAFT, not
  dispatch authority. Owner-agreed: visible own name; no-team join/create entry points; in-team
  communication Home; ordinary team presets already exist (no device report said otherwise); the 17-byte
  single-row cap is wrong — full text over multiple lines, capacity derived from transport/storage; one shared
  editor for names and one-off DM/team text (short next, double choose, equal fixed groups, minimal repertoire,
  no frequency/prediction; long holds stay emergency); explicit
  review before Save/Send; RAM-only drafts; cancel never saves/sends. Navigation (owner 2026-09-23, D1):
  Home is the default in list focus; `MENU` ends every top-level list and opens menu mode on the rail at the
  Home slot; the panel going dark never re-homes (screen, focus and arrow stay); design §6.1. Home (owner
  2026-09-23, D2): name row, team line, three-row list, counts in labels, position in My device, mark to the
  splash; `INBOX` first, `INVITE MEMBER` on Home for key holders, Settings through `MENU` (§6.2–§6.3). Inbox (owner
  2026-09-23, D13/D13b): one newest-first list across DMs and team posts; this session's rows by receive time,
  earlier-boot rows below with `--` ages (§6.8). Review (owner 2026-09-23, D5): every phrase, written message
  and name goes through one review that opens on the safe action (§7.3). Phrase size (owner 2026-09-23, D7):
  163 bytes per slot, ≈ +7.4 KB RAM estimate accepted, to be measured. Phrase record (owner 2026-09-23, D8): no
  migration — MeshRoute is not deployed, no backward compatibility; an old record boots as defaults with a clear
  message. Defaults (owner 2026-09-23, D9): the five existing plus team `Return to base now`, `On my way`,
  personal `Where are you?`, location off. Written messages (owner 2026-09-23, D6): one 163-byte limit for
  every panel message, no location on written messages (classified not location-eligible for the GPS design);
  a located phrase shows `LOC` on its review. Names (owner 2026-09-23, D10): an unnamed device is shown by its
  ID (`0x<HASH8>` where 10 columns fit, else the six-digit member fingerprint, never a clipped hash) and
  advertises no name (core package W1c); 32-byte names with `»`; rename preloads a typeable name; a name prompt
  before JOIN/CREATE only when unnamed, SKIP preselected. Setup from Home (owner 2026-09-23, D11): JOIN/CREATE
  pass the same settings gate; INVITE MEMBER opens without it (changes no settings); exits return to the recorded
  origin; a blocked gate shows a SETTINGS-slot note. Editor (owner 2026-09-23, D4): uppercase, digits, space and
  `. , ? ! -` in seven groups of six, controls under `EDIT`, return to group 1 after each character. Splash (owner
  2026-09-24, D3): the mark for about 1 s after start-up with the build's Git ID under it, dismissible, never on
  wake. Home card (owner 2026-09-24, D12): while this session's newest sealed team post is unread, Home's rows
  1–2 show `FROM T<n> <age> +<n>` and its first 19 bytes; no rows added, cleared by the existing unread rule. All
  decisions are ruled. Independent review 2026-09-24: HOLD with DR-1–DR-8 (card bound to the session arrival
  serial, preset `text_max` and reply buffer, per-caller review table, full-precision Inbox key, wording); the
  scoped re-review closed seven and kept DR-4 (name origins, request states, known refusal vs residual) plus
  minor DR-9–DR-11; the second re-review returned PASS with fold-ins (DR-12 folded into revision 2.17), and
  the owner confirmed the author's disclosed choices. The design is REVIEWED; each §13 package still needs a
  Quality-Agent pre-check, a measured allocation and a brief. B335 stays open until implementation QA. B335 stays open; B440–B448 were registered by this work
  (B446: the GPS design still contradicts preset ruling R-2; B447: a `peername` label is overwritten by the peer's
  next advertised name; B448: `cfg set name` silently shortens names over 32 bytes).

- **Remote administration v2 controller boundary (owner-ruled; design QA-passed 2026-09-04):** the locally
  attached MeshRoute node—not its companion—is the authenticated RPC endpoint. It seals/opens with its
  default `/mrid` identity or one explicitly selected seed-derived dedicated identity from ten persistent
  slots; the companion exchanges plaintext only. Four independent stores own the trust directions:
  `/mrmkeys` controller seeds, `/mracl` controller public keys/roles, `/mradmid` the target's stable
  administration seed, and a 32-row `/mrtargets` public target book—never `/mrpeers`. Ordinary `regen`
  preserves dedicated management trust; `self` grants cannot follow a regenerated controller identity.
  First-owner and target-root provisioning/rotation are USB-only. Authenticated carriers are global-plane,
  AEAD-bind the controller's stable `SOURCE_HASH`, and use carrier-specific packer-derived limits. The
  transmitted control stays one opcode/slot byte; no global `wire_version` bump. Safe session rollover never
  abandons an unacknowledged result; confirmed force rollover may report it unknown. Remote OTA only enters
  a locally reached Wi-Fi/BLE receiver—firmware never crosses MeshRoute. Open `status`/`routes` remain
  explicit, BLE results remain retained until locally acknowledged, mobile builds may originate/transport
  but never accept, and legacy `rcmd` is replaced rather than retained. See
  `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md`.
  R-RA-25 (2026-09-05, implemented by QA-passed 0h): 232 bytes is the one application-DM cap; `SOURCE_HASH`
  is mandatory and supplied/known `DST_HASH` is preserved. Derivable means only the enqueue's two existing
  lookups; a truly unknown by-ID destination may omit its hash but gains no body capacity. Park helpers refuse
  oversize bodies rather than clamp.
  R-RA-26 (owner-ruled 2026-09-05): `defined(ARDUINO)` distinguishes boards from native/lus. Mobile boards
  are client-only; static/gateway boards, including no-profile static envs, are accept-only; native/lus have
  both endpoints. Board checks enforce exactly one endpoint and ACCEPT equal to legacy REMOTE_MGMT until
  Slice 9. Slice 1 is software-complete / QA-passed 2026-09-06, committed at `5d2c00e`: a consumer-free header
  scaffold with no static profile or `platformio.ini` change, measured zero RAM/flash/section/object/symbol
  movement on the ruled pair and 36/36 stream identity after a forced simulator rebuild.
  R-RA-27 (owner-ruled 2026-09-06; Slice 1b software-complete / QA-passed, owner closure commit `cc35137`): strict ACCEPT-for-CMD and
  CLIENT-for-RESP receive ownership; the legacy switch widens neither. MeshRoute is undeployed, so a
  static/gateway legacy issuer losing replies is accepted; its old `rcmd` round-trip bench step is suspended
  from 1b until Slice 9's replacement. Mobiles ignore unowned commands at the existing fail-closed guard.
  Native tests a production-shared pure routing decision with explicit capability values; production passes
  real macros, never runtime role state or test-only overrides. RAM stays unchanged until the later storage
  slice; 1b measured RAM ±0 on both boards, fully attributed flash gateway +16 / heltec_mobile −8, and 36/36
  byte-identical streams after a forced simulator rebuild. Native 2615/111354/0; mutation union 99/99 RED.
  Evidence: `docs/superpowers/evidence/2026-09-06-radmin-slice1b.md`. No new metal for 1 or 1b.
  R-RA-28 (owner-ruled 2026-09-06): always reserve four DST_HASH bytes in the RPC capacity authority, even
  when the legal carrier omits that field; no reclaimed body space. Derive from storage/air-fit minus named
  reserved fields and carrier extras; test admission refusal separately from raw-packer capacity. Attaching
  the known hash on a home's local last-mile sends is only under consideration (B310), not an authorized
  change to R-RA-25's narrow lookup rule and not part of 1b or consumer-free Slice 2. Rulings live in
  `docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md`.
  Slice 2 software-complete / implementation QA-passed 2026-09-06: §8.9's terminal allocation is append-only;
  decoded results retain a typed opcode domain (`0x00` terminal completed ≠ authenticated protocol-error already_acknowledged),
  with independent domain/invalid-code KATs. Native 2640/115288/0; independent reference 87/87; mutation union
  66+5 = 71/71 RED; 36/36 byte-identical anchored streams. Both simulator variants compile the codec, but the
  executable and both ruled board ELFs remain byte-identical; no runtime consumer or HAL change. B308/B309
  are closed by admission-versus-packing and wrapper-depth proof. B312's checked codec entropy boundary is
  complete; current HAL draws still return void, so truthful real-provider integration remains open.
  Evidence `docs/superpowers/evidence/2026-09-06-radmin-slice2.md` and implementation are committed at
  `f2735f7` (both MeshRoute checkouts); simulator integration is committed at `8688884`. Measured dispatch
  bases were `9ea4947` / simulator `fd3295d`; Author closure is committed at `231e1be`. B314 is avoided/closed,
  B315 open, B316 tracked
  by existing B286; B311/B313 remain open. No metal added by Slice 2; older debts are unchanged.
  R-RA-29 (owner-ruled 2026-09-06, committed with QA's Slice 3 pre-check at `7299eb9`): the one v2 key
  fingerprint is BLAKE2b-512 over the 32-byte public key, first eight digest bytes as 16 lowercase hex;
  USB listings also print the full public key. The whole Slice 3 target family is USB-only, refused at
  the ACCEPT-build BLE boundary before the seam; controller-side BLE remains Slice 4's question.
  Slice 3 brief `docs/superpowers/plans/2026-09-06-radmin-slice3-target-stores.md` is DRAFT; QA's single
  inventory-profile HOLD fold-in is applied, awaiting confirmation of the changed sections. It remains
  pinned to existing preparation `7299eb9` and unchanged simulator `8688884`; later preparation commits
  require an explicit Author repin. Author decisions: no resident target Identity/ACL or static I/O buffer;
  40-byte identity and 368-byte ten-slot ACL records; explicit confirm-only invalid recovery, ANY io_failed
  write refused; no remote execution. B317 records destructive self-heal outside ordinary reset preservation;
  nRF52 save is non-atomic. B318 is closed after QA's in-place pre-check corrections. B319 tracks the
  inventory's explicit fifth ACCEPT axis (one on the four static/gateway profiles, zero on both mobile
  profiles), never derived from the legacy macro; generator fixtures and per-profile ownership pins are
  re-derived by the coder. The real-router gate is
  inbox-verbs; console-sink owns help/extracted BLE guard, and both extensions are fenced. Full mutation
  union is changed-source devicenv + three new services, plus unchanged teamkeyring/cfgparse/sliceDtoken
  dependencies. Bench Part 55a is drafted, not run; target half only, controller Part 55b later.
- **Deterministic board measurement (B138/B206 closed after independent QG, 2026-08-28):** build identity has one device-TU authority. Actionable
  RAM/flash comparisons use `tools/measure_board.py` with fixed epoch/revision, the same checkout and stable
  `.pio-measure/` build paths, one runner lock, exact source/toolchain/wrapper manifests, and two matching clean arms
  per measured ABI; ordinary `.pio/` must remain untouched. The lock does not cover source-mutating batteries, which
  remain operationally exclusive. Manifests must bind CC, CXX and LINK independently and an external literal test
  inventory must redden every omitted comparison. A normal `pio run` size line is informational. B253 is the next
  separately reviewed slice (untracked-source provenance + fail-loud Git); B246 board-ABI struct visibility remains
  separate.
- **Hybrid RTS closure authority (B157/B153, 2026-08-27):** B157 is closed on complete flight identity plus the
  restored exact implicit-forward credit. B163 has no separate implementation: B182 supersedes its time-windowed
  alias proposal with configured-logical identity plus separate wire correlation and fail-loud ambiguity. The old
  732/733 absolute delivery floor was frozen before B182 and is not a current gate. The owner accepts 757/1041 once
  to close B153; this is not a permanent floor. Current comparison baseline: raw 760; s06 110/148, s07 84/207, s22
  8/8, s27 15/15; residue census `1/11/44/3`; the 44 unresolved logical destinations are B252 measurement debt.
  Future movement in delivery, decisive rows, airtime, duplicates or residue needs explicit attribution. See
  `docs/superpowers/specs/2026-08-08-hybrid-rts-flight-identity-design.md`.
- **Hosted-mobile counter boundary (B251, 2026-08-27; closed, independent QG passed):** a qualifying plaintext
  static/global transit keeps the mobile counter on the mobile-home hop and forwards once under a newly allocated home
  counter. Direct by-id transit admits the outward queue and any correlation before its hop ACK. Hash-wrapper transit
  reserves correlation before that ACK, then treats `SendDispatch` as the sole outward admission authority: queued
  activates, parked retains the reservation until admission, and refused releases it without origin evidence. The
  broader B112 first-hop-ACK contract remains open. First-hop loop identity includes the verified hosted-mobile hash;
  reverse E2E-ACK correlation also binds the return peer and layer, never evicts a live row, and translates back to the
  mobile counter. Team-plane, ordinary static and CRYPTED traffic are unchanged. See
  `docs/superpowers/specs/2026-08-27-b251-home-counter-translation-design.md`.
- **B197/B198 UI sleep/wake (design pending review, 2026-08-14):** an OLED build may light-sleep only after active-low
  button GPIO wake is armed and the panel is blanked, `InputFsm` is inactive, and `FrameGate` has no open logical
  frame. Non-OLED builds return `true`; retain DIO1/timer wake and one-page-per-service-pass rendering. See
  `docs/superpowers/specs/2026-08-14-b197-b198-ui-sleep-wake-design.md`.
- **Heltec V4 port (V4-4, 2026-08-26; metal validation pending):** `heltec_v4`, `heltec_v4_mobile` and
  `gateway_heltec_v4` support original
  high-power V4.2/V4.3 in 863–928 MHz, detects GC1109/KCT8103L at runtime with a fail-closed two-pull check, enables
  the V4.3 receive LNA, and supports nominal conducted 22 dBm via SX1262 drive 10 dBm. The two derived images add
  only the existing mobile or gateway role flags. Metal proves the shared OLED Vext contract is GPIO36 active LOW on
  both V4.2 and V4.3; the provisional HIGH level fails the V4.3 panel ACK. V4 R8/low-power/433–510, GNSS, calibrated
  power tables and runtime LNA control remain outside this slice. See
  `docs/superpowers/specs/2026-08-01-heltec-v4-radio-port-and-board-rf-seam-design.md`.
- **Roster grant return context (B250, 2026-08-26):** the reused N5/N6 grant chain carries one private, explicit
  caller authority. Invitation-origin navigation stays unchanged; TEAM-roster exits restore the entered roster by
  saved TEAM-local identity through the existing B64 follow/refuse authority. A matching roster-origin pubkey opens
  the existing REJECT-default confirmation. Missing callers fail closed, and unrelated provisioning close or
  emergency pre-emption retires the context; no parent is inferred from window, screen or sentinel fields. See
  `docs/superpowers/specs/2026-08-26-b250-roster-grant-return-context-design.md`.

- **Remote-admin Slices 5 and 6 — software QA PASS (2026-09-07; QA now authors and gates, Codex codes):** Slice 5
  committed `d226189` (target session/admission/on-air bootstrap; B341 fixed in-slice). Slice 6 (shared validator,
  dispatcher context/outcome, ruled authority table + three-artefact checker; B343–B348 preflight fold-ins)
  QA-passed, uncommitted at report: native 2839/121831/0, `lus` unchanged, corpus 36/36, inventory 203 rows, union
  114/0, boards RAM ±0. Register B336–B351 landed; next free B352; open follow-ups B337/B342/B350/B351; bench
  residue Part 63. NEXT: owner commits Slice 6; Slice 7a/7b pre-check + brief.

- **Remote-admin Slice 7a — software QA PASS (2026-09-08):** 7a-0 refactor committed `89071fb`; the feature
  (NV v25 activation delay, budget authority, resolver, cfg key, read-outs, boot line) QA-passed, uncommitted at
  report; eleven preflight/implementation findings folded in and closed (B352–B358, B360–B363); B354 fixed a
  pre-existing unreachable `cfg set gw_announce_interval`; B364 (invite-test over-read) open; Part 57a landed;
  owner veto on the new row's operator class still open. NEXT: owner commits 7a; 7b brief.
