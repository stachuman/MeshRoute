<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 4 — controller keyring, target book and the R-RA-30 BLE split · CODER EVIDENCE · 2026-09-06

Brief: `docs/superpowers/plans/2026-09-06-radmin-slice4-controller-stores.md` (Quality-Agent preliminary PASS with
its B321 layering fold-in applied), with §1's placeholders resolved by the dispatcher at dispatch. Design
`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md` §§6.2-6.4, 12.1, 14, 19/19.1/19.2;
rulings R-RA-6 / R-RA-8 / R-RA-12 / R-RA-21 / R-RA-26 / R-RA-27 / R-RA-28 / R-RA-29 / R-RA-30.

⛔ **NOTHING HERE IS A METAL RESULT.** Every store is a fake or a host-compiled stand-in: no flash, no wear, no
power cut, no real BLE transport, no radio, no on-air frame. Bench **Parts 55b / 56** are drafted-but-NOT-RUN and
**Part 59** gains the conditional client warning; [[B312]] and [[B317]] both stay OPEN for their own obligations.

---

## 0. Provenance

| fact | value |
| --- | --- |
| MeshRoute checkout (ALL edits and measurements) | `/home/staszek/MeshRoute`, branch `main` |
| base commit | `a0ff994af766c8880f160bc2afbb8253b6dbe69e` ("Slice 3 brief"), verified by `git rev-parse HEAD` |
| status at start | **EMPTY** — `git status --short` produced no output |
| simulator | `/home/staszek/lora-universal-simulator` @ `868888419c7cc250d7019860d3403a7721ade1fc`, ⛔ UNCHANGED (`git status --short` empty at start and at end; `git diff --check` clean) |
| paired simulator build | the simulator's OWN `build/`, `MESHROUTE_DIR:PATH=/home/staszek/lora-universal-simulator/../MeshRoute` — i.e. THIS checkout; `lus` md5 `b1b1d92c541cc7f6f63864a2bcc6a355` at base AND at the end |
| mutation staging | `rsync -a --delete --exclude=.git --exclude=.pio --exclude=.pio-measure --exclude=.claude` into a scratch `stage/` + `stage2/`; ⛔ NO battery ever ran in the measured checkout |
| board captures | `.pio-measure/s4-base-pair`, `s4-base-xiao`, `s4-final2-pair`, `s4-final2-xiao` — fresh dirs, all OUTSIDE `.pio-measure/env` ([[B315]]) |
| independent fingerprint reference | CPython 3.11 `hashlib.blake2b(digest_size=64)`, self-validated against the **RFC 7693 Appendix A** vector for BLAKE2b-512("abc") before emitting a value; it also REPRODUCED all three reused Slice-3 literals exactly |

### 0.1 Explained concurrent inputs — QA's and the Author's, ⛔ NOT this slice's

Ten Markdown paths moved under other people's hands while this slice ran. Each was relayed to me at the time and
each is documentation read by **no build, probe, generator or battery**:

* ` M docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md` — QA appended **R-RA-31**. I READ it to check it
  for Slice-4 obligations: it is entirely Slice-5 scoped (on-air bootstrap reply + the Slice-5 `sizeof(Node)`
  re-pin) and changes nothing here.
* ` M tracker.md`, ` M MEMORY.md`, ` M docs/2026-07-30-open-bug-register.md`,
  ` M docs/2026-07-31-bench-test-script.md`, ` M docs/superpowers/specs/2026-08-23-…-design.md`
* `?? docs/superpowers/plans/2026-09-06-radmin-slice5-precheck.md`,
  `?? docs/superpowers/plans/2026-09-06-radmin-slice5-target-session.md`,
  `?? docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md`
* `?? docs/superpowers/plans/2026-09-06-radmin-slice6-precheck.md`,
  `?? docs/superpowers/plans/2026-09-06-radmin-authority-classification-proposal.md`

⛔ I did not stage, read (beyond R-RA-31), move or edit any of them, and they are EXCLUDED from §13's inventory.
⛔ The coder fence forbids editing the register / bench / manual / design / rulings / QA ledgers, and none of those
edits is mine.

### 0.2 ⚠ ONE MEASUREMENT ERROR I MADE, AND HOW IT WAS CORRECTED

While removing three mutation entries (§8.2) my splice script over-ran a list boundary and **deleted the whole
`MUTS_TEAMKEYRING` header** from `tools/probe_ui_model_mutations.py`. The very next battery run FAILED LOUD with
`NameError: name 'MUTS_TEAMKEYRING' is not defined` — i.e. the tool refused rather than silently measuring a
merged battery. I restored the six deleted lines verbatim and re-verified with
`git diff … | grep '^-'`: the **only** removed line in the whole file is now the `PIN_CASES` line it replaces.
Every mutation battery reported here was then re-run from a FRESH staging copy. Reported rather than repaired
silently.

---

## 1. Base captures — taken BEFORE the first edit, with the predictions written against them

| gate | BASE |
| --- | --- |
| `pio test -e native` → `./.pio/build/native/program` | **2702 cases / 116902 assertions / 0 failed** (the wrapper printed its usual false *"0 test cases"*; the figure is the BINARY's) |
| `run_corpus.py --jobs=8 --require-anchors` | **PASS: 36/36 · anchors 36/36 reproduce `simulation/BASELINE.md`**; s18 `32afbf11` / 269517 |
| `lus` md5 | `b1b1d92c` (the base `cmake --build` did **5** build actions — a settling rebuild of `remote_codec.cpp` — and produced the byte-identical binary; those actions belong to the base capture) |
| `measure_board.py pair` | gateway **RAM 195844 · flash 530972 · 284 obj**; heltec_mobile **RAM 205684 · flash 1355292 · 328 obj** |
| one-off `xiao_mobile` (R-RA-30) | **RAM 170516 · flash 577644 · 284 obj** |
| `probe_board_abi.py` / `probe_b278_row_abi.py` | 191 checks, 9/9 controls RED / 42 measurements, 6/6 controls RED |
| `probe_console_sink` | `profiles=6 checks=720 structural=39 ble_guard=480 ownership=6 ownership_controls=3 controls=78 unusable_controls=0` |
| `probe_inbox_verbs` | 137 checks (pin 137) · 30 controls / 0 unusable |
| `probe_firmware_ui` / `probe_custody_usb` / `probe_ble_line` | 223 controls / 27 checks + 10 controls / 40 checks + 8 controls |
| `probe_features` | 9 cells · 118 checks · 44 controls · ownership 25 controls / 0 unusable |
| tools sweep | `Ran 320 tests` … `OK`, exit 0 |
| `gen_command_inventory.py` (bare + `--check`) | PASS, **188** command rows |
| help/router projection | full_oled 44 · full_headless 43 · gateway 41 · gateway_oled 42 · mobile 38 · mobile_oled 39; parser 7 everywhere; intersection EMPTY; **union 51** |
| `warning_census.sh` | PASS — 6 envs at their pins (`gateway_heltec` 173 · `gateway_heltec_v4` 178 · `heltec_mobile` 177 · `heltec_v3` 177 · `heltec_v4` 182 · `heltec_v4_mobile` 182), `-Wswitch` **0** |
| `check_a0_matrix.py` · `check_data_type_literals.py` · `git diff --check` (both repos) | all exit 0 |

### 1.1 Predictions, written before the first edit

1. native cases **+N**, N derived per file; 2. inventory 188 → 188+N with N enumerated; 3. help +2 on the two
mobile profiles ONLY, union +2, `full_*`/`gateway*` unchanged (B320); 4. Node + B278 ABI **unmoved**;
5. gateway RAM **±0** and gateway flash **±0 EXCEPT** the attributed [[B321]] wipe; 6. heltec_mobile RAM
**+2056** for the sole resident book plus anything separately attributed; 7. corpus **36/36 identical** with
**zero** post-edit build actions, plus a separate recompile control that MUST act; 8. census multiset extended,
never swapped.

**Every one of these held.** The single miss is stated in §5.1: the inventory grew by **16** rows, not the 15 I
first derived — the extra row is the chained `admin-key show self` arm, which the delivered schema records as its
own sub-verb row. The brief warned about exactly this ("derive total row delta before edit rather than equating 13
forms to every generated row"); I derived 13 forms + 2 family rows and missed the chained one.

---

## 2. What changed, per production file

| file | what |
| --- | --- |
| `src/device_nv.h` | the two v1 records (`MgmtKeyRow` 36/1, `MgmtKeyBlob` 368/4 `'MRMK'`, `TargetRow` 64/4, `TargetBlob` 2056/4 `'MRTB'`) with per-ABI size/align/offset asserts, the four-state `MgmtKeyRead`/`TargetRead` + their `blob_state`/`blob_init`, the `kSlotMgmtKeys`/`kSlotTargets` entries in the `"mr"` namespace, and the typed `load_/save_` wrappers. ⛔ Not in `mount_or_repair()`'s probe list. |
| `src/firmware_admin_keyring.h` (NEW, 418 ln) | the `/mrmkeys` service: content policy, four-state composition, `IMgmtKeyStore` + the reused `IAdminSeedSource`, the future-caller `IMgmtKeyUse`, and list/show/show-self/generate/import/export/remove/reset over ONE mint path. All transients `SecretWipeGuard`-ed. |
| `src/firmware_admin_targets.h` (NEW, 455 ln) | the `/mrtargets` service: the trust/routing split, the whole content policy, the **mask→verdict** selector split that makes `ambiguous` reachable, and add/set/remove/reset over ONE commit path with restore-on-failure. ⛔ Owns no storage — the book is the caller's buffer. |
| `src/firmware_admin_client_verbs.h` (NEW, 673 ln) | the frozen grammar and every emitted byte, the bounded numeric/layer/label/slot parsers, four fixed pages of eight physical slots, R-RA-30's `admin_client_ble_refuses` predicate, and the CLIENT `regen` admission + warning. |
| `src/firmware_config_parse.h` | **[[B321]] ONLY**: a direct `#include "monocypher.h"` and a function-local RAII guard over `parse_hex32`'s existing 32-byte scratch. Grammar and output-on-failure unchanged. ⛔ No NV/team-keyring include, no guard relocation, no second decoder. |
| `src/firmware_commands.cpp` | the CLIENT bindings (typed stores, the CHECKED draw, the two no-reference future-caller predicates, the no-debt regen predicate, the `AdminClientPrintLines` sink), the ONE resident `static mrnv::TargetBlob s_targets`, the two entry points, `admin_client_router_arm`, the boot report, the dispatch arm, and `do_regen`'s admission + warning. |
| `src/firmware_commands.h` | one CLIENT-gated boot declaration, ⛔ no `#else` stub. |
| `src/fw_main.cpp` | the CLIENT-gated `admin-client` BLE refusal before the seam, the CLIENT-gated boot call, and the `firmware_admin_client_verbs.h` include (§4.1). |
| `src/firmware_help.h` | two CLIENT-gated bare names, bytewise ascending and contiguous with the ACCEPT pair. |

⛔ **NOT TOUCHED:** `lib/core/`, `lib/console/`, `platformio.ini`, any build filter, any legacy remote code, any
radio/codec/session/custody consumer, the main schema, `wire_version`, the simulator, `simulation/BASELINE.md`.

---

## 3. The records, and what they measure

`MgmtKeyRow` seed[32]@0 + reserved[4]@32 = **36 / alignof 1**; `MgmtKeyBlob` magic@0 `0x4D524D4B` + version@4 +
count@6 + rec[10]@8 = **368 / alignof 4**. `TargetRow` admin_pub[32]@0, key_hash32@32, hops[4]@36, hop_count@40,
label_len@41, label[16]@42, flags@58, reserved[5]@59 = **64 / alignof 4**; `TargetBlob` magic@0 `0x4D525442` +
version@4 + count@6 + rec[32]@8 = **2056 / alignof 4**. Both magics were checked against **all eight** shipped
ones (`test_device_nv.cpp`, and again in the two service suites). Every figure is asserted per-ABI in
`device_nv.h` and re-asserted on the host, so a silent layout move breaks ARM, Xtensa AND native.

⚠ The two records are **NOT** in `mount_or_repair()`'s `kFiles[]` — structural rows S36/S47 pin that the list is
still the SAME SIX. [[B317]]'s converse (a reformat triggered by one of those six wipes these two as well) is
**NOT closed** and is marked beside both stores in source.

---

## 4. Two REAL defects the gate found, both mine

### 4.1 ⚠⚠ A BOARD-ONLY BUILD FAILURE — `fw_main.cpp` never included the header its guard calls

`heltec_mobile` and `xiao_mobile` **failed to compile**:

```
src/fw_main.cpp:617:15: error: 'admin_client_ble_refuses' is not a member of 'mrfw'
```

**Why no earlier instrument could see it.** `gateway` compiles the guard OUT (`MR_FEAT_RADMIN_CLIENT` 0), the
native suite compiles **no** `src/*.cpp` at all (`test_build_src = no`), and every host probe that reads
`fw_main.cpp` **EXTRACTS** the guard's condition into a generated TU rather than compiling the real one. ⇒ the
two-board pair was the ONLY gate that could fail on it — exactly the [[B169]] shape. Fixed by adding the include
(with that history recorded at the include site) and re-measuring the pair, the one-off and the census from
scratch.

### 4.2 The probe's NV fake had been silently dropping every `/mrpeers` record

`MrProbeNvSlot::data` was 512 bytes and `put()` refuses anything longer, so a **1160-byte** `PeerBlob` was never
stored while the write was still counted. Every `peerkey` therefore re-read an ABSENT store, `peer_rec_put`
answered `inserted`, and X8's `writes == 1` passed **without ever exercising production's wear guard**. Enlarging
the medium for the 2056-byte book exposed it (X8 went red). Closed by pinning a genuinely NEW peer on that arm and
by adding **X8b**, which asserts the coalescing production has always had: re-pasting an identical key is
`unchanged` and costs **zero** writes. Registered below as **[[B327]]**.

---

## 5. Surfaces, transports and the profile axes

### 5.1 The command inventory: 188 → **204** rows (+16), enumerated

* 2 top-level family rows (`admin-key`, `admin-target`) from the new `admin_client_router_arm` surface, gate
  `MR_FEAT_RADMIN_CLIENT`, transports **`serial`** — a BARE family name is console-only under R-RA-30.
* 13 ruled sub-verb forms (7 key + 6 book).
* **1 chained row** — `admin-key show self` — which the delivered schema records as its own sub-verb arm because
  `self` is a second comparison site inside `show`. **This is the row my pre-edit derivation missed.**

R-RA-30 is recorded **per row**: `Surface` gained a literal `sub_transports` column, and exactly five rows carry
`serial,ble` — `admin-key list`, `admin-key show`, `admin-key show self`, `admin-target list`, `admin-target show`.
`test_gen_command_inventory.py` pins that set by name AND names the nine secret forms that must NOT be in it, and
a separate case proves the override never widened a surface it was not spelled on (`acl list` stays `serial`).

### 5.2 [[B319]]'s twin — the `MR_FEAT_RADMIN_CLIENT` profile axis

Six literal ruled values (`full_* 0 · gateway* 0 · mobile 1 · mobile_oled 1`), ⛔ never computed. The new
`TestRadminClientAxis` evaluates **all four** ACCEPT×CLIENT combinations through `eval_gate` (including the two the
product table never carries — `{1,1}` IS the host, `{0,0}` is nothing), preserves the unknown-axis refusal, and
greps the generator's own source to prove the column is not derived from ACCEPT or from `MR_FEAT_MOBILE`.

### 5.3 Help / router projection — the asymmetry IS the product gate

| profile | base | final |
| --- | --- | --- |
| full_oled / full_headless | 44 / 43 | **44 / 43 — UNCHANGED** |
| gateway / gateway_oled | 41 / 42 | **41 / 42 — UNCHANGED** |
| mobile / mobile_oled | 38 / 39 | **40 / 41 (+2 each)** |
| union | 51 | **53** |

Parser 7 on every profile; router∩parser **EMPTY** on all six.

---

## 6. The instruments, extended

### 6.1 `tools/probe_inbox_verbs` — TWO independently compiled product arms

The runner now re-execs itself once per arm. Each arm compiles **every** object — the production TU, `lib/core`,
`lib/console`, `lib/hal`, monocypher and `probe_main.cpp` — under **its own** defines into **its own** `$OUT`.
Arm 2 = arm 1 + `-DMR_PROFILE_MOBILE`, which is literally `platformio.ini`'s own `[env:heltec_v3]` →
`[env:heltec_mobile]` delta (`extends` + one flag).

⚠⚠ **ONE MEASURED LIMIT, stated rather than glossed:** neither arm sets `-DMR_FEAT_OLED=1`, although both real
envs do. That is arm 1's pre-existing shape and it is **not free** to change: with OLED on,
`firmware_commands.cpp` references `mrfw::ui_emergency_active()`, which lives in `src/firmware_ui.cpp` — a TU this
probe does not compile and could not without the whole U8g2/board_ui canvas. **Measured, not assumed:** the link
fails with exactly that undefined symbol. The OLED axis of the console surface is covered instead by
`probe_console_sink`'s six-profile matrix, which includes `mobile_oled`. Registered as **[[B328]]**.

| arm | checks | controls |
| --- | --- | --- |
| ACCEPT (`heltec_v3`-shaped) | **146** (base 137 + 8 [[B321]] rows + X8b) | **30** / 0 unusable |
| CLIENT (`heltec_mobile`-shaped) | **178** | **33** / 0 unusable |

The CLIENT arm's **78** new rows drive the REAL `mrfw::dispatch()` over the REAL `src/firmware_commands.cpp`:
both families are owned and **the TARGET half is proven ABSENT** (Q1c/Q1d — the mirror of R30c); `show self`
answers at **zero** reads and zero draws; `generate` costs exactly one write and one full 32-byte draw and the
bytes land on the `/mrmkeys` backend key and **not** on `/mradmid` or `/mrtargets`; a dead platform draw refuses
`entropy_failed` having ASKED; a refused medium answers `nv_save_failed` after exactly one attempt; `export`
returns exactly the imported seed at zero writes; **ten** ordinary refusals are each measured as
`writes==0 && draws==0` and no foreign handler and no global sink; the book runs end to end over the ONE resident
scratch and two identical listings agree byte for byte; the boot report is two exact lines at zero writes with no
key/seed/fingerprint byte; a dead backend makes both records `io_failed` and **even both confirm-gated recoveries
refuse**; and ★ `regen` is driven with both controller records provisioned and **both come back byte-identical**,
with the ruled warning on the supplied sink and ⛔ absent on the failure path.

### 6.2 [[B321]] — the wipe is EXECUTED, not grepped

`parse_hex32`'s scratch is a stack frame of a function that has already returned, so no `test/` case can observe
it without reading dangling storage. The probe links with `-Wl,--wrap=crypto_wipe` and its `__wrap_crypto_wipe`
copies the bytes, performs the **real** wipe and re-reads the **same live storage** — fully-defined behaviour.
Rows **Z1..Z8**: exactly ONE 32-byte wipe per decode, over the buffer that held the decoded key material, read
back all-zero; the same on a post-allocation **refusal**; **no** wipe at all for a null input (no scratch is
allocated); and Z8 proves the interposer is actually linked. Control **C40** deletes the guard and is RED on both
arms.

### 6.3 `tools/probe_console_sink`

| pin | base → final | derivation |
| --- | --- | --- |
| structural | 39 → **50** | S40..S50: the CLIENT boot call is once, gated, and AFTER the mount · it writes nothing and draws nothing · ★ **exactly ONE** resident controller buffer and it is the PUBLIC book (a COUNT, not an absence) · ★★ ⛔ **NO** resident keyring/service/seed · no Node link · ★★ the wrappers address `kSlotMgmtKeys`/`kSlotTargets` and **neither touches `kSlotAdmid`** (design §6.4 as source) · [[B317]]'s probe list · `do_regen`'s write set is STILL `{/mrid}` · `handle_leave`'s STILL `{/mrcfg}` · the CLIENT guard's OWN `admin-client` envelope |
| ble_guard | 480 → **905** | help 53×4=212 · admin 67×4=268 · **client 85×5=425**. The fifth assertion (D4) is the one a whole-family guard cannot express: *a RULED PUBLIC form must NOT be refused.* |
| controls | 78 → **99** | +21: 5 executed guard sabotages (incl. **both** failure directions), 6 extraction REFUSALS (incl. ★ the envelope COLLIDING with the target family's), 10 structural sabotages |

⛔ Not one of S1..S39 moved, and every new structural row has its own sabotage control.

### 6.4 The feature-ownership census

`APPROVED_SITES` extended with the exact new multiset — `firmware_commands.cpp` **+4** (bindings · regen
admission · regen warning · dispatch arm), `firmware_commands.h` **+1**, `fw_main.cpp` **+2** (BLE split · boot
call), `firmware_help.h` **+1** — and the check compares the **sorted list**, so swapping a CLIENT gate for an
ACCEPT one changes no count but fails. Eight new sabotage controls in the same five shapes as slice 3's six,
including ★ `do_regen`'s CLIENT admission inverted onto ACCEPT and BOTH new pure headers acquiring a capability
macro. Ownership controls 25 → **33**; probe controls 44 → **52**; cells 9 and checks 118 **unchanged**.

---

## 7. Final gate

| gate | result |
| --- | --- |
| `pio test -e native` → `./.pio/build/native/program` | **2763 cases / 118344 assertions / 0 failed** |
| `run_corpus.py --jobs=8 --require-anchors` | **PASS: 36/36 · anchors 36/36** · s18 **`32afbf11` / 269517 / 0** — IDENTICAL |
| `lus` post-edit build | **ZERO build actions**, md5 **`b1b1d92c`** unchanged |
| ★ recompile control | touching `lib/core/remote_codec.cpp` → **5 build actions**; the file restored (md5 `281acaa…` before **and** after) and `lus` back to `b1b1d92c` |
| `measure_board.py pair` | gateway **195844 / 531004 / 284** · heltec_mobile **207740 / 1367448 / 328** |
| one-off `xiao_mobile` | **172572 / 664636 / 284** |
| `probe_board_abi.py` / `probe_b278_row_abi.py` | **191 checks, 9/9 RED** / **42 measurements, 6/6 RED** — ⛔ no ABI re-pin made or needed |
| `probe_console_sink` | **PASS** — `profiles=6 checks=720 structural=50 ble_guard=905 ownership=6 ownership_controls=3 controls=99 unusable_controls=0` |
| `probe_inbox_verbs` | **BOTH ARMS PASS** — 146/30 and 178/33, 0 unusable |
| `probe_firmware_ui` / `probe_custody_usb` / `probe_ble_line` | **PASS** — 223 controls / 27+10 / 40+8, all UNCHANGED |
| `probe_features` | **PASS** — 9 cells (pin 9), 118 checks (pin 118), 52 controls (pin 52), ownership **33** / 0 unusable |
| `probe_features --no-neg` | run and reported as **PROBE-ONLY — NOT A GATE** |
| `gen_command_inventory.py` `--write` then bare then `--check` | **PASS**, **204** rows, byte-for-byte |
| `warning_census.sh` | **PASS** — 6 envs at their pins, `-Wswitch` **0**, **ZERO new warnings** |
| `check_a0_matrix.py` · `check_data_type_literals.py` · `git diff --check` ×2 | exit 0 |
| tools sweep | **`Ran 329 tests in 444.049s`** … **`OK`**, exit 0 read from the command itself (⛔ never from a `tail` window and ⛔ never from `$?` after a pipe — [[B326]]). 320 → 329 = **+9**, and the derivation is exact: `grep -c '    def test_' tools/test_gen_command_inventory.py` moved 55 → 64, and no other wrapper gained a method. ⓘ The sweep's output contains one line reading `FAIL: §B278 census selftest, 9 failure(s)` — that is a PRE-EXISTING control FIRING (`test_b278_correlation_census` sabotages its binder and asserts the battery bites, then restores it), which is why unittest still reports `OK`. Named here so the next reader does not mistake it for a failure. |

### 7.1 Board attribution, by symbol

| env | RAM | flash | attribution |
| --- | --- | --- | --- |
| **gateway** (ACCEPT, ARM) | 195844 → **195844 — ±0** | 530972 → 531004 (**+32**) | **⛔ ZERO `mrfw::admin_client_*` symbols and ZERO `s_targets`** — the whole CLIENT family is verifiably ABSENT. The entire movement is 1 new jump-table constant (+16), 1 gone (−32) and 3 moved (+60), dominated by **+44 inside `mrfw::acl_verb`**, which is where `parse_hex32` inlines. ⇒ the ONLY semantic change reaching an ACCEPT board is [[B321]]'s wipe — exactly the qualified exception the brief authorises. |
| **heltec_mobile** (CLIENT, Xtensa) | 205684 → **207740 (+2056)** | 1355292 → 1367448 (**+12156**) | **+2056 is EXACTLY `s_targets`**, the one resident book — no other RAM moved. 84 new symbols / 12572 B, ALL `mrfw::` CLIENT code (`admin_target_verb` 2486, `admin_key_verb` 2446, `s_targets` 2056, `MgmtKeyService::mint_` 730, …). Six symbols moved by ≤18 B, of which `parse_hex32` **+18** is the B321 wipe. |
| **xiao_mobile** (CLIENT, ARM, R-RA-30 one-off) | 170516 → **172572 (+2056)** | 577644 → 664636 (**+86992**) | Same **+2056** = `s_targets`. 70 new symbols / 88392 B, all `mrfw::` CLIENT; the ARM `-O2` inliner expands the pure services into their verbs (`admin_target_verb` 20264, `TargetService::add` 15078, `set` 10506). ⇒ 88392 − 2056 (.bss) + 368 moved = 86704 against a measured .text **+86992**; the ~288 B residue is section alignment. ⛔ This is an evidence-only capture — not a third ruled board and not an ABI pin. |

The one-off used the brief's adapter verbatim after verifying `ENVIRONMENTS`, `validate_output_dir`,
`MeasurementLock` and `run_measurement` all exist, and after verifying `xiao_mobile` really produces
`firmware.hex` (it `extends = env:xiao_sx1262`, exactly as `gateway` does). It ran sequentially, outside the pair,
under the same lock, into a fresh dir below `.pio-measure` and outside `.pio-measure/env`.

### 7.2 `PIN re-synced?`

`PIN re-synced? YES — tools/probe_ui_model_mutations.py PIN_CASES, PIN_ASSERTS 2702, 116902 -> 2763, 118344.
+61 cases DERIVED PER FILE: test_firmware_admin_keyring.cpp 21 + test_firmware_admin_targets.cpp 15 +
test_firmware_admin_client_verbs.cpp 21 + test_device_nv.cpp 2 ADDITIVE + test_firmware_config_parse.cpp 2
ADDITIVE = 61 (grep -c '^TEST_CASE' on the five). Assertions +1442. ⛔ Not one pre-existing case was removed or
renamed: the base tree was measured at 2702/116902/0 before the first edit and the delta is wholly additive.`

---

## 8. Mutation coverage — both selectors, the union gated

Isolated staging, `--workers=2`, restoration proved by the harness's own *"real tree untouched: all N target files
byte-identical to launch (md5); no build ran in <stage>"* line on every battery.

| selector | target | mutants | RED | unusable / survivors |
| --- | --- | --- | --- | --- |
| changed-source | `devicenv` | 42 | 42 | **0** |
| changed-source | `cfgparse` | 8 | 8 | **0** |
| changed-source | `sliceDtoken` | 2 | 2 | **0** |
| changed-source | `radmin4key` | 28 | 28 | **0** |
| changed-source | `radmin4targets` | 31 | 31 | **0** |
| changed-source | `radmin4verbs` | 30 | 30 | **0** |
| dependency | `radmin3id` | 23 | 23 | **0** |
| dependency | `teamkeyring` | 68 | 68 | **0** |
| dependency | `radmin3acl` | 36 | 36 | **0** |
| dependency | `radmin3verbs` | 28 | 28 | **0** |
| **UNION (deduplicated)** | 10 batteries | **296** | **296** | **0 / 0** |

`cfgparse` **and** `sliceDtoken` are both run because both target `src/firmware_config_parse.h`, the file [[B321]]
touched. `radmin3acl`/`radmin3verbs` are included because they are a **real acceptance dependency**: the client
verb header includes `firmware_admin_verbs.h` and REUSES `IAdminLines`, `admin_emit`'s siblings, the bounded token
scanners and `admin_primary_is` — not merely a type import. `radmin3id` and `teamkeyring` are the reused
fingerprint / checked-seed / `SecretWipeGuard` authorities.

### 8.1 ★ Round 1 produced SEVEN survivors. Every one is accounted for, and four became TESTS.

| survivor | what it proved | resolution |
| --- | --- | --- |
| **K24** `export` over an unreadable store | a REAL gap — deleting the state gate let the fake's 0xA5 garbage be returned **as a seed** | new assertions in the keyring suite: the refusal, its reason, and `admin_buf_all_zero(seed)` |
| **T13** the `label=` selector as a PREFIX | a REAL gap — every row in the case carried a ONE-character label, so `alpha` vs `al` was never asked | a 5-char stored label added; `label=al` and `label=alphax` must both resolve to nothing |
| **V09** the `0x` prefix dropped | a REAL gap — `abc`, `1234`, `deadbeef` all parse as hashes without it (the id-vs-hash ambiguity returns) | those three plus three more added to the refusal corpus |
| **V30** `show self` with a trailing token | a REAL gap — the ONE arm that answers without touching the store had no trailing-token row | ` show self x` / ` show self  x` added |
| **V18** `ambiguous` renamed to `not_found` | reachable only by a DIRECT call (a real BLAKE2b collision cannot be constructed) | a new case drives `target_emit_sel_err` over all four verdicts |
| **V26** the emitter's length gate | "unreachable by construction" is a claim about the CALLERS, not the emitter | a new case calls `admin_client_emit` at the bound, past it, at 0 and at −1 |
| **T16** `add` appends past the holes | **a DEFECTIVE MUTATION** — my reverse loop kept assigning, so it still found the lowest free slot (an equivalent mutant) | the mutation fixed to `{ slot = i; break; }`; now RED |

### 8.2 Four entries were WITHDRAWN, each with its measurement and its replacement cover

* **N-S4-8 / N-S4-9 / N-S4-10** (`load_targets` reads the keyring slot · `save_mgmt_keys` writes the target slot ·
  the wrappers stop asking for `SlotIo`) live INSIDE the platform `#if`. The host takes the no-backend arm, where
  `read_slot` always answers `kSlotAbsent` and `write_slot` always answers false **regardless of the slot** ⇒ each
  is unobservable from `test/` **by construction**, and each survived when run. ★ Their EXECUTED cover is
  `probe_inbox_verbs` **C37/C38/C39** on the CLIENT arm, all three verified RED. This is the SAME resolution
  slice 3 reached for the identical three on `/mradmid` + `/mracl` (its C27..C29).
* **T21** (the composed-candidate validation dropped) is an **equivalent mutant**: every mutation in
  `TargetService` starts from a book that already passed `target_content_valid` and changes it in exactly one
  permitted way. The line is KEPT in production (it is the belt Slice 8a's composing caller will need) and is now
  marked **unreddenable** in the source — the `/mracl` idiom.
* **P-B321-1/2/3** were all three written and all three survived, for the reason §6.2 gives. The battery now
  carries a note saying so and naming Z1..Z8 + C40 as the executed cover; a permanently-red entry would have
  taught the next reader that a survivor is normal there.

⛔ **No unknown survivor. No baseline failure. No lost worker. No unusable control. No mutation ran concurrently
with a board capture in the measured checkout** (every battery ran from `stage/` or `stage2/`).

---

## 9. NV and stack budget (§8.6)

New serialized storage is **368 + 2056 = 2424 bytes**, independent of every other store.

| profile | coexisting records | serialized bytes |
| --- | --- | --- |
| CLIENT (mobile) | `/mrcfg` + `/mrid` + `/mrpeers` 1160 + `/mrjoin` + `/mrteams` + `/mrui` 372 + **`/mrmkeys` 368** + **`/mrtargets` 2056** | the two new records add 2424 |
| ACCEPT (static/gateway) | the same minus the two new, plus `/mradmid` 40 + `/mracl` 368 | **unchanged — ⛔ 0 bytes added** |

⚠ That is a **raw-byte** sum and is deliberately **not** a guaranteed-fit claim: NVS carries per-entry metadata
and a page structure, LittleFS carries per-file metadata and block granularity, and the durable inbox consumes the
same medium. The measured RAM figures are the load-bearing ones: `.bss` **+2056 on both CLIENT ABIs and +0 on the
ACCEPT ABI**, attributed to `s_targets` by symbol on all three.

**Stack.** The keyring's 368-byte record is an automatic transient inside the service; the book is resident
precisely so the 2056 bytes never land on the nRF52 loop task's FIXED 4 KB stack, where this tree has already
HARDFAULTED once. The composed startup path is `setup()` → `admin_client_stores_boot_report_console()` → one
`MgmtKeyBlob` (368) + the derivation's `Identity` (196) + the emitter's 192-byte line buffer + the two services'
references — well under a kilobyte, with the book NOT on it. The console path adds the parser's bounded scratch
(66 B hex + 32 B seed) and a 64-byte row edit buffer. ⛔ **I did not measure a hardware stack high-water mark:**
that needs the device, and it is bench Part 56's. Nothing here exceeds the accepted budget, so no STOP fired.

---

## 10. STOP audit — every condition evaluated

| # | condition | verdict |
| --- | --- | --- |
| 1 | base mismatch / dirty start / unexplained concurrent input | **NOT FIRED.** Base and status verified before anything; the simulator untouched. The concurrent Markdown moves are §0.1's, each relayed and each documentation-only. |
| 2 | an owner decision needed · out-of-fence edit · remote/codec/Node consumer · schema or wire bump · simulator mover | **NOT FIRED.** No capacity/RAM/NV decision was needed (§9); zero files outside the fence; zero codec/session/Node consumers; no schema or `wire_version` change; the simulator is byte-identical. |
| 3 | unreadable-state writes · implicit recovery/default/eviction · seed leak · missing wipe · duplicate principal · partial save acknowledged · failed candidate activated | **NOT FIRED**, and each is EXECUTED: `io_failed` refuses even the confirm-gated recoveries (native + probe); no eviction anywhere (the 33rd target is `full`); the only seed-bearing line is `export`; the wipe is observed on the real call (§6.2); duplicates are refused against self AND every other slot; a failed save publishes nothing and the candidate is restored. |
| 4 | client mutation crosses BLE · public list/show disabled there · target listing escapes its old guard · a synthetic/mixed-profile arm · generated inventory or census differs from its literal contract | **NOT FIRED.** 425 executed guard checks over 85 rows, with both failure directions controlled; the target family's guard and envelope are untouched and still RED under D-C9; both router arms are REAL and independently compiled; the inventory is byte-identical under `--check` and the census multiset is literal. |
| 5 | book residency/size/capacity changed · stack/NV budget fails · compile-out fails · unexplained RAM/flash · a non-comparable one-off | **NOT FIRED.** 32 rows, 2056 bytes, ONE resident buffer (structural S43/S44); every byte of RAM and flash attributed by symbol on all three ABIs; compile-out proven by the ABSENCE of every `mrfw::admin_client_*` symbol on gateway; the one-off used identical fixed-identity paths at base and final. |
| 6 | a failed standing gate · corpus delta · survivor · unusable control · lost worker · count/anchor mismatch · absent restoration · skipped mandatory gate | **NOT FIRED at the end.** ⚠ It DID fire twice during the run — the board build failure of §4.1 and the seven round-1 survivors of §8.1 — and both are reported above with their output rather than smoothed away. Final: 0 survivors, 0 unusable, 36/36 anchors, every restoration proved. |
| 7 | a pending-request/retained-result or metal arm reported as production-executed · evidence omitting a finding or bench residue | **NOT FIRED.** Every busy-predicate test is labelled a **future-caller service test**; the production bindings answer "nothing in use" and "no debt"; §11 names the bench residue. |

---

## 11. What is NOT proven here

* ⛔ **No metal.** Bench **Part 55b** (controller-side two-node USB trust exchange) and **Part 56** (real
  seed-store/export/import lifecycle + secured-BLE public list/show vs mutation refusal on `xiao_mobile`) are the
  Author's to draft and the owner's to run. **Part 59** gains the conditional client `regen` warning. None is run.
* ⛔ **No BLE transport.** A `LineSink`-shaped sink is a sink, not a link. R-RA-30's guard is measured by
  extraction-and-execution, and a bond is not physical presence.
* ⛔ **No power cut, no flash wear, no real filesystem.** Every store is a fake.
* ⛔ **No entropy claim.** The seam's `true` means only "32 bytes arrived and are not all zero". **[[B312]] stays
  OPEN.**
* ⛔ **[[B317]] stays OPEN**: a reformat triggered by one of the six probed files erases both new records too.
* ⛔ **No on-air RPC, session, retained result or remote issuer.** Slice 4 can hold keys and targets and send
  NOTHING. The 36-scenario corpus is inert **by construction** and measured identical.

---

## 12. Register rows proposed (⛔ the Author closes; I do not edit the register)

**[[B327]] — the inbox probe's NV fake silently dropped every record larger than 512 bytes.** `MrProbeNvSlot::data`
was 512 B while `/mrpeers` is 1160 B, so `put()` refused it and `writes` was still counted ⇒ `peer_store_sync`'s
wear guard was never exercised and X8's `writes == 1` passed for the wrong reason. Measured by enlarging the medium
to 2304 B for `/mrtargets`, which turned X8 red. Fixed in place (a genuinely new peer + the new **X8b** coalescing
row). Residual: any other probe fake with a payload cap smaller than the record it models has the same latent
shape.

**[[B328]] — the inbox probe's arms carry a REDUCED board define set (no `MR_FEAT_OLED`).** Both real envs set
`-DMR_FEAT_OLED=1`; neither probe arm can, because `firmware_commands.cpp` then references
`mrfw::ui_emergency_active()` from `src/firmware_ui.cpp`, a TU this probe does not compile (measured: the link
fails with exactly that undefined symbol). The OLED axis is covered by `probe_console_sink`'s six profiles.
Closing it means either compiling the UI TU here or hoisting the emergency predicate behind a seam — a refactor,
not this slice's.

**[[B329]] — `fw_main.cpp`'s BLE guards depend on includes only ONE gate can check.** §4.1's failure was invisible
to native (no `src/*.cpp`), to `gateway` (feature compiled out) and to every host probe (they EXTRACT the
condition instead of compiling the TU). The two-board pair caught it. Worth a structural row asserting that every
predicate `ble_dispatch_line` names has a corresponding include in the same file.

---

## 13. Modified and untracked paths — THIS SLICE'S ONLY

**Modified (tracked):**

| path | +/− |
| --- | --- |
| `src/device_nv.h` | +235 / −0 |
| `src/firmware_commands.cpp` | +164 / −0 |
| `src/firmware_commands.h` | +11 / −0 |
| `src/firmware_config_parse.h` | +19 / −1 |
| `src/firmware_help.h` | +11 / −1 |
| `src/fw_main.cpp` | +36 / −0 |
| `test/test_device_nv.cpp` | +71 / −0 |
| `test/test_firmware_config_parse.cpp` | +50 / −0 |
| `tools/gen_command_inventory.py` | +73 / −13 |
| `tools/probe_console_sink/ble_guard.py` | +122 / −8 |
| `tools/probe_console_sink/negctl.py` | +162 / −2 |
| `tools/probe_console_sink/ownership.py` | +10 / −2 |
| `tools/probe_console_sink/run.sh` | +46 / −8 |
| `tools/probe_console_sink/structural.py` | +76 / −0 |
| `tools/probe_features/ownership.py` | +52 / −0 |
| `tools/probe_features/run.sh` | +16 / −4 |
| `tools/probe_inbox_verbs/fakes/Preferences.h` | +21 / −6 |
| `tools/probe_inbox_verbs/probe_main.cpp` | +479 / −13 |
| `tools/probe_inbox_verbs/run.sh` | +124 / −11 |
| `tools/probe_ui_model_mutations.py` | +379 / −13 |
| `tools/test_gen_command_inventory.py` | +110 / −4 |
| `tools/test_probe_console_sink.py` | +32 / −8 |
| `tools/test_probe_features.py` | +8 / −1 |
| `docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md` | GENERATED by `--write` |

**Untracked (new):** `src/firmware_admin_keyring.h`, `src/firmware_admin_targets.h`,
`src/firmware_admin_client_verbs.h`, `test/test_firmware_admin_keyring.cpp`,
`test/test_firmware_admin_targets.cpp`, `test/test_firmware_admin_client_verbs.cpp`, and this file.

⛔ Every other path in `git status` belongs to QA or the Author — see §0.1. **The work is left UNCOMMITTED; the
owner commits and benches.**
