<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 5 — the target's authenticated session, admission and on-air bootstrap · implementation evidence · 2026-09-07

Brief: `docs/superpowers/plans/2026-09-06-radmin-slice5-target-session.md` (QA final PASS 2026-09-07).
Pre-check: `docs/superpowers/plans/2026-09-06-radmin-slice5-precheck.md` (S5-A1/A2/A3 = [[B331]]/[[B332]]/[[B333]]).
Rulings: R-RA-8 / R-RA-13 / R-RA-22 / R-RA-25 / R-RA-27 / R-RA-28 / **R-RA-31**.
⛔ The brief §5 names the evidence file `2026-09-06-radmin-slice5.md`; the dispatch names
`2026-09-07-radmin-slice5.md`. The dispatch's name is used. Stated rather than silently reconciled.

---

## 0. Bases, statuses and the concurrent-input audit

| repository | base at dispatch | verified | status at start |
| --- | --- | --- | --- |
| MeshRoute `/home/staszek/MeshRoute` | **`1677b4435aa16ed560a9e49bcddd02749b343c29`** | `git rev-parse HEAD` | `git status --short` **EMPTY** |
| simulator `/home/staszek/lora-universal-simulator` | **`868888419c7cc250d7019860d3403a7721ade1fc`** | `git rev-parse HEAD` | `git status --short` **EMPTY** |

Measured checkout: `/home/staszek/MeshRoute`. Simulator build cache `MESHROUTE_DIR` (read from
`build/CMakeCache.txt`): `/home/staszek/lora-universal-simulator/../MeshRoute` — i.e. the measured checkout.

### 0.1 ⚠⚠ CONCURRENT INPUT — AUDITED, NOT SILENTLY ABSORBED (STOP 1 shape)

Both trees were verified CLEAN at dispatch. **Three tracked Markdown files were modified in the MeshRoute tree
DURING this session by something other than this slice** (mtimes 06:48:47 and 06:49:15; this slice's first edit
was later, and none of the three is in the §5 fence):

| path | what it is | read by any gate? |
| --- | --- | --- |
| `MEMORY.md` | the durable-decisions index — an edit to the **standalone mobile Home redesign** arc, ⛔ not remote-admin | ⛔ NO gate reads it. `ownership.py` scans `lib/ src/ test/` only; `gen_command_inventory.py`, `check_a0_matrix.py` and `check_data_type_literals.py` scan sources. |
| `tracker.md` | the same arc's tracker | ⛔ NO |
| `docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md` | that arc's design draft | ⛔ NO |

⇒ the three cannot affect any measurement in this report. The ONE gate that sees them at all is
`git diff --check` (whitespace), whose scope is the whole worktree; run on those three paths alone it is CLEAN.
**⛔ Nothing was reverted, stashed, committed or repaired**, and they are excluded from this slice's file lists
in §11. This is reported for the dispatcher/QA to rule on, ⛔ not resolved here.

ⓘ ONE untracked artifact WAS this slice's and was removed: `a-t.d`, a `-MMD` dependency file the Xtensa/RISC-V
toolchain wrote into the repository root during a compile-only `sizeof` reveal (the `idedata` flag set carries
`-MMD`). It is gone; `git status` shows no stray artifact.

---


---

## 0.2 ⚠⚠ QA FOLD-IN [[B341]] (S5-Q1) — LIVE ACTIVATION DID NOT COMPOSE ACROSS A REBOOT

**Status: FIXED IN THIS SLICE, before PASS. The pre-fix figures are kept visible below.**

### The defect QA reproduced

`admin_runtime_boot` committed the **CLEARED image** (`clear_root` + a full ACL reset) whenever
`admin_provisioning_ready` was false — i.e. whenever EITHER record was bad or the ACL held no owner. That
discarded the **VALID** half too. And because each verb prepares only its OWN half — `AclService::commit_` sets
`set_acl` and never the root, `AdminIdService::mint_` sets `set_root` and never the ACL image — two ordinary
sequences printed a **durable success line over a running node that was not accepting**, until the next reboot,
with ⛔ no console line afterwards that would reveal it:

| | sequence | pre-fix running state after the last durable success |
| --- | --- | --- |
| **B** | `admin-id generate` → REBOOT (ACL still absent) → `acl add owner <key>` | prints `> acl added slot=0 role=owner`; `root_present` **0**, `remote_session_accepting` **false** |
| **D** | valid owner ACL + CORRUPT root record → REBOOT → `admin-id reset confirm` | succeeds; `acl_occupied` **0**, accepting **false** |

This is the codebase's recurring **"a success that isn't"** shape. My shipped `§radmin-5/R14` and the inbox-verbs
rows `S5-1a..S5-6b` never crossed a boot with only ONE record valid, which is exactly why both stayed green —
the gap was in the *sequence*, not in any single step.

### Why the counting fake could not see it, and what does

`FakeRuntime` records the LAST plan; the defect is that a sequence of individually-correct plans leaves the
running block wrong. The fold-in therefore adds a **`RealRuntime`** to `test_firmware_admin_runtime.cpp` that
applies each plan through the production `remote_session_install`, exactly as `Node::admin_session_commit` does.

### The fix — each valid half installs independently

`src/firmware_admin_runtime.h`, `admin_runtime_boot`:
* **root half** — installed from a valid `/mradmid` (pair + its ten prepared epochs); a bad one installs
  `clear_root`.
* **ACL half** — installed from a valid `/mracl` that already holds an owner (image + a prepared epoch per
  occupied row); a bad one installs the **cleared** image, so no stale live ACL can survive.
* readiness stays the recomputed conjunction (`admin_provisioning_ready`), and a boot always sets
  `invalidate_all` — a cold start keeps no session.

⛔ **Design §6.4 is unchanged**: no implicit provisioning, no fallback key, no invented owner. ★ And the two
readiness notions cannot diverge: `acl_content_valid` already REFUSES an occupied-but-ownerless record
(`src/firmware_admin_acl.h`, `if (occupied > 0 && owners == 0) return false;`), so an `ok` non-empty ACL always
has an owner — which is why "install the ACL half when it is `ok` **and** has an owner" keeps
`remote_session_accepting` and `admin_provisioning_ready` in exact agreement.

### The property, now executed on both instruments

> **After any sequence of durable successes, the running readiness and slot count equal what a fresh boot on the
> same medium installs.**

| where | cases |
| --- | --- |
| native, through the pure services against the REAL `RemoteSessionState` | **`§radmin-5/R16`** (sequence B) · **`§radmin-5/R17`** (sequence D) · **`§radmin-5/R18`** (a bad half still installs ITS cleared half — no stale live ACL, no stale root). Each ends by re-booting on the same medium and asserting the SAME readiness. |
| `tools/probe_inbox_verbs`, through the REAL router, the REAL NV wrappers and the REAL `admin_stores_boot_report_console()` | **`S5-7a..S5-7f`** (sequence B) · **`S5-8a..S5-8f`** (sequence D), on the actual `g_node`. ⛔ Neither instrument alone proves it: native cannot reach `firmware_commands.cpp` (§B115), and the probe cannot reach the ordering rules. |
| the mutation harness | **`radmin5runtime` T15** restores the pre-fix conjunction-gated whole-image boot; **T16** is the HALF-fix (the root half composes, the ACL half does not). Both must turn the suite RED. |

**`§radmin-5/R14`'s draw/commit expectations were RE-DERIVED, not accommodated** — and the re-derivation is the
point rather than a formality: its "a bad prerequisite costs `rt.draws == 0`" was true *only because the old boot
installed nothing*. It now asserts, per arm, that the bad half is cleared AND the good half is installed (root
absent ⇒ 2 draws for the two occupied ACL rows; ACL bad ⇒ 10 draws for the root and an all-empty ACL image).

### Figures across the fold-in

| | pre-fold-in | post-fold-in |
| --- | --- | --- |
| native | 2822 / 119652 / 0 | **2825 / 119784 / 0** (+3 cases, +132 assertions) |
| `probe_inbox_verbs` ACCEPT arm | 168 checks / 30 controls | **180 / 30**, 0 unusable (+12 rows) |
| `radmin5runtime` battery | 14 entries | **16 entries** (+T15, +T16) |

⛔ The register row [[B341]] itself was **not edited** — the Author closes it. ⛔ Nothing else in the register,
the rulings ledger or any QA document was touched.
## 1. What changed, per file, in BOTH repositories

### 1.1 MeshRoute — production (`lib/core`, `lib/hal`, `src`)

| path | +/− | what |
| --- | --- | --- |
| `lib/core/remote_session.h` | **NEW** | the pure value types, the 2 064-byte ACCEPT state block, the §10 verdicts, the install carrier and the expiry API |
| `lib/core/remote_session.cpp` | **NEW** | the layout `static_assert`s, the ONE ECDH adapter, the classifier, the read-only bootstrap encoder, the partitioned admission and the earliest-deadline scan |
| `lib/core/node.h` | +104 / −? | the include, `kRadminExpiryTimerId = 91`, the ACCEPT session API, the ONE `_radmin_session` member, `_remote_inbound` → CLIENT-only, the four private arm halves, `test_last_allocated_timer_id()`, the re-pinned `sizeof(Node)` + its ledger entry |
| `lib/core/node.cpp` | +48 | the ACCEPT timer case, `admin_draw_epoch`, `admin_session_commit`, `admin_session_entropy_failed` |
| `lib/core/node_mac_rx.cpp` | +206 | the v2 ACCEPT admission replacing the legacy staging, `radmin_build_carriers`, `radmin_send_reply`, `radmin_expiry_arm/_fire`; the staging helper → CLIENT-only |
| `lib/core/node_mac.cpp` | +12 | the CLIENT guard on `take_remote_inbound` + its zero-state `#else` stub. ⛔ Nothing else moved. |
| `lib/core/protocol_constants.h` | +14 / −2 | comment only: the corrected timer-exhaustion note and the named derivation of the staging lifetime |
| `lib/hal/timer_wheel.h` | +16 / −2 | `kCap` 91 → 92 and its history comment |
| `src/firmware_admin_runtime.h` | **NEW** | the prepare/commit/discard bridge, the ONE record→image conversion, the per-slot plan builders and the boot conjunction |
| `src/firmware_admin_identity.h` | +52 | `IAdminLiveInstall`, `AdminIdErr::runtime_unavailable`, the ordered `mint_()` |
| `src/firmware_admin_acl.h` | +29 | the optional seam, `AclErr::runtime_unavailable`, the ordered `commit_()` and `recover()` |
| `src/firmware_admin_verbs.h` | +5 | the two `runtime_unavailable` lexemes |
| `src/firmware_commands.cpp` | +49 | `DeviceAdminRuntime`, the per-call `AdminLiveInstall`, the boot install and the one `> admin-session boot` line |
| `src/fw_main.cpp` | +9 | the CLIENT guard around the WHOLE legacy drain block (its two statics included) |

⛔ `lib/core/remote_codec.{h,cpp}` were **NOT modified at all** — not even the "no consumer" comment. That
correction landed in the SIMULATOR's source list instead (§1.3), where the claim actually gated a build
decision; editing the codec header would have touched a KAT-pinned file for a comment. Stated as a deliberate
narrowing of the fence, not an omission.

### 1.2 MeshRoute — tests and tools

| path | what |
| --- | --- |
| `test/test_remote_session.cpp` | **NEW** — 32 cases, the pure suite |
| `test/test_node_remote_session.cpp` | **NEW** — 12 cases, the REAL receive/TX path |
| `test/test_firmware_admin_runtime.cpp` | **NEW** — 18 cases, the prepare/commit/discard ordering and the [[B341]] reboot-composition property |
| `test/test_node_r3.cpp` | four legacy-ACCEPT cases SUPERSEDED IN PLACE (⛔ none deleted) |
| `test/test_timer_wheel.cpp` · `test/test_node_join.cpp` · `test/test_radmin_characterization_0e.cpp` | the three executed `kCap == 91` obligations updated, each keeping its own claim |
| `test/radmin_0e_candidate_types.h` | COMMENT ONLY — the "no 92nd slot may be spent" claim resolved to "option 2 shipped", and the 32-byte seen candidate marked as 0e HISTORY against the 48-byte production row. ⛔ Not one candidate type changed, and the P1 mirror is NOT re-pointed. |
| `test/test_custody_receive_g.cpp` | the `sizeof(Node)` line re-pinned (its four S3 pins untouched) |
| `tools/probe_board_abi.py` | RE-PIN 2 (native + gateway) with its measured derivation |
| `tools/probe_features/ownership.py` · `run.sh` | the census 7 → 9 files, the staging guard, 7 new controls, the O4x letters re-derived |
| `tools/probe_inbox_verbs/probe_main.cpp` · `run.sh` | R32g re-derived + 22 new ACCEPT wiring rows |
| `tools/probe_ui_model_mutations.py` | 3 new batteries (52 entries) + the PIN re-sync |

### 1.3 Simulator — ONE authorized edit

`CMakeLists.txt`, `_meshroute_core_srcs`: **one added line** for `remote_session.cpp` (compiled by BOTH core
variants), plus the comment-only correction of the neighbouring `remote_codec.cpp` line's obsolete
*"NO consumer yet … nothing calls it"* claim. ⛔ No other simulator file was touched.

```
$ cd /home/staszek/lora-universal-simulator && git diff --stat
 CMakeLists.txt | 3 ++-
 1 file changed, 2 insertions(+), 1 deletion(-)
```

---

## 2. Pre-measurement predictions (recorded BEFORE the runs they name)

| # | prediction | outcome |
| --- | --- | --- |
| P1 | the simulator rebuilds BOTH core variants with `remote_session.cpp`; the `lus` **executable CHANGES** (Node's layout moves on the host ABI) | ✅ `b1b1d92c…` → `db6582a1…`, 44 build actions, both variants |
| P2 | corpus **36/36 byte-identical** streams, anchors 36/36, s18 `32afbf11`/269517/0 — ⛔ no re-anchor | ✅ 36/36 identical by md5, anchors 36/36 |
| P3 | ZERO `radmin_*` events in all 36 streams | ✅ zero |
| P4 | `gateway` RAM: `sizeof(Node)` +1824, PLUS the freed `fw_main` drain statics, PLUS the wheel's +8 ⇒ **net BELOW +1824** | ✅ **+1336**, attributed to five symbols exactly |
| P5 | `heltec_mobile`: `sizeof(Node)` UNMOVED, RAM **+8 exactly** (the wheel inside `DeviceHal`) | ✅ Node 117912 unmoved, RAM +8, and it is the ONLY changed .bss symbol |
| P6 | native cases/assertions UP, 0 failed | ✅ 2763/118344 → **2825/119784** |
| P7 | `probe_board_abi.py` PASS at the re-pinned table; `probe_b278_row_abi.py` 42 measurements UNCHANGED | ✅ |
| P8 | the ownership census grows by the fenced owners and by NOTHING in the two new pure files | ✅ 7 → 9 files; `remote_session.{h,cpp}` and `firmware_admin_runtime.h` name no capability at all |
| P9 | inventory/help UNCHANGED (204 rows, union 53) — no verb, no BLE policy change | ✅ |
| P10 | warning census: 6 envs at their pins, `-Wswitch` 0, zero new warnings | ✅ |
| P11 | `sizeof(RemoteSessionState)` 2064 and `sizeof(Node::RemoteInbound)` 245 on ALL THREE ABIs | ✅ measured on native, heltec_mobile and gateway |

⚠ **ONE PREDICTION WAS NOT RECORDED BEFORE ITS MEASUREMENT, and it is named rather than back-dated:** the
per-ABI `sizeof(Node)` figures. The re-pin is required to come *from* the measurement (brief §7: "Re-pin
native/gateway Node ONLY from actual ABI measurements"), so the numbers were measured first and the ledger
written from them. What WAS predicted in advance, and held, is the SHAPE: native pays the block alone, gateway
pays the block minus the freed slot, mobile pays nothing.

---

## 3. Native suite

| | base (`1677b44`) | final | delta |
| --- | ---: | ---: | ---: |
| cases | 2763 | **2825** | +62 |
| assertions | 118344 | **119784** | +1440 |
| failed | 0 | **0** | — |

```
$ pio test -e native && ./.pio/build/native/program
[doctest] test cases:   2825 |   2825 passed | 0 failed | 0 skipped
[doctest] assertions: 119784 | 119784 passed | 0 failed |
[doctest] Status: SUCCESS!
```
⚠ The `pio` wrapper prints its usual false *"0 test cases"* — the figures above are the BINARY's, read from a
separate invocation of `./.pio/build/native/program`.

**+62 cases**: `test_remote_session.cpp` 32 · `test_node_remote_session.cpp` 12 · `test_firmware_admin_runtime.cpp` 18
(the last three are the [[B341]] fold-in's `R16`/`R17`/`R18` — §0.2).
**⛔ NO case was deleted.** Eight existing cases changed and each kept its own obligation:

| case | what moved | what it still asserts |
| --- | --- | --- |
| `test_timer_wheel.cpp` "out-of-range caller id" | 90/91 → 91/92 | the wheel admits the LAST allocated id and refuses the one past `kCap`; 90 is still checked so the id below the new one is undisturbed |
| `test_node_join.cpp` §S0-5 gate 16 | `kCap == 91` → `== 92` **+ a new line** | the mobile-aging expiry allocates NO timer id of its own — now asserted DIRECTLY (`test_last_allocated_timer_id() == 91`) instead of implied by the cap |
| `test_radmin_characterization_0e.cpp` 0e-B | `kCap == 91` → `== 92` **+ 4 new lines** | the 0e mirrors are KEPT; the shipped wheel is now proved byte-identical to the **92-id** mirror and exactly +8 vs the 91-id one — 0e's projection became a measurement |
| `test_custody_receive_g.cpp` §B278-S3/20 | `sizeof(Node)` 222072 → 224136 | its four S3 pins (action 56/4, the two record sizes) are untouched |
| `test_node_r3.cpp` "rcmd: a REMOTE_CMD DM STAGES…" | REWRITTEN | that a `REMOTE_CMD` no longer stages, that nothing leaked sideways, WHY it was refused, and that the CLIENT half still works |
| `test_node_r3.cpp` §radmin-1b/2 | REWRITTEN | that a REAL flight still REACHES the ACCEPT owner (not the fail-closed guard) — and is now refused there |
| `test_node_r3.cpp` §radmin-1b/4 | re-anchored to `REMOTE_RESP` | the empty-body and 200-byte staging boundaries, unchanged, on the owner that still has a slot |
| `test_node_r3.cpp` §radmin-1b/5 | REWRITTEN | the full-slot refusal still fires — on the SAME owner now — and an interleaved `REMOTE_CMD` is INERT to it |

---

## 4. The R-RA-31 ABI re-pin — measured, and attributed member by member

**The authority, verbatim (R-RA-31):** *"Agree - Slice 5 answers bootstrap on air, ABI re-pin authorized"*.

### 4.1 The measurement

A compile-only `template<unsigned long long N> struct Reveal;` on each target's OWN `pio run -t idedata` flag
set, driven through `tools/probe_board_abi.py`'s flag authority — ⛔ never inferred from a delta, and ⛔ never
from an ad-hoc flag set. **Four tree variants** were measured so the two halves are INDEPENDENT rather than
one derived from the other:

| variant | native | heltec_mobile | gateway |
| --- | ---: | ---: | ---: |
| BASE `1677b44` | 222072 | 117912 | 148680 |
| **VAR-B** = final, but the block removed AND the legacy slot restored unconditional | **222072** | **117912** | **148680** |
| VAR-A = final, block removed (slot CLIENT-only) | 222072 | 117912 | 148440 |
| **FINAL** | **224136** | **117912** | **150504** |

★ VAR-B reproduces the BASE byte for byte on all three targets ⇒ neither term is inferred from the other.

Component sizes, on all three ABIs: `sizeof(RemoteSessionState)` = **2064** (alignof 8);
`sizeof(Node::RemoteInbound)` = **245**.

### 4.2 The arithmetic, by `offsetof`

| target | role | `_admin_provisioned` | `_remote_inbound` | `_channel_seal_ctr` | `_radmin_session` | `_cfg` |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| native BASE | {1,1} | 224 | 225 | 470 | — | 472 |
| native FINAL | {1,1} | 224 | 225 *(RETAINED)* | 470 | **472** | **2536** |
| heltec_mobile BASE | CLIENT | — | 180 | 426 | — | 432 |
| heltec_mobile FINAL | CLIENT | — | **180** | **426** | — | **432** |
| gateway BASE | ACCEPT | 152 | 153 | 398 | — | 400 |
| gateway FINAL | ACCEPT | 152 | *(absent)* | **154** | **160** | **2224** |

* **native +2064 EXACTLY.** `_channel_seal_ctr` ends at 472, an 8-byte boundary, so the 8-aligned block lands
  there with ⛔ ZERO padding and `_cfg` shifts by exactly 2064. **No hole opens anywhere.** Native compiles BOTH
  roles (R-RA-17), so the CLIENT slot at 225 is RETAINED and is ⛔ not subtracted. *Sixteenth application of the
  `radio_freq_mhz` / `team_hop_cap` / T-K1 padding-placement rule.*
* **heltec_mobile +0 — THE CONTROL.** Every offset is byte-identical. ⚠ **And an unmoved `sizeof(Node)` is NOT
  unmoved board RAM ([[B331]])** — see §5.
* **gateway +1824**, and the three terms close exactly:
  `−244` (the 245-byte slot, LESS the ONE new pad byte the 2-aligned `_channel_seal_ctr` needs at the now-odd
  offset 153) `+ 4` (the block's own 8-alignment pad, 156..159) `+ 2064` = **+1824**.
  Cross-check from `sizeof` alone: the gate-only variant measures 148440 = 148680 − 240, i.e. the −244 rounded
  by the struct's 8-byte tail alignment; the block then adds 2064 on top.

### 4.3 What was re-pinned, and what was NOT

| pin | before | after |
| --- | --- | --- |
| `lib/core/node.h:` the native `static_assert` + its ledger entry | 222072 | **224136** |
| `tools/probe_board_abi.py` PIN_TABLE `native/meshroute::Node` | 222072 | **224136** |
| `tools/probe_board_abi.py` PIN_TABLE `gateway/meshroute::Node` | 148680 | **150504** |
| `tools/probe_board_abi.py` PIN_TABLE `heltec_mobile/meshroute::Node` | 117912 | **117912 — UNCHANGED (the control)** |
| every OTHER PIN_TABLE row, all three targets | — | **UNCHANGED** |
| `tools/probe_b278_row_abi.py` — every row | — | **UNCHANGED**; its 42 measurements reproduce, which is itself the proof rather than an assertion that it mirrors Node |
| `test/test_custody_receive_g.cpp`'s `sizeof(Node)` line | 222072 | **224136** (the only test that names it) |

⛔ **No B278 row was touched.** The brief required proof of an actual dependency before any mirror edit; there is
none — `probe_b278_row_abi.py` measures the delegated-custody correlation row and its neighbours, none of which
this slice moves, and it reproduces unchanged.

---

## 5. Boards — the ruled pair, fixed identity, same paths

Captures: `.pio-measure/s5-base-pair/{gateway,heltec_mobile}` (taken on the CLEAN tree, before the first edit)
and `.pio-measure/s5-gate3-pair/{gateway,heltec_mobile}` (the FINAL tree, post-[[B341]]-fold-in; the
pre-fold-in `s5-final-pair2` capture measured the identical RAM and the identical five/two symbols — only
gateway flash moved, 544460 → 544668, for the per-half boot logic). Both under `.pio-measure/`, both OUTSIDE
`.pio-measure/env` ([[B315]]), both at the identical fixed-identity per-env build paths.

| env | RAM base → final | Δ | flash base → final | Δ | objects |
| --- | --- | ---: | --- | ---: | --- |
| `gateway` | 195844 → **197180** | **+1336** | 531004 → **544668** | **+13664** | 284 → 285 |
| `heltec_mobile` | 207740 → **207748** | **+8** | 1367448 → **1367444** | **−4** | 328 → 329 |

### 5.1 gateway RAM — EVERY byte attributed, by symbol

Exactly **five** `.bss` symbols changed size. Nothing else in the image did.

| Δ | symbol | base → final | what it is |
| ---: | --- | --- | --- |
| **+1824** | `g_node` | 148680 → 150504 | the `sizeof(Node)` re-pin of §4 |
| **−245** | `mesh_service_once::ri` | 245 → 0 | the legacy drain's `static RemoteInbound`, freed by the CLIENT guard |
| **−241** | `mesh_service_once::pt` | 241 → 0 | the sealed-response arm's `static uint8_t pt[241]`, freed with it |
| **+8** | `g_hal` | 4376 → 4384 | `TimerWheel::kCap` 91 → 92 — HAL storage, ⛔ NOT a Node member |
| **−2** | `mrfw::remote_seal_resp::s_resp_ctr` | 2 → 0 | link-time-collected with the legacy target execution |

symbol total **+1344**; `.bss` section **194836 → 196172 = +1336**. The 8-byte difference is inter-symbol
alignment padding INSIDE the section, and it is measured rather than argued: the section carried 129 bytes of
padding at base and 121 at final. ⇒ +1344 − 8 = **+1336, fully closed**. (`.heap` absorbs the mirror −1336; the
linker script gives `.bss` and `.heap` one budget.)

★ **AND THE LEGACY TARGET EXECUTION REALLY LEFT THE IMAGE.** These `.text` symbols are present at base and
ABSENT at final on `gateway`, collected once the CLIENT guard removed their only caller:
`mrfw::remote_exec`, `mrfw::remote_seal_resp`, `meshroute::admin_cmd_open`, `meshroute::admin_cmd_verify`,
`meshroute::Node::take_remote_inbound`. That is R-RA-27's hand-off discharged, measured at the linker.

### 5.2 heltec_mobile — the control, and the [[B331]] correction it proves

Exactly **two** symbols changed on the whole image:

| Δ | symbol | section | what it is |
| ---: | --- | --- | --- |
| **+8** | `g_hal` | `.dram0.bss` | `TimerWheel::kCap` 91 → 92 — **the ONLY RAM change on the client product** |
| **−4** | `setup()` | `.flash.text` | codegen only |

★★ **[[B331]] IS EXACTLY RIGHT, AND THIS IS THE MEASUREMENT.** `sizeof(Node)` is UNMOVED on this product and its
RAM still moves, because the wheel is `DeviceHal::_wheel` and not a `Node` member. The pre-check's original
"heltec_mobile ±0" would have been wrong by 8 bytes.

⛔ **AND THE ACCEPT STATE IS COMPILED OUT, PROVEN BY ABSENCE:** a search of the mobile symbol table for
`remote_session` / `RemoteSession` / `radmin` returns **0** matches.

---


### 5.3 Re-confirmed on the final tree

The whole of §5.1 and §5.2 was re-derived from `gate3`'s capture after the [[B341]] fold-in landed, and is
byte-identical: the SAME five `.bss` symbols on `gateway` (+1824 / −245 / −241 / +8 / −2, section +1336 with the
same 8 bytes of measured inter-symbol padding) and the SAME two on `heltec_mobile` (`g_hal` +8, `setup()` −4).
A search of the final mobile symbol table for `remote_session` / `RemoteSession` / `radmin` still returns **0**.
## 6. Simulator and the 36-stream corpus

### 6.1 The build

```
$ cd /home/staszek/lora-universal-simulator && cmake -S . -B build && cmake --build build -j8
[ 52%] Building CXX object CMakeFiles/meshroute_core_gw.dir/…/lib/core/remote_session.cpp.o
[ 71%] Building CXX object CMakeFiles/meshroute_core_normal.dir/…/lib/core/remote_session.cpp.o
[100%] Built target lus            BUILD_RC=0
```
* **44 build actions**, and `remote_session.cpp` is compiled by **BOTH** core variants (`meshroute_core_gw` and
  `meshroute_core_normal`) — the lines above are the proof, not the CMake edit.
* `lus` md5 **`b1b1d92c541cc7f6f63864a2bcc6a355` → `db6582a171a6d785720e324c2cfe43f1`** — the executable CHANGED,
  exactly as predicted (Node's layout moves on the host ABI). ⛔ A no-op build was NOT predicted and did not occur.
* `MESHROUTE_DIR` in `build/CMakeCache.txt` = `/home/staszek/lora-universal-simulator/../MeshRoute` — the measured
  checkout. A build pointing elsewhere could not have passed.

### 6.2 The corpus

```
$ python3 tools/run_corpus.py --jobs=8 --require-anchors
  [35/36] ok  s18_meshroute   32afbf11 events=269517 (20.4s)
PASS: 36/36 streams produced and validated, 0 failures
  anchors: 36/36 rows reproduce simulation/BASELINE.md
```

* **s18 keystone `32afbf11` / 269517 / 0** — the current `simulation/BASELINE.md` value, reproduced. ⛔ NO
  re-anchor, and `simulation/BASELINE.md` was **NOT EDITED**.
* **36/36 streams BYTE-IDENTICAL** to the base run, compared by md5 of every `streams/*.ndjson`:
  `identical=36 differing=0`. The base and final run directories differ only in `inputs/lus` (the changed
  binary) and the `logs/` (timings/paths).
* **Remote-event census: ZERO.** `grep` for `radmin*`, `remote_inbound*` and `rcmd*` across all 36 streams
  returns nothing. That is by CONSTRUCTION, not by luck: no scenario airs `0xA0`/`0xA1`, and `Node::on_init`
  performs no new draw, arms no timer and emits nothing (asserted natively — §radmin-5/N10).

### 6.3 The RNG-isolation contract, stated as the mechanism

⛔ Every entropy draw this slice adds is reached ONLY from `src/` (`admin_runtime_boot` at `setup()`, and the two
verb paths). `platformio.ini`'s native env and the simulator both compile ZERO `src/*.cpp`, so a simulator node
can reach `Node::admin_draw_epoch` by no path at all. Case §radmin-5/N10 asserts the same thing directly on a
fresh `Node`: readiness `disabled`, 0 slots, timer 91 armed **0** times, no `radmin_*` emit.

---

## 7. The receive / crypto / reply contract — executed transcripts

All 59 new/renamed `§radmin-5/*` cases:
```
$ ./.pio/build/native/program -tc='*radmin-5*'
[doctest] test cases:   59 |   59 passed | 0 failed | 2763 skipped
[doctest] assertions: 1284 | 1284 passed | 0 failed |
```

### 7.1 The two-endpoint bootstrap exchange, over REAL frames (§radmin-5/N1)

The request is built by the PRODUCTION codec from a controller-side base key the fixture derives itself
(there is no controller implementation until Slice 8a), delivered as a real RTS → `pack_data` DATA carrying a
real `pack_unicast_inner` inner with `SOURCE_HASH` → the post-ACK timer, and the answer is read back out of the
HAL's **captured TX bytes** after driving the real CTS → DATA → ACK hand-off.

```
CHECK( node.admin_draw_epoch(e) )                        -> true          (x3, the REAL checked HAL draw)
CHECK( node.admin_session_readiness() == ready )         -> 1 == 1
CHECK( node.admin_session_slots() == n )                 -> 3 == 3
CHECK( dn > 0 )                                          -> 75 >  0       (the DATA frame really packed)
CHECK( t.hal.count("radmin_rx") == 1 )                   -> 1 == 1        (the arm ran)
CHECK( t.hal.count("radmin_bootstrap_tx") == 1 )         -> 1 == 1        (the ONE reply this slice may send)
CHECK( t.hal.tx_frames.size() > tx_before )              -> 4 >  0        (bytes actually left the node)
CHECK( body.size() == kRemoteOverheadBootstrapResponse ) -> 33 == 33      (read out of the AIRED frame)
CHECK( resp_src == t.self.key_hash32 )                   -> 1518927972 == 1518927972
CHECK( resp_src != t.ctrl[1].key_hash32 )                -> 1518927972 != 2125651976
CHECK( st == RemoteStatus::ok )                          -> 0 == 0        (the CONTROLLER opens it)
CHECK( d.authenticated )                                 -> true
CHECK( d.layout.domain == RemoteDomainId::resp_bootstrap)-> 11 == 11
CHECK( d.msg.slot == 1 )                                 -> 1 == 1        (the matched slot)
CHECK( d.msg.request_id == 0xC0FFEEu )                   -> 12648430 == 12648430
CHECK( d.msg.admin_epoch == …state().epoch[1] )          -> 13735838543713076453 == …
CHECK( d.msg.admin_epoch != 0 )                          -> … != 0
CHECK( remote_session_seen_used(...) == 0 )              -> 0 == 0        (⛔ read-only: no seen row)
CHECK( remote_session_staging_used(...) == 0 )           -> 0 == 0        (the reserved row was RELEASED)
```
★★ **THE TWO SOURCE FACTS ARE DIFFERENT AND BOTH ARE MEASURED.** The response CARRIER's own `SOURCE_HASH` is
this target's routing identity (`1518927972`); the CRYPTO `RemoteSource` stays the ORIGINAL controller's
(`2125651976`) in both directions — which is why the controller can open the answer at all. ⛔ The physical
response source is never fed back as the codec's controller-domain input.

### 7.2 Cross-layer: the reversal is MEASURED, not asserted (§radmin-5/N5)

Depths 2, 3 and 4, each with the received path terminating on our layer. The reversal is observed through
`xl_send_no_gateway`'s own `target_layer` FIELD — which IS `hops[0]`, the first reversed destination hop
`originate_layer_path` was handed:

| depth | received path | expected `hops[0]` = `path[depth-2]` | measured `target_layer` | not the un-reversed `path[0]` |
| ---: | --- | ---: | ---: | --- |
| 2 | `{0x11, 0x00}` | `0x11` (17) | **17** | (vacuous at depth 2 — stated, not asserted) |
| 3 | `{0x21, 0x11, 0x00}` | `0x11` (17) | **17** | `17 != 33` ✅ |
| 4 | `{0x31, 0x21, 0x11, 0x00}` | `0x11` (17) | **17** | `17 != 49` ✅ |

At every depth: `radmin_rx` 1, `radmin_bootstrap_tx` 1, ⛔ **no same-layer REMOTE_RESP was aired** and the
staging row was released. §radmin-5/N6 adds the unreversible case (a path that does not terminate on our
layer): `radmin_reply_bad_path` 1, `radmin_bootstrap_tx` **0**, ⛔ no same-layer substitute.

### 7.3 The other executed obligations, by case

| requirement (brief §6) | case |
| --- | --- |
| all ten ACL slots answer bootstrap | `B1` — ten iterations, each decoded under ITS controller's base key at ITS slot and epoch |
| wrong target · absent key · bad tag · bad source | `B2` (a–d) — all SILENT and mutually indistinguishable |
| low-order key refused before a key exists | `B3` |
| bad epoch | `G4` — fails as `auth_failed` BY CONSTRUCTION; ⛔ there is no epoch comparison in the code to fool |
| cold-boot dead draw | `N7` — refuses, leaves the caller's value untouched, node stays disabled, request unanswered |
| queued vs parked vs refused | `N4` — parked (no binding ⇒ nothing airs, row still released) and queued (binding learned ⇒ 33 B airs) |
| lost response, then exact read-only retry | `N2` — second answer **byte-identical**, epoch unmoved, seen still 0 |
| ONLY `REMOTE_RESP` bootstrap is emitted by the new owner | `N9` — an authenticated EXECUTE reserves state and airs **nothing** |
| atomic seen+ingress · full 16-row pool · both starvation directions · 3 open + reserved bootstrap · per-source open bound · body lifetime · immutable route after ingress expiry | `P4` · `C5` · `P1` · `B4` · `B5` · `E5` · `E5` |
| now below / equal / above expiry · equal deadlines · cancel after invalidation · safe overflow · ⛔ no seen deletion | `E2` · `E3` · `E4` · `E1` · `E2`+`N9` |
| invalid packets reserve nothing | `G3` |
| a ZERO reply destination is an explicit send FAILURE, ⛔ never a fallback to the origin | `N11` |
| the reservation's deadline is stamped from the CURRENT time, not from zero | `N12` |

⚠ **SYNTHETIC-ARM LABEL, stated once and clearly:** `§radmin-5/C4` (design §10 case 4, `already_acknowledged`)
is driven by an EXPLICIT VALUE FIXTURE — the seen row's `state` byte is written to `acknowledged` directly,
because Slice 7b owns the ACK producer. ⛔ It is **not** executed RF behaviour and must not be reported as one.
`session_full` (case 5) is by contrast fully REAL and reachable; only its wire ANSWER does not exist yet.

---

## 8. Mutation selectors and the union

Both selectors are DERIVED from the completed closure and the final diff, and printed separately, as §7 requires.
Every battery ran from an isolated staging copy
(`rsync -a --delete --exclude=.git --exclude=.pio --exclude=.pio-measure --exclude=.claude`) with
`--workers=2`; ⛔ **no battery ever ran in the measured checkout**, and each run reports
`real tree untouched: all N target files byte-identical to launch (md5)`.

### 8.1 Selector A — CHANGED SOURCE (22 targets)

Derived mechanically: every battery target whose configured source file this slice modified.

| source file this slice changed | targets |
| --- | --- |
| `lib/core/remote_session.cpp` | **`radmin5session`** (NEW) |
| `lib/core/node_mac_rx.cpp` | **`radmin5rx`** (NEW) · `a0rx` · `b159rx` · `b161rx` · `b251rx` · `sliceBrx` · `sliceGrx` |
| `lib/core/node.cpp` | `b159map` · `sliceBnode` · `sliceEnode` · `teamgrant` |
| `lib/core/node_mac.cpp` | `b159mac` · `b161mac` · `b20mac` · `grantadmit` · `sliceBmac` |
| `lib/core/protocol_constants.h` | `b159const` |
| `src/firmware_admin_runtime.h` | **`radmin5runtime`** (NEW) |
| `src/firmware_admin_identity.h` | `radmin3id` |
| `src/firmware_admin_acl.h` | `radmin3acl` |
| `src/firmware_admin_verbs.h` | `radmin3verbs` |

⛔ **Five changed source files have NO battery target, and each absence is accounted for rather than passed over:**

| file | why there is no battery, and what covers it instead |
| --- | --- |
| `lib/core/node.h` | it has never had one (declarations, members and the ABI ledger). Its decisions are covered by the `static_assert` (a compile-level control no mutation can pass), by `tools/probe_board_abi.py`, and by the `ownership.py` controls **W-S5-DROP-SESSION** and **W-S5-INVERT-SLOT**, which attack the two capability gates the ABI depends on. |
| `lib/core/remote_session.h` | types, capacities and `static_assert`ed offsets. The CAPACITIES are attacked from the `.cpp` side (`radmin5session` S18/S19 change what the pool does); the layout assertions cannot be mutated into a green build at all. |
| `lib/hal/timer_wheel.h` | one constant. It is pinned by THREE executed native cases (`test_timer_wheel.cpp`, `test_node_join.cpp` gate 16, `test_radmin_characterization_0e.cpp` 0e-B) and by the board RAM measurement (+8 on both). |
| `src/firmware_commands.cpp` · `src/fw_main.cpp` | compiled by NEITHER the native suite NOR the simulator (§B115), so a mutation battery cannot build them. Their cover is `tools/probe_inbox_verbs`' **22 new executed rows** (§9) and `ownership.py`'s **W-S5-WIDEN-TIMER / W-S5-INVERT-TAKE / W-S5-DROP-DRAIN**. |

### 8.2 Selector B — HISTORICAL / DEPENDENCY (6 targets)

| target | why it is included |
| --- | --- |
| `radmin2codec` | **IN FULL.** Slice 5 is the codec's FIRST CONSUMER: every request is decoded and the one reply encoded through it, so a KDF/nonce/AAD/cap defect that was previously provable only against the KATs is now reachable through live state. |
| `grantpark` | `park_send`'s stored-or-dropped FACT — the `SendDispatch::parked` this slice's reply path classifies (§radmin-5/N4). |
| `b161hash` · `b251hash` | the by-hash resolution and receive-flight admission that decide whether the same-layer reply resolves at all. |
| `teamkeyring` | `SecretWipeGuard` is REUSED verbatim by `firmware_admin_runtime.h` (U1) — the plan's secret half is scrubbed by that exact type. |
| `devicenv` | `firmware_admin_runtime.h` binds its `static_assert`s to `mrnv::kAclSlots` and the three `kAclRole*` values; a change to either would silently re-shape the live image. |

⛔ **Excluded, with reasons** (⛔ never "not obviously related"): `radmin4key/targets/verbs` — the CLIENT-side
controller stores, untouched and not reused; `a0codec`/`sliceAcodec`/`sliceFcodec`/`sliceGcodec`/`b20codec` —
`frame_codec.{h,cpp}` is untouched, and the receive path's use of `parse_unicast_inner`/`pack_data` is exercised
through the six RX batteries already in selector A; `b134*`/`sliceC*`/`sliceD*` — inbox/NVS storage, untouched;
`b159dl`/`b159hal` — the gateway-deadline half of B159, whose constants this slice does not touch; every
`ui*`/`model`/`config`/`chrome`/`icons`/`joinprofiles`/`cfgparse`/`teamseen*`/`provservice`/`uipreset*` target —
display/config surfaces with no relationship to this slice.

### 8.3 The mutation entries this slice ADDED

**`radmin5session` (25 entries, all RED)** — design §10's five cases, the exact 128-bit fingerprint, the NO-EVICTION and
NO-TTL rules, the hard ingress partition, the 3-open + 1-bootstrap split, the per-source open bound, the
read-only bootstrap (⛔ no seen row, ⛔ no epoch change), refuse-don't-clamp, the ordered key derivation, the
saturating deadline, the per-slot vs root-wide invalidation, the first-admitted route and the body wipe.

**`radmin5rx` (14 entries, all RED)** — R-RA-13's mandatory `SOURCE_HASH`, the reply DESTINATION (the 32-bit captured
source vs the 8-bit origin), the cross-layer reversal and its two validations, the `rev + 1` hand-off, the two
carrier descriptions, the shared scan's arm/fire/cancel shape, the staging release and the zero-destination
refusal.

**`radmin5runtime` (16 entries, all RED — T15/T16 added at the [[B341]] fold-in)** — the prepare/commit/discard order, the per-slot invalidation rule, the
complete ten-epoch candidate, the dead-seed refusal, the boot conjunction, the cleared-image-on-disable rule and
the unarmed-after-refusal rule.

⛔ **THERE IS NO "one HAL snapshot per scan" MUTATION, AND ITS ABSENCE IS THE STRONGER STATEMENT.** §4.5 requires
one `now` per scan; that is guaranteed BY CONSTRUCTION — `remote_session_expire` is PURE and takes `now_ms` BY
VALUE, so there is no HAL inside `remote_session.cpp` for a second read to come from and no single-site edit can
break it. The first draft of this battery DID carry such an entry; it measured nothing (the mutant was
behaviourally identical) and was replaced by `X11`, which attacks a decision that really exists — the receive
path stamping the deadline from the CURRENT time. Recorded because "we removed a worthless entry" is the kind of
change that must never look like a silent weakening.

### 8.4 Entries RE-ANCHORED (⛔ none deleted, ⛔ none weakened)

Four historical entries anchored on lines this slice legitimately moved, and went VACUOUS (match count 0) — which
the harness correctly refused as WORTHLESS rather than scoring. Each was RE-POINTED with its reason; the defect
each injects is unchanged.

| entry | old anchor | why it moved |
| --- | --- | --- |
| `radmin3acl` B31 (recovery grants an owner) | `acl_reset`'s save line | the line now carries the slice's `prepare_acl_reset` / `discard()` |
| `radmin3acl` B33 (a failed save reported as success) | `commit_`'s save line | the failure path now carries `discard()`; the replacement also drops the `commit()` so the defect is complete |
| `radmin3id` A03 (save before the draw is validated) | `mint_`'s save line | same reason |
| `radmin3id` A04 (a failed save reported as success) | `mint_`'s save line | same reason |

---

---

## 9. The full gate — every instrument, base → final

⚠ **THE GATE WAS RUN THREE TIMES, AND WHY IS PART OF THE EVIDENCE.** The first full run's warning census found a NEW
warning (§9.1) — `lib/core/node_mac_rx.cpp`'s `rc`, an `MR_EMIT`-only local, i.e. the documented [[B169]] shape.
It was fixed with `[[maybe_unused]]` and every source-dependent gate was re-run on the fixed tree. The figures
below are the FINAL tree's; where an earlier figure is quoted it is labelled.
The SECOND full run (`gate2`) was superseded by QA's [[B341]] fold-in (§0.2), which changed production source.
The THIRD (`gate3`) is the authoritative one: every gate below is from it, on the fixed tree — except the tools
sweep, whose two wrapper failures §9.2 repairs and re-runs.

| gate | base (`1677b44`) | final | verdict |
| --- | --- | --- | --- |
| `pio test -e native` → `./.pio/build/native/program` | 2763 / 118344 / 0 | **2825 / 119784 / 0** | PASS (+62 cases, +1440 assertions) |
| `run_corpus.py --jobs=8 --require-anchors` | 36/36, anchors 36/36 | **36/36, anchors 36/36** | PASS · s18 `32afbf11` / 269517 / 0 · **36/36 streams BYTE-IDENTICAL to base** · zero `radmin_*` events |
| `lus` md5 | `b1b1d92c541cc7f6f63864a2bcc6a355` | **`db6582a171a6d785720e324c2cfe43f1`** | CHANGED as predicted; both variants compile `remote_session.cpp` |
| `measure_board.py pair` — `gateway` | 195844 / 531004 / 284 | **197180 / 544668 / 285** | RAM **+1336** (five symbols, §5.1) · flash +13664 · +1 object |
| `measure_board.py pair` — `heltec_mobile` | 207740 / 1367448 / 328 | **207748 / 1367444 / 329** | RAM **+8** (the wheel, ONLY changed symbol) · flash −4 · +1 object |
| `probe_board_abi.py` | 191 checks, 9/9 RED | **191 checks, 9/9 RED** | PASS at the R-RA-31 re-pin |
| `probe_b278_row_abi.py` | 42 measurements, 6/6 RED | **42 measurements, 6/6 RED** | PASS — ⛔ NO B278 row moved |
| `probe_inbox_verbs` ACCEPT arm | 146 checks / 30 controls | **180 / 30**, 0 unusable | PASS (+34 executed wiring rows: 22 for the slice, 12 for the [[B341]] fold-in) |
| `probe_inbox_verbs` CLIENT arm | 178 / 33 | **178 / 33**, 0 unusable | PASS — the mobile arm's Slice 4 byte pins are UNCHANGED |
| `probe_firmware_ui` | 223 controls | **223 controls / 0 unusable** | PASS — unchanged |
| `probe_custody_usb` | 27 checks / 10 controls | **27 / 10** | PASS — unchanged |
| `probe_ble_line` | 40 checks / 8 controls | **40 / 8** | PASS — unchanged |
| `probe_features` | 9 cells / 118 checks / 52 controls · ownership 33 | **9 / 120 / 59 · ownership 40**, 0 unusable | PASS (+2 checks = the two new census files; +7 controls = the Slice 5 owner boundaries) |
| `gen_command_inventory.py` bare + `--check` | 204 rows | **204 rows**, byte-for-byte | PASS — semantically identical (only `file:line` moved) |
| `check_a0_matrix.py` | PASS | **PASS** — 21 enum members, 6 special rows | unchanged |
| `check_data_type_literals.py` | PASS | **PASS** — 200 files scanned, zero surviving literals | unchanged |
| `warning_census.sh` | 6 envs at pins, `-Wswitch` 0 | **6 envs at pins, `-Wswitch` 0** | PASS — ZERO new warnings (after §9.1's fix) |
| `probe_console_sink` | `profiles=6 checks=720 structural=50 ble_guard=905 ownership=6 ownership_controls=3 controls=99 unusable=0` | **`profiles=6 checks=720 structural=52 ble_guard=905 ownership=6 ownership_controls=3 controls=101 unusable=0`** | PASS (+2 structural = S51/S52; +2 controls = S-C34b/S-C34c; S-C34 re-pointed S34 → S51) |
| `probe_inbox_verbs` CLIENT arm | 178 / 33 | **178 / 33**, 0 unusable | PASS — the mobile arm's Slice 4 byte pins are UNCHANGED |
| `probe_firmware_ui` | 223 controls | **223 controls / 0 unusable** | PASS — unchanged |
| `probe_custody_usb` | 27 checks / 10 controls | **27 / 10**, 0 unusable | PASS — unchanged |
| `probe_ble_line` | 40 checks / 8 controls | **40 / 8**, 0 unusable | PASS — unchanged |
| `probe_features` | 9 cells / 118 checks / 52 controls · ownership 33 | **9 / 120 / 59 · ownership 40**, 0 unusable | PASS (+2 checks = the two new census files; +7 controls = the Slice 5 owner boundaries) |
| mutation union | — | **28 targets, 27 rc 0** | see §8.3; the one non-zero is `sliceBmac`/M04, worthless AT BASE ([[B342]]) |
Warning census, final:
```
env                   objs      warn    expect  -Wswitch        RAM      Flash  verdict
gateway_heltec         329       173       173         0     232300    1319332  ok
gateway_heltec_v4      330       178       178         0     232572    1317316  ok
heltec_mobile          329       177       177         0     207748    1367444  ok
heltec_v3              329       177       177         0     207508    1374512  ok
heltec_v4              330       182       182         0     207780    1372532  ok
heltec_v4_mobile       330       182       182         0     208020    1365424  ok

PASS — 6 OLED env(s) match their pinned warning baseline
```

**Re-run after the fix** — the two wrappers alone, then the complete sweep:

```
$ python3 -m unittest tools.test_probe_console_sink tools.test_probe_features -v
Ran 55 tests in 467.211s
OK                                        (exit 0, read from the command itself — ⛔ never from `$?` after a
                                           pipeline, [[B326]])
```
```
$ python3 -m unittest discover -s tools -p "test_*.py" -v      # the COMPLETE sweep, on the final tree
Ran 329 tests in 518.367s
OK                                        (exit 0, read from the command itself — ⛔ never from `$?` after a
                                           pipeline, [[B326]])
```
⛔ **329, UNCHANGED** — the fix moved no test count in either direction: it replaced two duplicated literals with
derivations inside tests that already existed. ⓘ The sweep's output still contains one line reading
`FAIL: §B278 census selftest, 9 failure(s)` — that is a PRE-EXISTING control FIRING
(`test_b278_correlation_census` sabotages its binder and asserts the battery bites, then restores it), which is
why unittest still reports `OK`. Named here so the next reader does not mistake it for a failure.

The derived values the fixed wrappers now read: `PIN_PROFILES 6 · PIN_CHECKS 720 · PIN_STRUCTURAL 52 ·
PIN_BLE_GUARD 905 · PIN_OWNERSHIP 6 · PIN_OWN_CTL 3 · PIN_CONTROLS 101` (from `run.sh`) and
`len(ownership.CONTROLS) 34 + 6 Y-controls = 40` — each equal to what the probes actually print.

### 8.3 The union's results — 28 targets, from `gate3`'s fresh stage

Every battery ran from `stage6`, an isolated `rsync` copy; each reports
`real tree untouched: all N target files byte-identical to launch (md5)`.

| target | entries RED | unusable |
| --- | ---: | ---: |
| **`radmin5session`** (NEW) | **25** | 0 |
| **`radmin5rx`** (NEW) | **14** | 0 |
| **`radmin5runtime`** (NEW) | **16** | 0 |
| `radmin2codec` (FULL — first consumer) | 66 | 0 |
| `radmin3acl` · `radmin3id` · `radmin3verbs` | 36 · 23 · 28 | 0 |
| `a0rx` · `b159rx` · `b161rx` · `b251rx` · `sliceBrx` · `sliceGrx` | 7 · 3 · 8 · 19 · 16 · 42 | 0 |
| `b159map` · `sliceBnode` · `sliceEnode` · `teamgrant` | 2 · 8 · 3 · 4 | 0 |
| `b159mac` · `b161mac` · `b20mac` · `grantadmit` | 2 · 1 · 11 · 1 | 0 |
| `b159const` | 4 | 0 |
| `grantpark` · `b161hash` · `b251hash` · `teamkeyring` · `devicenv` | 3 · 6 · 55 · 68 · 42 | 0 |
| **`sliceBmac`** | 3 | **1 — M04, see below** |

**27 of 28 targets rc 0.** The one non-zero is `sliceBmac`, for M04 alone.

#### 8.3.1 Every entry that was WORTHLESS during this slice, and how each was closed

⛔ Not one entry was deleted, and not one was weakened. The harness refused to score all of them — a zero-match
edit is `VACUOUS`, a still-green mutant is `FAIL`, and neither counts as a pass. That refusal is what found them.

| entry | what it reported | how it was closed |
| --- | --- | --- |
| `radmin3acl` **B31**, **B33** · `radmin3id` **A03**, **A04** | `VACUOUS` — anchored on the `_store.save(cand)` lines this slice gave a `discard()` | **RE-ANCHORED** onto the new lines, with the reason recorded at each entry. The injected defect is unchanged; A03's replacement was also completed so it still saves *before* the dead-draw check. |
| `radmin5rx` **X05** | `VACUOUS` — its anchor predated the `[[maybe_unused]]` that §9.1 added to the same line | **RE-ANCHORED** onto the attribute-bearing form, with a note that the attribute is production ([[B169]]) and an anchor without it matches zero times. |
| `radmin5runtime` **T10/T11/T12** | `VACUOUS` — anchored on the pre-[[B341]] conjunction-gated boot body | **RE-ANCHORED** onto the per-half boot's three owning lines; each anchor verified to match EXACTLY once. |
| `radmin5session` **S13** | `FAIL` — a 4-byte key compare cannot collide across ten distinct Ed25519 identities, so the mutant was behaviourally identical | **REPLACED** by a decision that exists: the response reports the SENTINEL slot instead of the matched one, which the codec's own §8.1 pairing rule refuses. The absence of a "compare four bytes" entry is now recorded at the site. |
| `radmin5session` **S23** | `FAIL` — the injected write sat on the ADMIT path, which a replay never re-enters | **REPLACED** by storing an admitted row as `free` (the pool never fills, every retry re-admits). |
| `radmin5session` **S25** | `FAIL` — the explicit `crypto_wipe` is followed by a whole-struct reassignment, so removing the wipe alone changed nothing | **REPLACED** by removing BOTH, which is the defect the entry names: a released row keeping its decoded plaintext. |
| `radmin5session` **S16** | `FAIL` — writing `h.seen_index` alone leaves `remote_session_seen_used` untouched | **STRENGTHENED** so the mutant actually RESERVES the row, which is the defect (control admission consuming an execute seen row). |
| `radmin5session` **S03** | `FAIL` — the source half of the retry comparison is unreachable over the wire (the source is AEAD-bound) | **THE TEST GAINED THE MISSING ARM**: `§radmin-5/C3` now rewrites a stored row's `source_hash` by an explicit, clearly-labelled SYNTHETIC fixture and requires `reject_id_reuse`. The comparison is defence in depth and is now proved as such. |
| `radmin5session` **S21** | `FAIL` — `§radmin-5/I4`'s root-change plan replaced every epoch, so the per-slot invalidation masked the root's own | **THE TEST GAINED THE MISSING ARM**: `I4` now also installs a pair change carrying NO `epoch_set` and NO `invalidate_all`, and requires the sessions to die anyway. |
| `radmin5runtime` **T09** | `FAIL` — the services never `commit()` after a refused prepare, so an armed-after-refusal plan was invisible | **THE TEST GAINED THE MISSING ARM**: `R8` and `R12` now call `commit()` explicitly after a refusal and require zero installs. |
| `sliceBmac` **M04** | `FAIL` | ⛔ **NOT CLOSED — and it was already worthless at the BASE commit.** See [[B342]] in §12: applied to a scratch copy of `1677b44`, native still ran 2763/118344/0. It cannot redden because the call passes `generic_owed = false` (so only the grant arm can fire), that arm is gated on `own_origination`, and the `dlen == 0` bail is reachable ONLY from a forward. Annotated in place with the measurement; ⛔ not deleted. |

### 9.1 ⚠ THE WARNING THE FIRST RUN CAUGHT — [[B169]]'s shape, exactly as the codebase documents it

The first full gate's census read:

```
env                   objs      warn    expect  -Wswitch        RAM      Flash  verdict
gateway_heltec         329       174       173         0     232300    1319332  FAIL: warnings 174 != pinned 173
gateway_heltec_v4      330       179       178         0     232572    1317316  FAIL: warnings 179 != pinned 178
heltec_mobile          329       177       177         0     207748    1367444  ok
heltec_v3              329       178       177         0     207508    1374512  FAIL: warnings 178 != pinned 177
heltec_v4              330       183       182         0     207780    1372532  FAIL: warnings 183 != pinned 182
heltec_v4_mobile       330       182       182         0     208020    1365424  ok
```

★ **THE PATTERN IS THE DIAGNOSIS**: all four ACCEPT envs +1, both CLIENT envs EXACTLY at their pins — so the
warning had to be inside a `MR_FEAT_RADMIN_ACCEPT` body. Reproduced with an isolated
`PLATFORMIO_BUILD_DIR` build of `gateway_heltec`:

```
lib/core/node_mac_rx.cpp: In member function 'void meshroute::Node::radmin_send_reply(meshroute::RemoteRxResult&)':
lib/core/node_mac_rx.cpp:2085:23: warning: unused variable 'rc' [-Wunused-variable]
```

`rc`'s only reader is the `MR_EMIT` beside it, and `MR_EMIT` is DEVICE-STRIPPED — the exact class the register
records as [[B169]]: *"an `MR_EMIT`-only local ⇒ `-Wunused-variable` on the board envs, invisible to native AND
the corpus"*. **Native was green, all 36 corpus streams were green, and every probe was green.** The warning
census was the only instrument that could see it, and it did.

**Fix:** `[[maybe_unused]] const CmdCode rc = originate_layer_path(...)` — the established remedy
(`node_mac.cpp`'s `generic_lifecycle`, `node.cpp`'s `dsp`), with the measurement recorded at the site.
⛔ The variable was NOT deleted: it is what the emit reports as `queued`, i.e. §6.6's "check its CmdCode".
**Proof it is byte-identical codegen:** the simulator rebuild after the fix recompiled exactly TWO objects
(`node_mac_rx.cpp` in both variants) and produced the SAME `lus` md5 `db6582a1…`.

⛔ **EVERY SOURCE-DEPENDENT GATE WAS RE-RUN ON THE FIXED TREE.** Nothing in this report is quoted from the
pre-fix run except the census rows above, which are labelled as such.
## 10. STOP audit — all six, evaluated explicitly

| # | condition | verdict |
| --- | --- | --- |
| **0** | ⚠ QA's own gate finding [[B341]] — live activation did not compose across a reboot | **FIRED, AND IT WAS FIXED BEFORE PASS (§0.2).** Not a coder-detected STOP: QA reproduced it. The fix makes each valid half install independently; the two reproduction sequences are now executed cases on BOTH instruments (`R16`/`R17`/`R18` and `S5-7`/`S5-8`), `R14`'s expectations were re-derived, and `radmin5runtime` T15/T16 pin the regression. |
| **1** | §1 placeholder · wrong/dirty measured base · missing prior closure · unreviewed concurrent input · out-of-fence path | ⚠ **PARTIALLY FIRED, AND IT IS REPORTED RATHER THAN ABSORBED.** The bases were verified and BOTH trees were clean at dispatch; no §1 placeholder remained (the pending "final dispatch base" was resolved by the dispatcher to `1677b44`, which `git rev-parse HEAD` confirms). ⛔ BUT three tracked Markdown files were modified in the MeshRoute tree DURING the session by something outside this slice (§0.1). They are **audited, out-of-fence for this slice, untouched, and read by no gate**; the only instrument that sees them is `git diff --check`, which is clean on them. ⛔ Nothing was reverted, repaired, committed or re-based. **QA/the dispatcher must rule on whether this invalidates the run**; the coder's position is that it cannot affect any figure in this report, and every figure names the file set it came from. |
| **2** | a new product capacity/ownership/physical authority or an ABI pin outside R-RA-31 · missing headroom · unexplained padding/flash/RAM · mobile Node or B278 pin moves | **NOT FIRED.** The ONLY pins moved are R-RA-31's two (native + gateway `Node`); `heltec_mobile`'s Node is unchanged and every other PIN_TABLE row and every B278 row reproduces. Every byte of RAM on both boards is attributed to a named symbol (§5), with the residual 8 bytes identified as measured inter-symbol padding. No new capacity was invented: N = 16 is R-RA-22's, the partitions are §4.3's. |
| **3** | authentication fallback/oracle · missing SOURCE_HASH · mutable source/route replacement · over-cap clamp · secret lifetime leak · failed-save live activation · epoch-zero readiness · ordinary regen rotating administration state | **NOT FIRED**, and each half is executed: no fallback (`G3` — a RESPONSE domain, a reserved opcode and a reserved slot are all silent refusals with no trial decode); `SOURCE_HASH` mandatory (`G1`, `N3c`, and the `X01`/`X14` mutations); the first-admitted source/route is preserved (`C3`, `E5`, `S22`); over-cap REFUSED not clamped (`G2`, `S08`); every key/plaintext transient wiped including the decoder-failure buffer (B313 — `E5` measures the released body is zeroed); a failed save installs nothing (`R7`, probe row `S5-4b`); epoch 0 never in service (`I2`, `S14`); `regen` preserves the administration root AND every epoch (probe row `S5-5`). |
| **4** | seen rows evicted by time/ACK/capacity · bootstrap consumes seen or changes epoch · partitions borrow · a raw counter misreported as send admission · a cross-layer request with no valid return shape | **NOT FIRED.** No TTL, no ACK release and no ring overwrite on a seen row (`E2`, `S04`, `S05`); bootstrap touches neither (`B1`, `N1`, `S10`, `S11`); no borrowing in either direction (`P1`, `B4`, `S15`, `S18`); the admission fact is `SendDispatch`, not the ctr (`N4`, [[B333]]); an unreversible cross-layer path is refused loudly with no same-layer substitute (`N6`, `X03`, `X06`). |
| **5** | any execute/terminal/output/ACK/rollover/protocol-error reply · a remote dispatcher/controller/custody consumer · a legacy ACCEPT fallback · a synthetic future-state arm called executed RF | **NOT FIRED.** The ONLY producer in the slice is the bootstrap response (`N9` measures that an authenticated EXECUTE airs nothing). No dispatcher, no controller state, no custody consumer. The legacy ACCEPT path is REMOVED, not fallen back to — proven at the linker (§5.1). The one synthetic arm (`C4`) is labelled in its own title, in the file header and in §7.3. |
| **6** | a new corpus event/stream delta · wrong simulator source/variant · an extra timer id · an unconditional epoch draw at unprovisioned init · a failed gate · a mutation survivor or unusable worker · a lost row/pin/control · missing restoration/evidence | **NOT FIRED.** 36/36 byte-identical, zero remote events; both variants compile `remote_session.cpp` and `MESHROUTE_DIR` names the measured checkout; EXACTLY one timer id (`N8` asserts 91 is the last allocated and 92 is refused); zero draws at unprovisioned init (`N10`); every battery restores with a matching md5. |

⚠ **AND TWO GATE FAILURES WERE FOUND AND FIXED RATHER THAN EXPLAINED AWAY**, both by instruments doing exactly
what they exist for: the warning census caught an `MR_EMIT`-only local invisible to native and the corpus
(§9.1), and the tools sweep caught two wrapper pins that had been duplicated instead of derived (§9.2). One
finding is reported UNFIXED with its proof — `sliceBmac`/M04, [[B342]] — because it was already worthless at the
base commit and its site is unreachable-by-construction from an origination.

⛔ **NO STOP WAS WIDENED AND NOTHING WAS FORCED GREEN.** The one condition that fired is reported above in full,
with the exact file set and the exact reason it cannot move a number, and the decision is left to QA.

---

---

## 11. Exact modified / untracked lists (this slice's, ⛔ excluding the audited concurrent input)

### MeshRoute — MODIFIED (29)
```
docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md
lib/core/node.cpp
lib/core/node.h
lib/core/node_mac.cpp
lib/core/node_mac_rx.cpp
lib/core/protocol_constants.h
lib/hal/timer_wheel.h
src/firmware_admin_acl.h
src/firmware_admin_identity.h
src/firmware_admin_verbs.h
src/firmware_commands.cpp
src/fw_main.cpp
test/radmin_0e_candidate_types.h
test/test_custody_receive_g.cpp
test/test_node_join.cpp
test/test_node_r3.cpp
test/test_radmin_characterization_0e.cpp
test/test_timer_wheel.cpp
tools/probe_board_abi.py
tools/probe_console_sink/negctl.py
tools/probe_console_sink/run.sh
tools/probe_console_sink/structural.py
tools/probe_features/ownership.py
tools/probe_features/run.sh
tools/probe_inbox_verbs/probe_main.cpp
tools/probe_inbox_verbs/run.sh
tools/probe_ui_model_mutations.py
tools/test_probe_console_sink.py
tools/test_probe_features.py
```
⚠ `docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md` is the generator's own output
(`gen_command_inventory.py --write`) and is **semantically identical**: 204 rows, the same verbs, sub-verbs,
owning functions, transports and feature gates. Stripping every `file:line` NUMBER makes the old and new files
byte-identical — the whole diff is line numbers moved by this slice's +49 lines in `firmware_commands.cpp`.

### MeshRoute — UNTRACKED / NEW (7)
```
docs/superpowers/evidence/2026-09-07-radmin-slice5.md
lib/core/remote_session.cpp
lib/core/remote_session.h
src/firmware_admin_runtime.h
test/test_firmware_admin_runtime.cpp
test/test_node_remote_session.cpp
test/test_remote_session.cpp
```

### ⛔ NOT THIS SLICE'S — the audited concurrent input (§0.1), untouched
```
MEMORY.md
tracker.md
docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md
docs/2026-07-30-open-bug-register.md                                            (the MAINTAINED register)
docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md                    (R-RA-32 / R-RA-33 landed)
docs/superpowers/plans/2026-09-06-radmin-authority-classification-proposal.md   (the ruled classification)
```
⛔ **THE LAST THREE ARE REMOTE-ADMIN DOCUMENTS AND THE CODER MAY NOT EDIT ANY OF THEM.** They appeared in the
measured tree at 08:21 and 08:28 (this slice's own edits bracket them on both sides), carrying the Author's
[[B341]] row, the owner's R-RA-32/R-RA-33 rulings and the ruled authority table. They are read-only inputs
here: §12's proposals were re-checked against the register's updated "next free" line and start at **B336**,
which that line reserves for exactly this slice. ⛔ Nothing in them was changed, and ⛔ [[B341]] is QA's row —
the fold-in is evidenced in §0.2, not registered by me.

### Simulator — MODIFIED (1)
```
CMakeLists.txt
```

⛔ Every path above is inside the brief §5 fence, with two exceptions BOTH stated here rather than buried:
* `tools/probe_console_sink/{structural.py,negctl.py,run.sh}` — the fence permits "Console-sink/ownership
  wrappers: rederive pins only where source anchors or the new runtime error/boot proof requires". S34's blanket
  *"the ACCEPT bindings touch NO Node state"* is precisely a pin the ruled live-install seam invalidates, and its
  control STAYED GREEN until re-pointed. The edits narrow S34, add S51/S52 and re-point/add three controls.
  ⛔ No help, transport or profile change.
* `docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md` — the fence says "Generator `--write` is
  coder-owned when command anchors move". They moved (line numbers only).
## 12. Proposed register rows (⛔ the coder proposes; it does NOT edit the register)

Numbering starts at **B336**, the next free number recorded in the brief §4.7 and re-checked against
`docs/2026-07-30-open-bug-register.md` at implementation time.

### [[B336]] — a concurrent, out-of-fence edit landed in the measured checkout mid-slice

**Measurement.** Both trees were `git status --short` EMPTY at the dispatch base `1677b44`. During the Slice 5
implementation, three tracked Markdown files were modified by something outside this slice (mtimes 06:48:47 /
06:49:15): `MEMORY.md`, `tracker.md` and
`docs/superpowers/specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md` — all belonging to the
**standalone mobile Home redesign** arc, not to remote-admin.
**Impact, audited:** no gate reads any of the three (`ownership.py` and the census tools scan `lib/ src/ test/`;
the inventory generator scans sources). The only instrument whose scope includes them is `git diff --check`,
which is clean on them. ⇒ no figure in this report can be affected.
**⛔ Not resolved here.** Nothing was reverted, stashed or committed. It is a PROCESS finding: the measured
checkout is supposed to be exclusively the dispatched slice's for the duration of a measured run, and it was not.
**Proposed disposition:** the owner/dispatcher rules whether concurrent doc work in the measured tree is
acceptable; if it is, the board-measurement runner's source guard (which hashes untracked files too, §B336a
below) needs a documented exclusion, because it currently turns any such edit into a hard measurement failure.

### [[B337]] — `measure_board.py`'s source guard hashes UNTRACKED files, so writing the slice's own evidence file mid-measurement fails the run

**Measurement.** `tools/measure_board.py`'s `source_snapshot` hashes the file set including untracked paths and
`git status --porcelain --untracked-files=all`. Appending to
`docs/superpowers/evidence/2026-09-07-radmin-slice5.md` (an untracked, documentation-only file that no board build
reads) while `pair` was measuring produced
`ERROR: source tree changed during measurement; mutation batteries are exclusive` — **twice** in this session.
**Why it matters:** the guard's stated purpose is mutation exclusivity, i.e. *production source* stability. A
documentation file cannot change a firmware image, so the refusal is a FALSE POSITIVE that costs a full re-run
and, worse, invites the wrong lesson ("do not write evidence while measuring") instead of the right one.
**⛔ NOT FIXED HERE** — `tools/measure_board.py` is out of this slice's fence. Proposed remedy: scope the
snapshot to the paths a board build can actually read (`lib/`, `src/`, `boards/`, `arduino_variants/`,
`platformio.ini`, `*.py` build scripts), or exclude `docs/` explicitly, with the exclusion stated in the
manifest so it is auditable.

### [[B338]] — the `radmin5rx` battery's first draft carried an UNMEASURABLE entry, and the harness proved it

**Measurement.** A drafted mutation ("the expiry scan takes a fresh `now` per class instead of one snapshot")
was applied at match count 1, compiled, ran — and the suite still PASSED. It was not a coverage gap: the mutant
is **behaviourally identical**, because `remote_session_expire` is pure and takes `now_ms` BY VALUE, so both
loops see the same value no matter how the caller obtains it.
**Disposition: CLOSED BY THIS SLICE.** The entry was replaced by one that attacks a decision that really exists
(the receive path stamping the deadline from the current time), and `remote_session.cpp`'s comment now records
that the single-snapshot property is guaranteed by construction rather than by a check. Registered because the
GENERAL lesson recurs: *a mutation whose mutant is semantically equivalent is worthless, and only running it
tells you which kind you wrote.*

### [[B339]] — three ownership/structural controls silently stopped proving anything when their rule legitimately changed

**Measurement.** `probe_console_sink`'s `S-C34` reported `!! STAYED GREEN -- S34 did not notice; this control
proves NOTHING`, and four `probe_ui_model_mutations` entries (`radmin3acl` B31/B33, `radmin3id` A03/A04) reported
`VACUOUS … match count 0`. All five had anchored on lines this slice legitimately moved.
**★ THE INSTRUMENTS CAUGHT ALL FIVE THEMSELVES** — none was scored as a pass, which is the designed behaviour
working. All five are re-pointed in place with their reason, and none was deleted.
**Disposition: CLOSED BY THIS SLICE** for these five. Registered because the SHAPE is standing: a control
anchored on a line, rather than on a property, decays the moment a later slice touches that line — and the only
reason it did not decay silently here is that both harnesses refuse to score a zero-match or a still-green edit.

### [[B340]] — the ACCEPT product's legacy `rcmd` execution is GONE from the image, and the bench's suspended row can now say so

**Measurement.** On `gateway`, five `.text` symbols present at base are ABSENT at final, collected once the
CLIENT guard removed their only caller: `mrfw::remote_exec`, `mrfw::remote_seal_resp`,
`meshroute::admin_cmd_open`, `meshroute::admin_cmd_verify`, `meshroute::Node::take_remote_inbound`; and two
`.bss` statics (245 B + 241 B) plus one 2-byte counter went with them.
**Disposition: INFORMATIONAL, for the Author's §9 landing.** The bench's suspended static/gateway `rcmd`
round-trip note can be updated from "suspended" to "receive REMOVED in Slice 5" on measured evidence. ⛔ This
slice adds no bench part and makes no metal claim.

---


### [[B342]] — `sliceBmac` M04 is a control that **provably cannot redden**, and it was already so at the base commit

**Measurement, and it is a measurement rather than a reading:** the M04 mutation (delete
`terminal_carrier_outcome(...)` from `do_data_tx`'s pack-failed path, `lib/core/node_mac.cpp`) was applied to a
scratch copy of **the base commit `1677b44`** and the native suite still ran **2763 / 118344 / 0**. ⇒ ⛔ nothing
in Slice 5 broke it; the `1 unusable` this slice's union reports is a **pre-existing** condition that Slice 4's
union simply never ran (`sliceBmac` is not in its selector — brief §1.1 lists it: devicenv, cfgparse,
sliceDtoken, radmin4{key,targets,verbs}, radmin3{id,acl,verbs}, teamkeyring).

**Why it cannot redden, stated as the mechanism:** the call passes `generic_owed = false`, so the ONLY effect
`terminal_carrier_outcome` can have at this site is the `team_key_grant_failed` push — and that arm is gated on
`own_origination`. The `dlen == 0` bail above it is reachable **only from a FORWARD**: `node_mac.cpp`'s own §0h
note records that neither `enqueue_data` arm admits a zero-length inner any more, and `test_node_r3.cpp` §B20/B21
*proves* it by asserting `pack_failed == false` for every carrier shape across the whole body-length band. The one
case that does reach the line (`test_node_r3.cpp:7888`) builds a doomed forward, where `own_origination` is false
and BOTH arms are skipped by design. ⇒ the call is a deliberate no-op on every reachable path.

**Disposition: REGISTERED, ⛔ NOT DELETED and ⛔ NOT SILENTLY REPAIRED.** The entry is annotated in place with
this measurement so the next reader does not re-derive it, and it becomes measurable again the moment an
ORIGINATED grant can reach a pack failure. **Proposed remedy (⛔ not this slice's fence — `node_mac.cpp`'s
grant/[[B268]] arc owns it):** either give the pack-failed path an originated-grant fixture (which needs a way to
build an over-cap grant that survives `enqueue_data`'s own guard), or record at the call site that this arm is
unreachable-by-construction today and keep the call as the structural belt [[B268]] intended. ⛔ The battery's
`1 unusable` is reported here rather than argued away.

---

## 13. The required line

`PIN re-synced? YES — tools/probe_ui_model_mutations.py PIN_CASES, PIN_ASSERTS 2763, 118344 -> 2825, 119784.
Derivation: +62 test cases and +1440 assertions, all ADDITIVE. test_remote_session.cpp 32 cases (the pure
state, design §10's five classification cases, the two partitions, the read-only bootstrap, the shared expiry
and the layout/N assertions); test_node_remote_session.cpp 12 cases (the REAL RTS/DATA/post-ACK accept path,
the on-air bootstrap answer decoded out of the captured TX bytes, the cross-layer reversal at depths 2-4, the
zero-destination refusal, the deadline stamp and the ONE shared timer); test_firmware_admin_runtime.cpp 18
cases (the prepare/commit/discard ordering, the per-slot invalidation rule, the boot conjunction, and the
three [[B341]] fold-in cases R16/R17/R18 that prove live activation composes across a reboot).
32 + 12 + 18 = 62. ZERO cases were deleted: four legacy-ACCEPT cases in test_node_r3.cpp were superseded IN
PLACE, and five pins were updated in place (the two kCap gates, the 0e timer mirror, the custody sizeof(Node)
line and R14's re-derived draw/commit expectations), which move assertions rather than case counts. The PIN was
re-synced FOUR times during the slice as cases were added; only the final value is pinned, and both figures are
the BINARY's, read from ./.pio/build/native/program on the final tree — never from the pio wrapper, which
prints its usual false "0 test cases".`
