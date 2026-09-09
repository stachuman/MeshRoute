<!-- Production coder: Codex. QA/Author owns briefs, rulings and maintained-document landings. -->
# Slice 7b-2-0 — coder preflight history and implementation evidence — 2026-09-09

**Current receipt: revision-2 implementation and full coder verification COMPLETE — frozen for independent QA.**
Native **2888/172264/0**, corpus **36/36 byte-identical**, union **712 RED / one known unusable B342**,
all remaining required gates completed. See §6 onward. The revision-1 preflight below is preserved as history.
No independent QA verdict or owner commit is claimed; the behavior slice still waits for that separate closure.

**Preflight report: one comment-contract fold-in requested before implementation.**
R-RA-35/R-RA-36 are recorded and no additional owner ruling is requested. The new admission wire contract
matches those rulings and the existing codec seams. Proposed **B383** below is a pre-existing public API
comment mismatch, not a new wire-design defect. Per the brief's source-validation rule, it is returned to
QA/Author for an explicit comment-only fold-in; production implementation has not begun.

Only this coder evidence file was added. Production, tests, tools, simulator, the earlier coder receipt and
all QA-owned preparation documents were preserved. Nothing was committed.

## 1. Inputs

MeshRoute HEAD: `1d4b3ad5a74c2f24121d8fbe28b8d6e4b04f8dfe`.
Simulator: `/home/staszek/lora-universal-simulator`, clean at
`06746a97de5764415d6fcef10b97bca90569b9c7`.

Consumed brief: `docs/superpowers/plans/2026-09-09-radmin-slice7b2-0-admission-codec.md`, revision 1,
SHA-256 `0e9635c4a8457eab55ea7b722669257e678171953fdfd55eaa383df049261fc3`.
The shared tree is deliberately documentation-dirty, not a clean implementation snapshot.

All 13 expected modified/untracked preparation files were inventoried and hash-checked after the
diagnostics; all matched. The inventory is reproduced here (SHA-256, path):

```text
b152bdba8dca195d0b585ca4e93b7300fa34d3e5c00637b5fd95c90f420ecb68  docs/superpowers/evidence/2026-09-08-radmin-slice7b2-precheck/candidate-measurements.json
e80bbe7750a04bc71104276538779a5a9ba1e28a65165fc872635fec86e55f2c  docs/superpowers/evidence/2026-09-08-radmin-slice7b2-precheck/measure_candidate.py
5530c51610b753ed24071f9cef507e2bd75f85beb8e6032fe0a87b805f057dca  docs/superpowers/evidence/2026-09-08-radmin-slice7b2-precheck/nonce-proof.cpp
70dedbb9c508ea761d3c941261945117fd150ec189e78465babc51d409eb8261  docs/superpowers/evidence/2026-09-08-radmin-slice7b2-precheck/receipt.json
ee0f42aa690f0a84b7f2b8580402c85e9a0f34054afa67a0352ef573ca544ab6  docs/superpowers/evidence/2026-09-08-radmin-slice7b2.md
9dbfa4e57a4947f5b514087c459c4510296795b4088045196d8124b2872583dc  docs/superpowers/plans/2026-09-08-radmin-slice7b2-precheck.md
a7488f3595914997ef5f33e36afd1efb0bd10374591edc13e223d8d2f5291be8  docs/superpowers/plans/2026-09-08-radmin-slice7b2-session-open-status.md
0e9635c4a8457eab55ea7b722669257e678171953fdfd55eaa383df049261fc3  docs/superpowers/plans/2026-09-09-radmin-slice7b2-0-admission-codec.md
462b3b1a820641ebf483d92c9574728a0d1cd3d326b288dcabb4a63254c9878a  docs/superpowers/plans/2026-09-09-radmin-slice7b2-0-precheck.md
b89c4621a51179ba1a5945c83fad490b0afcefe60a49b732aa794ff9d2a4dc7b  MEMORY.md
0d5e1477214224239ec38f15b0bad6b0676afdebbfad33ef313567a6e9ef95e8  docs/2026-07-30-open-bug-register.md
3c336eb753ed38ce1e68d88024101224af6567628107cf86487ee1b3edfa3752  docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md
9404367c038093582cc89f144f941252b7e3e84836d371b1f480081ab65c773e  docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md
```

Raw diagnostic root: `/tmp/mr-codex-s7b20-preflight-FPUF3b`. Its `preparation-inputs.sha256` is
`287034f3fd87cea29dc6801228d7c9cd5949f73b014538094e811115fb0f654b`.
This is a preflight input inventory, not the eventual full implementation/QA freeze.

## 2. Executed observations

| Check | Fresh coder result | Scope |
| --- | --- | --- |
| `pio test -e native`, then `./.pio/build/native/program` | **2883 cases / 127709 assertions / 0 failed / 0 skipped** | Unmodified base; wrapper and actual binary both exit 0 |
| Original independent reference | Both external primitive anchors; **87/87 existing literals match**, 45 old authenticated-domain nonce pairs distinct | Source extracted directly from Slice-2 evidence §3.7; no new admission vector generated |
| Focused old-domain failure-output proof | **24 checks PASS**, two valid-tag/invalid-result cases plus bad-tag controls | Real unchanged codec, synthetic legal-key test inputs |
| Verbatim authority comparison | **5/5 brief block quotes found exactly** | Rulings, design, 7b-1 contract and roles source |
| Preparation preservation | **13/13 hashes unchanged** | Named existing dirty inputs, including prior coder receipt |

Reference runtime: Python 3.11.2, PyNaCl 1.6.2, hashlib implementation `_blake2`, from the existing disposable
`/home/staszek/mr-slice2-ref` environment. No dependency was installed. Native logs are `native-build.log`
and `native-program.log`; reference output is `old-reference.log`; the focused proof is
`failure-output-proof.cpp` / `failure-output-proof.log`.

No fresh corpus, board links, ABI gate, standing probe chain, tools sweep, warning census, mutation union or
new-domain KAT gate ran in this preflight. QA's reported 36/36 corpus result is not relabelled as this coder's
execution. The complete implementation gate remains required after the codec is written. No new PIN,
resident-state, linked RAM/flash, simulator-binary or on-air result is claimed.

## 3. Proposed B383 — the existing decoder's blanket failure-output comment is false

**Classification:** pre-existing API comment drift; scoped documentation correction requested. This is not
a new authenticated-execution flaw and does not ask to change old wire behavior.

**Anchors:** `lib/core/remote_codec.h:314–322`; `lib/core/remote_codec.cpp:554–590`;
codec brief §3's new-domain failure-publication contract and preservation of old behavior, plus §5's
comment fence, which currently names only the B382 introductions.

The public header promises that the codec writes nothing into `plaintext_out` on failure. The implementation
first decrypts a valid tag into the supplied buffer, then checks the old TERMINAL/PROTOCOL_ERROR result byte.
An invalid result therefore returns `bad_result_code` after the buffer has changed. The decoded output
object is not published: its assignment remains after those checks. In contrast, an invalid tag returns
`auth_failed` without writing the caller's plaintext buffer.

Fresh reproduction with two-byte payloads:

```text
opcode 1: valid tag / invalid result -> bad_result_code; plaintext 08 d1; decoded sentinel retained
opcode 4: valid tag / invalid result -> bad_result_code; plaintext 01 d1; decoded sentinel retained
PASS: 24 checks; bad-tag controls preserve the plaintext buffer
```

The new fixed ADMISSION_RESULT has zero application bytes, so it can satisfy the brief's stronger
all-failures preservation rule without changing any old decoding behavior. No production caller repair
or transactional-decode refactor is proposed.

**Requested QA fold-in:** explicitly include correction of the existing public decoder comment in the
already-fenced header. State separately: decoded result publication occurs only on full success; failed
authentication leaves plaintext untouched; old variable-body semantic failure after valid authentication
can leave decrypted bytes in the caller buffer; the caller owns wiping. Preserve old behavior and retain
the new domain's exact untouched-output tests. Do not broaden that new-domain promise into a whole-codec
guarantee or change old result acceptance rules.

This is related to, but does not close, B313: that register row concerns the separate primitive comment's
false claim of wiping on failed authentication. Here the codec header overstates preservation on failures
that happen **after successful authentication**. QA owns registration/number assignment; B383 was next free
at this source-validation.

## 4. Other source-validated conclusions

- Response opcode 5 is reserved today; the approved response allocation does not change command opcode 5.
  Header 12 / fixed overhead 28 fits the existing 41-byte header and 46-byte AAD maxima.
- The existing nonce scratch's eight-byte epoch allowance covers the new three-byte suffix. Old epoch
  domains must keep their preimages unchanged; the new suffix is domain-specific.
- `remote_layout`, `write_header`, `remote_nonce`, `remote_aad` and public encode/decode are the existing
  authorities to extend. Fixed session-key domains already authenticate empty plaintext.
- The new clear typed result must not enter the existing encrypted-payload result fallback. Code zero's
  three meanings, request-ctl pairing and independently authenticated invalid tuples need the new tests
  exactly as specified by the brief.
- `RemoteLayout`, `RemoteMessage` and `RemoteDecoded` are transient caller-owned records in the existing
  session callers; no resident Node allocation is required for this codec extension.
- Actual transcript/bootstrap/session callers exist. B382's stale no-consumer introductions are real;
  the gateway flash prediction must account for already-linked shared codec functions.
- The original independent reference uses an exact literal-set comparison, rejecting missing, changed and
  extra `kRef*` literals. Its preserved 87-vector check and the extended new-vector check must both remain
  honest; generating new expectations through production would not satisfy the brief.
- The separate target-wide rate/storage policy remains behavior-only. No open buffer, producer, timer,
  counter, epoch lifecycle, sender, Node ABI pin or simulator source change belongs in 7b-2-0.

## 5. Focused proof source and reproduction

The complete preflight fixture below links the real base codec; it is not a production or test-suite edit.
It checks the specifically named decoded sentinel fields, not a padding-dependent whole-object comparison.
The stronger complete logical-output comparison for the new domain remains the implementation brief's test
obligation.

Fixture SHA-256: `155ac8f5c5fbb87cd2d040eaa3e5d83bb4c304f224a29de7f1f87967b459966c`.

```cpp
// Preflight-only: real old codec behavior, no production repair.
#include "remote_codec.h"
#include "frame_codec.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
using namespace meshroute;
static unsigned checks;
static void require(bool ok) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL check %u\n", checks); std::exit(1); }
}
int main() {
    std::array<uint8_t, 32> key{}; key.fill(0x45);
    const RemoteKeys keys{{}, key};
    const RemoteSource source{true, 0x12345678};
    RemoteCarrier carrier{}; carrier.outer_data_type = DATA_TYPE_REMOTE_RESP;
    for (const auto opcode : {RemoteRespOpcode::terminal, RemoteRespOpcode::protocol_error}) {
        RemoteMessage msg{};
        msg.outer_type = DATA_TYPE_REMOTE_RESP;
        msg.opcode = static_cast<uint8_t>(opcode);
        msg.slot = 3; msg.request_id = 0x1234;
        const std::array<uint8_t, 2> invalid{
            static_cast<uint8_t>(opcode == RemoteRespOpcode::terminal ? 0x08 : 0x01), 0xD1};
        std::array<uint8_t, 64> wire{};
        size_t written = 999;
        require(remote_body_encode(wire, written, msg, invalid, keys, source, carrier) == RemoteStatus::ok);
        require(written == kRemoteOverheadAuthResponse + invalid.size());
        RemoteDecoded decoded{}; decoded.msg.request_id = 0xFEEDFACE;
        std::array<uint8_t, 4> plain{}; plain.fill(0xC7);
        require(remote_body_decode(decoded, msg.outer_type, {wire.data(), written},
                                   keys, source, carrier, plain) == RemoteStatus::bad_result_code);
        require(plain[0] == invalid[0]);
        require(plain[1] == invalid[1]);
        require(plain[2] == 0xC7 && plain[3] == 0xC7);
        require(decoded.msg.request_id == 0xFEEDFACE);
        require(!decoded.authenticated);
        std::printf("opcode %u: valid tag / invalid result -> bad_result_code; plaintext %02x %02x; decoded sentinel retained\n",
                    msg.opcode, plain[0], plain[1]);
        wire[written - 1] ^= 1;
        plain.fill(0xC7);
        require(remote_body_decode(decoded, msg.outer_type, {wire.data(), written},
                                   keys, source, carrier, plain) == RemoteStatus::auth_failed);
        require(std::all_of(plain.begin(), plain.end(), [](uint8_t b) { return b == 0xC7; }));
        require(decoded.msg.request_id == 0xFEEDFACE);
        require(!decoded.authenticated);
    }
    std::printf("PASS: %u checks; bad-tag controls preserve the plaintext buffer\n", checks);
}
```

After building native, save the fixture in a fresh temporary directory and compile with the native archive
paths reported by that build. The exact command executed here was:

```sh
g++ -std=c++20 -DMESHROUTE_NATIVE -Ilib/core -Ilib/monocypher /tmp/mr-codex-s7b20-preflight-FPUF3b/failure-output-proof.cpp -Wl,--start-group .pio/build/native/lib095/libhal.a .pio/build/native/lib1d5/libmonocypher.a .pio/build/native/lib6be/libcore.a .pio/build/native/libf08/libconsole.a -Wl,--end-group -o /tmp/mr-codex-s7b20-preflight-FPUF3b/failure-output-proof
/tmp/mr-codex-s7b20-preflight-FPUF3b/failure-output-proof
```

The unchanged independent Python source is the complete executable block in
`docs/superpowers/evidence/2026-09-06-radmin-slice2.md` §3.7. It was extracted to interpreter stdin and run
as `/home/staszek/mr-slice2-ref/bin/python - --quiet-vectors --compare test/test_remote_codec.cpp`.

**Next:** QA folds in/registers the public-comment correction, then coder resume validates the revised brief
and implements only the codec prerequisite. No new owner choice or separate code refactor is needed.

## 6. Revision-2 resume, scope and fresh attribution base

Consumed revision-2 brief SHA-256:
`16ee9214041bca71699c2c04fd1ea932f09b203af2b680b68176e4edeb282f1d`.
MeshRoute remains `1d4b3ad5a74c2f24121d8fbe28b8d6e4b04f8dfe`; the simulator remains clean at
`06746a97de5764415d6fcef10b97bca90569b9c7`. B383 changes only the inaccurate public comment;
old decoder ordering, post-authentication plaintext writes and caller-owned wiping are preserved.

Scratch/evidence root for this run: `/tmp/mr-codex-s7b20-sX2lBR` (called `$slice_run` below).
Before implementation, all 14 preparation inputs were hashed in `preparation-inputs.sha256`:
the prior 13 preparation files, now at their revision-2 content, plus this preflight report.
All were rechecked unchanged before appending this implementation receipt. QA-owned files are untouched.
The exact resume inventory (the evidence-file hash is its preserved preflight version, not a circular
hash of this subsequently appended report):

```text
b152bdba8dca195d0b585ca4e93b7300fa34d3e5c00637b5fd95c90f420ecb68  docs/superpowers/evidence/2026-09-08-radmin-slice7b2-precheck/candidate-measurements.json
e80bbe7750a04bc71104276538779a5a9ba1e28a65165fc872635fec86e55f2c  docs/superpowers/evidence/2026-09-08-radmin-slice7b2-precheck/measure_candidate.py
5530c51610b753ed24071f9cef507e2bd75f85beb8e6032fe0a87b805f057dca  docs/superpowers/evidence/2026-09-08-radmin-slice7b2-precheck/nonce-proof.cpp
70dedbb9c508ea761d3c941261945117fd150ec189e78465babc51d409eb8261  docs/superpowers/evidence/2026-09-08-radmin-slice7b2-precheck/receipt.json
ee0f42aa690f0a84b7f2b8580402c85e9a0f34054afa67a0352ef573ca544ab6  docs/superpowers/evidence/2026-09-08-radmin-slice7b2.md
dfdaf0409099bda8dcf28783a5cd90e5dc3fc3432f1c40104bbe32e74512c853  docs/superpowers/evidence/2026-09-09-radmin-slice7b2-0.md
9dbfa4e57a4947f5b514087c459c4510296795b4088045196d8124b2872583dc  docs/superpowers/plans/2026-09-08-radmin-slice7b2-precheck.md
a7488f3595914997ef5f33e36afd1efb0bd10374591edc13e223d8d2f5291be8  docs/superpowers/plans/2026-09-08-radmin-slice7b2-session-open-status.md
16ee9214041bca71699c2c04fd1ea932f09b203af2b680b68176e4edeb282f1d  docs/superpowers/plans/2026-09-09-radmin-slice7b2-0-admission-codec.md
e2fd76a773a310d5c89dcfbe20b2efaa205a2cf950bab1d9c6d08736d81dc1ab  docs/superpowers/plans/2026-09-09-radmin-slice7b2-0-precheck.md
918d5906a84b6af6264a2505fc1bd068f90f0d55464a9e554bbf014298e91666  MEMORY.md
c25903463408b4d4621c5b764e4c81e827beb41215a7a3a55602a14283b726ae  docs/2026-07-30-open-bug-register.md
3c336eb753ed38ce1e68d88024101224af6567628107cf86487ee1b3edfa3752  docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md
b02f0af6045eddd4d2661b0a97a26a86dc7f69289745ef44cae432d8c1f61463  docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md
```

The complete index, including gitlinks `spec/docs`, `spec/scenarios`, `spec/test`, is in
`tracked-index.txt`; regular working inputs are in `implementation-files.sha256`. An initial blanket
`sha256sum` correctly refused the three gitlink directories; no directory was represented as a file hash.

Fresh baselines ran before the source edit: native **2883 / 127709 / 0**, codec-only XML **25 / 3934 / 0**,
sequential gateway/mobile pair, and both simulator core variants followed by all 36 anchored scenarios.
The private `measure` checkout was cloned at the production base with every dirty preparation file overlaid.
Its baseline and final board builds use the **same paths**, fixed stamp `Jan 1 2000 00:00:00`, fixed Git
identity `b206b206b206`, and one job. No normal `.pio` build or document edit overlapped either board capture.
After implementation stabilized, full-state `gate` and `mutations` snapshots received all dirty/new inputs.
The mutation snapshot has no generated `.pio-measure` tree, avoiding the known B286/B316 rsync exclusion
limit without changing the harness. The gate snapshot contains a copied measured ELF/manifest so the tools
test that parses a real ELF executes rather than skips. Logs remain outside these measured source trees.

Prediction made before implementation: no Node/resident state or board RAM movement; shared ACCEPT codec
flash may move; CLIENT link reach determines mobile flash. All 36 streams must remain byte-identical.
No live admission producer, controller consumer, rate limiter, storage allocation, session behavior,
wire/NV bump, command/inventory change or metal-only behavior is implemented.

Production diff is only `lib/core/remote_codec.h` and `.cpp`: response opcode 5, appended typed domain/result
kind/code enum, caller-owned request-ctl/code/detail fields, derived fixed overhead 28, and the existing
nonce/header/encode/decode pipeline extended for the three clear bytes. One local `admission_status`
authority validates before encode and after authenticated decode. B382's touched no-consumer claims and
B383's public decoder comment are corrected. Existing variable-result decoding is otherwise unchanged.

## 7. Independent reference, ordering, failures and native arithmetic

The complete new reference is the inventoried sidecar
[`2026-09-09-radmin-slice7b2-0-reference.py`](2026-09-09-radmin-slice7b2-0-reference.py).
It reuses the **unchanged complete source** in Slice-2 evidence §3.7, not any production crypto/codec code.
That embedded executable source, with its terminating newline, hashes to
`6be9de2413db8dd9f3a4a0cb08549ab45326c9a972cc28f3af7e2109ab154924`.
Interpreter: `/home/staszek/mr-slice2-ref/bin/python`, CPython 3.11.2, PyNaCl 1.6.2, `_blake2`.
No dependency was added. Both external primitive anchors run before new vector generation, then the old
45-pair nonce matrix and unchanged **87/87** old-literal comparison. New headers/nonces/AAD/tags/bodies use
explicit byte concatenation, hashlib BLAKE2b-512 and libsodium XChaCha20-Poly1305 with empty plaintext.

Initial generation preceded the first production implementation/output. Its 25 positive records were
unchanged throughout, SHA-256 `c050f8d7b7046f05e922dc9321cf62ea98b3c5bc23dbe8e7124cb06455c8f3f1`
over 2425 bytes. The initial **negative input generator was wrong**: one unsupported-opcode sweep used
`op | 3`, without the required `op << 4`. Thus it labelled some valid AUTH_EXECUTE notices invalid. The
production-path test rejected that false expectation in `feature-build-2.log` (the pio runner then reported
SIGHUP). The correction follows ctl's ruled high-nibble allocation, not production-emitted expected bytes.
`admission-literals.cpp` preserves the initial output; `admission-literals-v2.cpp` was independently
regenerated before the corrected production comparison, now with **1718** valid-tag invalid tuples.
Its SHA-256 is `60595529507dfe85d9b875752c519998dfe47b52756eac5d0fb427785d85a4c3`.
No old or new positive KAT changed. Proposed finding B384 is recorded in §11 for QA's maintained landing.

Earlier `feature-build-1.log` records three coder test-compilation errors: the old fixture's table/member
names were mistyped, and doctest required parentheses around a compound OR. They were corrected using
the existing `kDomainCases`/`fill_ctrl_pub` fixture and a parenthesized expression. Neither run is a PASS.
The final `feature-build-3.log`, actual native binary, and filtered XML all pass. Final native executable
SHA-256: `438719e4585062f5e0b6da7329595b92ed91d3d4a24fcf553cf0cef144c1730c`.

Reference commands from the main checkout:

```sh
/home/staszek/mr-slice2-ref/bin/python docs/superpowers/evidence/2026-09-09-radmin-slice7b2-0-reference.py --emit
/home/staszek/mr-slice2-ref/bin/python docs/superpowers/evidence/2026-09-09-radmin-slice7b2-0-reference.py --compare test/test_remote_codec.cpp --selftest
/home/staszek/mr-slice2-ref/bin/python docs/superpowers/evidence/2026-09-09-radmin-slice7b2-0-reference.py --compare /tmp/mr-codex-s7b20-sX2lBR/corrupt-reference-test.cpp
pio test -e native
./.pio/build/native/program
./.pio/build/native/program --source-file='*test_remote_codec.cpp' --reporters=xml
```

The extended comparison reports **89/89 literal arrays**: 87 unchanged arrays plus packed new positive
and invalid-vector arrays. It checks every initializer, declared length and name, not merely array count.
Missing/extra/changed/duplicate controls all refuse. The separately executed private one-byte-corrupted
test copy exits **1**, with `MISMATCH kAdmissionRefValid at byte 0`. It changes the first `0x53` to `0x52`.
The native tests consume frozen literals; they never regenerate expected values.

Final whole-native: **2888 cases / 172264 assertions / 0 failed / 0 skipped**.
Per-file arithmetic: codec **25 / 3934 → 30 / 48489**; the whole-minus-codec remainder stays
**2858 cases / 123775 assertions**. No test outside that file was edited.
The filtered run is attribution only, not a substitute for whole-native coverage (B364).

| Codec case | Base assertions | Final assertions | Delta |
| --- | ---: | ---: | ---: |
| Exhaustive ctl layout | 1519 | 1569 | +50 |
| Frozen overhead table | 62 | 65 | +3 |
| Fixed length prefixes/suffix | 418 | 477 | +59 |
| New independent wire/metadata | 0 | 953 | +953 |
| New authenticated invalid tuples/publication | 0 | 41232 | +41232 |
| New malformed/auth/buffer boundaries | 0 | 1598 | +1598 |
| New nonce separation/old-field isolation/typed zero | 0 | 529 | +529 |
| New fixed carrier/physical packing | 0 | 131 | +131 |

**PIN re-synced? YES — 2883 + 5 = 2888 cases; 127709 + 44555 = 172264 assertions.**
The instrument retains a literal assignment. New failure tests compare every decoded logical scalar,
every span's address/length and all plaintext canaries, never padding `memcmp`. The 1718 negative vectors
must yield semantic errors, not auth failures, and their encode refusals preserve the output-length sentinel.
Busy 1/2/4/255, every slot, source zero and request-ID zero are covered independently.

The unchanged §5 B383 fixture was linked against the **final** native archives using the same command with
output under this run root. `b383-final.log` reproduces **24 checks**: valid-tag old TERMINAL `08 d1` and
PROTOCOL_ERROR `01 d1` leave their decrypted bytes, return `bad_result_code`, retain the named decoded
sentinels; bad-tag controls retain plaintext. No new case/PIN increment is attributed to that comment fix.

## 8. Simulator, ABI and board attribution

The simulator is bound through `-DMESHROUTE_DIR=/tmp/mr-codex-s7b20-sX2lBR/measure`, not an inherited
default path. CMake Release/Ninja, verbose compilation logs, both `meshroute_core_normal` and `_gw`:
baseline 70 build actions; final header-triggered rebuild 38 actions, including **two codec compilations**.
No simulator source or source-list edit. Baseline executable was preserved and its hash matched its corpus
manifest: `7ad152073f13a76192ff2185488326910d28a759c5c882b1cfa59b012b95e2b9`.
Final executable: `3826d36ee5b07fdaf2eb4836bfcf81240cb4ace18151fce4e66fe693670e186c`.

Both fresh corpora used `tools/run_corpus.py --jobs 3 --require-anchors`, followed by independent `--validate`
runs. All **36/36** anchors reproduce. The live `simulation/BASELINE.md` keystone was read before editing:
s18 `32afbf11`, **269517 events**, zero assertion failures. The measured full MD5 is
`32afbf11e43b4bf9d0bd470ad502ba0a` in both validated manifests.
The standard `--compare` correctly refuses **only `lus_sha256`**, because it is a same-binary determinism
instrument. A separate comparison, after both validations, checks every `COMPARISON_SCENARIO` field and
the actual bytes of all 36 paired `.ndjson` files: **36/36 identical**. No hash field was erased from a
manifest, no anchor was edited, and that distinct proof is not labelled a same-binary comparator PASS.

Both ABI instruments run with their controls: Node remains native **225920/8**, heltec_mobile **117912/8**,
gateway **152288/8**; full ABI **191 checks, 9/9 controls RED**, B278 mirror unchanged.

```sh
python3 tools/measure_board.py pair --output .pio-measure/s7b20-base --jobs 1
python3 tools/measure_board.py pair --output .pio-measure/s7b20-final --jobs 1
```

Both commands run inside the same private `measure` checkout, gateway then heltec_mobile. The complete
compiler commands/flags, source hashes, normal-pio integrity checks, build warnings, object counts, fixed
identity, ELF/payload hashes and pristine copies are in each artifact manifest and `compiler-state.json`.

| Board | RAM base → final | Flash base → final | Objects base → final |
| --- | ---: | ---: | ---: |
| gateway | 198980 → 198980 | 562812 → 562748 (−64) | 285 → 285 |
| heltec_mobile | 207756 → 207756 | 1372992 → 1372992 | 329 → 329 |

Gateway `.text` alone changes size, **−64**; `.data` 976 and `.ARM.exidx` 8 stay the same size. Every
nonzero symbol-size delta is in the codec. The measured seven-symbol accounting is:

| Symbol | Base | Final | Delta |
| --- | ---: | ---: | ---: |
| write_header constprop clone | 402 | 460 | +58 |
| admission_status | 0 | 94 | +94 |
| remote_nonce part clone | 368 | 448 | +80 |
| remote_layout | 518 | 612 | +94 |
| remote_layout part clone | 0 | 584 | +584 |
| remote_body_decode | 1630 | 1174 | −456 |
| remote_body_encode | 1048 | 528 | −520 |

Symbol sizes sum to **−66**; the independent ELF interval-union coverage falls **528877 → 528811** and
uncovered text bytes rise **32951 → 32953** (+2), reconciling exactly to −64. Compiler outlining reduces
the two callers while adding a layout part clone; this is code generation, not a hand-written refactor.
Disassembly confirms both final body functions call `remote_layout [clone .part.0]`; neither base body
function called it. Changed `.data` words retain the same named-symbol offsets or byte-identical referents.
The `.ARM.exidx` PREL31 entry still targets `0x27250`, unwind value 1; its section moves −64 with `.text`.

Mobile **all allocated ELF sections are byte-identical**, and its complete symbol inventory is identical
(no linked remote-codec symbol). Only `.debug_info` and `.debug_loc` contents change; `.debug_loc` shrinks
11 bytes. Its flashed payload therefore differs only in the embedded full-ELF SHA-256 at offsets **176..207**,
the segment XOR checksum at **1373599**, and the derived final 32-byte SHA-256. Both embedded ELF hashes,
all segment XORs and both image SHA trailers were independently verified. No executable/data byte changes
are hidden behind a same-size assertion. ELF inspection was read-only; pristine artifacts were not rewritten.

Two read-only attribution checks were refined after explicit diagnostic failures: the first mobile image
comparison omitted its XOR checksum byte; the first gateway pointer comparison assumed every referent
had an exact symbol name, which pooled strings do not. The final checks validate both derived checksum
forms and the actual relocated referent bytes/symbol offsets. These were analysis assumptions, not firmware
changes. `elf-attribution.log` contains the final check; `gateway-relocations*.log` retain both pointer-check
attempts. The initial mobile diagnostic was an assertion on differing offset 1373599, the checksum byte
now explicitly parsed/verified; its initial log was superseded by the corrected run.

Pristine ELF SHA-256 pairs (base → final):

```text
gateway
c7540dfd2571d1c9a03ff27caad915197e518be89cfb189a69eecf2deb203b9b
6898849c74046ccd4a400fe1c03e350fcf500cefa6f7798d86273946bf1dec7e
heltec_mobile
5a4792d0ddee3949545db4e29a9e7a738b97e2218baa71a9162ac8da62ab9497
c4d55d3e61d2feb84a55eea46fc3e86883bc83a031991a196449d5d03119c24c
```

Exact simulator build recipe (the second build uses the overlaid final source at the same path):

```sh
cmake -S /home/staszek/lora-universal-simulator -B /tmp/mr-codex-s7b20-sX2lBR/sim-base -G Ninja -DMESHROUTE_DIR=/tmp/mr-codex-s7b20-sX2lBR/measure -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/mr-codex-s7b20-sX2lBR/sim-base --parallel 3 --verbose
python3 tools/run_corpus.py --out /tmp/mr-codex-s7b20-sX2lBR/corpus-base --lus /tmp/mr-codex-s7b20-sX2lBR/sim-base/orchestrator/lus --jobs 3 --require-anchors
# Preserve/hash the baseline lus, overlay the final frozen source, then:
cmake --build /tmp/mr-codex-s7b20-sX2lBR/sim-base --parallel 3 --verbose
python3 tools/run_corpus.py --out /tmp/mr-codex-s7b20-sX2lBR/corpus-final --lus /tmp/mr-codex-s7b20-sX2lBR/sim-base/orchestrator/lus --jobs 3 --require-anchors
python3 tools/run_corpus.py --validate /tmp/mr-codex-s7b20-sX2lBR/corpus-base
python3 tools/run_corpus.py --validate /tmp/mr-codex-s7b20-sX2lBR/corpus-final
```

## 9. Full standing chain and mutation union — complete

The exact command driver is `/tmp/mr-codex-s7b20-sX2lBR/gate_driver.py`; commands and exit codes are logged
in `chain-results.json` and `mutations-results.json`, with one raw log and SHA-256 per instrument.
Driver SHA-256: `e9b176bee2233b14a6668bd5d257ac4cb79b1790971c589e8f24e85a0774d5e6`.
Read-only ELF helper `attribute.py` SHA-256:
`c2822871a3ac64759b4f1aa1466dafca9a0a78e655f81828891a6931e6c3165a`.
`MR_LUS_SRC=/home/staszek/lora-universal-simulator` is explicit for all private probe calls.

The **23-command standing chain completed with every exit code 0**. Exact commands, from the frozen
`gate` checkout (the loop names both controlled and explicitly probe-only invocations):

```sh
export MR_LUS_SRC=/home/staszek/lora-universal-simulator
python3 tools/probe_board_abi.py
python3 tools/probe_b278_row_abi.py
for probe in console_sink inbox_verbs firmware_ui custody_usb ble_line features; do
  bash "tools/probe_${probe}/run.sh"
  bash "tools/probe_${probe}/run.sh" --no-neg
done
python3 -m unittest discover -s tools -p 'test_*.py'
python3 tools/gen_command_inventory.py --write
python3 tools/gen_command_inventory.py
python3 tools/gen_command_inventory.py --check
python3 tools/check_command_authority.py
python3 tools/check_command_authority.py --selftest
python3 tools/check_a0_matrix.py
python3 tools/check_data_type_literals.py
bash tools/warning_census.sh
```

| Instrument | Reproduced result |
| --- | --- |
| Full ABI / B278 mirror | 191 checks + 9 controls / 42 measurements + 6 controls; unchanged pins |
| Console sink | 6 profiles, 720 checks, 82 structural, 905 BLE guard, ownership 6 + 3 controls; 146 controls RED |
| Inbox verbs | ACCEPT 771 + 50 controls; CLIENT 378 + 45 controls; both real-TU arms PASS |
| Firmware UI | 433 / 868 / 433 executed; 223 controls; coverage 703/840 |
| Custody USB | 27 checks, 10 controls |
| BLE line | 40 checks, 8 controls |
| Features | 9 cells, 120 checks, 59 controls; 9-file ownership, 40 ownership controls |
| All six `--no-neg` invocations | Executed successfully at their positive pins; **not control gates** |
| Tools sweep | 343 tests, zero skips, OK (638.091 seconds) |
| Inventory write/bare/check | 204 rows; byte-identical before/after and to main |
| Authority | table/header/inventory agree; 6/6 selftests RED |
| A0 / DataType literals | PASS / PASS |
| Warning census | 6/6 pinned environments; zero switch warnings |

B350 is not silently fixed: the firmware-UI `--no-neg` mode still prints `PASS`; here it is explicitly
probe-only and contributes no negative-control claim. All full probe controls ran; none was unusable.
Inventory SHA-256 before/after: `99cf5bd9e283dcbd5ba702ccb2f7764873e36d69651b39ab26a24623fa06bea3`.
The six census warnings are gateway_heltec **173**, gateway_heltec_v4 **178**, heltec_mobile **177**,
heltec_v3 **177**, heltec_v4 **182**, heltec_v4_mobile **182**, all exactly pinned and `-Wswitch` 0.
The census's incidental sizes are not substituted for the separately fixed-identity ruled board pair.

Changed-source selector: **`radmin2codec`**, the only changed configured `TARGET_SRC` file.
Dependency/historical selector: the **47-battery** 7b-1 QA floor, including the overlap `radmin2codec`:

```text
radmin2codec a0rx b159map b159rx b161rx b251rx radmin3verbs radmin5rx radmin5session
radmin7exec radmin7rx radmin7transcript sliceBnode sliceBrx sliceEnode sliceGrx teamgrant
consoleline cmdauthority radmin3acl radmin3id radmin5runtime devicenv teamkeyring grantpark
grantadmit b161hash b251hash b161mac b159mac b20mac b20codec sliceBmac sliceAinbox sliceGinbox
sliceCinbox sliceCpull sliceDclear sliceDstore sliceDtoken b134inbox b134store b134ram
sliceAjson sliceDack sliceGjson b134ack
```

Union is those same **47** batteries, not two separately counted overlapping sets. The unchanged receive,
state, transcript, command, routing, frame-codec and inbox dependencies remain because the codec is already
consumed, and because the independent-QA acceptance floor is binding. Each command is
`python3 tools/probe_ui_model_mutations.py --target=NAME --workers=2` from the frozen mutation snapshot.
Each worker builds and runs its own unmutated baseline, then restores by hash; compilation failure is
UNUSABLE, never assertion RED. B342/sliceBmac M04 is the sole authorized unusable exception.

The codec battery has completed **103 RED / 0 unusable**: old 66 retained, R29 retargeted only for the
now-reserved `0x6..0xF` comment, plus 37 new admission controls. All match counts one; both workers' final
restoration matches, and all 56 configured source files in the parent snapshot match their launch hashes.
New controls separately target domain/security/fixedness/key choice, each nonce suffix term/order, each
clear AAD field (existing controls retain outer/source/length coverage), tuple validation, all publication
failures, typed metadata, cap/fixed-length admission and old-domain contamination. Comparator controls are
reported separately in §7 and are not native mutation REDs.

The full union completed: **712 RED / 1 unusable**, exactly the historical **675 RED + 37 new controls**,
with the sole unchanged B342/M04 exception. Every battery independently derived **2888 / 172264 / 0**;
every worker completed its assigned entries and restored its source; every parent verified all **56**
configured source files unchanged. No new green/vacuous/unusable control, lost worker or failed restoration.
The final roll-up independently reconciles all 47 summaries, worker baselines, restorations, return codes
and raw-log SHA-256s in `mutation-audit-final.log` (and all 23 standing-command logs).

The first roll-up attempt counted the merged baseline summary as a third worker. That assertion refused
the report before any verdict; the corrected parser checks all baseline values but counts only the
`[wN]` baseline lines against worker restorations. Raw instrument outputs were never changed.

| Battery | RED | Unusable | Exit | Restored worker source MD5 |
| --- | ---: | ---: | ---: | --- |
| radmin2codec | 103 | 0 | 0 | `22e3cfc10c678418ece5ce2daea0607b` |
| a0rx | 7 | 0 | 0 | `2e7f9edaa9d48e072f3e061ac411f322` |
| b159map | 2 | 0 | 0 | `f1cc7ffeda0ba77db8dda82fc5356c25` |
| b159rx | 3 | 0 | 0 | `2e7f9edaa9d48e072f3e061ac411f322` |
| b161rx | 8 | 0 | 0 | `2e7f9edaa9d48e072f3e061ac411f322` |
| b251rx | 19 | 0 | 0 | `2e7f9edaa9d48e072f3e061ac411f322` |
| radmin3verbs | 28 | 0 | 0 | `a2720a19ea9fa8ee0608069dfc2b1780` |
| radmin5rx | 14 | 0 | 0 | `2e7f9edaa9d48e072f3e061ac411f322` |
| radmin5session | 25 | 0 | 0 | `56aa83a751f3ec36d8cbdc616d98911c` |
| radmin7exec | 11 | 0 | 0 | `bc7b2b09749989229b1aaca9861ca4f4` |
| radmin7rx | 13 | 0 | 0 | `2e7f9edaa9d48e072f3e061ac411f322` |
| radmin7transcript | 20 | 0 | 0 | `56aa83a751f3ec36d8cbdc616d98911c` |
| sliceBnode | 8 | 0 | 0 | `f1cc7ffeda0ba77db8dda82fc5356c25` |
| sliceBrx | 16 | 0 | 0 | `2e7f9edaa9d48e072f3e061ac411f322` |
| sliceEnode | 3 | 0 | 0 | `f1cc7ffeda0ba77db8dda82fc5356c25` |
| sliceGrx | 42 | 0 | 0 | `2e7f9edaa9d48e072f3e061ac411f322` |
| teamgrant | 4 | 0 | 0 | `f1cc7ffeda0ba77db8dda82fc5356c25` |
| consoleline | 12 | 0 | 0 | `906e1a837b8ca667d191e55ac5bdd848` |
| cmdauthority | 16 | 0 | 0 | `401ed4d149fc410899f51d98fac667c1` |
| radmin3acl | 36 | 0 | 0 | `9e9fc3a9f19be1a654e034b15e4ae9ce` |
| radmin3id | 23 | 0 | 0 | `e919adc3b622885c788e470b854ab742` |
| radmin5runtime | 16 | 0 | 0 | `2fc7c0e9d04c5235dc9e60bf05f2a66f` |
| devicenv | 42 | 0 | 0 | `eaf395c5d9caea43c256eaf84fbb9e8b` |
| teamkeyring | 68 | 0 | 0 | `d5ec2e56c194d51c8d5c9f679232e380` |
| grantpark | 3 | 0 | 0 | `ffb88cb2cb558a1158d741e6bc9eb6e4` |
| grantadmit | 1 | 0 | 0 | `677c65439817a380f3fc94d9b651341d` |
| b161hash | 6 | 0 | 0 | `ffb88cb2cb558a1158d741e6bc9eb6e4` |
| b251hash | 55 | 0 | 0 | `ffb88cb2cb558a1158d741e6bc9eb6e4` |
| b161mac | 1 | 0 | 0 | `677c65439817a380f3fc94d9b651341d` |
| b159mac | 2 | 0 | 0 | `677c65439817a380f3fc94d9b651341d` |
| b20mac | 11 | 0 | 0 | `677c65439817a380f3fc94d9b651341d` |
| b20codec | 5 | 0 | 0 | `70aff75d2bb570b9320bb471b7142e40` |
| sliceBmac | 3 | 1 | 1 | `677c65439817a380f3fc94d9b651341d` |
| sliceAinbox | 1 | 0 | 0 | `a8088417e948aef2e5ffbe8cec312b27` |
| sliceGinbox | 4 | 0 | 0 | `a8088417e948aef2e5ffbe8cec312b27` |
| sliceCinbox | 2 | 0 | 0 | `6e60f13c2aec55cbffc5b48d24cc9d8d` |
| sliceCpull | 2 | 0 | 0 | `a8088417e948aef2e5ffbe8cec312b27` |
| sliceDclear | 5 | 0 | 0 | `a8088417e948aef2e5ffbe8cec312b27` |
| sliceDstore | 3 | 0 | 0 | `29884575bbf3b33bf2ca456de63b1d69` |
| sliceDtoken | 2 | 0 | 0 | `668d664775bed49047d60e9bceb1d702` |
| b134inbox | 5 | 0 | 0 | `a8088417e948aef2e5ffbe8cec312b27` |
| b134store | 39 | 0 | 0 | `29884575bbf3b33bf2ca456de63b1d69` |
| b134ram | 3 | 0 | 0 | `39644fd2d33466f12779c92431a3dc71` |
| sliceAjson | 1 | 0 | 0 | `45df30d5066440b1dc098b65e23e4fd8` |
| sliceDack | 1 | 0 | 0 | `21b1db7be690a55771a96490f003f769` |
| sliceGjson | 16 | 0 | 0 | `45df30d5066440b1dc098b65e23e4fd8` |
| b134ack | 2 | 0 | 0 | `21b1db7be690a55771a96490f003f769` |

B342's exact executed failure remains: `the suite still PASSES; nothing measures this`, M04 in sliceBmac;
the battery exits 1 with **3 RED / 1 unusable**. It is neither closed nor included in 712 RED.

**Coder verification is complete, not an independent QA PASS.** QA must rerun the frozen handoff; the owner
then commits this codec prerequisite separately before QA reissues the behavior brief at the actual successor.

## 10. Frozen implementation files and handoff

```text
c090f2d577b0aa247cc0952c87f2c004da7464962862f2dc3bd92f3ee0d47cb2  lib/core/remote_codec.h
a972c2fceef24ac38cbc442c92d5926b4783648f04567bc7f0d4ba5c031c8820  lib/core/remote_codec.cpp
b5c1837f7e9b436186071cee62a38e4431b43ba46c871543649c742ddc9b112b  test/test_remote_codec.cpp
12d4e1d03404d4a3a31957dad5d89f418743c9b1259cf5ab7389a91ad4b7cc26  tools/probe_ui_model_mutations.py
ca485257119e1fb01ede94d5679ec40ac52bad2dc8ee22faf643b1f709227d96  docs/superpowers/evidence/2026-09-09-radmin-slice7b2-0-reference.py
```

This report is the only file changed after the implementation snapshots were frozen, and only by appending
evidence/correcting its current-status heading. QA-owned preparation remains preserved. No commits.

Final source-integrity audit rechecks all inventoried working files against the implementation freeze,
excluding only this evolving coder receipt: **no differences**. Both repository bases remain as recorded;
MeshRoute's named preparation and six coder-delivered files are intentionally uncommitted, while the
simulator is clean. All eight preserved board ELF/payload hashes still match their manifests. Whitespace
checks pass in both repositories. This report has no self-referential hash; QA records its actual hash
alongside the five explicit implementation/reference hashes when snapshotting the handoff.

## 11. Findings and QA-owned landing proposals

- **B382:** touched header/test introduction corrected; real session/bootstrap/transcript consumers exist,
  while only the newly allocated admission domain has no live producer. Ready for QA verification/closure.
- **B383:** public decoder comment now distinguishes auth failure from valid-tag semantic failure and
  caller-owned wiping. Final 24-check fixture reproduced; no old decode behavior changed. Ready for QA closure.
- **Proposed B384 — coder reference negative-input opcode not shifted.** New reference initially constructed
  an unsupported request opcode with `op | 3` instead of `(op << 4) | 3`, mislabelling valid notices invalid.
  The executed production-path test caught it. Corrected in the evidence-side generator and negative literals;
  final **1718** authenticated invalid cases all refuse semantically; all 25 positive vectors and all 87 old
  arrays are unchanged. Suggest record **CLOSED (fixed before handoff)** after independent QA verification.
  This is an implementation-time instrument defect, not a pre-existing production codec defect.
- **B378/B379 stay OPEN** for the separately committed/reissued behavior slice's storage/rate/live-producer
  and lifecycle proofs. B342 remains the named historical unusable; B312/B315/B350/B359/B364 remain outside
  scope. No bench part is owed by this codec-only slice.
