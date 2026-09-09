<!-- Independent QA/Author: OpenAI Codex; production coder: separate Codex session -->
# Slice 7b-2-0 — independent QA gate — 2026-09-09

**SOFTWARE PASS — CODEC PREPARATION ONLY / UNCOMMITTED.** This is QA's independently executed gate of the complete frozen working state.
The coder's recommendation is not the evidence for this verdict. Production, tests, mutation instrument,
reference and coder report were preserved. Nothing was committed; the simulator remains unchanged.

## 1. Frozen input and scope

MeshRoute HEAD/base `1d4b3ad5a74c2f24121d8fbe28b8d6e4b04f8dfe`; simulator
`06746a97de5764415d6fcef10b97bca90569b9c7`, clean throughout. Consumed codec brief **revision 2** SHA-256
`16ee9214041bca71699c2c04fd1ea932f09b203af2b680b68176e4edeb282f1d`.
Frozen coder report SHA-256 `a4853967654404a175c7e3ef84dd4f7324de69f8ac2da146c417953897067db3`.
Its original source-validation sections remain historical; §§6–11 are the completed handoff.

QA root: `/tmp/mr-qa-s7b20-gate-qfpdn1ul`. The input manifest contains **933 entries: 929 regular files
and four symlinks**, including every tracked/untracked delivered input. It captures the existing dirty
QA documentation and the new implementation/reference, not just HEAD. Input bytes/modes/links were checked
around snapshot creation and again before the documentation landing. Independent clones `frozen`, `gate`,
`mutations` and `measure` isolated builds from the shared checkout. `frozen` was never built or mutated.
The `measure` baseline used HEAD production with the preserved current documentation; final attribution
replaced exactly the four fenced source/test/tool files with their frozen working bytes at the same paths.

| Frozen implementation input | SHA-256 |
| --- | --- |
| `lib/core/remote_codec.h` | `c090f2d577b0aa247cc0952c87f2c004da7464962862f2dc3bd92f3ee0d47cb2` |
| `lib/core/remote_codec.cpp` | `a972c2fceef24ac38cbc442c92d5926b4783648f04567bc7f0d4ba5c031c8820` |
| `test/test_remote_codec.cpp` | `b5c1837f7e9b436186071cee62a38e4431b43ba46c871543649c742ddc9b112b` |
| `tools/probe_ui_model_mutations.py` | `12d4e1d03404d4a3a31957dad5d89f418743c9b1259cf5ab7389a91ad4b7cc26` |
| `2026-09-09-radmin-slice7b2-0-reference.py` | `ca485257119e1fb01ede94d5679ec40ac52bad2dc8ee22faf643b1f709227d96` |

All five reported hashes matched independently. The production diff is limited to the two codec files;
there is no new producer, controller handling, session/open state, Node allocation, timer, primitive,
carrier, wire-version or simulator change. The final QA documentation does not change this frozen code set.

## 2. Source, independent reference and failure contracts

QA checked R-RA-36 against actual `remote_layout`, `header_bytes_of`, `admission_status`, `remote_nonce`,
`write_header`, `remote_body_encode` and `remote_body_decode`. Only response opcode 5 at actual slots 0..9
is newly admitted; command allocation is unchanged. The fixed authenticated/session-key envelope is
12 clear bytes + 16 tag bytes, zero application bytes, no sequence or epoch. One semantic authority checks
request/code/detail on encoding and after successful authentication on decoding. Request ctl/code/detail
enter the new nonce suffix and the exact-header AAD through the existing conversion path. Old domains
ignore the new fields. Decoded publication remains the single `out = d` at `remote_codec.cpp:655`.
The typed admission branch precedes old payload-result interpretation and never publishes an invalid tuple.

The executable reference was run with `/home/staszek/mr-slice2-ref/bin/python` (CPython 3.11.2,
PyNaCl 1.6.2, hashlib `_blake2`), with both external anchors before vector comparison. Original reference
source SHA-256 `6be9de2413db8dd9f3a4a0cb08549ab45326c9a972cc28f3af7e2109ab154924`.
QA reproduced **87/87 unchanged old literals**, all 45 old authenticated-domain nonce inequalities,
**25 positive records × 97 bytes**, and **1718 valid-tag invalid records × 28 bytes**. Strict comparison
matched **89/89 arrays**; changed/missing/extra/duplicate selftests all rejected. A separate private one-byte
corruption exited **1**, `MISMATCH kAdmissionRefValid at byte 0`; the real test source stayed untouched.
These are reference/comparator controls, separate from compiled native mutations.

**B384 independently reproduced and closed:** the initially unshifted `op | 3` reference expression placed
**two legal tuples** in the negative set (AUTH_EXECUTE/session_full and AUTH_EXECUTE/ingress_full).
QA reintroduced only that expression in a private reference copy, independently transcribed the ruled
permitted pairs, and verified every tag with libsodium. The final `(op << 4) | 3` set has **1718/1718 genuinely
invalid semantic tuples with valid tags**, and all 25 positives are legal. The control yields exactly two
mislabelled positives; no old vector or new positive changed. Positive blob SHA-256
`c050f8d7b7046f05e922dc9321cf62ea98b3c5bc23dbe8e7124cb06455c8f3f1`, corrected negative blob SHA-256
`e3ff4e3bc0d2441a46d193bb7325e90746bdd3ff4239cd3b2b25d3dd5836dd8e`.
This was a reference-generator defect corrected before handoff, not a production rejection bug.

**B383 final-codec reproduction:** QA extracted the complete coder §5 fixture (SHA-256
`155ac8f5c5fbb87cd2d040eaa3e5d83bb4c304f224a29de7f1f87967b459966c`) and linked it against QA's freshly
built final native archives. **24/24 checks pass:** valid-tag invalid TERMINAL `08 d1` / PROTOCOL_ERROR
`01 d1` leave decrypted bytes and return `bad_result_code`; bad-tag controls leave plaintext unchanged.
The fixture checks named decoded sentinels. Full logical output preservation for the new fixed domain
is covered by the new native tests, which compare every scalar, span identity/length and buffer canary,
without padding `memcmp`. The public comment now matches this distinction and caller-owned wiping.
B382's header/test introductions now recognize the existing transcript/bootstrap/session consumers.
B313's separate primitive-comment debt is not closed.

**B379 boundary corroboration:** QA extended the existing 116-check real-session reproducer with 18 codec
checks and ran **134/134** against final native archives. The shared-pool full → other-slot rotation →
same-key execution path still occurs. A labelled synthetic new full notice has a different nonce from the
real completed **sequence-zero** transcript; identical notices repeat byte-for-byte, and busy 2→1 changes
nonce and wire. This does not claim a real target ADMISSION_RESULT producer exists. B379 remains open for
that producer and its retry/control lifetimes in 7b-2.

## 3. Native, simulator and corpus

Both `pio test -e native` and the actual `./.pio/build/native/program` ran on baseline and final state:
**2883 cases / 127709 assertions / 0 failed / 0 skipped → 2888 / 172264 / 0 / 0**.
Filtered codec XML is attribution only (B364): **25 / 3934 → 30 / 48489**; all other cases remain
**2858 / 123775**. QA derived each changed case from its own XML:

| Codec case | Base assertions | Final assertions | Delta |
| --- | --- | --- | --- |
| §radmin-2/admission — authenticated invalid tuples refuse without publishing any logical field | 0 | 41232 | +41232 |
| §radmin-2/admission — changed notices separate nonces and old domains ignore new metadata | 0 | 529 | +529 |
| §radmin-2/admission — fixed zero application capacity shares the real carrier and packing authorities | 0 | 131 | +131 |
| §radmin-2/admission — independent complete wire, nonce, AAD, tags and typed metadata | 0 | 953 | +953 |
| §radmin-2/admission — malformed, authentication and buffer boundaries keep caller output intact | 0 | 1598 | +1598 |
| §radmin-2/layout — exhaustive (direction x 256 ctl) domain table | 1519 | 1569 | +50 |
| §radmin-2/layout — the frozen §8.11 overheads and the header field sets | 62 | 65 | +3 |
| §radmin-2/length — a fixed layout accepts neither a short prefix nor a trailing byte | 418 | 477 | +59 |

**PIN re-synced? YES — 2883 + 5 = 2888 cases; 127709 + 44555 = 172264 assertions.**

**B385 — discarded QA incremental-build attempt:** `copy2` preserved final input mtimes older than already
built baseline objects. The first incremental simulator invocation reported `ninja: no work to do` and
retained the baseline executable. Its apparent corpus comparison PASS compared baseline with baseline;
QA rejected it before the verdict. The corrective run used a new `sim-final-fresh` build directory,
explicit `-DMESHROUTE_DIR=.../measure`, Release/Ninja and verbose output: **70 actions, both codec variants
compiled**. Final source hashes matched `frozen`. Future snapshot overlays must verify actual recompilation
or use a fresh build directory; a source manifest alone cannot establish executable provenance.

The valid baseline and final builds each have 64 object files. Exactly four changed: `remote_codec.cpp.o`
and its existing `remote_session.cpp.o` caller in each namespace. The caller objects retain their size;
disassembly changes are initialization/member access for larger caller-owned records (native layout
14→15 bytes, message 56→64, decoded 120→128, body offset 80→88), with no resident-state change.
The other **60 objects are byte-identical**. Full simulator executable SHA-256:

- Base: `7ad152073f13a76192ff2185488326910d28a759c5c882b1cfa59b012b95e2b9`.
- Final: `3826d36ee5b07fdaf2eb4836bfcf81240cb4ace18151fce4e66fe693670e186c`.

Both accepted corpora ran all 36 scenarios with `--jobs 3 --require-anchors` and passed separate `--validate`
runs. QA compared every scenario manifest field and every actual paired stream byte: **36/36 identical**,
all anchor matches, zero assertion failures. Live `simulation/BASELINE.md` SHA-256
`71f140988e9d1a5bf1be49dd1feb8fc7ebb8e46c7a4b119e16db62f0df76962f`; s18 full MD5
**`32afbf11e43b4bf9d0bd470ad502ba0a`**, **269517 events**, unchanged.
The standard same-binary `--compare` exits **1**, refusing only `lus_sha256`. It is not reported as PASS;
validated actual-stream comparison supplies the base/final behavior proof, without altering either manifest.
The QA driver initially expected refusal exit 2; its assertion was corrected in the interpretation of the
recorded exit/output. No simulator, anchor or instrument was changed to accept the runs.

The final native build emitted eight existing diagnostic lines, each present in the ten-line baseline
warning set; the different rebuilt TU set is not treated as a clean-build warning-count gate.

## 4. ABI, boards and warnings

Both ABI instruments ran with controls: **191 checks, 9/9 RED**, and B278 **42 measurements, 6/6 RED**.
Node remains native **225920/8**, gateway **152288/8**, mobile **117912/8**.
Fresh same-path fixed-identity measurements used `tools/measure_board.py pair --jobs 1`, baseline and final,
each **gateway then heltec_mobile sequentially**, under `measure/.pio-measure/qa-{base,final}`.
Compiler commands/flags, fixed identity, manifests and original artifact hashes were retained.

| Board | RAM base → final | Flash base → final | Objects |
| --- | ---: | ---: | ---: |
| gateway | 198980 → 198980 | 562812 → 562748 (−64) | 285 → 285 |
| heltec_mobile | 207756 → 207756 | 1372992 → 1372992 | 329 → 329 |

QA's own read-only ELF analysis verified all 28 measurement files remained pristine. Gateway `.text` shrinks
64 bytes; seven codec symbol deltas sum to −66, plus two extra uncovered text bytes. Both final body functions
call the newly outlined layout part; the base body functions do not. No manual refactor was introduced.

| Symbol | Base bytes | Final bytes | Delta |
| --- | --- | --- | --- |
| write_header constprop | 402 | 460 | +58 |
| admission_status | 0 | 94 | +94 |
| remote_nonce part | 368 | 448 | +80 |
| remote_layout | 518 | 612 | +94 |
| remote_layout part | 0 | 584 | +584 |
| remote_body_decode | 1630 | 1174 | -456 |
| remote_body_encode | 1048 | 528 | -520 |

Changed `.data` words retain their named referent/offset or identical pooled-string bytes; RAM sections
keep their size. The 8-byte `.ARM.exidx` entry retains target `0x27250` / unwind value 1 while moving −64.
Mobile allocated sections and complete symbol inventory are byte-identical; no codec symbol is linked.
Only debug information changes. QA parsed both firmware images: differences are confined to embedded ELF
SHA-256 bytes 176..207, XOR checksum at 1373599 and the final 32-byte SHA trailer. Each embedded ELF digest,
all segment XORs and both image trailers verify. The payloads contain **63 different bytes**, all attributed.

Normalized complete board warning multisets are unchanged: gateway **18631 → 18631**, mobile **177 → 177**.
The gateway retains three existing `CustomLFS_QSPIFlash` reorder diagnostics; these are neither new nor
suppressed, and no Node reorder occurred. Mobile has none; both have zero switch warnings. Stripping terminal
colour escapes was necessary to compare warning content rather than presentation. Census results below
are its separate six clean builds and pinned counts, not inferred from these two measurements.

## 5. Full standing chain

| Instrument | Independent result |
| --- | --- |
| Console sink | 6 profiles; 720 checks; 82 structural; 905 BLE guard; ownership 6 + 3 controls; 146 RED |
| Inbox verbs | ACCEPT 771 checks / 50 controls; CLIENT 378 / 45; both real-TU arms pass |
| Firmware UI | 433 / 868 / 433 passed; 223 controls; 703/840 control coverage |
| Custody USB | 27 checks; 10 controls |
| BLE line | 40 checks; 8 controls |
| Features | 9 cells / 120 checks; 59 total controls, including 40 ownership controls |
| All six --no-neg runs | Positive pins pass; no negative-control gate claimed |
| Full tools discovery | 343 tests; zero skips; OK (619.016 s) |
| Inventory write/bare/check | 204 rows; unchanged SHA-256 99cf5bd9e283dcbd5ba702ccb2f7764873e36d69651b39ab26a24623fa06bea3 |
| Command authority | Agreement; 6/6 selftests RED |
| A0 matrix | 21 enum members and 6 special rows; PASS |
| DataType literal checker | 215 active source files; PASS |

All six probes ran both controlled default and `--no-neg`. The latter is diagnostic only (B350), not a
replacement control gate. Existing documented probe coverage limits remain visible. Full tools discovery
used QA's pristine independently measured real gateway ELF; no coder ELF and no hidden ELF-test skip.
Inventory write/bare/check reproduced the frozen file exactly. The authority check plus six selftests,
A0 matrix and DataType literal check ran independently. The warning census alone exercises its six derived
OLED environments, sequentially; this is the role protocol's explicit exception to the two-board pair.

| Environment | Objects | Warnings = pin | Switch warnings | RAM | Flash |
| --- | --- | --- | --- | --- | --- |
| gateway_heltec | 329 | 173 | 0 | 234092 | 1330440 |
| gateway_heltec_v4 | 330 | 178 | 0 | 234364 | 1328484 |
| heltec_mobile | 329 | 177 | 0 | 207756 | 1372992 |
| heltec_v3 | 329 | 177 | 0 | 209308 | 1384924 |
| heltec_v4 | 330 | 182 | 0 | 209580 | 1382856 |
| heltec_v4_mobile | 330 | 182 | 0 | 208028 | 1371028 |

## 6. Full mutation union

QA parsed the configured source/battery tables independently before running controls, and checked every
pattern occurs exactly once. Changed-source selector **A = {radmin2codec}**. Historical/dependency selector
**B = the 47-battery 7b-1 acceptance floor**, including existing session/transcript/RX, persistence, transport
and inbox/command dependencies. Intersection is `{radmin2codec}`; union is all 47. The companion selector
record lists names, configured counts and reasons; no old acceptance battery was dropped.

| Battery | Entries | Native RED | Unusable | Worker baselines | Restored source MD5 |
| --- | --- | --- | --- | --- | --- |
| a0rx | 7 | 7 | 0 | 4 | `2e7f9edaa9d48e072f3e061ac411f322` |
| b134ack | 2 | 2 | 0 | 2 | `21b1db7be690a55771a96490f003f769` |
| b134inbox | 5 | 5 | 0 | 4 | `a8088417e948aef2e5ffbe8cec312b27` |
| b134ram | 3 | 3 | 0 | 3 | `39644fd2d33466f12779c92431a3dc71` |
| b134store | 39 | 39 | 0 | 4 | `29884575bbf3b33bf2ca456de63b1d69` |
| b159mac | 2 | 2 | 0 | 2 | `677c65439817a380f3fc94d9b651341d` |
| b159map | 2 | 2 | 0 | 2 | `f1cc7ffeda0ba77db8dda82fc5356c25` |
| b159rx | 3 | 3 | 0 | 3 | `2e7f9edaa9d48e072f3e061ac411f322` |
| b161hash | 6 | 6 | 0 | 4 | `ffb88cb2cb558a1158d741e6bc9eb6e4` |
| b161mac | 1 | 1 | 0 | 1 | `677c65439817a380f3fc94d9b651341d` |
| b161rx | 8 | 8 | 0 | 4 | `2e7f9edaa9d48e072f3e061ac411f322` |
| b20codec | 5 | 5 | 0 | 4 | `70aff75d2bb570b9320bb471b7142e40` |
| b20mac | 11 | 11 | 0 | 4 | `677c65439817a380f3fc94d9b651341d` |
| b251hash | 55 | 55 | 0 | 4 | `ffb88cb2cb558a1158d741e6bc9eb6e4` |
| b251rx | 19 | 19 | 0 | 4 | `2e7f9edaa9d48e072f3e061ac411f322` |
| cmdauthority | 16 | 16 | 0 | 4 | `401ed4d149fc410899f51d98fac667c1` |
| consoleline | 12 | 12 | 0 | 4 | `906e1a837b8ca667d191e55ac5bdd848` |
| devicenv | 42 | 42 | 0 | 4 | `eaf395c5d9caea43c256eaf84fbb9e8b` |
| grantadmit | 1 | 1 | 0 | 1 | `677c65439817a380f3fc94d9b651341d` |
| grantpark | 3 | 3 | 0 | 3 | `ffb88cb2cb558a1158d741e6bc9eb6e4` |
| radmin2codec | 103 | 103 | 0 | 4 | `22e3cfc10c678418ece5ce2daea0607b` |
| radmin3acl | 36 | 36 | 0 | 4 | `9e9fc3a9f19be1a654e034b15e4ae9ce` |
| radmin3id | 23 | 23 | 0 | 4 | `e919adc3b622885c788e470b854ab742` |
| radmin3verbs | 28 | 28 | 0 | 4 | `a2720a19ea9fa8ee0608069dfc2b1780` |
| radmin5runtime | 16 | 16 | 0 | 4 | `2fc7c0e9d04c5235dc9e60bf05f2a66f` |
| radmin5rx | 14 | 14 | 0 | 4 | `2e7f9edaa9d48e072f3e061ac411f322` |
| radmin5session | 25 | 25 | 0 | 4 | `56aa83a751f3ec36d8cbdc616d98911c` |
| radmin7exec | 11 | 11 | 0 | 4 | `bc7b2b09749989229b1aaca9861ca4f4` |
| radmin7rx | 13 | 13 | 0 | 4 | `2e7f9edaa9d48e072f3e061ac411f322` |
| radmin7transcript | 20 | 20 | 0 | 4 | `56aa83a751f3ec36d8cbdc616d98911c` |
| sliceAinbox | 1 | 1 | 0 | 1 | `a8088417e948aef2e5ffbe8cec312b27` |
| sliceAjson | 1 | 1 | 0 | 1 | `45df30d5066440b1dc098b65e23e4fd8` |
| sliceBmac | 4 | 3 | 1 | 4 | `677c65439817a380f3fc94d9b651341d` |
| sliceBnode | 8 | 8 | 0 | 4 | `f1cc7ffeda0ba77db8dda82fc5356c25` |
| sliceBrx | 16 | 16 | 0 | 4 | `2e7f9edaa9d48e072f3e061ac411f322` |
| sliceCinbox | 2 | 2 | 0 | 2 | `6e60f13c2aec55cbffc5b48d24cc9d8d` |
| sliceCpull | 2 | 2 | 0 | 2 | `a8088417e948aef2e5ffbe8cec312b27` |
| sliceDack | 1 | 1 | 0 | 1 | `21b1db7be690a55771a96490f003f769` |
| sliceDclear | 5 | 5 | 0 | 4 | `a8088417e948aef2e5ffbe8cec312b27` |
| sliceDstore | 3 | 3 | 0 | 3 | `29884575bbf3b33bf2ca456de63b1d69` |
| sliceDtoken | 2 | 2 | 0 | 2 | `668d664775bed49047d60e9bceb1d702` |
| sliceEnode | 3 | 3 | 0 | 3 | `f1cc7ffeda0ba77db8dda82fc5356c25` |
| sliceGinbox | 4 | 4 | 0 | 4 | `a8088417e948aef2e5ffbe8cec312b27` |
| sliceGjson | 16 | 16 | 0 | 4 | `45df30d5066440b1dc098b65e23e4fd8` |
| sliceGrx | 42 | 42 | 0 | 4 | `2e7f9edaa9d48e072f3e061ac411f322` |
| teamgrant | 4 | 4 | 0 | 4 | `f1cc7ffeda0ba77db8dda82fc5356c25` |
| teamkeyring | 68 | 68 | 0 | 4 | `d5ec2e56c194d51c8d5c9f679232e380` |

**713 attempted = 712 assertion-failing native RED + one known unusable B342; all 156 worker baselines
were 2888 cases / 172264 assertions / 0 failed.** `sliceBmac` M04 returns exit 1 and prints
`the suite still PASSES; nothing measures this`. That control is not RED; no new unusable control occurred.

Each native RED compiled, executed and failed at least one assertion with match count 1. Compile failure
was never counted as native RED. QA checked every worker's independently executed baseline, every restored
worker MD5 and all 56 configured source SHA-256 values after every battery. The new codec battery is
**103 RED = 66 retained + 37 new**. The whole union is the prior **675 RED + 37 = 712 RED**, with the same
one unusable exception. Reference comparator controls and ABI/probe controls are separate totals.

## 7. Evidence, landing and limits

[Companion evidence and reproduction instructions](2026-09-09-radmin-slice7b2-0-qa/README.md),
[input inventory](2026-09-09-radmin-slice7b2-0-qa/input-manifest.json),
[all mutation outcomes](2026-09-09-radmin-slice7b2-0-qa/union-audit.json), and
[ELF attribution](2026-09-09-radmin-slice7b2-0-qa/artifact-audit.json).

The retained QA scripts, command/result records, input manifest, source hashes, corpus/native arithmetic,
ELF analysis and all 47 mutation summaries are in the companion evidence directory. Raw full build/probe/
mutation/disassembly logs and pristine binaries remain under the QA root named above; their hashes are
inventoried. Standalone B383/B379 proofs compile with `g++ -std=c++20 -DMESHROUTE_NATIVE -Ilib/core
-Ilib/monocypher`, then the final native `lib*/lib*.a` in `--start-group/--end-group`. The B384 audit uses
the same isolated PyNaCl interpreter and only the explicitly labelled private reference-expression control.

QA did not edit production, tests, the mutation instrument, the reference, the coder report or the simulator.
Both repository whitespace checks pass. The maintained landing closes B382/B383 and records B384/B385 in place. **B378 remains open for actual
storage/rate/linked RAM; B379 remains open for the target producer/lifecycle proofs.** R-RA-35/R-RA-36 are
unchanged. The codec preparation awaits its separate owner commit. QA reissues 7b-2 at that actual successor;
the revision-3 behavior brief remains HOLD until then. Deferred actions remain 7b-3, controller/metal round
trips remain 8b, and no metal-only behavior or bench part is added by this pure codec slice.

B312 entropy-provider qualification, B313 primitive-comment debt, B315 measurement-path limits, B342,
B350, B359 and B364 remain separate limitations. QA makes no on-air producer, controller or hardware claim.
