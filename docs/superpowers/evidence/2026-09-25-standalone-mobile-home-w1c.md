<!-- Author: Claude (Opus 5.5), W1c coder — implementation receipt for QA's independent full gate; the owner rules and commits -->
# Standalone Home W1c — D10: unnamed devices advertise no name — coder receipt

**Status: IMPLEMENTED, CODER GATE (§4.1, steps 1–11) GREEN, READY FOR INDEPENDENT QA (§4.2).** Nothing is committed,
staged, reset or cleaned. The simulator was only read. B447's unnamed half stays open until QA's gate passes.

- **Contract:** [W1c brief revision 2](../plans/2026-09-25-standalone-mobile-home-w1c-no-default-name.md), SHA-256
  `39bfd6f0025b37c65826ac71b3effffbf82df25b7fa05c405c79b957992dfce6`. It was verified first, and is unchanged at the
  end.
- **Authorization:** [QA brief review](2026-09-25-standalone-mobile-home-w1c-brief-review.md) (PASS with fold-ins).
- **Evidence:** [`2026-09-25-standalone-mobile-home-w1c/`](2026-09-25-standalone-mobile-home-w1c/), with a
  `SHA256SUMS` over every file.
  - Raw `raw/*.log` files are gitignored by repository policy (`.gitignore:54`). They exist locally and are hashed.
  - Every figure is repeated in a tracked `*.txt`/`*.json` receipt or in this report.

## 1. Preflight

| Check | Found |
| --- | --- |
| MeshRoute | `HEAD` `8360802904f7bd0023279d3da844453d61207ede`, `main` |
| Simulator | `6585649ea5a780f0542b2931853a667be56a5b2b`, `git status` empty at preflight and at the end |
| Executable inputs 11/11 | Each is clean against `HEAD`, and its disk hash = `git show 8360802:` hash = the brief's §1 hash. Line counts are equal (2635 / 4088 / 1915 / 1875 / 376 / 11674 / 4829 / 9193 / 2061 / 855 / 12379). |
| Preparation set 6/6 | design `0b1af908…`, register `01a29418…`, pre-check `f7d81ec0…`, pre-check `SHA256SUMS` `fcfa56b5…`, `tracker.md` `bcb46358…`, `MEMORY.md` `881b47db…`: all OK |
| Pre-check directory | `sha256sum -c`: **18/18 OK** |
| Explained addition | The review receipt `7be92c30…` and its `SHA256SUMS` `9138fdf0…` (7/7 OK) |

`git status` classification: the four `M` documents are the preparation set; the brief, the pre-check and the review
are untracked and pinned or explained. Nothing else was present. Receipt:
[`preflight.txt`](2026-09-25-standalone-mobile-home-w1c/preflight.txt).

**Source validation (P4).** Every §1 fact holds by symbol:
- `Node::effective_name`, its two `node.h` comments, and the three producers:
  - INTRO `need = 33 + nlen` against the 232-byte cap;
  - the `0x8B` answer = 34-byte base + `[nlen][name]`;
  - `pack_h` appends a name only when `want_pubkey && name_len`.
- The two `peer_key_set` guards. The refresh guard carries its own trailing comment, so it is a unique anchor.
- `peer_name_set(…, 0)` erases.
- The receivers: INTRO passes a **null** name at count 0; routes 2–5 pass a non-null pointer.
- The `whoami`, boot and `peername` comment sites, and the inventory `--check` (197 rows).
- The probe's X block. The fake `Print` renders `HEX` in **decimal**.
- The mutation registry.

I found no semantic disagreement.

## 2. Baselines (before the first edit)

- **Inbox-verbs probe** (unmodified, both arms): ACCEPT **1394 checks / 0 failed, 61 controls / 0 unusable**; CLIENT
  **477 / 0, 69 / 0**; BOTH ARMS PASS ([summary](2026-09-25-standalone-mobile-home-w1c/inbox-verbs-summary.txt)).
- **Boards:** `pair --jobs=1` into `.pio-measure/w1c/base-1`, then `base-2`, as one chained command.

  | env | RAM | flash | objects | payload SHA-256 |
  | --- | --- | --- | --- | --- |
  | `gateway` | 203,740 | 572,240 | 285 | `cd56846e…` |
  | `heltec_mobile` | 211,724 | 1,394,732 | 329 | `ae615748…` |

  Stock `compare base-1 base-2`: **PASS `gateway`, PASS `heltec_mobile`**.
- **Borrowed from the pre-check, as §4.1 directs:** the native, corpus and ABI baselines.

## 3. Diff (fenced files)

```
 lib/console/console_parse.cpp          |   2 +-
 lib/core/node.cpp                      |  19 +-
 lib/core/node.h                        |   4 +-
 src/firmware_commands.cpp              |   2 +-
 src/fw_main.cpp                        |   2 +-
 test/test_dual_layer.cpp               |  70 ++++++
 test/test_node_hashlocate.cpp          | 384 ++++++++++++++++++++++++++++++++-
 test/test_node_r3.cpp                  |  54 ++++-
 tools/probe_inbox_verbs/probe_main.cpp |  55 +++++
 tools/probe_inbox_verbs/run.sh         |  26 ++-
 tools/probe_ui_model_mutations.py      |  63 +++++-
 11 files changed, 647 insertions(+), 34 deletions(-)
```

**Line counts of the three comment-edited files:** `firmware_commands.cpp` **1915**, `fw_main.cpp` **1875**,
`console_parse.cpp` **376** — all equal to base. `node.h` stays at 4088. Each of the three edits rewrites one comment,
and the code before `//` is byte-identical. The tracked inventory `--check` passes with the inventory untouched.

**Production:** `Node::effective_name` is now `n = min(_name_len, cap)`, a copy of `n` bytes, then `return n`. It
writes nothing for 0, never terminates, and has no default synthesis. The comments state the D10 contract.

## 4. Figures (§4.1, all derived here)

Input hashes were recorded at the freeze ([`stability-start.txt`](2026-09-25-standalone-mobile-home-w1c/stability-start.txt)).

1. **Hygiene:** `git diff --check` exit 0. Line counts 1915 / 1875 / 376, equal to base.
2. **Native:** `pio test -e native` exit 0, then the binary: **2962 cases / 195904 assertions / 0 failed / 0 skipped**.
   The base was 2951 / 195777.
   - **RED first:** with the new tests and the production code unchanged, exactly the **13** new or strengthened
     cases failed (45 assertions).
   - **Derivation:**

     | Change | Cases | Assertions |
     | --- | --- | --- |
     | §1.3 accessor case replaced in place (4 → 19) | 0 | +15 |
     | hashlocate B1–B5 and C routes 2–5 (4+7+5+11+12+13+11·4) | +10 | +96 |
     | dual-layer INTRO route | +1 | +14 |
     | INTRO golden strengthened (14 → 16) | 0 | +2 |
     | **Total** | **+11** | **+127** |

   - **Measured wire bytes** (B tests, unnamed): INTRO prefix **33**, `0x8B` body **35**, WANT_PUBKEY H **40** (team
     **44**), home forward body **33** (inner **34** with the origin byte). The INTRO fits **199** = 232 − 33 bytes of
     body, and at 200 it sends plain with exactly one `intro_attach_too_large`. Each named control differs only by
     `[len][name]`.
3. **Simulator and corpus:**
   - A fresh stock `lus` was built: `cmake -S ../lora-universal-simulator -B <scratch>/w1c-gate/sim-build
     -DCMAKE_BUILD_TYPE=Release -DMESHROUTE_DIR=…` (exit 0), then target `lus` (exit 0). Its SHA-256 is `e304147d…`;
     it differs from W1's because `lib/core` changed.
   - `run_corpus.py --jobs 4 --require-anchors`: **36/36 PASS, 36/36 anchors**.
   - **Compared with the pre-check `corpus-manifest.json`: 36/36 identical on all 14 per-scenario fields** (MD5,
     SHA-256, bytes, events, exit, assertion and anchor fields, scenario hash).
   - s18: **269,517 events**, MD5 `32afbf11e43b4bf9d0bd470ad502ba0a`.
   - The simulator stays clean ([compare](2026-09-25-standalone-mobile-home-w1c/corpus-compare.txt)).
4. **ABI:**
   - `probe_board_abi.py` (full): **PASS, 290 checks, 9/9 controls RED, 0 unusable**. `Node` is
     **235208/8 · 122176/8 · 157304/8** (native / heltec_mobile / gateway), unchanged.
   - `probe_b278_row_abi.py`: **PASS, 42 measurements, 6/6 controls RED**
     ([summary](2026-09-25-standalone-mobile-home-w1c/abi-summary.txt)).
5. **Probes.** Every run exited 0 ([summary](2026-09-25-standalone-mobile-home-w1c/probes-summary.txt)).

   | Probe | Default (gate) | `--no-neg` |
   | --- | --- | --- |
   | inbox verbs, both arms | ACCEPT **1400 / 0, 63 / 0 unusable**; CLIENT **483 / 0, 71 / 0**; BOTH ARMS PASS | 1400 / 483, 0 failed, PROBE-ONLY |
   | inbox verbs, explicit `MR_PROBE_ARM=client` | **483 / 0, 71 / 0**, PASS | — |
   | console sink | PASS (720 checks, 152 controls, 0 unusable) | PROBE-ONLY |
   | firmware UI | PASS (l2 467, v3 902, BLE 467; 225 controls, 0 unusable) | 467/467 |
   | custody USB | PASS (27 / 10) | PROBE-ONLY |
   | BLE line | PASS (55 / 12) | PROBE-ONLY |
   | feature matrix | PASS (9 cells, 112 checks, 58 controls, 43 ownership controls) | PROBE-ONLY |
   | deferred actions | ACTION PROBE PASS (4 backend/power configurations b0-p1 · b1-p1 · b2-p1 · b2-p0: 150 / 151 / 158 / 158 checks, 39 transcripts each) | ACTION PROBE PASS |

   Every other probe passed at its existing pins; only the four derived inbox-verbs pins moved.
6. **Parser compatibility:** the six lines printed by X21/X23/X25 on both arms were fed to
   `tools/lab/parsers.py::parse_whoami` (unchanged) and returned `""`, `"Bench 1"` and the 32-byte name. `node_id`,
   `hash`, `leaf`, `gw`, `gwonly` and `mobile` are intact: 6 parsed, 0 bad
   ([receipt](2026-09-25-standalone-mobile-home-w1c/gate6-parser-compat.txt)). The hash is in the fake `Print`'s decimal
   form (F-1).
7. **Inventory and literals** ([receipt](2026-09-25-standalone-mobile-home-w1c/gate7-inventory-literals.txt)). All
   exited 0, and whitespace is clean in both repositories.

   | Check | Result |
   | --- | --- |
   | `gen_command_inventory.py --check` | PASS, 197 rows, tracked inventory untouched |
   | `check_command_authority.py` | PASS |
   | `check_command_authority.py --selftest` | 6/6 RED |
   | `check_a0_matrix.py` | PASS |
   | `check_a0_matrix.py --selftest` | 10/10 RED |
   | `check_data_type_literals.py` | PASS, 218 files |
   | `check_data_type_literals.py --selftest` | 4/4 RED |
8. **Tools discovery** (after step 7): **356 tests, OK, 0 skipped**.
9. **Warning census:** **PASS**. The six pinned OLED envs are at their pins (171 / 175 / 175 / 175 / 179 / 179), with
   `-Wswitch` 0 and no pin changed.
10. **Boards, final.** `final-1` and `final-2`, chained, with nothing between or during them. Stock
    `compare final-1 final-2`: **PASS `gateway`, PASS `heltec_mobile`**. Base-to-final attribution (`base-1` against
    `final-1`, field by field):

    | env | field | base | final | Δ |
    | --- | --- | --- | --- | --- |
    | gateway | `ram_bytes` | 203,740 | 203,740 | **0** |
    | | `flash_bytes` / `.text` | 572,240 / 571,252 | 571,936 / 570,948 | **−304** |
    | | `symbol_count` / `symbol_size_total` | 6,597 / 752,617 | 6,593 / 752,316 | −4 / −301 |
    | | payload bytes / SHA-256 | 1,609,655 / `cd56846e…` | 1,608,800 / `97deca60…` | −855 (the hex encoding) |
    | heltec_mobile | `ram_bytes` | 211,724 | 211,724 | **0** |
    | | `flash_bytes` | 1,394,732 | 1,394,592 | **−140** |
    | | `.flash.text` / `.flash.rodata` | 1,072,486 / 218,116 | 1,072,362 / 218,100 | −124 / −16 |
    | | `symbol_count` / `symbol_size_total` | 13,297 / 1,496,134 | 13,296 / 1,495,992 | −1 / −142 |
    | | payload bytes / SHA-256 | 1,395,376 / `ae615748…` | 1,395,232 / `38739c4a…` | −144 |

    **Attribution, symbol by symbol.**
    - **gateway:**
      - `effective_name` 492 → 210 (−282);
      - the 19-byte `pfx` literal is removed;
      - three ARM `$d` data markers go with the removed data;
      - a compiler switch table is renumbered `CSWTCH.251` → `.244` at the same 4 B.

      That gives −301 and −4 symbols, exactly the manifest.
    - **heltec_mobile:**
      - `effective_name` 170 → 39 (−131);
      - `pfx` (19 B) is removed from `.flash.rodata`;
      - six callers move by ±4 B each (`dispatch`, which inlines `handle_whoami`, plus `emit_hash_query`,
        `intro_attach_prefix` and `send_hash_bind_pubkey_response` in `node_hashlocate.cpp.o`, and `on_command`
        and `on_timer` in `node.cpp.o`).

      Their object sizes differ from their linked sizes (for example `dispatch` is 6,394 B in the object and 5,578 B
      linked), so these are Xtensa link-time placement effects; net +8. The total is −142, exactly the manifest.
    - **Compatibility:** all 48 `toolchain.*`, `fixed_identity.*`, `paths.*`, `concurrency.*`, `host.*`, `schema`
      and `environment` fields are identical. `source.*` and `normal_pio_metadata.sha256` differ as expected.

    **The prediction is met:** RAM unchanged on both envs; flash falls on both, from losing the synthesis code and its
    string. Receipts: [attribution](2026-09-25-standalone-mobile-home-w1c/attribution-base1-final1.txt),
    [gateway symbols](2026-09-25-standalone-mobile-home-w1c/attribution-gateway-symbols.diff),
    [mobile symbols](2026-09-25-standalone-mobile-home-w1c/attribution-heltec_mobile-symbols.diff),
    [mobile object vs ELF](2026-09-25-standalone-mobile-home-w1c/attribution-heltec_mobile-object-vs-elf.txt).
11. **Mutation union** (§6). Each target was run alone, in scratch trees, after the last board pair.
    - **9 batteries / 34 entries: 34 RED / 0 unusable / 0 vacuous.**
    - All 34 worker trees derived the clean baseline **2962 / 195904 / 0**, equal to the PIN. No stale-PIN banner
      printed; the only "stale" matches are two `radmin73node` entry labels.
    - Every run reports the real tree untouched
      ([summary](2026-09-25-standalone-mobile-home-w1c/mutation-union-summary.txt)).

**Input stability:** the inventory taken at the freeze and the one taken after step 11 are **identical**. They cover:
- the 11 fenced hashes and line counts;
- the brief;
- the 6 preparation-set checks and the pre-check's 18;
- the review's 7;
- the tracked inventory;
- `git status`;
- both HEADs and the simulator status.

([start](2026-09-25-standalone-mobile-home-w1c/stability-start.txt),
[end](2026-09-25-standalone-mobile-home-w1c/stability-end.txt)). No input changed during the chain.

## 5. Controls and battery entries

**Probe controls** (`router` kind, shared by both arms). Each substitution has its own exactly-one-match guard
(`s6_ctl`, `source match count 1`).

| Control | ACCEPT | CLIENT |
| --- | --- | --- |
| `W1c-C1` whoami restores a made-up default name when unnamed | **RED, 2: X21 X22** | **RED, 2: X21 X22** |
| `W1c-C2` whoami omits the name field when unnamed | **RED, 2: X21 X22** | **RED, 2: X21 X22** |

Both compile, neither crashes, and neither reddens an unrelated row. Every existing control stays RED. Two existing
controls, `C11` (the LineSink never ships) and `C20` (a router reply reported as buffered), now also redden X21–X26,
because each breaks every sink-byte check; their meaning is unchanged.

**Native battery entries.** The harness prints the count and the match count. The cases named in the last column come
from a scratch reproduction: each exact entry was applied to an rsync'd copy, and the native suite was run there
([`controls-repro/`](2026-09-25-standalone-mobile-home-w1c/controls-repro/)).

| Entry | Harness verdict | Cases it reddens (reproduced) |
| --- | --- | --- |
| `w1cname` W01 — the retired default restored | RED, **45** failed assertions, match 1 | all **13**: (A), B1–B5, C routes 1–5, the INTRO golden |
| `w1cname` W02 — a terminator reserved (`cap − 1` + NUL) | RED, **8**, match 1 | (A) only: the 32-byte, cap-7 and no-terminator checks |
| `w1cname` W03 — the empty path writes a NUL | RED, **2**, match 1 | (A) only: both writes-nothing checks |
| `w1cname` W04 — the capacity ignored | RED, **4**, match 1 | (A) only: cap 0 named, cap 7 |
| `w1cretain` R01 — refresh guard `name && name_len` → `name` | RED, **8**, match 1 | **C routes 2, 3, 4, 5** (the C2 name and push checks) |

W02–W04 cannot move a producer byte: every production caller passes `cap` 32, and no producer case uses a 32-byte name.

**Route 1 (INTRO) is NOT covered by `w1cretain`, and this is stated rather than claimed.** Its receiver hands
`peer_key_set` a null name at count 0, so no single-site mutation of the refresh guard can erase the name there.

## 6. Selectors

- **(a) Batteries whose configured source this slice changes** (`node.cpp`, `node.h`):

  | Battery | Entries |
  | --- | --- |
  | `radmin8node` | 1 |
  | `radmin73node` | 5 |
  | `teamgrant` | 4 |
  | `b159map` | 2 |
  | `sliceBnode` | 8 |
  | `sliceEnode` | 3 |
  | new `w1cname` | 4 |
  | **Total** | **7 batteries / 27 entries** |

  - A static census found all 23 existing anchors at match count 1, and none overlaps an edited line.
  - The src and console comment files have no `TARGET_SRC` mapping.
- **(b) Dependency batteries:**
  - `b161hash` (6). Its H05 anchors on the key-answer producer's `body[n] = nlen; n += 1u + nlen;`, which consumes
    `effective_name`'s count; H04 anchors on the `0x95` name tail.
  - The new `w1cretain` (1).
  - That is **2 / 7**.
- **Union: 9 batteries / 34 entries, all RED** (§4 step 11).

## 7. Pin lines

`PIN re-synced? YES — PIN_CASES, PIN_ASSERTS 2951, 195777 -> 2962, 195904: §1.3 accessor case replaced 4->19 assertions (+15, 0 cases), test_node_hashlocate W1c B1-B5 + C routes 2-5 +10 cases / +96 assertions, test_dual_layer W1c C route 1 +1 / +14, the §S2 INTRO golden strengthened 14->16 (+2) = +11 / +127, measured by the full native binary; every mutation worker derived 2962/195904/0; tools discovery 356 OK.`

`PIN re-synced? YES — tools/probe_inbox_verbs pins derived from execution: PIN_CHECKS_ACCEPT 1394 -> 1400 and PIN_CHECKS_CLIENT 477 -> 483 (+6 each: the shared X21..X26 whoami exact-line rows — unnamed / Bench 1 / 32-byte × TEXT and JSON — appended at the end of the X block); PIN_CONTROLS_ACCEPT 61 -> 63 and PIN_CONTROLS_CLIENT 69 -> 71 (+2 each: the shared W1c-C1 / W1c-C2 router controls). Each literal stays a bare NAME=<integer> under its derivation comment lines (D5); the runner then passed at all four pins; tools discovery 356 OK.`

## 8. Freeze inventory

| Fenced file | SHA-256 | Lines |
| --- | --- | ---: |
| `lib/core/node.cpp` | `62ba7b086a00b0cf515ecb713632830ae18dd4ac9359a1f955b5fbff68f21604` | 2626 |
| `lib/core/node.h` | `647ad11fda25d38504616ca86c70c474a8be1fc24541103a32827d735707a3fe` | 4088 |
| `src/firmware_commands.cpp` | `3eea954914b6b3d788dbe9c672972578da15144bd8f6944ae65d3367ef13893f` | 1915 |
| `src/fw_main.cpp` | `4b9b86afd3303cec913a343faa159d4305591d31cf185fe8e8dfc27b509f7766` | 1875 |
| `lib/console/console_parse.cpp` | `414d18a8f5ca1bb815698998a52caaf90f30edfe114625ebd7f344b5d6c49aed` | 376 |
| `test/test_node_r3.cpp` | `6a5199144921d0f3866f806e6a38435040d66314ed5ebd7c450d23be327b49b6` | 11712 |
| `test/test_node_hashlocate.cpp` | `18dee2d5cdcc6da5d751b9ea50761d88fc5f17fe987e530de6e04b375e43fc54` | 5211 |
| `test/test_dual_layer.cpp` | `6b0f34a867437ec9297bca794a21fd907343677a08f952cdbd283f1831742550` | 9263 |
| `tools/probe_inbox_verbs/probe_main.cpp` | `e6f6ef9f13431b7bbb2560dd4eeea51f84acb5526977ec0b4d9c8b55327bbab6` | 2116 |
| `tools/probe_inbox_verbs/run.sh` | `06d49b25e3126970ac30e9e279d844a433f3b2f1fd1c994ebad762f43fcf76c4` | 873 |
| `tools/probe_ui_model_mutations.py` | `336e4c13ebb09b7ca7b8a4b4db42b1beda1157af09ca2edf7406b67b17c9d400` | 12438 |

- **Authorized brief:** `39bfd6f0025b37c65826ac71b3effffbf82df25b7fa05c405c79b957992dfce6`, unchanged.
- **Base:** MeshRoute `HEAD` `8360802904f7bd0023279d3da844453d61207ede`, unchanged; nothing staged or committed.
- **Simulator:** `6585649ea5a780f0542b2931853a667be56a5b2b`, clean.
- **Preparation set:** all 6 unchanged, equal to the brief's §1 hashes. The pre-check's 18 and the review's 7 checksums
  verify.
- **Tracked inventory:** `docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md` `ca393c0d…`, unchanged
  against `HEAD`; `--check` PASS.
- **New paths:**
  - this report (its hash cannot be stated inside itself);
  - [`2026-09-25-standalone-mobile-home-w1c/`](2026-09-25-standalone-mobile-home-w1c/), with every file listed and
    hashed in its `SHA256SUMS`.
- **Stability:** the freeze and post-step-11 inventories are identical (§4). The report and evidence were written only
  after the chain and after `final-2`, so QA's board source snapshot will differ from mine by exactly these files.
- **Locations outside the tracked tree** (none of them is a durable backup):
  - raw measurements: `.pio-measure/w1c/{base-1,base-2,final-1,final-2}` (gitignored);
  - working logs and ledger: `.pio-measure/w1c/logs/`;
  - simulator build and corpus streams:
    `/tmp/claude-1001/-home-staszek-MeshRoute/7019fa4d-8324-4b18-b429-3c8de8a32587/scratchpad/w1c-gate/`
    (temporary; the manifest carries every stream's full MD5 and SHA-256).

## 9. Not run, and why

- **`tools/probe_board_ui`:** B418 belongs to W2.
- **`tools/probe_device_radio`:** it reads `fw_main.cpp` and `firmware_commands.cpp`. The post-edit D6 audit
  ([`d6-reader-audit-post.txt`](2026-09-25-standalone-mobile-home-w1c/d6-reader-audit-post.txt)) confirms that its
  structural predicates and controls target only:
  - the radio provider binding;
  - the absence of FEM tokens (`GC1109`, `KCT8103`, `MR_RF_FEM_`);
  - `setFrequency` bypasses.

  None keys on `set_name`, `whoami`, `name=` or the edited comment text, and the new comments contain none of those
  tokens. No control is affected, so no rerun is needed.
- **`tools/probe_prov_tx`:** it reads only the unfenced provisioning service and configuration files.
- **Remote-admin codec references:** W1c changes no codec, vector or remote-admin test.
- **Metal:** no new metal row (M2). Every W1c behaviour is reached by the native suite, the inbox-verbs probe and the
  corpus.
- **Coder-side native, corpus and ABI baselines:** borrowed from the pre-check, as §4.1 directs.

## Findings for QA and the register (M1; the register is fenced, so none is edited here)

- **F-1 (out of fence): `tools/probe_board_ui/fakes/Arduino.h:90–91` now reads falsely.** It says "Nothing this probe
  asserts reads a based number". X21–X26 now assert a `hash=0x<digits>` built with the fake's **decimal** rendering of
  `HEX`, and this is disclosed at the rows. A future fake fix would redden them loudly, not silently. The file belongs
  to another probe and is outside §3.
- **F-2 (disposition): `tools/probe_inbox_verbs/run.sh` historical X-row table.** The pre-Slice-6 table still ends at
  "X20 … = 20". X21–X26 are named in the pin-derivation lines above `PIN_CHECKS_*`, which is the one-line convention
  since Slice 6. QA to rule on whether that satisfies §2.6's "row-family comment table names the new rows".
- **F-3 (observation, pre-existing):** `tools/probe_firmware_ui/run.sh --no-neg` ends with a plain `PASS`, while its
  siblings print `PROBE-ONLY — NOT A GATE`.

## Implementation record

- **TDD:** tests first. The new cases failed 13/45 against the unchanged production code; after the one-function change,
  2962 / 195904 / 0. The probe rows, controls and entries each had their RED confirmed as reported above.
- **Fresh read-only review** (whole diff): no Critical or Important findings. Before the freeze I fixed four Minor items,
  all inside the fence:

  | # | Fix |
  | --- | --- |
  | 1 | The battery header claimed producer coverage for W02–W04; it now states what was measured (accessor only). |
  | 3 | V1: "counted **wire** fields" became "counted fields", because `whoami` writes a counted console field; line counts kept. |
  | 4 | The section comment now says B2 has no named control. |
  | 6 | The golden no longer reads `body[32]` before its size guard; the assertion count is unchanged. |

  Its #2 and #5 are F-1 and F-2 above.
- **Rulings** (each with its cost if wrong; full list in [`coder-ledger.md`](2026-09-25-standalone-mobile-home-w1c/coder-ledger.md)):

  | Ruling | Reason | Cost if wrong |
  | --- | --- | --- |
  | Every (B)/(C) input is produced by a **real** producer inside the test; route 1's INTRO comes from a real unnamed sender | The strongest reading of "the exact zero-name bytes proven in (B)" | None |
  | X21–X26 expect the fake's decimal hash | They are built from `g_node`'s accessors exactly as production reaches the fake | A fake fix reddens them loudly |
  | B4-team uses `reqpubkey -t 228` (the §id-hash S1 TEAM fixture) and asserts byte 7 exactly (`HARD|WANT_PUBKEY|TEAM|MOBILE_REQ`) | Stricter than "TEAM set" | None |
  | The probe rows compare whole lines with CRLF | Exact-line contract | None |
