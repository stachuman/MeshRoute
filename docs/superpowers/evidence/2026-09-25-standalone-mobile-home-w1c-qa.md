<!-- Author: Codex, independent Quality Agent — W1c full P6 gate; owner rules and commits -->
# Standalone Home W1c — independent QA

**Verdict: INDEPENDENT SOFTWARE QA PASS — 2026-09-25.** The full §4.2/P6 gate reproduced the coder's results.
**B447's unnamed half is closed; named-peer precedence remains open.**

The reviewed implementation makes `Node::effective_name` a counted copy of the stored name. It writes nothing for
an empty name or zero capacity, preserves the full 32-byte contract and never terminates. Unnamed senders select
the existing nameless wire forms; `whoami` retains the explicit `name=""` field. The production change is confined
to that accessor and the approved comment sites. Receivers, codecs, NV, Node layout and the simulator are
unchanged. No production fixes, staging or commits by QA.

Contract: [brief revision 2](../plans/2026-09-25-standalone-mobile-home-w1c-no-default-name.md), SHA-256
`39bfd6f0025b37c65826ac71b3effffbf82df25b7fa05c405c79b957992dfce6`.
Coder freeze: [receipt](2026-09-25-standalone-mobile-home-w1c.md), SHA-256
`b7979322c22dc1e6037522b5dc5550c7ade8a8239cc2e2e82dceee1922fbe796`.
Compact QA evidence: [checksums](2026-09-25-standalone-mobile-home-w1c-qa/SHA256SUMS).

## 1. Inputs, independence and preservation

MeshRoute `main` at `8360802904f7bd0023279d3da844453d61207ede`; simulator at
`6585649ea5a780f0542b2931853a667be56a5b2b`, clean. The uncommitted candidate was reviewed and built in the shared
checkout. Mutation workers copied the working tree, including uncommitted and untracked inputs; no HEAD-only
snapshot was used. The simulator was configured and built fresh outside both repositories using its stock recipe.

QA independently verified all 11 frozen source hashes and line counts, all six preparation hashes, and the coder's
86-file, pre-check's 18-file and brief-review's seven-file checksum inventories. The initial whole-tree inventory
contains 1,321 MeshRoute paths and 285 simulator paths, including symlink identities. The eleven executable inputs,
approved brief and preparation set remained unchanged throughout the gate. See
[input pins](2026-09-25-standalone-mobile-home-w1c-qa/preflight.json),
[whole input inventory](2026-09-25-standalone-mobile-home-w1c-qa/inputs.json) and
[final preservation check](2026-09-25-standalone-mobile-home-w1c-qa/preservation-prelanding.json).

No QA evidence or landing document was added to the nonignored tree before the last gate step. Board measurements
ran alone, `--jobs=1`, with two back-to-back pairs and no intervening build or checkout edit. The source hash from
the coder's final measurement reconstructs exactly after removing only the 49 report/evidence files added after
that measurement; see [reconciliation](2026-09-25-standalone-mobile-home-w1c-qa/coder-board-source-reconciliation.json).
The production/test/tool candidate and approved brief remain hash-identical after QA's separate documentation
landing; the preserved input inventory identifies the preparation documents used during the gate.

## 2. Independent full gate (§4.1 steps 1–11)

Every result below comes from QA's own execution. The per-command argv, exits, durations and log hashes are retained
in [run ledger](2026-09-25-standalone-mobile-home-w1c-qa/run-ledger.json) and
[raw artifact inventory](2026-09-25-standalone-mobile-home-w1c-qa/raw-artifacts.json).

| Instrument | QA result |
| --- | --- |
| Hygiene | Both repositories clean under `git diff --check`; comment-only file line counts 1915 / 1875 / 376 preserved; `node.h` remains 4088 |
| Native | `pio test -e native`, then the binary itself: **2962 cases / 195904 assertions / 0 failed / 0 skipped** |
| Fresh stock simulator + corpus | **36/36 anchors PASS; every one of 14 per-stream fields identical** to the pre-check manifest; s18 **269517 events**, MD5 **`32afbf11e43b4bf9d0bd470ad502ba0a`** |
| Full Node ABI | **290 checks**, nine controls RED; Node **235208 / 122176 / 157304** (native / mobile / gateway), align 8, unchanged |
| B278 row ABI | **42 measurements**, six controls RED |
| Inbox verbs | ACCEPT **1400 / 63 controls**; CLIENT **483 / 71 controls**, zero failed/unusable; default both arms and separate explicit CLIENT run |
| Console sink | **720 checks / 152 controls**, structural 84, BLE guard 905, ownership 6 plus three ownership controls, six profiles; zero failed/unusable |
| Firmware UI | **467 / 902 / 467** checks on l2 / v3 / BLE-row; **225 controls verified**, zero unusable; C0 retains its required build-failure meaning |
| Custody USB | **27 checks / 10 controls**, zero failed/unusable |
| BLE line | **55 checks / 12 controls**, zero failed/unusable |
| Feature matrix | **9 cells / 112 checks / 58 controls**, including 43 ownership controls; zero failed/unusable |
| Deferred actions | Four variants: local **150/151/158/158**, remote **416/518/534/464**, radio **3160/3485/3689/3695**, 39 transcripts each; **40 executable controls RED**, four source-reader controls and six placement controls |
| Diagnostic modes | All seven probes also ran with `--no-neg`; these are diagnostic results, never substitutes for the default control-bearing gates |
| Parser compatibility | All **12 actual X21–X26 output lines** parsed: empty, `Bench 1`, 32-byte names × TEXT/JSON × ACCEPT/CLIENT; identity fields preserved |
| Inventory / checkers | Tracked **197-row** inventory byte-identical; command authority + six controls; A0 + ten controls; data-type literals (218 files) + four controls, all PASS |
| Tools discovery | **356 tests, OK, zero skips**; inventory check ran first |
| Warning census | All six OLED environments at unchanged pins **171/175/175/175/179/179**; `-Wswitch` **0** throughout |
| Stock board repeatability | **Both environments PASS**; all `measurements.*`, ELF and payload hashes equal the coder's `final-1` |
| Mutation selectors (a) ∪ (b) | **9 batteries / 34 RED / 0 unusable / 0 vacuous**; every worker baseline **2962/195904/0**, no stale-PIN banner |

The warning census is the existing six-environment exception to the two-board rule. Its incidental build-size
figures do not replace the stock board tool's measurements. Tools discovery emitted existing Python
`ResourceWarning` messages about `/dev/null`; no test failed or skipped.

## 3. Production scope and regression quality

The source diff was read independently. Outside `effective_name` and its comment, `node.cpp` is byte-identical to
HEAD. The other four production files contain comment-only edits; their executable line prefixes and line counts
are unchanged. No pre-existing mutation list, target mapping or control meaning changed.
[Source audit](2026-09-25-standalone-mobile-home-w1c-qa/source-review.json).

The new/strengthened native cases exercise the real producers and fresh receivers:

- INTRO prefix **33 B**, authoritative key-answer body **35 B**, WANT_PUBKEY H **40 B** or **44 B** in a team,
  requester-key forward body **33 B** (34 B including origin). Each has an independently seeded named counterpart.
- INTRO accepts a **199-byte** message beside the 33-byte prefix; 200 bytes takes the existing plain fallback with
  exactly one `intro_attach_too_large` event.
- All five receive paths accept nameless arrivals without inventing a name, preserve an existing unpinned label,
  and publish the expected cache event. Existing old-default strings remain valid cached data, with no migration.
- The poisoned 40-byte accessor fixture observes exact copies, clamping, cap zero and untouched tails without
  making its intended wrong-copy mutants overflow the fixture.
- Real firmware `whoami`, through `exec_console_line` on both transports and profiles, prints the complete identity
  line with empty, short and full 32-byte names. The shared fake's numeric-format limitation is explicit (§5).

QA ran both new router controls and all five new native entries, then directly ran the retained five mutant binaries
to capture the failing case names (expected exit 1, not a crash):

| Control | Independently observed failure |
| --- | --- |
| whoami W1c-C1, restore made-up name | Exactly **X21/X22**, two failed checks per ACCEPT/CLIENT arm |
| whoami W1c-C2, omit empty field | Exactly **X21/X22**, two failed checks per arm |
| w1cname W01, restore core default | Exactly **13 new/strengthened cases**, **45 failed assertions** |
| w1cname W02, reserve terminator | Only accessor case, **8 failed assertions** |
| w1cname W03, write on empty | Only accessor case, **2 failed assertions** |
| w1cname W04, ignore capacity | Only accessor case, **4 failed assertions** |
| w1cretain R01, remove refresh length guard | Exactly receive routes **2–5**, **8 failed assertions** |

R01 does **not** cover INTRO: that receiver passes a null name pointer for zero length, so removing only the length
term leaves its guard false. INTRO has its real receive case and is RED under W01. No broader control coverage is
claimed. [Case attribution](2026-09-25-standalone-mobile-home-w1c-qa/mutation-case-runs.json).

Selector (a): `radmin8node` 1, `radmin73node` 5, `teamgrant` 4, `b159map` 2, `sliceBnode` 8,
`sliceEnode` 3, `w1cname` 4. Selector (b): `b161hash` 6 and `w1cretain` 1. All 34 entries match once,
compile and fail assertions. [Union](2026-09-25-standalone-mobile-home-w1c-qa/mutation-union.json).

**PIN re-synced? YES — 2951/195777 + 11 cases / +127 assertions = 2962/195904**, independently reproduced.
Inbox pins: ACCEPT **1394+6=1400** checks, **61+2=63** controls; CLIENT **477+6=483**, **69+2=71**.

## 4. Board measurements and attribution

QA ran the stock pair twice under `.pio-measure/w1c-qa/final-1` and `final-2`; both per-environment stock
`compare` calls passed. Field-by-field comparison to the coder's final-1 matches **every measurement, toolchain,
fixed-identity and path field**, plus exact ELF and payload hashes. Source differences are solely the 49 disclosed
report/evidence additions; ordinary `.pio` metadata is identical within QA's pair. It need not equal the earlier
coder run after intervening native/census builds.

| Environment | RAM (base → final) | Flash (base → final) | Objects | Final payload SHA-256 |
| --- | ---: | ---: | ---: | --- |
| `gateway` | 203740 → **203740** (0) | 572240 → **571936** (-304) | 285 | `97deca60b8f3ca94d790986f016a0cefb43a82cd2aeb99ace7db6ed830682886` |
| `heltec_mobile` | 211724 → **211724** (0) | 1394732 → **1394592** (-140) | 329 | `38739c4aba867f1800caa596b3a8aa1e4e204301284858f9351bd76af03c1c3a` |

[Independent comparison](2026-09-25-standalone-mobile-home-w1c-qa/board-comparison.json);
[four QA manifests](2026-09-25-standalone-mobile-home-w1c-qa/manifests/).

QA additionally hash-verified the coder's archived base and final ELF/payload files and regenerated the section and
symbol inventories with the stock reader. This is fresh analysis of preserved artifacts, **not an independently
rebuilt before-state**. All recorded section/symbol fields match those artifacts.
[Attribution](2026-09-25-standalone-mobile-home-w1c-qa/archived-board-attribution.json).

- Gateway: `effective_name` **492→210 B** (−282), removed default prefix **19 B**; three zero-size `$d` rows disappear,
  and a same-size four-byte compiler switch table is renamed. Linked symbol-size total −301 B; `.text` and total
  flash −304 B. The remaining 3 B lie outside the named-symbol totals; their attribution to section layout/alignment
  is an inference. RAM is independently measured unchanged.
- Mobile: `effective_name` **170→39 B** (−131), removed prefix **19 B**. Six other linked functions change by four
  bytes each: `dispatch`, `emit_hash_query`, `intro_attach_prefix`, `send_hash_bind_pubkey_response` +4 each;
  `on_command`, `on_timer` −4 each. Net linked symbol-size total −142 B; `.flash.text` −124 and `.flash.rodata` −16,
  total flash −140. These small linked-code changes, with unchanged source bodies, are consistent with placement
  and instruction relaxation; that mechanism is attribution by inference, not an additional source change.
- Every other loadable section, object count and RAM total is unchanged. Symbol hashes and payload hashes change
  as expected. QA's rebuilt final outputs reproduce the coder's exact final artifact and measurement fields.

## 5. Coder handover findings and limits

1. **B451 registered, LOW / non-gating:** shared fake `tools/probe_board_ui/fakes/Arduino.h:90–91` says no assertion
   reads based numeric output. X21–X26 now do, explicitly pinning the fake's decimal rendering of a HEX request.
   QA observed `hash=0x2972535171`. Parser verification preserves that string and proves the name field; it does
   **not** prove real Arduino HEX formatting. Correct the comment under a future fence; no fake change here.
2. **Row table accepted:** the X1–X20 table explicitly describes the historical RADMIN-0c block. Current pin
   derivations name and count X21–X26, with variants and transports. That additive mapping satisfies the new-row
   documentation intent while preserving historical counts; no coder return or owner ruling needed.
3. **B350 already open:** firmware-UI `--no-neg` still prints bare `PASS` with zero verified controls. Reconfirmed,
   not duplicated or used as gate evidence. The default run independently verifies all 225 controls. Existing
   inbox child-summary ambiguity is B303; the command ledger distinguishes diagnostic and gate modes.

4. **B452 registered and closed at landing, documentation only:** the companion name paragraph called `nameof`
   both current human text and implemented JSON. `handle_nameof` and `write_peer_name` confirm the JSON path; the
   authorized paragraph update now distinguishes the current contract from the 2026-07-16 history. No code changed.

B450 remains open: hosted-mobile registration never fills its stored name and three comments still mention the
retired `0x94` push. W1c adds no name-bearing registration and deliberately leaves those files untouched.

Not run: board-UI (B418, package W2); full device-radio probe (only its source-reader check was run: **25 PASS**,
confirming the edited name comments do not affect its predicates); provisioning-TX (unfenced provisioning/config
owners); remote-admin codec references (no codec/vector/remote-admin test change); hardware tests. No new metal-only
behavior or bench check is introduced. Existing UI-13 and other hardware obligations remain the owner's.

## 6. QA landing and retained artifacts

After the final preservation check, QA updated the register's W1c dispatch and B447 in place, recorded B451,
reconfirmed B350, and recorded/closed B452's companion wording correction. B450 remains open; next free finding
is **B453**. The design §4.6/§13, tracker, MEMORY, address-book §2.3 and companion name contract now describe the
gated state. No brief, coder receipt, production/test/tool file, tracked inventory or simulator input was altered.
[Landing inventory and preservation](2026-09-25-standalone-mobile-home-w1c-qa/landing.json).

W1 and W1c are software-complete. W4a still requires W3; no next package or hardware PASS is dispatched by this
receipt. Commits remain the owner's and do not block subsequent authorized work.

Raw logs and supplementary analysis:
`/home/staszek/MeshRoute/artifacts/2026-09-25-standalone-mobile-home-w1c-qa/`.
Fresh simulator build, 36 output streams and retained new-battery scratch trees:
`/tmp/meshroute-w1c-qa-20260925-u2uf04es/`.
QA board artifacts: `/home/staszek/MeshRoute/.pio-measure/w1c-qa/{final-1,final-2}/`.
The deferred-action run's generated sources and detailed results are named in its log. These local raw artifacts
are hashed but ignored by Git; the compact receipts, manifests and findings are the durable repository record.
No cleanup of prior work was performed.
