<!-- QA/Author: Claude (revision 1); production coder: Codex; owner rules and commits -->
# Remote-admin v2 Slice 10 — the standalone main-NV cleanup (R-RA-6)

**Revision 1 — 2026-09-19 — READY FOR CODER SOURCE-VALIDATION AND IMPLEMENTATION; nothing is HOLD (no ruling is
requested: R-RA-6 is the authority and every number in this slice is a measured decrease).**
Base **`4ad9c34`** (owner commit `ready` = the Slice 9 freeze, [independent QA PASS](../evidence/2026-09-19-radmin-slice9-qa-gate.md)),
clean; simulator **`6585649`**, clean. The coder pins this brief by content hash plus the inventory of the QA documents
that announce it. A brief under implementation is frozen (P4); a mid-slice ruling lands in the ledger and register and
is re-pinned at a checkpoint. This is the last slice of the remote-admin v2 arc.

## 0. What Slice 10 is, in one paragraph

Slice 9 left three inert mirrors behind on purpose: the `/mrcfg` blob fields `admin_pubkey` / `admin_counter_floor` /
`admin_provisioned`, their `Node` copies, and the boot `admin_load` that moves one into the other. Nothing reads them
any more (Slice 9 proved it). Slice 10 removes them in ONE separately measured NV-version change — `kVersion` 25 → 26
with the record shrinking from 280 to a predicted 240 bytes — so the attribution of the size, Node and RAM movement is
clean (R-RA-6, design item 10). An older `/mrcfg` record is refused by both size and version and the node re-seeds
its main config from the live defaults, exactly the documented reprovision-on-reflash of the v22 and v25 bumps; the
four remote-admin stores (`/mrmkeys`, `/mracl`, `/mradmid`, `/mrtargets`) are separate records and survive untouched.
One new boot line names the schema so Part 57f can observe the migration. No wire change, no verb, no new TU.

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
| `src/device_nv.h` `mrnv::Blob`: `admin_pubkey[32]` @162, `admin_counter_floor` @196, `admin_provisioned` @200 (native offsets measured by QA; `intro_attach` @201, `team_ch_pub` @202, `team_key_team_id` @268, `remote_action_activation_ms` @276, `sizeof` 280 / `alignof` 8); `kVersionMinLoad = 25`, `kVersion = 25`; `static_assert(sizeof(Blob) == 280 && alignof(Blob) == 8)` and `static_assert(offsetof(Blob, remote_action_activation_ms) == 276)`; `load()` (`:~1425`) = `blob_valid_range(out, n, kMagic, kVersionMinLoad, kVersion)` after the `n == sizeof(out)` size rule | **Delete the three fields; `kVersion` and `kVersionMinLoad` both become 26**; re-pin the two `static_assert`s to the MEASURED values on every ABI. **QA measured the post-removal layout natively (shadow headers, the native flag set): `sizeof` 240 / `alignof` 8, `offsetof(remote_action_activation_ms)` 236, `intro_attach` 162, `team_ch_pub` 163, `team_key_team_id` 228.** The coder confirms native and measures ARM and Xtensa; the asserts fire at compile time on each board, so the board builds are the measurement. The `kVersion` comment gains a v26 line in the existing shape ("REPROVISION-ON-REFLASH: the record shrank, so both the size rule and the floor reject a v25 record"). |
| `src/firmware_config.cpp` `nv_load_stamped` (load-or-seed-from-live, then stamp `kMagic`/`kVersion`); `src/fw_main.cpp` boot: `mrnv::Blob nv{}; if (mrnv::load(nv)) { … } ` (`:~829`) with NO console line about the outcome | The reprovision path already exists and is unchanged. **Add ONE boot line** — the decision text lives in a header (U3: `mrfw::nv_boot_report_console(Print&, bool loaded, uint16_t version)` in `src/firmware_config.h`, beside the seed prologue) and `fw_main` gains exactly ONE call right after the load, with the explicit sink: `> nv: /mrcfg v26 loaded` or `> nv: /mrcfg v26 reseeded from defaults (no record, or a pre-v26 record was refused) — re-run cfg set`. The console-sink structural probe pins the call (once, after the load, explicit sink) with a control. |
| `src/fw_main.cpp:~929` `g_node.admin_load(nv.admin_pubkey, nv.admin_counter_floor, nv.admin_provisioned)` (the one remaining caller) | **Delete.** |
| `lib/core/node.h`: the `#if MR_FEAT_RADMIN_ACCEPT` block `:~126–136` (`admin_provisioned()`, `admin_pubkey()`, `admin_counter_floor()`, `admin_load`, and the `#else` stubs) and the field block `:~3022–3024` (`_admin_pubkey[32]`, `_admin_counter_floor`, `_admin_provisioned`); `:~354` comment "(mirrors admin_load)"; the layout ledger `:~3037` naming `_admin_pubkey`; `static_assert(sizeof(Node) == 235248)` | **Delete both blocks entirely** (no stubs remain — nothing calls them); fix the two comments (history stays factual); re-pin `sizeof(Node)` to the measured native value. |
| `tools/probe_board_abi.py` Node pins `235248 / 122176 / 157344` (native / heltec_mobile / gateway) | Re-pin native and gateway to the MEASURED values (the fields are ACCEPT-gated; **QA measured native 235248 → 235208 = −40** with the same shadow headers = 37 bytes of fields + 3 bytes of alignment slack; gateway predicted −40, the coder measures); **heltec_mobile stays 122176** (the block never existed there — the control). `mrnv::Blob` is not pinned by the probe; its own `static_assert`s are the pin. |
| `test/test_device_nv.cpp:~64–93` (`blob_valid_range` cases, all symbolic on `kVersion`/`kVersionMinLoad`; the B353 case builds a v24-stamped record of the current size and expects refusal) | Still valid symbolically; the B353 case's premise ("same size, padding") is now history — keep it, re-word its comment, and ADD the v26 cases: a v25-stamped record of the NEW size is refused (below the floor) and a 280-byte record is refused (size). |
| `test/test_firmware_config_service.cpp:~54/295/544/559` and `test/test_firmware_provisioning_service.cpp:~75/370` use `admin_counter_floor = 99` / `4096` as the "an unrelated persisted field is carried through a save" probe | Retarget the probe field to `remote_action_activation_ms` (a persisted u32 that no service touches) with the same values; the preservation proofs survive unchanged in meaning. |
| Simulator: `node.h` changes ⇒ both `lus` variants recompile; no scenario reads the mirrors | Corpus **predicted 36/36 byte-identical**; any stream delta is STOP. No new TU (P7). |
| Batteries: `devicenv` (`src/device_nv.h`) anchors none of the three fields, the size or the version (QA checked); no battery targets `node.h` | Union = the 61-battery floor (re-anchor only if a pattern moved; the receipt names any). |

Baselines (Slice 9 gate, 2026-09-19): native 2950/195768/0; corpus 36/36, s18 `32afbf11`/269517/0; Node 235248 native
/ 122176 mobile / 157344 gateway; gateway RAM 203820 / flash 572224; heltec_mobile RAM 211764 / flash 1394520;
xiao_mobile 176596 / 699548; inventory 197; union floor 61 batteries / 984 configured; census 171/175/175/175/179/179;
`mrnv::Blob` 280 / 8, `offsetof(remote_action_activation_ms)` 276. QA's native shadow measurement of the v26 layout: `Blob` 240 / 8 / 236, `Node` 235208.

## 2. Scope

**IN:** the three blob fields, the version bump and floor, the re-pinned asserts, the `Node` mirrors/accessors/stubs
and their size pin, the boot `admin_load` call, the one boot report line and its structural pin, the ABI-probe
re-pins, the test retargets, Part 57f. **OUT:** every other `Blob` field and every other record (`/mrid`, `/mrpeers`,
`/mrjoin`, `/mrteams`, `/mrui`, and the four remote-admin stores); the `leave` reset and the seed prologue (unchanged
— the removed fields simply no longer exist); any wire, verb, help, authority, inventory or contract change; the
companion; documentation beyond the bench part and the design status. C1: no refactor rides along.

## 3. Contract

1. **Schema v26 = v25 minus the three fields.** `kVersion = 26`, `kVersionMinLoad = 26`; `sizeof(Blob)`,
   `alignof(Blob)` and `offsetof(remote_action_activation_ms)` are re-asserted at the values measured on all three
   ABIs (native measured by QA: 240 / 8 / 236). Every other field keeps its order and meaning.
2. **Migration = the documented reprovision-on-reflash.** A v25 record fails `load()` twice over (size 280 ≠ 240;
   version 25 < 26); `nv_load_stamped` seeds the live defaults and stamps v26; the next persist writes v26. The
   operator re-runs `cfg set` for radio/config values, as after v22 and v25. **The four remote-admin stores are
   separate records and are neither read nor written by this path** — a control proves that a provisioned
   `/mradmid` + `/mracl` (target) and `/mrmkeys` + `/mrtargets` (controller) survive a `/mrcfg` reseed byte-for-byte
   and that the boot store reports (`admin_stores_boot_report_console`, `admin_client_stores_boot_report_console`)
   are unchanged.
3. **The boot line.** Exactly one new console line at boot, from the header-owned formatter, stating the schema
   version and whether the record was loaded or reseeded (wording in §1); `fw_main` makes one call with the explicit
   sink; the console-sink structural probe pins it and a control (call removed / duplicated / sink replaced) is RED.
4. **The Node.** No mirror, accessor or stub survives; `sizeof(Node)` moves only on the ABIs that carried the block
   (native, gateway) and is re-pinned at the measured value; heltec_mobile is byte-identical (control).
5. **Nothing else changes.** Corpus 36/36 byte-identical; the six standing probes + deferred-actions at their pins;
   inventory 197; authority agree; census at 171/175/175/175/179/179 (no TU is added or removed, so no re-pin is
   expected — a moved count is STOP until attributed); no verb, no wire, no `wire_version`.

## 4. Allocation (predicted; measured at the gate)

`mrnv::Blob` 280 → **240** (QA-measured natively: 39 bytes leave = 37 of fields + the 2-byte pad before
`admin_counter_floor`, and the pad before `team_key_team_id` shrinks from 2 to 1 ⇒ −40). `Node` native
**235248 → 235208 (−40, QA-measured)**; gateway predicted −40 (the coder measures); mobile unchanged. Gateway RAM down by the same amount (`g_node`); mobile
RAM unchanged; flash down slightly on ACCEPT builds (the copy loop and the boot call) and up by the one boot line's
string on both — **the NET flash may move either way by a few tens of bytes and must be attributed; RAM or `Node`
INCREASE anywhere is STOP-1.** The coder measures all three ABIs and both boards; QA re-measures.

## 5. Fence

Production: `src/device_nv.h` (the three fields, the two constants, the two asserts, the comments),
`src/firmware_config.{h,cpp}` (`nv_boot_report_console` only), `src/fw_main.cpp` (delete the `admin_load` call; add
the one report call), `lib/core/node.h` (the two blocks, two comments, the size assert). Tests: `test/test_device_nv.cpp`
(v26 cases), `test/test_firmware_config_service.cpp`, `test/test_firmware_provisioning_service.cpp` (probe-field
retarget), a store-survival control (in `test_firmware_admin_identity.cpp` / `…_acl.cpp` / `…_client_verbs.cpp`, or
the config-service test — the coder names the file in preflight), a boot-line golden in the console probe. Tools:
`tools/probe_board_abi.py` (native + gateway Node pins), `tools/probe_console_sink/structural.py` (+ `negctl.py`) for
the boot-line pin and its control, `tools/probe_ui_model_mutations.py` only if a `devicenv` pattern moved (named in
the receipt). Docs: `docs/2026-07-31-bench-test-script.md` (Part 57f). **No `platformio.ini` change, no new TU, no
simulator edit (P7).** **OUT:** everything in §2.

## 6. Required proofs

| Surface | Proof |
| --- | --- |
| Schema | the three `static_assert`s hold at the measured values on native, ARM and Xtensa (the board builds); `kVersion == kVersionMinLoad == 26`; `test_device_nv`: v26 record of the new size loads, v25-stamped new-size record refused (floor), 280-byte record refused (size), erased/future/wrong-magic refused as before |
| Migration | native: a v25 image's record → `load()` false → `nv_load_stamped` seeds the live defaults and stamps 26; the boot line says "reseeded"; after one persist the next boot line says "loaded"; the four remote-admin stores are byte-identical across the reseed and their boot reports unchanged |
| Node | no `admin_` accessor, stub or field remains (scoped grep); `sizeof(Node)` at the measured pins; heltec_mobile 122176 unchanged; the ABI probe 290-ish checks with 9 controls RED and its own pin controls |
| Boot line | golden for both wordings; structural: exactly one call, after the load, explicit sink; removal / duplication / global-sink substitution RED |
| Preservation tests | `remote_action_activation_ms` carried through every save path the two service tests exercise, same assertions, same counts |
| Nothing else | corpus 36/36 byte-identical; probes at pins; inventory 197; authority PASS; census at its six pins; union 61 batteries all RED except B342; board pair with Node/RAM decrease attributed by symbol, flash attributed |

## 7. Owner rulings

None requested. R-RA-6 authorizes the standalone NV-version slice and its measured re-pins; the reprovision-on-reflash
consequence follows the v22/v25 precedent under M3 (MeshRoute is unshipped). If any measured number is an INCREASE, or
the record does not shrink to one contiguous layout on some ABI, that is STOP-1 for QA, not a silent accept.

## 8. Gate and landing

Full gate on both sides (`lib/core/node.h` changes): native wrapper and binary; extended reference (94/94) and the
Slice-9 reference (94 + 1); simulator rebuild and corpus `--require-anchors` **predicted 36/36 byte-identical**; ABI
probes at the NEW native/gateway pins with mobile unchanged; six probes + deferred-actions + BLE-line, default and
`--no-neg`, explicit inbox CLIENT arm; tools discovery (349); inventory write/bare/check (197); authority + selftests;
A0; literals; whitespace both repos; census six envs (no re-pin expected); deterministic pair gateway then
heltec_mobile + the one-off `xiao_mobile`; union S ∪ H from the 61/984 floor; the exact `PIN re-synced? YES` line.
**STOP:** any stream delta; any RAM or `Node` increase; a `Blob` size other than the measured contiguous layout; a
touched store record; a census count moved without attribution; a new verb, wire or TU. Receipt:
`docs/superpowers/evidence/2026-09-19-radmin-slice10.md`. On PASS QA lands the register (§0; the arc's closing note),
design item 10 + §19.1 row 10 + the header, bench **Part 57f** — on an ACCEPT node flashed with v26 over a v25
record the boot prints the "reseeded" line followed by the unchanged `/mradmid` + `/mracl` reports, `cfg` shows no
admin fields, one `cfg set` later the next boot prints "loaded", and a v2 `remote` round trip (Part 57c step 1)
still works because the target ACL survived; on a mobile the same with `/mrmkeys` + `/mrtargets` — tracker, MEMORY,
and the ledger's arc-complete note.
