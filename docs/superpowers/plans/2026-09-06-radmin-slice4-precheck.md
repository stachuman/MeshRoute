<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 4 — Quality-Agent pre-check ledger (2026-09-06): mobile controller keyring and target book

Authority: design §19 item 4 (`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md:1911-1914`),
§19.1 row 4 ("mobile keyring/target-book storage and local command owners | zero remote events, 36/36 unchanged;
ruled pair | Bench Part 55b + Part 56"), §6.1 (:273-315), §6.2 (:317-368), §6.3 (:370-395), §6.4 step 3 (:403),
§12.1 physical-only on a client build (:1254-1257), §14 (:1387-1401), §19.2 (the store/regen/`admin-key` bullets
:2024-2033, :2063-2065); rulings R-RA-6/8/21/26/27/29; the Slice 3 pre-check (its §1 persistence-idiom table is the
template and is NOT repeated here) and the Slice 3 brief (`…-radmin-slice3-target-stores.md` §4-§7, whose services,
fingerprint helper, parsers, extractor generalization, census extension and B319 profile-axis idiom Slice 4 REUSES).
Written at `HEAD 7299eb9` while Slice 3 is in flight ⇒ every Slice-3-delivered anchor below is cited by NAME and must be
re-verified by symbol at brief time against the Slice 3 closure commit. Hypotheses, not authority.

## 0. What Slice 4 is, in one line

On CLIENT builds, land the two controller-side stores — `/mrmkeys` (ten seed-only management slots, SECRET) and
`/mrtargets` (32 managed-target rows: full administration public key + bounded label + replaceable routing hints,
PUBLIC) — in the same idiom, with the local `admin-key` family (§6.2) and a target-book family (name the Author's),
seed-derived identity, USB-only secret operations, duplicate-identity and in-use refusals, messaging-peer independence,
and the controller `regen` behaviour. NO on-air RPC, NO session, NO codec consumer, NO Node member.

## 1. Source facts — the client side of the product split

| fact | anchor |
| --- | --- |
| CLIENT = `MR_PROFILE_MOBILE` only: four envs — `xiao_mobile` (nRF52840, the ONLY client with BLE), `heltec_mobile` (ESP32, extends `heltec_v3`), `heltec_v4_mobile`, `xiao_esp32s3_mobile`. Host cells (native, both lus variants) are `{CLIENT 1, ACCEPT 1}` | `platformio.ini:518-541`; `lib/core/mr_features.h:60-67` |
| the RULED client board `heltec_mobile` has NO BLE: `device_ble.h` is an inert no-op off nRF52 ⇒ the BLE arm of Slice 4 (guard/refusal/list-over-BLE) is exercised on metal only on `xiao_mobile`, which is in NO automated gate (the warning census's pinned set is `gateway_heltec / gateway_heltec_v4 / heltec_mobile / heltec_v3 / heltec_v4 / heltec_v4_mobile`; the board pair is `gateway` + `heltec_mobile`) — §6.2 | `src/device_ble.h:5, :24-25`; `tools/warning_census.sh` pinned set; `tools/probe_board_abi.py:104` |
| ⚠ `tools/probe_inbox_verbs` — the instrument that compiles the REAL `firmware_commands.cpp` and drives `dispatch()` / `exec_console_line` — has ONE arm, `[env:heltec_v3]`'s defines (`-DARDUINO=100 -DMR_CONSOLE=1 -DBOARD_HELTEC_V3`, no `MR_PROFILE_MOBILE`) ⇒ under `mr_features.h` that arm is `{CLIENT 0, ACCEPT 1}`: Slice 4's `#if MR_FEAT_RADMIN_CLIENT` arms would be COMPILED OUT of the only real-router gate. Its own header says the arm "must mirror a REAL env or the probe measures a configuration no board builds" ⇒ the honest extension is a SECOND real-env arm mirroring `[env:heltec_mobile]` (`… -DMR_PROFILE_MOBILE`), run for the client rows — §6.4 | `tools/probe_inbox_verbs/run.sh:21-22, :66` |
| the inventory generator's profile table gains `MR_FEAT_RADMIN_ACCEPT` in Slice 3 (B319) but still has NO `MR_FEAT_RADMIN_CLIENT` axis, and `eval_gate` REFUSES an unknown macro ⇒ the first CLIENT-gated dispatch arm needs the twin column (`mobile`/`mobile_oled` = 1, the four static/gateway profiles = 0; a typed table, never derived) — §6.5 | `tools/gen_command_inventory.py:202-209, :735` |
| the ownership census is an exact per-file MULTISET of `MR_FEAT_RADMIN_*` sites; after Slice 3 it is the seven-file set the Slice 3 brief pins; Slice 4 adds CLIENT sites in the SAME glue files (`firmware_commands.cpp/.h`, `fw_main.cpp`, `firmware_help.h`) ⇒ the brief pre-authorises the exact new strings against the Slice 3 CLOSURE census, not against today's three | `tools/probe_features/ownership.py:62-90`; Slice 3 brief §5 |
| the feature probe's client cells are `board_mobile_oled0/1` (+ the host cells); heltec_mobile's OLED value decides which cell the ruled board maps to | `tools/probe_features/envmap.py:30-40` |

## 2. Source facts — the two stores, and the two RESIDENCY precedents they must each follow

| fact | anchor |
| --- | --- |
| `/mrmkeys` holds SECRETS (ten 32-byte seeds) ⇒ the `/mrteams` shape: stack transient + `SecretWipeGuard`, four-state read, one candidate, one save, no eviction, no reader that hands material out EXCEPT the ruled `export` (§6.2) — the single place in the codebase a seed is ever printed, USB-only, from a scope-guarded transient. An all-zero seed is REFUSED as material (dead RNG) and therefore an all-zero row IS the empty row — no occupancy flag needed; a named `reserved[4]` per row keeps the whole-record byte-compare deterministic ⇒ 36-B rows, `8 + 10 × 36 = 368 B` (candidate) | `src/firmware_team_keyring.h:461-465, :507-560`; `lib/core/identity.cpp:52-66`; Slice 3 brief §4.3 (the checked seed source) |
| `/mrtargets` is PUBLIC data ≈ 2 KB ⇒ the `/mrpeers` precedent, not the keyring's: `static mrnv::PeerBlob s_peers` (1160 B, resident `.bss`) was chosen "resident-over-stack" because the nRF52 Arduino loop task has a FIXED 4 KB stack and "this tree has already HARDFAULTED once (`stackhw` down to 72 B)"; the same rule placed the 1.15 KB `PresetCatalog` in `.bss`. ⇒ Slice 3's "no static I/O buffer" was right for 368 secret-free bytes; a 32-row public book of ~2 KB on the console path must be a resident buffer, and its RAM is attributed on the CLIENT board — §6.2 | `src/firmware_commands.cpp:60-65, :172-178`; `src/fw_main.cpp:277-285`, `:1764-1768` (nRF52 mesh task 8 KB, ESP32 loop ~8 KB) |
| the routing hints a row must carry are the two shapes the client carrier (Slice 8b) will feed: `send_by_hash(key_hash32, …, Plane)` (same-layer, plane GLOBAL by R-RA-12 — no plane field is stored) and `SendLayerCmd { hops[gw_env_max_hops=4]; hop_count; dst_hash }` (cross-layer) ⇒ candidate row: `admin_pub[32] · key_hash32 u32 · hops[4] · hop_count u8 · label_len u8 · label[16] · flags u8 · reserved` = 64 B, `8 + 32 × 64 = 2056 B`; rows keyed by `admin_pub` (duplicate refused), routing hints REPLACEABLE, the key IMMUTABLE for a row, full ⇒ loud refusal, "never evict a live/pinned management target" (§6.3); the label is the ONLY label store in remote-admin (the target keeps none, §6.5) | `lib/core/node.h:1643`; `lib/core/command.h:43-44`; design :380-395 |
| ⚠ §6.3 makes the 32-row cost a GATE: "measured on every essential controller ABI before its storage slice is approved… must not silently fall back to 16, reuse `/mrpeers`, or evict". The 0e sheet has NO target-book or keyring candidate (it priced §15's bounded-state tables only) ⇒ the ABI measurement of BOTH new records is Slice 4's own (unguarded `static_assert`s per ABI + a per-board RAM diff), and `heltec_mobile` is the only client ABI any gate measures — §6.2 | `test/radmin_0e_candidate_types.h:83-296`; `docs/superpowers/evidence/2026-09-04-radmin-0e.md:211-232` |
| NV budget: nRF52 InternalFS = 7 pages = 28 KB; ESP32 = the default NVS partition (20 KB; "the ESP32 envs use NVS, not LittleFS"). Records today: `/mrpeers` 1160, `/mrjoin` 104, `/mrteams` 296, `/mrui` 372 (+ `/mrcfg`, `/mrid`, `/mrfault`, inbox meta); Slice 3 adds 408; Slice 4 adds ≈ 2.4 KB ⇒ fits both, but the brief states the sum and `/mrtargets` becomes the LARGEST record on the medium (larger than `/mrpeers`) | `…/InternalFileSystem.cpp:34`; `platformio.ini:184`; `src/device_nv.h:216-217, :270-272, :318-320, :381-383` |
| independence from `/mrpeers` is structural: a different slot, magic, version and service; `/mrpeers` eviction/capacity (`kMaxPeerRecs = 16`, pinned-over-authoritative eviction) never touches the book; `key_hash32` in a row is a HINT the ordinary peer book may also know, never the identity (the identity is `admin_pub`) | `src/device_nv.h:179-217`; design :385-389 |
| `factory_reset` erases both (the `"mr"` namespace / the format), `regen` (`/mrid` only) and `leave` (`/mrcfg` only) preserve both — by construction, exactly as Slice 3 | `src/device_nv.h:395-427, :867-876`; `src/firmware_commands.cpp:672-683`; `src/firmware_config.cpp:2397-2409` |

## 3. Source facts — verbs, `self`, `regen`, and the BLE question R-RA-29 left to this slice

| fact | anchor |
| --- | --- |
| the design's family: `admin-key list · show <self\|keyN> · generate <keyN> · import <keyN> <64hex-seed> · export <keyN> · remove <keyN> confirm` (§6.2); `self` = the node's `/mrid` identity (`g_identity`, `identity_from_seed` at boot); `show self` prints the full `ed_pub` + R-RA-29 fingerprint — the controller-side twin of Slice 3's `admin-id show` (today NO verb prints the node's full 64-hex key; the companion sees it only in the status JSON) | design :336-341; `src/fw_main.cpp:814-822`; `src/firmware_commands.cpp:959-966`; `lib/console/console_json.cpp:582` |
| the target-book family is NOT named by the design (§6.4 step 3 only says the controller "transactionally stores that key and the target's initial routing metadata"); the Author names it (e.g. `admin-target list · add <label> <hex64> hash=<0x…> [layer=<ids>] · set <slot\|label> … · remove <slot> confirm · show`) — labels/hashes/layers via the existing parsers (`parse_hex32` :224, `parse_hex32_0x`-style hash, `parse_index_strict` :129, `parse_confirm_token` :545) | `src/firmware_config_parse.h`; design :403 |
| duplicate local public identities REFUSE: an imported/generated seed whose derived `ed_pub` equals another slot's OR `self`'s (§6.2 "rejected rather than creating two names for one principal") | design :347-348 |
| the in-use refusal ("a slot referenced by an in-flight request or a retained BLE result cannot be replaced or removed") has NO producer before Slice 8a ⇒ a caller-supplied predicate on the service (the Slice 3 "acting slot" shape: present in the seam, driven by a fake, labelled a future-caller test), never a runtime flag | design :348-349; Slice 3 brief §4.2 |
| controller `regen` (§6.2 last paragraph, §19.2): preserves `/mrmkeys` + `/mrtargets` (structural), REFUSES while any source-bound RPC/result/ACK debt is live (no producer before 8a ⇒ the same predicate shape, always clear in Slice 4), and "explicitly reports that old `self` ACL grants cannot follow the regenerated public key" ⇒ `do_regen` gains a CLIENT-gated warning line. ⚠ Part 59 (metal, pending) pins regen's exact success line on BOTH transports and the console-sink/inbox-verbs probes pin its bytes ⇒ the brief states the exact new CLIENT line and re-derives those pins; ACCEPT builds' line stays byte-identical. C1: Slice 3's "preserve `do_regen`'s body" was Slice 3's fence, not a standing rule — §6.6 | `src/firmware_commands.cpp:672-683`; `docs/2026-07-31-bench-test-script.md:4048-4064` |
| BLE: R-RA-29 refused the WHOLE target family and said the controller `admin-key list/show` allowance "is Slice 4's question". The design is explicit and asymmetric: §6.2 "`list` and `show` expose only public keys/fingerprints and may be used through USB or secured BLE. Generating, importing, exporting, or removing secret material is physical USB-serial only… BLE may select an already-installed key for a remote request but cannot create, extract, replace, or delete one"; §12.1 "`remote …` and public `admin-key list/show` are accepted only from local USB or secured BLE; the secret-key operations remain physical USB-only". The Slice 3 extractor compiles the guard's condition text, so a SUB-VERB-aware refusal is expressible; Slice 3's "listing-only escape = failure" control is a TARGET-family invariant, not a rule for this family — §6.1 | rulings R-RA-29; design :342-347, :1268-1271; Slice 3 brief §4.4/§6.4 |
| `help`: `admin-key` (+ the target-book verb) appear on CLIENT profiles only. ⚠ **CORRECTED 2026-09-06 (B320), old claim kept visible:** this row said *"full builds 51 → 53 because the host-shaped `full_*` profiles are `{1,1}`"* — WITHDRAWN: `PROFILE_ENVS` maps `full_oled` → `heltec_v3`/`heltec_v4` and `full_headless` → `xiao_sx1262`/`xiao_esp32s3`/`production`, i.e. REAL STATIC BOARDS with CLIENT = 0; the dual-role `{1,1}` hosts (native, lus) are not profiles of that table at all. ⇒ only `mobile`/`mobile_oled` gain the two names; the all-profile UNION gains two; the four static/gateway profiles are unchanged — exact counts from the B319-style projection once the CLIENT axis exists | `src/firmware_help.h`; `tools/gen_command_inventory.py:202-218, :791` |

## 4. Gates the brief must name explicitly

| gate | what Slice 4 changes in it |
| --- | --- |
| board pair | `heltec_mobile` is the CLIENT board: the resident target-book buffer (+≈2056 B) and the verbs' flash land there and are attributed; `gateway` must be RAM/flash-identical except compiled-in-but-unreferenced pure headers (predict ±0). ⚠ the nRF52 client (`xiao_mobile`, 256 KB RAM under a SoftDevice) receives the same resident buffer and is in NO gate — §6.2 |
| inbox-verbs probe | a SECOND real-env arm (`heltec_mobile`'s defines) for the client rows; the existing ACCEPT arm keeps every Slice 3 row; both arms' pins re-derived |
| console-sink probe | the BLE-guard rows for the `admin-key` split (or the whole family, per §6.1), the help projection on the two mobile profiles, ownership (router vs parser intersection EMPTY on all six profiles) |
| feature census | the exact new CLIENT-site multiset on top of the Slice 3 closure census; controls deleting/swapping/widening each new boundary; both boards preprocessed for presence (`heltec_mobile`) / absence (`gateway`) — the mirror image of Slice 3's proof |
| inventory / help | `PROFILES` gains the `MR_FEAT_RADMIN_CLIENT` axis; rows +N (two families, N sub-verbs, enumerated first); `--write` then bare + `--check` |
| ABI | unguarded per-ABI `static_assert`s for both records + `sizeof`/`offsetof` native pins; `sizeof(Node)` unmoved; NO `PINNED` duplicates |
| mutation | changed-source: full `devicenv` + new per-file targets for the new pure headers; dependency: the Slice 3 batteries whose code is REUSED (the fingerprint helper's `radmin3id`, `cfgparse`, `sliceDtoken`, `teamkeyring`); NOT `radmin3acl`/`radmin3verbs` unless a shared helper is touched |
| corpus / simulator | inert by construction (`src/` only); ZERO post-edit build actions as a checked no-op + the recompile control; 36/36 identical |
| bench | **Part 55b** (the controller half of the 55a exchange: `admin-key show self` on the controller, the target's `acl add owner <that key>`, the controller's target-book row with the target's key from `admin-id show`, both fingerprints agreeing across the two consoles) and **Part 56** (USB seed lifecycle: generate/export/import round-trip reproducing the same public identity, remove confirm; BLE public list/show only, on `xiao_mobile`) — the design names both |

## 5. Shape the brief must pin

- **Records** (`src/device_nv.h`): `MgmtKeyBlob { magic; version = 1 (EQUALITY); count u16; MgmtKeyRow rec[10] }` with
  `MgmtKeyRow { seed[32]; reserved[4] }` (all-zero seed = empty); `TargetBlob { magic; version = 1; count u16;
  TargetRow rec[32] }` with the 64-byte row of §2 (fields, offsets and both magics frozen in the brief); two slot
  entries; two four-state typed wrapper pairs; per-ABI asserts; `static_assert(kMgmtKeySlots == 10)` bound to the
  `keyN` grammar, and NOTHING binding the controller's ten to the target's ten (§6.2: "independent of the target's
  ten ACL slots").
- **Services** (pure headers, `IMgmtKeyStore` / `ITargetStore` seams, the Slice 3 checked seed source and fingerprint
  reused by include, never copied): keyring = generate (absent slot only) · import (absent slot only; derive, compare
  against every other slot AND `self`, refuse duplicates) · export (USB-only; the one seed print) · remove (confirm;
  refused by the in-use predicate) · list/show (public only); target book = add (keyed by `admin_pub`, duplicate
  refused, lowest free slot, full refuses) · set (label / routing hints replaceable, key immutable) · remove (confirm;
  refused by the in-use predicate) · list/show; `invalid` + `io_failed` forbid every ordinary write, an explicit
  confirm-gated reset recovers `invalid` only (the Slice 3 rule, verbatim).
- **Residency**: the keyring is a wiped stack transient; the target book is ONE resident `.bss` blob on CLIENT builds
  (the `s_peers` precedent, named), attributed on `heltec_mobile`; no other resident state, no Node member.
- **Verbs + transport** per §3 and the §6.1 ruling; boot = a read-only two-line report (state + counts, no key
  bytes), CLIENT-compiled, beside Slice 3's ACCEPT report; `regen`'s CLIENT line per §6.6.
- **Tests**: every verdict × read state, write-counted; duplicate-vs-self; export round-trip (`import(export(k))` is
  the same public identity); the in-use and debt predicates driven by fakes; the 32-row full refusal never evicts;
  peer-book independence (a `/mrpeers` eviction/put leaves the book byte-identical); regen preservation structural;
  the real router under the NEW client arm; the BLE split executed by the extractor; both boards' presence/absence.
- **Prediction (before editing)**: native +cases; 36/36 + zero build actions; Node unmoved; `gateway` ±0;
  `heltec_mobile` RAM = the resident book (+ any compiler-emitted storage, explained) and flash attributed; inventory
  +N; help per profile; census multiset; every probe pin re-derived.

## 6. Open points — 6.1 and 6.2 RULED (R-RA-30, 2026-09-06); the rest are Author decisions with QA's recommendation

- **6.1 ✅ RULED 2026-09-06 (R-RA-30: the design's split — `list`/`show` over USB or secured BLE, the four secret operations and every target-book mutation refused over BLE) — the question as put:** the `admin-key` BLE split. The design allows `list`/`show` over secured BLE and keeps the four
  secret operations USB-only; R-RA-29 deferred this family. **Recommendation: implement the design's split** — one
  ACCEPT-style guard refusing `generate`/`import`/`export`/`remove` (and the whole target-book mutation set) before the
  seam with one named envelope, `list`/`show` allowed — because on the only BLE-capable client (`xiao_mobile`) the
  operator's console IS BLE and Slice 8's `using=keyN` selection needs `list` there. The alternative is Slice 3's
  whole-family refusal, deferring the widening again. Either way it is a ruling, and the extractor already supports
  a sub-verb-aware condition.
- **6.2 ✅ RULED 2026-09-06 (R-RA-30: a one-off `xiao_mobile` RAM/flash measurement in the brief, attributed, not a third ruled board) — the question as put:** the unmeasured nRF52 client. `xiao_mobile` is the only BLE client and the most RAM-constrained one,
  and no gate measures it; Slice 4 adds a ≈2 KB resident buffer to every client. **Recommendation:** the brief adds a
  ONE-OFF `xiao_mobile` RAM/flash measurement (the warning census's "pinned-set exception" idiom, reported and
  attributed, not a third ruled board) — or rule that the ruled pair suffices and record the residual as a register
  row.
- **6.3 (Author) the `/mrtargets` row and the target-book verb family** — §2's 64-byte candidate and a family name;
  both frozen in the brief with the per-ABI numbers as the "ABI audit" §6.3 requires.
- **6.4 (Author, tools) the second inbox-verbs arm** mirroring `[env:heltec_mobile]`, with the existing arm's rows
  untouched and both arms' pins re-derived.
- **6.5 (Author, tools) the `MR_FEAT_RADMIN_CLIENT` profile axis** (the B319 twin: `mobile`/`mobile_oled` = 1, the
  four others = 0, typed, never derived) and the census extension on top of the Slice 3 closure multiset.
- **6.6 (Author) `regen`'s CLIENT-only warning line and the debt predicate** — exact bytes stated, Part 59's pins
  re-derived on mobile profiles, ACCEPT line byte-identical.
- **6.7 (Author, bench) Parts 55b and 56** authored under the design's names; Part 56's BLE half requires
  `xiao_mobile` hardware.

Starting pins = the Slice 3 CLOSURE's (record at brief time; unknown while Slice 3 is in flight). Bench: Parts 55b + 56.
