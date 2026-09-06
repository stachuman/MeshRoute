<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 3 — target identity, ACL and USB provisioning · CODER EVIDENCE · 2026-09-06

Brief: `docs/superpowers/plans/2026-09-06-radmin-slice3-target-stores.md` (Quality-Agent PASS with its B319
fold-in applied), supplied out-of-band to a CLEAN isolated worktree per its §1. Design
`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md` §§6.2-6.6, 12.1, 19 item 3, 19.1 row 3,
19.2; rulings R-RA-6 / R-RA-8 / R-RA-21 / R-RA-26 / R-RA-27 / R-RA-29.

⛔ **NOTHING HERE IS A METAL RESULT.** Every store is a fake or a host-compiled stand-in: no flash, no wear, no
power cut, no real BLE transport, no radio. Bench **Part 55a** remains pending owner execution, and [[B312]],
[[B313]], [[B315]] and [[B317]] all stay OPEN for their separate obligations.

---

## 0. Provenance — where every figure was measured

| fact | value |
| --- | --- |
| MeshRoute worktree (ALL edits and measurements) | `/home/staszek/mr-slice3` |
| base commit | `7299eb90c787c867cba3dc17902e87b838f5447c` ("Slice 3 prep"), verified by `git rev-parse HEAD` |
| worktree status at start | **EMPTY** (`git status --short` produced no output) |
| shared checkout `/home/staszek/MeshRoute` | `7299eb9`, carrying the Author's uncommitted preparation. ⛔ NOT edited, staged, stashed, reset or reverted at any point. The `git worktree add` is the ONE git write this slice made. |
| simulator | `/home/staszek/lora-universal-simulator` @ `868888419c7cc250d7019860d3403a7721ade1fc`, ⛔ UNCHANGED (`git status --short` empty at start and at end) |
| paired simulator build | `/home/staszek/mr-slice3-lus`, configured `-DCMAKE_BUILD_TYPE=Release -DMESHROUTE_DIR=/home/staszek/mr-slice3` |
| independent fingerprint reference | `/home/staszek/mr-slice3-ref/gen_fp_reference.py` + `fp_reference.txt` (outside BOTH repositories) |

### 0.1 The paired build really names the worktree — four independent proofs

```
CXX_INCLUDES = -I/home/staszek/lora-universal-simulator -I/home/staszek/lora-universal-simulator/third_party
               -I/usr/include/lua5.4 -I/home/staszek/mr-slice3/lib/core
               -I/home/staszek/mr-slice3/lib/monocypher/src -I/home/staszek/mr-slice3/lib/console
MESHROUTE_DIR:PATH=/home/staszek/mr-slice3
```

* every compiled `lib/` source resolves under the worktree — `meshroute_core_normal` 380, `meshroute_core_gw` 380,
  `meshroute_console` 19, `meshroute_monocypher` 19, all `/home/staszek/mr-slice3`;
* `grep -r "staszek/MeshRoute" /home/staszek/mr-slice3-lus` → **ZERO hits**;
* the build was configured and built **as base setup, before the first edit**.

⚠ **ONE MEASUREMENT ERROR I MADE, AND HOW IT WAS CORRECTED — reported because a silent repair would be the worse
fault.** The first base capture of the six probes + the tools sweep was launched in the background and was still
running when I applied my first production edit. `tools/probe_features/run.sh` CAUGHT it exactly as designed
(*"the shared checkout MOVED during the controls"* → 1 unusable control, FAIL). ⇒ I saved the work aside, restored
the pristine tracked file with `git show HEAD:src/device_nv.h > src/device_nv.h` (a git READ and a shell
redirect — ⛔ no `checkout`/`reset`/`stash`), removed the untracked headers, **re-verified `git status --short`
EMPTY**, and re-ran the whole probe/tools batch on that clean tree. Every base figure in §1 is from THAT run, and
its final `git status --short` was empty too. The board pair and the corpus were unaffected and are provably so:
both board manifests record `source/git_status_sha256 = e3b0c44298fc1c14…`, the SHA-256 of the empty string, i.e.
a clean tree at build time.

### 0.2 ⚠⚠ A FALSE GREEN I REPORTED, HOW IT HAPPENED, AND THE CORRECTION (QA gate, STOP 7)

**The claim that was wrong.** §1 and §12 of the first draft both recorded
`python3 -m unittest discover -s tools -p "test_*.py"` as **exit 0**. On the FINAL tree that was FALSE: the sweep
ran **`Ran 320 tests … FAILED (failures=2)`**. QA caught it on an independent re-run. ⛔ The old claim is left
visible in this paragraph rather than quietly overwritten.

**Why my batch could not see it — two independent instrumentation faults, both mine:**

```bash
python3 -m unittest discover -s tools -p "test_*.py" 2>&1 | tail -5
echo "### unittest_EXIT=$?"                 # <-- $? is `tail`'s status, NOT python's. Always 0.
```

1. **`$?` after a PIPELINE reports the LAST command's status.** I read `tail`'s exit code and called it the
   sweep's. ⓘ I used `${PIPESTATUS[0]}` correctly for the six probes and for `warning_census.sh` in the very same
   script — and then did not for the sweep, the inventory, `check_a0_matrix` and `check_data_type_literals`. (The
   latter three happen to be honest anyway: their `tail` window contained the word `PASS`, so the CONTENT carried
   the verdict even though the exit code did not. The sweep's did not — see 2.)
2. **`tail -5` truncated the summary.** unittest prints its `FAILED (failures=2)` line after a long tail of
   `ResourceWarning` noise, so the five lines I kept were all `git_rev.py` chatter. Neither the exit code nor the
   visible output carried the failure.

⇒ this is precisely the defect class this codebase's probes exist to prevent — *"an instrument that cannot fail
is not a gate"* — committed in my own ad-hoc reporting harness rather than in a repo instrument. It is raised as
proposed row **B326**.

**The two real failures, and why my own edits caused them.** Both are wrapper tests that hard-coded a number I had
moved, so they were reading a stale pin rather than measuring the new one:

| test | was | why it broke |
| --- | --- | --- |
| `test_probe_console_sink.TestProbeRunner.test_the_ble_refusal_was_measured_not_asserted` | `AssertionError: 480 != 212` | it `re.search`ed the FIRST `BLE-GUARD rows=… checks=…` line only. I generalized the extractor to TWO families and moved `PIN_BLE_GUARD` to 480, so the reader returned the help family's 212 and compared it against 480. **A pin no reader parses is a pin that measures nothing** — here it failed loudly, which was the lucky direction. |
| `test_probe_features.TheGateRuns.test_the_ownership_contract_ran_inside_the_gate` | asserted `"ownership controls: 19 verified / 0 unusable"` | my +6 ownership controls took the census to 25. |

**The fix — both now MEASURE rather than restate:**

* the BLE reader parses **every** `(BLE|ADMIN)-GUARD` line, requires the set to be exactly `{BLE, ADMIN}` (a
  missing family is a silently unmeasured owner ruling), requires `failed=0` **per family**, applies a row floor
  per family, and compares the **SUM** against `PIN_BLE_GUARD` — reporting the derivation in the failure message.
  It also now requires BOTH `extracted help guard:` and `extracted admin guard:` lines, which the old single
  `extracted guard:` assertion would have missed too;
* the ownership assertion is re-derived to **25**, with the +6 itemised in a wrapper comment;
* two drifted labels the generalization left behind were corrected in the same pass (V1): the runner's section
  header, which said `BLE help-refusal` while running two families, and `ble_guard.py`'s module docstring;
* and QA's item (c): in `src/fw_main.cpp` the pre-existing trailer
  `// §cleanup 2026-07-15: console command cluster (dispatch + diagnostics) — moved in batches` had been left
  dangling at the end of my multi-line `firmware_admin_verbs.h` include comment; it is moved back onto its own
  `#include "firmware_commands.h"` line. **Proven COMMENT-ONLY and LINE-COUNT-NEUTRAL** (1806 lines before and
  after; code-only md5 with comments stripped `e8b0fe58500d0e98` on both sides) — [[B254]]'s rule, since a comment
  edit that shifts `__LINE__` is not payload-inert on xtensa.

---

## 1. Base captures — every one taken BEFORE the first production edit, on a tree whose `git status` was EMPTY

| gate | base value |
| --- | --- |
| `pio test -e native` then `./.pio/build/native/program` | **2640 test cases / 115288 assertions / 0 failed** (the wrapper printed its usual false *"0 test cases"*; the figure is the BINARY's) |
| `run_corpus.py --jobs=8 --require-anchors` | **PASS: 36/36 streams produced and validated, 0 failures · anchors: 36/36 rows reproduce `simulation/BASELINE.md`** |
| s18 keystone (READ from the anchor table, never hardcoded) | `32afbf11` / 269517 events / 0 assertion failures |
| paired `lus` md5 | `b1b1d92c` |
| `measure_board.py pair` → `.pio-measure/base-pair` | gateway **RAM 195844 · flash 512092 · 284 objects**; heltec_mobile **RAM 205684 · flash 1355292 · 328 objects** |
| both board manifests' `source/git_status_sha256` | `e3b0c44298fc1c14…` = SHA-256 of the EMPTY STRING ⇒ a provably clean tree at build time |
| `probe_console_sink/run.sh` | PASS — `profiles=6 checks=720 structural=29 ble_guard=212 ownership=6 ownership_controls=3 controls=58 unusable_controls=0` |
| `probe_inbox_verbs/run.sh` | PASS — 91 checks (pin 91), 22 controls / 0 unusable (pin 22) |
| `probe_firmware_ui/run.sh` | PASS — 223 controls verified / 0 unusable |
| `probe_custody_usb/run.sh` | PASS — 27 checks (pin 27), 10 controls (pin 10) |
| `probe_ble_line/run.sh` | PASS — 40 checks (pin 40), 8 controls (pin 8) |
| `probe_features/run.sh` | PASS — 9 cells (pin 9), 114 checks (pin 114), 38 controls (pin 38), ownership 19 controls / 0 unusable |
| `probe_features/run.sh --no-neg` | PROBE-ONLY — **NOT A GATE**, run and reported as such |
| `python3 -m unittest discover -s tools -p "test_*.py"` | exit 0 — ⚠ **but read through the faulty `$?`-after-a-pipe of §0.2.** The base result is nonetheless sound, and for a reason that does not depend on that code: the tree was PRISTINE, so the two wrapper tests and the pins they read were mutually consistent by construction. It is recorded as measured, with the caveat visible. |
| `gen_command_inventory.py` (bare) and `--check` | PASS, **177 command rows**, tracked table byte-identical |
| `warning_census.sh` | PASS — 6 OLED envs at their pins (`gateway_heltec` 173 · `gateway_heltec_v4` 178 · `heltec_mobile` 177 · `heltec_v3` 177 · `heltec_v4` 182 · `heltec_v4_mobile` 182), `-Wswitch` **0** on every one |
| `check_a0_matrix.py` | PASS — 21 enum members, 6 named special rows |
| `check_data_type_literals.py` | PASS — 182 files, no numeric DataType literal survives |
| `probe_console_sink/ownership.py --show` | router **41 / 42 / 39 / 40 / 38 / 39**, parser **7** on every profile, intersection **EMPTY** on all six |

---

## 2. Predictions, written BEFORE the first edit — and what each one turned out to be

| # | prediction | outcome |
| --- | --- | --- |
| P1 | native cases 2640 → 2640 + N, N derived per file and summed | **2702** (+62 = 20 identity + 23 ACL + 19 verbs TEST_CASEs; `test_device_nv.cpp` gained CHECKs but ⛔ no new case). Assertions 115288 → **116902**. ✅ |
| P2 | inventory 177 → 177 + N with N enumerated first | predicted **11** = 2 top-level + 4 `admin-id` subcommands + 5 `acl` subcommands ⇒ **188**. ✅ EXACT |
| P3 | full-build primary-name union 49 → 51 | **51** on `full_oled`; **46** on `mobile_oled` (the CLIENT control: neither family present). ✅ |
| P4 | per-profile router counts +2 on ACCEPT profiles only | 41→**43** · 42→**44** · 39→**41** · 40→**42** · 38→**38** · 39→**39**; parser **7** everywhere; intersection EMPTY. ✅ EXACT |
| P5 | `sizeof(Node)` and the board ABI unmoved | `probe_board_abi.py` **PASS (191 checks, 9/9 controls RED)**; `probe_b278_row_abi.py` **PASS (42 measurements, 6/6 controls RED)**. No ABI re-pin was made or needed. ✅ |
| P6 | heltec_mobile RAM ±0 and live flash ±0 | **RAM +0 · flash +0 · objects +0 · every loadable section +0 · `symbols_sha256` IDENTICAL.** ✅ (the 64 moved bytes are attributed in §6) |
| P7 | gateway RAM ±0, flash grows and is attributed | **RAM +0 · objects +0 · flash +18880, ALL of it `.text`.** ✅ |
| P8 | corpus 36/36 identical with **ZERO** post-edit simulator build actions, plus a separate recompile control | **0 build actions, `lus` `b1b1d92c` → `b1b1d92c`**; control: `touch lib/core/node.cpp` → **5 build actions**, same md5, source restored by hash. ✅ |

---

## 3. The independent fingerprint reference (R-RA-29) — generated BEFORE the helper existed

Generator: `/home/staszek/mr-slice3-ref/gen_fp_reference.py`, run **outside both repositories**, CPython **3.11.2**,
`hashlib.blake2b(digest_size=64)` — a NON-PRODUCTION BLAKE2, ⛔ not `lib/monocypher`.

**The anchor is checked before a single line is emitted** — RFC 7693 Appendix A, BLAKE2b-512("abc"):

```
ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1
7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923   OK
```

The frozen literals (`test/test_firmware_admin_identity.cpp`, `kFpRef`):

| name | ed_pub (64 hex) | fp = BLAKE2b-512(pub)[:8] |
| --- | --- | --- |
| zeros | `0000…0000` | `9ab7a73a97a1a303` |
| ones | `ffff…ffff` | `83b5ade6991342ed` |
| seq | `000102…1e1f` | `5c52920a7263e39d` |
| seq_b0 | first byte `00`→`01` | `06b824f0d590f1e6` |
| seq_swap01 | first two bytes REORDERED | `f808c6cbb729c99b` |
| seq_b31 | last byte `1f`→`20` | `59a25fa5ddc30b96` |
| hi | `e0e1…feff` | `d70a8754d94c8f21` |
| mixed | `9f86d081…0a08` | `00fbe28e23ddf762` |

★ `mixed` is deliberately in the set: its fingerprint begins `00`, so a renderer that dropped a leading zero would
fail on it and on nothing else.

**THE COMPARISON CONTROL IS EXECUTED, not asserted.** `radmin3/id: the comparison DISCRIMINATES` corrupts EVERY
reference value in EVERY one of its 16 characters (8 vectors × 16 = 128 corrupted expectations) and requires each
to DISAGREE — without it, "every vector matches" would also be true of a comparison that compares nothing.

Two further discriminations, from the generator's own published full digest of `seq`
(`5c52920a7263e39d 57920ca0cb752ac6 …`):

* the helper must NOT produce `57920ca0cb752ac6` — that is bytes **8..15**, i.e. a prefix-OFFSET error;
* the helper must NOT produce `9de363720a92525c` — the byte-reversed prefix, i.e. a little-endian integer rendering.

---

## 4. What was built — the fence, and nothing outside it

### 4.1 Production (7 files; the new feature-site multiset is EXACTLY the brief's)

| file | change | `#if MR_FEAT_RADMIN_ACCEPT` sites |
| --- | --- | --- |
| `src/device_nv.h` | the two records, their two slots, two four-state classifiers + inits, two typed wrapper pairs, one `#include <stddef.h>` for the offset pins | **0** (ungated by design) |
| `src/firmware_admin_identity.h` | NEW — the `/mradmid` service, R-RA-29's fingerprint, the checked entropy seam | **0** |
| `src/firmware_admin_acl.h` | NEW — the `/mracl` service, §6.5/§6.6 clause by clause, the ten-slot `static_assert` | **0** |
| `src/firmware_admin_verbs.h` | NEW — the grammar, the emitters, the boot report, `admin_verb_owns` | **0** |
| `src/firmware_commands.cpp` | ungated pure include; ONE bindings + router-arm + boot-wrapper block; ONE dispatch forwarding block | **2** |
| `src/firmware_commands.h` | ONE block declaring the boot wrapper (⛔ no `#else` stub) | **1** |
| `src/fw_main.cpp` | ungated pure include; ONE BLE-refusal block; ONE boot-call block | **2** |
| `src/firmware_help.h` | ONE block, the two names bytewise-sorted at the head of the index | **1** |

`tools/probe_features/ownership.py --root .` derives the census from the sources and reports it:

```
ok O1 the pair is named in CODE by exactly the 7 approved files: lib/core/mr_features.h, lib/core/node.h,
      lib/core/node_mac_rx.cpp, src/firmware_commands.cpp, src/firmware_commands.h, src/firmware_help.h,
      src/fw_main.cpp
ok O4a mr_features.h 9   ok O4b node.h 3   ok O4c node_mac_rx.cpp 6
ok O4d firmware_commands.cpp 2   ok O4e firmware_commands.h 1   ok O4f firmware_help.h 1   ok O4g fw_main.cpp 2
```

⛔ **The three pure headers carry NO capability macro at all** ([[B255]] idiom) — and check **O1** now REJECTS one
that acquires one (new control `W-S3-GATE-PURE`).

### 4.2 The record layouts, pinned per ABI where every board compiles them

| record | layout | size / align | offsets pinned |
| --- | --- | --- | --- |
| `AdminIdBlob` | magic u32 `0x4D524131` 'MRA1' · version u16 = 1 (EQUALITY) · reserved u16 = 0 · seed[32] | **40 / 4**, no tail padding | magic 0 · version 4 · reserved 6 · **seed 8** |
| `AclRow` | ed_pub[32] · role u8 (0/1/2) · reserved[3] | **36 / 1**, no padding | ed_pub 0 · **role 32** · reserved 33 |
| `AclBlob` | magic u32 `0x4D524C31` 'MRL1' · version u16 = 1 · count u16 · rec[10] | **368 / 4**, 368 % 4 == 0 ⇒ no tail | magic 0 · version 4 · count 6 · **rec 8** |

`static_assert(mrnv::kAclSlots == meshroute::kRemoteSlotSessionMax + 1)` lives in `src/firmware_admin_acl.h`, beside
its one `#include "remote_codec.h"`, ⛔ NOT in `device_nv.h` — that header is pulled into ~every firmware TU and
importing the codec's include graph (`<span>`, dm_crypto.h, identity.h) to read one constant would widen all of
them. It is compiled on **every board** anyway, because `firmware_commands.cpp` includes that pure header ungated.

⛔ NO `wire_version` change, NO `mrnv::kVersion` change, NO new `Blob` field, NO Node member, NO simulator edit.

### 4.3 The slot table — factory reset by construction, self-heal untouched

`kSlotAdmid { "/mradmid", "mr", "admid" }` and `kSlotAcl { "/mracl", "mr", "acl" }`. The `"mr"` namespace IS the
factory-reset ruling expressed as data. ⛔ Neither is added to `mount_or_repair()`'s `kFiles[]` probe list —
structural row **S36** pins that the list is still the SAME SIX files, with sabotage control **S-C36**.
⚠ [[B317]] is NOT closed: a reformat triggered by one of those six erases both new records too, and the header
says so at the site.

---

## 5. The instruments — what each one now EXECUTES, and every pin's derivation

### 5.1 `tools/probe_inbox_verbs` — the REAL router, store and entropy path

⛔ **Why this is the wiring gate and the native suite is not:** `platformio.ini`'s `test_build_src = no` keeps every
`src/*.cpp` out of every host build, so the store binding, the entropy binding, the Print adapter and the dispatch
arm are reachable from NO native test. This probe compiles the REAL `src/firmware_commands.cpp` against the real
`lib/core` + `lib/console` and the probe-local ESP32 platform fakes, and drives `mrfw::dispatch()`.

**PIN_CHECKS 91 → 137 (+46), fully derived** — 48 named rows, two of which (`R34`'s five spellings) share one id:

| rows | what is executed |
| --- | --- |
| R30 · b · c · d | both families are OWNED by the real router; `admin-key show self` (Slice 4's CONTROLLER verb) and `aclx` are NOT, and emit zero bytes |
| R31 · b | an ABSENT store answers honestly, with **0 writes and 0 draws** |
| R32 … R32g | generate = EXACTLY ONE durable write, into namespace `mr` key `admid`, exactly `sizeof(AdminIdBlob)` = 40 B; ⛔ `/mrid`, `/mrcfg`, `/mrpeers`, `/mracl` all still absent; the stored record is a valid v1 'MRA1' with a non-zero seed; a reload reproduces the SAME fingerprint; ⛔ the SEED's hex appears NOWHERE in the output; the seed came from EXACTLY **8** platform draws (32 B / 4) |
| R33 | a second `generate` refuses `already_present` — 0 writes, 0 draws |
| R34 ×5 + b | every malformed confirmation is `bad_args` and the whole set costs **0 writes and 0 entropy draws** |
| R35 … R35h | the first-owner ceremony end to end: absent `/mracl` seeded and the row added in ONE write into key `acl` at 368 B; a second owner at slot 1; `updated`; a repeat that is `unchanged` at **ZERO writes**; ⛔ the LAST OWNER refused; the listing's two rows and end line; an operator IS removable |
| R36 | a first owner cannot be granted without a valid administration root — 0 writes, and the ACL is not even READ |
| R37 · b | ★ an all-zero PLATFORM draw refuses `entropy_failed` **having ASKED** (8 draws) and mints nothing |
| R38 | a refused medium answers `nv_save_failed` after exactly ONE attempt |
| R39 · b · c | a wrong-version `/mracl` is `store_invalid`; an ordinary `acl add` over it costs 0 writes; only `acl reset confirm` recovers, in ONE write |
| R40 · b · c | the BOOT wrapper writes nothing, draws nothing, reports BOTH states, and prints ⛔ no key and no fingerprint |
| R41 | the whole response lands on the SUPPLIED sink — `mrcon` 0 B, BLE 0 B |
| R42 … R42g | ★★ the `io_failed` arm on the REAL ESP32 read sequence: a dead NVS (⛔ not merely an unwritten namespace) makes BOTH records `io_failed`, both consoles name it, **EVEN THE CONFIRM-GATED RECOVERIES REFUSE** at 0 writes, and the boot report says so |

**PIN_CONTROLS 22 → 30 (+8).** Each applies ONE tempting wrong edit to a COPY of a REAL source and must turn the
probe RED on named rows:

| control | rows it reddened |
| --- | --- |
| C22 the target-store DISPATCH ARM deleted | 29 rows |
| C23 ★ the seed binding STOPS DRAWING from the platform | 16 rows (R32g, R37, …) |
| C24 the admin-identity store binding stops reading its record | 10 rows |
| C25 the Print adapter re-chooses `mrcon` ([[B279]]'s shape) | 23 rows |
| C26 the READ-ONLY boot report starts WRITING | R40 |
| C27 ★ `load_acl` stops asking the primitive for `SlotIo` | R42b R42d R42f |
| C28 ★★ `save_acl` writes the WRONG slot | R35b … R35h (7 rows) |
| C29 ★★ `load_admin_id` reads the WRONG slot | R32d … R35h (11 rows) |

⚠ **ONE INSTRUMENT DEFECT FOUND AND FIXED WHILE BUILDING C27..C29, recorded because it is the exact shape that
makes a control worthless:** the first `nvh` shadow kind wrote a lone mutated `device_nv.h` into a shadow include
dir. A QUOTED include resolves against the INCLUDING FILE'S OWN directory first, so `probe_main.cpp` (outside
`src/`) picked up the shadow while `src/firmware_commands.cpp` kept the real one — two files defining `mrnv::Blob`,
a hundred redefinition errors, and three controls reporting *"the mutant does not COMPILE"*, i.e. measuring
nothing. The kind now shadows the WHOLE `src/` directory and compiles the router/handler FROM it.

⛔ **What these rows are NOT:** a static ACCEPT HOST profile (`-DARDUINO -DBOARD_HELTEC_V3`), ⛔ not execution of
gateway hardware, ⛔ not a real BLE transport, ⛔ not a flash test. The NV medium is a fake: no wear, no power cut.

### 5.2 `tools/probe_console_sink` — the BLE refusal and the device boundary

**`ble_guard.py` is GENERALIZED, not copied.** It now answers the same question once per FAMILY, and the ADMIN
extraction additionally REFUSES unless the site is inside `ble_dispatch_line`, **BEFORE** `exec_console_line`, and
gated on **EXACTLY** `MR_FEAT_RADMIN_ACCEPT`:

```
extracted help  guard: ((len == 4 || (len > 4 && line[4] == ' ')) && !strncmp(line, "help", 4)) || (len == 1 && line[0] == '?')
BLE-GUARD   rows=53 checks=212 failed=0
extracted admin guard: mrfw::admin_verb_owns(line, len)
ADMIN-GUARD rows=67 checks=268 failed=0
```

**PIN_BLE_GUARD 212 → 480** = 212 (help, UNCHANGED and still executed) + 268 (admin: 67 rows × 4 assertions).
The admin corpus is 2 families × 22 subforms = 44 OWNED rows — malformed subforms included — plus 23 near misses
and foreign tokens, and its `must_refuse` column is **R-RA-29's rule written out row by row**, ⛔ never read off
the predicate under test.

★ **The router and the guard evaluate the SAME predicate**, `mrfw::admin_primary_is` / `admin_verb_owns`, so they
cannot drift — which is precisely how the slice-0a help defect happened (the router grew argument-bearing forms
while the guard still matched `len == 4`). The instrument's job is therefore presence, placement, envelope and
gate, and its nine controls attack exactly those.

**PIN_STRUCTURAL 29 → 39 (+10)** — the two stores' device boundary, S30..S39. **PIN_CONTROLS 58 → 78 (+20)**:

| control | must redden |
| --- | --- |
| A-C1 PARTIAL FAMILY (only `acl` refused) | 44 executed rows |
| A-C2 LISTING-ONLY ESCAPE (`list`/`show` let through) | 24 executed rows |
| A-C3 BROAD `admin` PREFIX (swallows `admin-key`) | extraction REFUSES — the condition no longer calls `admin_verb_owns` |
| A-C4 the refusal DELETED | extraction REFUSES (0 anchors) |
| A-C5 the refusal DUPLICATED | extraction REFUSES (2 anchors) |
| A-C6 the refusal COMMENTED OUT | extraction REFUSES (0 executable anchors) |
| A-C7 the ENVELOPE is `acl` instead of `admin` | extraction REFUSES |
| A-C8 the guard MOVED BELOW the transport seam | extraction REFUSES — *"it refuses nothing"* |
| A-C9 the guard gated on `MR_FEAT_RADMIN_CLIENT` | extraction REFUSES — the R-RA-8 inversion |
| S-C30 / S-C30b / S-C31 | the boot call duplicated · its gate deleted · hoisted above the FS mount |
| S-C32 / S-C33 / S-C34 | the boot path starts writing · a RESIDENT service appears · the bindings reach into `Node` |
| S-C35 / S-C36 | `load_acl` re-pointed at the other slot · `/mradmid` added to the self-heal probe list |
| S-C37 / S-C38 / S-C39 | `do_regen` starts writing the ACL · `handle_leave` starts writing `/mradmid` · `factory_erase` stops erasing wholesale |

⚠ **TWO INSTRUMENT DEFECTS FOUND IN MY OWN NEW ROWS AND FIXED, both of which would have made a control worthless:**
S33's resident-state detector first matched INDENTED locals (the three entry points' automatic services) and
reported five "resident" objects on a tree that has none — it now matches only `static` and column-0 declarations;
and `W-S3-INVERT-HELP` first named O1 among its expected refusals, but O1 is unmoved (the file is still an approved
namer) — the honest answer is **O4f alone**, the per-file MULTISET, which is the class that rule belongs to.

### 5.3 `tools/gen_command_inventory.py` — the surface, and [[B319]]'s fifth axis

**177 → 188 rows, and every one of the 11 was predicted before the edit:**

```
| `acl`      | —          | `admin_router_arm` | serial | `MR_FEAT_RADMIN_ACCEPT` | src/firmware_commands.cpp:291 |
| `admin-id` | —          | `admin_router_arm` | serial | `MR_FEAT_RADMIN_ACCEPT` | src/firmware_commands.cpp:290 |
| `acl`      | `add`      | `acl_verb`         | serial | —                       | src/firmware_admin_verbs.h:328 |
| `acl`      | `list`     | `acl_verb`         | serial | —                       | …:312 |
| `acl`      | `remove`   | `acl_verb`         | serial | —                       | …:379 |
| `acl`      | `reset`    | `acl_verb`         | serial | —                       | …:393 |
| `acl`      | `set`      | `acl_verb`         | serial | —                       | …:357 |
| `admin-id` | `generate` | `admin_id_verb`    | serial | —                       | …:261 |
| `admin-id` | `reset`    | `admin_id_verb`    | serial | —                       | …:273 |
| `admin-id` | `rotate`   | `admin_id_verb`    | serial | —                       | …:267 |
| `admin-id` | `show`     | `admin_id_verb`    | serial | —                       | …:255 |
```

★★ **WHY `admin_router_arm` EXISTS AS A FUNCTION AT ALL, and it is a MEASUREMENT reason rather than a stylistic
one:** the generator records `transports` PER SURFACE, and `dispatch`'s surface is `serial,ble`. Two arms written
inline there would have published `acl` and `admin-id` as **BLE-reachable** in the authority table — the exact
opposite of R-RA-29. So the arms live in their own surface, recorded **`serial`-only**, reached from `dispatch`
and PROVEN so by `reached_from`. That is `help_command`'s shape (slice 0a) and its reason, one family over.

The two literals sit INSIDE the ACCEPT gate, so the rows carry the product gate and the help projection drops them
on the two mobile profiles.

**Two exclusions, each with a written reason** (the `remote_verb_open` idiom): `admin_verb_owns` (the BLE guard's
predicate re-asks the same two family tokens `admin_router_arm` already owns) and `acl_parse_role` (role ARGUMENT
values — publishing them would invent the grammars `acl operator` and `acl owner`).

**[[B319]] — `PROFILES` gains an explicit integer `MR_FEAT_RADMIN_ACCEPT` column** with the six ruled literal
values (`full_oled`/`full_headless`/`gateway`/`gateway_oled` = 1, `mobile`/`mobile_oled` = 0), and
`tools/test_gen_command_inventory.py` gains a `TestRadminAcceptAxis` class of **8** tests:

* every profile declares the axis with its ruled literal value, and the axis SEPARATES the profiles;
* a profile missing the axis **still REFUSES** — `eval_gate`'s unknown-axis refusal is PRESERVED, not weakened;
* an unrelated unknown macro still refuses;
* ★ a **SYNTHETIC evaluator fixture** varies `MR_FEAT_REMOTE_MGMT` with ACCEPT held fixed, and vice versa — ⛔
  neither combination is a newly legal BOARD profile (`mr_features.h`'s `#error` makes the two agree until Slice
  10), the point is that the generator reads two INDEPENDENT columns;
* the axis is NOT derivable from `MR_FEAT_MOBILE` either — the two FULL static profiles set MOBILE=1 AND ACCEPT=1;
* the generator's own source carries **no derivation** of the column (a literal typed cell, [[B319]]'s whole point);
* the REAL recorded ACCEPT-gated rows are exactly `{acl, admin-id}`, all `transports == "serial"`, and they project
  onto exactly the four ACCEPT profiles and onto NEITHER mobile one — asserted BY NAME, not by a count.

`tools/test_gen_command_inventory.py` — **55 tests, all pass.**

### 5.4 `tools/probe_features` — the seven-file ownership census

**PIN_CHECKS 114 → 118 (+4), fully attributed:** O4 is one check PER APPROVED FILE and the census grew from three
files to seven ⇒ O4a-c became O4a-g. ⛔ Nothing else moved: S1..S2 still 2, E1..E13 still 13, the 9 cells still 81.

**PIN_CONTROLS 38 → 44 (+6)**, one per NEW owner boundary: the router arm's gate DELETED · the BLE refusal's gate
DELETED · the boot call's gate legacy-WIDENED with `|| MR_FEAT_REMOTE_MGMT` (R-RA-27) · the help names INVERTED
onto the CLIENT capability · a DUPLICATE guard in the boot-wrapper header · a PURE SERVICE HEADER acquiring a
capability macro.

⚠ **One instrument defect fixed:** `run_checks` mapped the O4 suffix from a hand-kept `{HDR:"a", NODE_H:"b",
RX:"c"}` literal — a shape whose failure mode is a `KeyError`, i.e. an INSTRUMENT CRASH, the moment a reviewed
file is added. The suffix is now derived from the file's position in `APPROVED_FILES`.

---

## 6. Mutation — BOTH selectors reported independently, and their FULL UNION run

### 6.1 The two selectors

**Changed-source selector** — every source this slice edited that HAS a per-file battery:

| target | file | why |
| --- | --- | --- |
| `devicenv` | `src/device_nv.h` | the two records, their slots, classifiers, inits and typed wrappers — run in FULL, not only the new entries |
| `radmin3id` | `src/firmware_admin_identity.h` | **NEW** target |
| `radmin3acl` | `src/firmware_admin_acl.h` | **NEW** target |
| `radmin3verbs` | `src/firmware_admin_verbs.h` | **NEW** target |

⛔ The changed COMMAND/GLUE files (`firmware_commands.{cpp,h}`, `fw_main.cpp`, `firmware_help.h`) have **no native
per-file battery**, because neither the native suite nor the simulator compiles them. Their cover is the EXECUTED
router/guard/structural controls of §5.1 and §5.2, reported **separately** above and ⛔ never claimed as native REDs.

**Historical / dependency selector:**

| target | file | why it qualifies |
| --- | --- | --- |
| `teamkeyring` | `src/firmware_team_keyring.h` | its `SecretWipeGuard` is a LIVE acceptance dependency — `firmware_admin_identity.h` includes this header and guards every secret-bearing transient with it. Its source is UNCHANGED; the dependency is what qualifies it. |
| `cfgparse` | `src/firmware_config_parse.h` | `parse_hex32`, `parse_index_strict` and `parse_confirm_token` are all REUSED by the new grammar. Source UNCHANGED. |
| `sliceDtoken` | `src/firmware_config_parse.h` | the same file's confirmation-token acceptance boundary, the second battery that pins it. |

**Explicitly EXCLUDED, with the reason:** `radmin2codec`. `firmware_admin_acl.h` includes `lib/core/remote_codec.h`
for exactly ONE constant (`kRemoteSlotSessionMax`, the ten-slot binding). ⛔ No codec FUNCTION is reached, no
remote event is emitted, and a slot-count constant is not a runtime dependency on the codec's behaviour.

### 6.2 The full union — all seven batteries, `--workers=2`, from an isolated scratch root

`MR_MUT_SCRATCH=/tmp/claude-1001/-home-staszek-MeshRoute/s3/mut`, free disk measured beforehand (**69 GB**;
[[B286]]/[[B316]] respected — two workers, an isolated copy, the owner's build trees never deleted).

| target | mutations RED | unusable | exit |
| --- | --- | --- | --- |
| `radmin3id` | **23** | **0** | 0 |
| `radmin3acl` | **36** | **0** | 0 |
| `radmin3verbs` | **28** | **0** | 0 |
| `devicenv` (FULL) | **35** | **0** | 0 |
| `teamkeyring` (FULL) | **68** | **0** | 0 |
| `cfgparse` (FULL) | **8** | **0** | 0 |
| `sliceDtoken` (FULL) | **2** | **0** | 0 |
| **UNION** | **200** | **0** | **all 0** |

Every battery reported, verbatim:

```
real tree untouched: all 45 target files byte-identical to launch (md5); no build ran in /home/staszek/mr-slice3
source restored: md5 … (MATCHES)          [per worker, per battery]
```

Each mutation must match its find text **exactly once** (a 0- or 2-match entry is VACUOUS, never a RED) and each
must make the native suite go RED against the mutant.

### 6.3 What the new batteries attack — the ruled decisions, one at a time

`radmin3id` (23): the dead-RNG refusal · the provider's answer · the mint ORDER (draw → validate → save → only
then derive) · a failed save reported as success · the all-zero predicate's early exit · `reserved` leaving the
content policy · the four-state composition (each arm) · `generate` over an existing / invalid / **unreadable**
record · `rotate` on an absent node · **recovery over `io_failed`** · recovery over an `ok` record · a writing
`show` · ★ the fingerprint's **digest size**, **prefix offset**, **prefix length**, **input length**, hex case and
nibble order · ★ publishing the SEED in place of the public key.

`radmin3acl` (36): count-equals-population · clamping instead of refusing · **an ownerless non-empty ACL** ·
duplicate keys · illegal role bytes · zero-keyed occupied rows · a "deleted" row that kept its key · `reserved` ·
a census that trusts the stored count · **the first-owner rule** · duplicate add succeeding / answering
"unchanged" · zero keys · **a full ACL evicting** · **the lowest-free-slot rule** · the identity gate · adding over
an invalid record · **the last owner demoted** · **the last owner removed** · **the self-slot rule** (remove, set,
and its over-widening onto a legal no-op) · the no-op arm claiming a write · empty/out-of-range slots ·
**compaction** · a removed row keeping its key · recovery over `io_failed` / over `ok` / **inventing an owner** ·
a failed save · **the byte-comparison guard INVERTED** · `read()` leaving partial bytes · readiness without an
owner · the role domain opening · the role NAMER collapsing.

`radmin3verbs` (28): the primary token becoming a PREFIX · losing either family half · tab handling · ★ **a token
scan that ignores `len`** · **key truncation to the field width** · an undersized key scratch · **all four confirm
gates** · a trimmed confirmation · ★ **the strict slot parse reverting to `atol`** · the 0..9 domain · the role
token becoming a prefix / defaulting · three trailing-token refusals · an unknown subcommand falling through ·
`unchanged` reported as `updated` · empty rows listed · the end line from the stored count · the ADD line printing
the fingerprint twice · the BOOT line losing `state=` · a boot line printing a key · two lexeme mappings · the
verb bypassing the identity read.

`devicenv` +14 new (35 total): both records' EQUALITY version policy (attacked with version **0**, the only value
that separates equality from a range) · both classifiers' branch ORDER · both over-length checks · both `*_init`
stampings · the ACL slot naming the admin record's file · **`/mradmid` leaving the factory-reset namespace**.

### 6.4 Three coverage gaps the union FOUND in my own work, and what each cost

⛔ **Reported because a silent deletion would be the worse fault.** The first union pass left **15** green or
vacuous mutants. None was scored as a RED; each was diagnosed:

1. **Six ACL mutants stayed green — a real TEST gap.** Two content-policy subcases were passing for the WRONG
   REASON: they corrupted a row while leaving `count` describing the OLD population, so an implementation that
   merely SKIPPED the bad row was still caught by the count rule rather than by the rule under test. Both now
   carry a DISCRIMINATING shape in which `count` agrees with the skip. **+2 assertions.**
2. **Four lines are REDUNDANT BY CONSTRUCTION.** `cand.rec[slot] = AclRow{}` (the chosen slot is one
   `acl_content_valid` already proved all-zero), `commit_`'s `acl_content_valid` call (every candidate this service
   composes is valid by the checks that precede it), the no-op early return and `commit_`'s `memcmp` (each is the
   other's belt), and `admin_emit`'s `>= kAdminLineMax` arm (two `static_assert`s prove `snprintf` cannot
   truncate). Mutants of them stay GREEN = **UNUSABLE, never RED** ⇒ the entries were REMOVED and **each line is
   annotated in source** saying it is a belt for a FUTURE caller. Two of the four were re-targeted to decisions
   that ARE observable (the no-op arm CLAIMING a write; the byte-compare guard INVERTED) — both now RED.
3. **Four `device_nv.h` properties are UNREACHABLE from a native battery** — the host has NO NV backend, so a
   re-pointed slot and a dropped `SlotIo` change nothing native can see. Removed from the battery, reason recorded
   in it, and the cover **moved to instruments that can execute it**: `probe_inbox_verbs` C27/C28/C29 against the
   REAL ESP32 sequence, plus structural S35. Raised as proposed register row **B324**.

⇒ the reported union is **200 RED / 0 unusable**, and the three findings are in §10 rather than buried.

---

## 7. The simulator: a CHECKED no-op, with a working recompile control

```
lus md5 BEFORE the post-edit build : b1b1d92c
cmake --build /home/staszek/mr-slice3-lus
  [  2%] Built target meshroute_monocypher
  [ 35%] Built target meshroute_core_normal
  [ 67%] Built target meshroute_core_gw
  [ 72%] Built target meshroute_console
  [ 85%] Built target lus_core
  [100%] Built target lus
relevant build actions (Building CXX | Linking CXX) : 0
lus md5 AFTER : b1b1d92c            <-- IDENTICAL
```

★★ **AND "0 ACTIONS" IS A MEASUREMENT RATHER THAN A BROKEN BUILD SYSTEM, because the control fired:**

```
touch lib/core/node.cpp
cmake --build … -> 5 build actions (…Linking CXX static library libmeshroute_core_gw.a … Linking CXX executable lus)
lus md5 after the control : b1b1d92c   <-- identical, because a touch changes no bytes
lib/core/node.cpp md5 before : 2374d24fd8189609e661e99896682758
lib/core/node.cpp md5 after  : 2374d24fd8189609e661e99896682758   RESTORED, byte-identical
git diff --stat lib/ : EMPTY
```

⛔ This is a **CHECKED NO-OP BUILD**, ⛔ not a forced recompilation, and ⛔ no simulator or `lib/core` source was
edited or re-anchored. The inertness is ALSO structural: this slice touched **0 files under `lib/core` or
`lib/console`**, and the simulator's `CMakeLists.txt` names no `src/` path.

### The 36/36 corpus, run anyway

```
PASS: 36/36 streams produced and validated, 0 failures
  anchors: 36/36 rows reproduce simulation/BASELINE.md
  s18_meshroute   32afbf11   events=269517   (0 assertion failures)
```

⛔ `simulation/BASELINE.md` was **NOT edited**, **NOT re-anchored**, and no anchor value was hardcoded anywhere —
`run_corpus.py` parses the `### 36/36 corpus` table itself and refuses a drifted set.

---

## 8. Boards — every byte of movement attributed

`measure_board.py pair --output .pio-measure/final-pair --jobs=2` (**gateway + heltec_mobile only**, under the
repository's own `.pio-measure/`, a NEW empty directory per [[B315]]).

| measurement | gateway base → final | heltec_mobile base → final |
| --- | --- | --- |
| **RAM** | 195844 → **195844** (**+0**) | 205684 → **205684** (**+0**) |
| **live flash** | 512092 → **530972** (**+18880**) | 1355292 → **1355292** (**+0**) |
| objects | 284 → 284 (+0) | 328 → 328 (+0) |
| symbols | 6160 → 6267 (+107) | 13084 → 13084 (**+0**) |
| `symbols_sha256` | moved | **IDENTICAL** |
| loadable sections | `.text` +18880; **every other section +0** | **every section +0** |

★ **gateway RAM is +0 exactly as design §6.2 was implemented to require** — no resident identity, no ACL cache, no
service member, no static I/O buffer. The +18880 B of flash is entirely `.text`: the two pure services, the verb
grammar and emitters, R-RA-29's BLAKE2b fingerprint path, and the bindings.

★★ **heltec_mobile's LIVE image is BYTE-IDENTICAL.** `firmware.bin` is 1355936 B on both arms and **exactly 64
bytes differ**, in two runs, both of them hash fields:

| offset | 32 bytes | what it is |
| --- | --- | --- |
| `0x0000b0` | `esp_app_desc_t::app_elf_sha256` | the ELF identity the descriptor carries (0x20 + magic 4 + secure_version 4 + reserv1 8 + version 32 + project_name 32 + time 16 + date 16 + idf_ver 32 = 0xb0) |
| `0x14b080` | the trailing image SHA-256 | esptool's whole-image checksum, which NECESSARILY moves with the field above |

⇒ **1355872 of 1355936 bytes (99.9953 %) are byte-identical**, and the cause of the ELF hash move is measured, not
guessed: `.debug_*` grew **19158452 → 19159502 B (+1050)** because DWARF now names the three new headers and the
shifted lines. ⛔ No code or data byte moved on the CLIENT board.

**Symbol presence / absence, on the linked images:**

| symbol | gateway | heltec_mobile |
| --- | --- | --- |
| `mrfw::admin_stores_boot_report_console()` | **PRESENT** | **ABSENT** |
| `handle_admin_id` / `handle_acl` / `admin_router_arm` | file-local (`static`, inlined — no external symbol) | **ABSENT** |

⇒ the family is verifiably absent from a CLIENT product, not merely inert in it.

### ABI

* `probe_board_abi.py` — **PASS (191 checks, 9/9 controls RED, 0 unusable)**
* `probe_b278_row_abi.py` — **PASS (42 measurements, 6/6 controls RED)**

⛔ **No ABI re-pin was made.** The new records' `sizeof`/`alignof`/`offsetof` are UNGUARDED `static_assert`s in
`device_nv.h` (the `TeamKeyRecord` idiom), so both board toolchains compile them; they are deliberately NOT added
as duplicated `PINNED` entries — the probe's own note says why.

### Stack demand — MEASURED on the real ARM ABI, not estimated

`arm-none-eabi-g++ -Os -mcpu=cortex-m4 -mthumb -fstack-usage`, the services instantiated exactly as the gateway's
three entry points instantiate them:

| frame | bytes |
| --- | --- |
| `mrfw::acl_verb(…)` | **944** |
| `mrfw::AclService::add(…)` | **792** |
| `mrfw::AclService::recover()` | 752 |
| `mrfw::AclService::boot_report()` | 384 |
| `mrfw::admin_id_emit_ok(…)` | 280 |
| `mrfw::AdminIdService::pub_of_(…)` (the 196-B `Identity`) | 216 |
| `mrfw::admin_id_verb(…)` | 168 |
| `mrfw::AdminIdService::mint_()` | 104 |
| `mrfw::acl_content_valid(…)` | 56 |
| `mrfw::AclService::commit_(…)` | 32 |

**Worst composed chain: `acl_verb` → `add` → `commit_` → `acl_content_valid` = 944 + 792 + 32 + 56 = 1824 B**, plus
the ~48 B device-binding frame ⇒ **≈ 1872 B**.

⚠⚠ **CORRECTED ON QA's GATE (2026-09-06) — THE OLD CLAIM IS KEPT VISIBLE BECAUSE IT OVERSTATED THE RISK.** This
paragraph read *"on the nRF52 loop task's fixed 4 KB stack"*. **That is the wrong task**, and I verified the
correction at the source rather than accepting it:
* `service_console()` is called at `src/fw_main.cpp:1743`, INSIDE `mesh_service_once()`;
* `mesh_service_once()` is the body of `mesh_task_fn`, and `loop()` creates it as
  `xTaskCreate(mesh_task_fn, "mesh", 8192 / sizeof(StackType_t), …)` — an **8 KB** task (`:1801`), with the
  comment at `:1790` saying so outright (*"Run the mesh in a DEDICATED 8 KB FreeRTOS task; the 4 KB Arduino loop
  task then just idles"*). On ESP32 `loop()` calls `mesh_service_once()` on a loopTask with a comparable ~8 KB.
* the ONLY part of this slice that runs on the 4 KB Arduino loop task is `setup()`'s boot report at `:865`,
  whose deepest frame is `AclService::boot_report()` at **384 B**.
⇒ the ≈1872 B is **≈1872 B of the 8 KB MESH task**, not of the 4 KB loop task. QA additionally reports that the
mesh task's deepest RX nesting is ~1.4 KB and does not nest with the console path — ⛔ that figure is QA's, not
mine; I did not measure the RX chain. The dominant term is the
design's own: `add` holds `b` and `cand`, two 368-byte `AclBlob`s (736 B), because the ruled transaction is
*compose ONE candidate → byte compare → at most ONE save*. ⛔ It is AUTOMATIC scratch: no static buffer and no
cache was added (design §6.2 / Author decision §4.1 forbid both).

⚠ **This is a real cost and is raised as a finding rather than buried** — see §10, proposed row **B323**.

---

## 9. STOP audit — all eight evaluated explicitly

| STOP | verdict |
| --- | --- |
| **1** base / dirty start / wrong simulator / concurrent input change | **NOT FIRED as a base defect.** Base `7299eb9` verified by `rev-parse`; `git status --short` EMPTY at start; simulator `8688884` untouched. ⚠ I DID cause one concurrent-input change myself, by starting a background probe batch and then editing — `probe_features` caught it, I restored the pristine file with a git READ, re-verified an empty status and **re-ran the whole batch**. Reported in §0.1 rather than repaired silently. |
| **2** new owner policy · changed fingerprint/role/capacity · BLE widening · remote/codec/session/Node consumer · legacy-flow change · wire or main-NV version change · edit outside the fence | **NOT FIRED.** Fingerprint = R-RA-29 verbatim; roles = the ruled three; capacity = ten, bound to `kRemoteSlotSessionMax`; the BLE set gains ONE family and the standing reboot/regen/ota/factory_reset note is untouched; no codec function is called; `Node` untouched (ABI probes PASS); `do_regen`/`handle_leave`/`admin_load`/`password`/`unlock`/`lock` bodies unchanged (structural S37/S38); no `wire_version`, no `mrnv::kVersion`. Every edited path is in the brief's §5 list. |
| **3** record ABI/layout differs · the ten-slot binding · clamping/compaction · an invalid or ANY io_failed state permitting a write · a failed save reporting success | **NOT FIRED.** Layouts pinned per ABI and by native literals; `static_assert(kAclSlots == kRemoteSlotSessionMax + 1)`; counts are REFUSED, never clamped (mutations B01/B02 RED); holes never compacted (B26 RED); `io_failed` permits NO write including recovery (A14/B29 RED, and rows R42e/R42f execute it); a failed save publishes no success (A04/B33 RED). |
| **4** entropy converted to unconditional success · a zero/fallback seed accepted · secrets escaping · synthetic draws claimed as hardware proof | **NOT FIRED.** The binding returns `!admin_buf_all_zero(out, 32)` — the ACTUAL check; the all-zero draw refuses in BOTH the binding and the service; no clock/counter fallback exists; ⛔ no seed byte reaches any sink (native + R32f); and this document says in three places that a fake draw stream is **NOT** a hardware or RF entropy proof. **[[B312]] STAYS OPEN.** |
| **5** the real router/store/guard bypassing the tested service · output ignoring its sink · an ambiguous or unreachable extraction · a surface without ownership/transport proof · a pin repointed without executed-row accounting | **NOT FIRED.** The router path is EXECUTED (`probe_inbox_verbs`, 137 checks) and its bypasses are controlled (C22..C29); the sink rule is executed (R41) and controlled (C25); the BLE extraction refuses on nine distinct ambiguities; the new surface carries `reached_from` proof and a `serial` transport claim; every moved pin above is accompanied by its derivation. |
| **6** corpus/anchor/live-mobile-flash/Node/RAM movement contradicting prediction · unattributed board or stack cost · a claimed unchanged simulator build with no working recompile control | **NOT FIRED.** 36/36 anchors reproduce; mobile live flash +0 and its 64 metadata bytes are attributed to the byte; gateway RAM +0; Node ABI unmoved; the recompile control fired (5 actions). The stack figure is measured and raised as a finding. |
| **7** green/vacuous/multi-matched/unusable/misclassified control · lost worker · failed restoration · failed standing gate · warning · unreproduced reference | **NOT FIRED in the reported run.** ⚠ It DID fire during construction — 6 ACL, 4 verbs and 5 devicenv mutants stayed green, one was VACUOUS, and three inbox controls failed to compile. Every one was diagnosed and corrected (§6); ⛔ NONE was scored as a RED. The reference reproduces and its corruption control fires. |
| **8** power-cut/factory-repair claimed atomic or identity-preserving without proof · a future controller/session operation claimed implemented · required evidence or metal residue omitted | **NOT FIRED.** The headers state at three sites that a failed save does NOT promise the previous bytes survived, that [[B317]]'s self-heal can erase both records, and that there is no two-store atomic commit. No controller verb, no session, no remote execution is claimed. Bench **Part 55a** is named as pending owner execution. |

---

## 10. Findings — PROPOSED register rows (⛔ the register itself was NOT edited)

### B323 (proposed) — the ACL console path costs ≈ 1.9 KB of the nRF52 MESH task's 8 KB stack

⚠ **RELABELLED B320 → B323 on QA's gate (2026-09-06):** B320 and B321 are already taken by the Author's
Slice 4 preparation. These three proposals are B323, B324 and B325; ⛔ the old numbers are kept visible here
so a reader of an earlier draft can follow the renumbering.

**Measurement** (`arm-none-eabi-g++ -Os -mcpu=cortex-m4 -mthumb -fstack-usage`, the services instantiated exactly
as `handle_acl` instantiates them): `acl_verb` **944 B** → `AclService::add` **792 B** → `commit_` 32 →
`acl_content_valid` 56 = **1824 B**, plus ~48 B of device-binding frame ⇒ **≈ 1872 B**.

**Cause, exactly:** `AclService::add` holds `b` and `cand`, two 368-byte `AclBlob`s = **736 B of the 792**, because
the ruled transaction is *load → compose ONE candidate → byte compare → at most ONE save* (design §6.5/§6.6). The
verb frame's 944 B is the listing's own `AclBlob` (368) plus the key scratch (66), the two hex renderings (17 + 65)
and the line buffer (160), which a compiler is free not to overlap across disjoint scopes.

**Why it is a finding and not a STOP:** it is AUTOMATIC scratch — ⛔ no static buffer and ⛔ no cache was added,
which is what design §6.2 and the Author's §4.1 require — and on the corrected task it leaves **~6.1 KB of the
8 KB mesh task free**, on a path that is not the `do_post_ack` chain the 2026-06-25 hardfault came from. It is
still the largest single console frame this codebase has added, and the owner should see the number rather than
discover it — but it is a COST, not a hazard, and my first statement of it was alarmist by a factor of two.

**If it must come down** (a later slice, ⛔ not this one — C1): comparing only the CHANGED ROW plus `count` instead
of the whole record would remove one 368-byte copy; that is a change to the ruled transaction shape and needs an
owner ruling, not a coder's optimisation.

### B324 (proposed, was B321) — four `device_nv.h` properties are UNREACHABLE from `--target=devicenv`

**Measurement:** the mutants *"`load_admin_id` reads the wrong slot"*, *"`save_acl` writes the wrong slot"* and
*"the wrapper stops asking the primitive for `SlotIo`"* all stay **GREEN** under the native suite. **Cause:** the
host arm has **no NV backend at all** — `read_slot` returns `kSlotAbsent` unconditionally and never touches
`SlotIo` (`device_nv.h`'s `#else` arm) — so a re-pointed slot and a dropped `&io` change nothing a native test can
observe. This is a **structural blind spot of `--target=devicenv` for EVERY record**, not a property of the two new
ones: the same holds for `/mrcfg`, `/mrid`, `/mrpeers`, `/mrjoin`, `/mrteams` and `/mrui`.

**What this slice did about it:** removed the four green entries (a green mutant is UNUSABLE, never RED), recorded
the reason in the battery, and moved the cover to instruments that CAN execute it — `probe_inbox_verbs` controls
**C27/C28/C29** against the REAL ESP32 read/write sequence over a byte-counted medium keyed by namespace + key,
plus structural row **S35**. **Residue:** the six OLDER records have no equivalent executed cover.

### B325 (proposed, was B322) — `probe_features/ownership.py`'s O4 check id came from a hand-kept three-entry map

**Measurement:** extending `APPROVED_SITES` from three files to seven raised `KeyError:
'src/firmware_commands.cpp'` inside `run_checks` — an INSTRUMENT CRASH, not a failed check. **Fixed here** (the
suffix is derived from the file's position in `APPROVED_FILES`), and recorded because the failure mode of a
hand-kept map in a gate is that the gate stops running rather than reporting.

### B326 (proposed) — a reporting harness read `$?` after a pipeline and reported a FALSE GREEN

**Measurement:** the Slice 3 final gate batch recorded the tools unit sweep as `exit 0` while it was actually
`Ran 320 tests … FAILED (failures=2)`. **Cause:** `cmd | tail -5` followed by `echo $?` reads `tail`'s status, not
the command's — and the `tail` window was narrower than unittest's trailing `ResourceWarning` noise, so the
`FAILED` line was invisible too. Both halves of the signal were destroyed by the harness. ⓘ The same script used
`${PIPESTATUS[0]}` correctly for the six probes and the warning census, which is what makes this an inconsistency
rather than an unknown idiom.

**Why it is worth a row even though the harness is ad-hoc and not in the repo:** this is the `[[B237]]` /
*"an instrument that cannot fail is not a gate"* class, and the ad-hoc final-gate batch is a shape every dispatched
coder rebuilds from scratch. **The durable rule:** a gate batch reports `${PIPESTATUS[0]}`, and it either keeps the
FULL output or greps for the tool's own verdict token — ⛔ never a fixed `tail -N` window over a summary whose
position it does not control. Proposed for the register so the next coder inherits the rule rather than the trap.

### Register rows explicitly LEFT OPEN

* **[[B312]]** — the checked entropy seam is a SEAM. `mrrng::fill` is still `void`, a synthetic all-zero stream is
  ⛔ not a hardware or RF entropy proof, and the binding's `true` means only *"32 bytes arrived and are not all
  zero"*. Nothing here closes it.
* **[[B313]]**, **[[B315]]**, **[[B317]]** — untouched, and [[B317]] is stated at three source sites: a whole-FS
  self-heal triggered by one of the six OTHER probed files erases both new records too.
* **[[B286]] / [[B311]] / [[B316]]** — not repaired (the brief forbids it). The mutation runs used
  `--workers=2` and an isolated scratch root, and free disk was measured before each.

---

## 11. Every modified and untracked path, in BOTH repositories


### `/home/staszek/mr-slice3` (the measured worktree) — the COMPLETE inventory

**Modified (23 files):**

| file | +/- |
| --- | --- |
| `docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md` | +92 / -78 (regenerated by `--write`) |
| `src/device_nv.h` | +226 / -0 |
| `src/firmware_commands.cpp` | +121 / -1 |
| `src/firmware_commands.h` | +16 / -0 |
| `src/firmware_help.h` | +8 / -1 |
| `src/fw_main.cpp` | +38 / -1 — incl. the QA-gate comment-only trailer move (line-count-neutral, code-identity proven) |
| `test/test_device_nv.cpp` | +68 / -0 |
| `tools/gen_command_inventory.py` | +59 / -9 |
| `tools/probe_console_sink/ble_guard.py` | +211 / -23 — incl. the QA-gate docstring correction |
| `tools/probe_console_sink/negctl.py` | +171 / -10 |
| `tools/probe_console_sink/ownership.py` | +13 / -4 |
| `tools/probe_console_sink/run.sh` | +49 / -13 — incl. the QA-gate section-header correction |
| `tools/probe_console_sink/structural.py` | +115 / -5 |
| `tools/probe_features/ownership.py` | +69 / -1 |
| `tools/probe_features/run.sh` | +15 / -7 |
| `tools/probe_inbox_verbs/fakes/esp_random.h` | +9 / -0 |
| `tools/probe_inbox_verbs/fakes/nvs.h` | +12 / -1 |
| `tools/probe_inbox_verbs/probe_main.cpp` | +274 / -0 |
| `tools/probe_inbox_verbs/run.sh` | +97 / -2 |
| `tools/probe_ui_model_mutations.py` | +426 / -1 |
| `tools/test_gen_command_inventory.py` | +98 / -3 |
| `tools/test_probe_console_sink.py` | +58 / -15 — incl. the QA-gate BLE two-family reader fix |
| `tools/test_probe_features.py` | +8 / -1 — QA-gate: ownership controls 19 -> 25, re-derived |

**Untracked (7 files) — the complete `git status --short | grep '^??'` output:**

```
?? docs/superpowers/evidence/2026-09-06-radmin-slice3.md      (this file, 306 lines of it the brief's §8)
?? src/firmware_admin_acl.h                                   410 lines
?? src/firmware_admin_identity.h                              306 lines
?? src/firmware_admin_verbs.h                                 416 lines
?? test/test_firmware_admin_acl.cpp                           650 lines
?? test/test_firmware_admin_identity.cpp                      572 lines
?? test/test_firmware_admin_verbs.cpp                         624 lines
```

⛔ **Nothing is committed and nothing was offered to be committed (D4).** The work is green and UNCOMMITTED;
the owner commits and bench-verifies.

⛔ **`.pio-measure/base-pair` and `.pio-measure/final-pair` are build artefacts under a `.gitignore`d path** and
are left in place for QA, as are the worktree, the paired build `/home/staszek/mr-slice3-lus` and the reference
env `/home/staszek/mr-slice3-ref`.

### `/home/staszek/lora-universal-simulator` @ `8688884`

```
git status --short : (empty)
git diff --check   : (empty)
```
⛔ **NOT ONE BYTE was edited.** The paired build tree `/home/staszek/mr-slice3-lus` is outside the repository.

### `/home/staszek/MeshRoute` (the shared checkout) @ `7299eb9`

⛔ **NOT edited by me.** Its working tree still carries exactly the Author's own uncommitted preparation
(`MEMORY.md`, the register, the bench script, the pre-check, the design, `tracker.md`, and the untracked brief).
No `commit`, `add`, `stash`, `checkout --`, `reset` or `revert` was run in it. The `git worktree add` is the ONE
git write this slice made anywhere.

---

## 12. The FINAL gate — every step re-run at the final tree state

| gate | result |
| --- | --- |
| `pio test -e native` → `./.pio/build/native/program` | **2702 cases / 116902 assertions / 0 failed** (base 2640 / 115288 / 0) |
| `probe_console_sink/run.sh` | **PASS** — `profiles=6 checks=720 structural=39 ble_guard=480 ownership=6 ownership_controls=3 controls=78 unusable_controls=0` |
| `probe_inbox_verbs/run.sh` | **PASS** — 137 checks (pin 137), 30 controls / 0 unusable (pin 30) |
| `probe_firmware_ui/run.sh` | **PASS** — 223 controls / 0 unusable (UNCHANGED) |
| `probe_custody_usb/run.sh` | **PASS** — 27 checks (pin 27), 10 controls (pin 10) (UNCHANGED) |
| `probe_ble_line/run.sh` | **PASS** — 40 checks (pin 40), 8 controls (pin 8) (UNCHANGED) |
| `probe_features/run.sh` | **PASS** — 9 cells (pin 9), 118 checks (pin 118), 44 controls (pin 44), ownership 25 controls / 0 unusable |
| `probe_features/run.sh --no-neg` | run and reported as **PROBE-ONLY — NOT A GATE** |
| `python3 -m unittest discover -s tools -p "test_*.py"` | ⚠ **FIRST DRAFT CLAIMED `exit 0` AND WAS WRONG (§0.2).** After the two wrapper fixes, the REAL result, taken from the tool's own verdict lines rather than a `tail` window: `Ran 320 tests in 353.135s` / `OK` (`sweep.out:139` and `:141`), exit code **0** read from the command itself. ⓘ The sweep's output contains ONE line reading `FAIL: §B278 census selftest, 9 failure(s)` — that is `test_b278_correlation_census.test_the_selftest_is_not_vacuous` DELIBERATELY sabotaging the binder into permissiveness and asserting the battery bites, then restoring it. It is a pre-existing control FIRING, not a failure, which is why unittest reports `OK`; it is named here so the next reader does not mistake it for one. |
| `gen_command_inventory.py` bare, then `--check` | **PASS both**, tracked table byte-identical, **188 command rows** |
| `warning_census.sh` | **PASS** — 6 envs at their pins, `-Wswitch` **0** on every one, **ZERO new warnings** |
| `check_a0_matrix.py` | **PASS** — 21 enum members, 6 named special rows |
| `check_data_type_literals.py` | **PASS** — 188 files (was 182: the six new files), no numeric DataType literal survives |
| `probe_board_abi.py` | **PASS** — 191 checks, 9/9 controls RED, 0 unusable |
| `probe_b278_row_abi.py` | **PASS** — 42 measurements, 6/6 controls RED |
| `run_corpus.py --require-anchors` | **PASS: 36/36 · anchors 36/36 reproduce `simulation/BASELINE.md`** |
| paired `lus` | `b1b1d92c` → **0 build actions** → `b1b1d92c`; recompile control **5 actions**, source restored by hash |
| mutation UNION (7 batteries) | **200 RED / 0 unusable**, every battery exit 0, tree byte-identical to launch |
| `git diff --check` (worktree) | clean (exit 0) |
| `git diff --check` (simulator) | clean (exit 0), and `git status --short` **EMPTY** |

### ★ A SIX-ENV CONFIRMATION OF THE PRODUCT GATE, from the census's own numbers

The warning census builds six envs of its own — a wider set than the ruled pair — and its RAM/flash columns give an
independent reading of the ACCEPT gate:

| env | `MR_FEAT_RADMIN_ACCEPT` | ΔRAM | Δflash |
| --- | --- | --- | --- |
| `gateway_heltec` | 1 | **+0** | +7096 |
| `gateway_heltec_v4` | 1 | **+0** | +7176 |
| `heltec_v3` | 1 | **+0** | +6976 |
| `heltec_v4` | 1 | **+0** | +6932 |
| `heltec_mobile` | **0** | **+0** | **+0** |
| `heltec_v4_mobile` | **0** | **+0** | **+0** |

⇒ **RAM is +0 on all six**, and flash grows on **exactly** the four ACCEPT envs and on **neither** CLIENT env.
The two mobile builds are flash-BYTE-IDENTICAL. That is the product gate measured six ways, ⛔ not asserted.
(The census's flash figures include its own build stamp and so differ in absolute value from the ruled pair's
fixed-identity build; the DELTAS are what this table reads.)

---

## 13. Executed transcripts — the exact bytes, from the REAL router

Captured by `tools/probe_inbox_verbs` driving `mrfw::dispatch()` over the real `src/firmware_commands.cpp`:

```
> admin-id err absent
> acl end count=0 owners=0 operators=0
> admin-id generated fp=<16hex> pub=<64hex>
> admin-id ok fp=<16hex> pub=<64hex>
> admin-id err already_present
> admin-id err bad_args
> acl added slot=0 role=owner fp=aaaa226d59875f1c pub=1111111111111111111111111111111111111111111111111111111111111111
> acl added slot=1 role=owner fp=7542389f7290cc83 pub=2222222222222222222222222222222222222222222222222222222222222222
> acl updated slot=1 role=operator
> acl unchanged slot=1 role=operator
> acl err last_owner
> acl slot=0 role=owner fp=aaaa226d59875f1c pub=1111111111111111111111111111111111111111111111111111111111111111
> acl slot=1 role=operator fp=7542389f7290cc83 pub=2222222222222222222222222222222222222222222222222222222222222222
> acl end count=2 owners=1 operators=1
> acl removed slot=1
> acl err identity_absent
> admin-id err entropy_failed
> admin-id err nv_save_failed
> acl err store_invalid
> acl recovered count=0 owners=0 operators=0
> admin-id err store_io_failed
> acl err store_io_failed
```

**Boot report** (the read-only adapter, on the same platform facts):

```
> admin-id boot state=absent
> acl boot state=absent count=0 owners=0 operators=0
```

**The BLE refusal**, extracted VERBATIM from `src/fw_main.cpp` and executed:

```
condition : mrfw::admin_verb_owns(line, len)
envelope  : write_err(out, cap, "admin", "console_only")   ->   {"err":"admin","msg":"console_only"}
gate      : #if MR_FEAT_RADMIN_ACCEPT   (asserted EXACTLY, by the extractor)
position  : inside ble_dispatch_line, BEFORE exec_console_line   (asserted by the extractor)
```

**Bare help**, ACCEPT profiles, bytewise ascending at the head of the index:

```
acl
admin-id
cfg
…
```

---

## 14. `PIN re-synced?`

`PIN re-synced? YES — tools/probe_ui_model_mutations.py PIN_CASES, PIN_ASSERTS 2640, 115288 -> 2702, 116902.
The +62 cases are DERIVED PER FILE and their sum is proven: test/test_firmware_admin_identity.cpp 20 +
test/test_firmware_admin_acl.cpp 23 + test/test_firmware_admin_verbs.cpp 19 = 62 (grep -c TEST_CASE on the three
files); test/test_device_nv.cpp was EXTENDED but gained ZERO new TEST_CASEs, which is why the case delta is exactly
the three new files' and nothing else. The +1614 assertions include the last +5 from the three DISCRIMINATING cases
the mutation union forced (the /mracl version-0 EQUALITY case and the two content-policy shapes whose count agrees
with a skip). Every pre-existing case is preserved: the base tree was measured at 2640/115288/0 before the first
edit and the delta is wholly additive.`

---

## 15. What this slice did NOT do

⛔ No remote execution · no session, epoch or invalidation · no codec CALL (only `remote_codec.h`'s slot constant,
by `static_assert`) · no `CommandContext` and no remote caller authority · no `Node` member and no core edit · no
simulator edit · no `wire_version` and no `mrnv::kVersion` bump · no new firmware `.cpp` · no `platformio.ini` or
source-list edit · no `lib/core`, `lib/console` or HAL change · no legacy-flow change (`do_regen`, `handle_leave`,
`admin_load`, `password`/`unlock`/`lock`, `remote_exec` all untouched, and structurally pinned so) · no
`simulation/BASELINE.md` edit and no re-anchor · no ABI re-pin · no [[B286]]/[[B311]]/[[B315]] repair · no register,
design, manual, bench or QA-ledger edit · no shared-helper refactor · **no `git commit`, and none offered** (D4).

⛔ **No metal PASS is inferred from host fakes.** Bench **Part 55a** remains pending owner execution: the real
power cut across `write_slot`'s remove-then-write, the `factory_reset confirm` erasure of both records, `regen`
and `leave` preservation across a reboot, a corrupt-record boot, and the BLE refusal envelope over real NUS.

⛔ **No two-node trust exchange is claimed.** This is the TARGET half only; the controller's `admin-key show self`,
the target book and the other node's USB output are Slice 4's (bench Part 55b).
