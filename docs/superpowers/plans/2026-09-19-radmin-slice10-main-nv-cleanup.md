<!-- QA/Author: Claude (revision 3); production coder: Codex; owner rules and commits -->
# Remote-admin v2 Slice 10 — the standalone main-NV cleanup (R-RA-6)

**Revision 3 — 2026-09-19 — READY FOR THE CODER TO LAND THE TWO FENCED REPAIRS AND RE-RUN THE FULL CHAIN; nothing is HOLD (no ruling is
requested: R-RA-6 is the authority and every number in this slice is a measured decrease).**
Base **`4ad9c34`** (owner commit `ready` = the Slice 9 freeze, [independent QA PASS](../evidence/2026-09-19-radmin-slice9-qa-gate.md)),
clean; simulator **`6585649`**, clean. The coder pins this brief by content hash plus the inventory of the QA documents
that announce it. A brief under implementation is frozen (P4); a mid-slice ruling lands in the ledger and register and
is re-pinned at a checkpoint. This is the last slice of the remote-admin v2 arc.

**Revision 2 folds B430** ([coder receipt](../evidence/2026-09-19-radmin-slice10.md)): revision 1 required additive
console checks and controls but fenced neither `tools/probe_console_sink/run.sh` (whose literal pins 83/149 refuse any
added row) nor — found by QA while folding — `tools/probe_inbox_verbs/{probe_main.cpp,run.sh}`, whose A7-1..A7-8 rows
and A7-C1/A7-C2 controls carry the v25 layout as LITERALS (`sizeof == 280`, versions 24/25/26; floor mutations to 24
and 26) and go RED or inert at v26. It also promised a native execution of the reseed path that no host instrument
can reach (`firmware_config.cpp` is compiled by neither the native suite nor any probe; the host NV arm answers
"absent" and refuses writes). §5 now fences all four instrument files, §1/§3/§6 name where each proof executes, and
the migration proof is re-shaped onto the inbox probe's REAL ESP32 NV arm over its byte-backed medium — an existing
instrument, so the receipt's proposed new host probe is NOT adopted (see §6). Everything else is unchanged from
revision 1, including every measured number.

**Revision 3 folds B431 and B432** ([coder receipt §7](../evidence/2026-09-19-radmin-slice10.md#7-revision-2-preserved-implementation-checkpoint--hold-b431b432),
[checkpoint](../evidence/2026-09-19-radmin-slice10-r2-checkpoint/README.md)): the coder's full gate on the revision-2
candidate found two more instrument users outside §5, both QA-verified against the tree — `test/test_custody_receive_g.cpp`
pins `sizeof(Node) == 235248` (the only Node-size pin besides `node.h` and the ABI probe), and
`tools/probe_features/ownership.py` `APPROVED_SITES['lib/core/node.h']` is a per-file MULTISET census of the feature
guards (base ten sites, five `MR_FEAT_RADMIN_ACCEPT`; the two deleted blocks leave eight sites, three ACCEPT — O4c
correctly refuses). §5 fences both; nothing else moves. The coder's own in-fence correction (the `test_device_nv`
layout case's four literals 25/25/280/276 → 26/26/240/236) needs no brief change. The coder's measured board pair
is recorded in §4 as the gate expectation. The implemented candidate is preserved; the coder lands exactly the two
repairs and re-runs the whole §8 chain fresh (no result from the interrupted run is inherited).

## 0. What Slice 10 is, in one paragraph

Slice 9 left three inert mirrors behind on purpose: the `/mrcfg` blob fields `admin_pubkey` / `admin_counter_floor` /
`admin_provisioned`, their `Node` copies, and the boot `admin_load` that moves one into the other. Nothing reads them
any more (Slice 9 proved it). Slice 10 removes them in ONE separately measured NV-version change — `kVersion` 25 → 26
with the record shrinking from 280 to 240 bytes — so the attribution of the size, Node and RAM movement is clean
(R-RA-6, design item 10). An older `/mrcfg` record is refused by both size and version and the node runs its
compile-time defaults until the next `cfg set` persists a v26 record, exactly the documented reprovision-on-reflash
of the v22 and v25 bumps; the four remote-admin stores (`/mrmkeys`, `/mracl`, `/mradmid`, `/mrtargets`) are separate
records and survive untouched. One new boot line names the schema so Part 57f can observe the migration. No wire
change, no verb, no new TU.

## 1. What binds this slice (links, not quotes)

Design `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md`: §17 (the last replacement bullet:
"remove the legacy `admin_pubkey`, `admin_counter_floor`, and `admin_provisioned` fields when the NV cleanup slice can
do so atomically"), §19 item 10 (standalone; "do not combine it with legacy protocol deletion, companion work, or
documentation cleanup"), §19.1 row 10 (36/36 unchanged; ABI/NV migration; ruled pair; **Part 57f**: the boot migration
line reports the new schema healthy, legacy fields absent, and all four replacement stores retained). Rulings:
**R-RA-6** ("make the blob cleanup its own NV-version slice so its attribution stays clean" — the authority for the
version bump and the measured re-pins), R-RA-26/27 (capability gates), the M3 precedent that a `/mrcfg` bump
reprovisions (`device_nv.h`'s `kVersion` note; Slice 7a's B353 floor). Working rules CLAUDE.md C1–C4, D1–D7, P4–P7.

| Verified seam at `4ad9c34` (pin by symbol; lines are hints) | What Slice 10 does |
| --- | --- |
| `src/device_nv.h` `mrnv::Blob`: `admin_pubkey[32]` @162, `admin_counter_floor` @196, `admin_provisioned` @200 (native offsets measured by QA; `intro_attach` @201, `team_ch_pub` @202, `team_key_team_id` @268, `remote_action_activation_ms` @276, `sizeof` 280 / `alignof` 8); `kVersionMinLoad = 25`, `kVersion = 25`; `static_assert(sizeof(Blob) == 280 && alignof(Blob) == 8)` and `static_assert(offsetof(Blob, remote_action_activation_ms) == 276)`; `load()` (`:~1425`) = `blob_valid_range(out, n, kMagic, kVersionMinLoad, kVersion)` whose `slot_size_ok(n, sizeof)` is the size rule | **Delete the three fields; `kVersion` and `kVersionMinLoad` both become 26**; re-pin the two `static_assert`s to the MEASURED values on every ABI. **Measured (QA natively with shadow headers; the coder's receipt on all three ABIs): `sizeof` 240 / `alignof` 8, `offsetof(remote_action_activation_ms)` 236, `intro_attach` 162, `team_ch_pub` 163, `team_key_team_id` 228, identical on native, ARM and Xtensa.** The asserts fire at compile time on each board, so the board builds are the measurement. The `kVersion` comment gains a v26 line in the existing shape ("REPROVISION-ON-REFLASH: the record shrank, so both the size rule and the floor reject a v25 record"). |
| `src/firmware_config.cpp` `nv_load_stamped` (`:94`, load-or-seed-from-live, then stamp `kMagic`/`kVersion`; `seed_blob_from_live` file-static `:776`); `src/fw_main.cpp` boot: `mrnv::Blob nv{}; if (mrnv::load(nv)) { … }` (`:~830`) with NO console line about the outcome; on a failed load `setup()` keeps the compile-time defaults and the next persisting verb runs `nv_load_stamped` | The reseed path is **unchanged by this slice (C1)**. **Add ONE boot line.** The formatter is header-owned (U3): `inline void nv_boot_report_console(Print& out, bool loaded)` in `src/firmware_config.h`, directly beside the §nv-ritual prologue (`nv_load_stamped`'s declaration), printing `mrnv::kVersion` — never a literal — as: `> nv: /mrcfg v26 loaded` / `> nv: /mrcfg v26 not loaded (absent, or a pre-v26 record refused): compile-time defaults until the next cfg set`. `fw_main` gains exactly ONE call, `mrfw::nv_boot_report_console(mrcon, <the bool that mrnv::load(nv) returned>)`, after the `if (…load…) { … }` block and before the `#if MR_FEAT_RADMIN_ACCEPT` store boot report (the schema line precedes the store lines on every build); capturing the load result in a named `const bool` is the one permitted glue change. The `mrcon` argument IS the explicit sink (the `AdminPrintLines lines(mrcon)` precedent). |
| `src/fw_main.cpp:~929` `g_node.admin_load(nv.admin_pubkey, nv.admin_counter_floor, nv.admin_provisioned)` (the one remaining caller, receipt-verified) | **Delete** (with its `§remote-mgmt (v20)` comment line). |
| `lib/core/node.h`: the `#if MR_FEAT_RADMIN_ACCEPT` block `:~124–136` (the two comment lines, `admin_provisioned()`, `admin_pubkey()`, `admin_counter_floor()`, `admin_load`, and the `#else` stubs) and the field block `:~3021–3025` (`_admin_pubkey[32]`, `_admin_counter_floor`, `_admin_provisioned`, its `// ---- REMOTE-MGMT` banner); `:~354` comment "(mirrors admin_load)"; the layout ledger `:~3037` naming `_admin_pubkey`; `static_assert(sizeof(Node) == 235248)` | **Delete both blocks entirely** (no stubs remain — nothing calls them); fix the two comments (history stays factual); re-pin `sizeof(Node)` to the measured native value **235208**. |
| `tools/probe_board_abi.py` Node pins `235248 / 122176 / 157344` (native / heltec_mobile / gateway); **`test/test_custody_receive_g.cpp:~2315` `CHECK(sizeof(Node) == 235248)` (B431 — the S3 custody case's whole-Node line, whose own obligation is the four `CustodyTranslated*` size/alignment lines above it)** | Re-pin native **235208** and gateway **157304** (both −40, receipt-measured; 37 bytes of fields + 3 of alignment slack); **heltec_mobile stays 122176** (the block never existed there — the control). `mrnv::Blob` is not pinned by the probe; its own `static_assert`s are the pin. **The custody test: that ONE literal → 235208 plus its derivation comment ("Slice 10 R-RA-6: −40, the inert admin mirrors"); every custody-specific assertion unchanged.** |
| `tools/probe_features/ownership.py` `APPROVED_SITES['lib/core/node.h']` = ten normalized guard lines (five `#if MR_FEAT_RADMIN_ACCEPT`, four `#if MR_FEAT_RADMIN_CLIENT`, one shared), compared by O4c as a per-file multiset; runner pins `PIN_CELLS=9 / PIN_CHECKS=112 / PIN_CONTROLS=58`; `tools/test_probe_features.py` pins only the `NODE_H` constant and the no-test-owner rule (B432) | **Remove the two `#if MR_FEAT_RADMIN_ACCEPT` entries** that were the deleted accessor block and field block (ten → eight sites, ACCEPT five → three; CLIENT four and the shared one unchanged) and say so in the census comment; O4c's exact multiset comparison, every other approved list, every control and the three runner pins stay unchanged (a moved pin is STOP); `tools/test_probe_features.py` is not edited. |
| `test/test_device_nv.cpp:~64–93` (`blob_valid_range` cases, symbolic on `kVersion`/`kVersionMinLoad`; the B353 case builds a literal-24 record of the current size and expects refusal; the case title says "v25 only at this layout") | Still valid symbolically; retitle for v26; the B353 case's premise ("same size, padding") is now history — keep it, re-word its comment, and ADD the v26 cases: a **v25-stamped record of the new size is refused (floor)** and a **280-byte read (`n = 280`, the v25 record's size) of a v26-stamped blob is refused (size)**. |
| `tools/probe_inbox_verbs/probe_main.cpp` A7-1..A7-8 (B360 block, both arms: `record.version = 24`, `sizeof record == 280`, "accepts v25", "rejects v26" — the REAL typed `/mrcfg` wrappers over the REAL ESP32 NV arm and the byte-backed fake medium `mrprobe_nv()`, with `writes` counting); `tools/probe_inbox_verbs/run.sh` A7-C1/A7-C2 (`:757–760`: the loader floor literally lowered to 24 / raised to 26) and pins `PIN_CHECKS_ACCEPT=1374`, `PIN_CHECKS_CLIENT=457`, `PIN_CONTROLS_ACCEPT=60`, `PIN_CONTROLS_CLIENT=68` | **Re-fixture A7-1..A7-8 in place** (240; a v25-stamped 240-byte fixture is stored and REFUSED below the floor; v26 accepted with `remote_action_activation_ms` surviving reload; v27 refused as the future layout) — eight rows retained, none deleted; **A7-C1/A7-C2 literals become 25 / 27** so both controls keep biting; **ADD the executed migration rows and the boot-line goldens here** (§6); re-derive the four pins from the implemented instrument (the runner's comment lines say what moved, D5 bare integers). |
| `tools/probe_console_sink/structural.py` (S30/S31 pin the store boot report: exactly once, in `setup()`, gated, after the mount), `negctl.py` (its controls), `run.sh` `PIN_STRUCTURAL=83` / `PIN_CONTROLS=149` (`:198/:206`, enforced by `pin_cmp` before PASS); `tools/test_probe_console_sink.py::_run_sh_pins` reads those pins from the runner's source | **ADD S84** for the schema line in the S30 shape: exactly one `nv_boot_report_console(` call in `setup()`, positioned AFTER the `mrnv::load(nv)` call and BEFORE the `admin_stores_boot_report_console` gate, first argument `mrcon`; **three controls** (call deleted / call duplicated / `mrcon` replaced by `Serial`) each RED; **re-pin `PIN_STRUCTURAL` and `PIN_CONTROLS`** to the derived counts (predicted 84 / 152) with a comment line; the discovery test stays unchanged and re-runs. |
| `test/test_firmware_config_service.cpp:~54/295/544/559` and `test/test_firmware_provisioning_service.cpp:~75/370` use `admin_counter_floor = 99` / `4096` as the "an unrelated persisted field is carried through a save" probe (receipt-verified: neither service touches `remote_action_activation_ms`) | Retarget the probe field to `remote_action_activation_ms` with the same values; the preservation proofs survive unchanged in meaning. |
| Simulator: `node.h` changes ⇒ both `lus` variants recompile; no scenario reads the mirrors | Corpus **predicted 36/36 byte-identical**; any stream delta is STOP. No new TU (P7). |
| Batteries: `devicenv` (`src/device_nv.h`) anchors none of the three fields, the size or the version (QA checked); no battery targets `node.h` | Union = the 61-battery floor (re-anchor only if a pattern moved; the receipt names any). |

Baselines (Slice 9 gate, 2026-09-19): native 2950/195768/0; corpus 36/36, s18 `32afbf11`/269517/0; Node 235248 native
/ 122176 mobile / 157344 gateway; gateway RAM 203820 / flash 572224; heltec_mobile RAM 211764 / flash 1394520;
xiao_mobile 176596 / 699548; inventory 197; union floor 61 batteries / 984 configured; census 171/175/175/175/179/179;
`mrnv::Blob` 280 / 8, `offsetof(remote_action_activation_ms)` 276; console-sink pins 83 / 149; inbox pins 1374 / 457
/ 60 / 68. Measured v26 layout (QA native + coder three-ABI): `Blob` 240 / 8 / 236; `Node` 235208 / 122176 / 157304.

## 2. Scope

**IN:** the three blob fields, the version bump and floor, the re-pinned asserts, the `Node` mirrors/accessors/stubs
and their size pin, the boot `admin_load` call, the one boot report line and its structural pin, the ABI-probe
re-pins, the test retargets, the four instrument files in §5, Part 57f. **OUT:** every other `Blob` field and every
other record (`/mrid`, `/mrpeers`, `/mrjoin`, `/mrteams`, `/mrui`, and the four remote-admin stores); the `leave`
reset, `nv_load_stamped` and `seed_blob_from_live` (unchanged — the removed fields simply no longer exist); any wire,
verb, help, authority, inventory or contract change; the companion; a new probe or TU; documentation beyond the
bench part and the design status. C1: no refactor rides along.

## 3. Contract

1. **Schema v26 = v25 minus the three fields.** `kVersion = 26`, `kVersionMinLoad = 26`; `sizeof(Blob)`,
   `alignof(Blob)` and `offsetof(remote_action_activation_ms)` are re-asserted at the measured 240 / 8 / 236 (the
   board builds re-measure ARM and Xtensa). Every other field keeps its order and meaning.
2. **Migration = the documented reprovision-on-reflash.** A v25 record fails `load()` twice over (size 280 ≠ 240;
   version 25 < 26); boot keeps the compile-time defaults and prints the "not loaded" line; the next persisting verb's
   `nv_load_stamped` seeds the live config and stamps v26; the following boot prints "loaded". **The four remote-admin
   stores are separate records that this path neither reads nor writes** — proven EXECUTED in the inbox probe over
   the real ESP32 NV arm (§6) and on metal in Part 57f.
3. **The boot line.** Exactly one new console line at boot, from the header-owned formatter, stating `kVersion` and
   whether the record loaded (wording in §1); `fw_main` makes one call passing `mrcon`; S84 pins it and three
   controls are RED; the goldens execute in the inbox probe on both arms.
4. **The Node.** No mirror, accessor or stub survives; `sizeof(Node)` moves only on the ABIs that carried the block
   (native 235208, gateway 157304) and heltec_mobile is byte-identical at 122176 (control).
5. **Nothing else changes.** Corpus 36/36 byte-identical; the six standing probes + deferred-actions at their pins
   (the console-sink and inbox pins re-derived as in §1, every prior row and control retained); inventory 197;
   authority agree; census at 171/175/175/175/179/179 (no TU is added or removed, so no re-pin is expected — a moved
   count is STOP until attributed); no verb, no wire, no `wire_version`.

## 4. Allocation (measured layout; footprint measured at the gate)

`mrnv::Blob` 280 → **240** on all three ABIs (39 bytes leave = 37 of fields + the 2-byte pad before
`admin_counter_floor`; the pad before `team_key_team_id` shrinks from 2 to 1 ⇒ −40). `Node` native **235248 →
235208**, gateway **157344 → 157304**, mobile **122176 unchanged**. **RAM attribution (the receipt's point): every
linked `Blob` instance shrinks by 40 — `mrnv::save`'s static `Blob cur` and any other static/global `Blob` — plus
the 40 of `g_node` on ACCEPT builds; so heltec_mobile RAM is predicted DOWN by the Blob statics alone (not
"unchanged"), gateway DOWN by the Blob statics plus 40. The pair's symbol diff attributes every byte; a RAM or `Node`
INCREASE anywhere is STOP-1.** Flash: down slightly on ACCEPT builds (the copy loop and the boot call) and up by the
one boot line's string and call on both — the NET may move either way by a few tens of bytes and must be attributed.
**Coder-measured on the revision-2 candidate (fresh stock pair; the gate expectation, re-measured by QA): gateway RAM
203820 → 203740 (−80) / flash 572224 → 572240 (+16); heltec_mobile RAM 211764 → 211724 (−40) / flash 1394520 →
1394704 (+184); symbol diff closes exactly — `mrnv::save(...)::cur` 280 → 240 on both, `g_node` 157344 → 157304 on
gateway and 122176 unchanged on mobile, no other RAM symbol moves.**

## 5. Fence

Production: `src/device_nv.h` (the three fields, the two constants, the two asserts, the comments),
`src/firmware_config.h` (`nv_boot_report_console` inline only), `src/fw_main.cpp` (delete the `admin_load` call; the
named load bool; the one report call), `lib/core/node.h` (the two blocks, two comments, the size assert). Tests:
`test/test_device_nv.cpp` (retitle + v26 cases), `test/test_firmware_config_service.cpp`,
`test/test_firmware_provisioning_service.cpp` (probe-field retarget). Instruments (all four, B430):
`tools/probe_console_sink/structural.py` (S84), `tools/probe_console_sink/negctl.py` (three S84 controls),
`tools/probe_console_sink/run.sh` (`PIN_STRUCTURAL`, `PIN_CONTROLS` + comment lines; D5 bare integers),
`tools/probe_inbox_verbs/probe_main.cpp` (A7-1..A7-8 re-fixtured; the new A7 migration rows + boot-line goldens),
`tools/probe_inbox_verbs/run.sh` (A7-C1/A7-C2 literals 25 / 27; one new control; the four pins + comment lines).
Also `tools/probe_board_abi.py` (native + gateway Node pins), **`test/test_custody_receive_g.cpp` (the one
`sizeof(Node)` literal + comment, B431), `tools/probe_features/ownership.py` (the two deleted guard entries in
`APPROVED_SITES['lib/core/node.h']` + comment, B432)**, `tools/probe_ui_model_mutations.py` only if a
`devicenv` pattern moved (named in the receipt). Docs: `docs/2026-07-31-bench-test-script.md` (Part 57f).
`tools/test_probe_console_sink.py` and `tools/test_probe_features.py` are NOT edited (the first derives its pins from
`run.sh`; the second pins no site list) and are re-run by discovery.
**No `platformio.ini` change, no new TU, no new probe, no `firmware_config.cpp` edit, no simulator edit (P7).**
**OUT:** everything in §2.

## 6. Required proofs

| Surface | Proof |
| --- | --- |
| Schema | the three `static_assert`s hold at 240 / 8 / 236 on native, ARM and Xtensa (the board builds); `kVersion == kVersionMinLoad == 26`; `test_device_nv`: v26 record of the new size loads, v25-stamped new-size record refused (floor), an `n = 280` read refused (size), erased/future/wrong-magic refused as before |
| Migration (EXECUTED, inbox probe, both arms, real ESP32 NV arm over `mrprobe_nv()`) | beside the re-fixtured A7-1..A7-8: (a) a **280-byte v25 record** placed in the `/mrcfg` slot through the primitive (`mrnv::write_slot(kSlotCfg, …, 280)`, the fake's slot holds 2304) → the real `mrnv::load` returns false and `nv_boot_report_console(mrcon, that bool)` prints the exact "not loaded" line; (b) a v26 record via `mrnv::save` → `load` true and the exact "loaded" line, `v26` literal in both goldens; (c) the four remote-admin slots (`kSlotAdmid`, `kSlotAcl`, `kSlotMgmtKeys`, `kSlotTargets`), pre-filled through the primitives, are **byte-identical** across (a)+(b) and the medium's `writes` counter moved only by the one `save`; **one new control** on `device_nv.h` (`nvh` target, the C29 shape): `load(Blob&)` reads `kSlotAdmid` instead of `kSlotCfg` → (b) RED. ⛔ NOT claimed: an executed `nv_load_stamped` reseed — that function is unchanged by this slice and is compiled by no host instrument (receipt §4); per M2 its execution is Part 57f on metal. The receipt's proposed source-extracted host probe (`nv_migration.py`) is **not adopted**: a new instrument on the last slice, for a path the slice does not change, when the existing probe already runs the real NV arm |
| Node | no `admin_` accessor, stub or field remains (scoped grep; comments classified); `sizeof(Node)` at 235208 / 122176 / 157304 in `node.h`, the ABI probe AND the custody test (native 2950 cases / 0 failed); the ABI probe's controls RED and its own pin controls; the feature probe at 9 cells / 112 checks / 58 controls / 0 unusable with the eight-site `node.h` census and its ownership controls all still RED |
| Boot line | the two goldens above; structural S84 (exactly one call, in `setup()`, after the load, before the store report, `mrcon` first); removal / duplication / `Serial` substitution RED |
| Preservation tests | `remote_action_activation_ms` carried through every save path the two service tests exercise, same assertions, same counts |
| Nothing else | corpus 36/36 byte-identical; probes at the re-derived pins; inventory 197; authority PASS; census at its six pins; union 61 batteries all RED except B342; board pair with Node/RAM decrease attributed by symbol (every linked `Blob`), flash attributed |

## 7. Owner rulings

None requested. R-RA-6 authorizes the standalone NV-version slice and its measured re-pins; the reprovision-on-reflash
consequence follows the v22/v25 precedent under M3 (MeshRoute is unshipped). If any measured number is an INCREASE, or
the record does not shrink to one contiguous layout on some ABI, that is STOP-1 for QA, not a silent accept.

## 8. Gate and landing

Full gate on both sides (`lib/core/node.h` changes): native wrapper and binary; extended reference (94/94) and the
Slice-9 reference (94 + 1); simulator rebuild and corpus `--require-anchors` **predicted 36/36 byte-identical**; ABI
probes at the NEW native/gateway pins with mobile unchanged; six probes + deferred-actions + BLE-line, default and
`--no-neg`, explicit inbox CLIENT arm (the console-sink and inbox pins re-derived, `PIN re-synced? YES`); tools
discovery (349); inventory write/bare/check (197); authority + selftests; A0; literals; whitespace both repos; census
six envs (no re-pin expected); deterministic pair gateway then heltec_mobile + the one-off `xiao_mobile`; union
S ∪ H from the 61/984 floor. **STOP:** any stream delta; any RAM or `Node` increase; a `Blob` size other than 240
on any ABI; a touched store record; a census count moved without attribution; a deleted or unusable prior row or
control; a new verb, wire, TU or probe. Receipt: `docs/superpowers/evidence/2026-09-19-radmin-slice10.md` (the
existing receipt continues; the interrupted revision-2 run contributes nothing — every instrument re-runs fresh on the
repaired candidate). On PASS QA lands the register (§0; B430–B432 close; the arc's closing note), design item 10
+ §19.1 row 10 + the header, bench **Part 57f** — on an ACCEPT node flashed with v26 over a v25 record the boot
prints the "not loaded" line followed by the unchanged `/mradmid` + `/mracl` reports, `cfg` shows no admin fields,
one `cfg set` later the next boot prints "loaded", and a v2 `remote` round trip (Part 57c step 1) still works
because the target ACL survived; on a mobile the same with `/mrmkeys` + `/mrtargets` — tracker, MEMORY, and the
ledger's arc-complete note.
