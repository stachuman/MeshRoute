<!-- Production coder: Codex. QA/Author owns maintained-document landings and the independent verdict. -->
# Slice 7b-3-0 — action_busy codec implementation and coder verification

**Implementation and full coder verification COMPLETE — handoff bound by the final input manifest in §7.**
Executed on 2026-09-14: native **2912/184461/0**, reference **94/94**, corpus **36/36 byte-identical**,
union **816 RED /one known unusable B342**, all remaining required instruments complete.
No behavior implementation, owner allocation, independent QA verdict, metal result or commit is claimed.

## 1. Inputs, source-validation and fence

MeshRoute remains at **`b9d75aaca55a0e350d9707512e9b6eec8a81ab22`** (`7b-3 prep`). Simulator remains clean at
**`06746a97de5764415d6fcef10b97bca90569b9c7`**, `/home/staszek/lora-universal-simulator`.
The revision-1 codec brief is `docs/superpowers/plans/2026-09-13-radmin-slice7b3-0-action-busy-codec.md`.
Its SHA-256 is **`68c188736515a8b7bd9910a7220db00543225798f1534cc4be34ef56e791879a`**.
The executed comparison of `f993191..b9d75aa` over `lib`, `src`, `test`, `tools`, `simulation` and
`platformio.ini` is empty. Every named source boundary was re-read before editing; no new fold-in was needed.

The starting tree deliberately included QA's three modified files (register, MEMORY and design), the new
codec brief/pre-check and its evidence directory. The complete **1,352 tracked/untracked inputs**, including
bytes, modes and symlink spellings, were captured before the implementation. Builds use complete isolated
copies of that state, not a clean HEAD archive. The final source snapshot includes the new reference too.
QA's prepared and historical documents are preserved. All original inputs except the following four are
unchanged; no build or mutation ran in the shared checkout:

- Production: `lib/core/remote_codec.h`, `lib/core/remote_codec.cpp`.
- Tests: `test/test_remote_codec.cpp`.
- Instrument: `tools/probe_ui_model_mutations.py` (codec controls/PIN and separately attributed B391 X09).

Raw coder artifacts are under **`/tmp/mr-codex-s7b30-C40uYn`**. `base-inputs.json`, `gate-inputs.json`,
`mutations-inputs.json`, `measure-inputs.json` and `measure-final-inputs.json` inventory the actual builds.
Each command group records argv, cwd, exit, duration and log SHA-256 in its `*-results.json`; its before/after
inventories check restoration. `MR_LUS_SRC` explicitly names the real simulator in every driver environment.
The sibling simulator symlink needed by scenario inputs was supplied outside the copied repositories;
repository symlinks were not rewritten.

Source-validation confirmed that the shared decoder's terminal ceiling is the sole production admission
change. Its encoder has no result-byte ceiling and is left untouched. The terminal enum has a uint8_t
underlying type; its static ceiling assertion remains. Authenticated PROTOCOL_ERROR has its own 00-only
predicate. Consumer inspection found no terminal-name table, external ceiling pin or exhaustive terminal
switch needing a wider fence. Existing session/executor mappings remain unchanged. The authenticated 08
literal is reused; the separately named protocol-error 08 negative stays. D6's source-reader audit identified
R60's changing comment and the already-registered X09 defect, both repaired with unique code-based patterns.

## 2. Implemented boundary

`RemoteTerminal::action_busy = 0x08` is appended; previous 00..07 values and all layouts remain fixed.
The ceiling/static assertion now names 08/action_busy. Authenticated and open TERMINAL both recognize 08;
09..FF still return `bad_result_code`. Empty/reserved terminal bodies still encode and still fail decoding.
Authenticated PROTOCOL_ERROR remains 00-only; open PROTOCOL_ERROR remains untyped; ADMISSION_RESULT's
five-code domain and exhaustive tuple negatives are unchanged. No encoder validation was added.

The decoded result publishes only on full success. Bad tags leave plaintext untouched; valid-tag semantic
failure can expose decrypted bytes without publishing a decoded result, preserving B383/caller-owned wiping.
The new tests distinguish successful `08 d1` from terminal `09 d1` and protocol-error `01 d1`/`08 d1` failures.
The one-byte busy result and opaque detail use the existing header, nonce, AAD and body-cap rules.
Alternate terminal fixture plaintexts intentionally share the existing nonce: this is a synthetic decoder
test, not permission for a runtime producer to replace an immutable transcript.

No Node/session/state/counter/scheduler/authority/NV/transport/packer/ABI pin or simulator source changed.
No action_busy producer is claimed. Touched comments now describe the live admission consumers and the new
terminal namespace without calling the separate protocol error a ninth terminal.

## 3. Native arithmetic and independent reference

**PIN re-synced? YES — 2909 cases /174485 assertions +3 cases /9976 assertions = 2912/184461/0.**

Fresh base and candidate builds both ran `pio test -e native` and then the actual native binary, not just
the wrapper. Base: **2909/174485/0, zero skipped**. Candidate: **2912/184461/0, zero skipped**.
Filtered per-case XML is attribution only; the whole suite, including B364's known fixture, was executed.

| Codec contribution | Cases | Assertions |
| --- | ---: | ---: |
| Existing codec before | 30 | 48489 |
| Existing allocated-terminal matrix: add 08 | 0 | +6 |
| New append-only/public-wire/detail/slot case | +1 | +372 |
| New exhaustive four-domain byte case | +1 | +9153 |
| New failure-publication/caller-buffer case | +1 | +445 |
| Codec after | 33 | 58465 |
| Outside codec, unchanged | 2879 | 125996 |

The old case name `all eight terminal meanings` becomes `all allocated terminal meanings`; the XML audit
normalizes this one explicit rename, not missing cases. The runner's new PIN remains a bare assignment.

The new independent extension is `2026-09-13-radmin-slice7b3-0-reference.py` beside this receipt. It imports
the preserved 7b-2-0 reference, which extracts the original Slice-2 reference; no production crypto is imported.
CPython **3.11.2**, PyNaCl **1.6.2**, hashlib **_blake2**, existing disposable environment
`/home/staszek/mr-slice2-ref`; no dependency installation. Both RFC 7693 BLAKE2b and XChaCha draft primitive
anchors ran before emission. The original embedded reference SHA-256 remains
`6be9de2413db8dd9f3a4a0cb08549ab45326c9a972cc28f3af7e2109ab154924`.

Five additions, including complete header/nonce/AAD/body inputs, reproduce QA's pre-frozen boundary vectors
exactly, before consulting production output. The final strict comparison is **94/94**, with a separate
**89/89 old-name/byte identity check**. Changed/missing/extra/duplicate/mis-sized comparator controls all refuse.
A separately executed copy with the auth-code09 header byte changed 13→12 exits 1; the real test is unchanged.
These are reference-comparator controls, not compiled mutation RED counts. The old 89-only whole-file command
is superseded by this strict 94-array command, not relaxed to ignore extras.

```sh
/home/staszek/mr-slice2-ref/bin/python docs/superpowers/evidence/2026-09-13-radmin-slice7b3-0-reference.py --freeze-check --compare test/test_remote_codec.cpp --selftest
```

The independent pre-check's real-codec proof was relinked against fresh base and final native archives:
baseline **9781 checks**, final `--allocated` **9787 checks**, both exit 0. Baseline `--allocated` exits 1
as the deliberate stale-ceiling control. Final exhaustive accepted/refused totals are terminal auth **9/247**,
terminal open **9/247**, authenticated protocol error **1/255**, open protocol error **256/0**.
The reference confirms code09 and code08 terminal nonces are equal, as required by the unchanged preimage.

## 4. Simulator and board attribution

Both simulator builds are fresh, against the complete base/final source snapshots, with the same simulator
commit. Verbose compile output and `ninja -t compdb` independently identify the normal and gateway codec
objects. Only those two object hashes change. All **36 validated streams and current BASELINE anchors are
byte-identical**, including s18's **269517 events**, MD5 **`32afbf11e43b4bf9d0bd470ad502ba0a`**.
No manifest/anchor was rewritten. Canonical comparison exits 1 solely for `lus_sha256`; that refusal is retained,
and actual validated stream bytes are separately compared exactly.

Simulator executable SHA-256:

```text
base  862241173963b3c30a501849dea9e5e6184227575225aa8e263f0f5c48d1170a
final 07d68fc561f352ceea4b0dd32e7685035f777d3053096ecdb987e6a09136728e
```

Deterministic board pairs run **gateway then heltec_mobile**, sequentially, at one private checkout/path
and fixed identity. The final comparison overlays precisely the four fenced source/test/tool files onto
that complete initial snapshot, holding documentation bytes constant to avoid B337 metadata noise. Final
source bytes equal the full gate snapshot. Pristine ELFs/payloads/manifests/symbol listings/build logs remain
under `measure/.pio-measure/{base,final}/{gateway,heltec_mobile}`; attribution only reads them.

| Board | RAM before→after | Flash before→after | Objects before→after |
| --- | ---: | ---: | ---: |
| gateway ARM | 203956→203956 | 570588→570588 | 285→285 |
| heltec_mobile Xtensa | 207756→207756 | 1372992→1372992 | 329→329 |

All section sizes used for loadable flash, symbol counts, summed symbol sizes and the entire sized-symbol
listing are identical. Gateway `.text` has exactly **one changed byte**, address **0x85992**, 07→08:
`cmp r2, #7` becomes `cmp r2, #8` inside the unchanged-size **1174-byte remote_body_decode**. No other allocated
byte changes. Mobile has no allocated-section byte changes and no linked encode/decode symbol. Its ELF
debug-info contents change and `.debug_loc` shrinks 30 bytes; ELF file alignment makes total file size −24.
The mobile payload's 65 changed bytes are confined to its embedded ELF SHA-256 (offsets 176..207), XOR checksum
(1373599) and final SHA-256 (1373600..1373631); each is independently recomputed against the pristine image.
These metadata differences are not flash-size or behavior deltas. Both board warning multisets match base.

Both ABI probes passed: Node **230896/8 native, 117912/8 mobile, 157264/8 gateway**, unchanged;
default ABI **191 checks, 9/9 controls RED**, B278 **42 measurements, 6/6 controls RED**.
These are ABI measurements, not substitutes for the linked RAM results above.

## 5. Completed standing chain and mutation union

The full chain and union completed in separate complete frozen copies. The following results come from
the coder's executed logs, not inherited pins; independent QA must still rerun its own gate:

| Instrument | Executed result |
| --- | --- |
| Console-sink | 6 profiles; 720 checks; structural 83; BLE guard 905; ownership 6 +3 controls; 149 controls, 0 unusable |
| Inbox-verbs | ACCEPT 1363 checks +60 controls; CLIENT 384 checks +45 controls; both pass |
| Firmware-UI | l2 404 /v3 839 checks; 223 controls, 0 unusable; coverage 703/840 as reported, not full per-assertion mutation coverage |
| Custody-USB | 27 checks /10 controls, 0 unusable |
| BLE-line | 40 checks /8 controls, 0 unusable |
| Features | 9 cells /120 checks /59 controls; ownership 40 controls, unchanged census; 0 unusable |
| Tools unit discovery | 343 tests, OK, zero skips; own fresh measured gateway ELF provided |
| Inventory write /bare /check | All exit 0; 204 rows, byte-identical generated artifact |
| Authority checker | Table/header/inventory agree; 6/6 selftests RED |
| A0 /DataType literal checkers | Both exit 0 |
| Warning census | Six pinned OLED environments pass; zero switch warnings; table below |

All six diagnostic `--no-neg` runs also completed; they are not a replacement gate and firmware-UI's B350
wording remains disclosed. All 25 standing-chain commands exit 0 and its complete source inventory is restored.

| Census environment | Objects | Warnings (actual = pin) | RAM | Flash |
| --- | ---: | ---: | ---: | ---: |
| gateway_heltec | 329 | 173 | 239068 | 1335816 |
| gateway_heltec_v4 | 330 | 178 | 239340 | 1333812 |
| heltec_mobile | 329 | 177 | 207756 | 1372992 |
| heltec_v3 | 329 | 177 | 214284 | 1390048 |
| heltec_v4 | 330 | 182 | 214556 | 1388052 |
| heltec_v4_mobile | 330 | 182 | 208028 | 1371028 |

These six census outputs are not the deterministic pair's before/after size attribution; both instruments
ran separately as required. There is no all-thirteen-board or hardware gate claim.

Selectors are explicit: **S = {radmin2codec}**, the configured TARGET_SRC battery whose production source
changed; **H = all 52 named historical/dependency batteries** frozen by the brief, including the prior
49 plus remoteactivation, fwactivation and macwait. **S ∪ H =52 batteries**. The base has 815 patterns, with
X09 the sole zero-match defect; final has **817**, every pattern matching exactly once after repair and two
new codec controls. No historical battery was dropped or replaced by a prior result.

Completed codec battery: **105 RED /0 unusable** (103 retained +R67/R68). R67 changes only the decoder
predicate back to 07, compiles and fails 77 assertions. R68 misreports 08 as session_busy and fails 18 assertions.
R60 still removes the reserved-result refusal, fails 510 assertions, and its pattern is now code-based.
All three match once and execute a compiled binary. Complete per-worker baselines/restoration and
all final counts are retained in the union logs. Final union: **816 RED /one known unusable B342 /52 batteries**.
Every RED compiled and executed, failed at least one assertion and matched its source once. Each worker's
fresh clean baseline is 2912/184461/0; all source hashes are restored, all 56 configured target files remain
byte-identical to launch, and the complete mutation snapshot inventory is unchanged. The sliceBmac process
exits 1 solely for M04 (suite still passes); its other three controls are RED. Every other battery exits 0.
No lost worker, new unusable or vacuous result is accepted. The aggregate is 817 configured patterns, **not
817 RED**, and does not include reference-comparator, standing-probe or ABI controls.

| Battery | Executed RED | Unusable |
| --- | ---: | ---: |
| a0rx | 7 | 0 |
| b134ack | 2 | 0 |
| b134inbox | 5 | 0 |
| b134ram | 3 | 0 |
| b134store | 39 | 0 |
| b159mac | 2 | 0 |
| b159map | 2 | 0 |
| b159rx | 3 | 0 |
| b161hash | 6 | 0 |
| b161mac | 1 | 0 |
| b161rx | 8 | 0 |
| b20codec | 5 | 0 |
| b20mac | 11 | 0 |
| b251hash | 55 | 0 |
| b251rx | 19 | 0 |
| cmdauthority | 16 | 0 |
| consoleline | 12 | 0 |
| devicenv | 42 | 0 |
| fwactivation | 10 | 0 |
| grantadmit | 1 | 0 |
| grantpark | 3 | 0 |
| macwait | 10 | 0 |
| radmin2codec | 105 | 0 |
| radmin3acl | 36 | 0 |
| radmin3id | 23 | 0 |
| radmin3verbs | 28 | 0 |
| radmin5runtime | 16 | 0 |
| radmin5rx | 14 | 0 |
| radmin5session | 25 | 0 |
| radmin72rx | 15 | 0 |
| radmin72session | 36 | 0 |
| radmin7exec | 20 | 0 |
| radmin7rx | 13 | 0 |
| radmin7transcript | 20 | 0 |
| remoteactivation | 22 | 0 |
| sliceAinbox | 1 | 0 |
| sliceAjson | 1 | 0 |
| sliceBmac | 3 | 1 |
| sliceBnode | 8 | 0 |
| sliceBrx | 16 | 0 |
| sliceCinbox | 2 | 0 |
| sliceCpull | 2 | 0 |
| sliceDack | 1 | 0 |
| sliceDclear | 5 | 0 |
| sliceDstore | 3 | 0 |
| sliceDtoken | 2 | 0 |
| sliceEnode | 3 | 0 |
| sliceGinbox | 4 | 0 |
| sliceGjson | 16 | 0 |
| sliceGrx | 42 | 0 |
| teamgrant | 4 | 0 |
| teamkeyring | 68 | 0 |

## 6. B391, failed attempts and limits

B391 is delivered as a **separate narrow instrument repair**, with no `node_mac_rx.cpp` edit. X09 now matches
the unique arming call plus its code/gate boundary, not B388's retired comment. Before/after search and
replacement text/hashes and the unchanged source hash are in `preservation-checkpoint.json`. The final
full radmin5rx run is **14 RED /0 unusable**; X09 compiles and fails **one assertion, one match**. Both workers
start at 2912/184461/0 and restore source MD5 **13aab3b4d2b0d9e36b39d4201790d030**. Full tools discovery and
the complete union remain part of its handoff obligation; independent QA owns closure of B391.

Unsuccessful attempts are not hidden:

- The first new reference freeze command exited 1 with `KeyError: 'old_literal_sha256'`: the coder used the
  wrong JSON key name in the new evidence extension. Corrected to the existing `existing_array_sha256`, then
  frozen before production output; strict final comparisons and controls pass. Initial log/exit are retained
  as `reference-initial-freeze.log` / `reference-initial-results.json`.
- The first scratch attribution audit assumed CMake had emitted `compile_commands.json` and exited 1 after
  native arithmetic. It had not. The corrected audit reads the existing Ninja compilation graph and binds
  both codec commands to actual verbose fresh-build logs; no rebuild, source edit or inferred provenance.
- The final scratch aggregate audit initially rejected the word `VACUOUS` anywhere in a log. sliceGrx G25's
  historical description contains that word, but its current result is **RED /11 assertions /one match**.
  The audit exited 1 at its substring assertion. Corrected to parse every merged RED result/count and the
  sole allowed M04 FAIL row; all 52 logs pass. No harness, control, source or raw log was changed, and no
  failed audit was presented as a completed gate.
- The canonical corpus hash refusal, old-ceiling negative and corrupted-reference negative are expected
  refusals, separately labelled above; none is counted as a successful full-gate run.

**M1 draft for QA's maintained register — proposed B394 (next free observed):** the newly authored reference
extension initially addressed a nonexistent key in the frozen metadata file, causing a loud exit before any
literal emission. This is an evidence-instrument defect introduced and corrected in this slice, not a production
codec defect or changed reference bytes. Correction: use the real `existing_array_sha256` member; fresh freeze,
94-array comparison, explicit old-89 identity and all five comparator controls pass. The failed command/log
above and corrected script hash remain auditable. QA owns number assignment and independent closure; the
coder does not edit the maintained register. Scratch attribution/setup failures remain disclosed attempt
history rather than being misreported as production failures.

Known limits remain B312 real entropy qualification, B315 output-path validation, B337 docs-hash measurement
noise, B342 the unusable MAC control, B350 firmware-UI's misleading no-neg PASS wording, B359 the optional stale
ABI overlay and B364 the pre-existing invite-fixture over-read. No-neg output is diagnostic, never a controlled
gate substitute. Default ABI pins are used; the stale optional overlay is not silently repinned. The complete
native suite was not filtered to avoid B364. No existing limit is repaired or closed incidentally.

B390's runtime producer/lifecycle, B389's prepared allocation and B392's warning/metal/controller obligations
remain separate. The parallel typed-plan preflight has its own receipt, not behavior authorization. No new
bench part is owed by this codec-only allocation. No maintained QA document or historical receipt was edited.

## 7. Frozen handoff

The shared implementation is **uncommitted and frozen for independent QA**, not independently QA-passed.
`2026-09-13-radmin-slice7b3-0-coder/freeze.json` inventories the complete final tracked/untracked delivery,
including modes/symlink spellings, the actual base and simulator, and every new coder artifact. Only that
manifest excludes itself to avoid a self-hash. The four changed source/test/tool inputs equal the executed
gate snapshot; all 1348 other original inputs and the simulator are preserved. Original QA preparation remains
intact. Both repositories' whitespace checks pass; new text evidence is also checked directly.

The sibling `artifacts.tar.gz` preserves the coder's raw logs, command/exit records, full build-input
inventories, scripts, independent-proof results, selector audit, per-worker mutation outcomes, board manifests/
symbol listings/build logs and corpus manifests. `artifact-sha256.json` inventories each archive member;
`published-sha256.json` binds the archive and readable summaries. The full pristine board binaries and actual
36-stream runs remain at the raw workspace named in §1; their hashes/byte comparisons are retained, not
replaced by edited ELF metadata or rewritten manifests. The archived preservation checkpoint predates the
final evidence packaging; `freeze.json` is the final delivery authority.

The codec archive contains **178 individually hashed artifacts**, **3934419 compressed bytes**; SHA-256
**`25598cfdb31073e9e58b4b8e8c73348ee9609edb3e24851c16d663516ddbfaca`**.

QA independently verifies this full state, handles B391 and the proposed B394 landing, and issues PASS/HOLD.
The owner then separately commits the codec. The behavior brief is reissued only at that actual closure
hash; the parallel typed-plan receipt does not settle B389's prepared allocation or authorize behavior coding.
