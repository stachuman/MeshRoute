# MeshRoute durable decisions

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
- **Remote-admin Slice 4 advance preparation (2026-09-06):** owner reports Slice 3 still running;
  the preceding Slice 3 preparation entry is historical, not a new implementation verdict.
  `docs/superpowers/plans/2026-09-06-radmin-slice4-controller-stores.md` is DRAFT/non-dispatchable:
  exact base, delivered helpers/extractor/census and starting pins await the owner's Slice 3 closure.
  R-RA-30 now settles client public list/show on secured BLE, USB-only secret/book mutations and a
  one-off xiao_mobile fixed-identity measurement (not a third ruled board). Author contracts pending QA:
  368-byte ten-seed keyring, one client-only 2056-byte public target-book scratch (32 × 64-byte rows),
  admin-target family with eight-slot pages; strict CLIENT axis on real mobile profiles and a second
  complete heltec_mobile real-router probe arm. Destination hints exclude the origin (0 or 1–3 hops).
  Client regen adds the exact warning in the brief; source-bound debt predicates have no live producer
  before Slice 8a. Parts 55b/56 and Part 59's mobile extension are draft/not run. B320 is closed after
  QA corrected its full_* host/profile claim in place. Preliminary QA gate: otherwise PASS pending
  closure; sole B321 fold-in applied — directly include monocypher.h in the parser and use a function-local
  crypto_wipe scope guard, no NV/team-keyring include or shared-guard relocation (C1). B321 stays open
  until implementation QA; any gateway flash cost is separately attributed. Final re-gate waits for §1.
  No code/tool edit, Slice 3 closure, implementation gate or owner commit is claimed by this preparation.
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
