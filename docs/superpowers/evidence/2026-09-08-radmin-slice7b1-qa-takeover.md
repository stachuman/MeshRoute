<!-- Quality Agent: OpenAI Codex; independent of the implementation session -->
# Slice 7b-1 QA takeover — revision 5 / B374, 2026-09-08

**Focused takeover conclusion (revision 5): B374's immutable-transcript resolution is supported by source and fresh synthetic execution. The implementation and independent full gates are PENDING. Revision 5 needed the consistency fold-ins recorded as B375 below; revision 6 now contains them (follow-through receipt §7).** This is a takeover review, not a frozen coder report, implementation PASS, or authorization for 7b-2. Sections 1–6 retain the original takeover observations and focused-run results.

## 1. Authority and observed checkout

The owner's takeover instruction applies the 2026-09-07 responsibilities in `docs/2026-09-02-agent-roles.md` with this session replacing Claude: QA authors specifications/briefs, independently gates, and maintains documentation; the other Codex session implements production changes. The owner rules, commits, and bench-verifies. No production fixes, reset, clean, commit, simulator edits, or full gate were performed here.

Observed independently, before QA documentation edits:

- MeshRoute HEAD `d467787f5e02836ad19b79624d97729c81ebacf8`; 23 modified tracked files and seven untracked files. None staged. The tracked changes comprise three QA documents, ten production files, three tests and seven tools. The untracked paths are listed below.
- Simulator HEAD `06746a97de5764415d6fcef10b97bca90569b9c7`; porcelain status empty.
- Brief revision 5 SHA-256 `b740a9ff7f67276b0d9a49dbf35ad3216f5cdc12d49841f4ca08fc9d8d0979f6`.
- The coder evidence's latest checkpoint is revision 4, §§7.1/8. Its opening STOP before implementation, old base figures and previous proposed resolutions are historical. Its checkpoint numbers have NOT been independently re-gated by this session.
- The owner's current instruction explicitly permits inspecting the partial implementation plus QA changes. It supersedes the brief's stale opening restriction to brief/register/evidence only; it does not authorize discarding anything or taking a HEAD-only QA snapshot.

Untracked at takeover:

```text
docs/superpowers/evidence/2026-09-08-radmin-slice7b1.md
src/firmware_remote_executor.h
test/test_firmware_remote_executor.cpp
test/test_node_remote_exec.cpp
test/test_remote_transcript.cpp
tools/probe_inbox_verbs/fakes/helpers/radiolib/CustomSX1262.h
tools/probe_inbox_verbs/remote_exec_rows.h
```

The fresh QA snapshot is `/tmp/mr-qa-s7b1-n87ss3cv/snapshot`, containing **920 tracked and non-ignored untracked input paths**, including the seven above. Files were copied from the working tree, not exported from HEAD. `manifest.json` records each path, SHA-256 and mode; deleted paths would be recorded explicitly. HEAD, porcelain status and every input hash were checked again after copying and again after the focused tests: no concurrent input change. Build products and the installed doctest header are outside that source snapshot; the header was copied separately and hashed.

Snapshot manifest SHA-256: `6dad4e4cf4c014128c58fce44c30f35acdcb32f519ccee033fbec47ef399f017`. The sibling `head.txt`, `status.txt`, `diff.patch` and `receipt.txt` preserve the observed state. Ignored build products were not copied. This snapshot is valid for this focused review only; final QA must capture the coder's later frozen tree again.

## 2. Current implementation, verified by source inspection (V1)

- `remote_session.{h,cpp}` contains the four-header/eight-chunk transcript pool, capture/completion/encoding, exact-retry cursor reset, synchronous authenticated ACK release with retained seen tombstones, and the B369 never-executed expiry exception. Completion and append only modify a capturing transcript (`remote_session.cpp:492`, `:527`); encoding accepts a const session (`:560`). This is inspection of partial code, not a complete lifecycle audit.
- Node exposes the executor seam (`node.cpp:126`) and the checked response sender (`node_mac_rx.cpp:2119`). The sender advances only on queued/parked admission. The current encode-refusal path preserves the transcript/cursor but falls through to `response_enqueue_failure` (`:2138`).
- The pure executor and outcome map are in the untracked `src/firmware_remote_executor.h`; device adapters are in `firmware_commands.cpp:252` and `:266`. The common seam publishes `CommandContextScope` at `:1630`; `fw_main.cpp:1759` calls the ACCEPT executor from the main loop. Native's `test_build_src = no` still excludes the real firmware dispatcher.
- B370's ACL actor is read at `firmware_admin_verbs.h:310`; B371's semantic filter is at `firmware_inbox.cpp:22`. B372's probe medium now uses `inbox_record_max_bytes` and refuses oversize (`probe_main.cpp:142`, `:151`). Their claimed end-to-end probe outcomes remain unverified here.
- The mutation harness still pins `2855 / 121999` at `tools/probe_ui_model_mutations.py:687`. AST inspection of its `TARGET_SRC` and a full-text search show **none** of `radmin7transcript`, `radmin7exec`, `radmin7rx` exists yet. This corroborates the coder's explicit pending-work statement; no mutation run was started.

## 3. B374: independent reasoning and execution

`remote_codec.cpp:277` derives the nonce from the selected key, outer type, control byte, request ID, sequence, source hash, and an epoch only for the specified bootstrap/rollover domains. The TERMINAL result is plaintext, not a nonce input. `remote_transcript_encode` (`remote_session.cpp:560`) selects the retained terminal/chunk and reseals it under the retained session identity. `remote_transcript_complete` (`:527`) freezes the result and frame count once; later completion/append calls cannot rewrite a ready transcript. `remote_transcript_sent` (`:598`) alone advances its send cursor; the exact authenticated retry resets the cursor for a completed transcript (`:1013`).

Thus, substituting another terminal after the original was sealed can encrypt different plaintext under the same key/nonce. Queued **or parked** admission already gives transport a copy (`node_mac_rx.cpp:2134`); observed radio airtime is not the boundary. Revision 5's freeze-at-completion rule is deliberately stronger than waiting for publication. A completion that has never been sealed has not itself consumed a nonce, but it is still immutable under this rule. No production nonce-reuse vulnerability was demonstrated: the inspected sender already avoids the dangerous substitution.

Design §8.9 (`2026-08-23-remote-admin-independent-rpc-design.md:921`) permits `internal_error` for execution or staging failure before a truthful normal result existed. Design §10 (`:1143`) requires original-transcript replay and forbids different plaintext in the old nonce space. Completion-time failure and send-time failure must remain separate.

Fresh builds used GCC/G++, the copied installed doctest **2.4.12** header, the snapshot's unchanged `test/test_remote_transcript.cpp`, and freshly compiled production `remote_session.cpp`, `remote_codec.cpp`, `identity.cpp`, `dm_crypto.cpp`, `frame_codec.cpp`, and `monocypher.c`. No existing `.pio` binary/object was linked or executed. Native C++20/no-exception/no-RTTI and role flags were supplied explicitly; this was a small standalone host executable, not the PlatformIO/full native gate. Exact compiler commands are in `focused-build.log` and `qa-b374-build.log` beside the snapshot.

| Fresh focused instrument | Independently observed result | Limit |
| --- | --- | --- |
| Existing B374 case, `--test-case='*failed reseal*'` | **1 case, 32 assertions, 0 failed; 11 unrelated cases skipped** | Real session/codec; deliberately one-byte output buffer. No real Node sender/counter or transport admission. |
| Separately authored QA case, `--test-case='QA B374*'` | **1 case, 128 assertions, 0 failed; 12 unrelated cases skipped** | Synthetic host proof over the same real pool/codec; no firmware handler or Node failure-counter claim. |

The existing case authenticates/decrypts both original and substituted terminals, observes completed versus internal_error, different encoded frames and equal 24-byte nonces. Its legal-state branch obtains byte-identical replay after `bad_buffer`; the substitution occurs only in a separate copied state.

The additional QA case covers silent and 207-byte output (one and three frames respectively). It snapshots the **entire session bytes**, attempts a late append and second completion with internal_error, and checks no change. After fully draining and retrying, it forces a one-byte-buffer refusal with `written` initially **123**, requires `written == 0` and byte-identical full retained state, then compares every replay frame to the original. Source: `/tmp/mr-qa-s7b1-n87ss3cv/qa-b374.cpp`; logs: `b374.log` and `qa-b374.log`. Both final focused builds had no diagnostics. The first QA-only build had a range-loop string-copy warning; it was corrected to a const reference and rebuilt/re-run, with the initial log retained as `qa-b374-build-initial.log`.

Important binding hashes (SHA-256):

```text
005150e7f0079711b7170ba3db692f1cbf4c5306aa0e5ba6b53dd1c9d79858d3  test/test_remote_transcript.cpp
272b85c194d0a85eb95bc7ca481cf5bbb3e1ac35fcf16f5026cfe5fb97817b01  lib/core/remote_session.cpp
a24dbbf1970e111f27eacb9705e0a76a00dc626cbc8397358d73caaff42b6ee0  lib/core/remote_codec.cpp
b9a4c2a128fb8d925675faaa4cfaeeba92dcf14bc08dc25664c0449097736b59  lib/core/node_mac_rx.cpp
dc2b417c3c41af7198c66e612d0c848d547ff277ae9d83b6f0507fcb319a2d38  src/firmware_remote_executor.h
94029a7d32da24a56249658147dbd2b33ff0b9ed665295cbbaf19aafff5b0ced  vendor/doctest.h
3ce520e5a6c001c98e8a647ce42aa6a55f5609d16cabd367259ddd7825b80ed9  transcript-tests
```

## 4. Remaining contradictions and work

**B374 implementation/proof remains open.** Revision 5 §4.5/§4.8 requires `response_seal_failure`. `RemoteSessionState` (`remote_session.h:255`) still has only transcript_exhaustion and response_enqueue_failure; the sender has no separate seal-failure branch/counter/emit. The existing synthetic case cannot establish §6.1's Node counter obligation. The coder must implement that classification and produce a discriminating, explicitly synthetic Node-path proof, preserving result, chunks, frame count and cursor and avoiding an enqueue attempt. Existing ABI/board checkpoint figures cannot substitute for measurement of the final state, even if an added field may fit padding.

**B375 — revision-5 consistency follow-through (QA-owned, not a new owner ruling):**

1. **Three versus two counters.** §4.8 (`brief:282`) requires three; §4.9 (`:288`, `:293`) and the production fence (`:299`) still say two. Align all operative text to the three named counters and measured overhead. R-RA-34 already covers measured counters/alignment; no new allocation authorization is inferred or requested.
2. **Retry trigger ambiguity.** §4.5 (`:244`) says replay on the controller's retry. Current `radmin_service_once` (`firmware_remote_executor.h:49`) offers a pending frame on every eligible service pass; encoding failure leaves it pending (`remote_session.cpp:550`). Consequently another pass retries without a controller request, and an exact request retry additionally resets to seq0. Clarify that relation explicitly; if a retry-only suspension is intended, it is a different scheduling/state requirement and must be specified before coding. No suspension, new timer, fallback terminal or fairness policy was invented by QA.
3. **Proof reachability.** §6.1 (`brief:343`) groups the forced seal failure with real Node flights but does not explicitly identify its synthetic fault mechanism. The present one-byte-buffer case calls only the pure encoder; the production sender owns a full-size stack buffer (`node_mac_rx.cpp:2124`). Separate the real wrapper classification proof from the synthetic fault and from actual over-air behavior, following B367's existing discipline.
4. **Earlier fold-ins remain contradicted by operative shorthand.** §4.3 (`brief:221`) still calls inbox the ONE context consumer, although §4.7b and `firmware_admin_verbs.h:310` require the ACL consumer. §5 (`brief:328`) excludes every handler except the inbox filter despite expressly permitting the ACL actor binding at `:306`. The opening resume list (`:7`) omits the preserved partial implementation and QA ruling-ledger update; the owner has already explicitly authorized both. §4.10 (`:369`) still allocates B371 after §0.4 allocated B375. Correct these together; preserve the historical tables as history, not current dispatch constraints. After registering this review, next free is B376.
5. **Completion mapping wording.** §4.5 (`brief:243`) names only response-staging failure, whereas design §8.9 includes execution failure too and the pure mapper handles `DispatchOutcome::internal_failure` (`firmware_remote_executor.h:44`). State both completion-time causes without restoring any send-time terminal replacement.

This review leaves revision 5 unchanged and records the corrections for the QA-authored next brief revision. It does not silently bind the coder to new production behavior. The B374 cryptographic conclusion stands independently of these text/proof follow-throughs.

## 5. Full gate and handoff remain pending

The standing gate comes from the current roles, Slice 7b-1 §§6–7, its cited Slice 7a §7/§10b chain, the design §19.1, and the live tools. The ruled normal board pair is **gateway + heltec_mobile**, sequentially, with the warning census's own derived pinned set as the explicit exception. This later owner protocol governs the broader historical D1 board wording. The maintained corpus authority is the 36-row table at `simulation/BASELINE.md:10505`, including s18 `32afbf11` / 269517 rows; these are read anchors, not fresh reproduced results. No anchor edit is authorized.

After the coder finishes and hands over a frozen report, independently rerun: native build **and** real binary; complete mutation union derived from both changed-source and dependency/historical selectors; both simulator variants rebuilt with compile-action receipts and all 36 streams compared to the base/current anchors; full ABI probes and controls; all six standing probes/controls and required no-neg arms; tools unit sweep; generated inventory and authority checker/selftests; warning census, A0/literal checkers and whitespace checks; deterministic board pair with final symbol/section/RAM attribution. Re-derive every pin and require the exact `PIN re-synced? YES — <derivation>` line. No inherited checkpoint is a substitute.

Read-only mutation-map inspection currently finds 13 existing changed-source batteries: radmin3verbs, radmin5session, radmin5rx, teamgrant, b161rx, b251rx, b159rx, b159map, a0rx, sliceBrx, sliceBnode, sliceEnode, sliceGrx. This is **not** the final acceptance union: the three new batteries and unchanged codec/authority/inbox dependencies still have to be derived and included. No selector was executed here.

The coder's reported 2882/127520 native checkpoint, probe/control counts, Node sizes, board costs and base corpus figures remain **coder-reported, independently unverified by this takeover**. No board firmware build, full native run, mutation battery, board/ABI gate, simulator rebuild, corpus run or hardware check was performed. Whitespace inspection passed; all pre-existing inputs remained hash-identical through the focused work. QA documentation additions are this report plus the B374 follow-through/B375 entry in the maintained register. The coder evidence and revision-5 brief are preserved unchanged.

## 6. Focused reproduction receipt

The following exact commands built the existing synthetic test from the preserved snapshot. They are not the full native gate. The separate `vendor/doctest.h` is bound above.

```sh
gcc -std=c99 -O0 -g -c /tmp/mr-qa-s7b1-n87ss3cv/snapshot/lib/monocypher/src/monocypher.c -o /tmp/mr-qa-s7b1-n87ss3cv/monocypher.o
g++ -std=gnu++2a -O0 -g -Wall -Wextra -Werror=switch -fno-exceptions -fno-rtti -DMESHROUTE_NATIVE=1 -DMR_N_LAYERS=2 -DMR_RADIO_CANARY=1 -DPROTOCOL_VERSION=1 -DMR_CONSOLE=1 -I/tmp/mr-qa-s7b1-n87ss3cv/vendor -I/tmp/mr-qa-s7b1-n87ss3cv/snapshot/lib/core -I/tmp/mr-qa-s7b1-n87ss3cv/snapshot/lib/hal -I/tmp/mr-qa-s7b1-n87ss3cv/snapshot/lib/monocypher/src -I/tmp/mr-qa-s7b1-n87ss3cv/snapshot/src /tmp/mr-qa-s7b1-n87ss3cv/doctest-main.cpp /tmp/mr-qa-s7b1-n87ss3cv/snapshot/test/test_remote_transcript.cpp /tmp/mr-qa-s7b1-n87ss3cv/snapshot/lib/core/remote_session.cpp /tmp/mr-qa-s7b1-n87ss3cv/snapshot/lib/core/remote_codec.cpp /tmp/mr-qa-s7b1-n87ss3cv/snapshot/lib/core/identity.cpp /tmp/mr-qa-s7b1-n87ss3cv/snapshot/lib/core/dm_crypto.cpp /tmp/mr-qa-s7b1-n87ss3cv/snapshot/lib/core/frame_codec.cpp /tmp/mr-qa-s7b1-n87ss3cv/monocypher.o -o /tmp/mr-qa-s7b1-n87ss3cv/transcript-tests
/tmp/mr-qa-s7b1-n87ss3cv/transcript-tests --test-case='*failed reseal*' --no-colors=true
```

The additional QA test source is preserved here to keep its proof reviewable if `/tmp` is later removed. Compile the same C++ command with `qa-b374.cpp` in place of `snapshot/test/test_remote_transcript.cpp`, output `qa-b374-tests`, then run only `--test-case='QA B374*'`.

```cpp
#include "snapshot/test/test_remote_transcript.cpp"
TEST_CASE("QA B374 synthetic send refusal preserves the full state and all replay frames") {
    for (const std::string& text : {std::string{}, std::string(207, 'x')}) {
        Pool p; const uint8_t si = p.admit(17); p.finish(si, text);
        std::array<uint8_t, sizeof(RemoteSessionState)> frozen{};
        memcpy(frozen.data(), &p.state, frozen.size());
        const uint8_t later[] = {'n', 'e', 'w'};
        remote_transcript_append(p.state, si, later, sizeof later);
        remote_transcript_complete(p.state, si, RemoteTerminal::internal_error);
        CHECK(memcmp(frozen.data(), &p.state, frozen.size()) == 0);
        std::string first; RemoteTerminal term{}; uint8_t seq = 0;
        const auto original = p.drain(first, term, seq);
        CHECK(first == text); CHECK(term == RemoteTerminal::completed);
        CHECK(original.size() == (text.empty() ? 1 : 3));
        CHECK(p.receive(17).verdict == RemoteAdmitVerdict::replay_transcript);
        memcpy(frozen.data(), &p.state, frozen.size());
        std::array<uint8_t, 1> too_small{}; size_t written = 123;
        CHECK(remote_transcript_encode(p.state, si, too_small, written) == RemoteStatus::bad_buffer);
        CHECK(written == 0);
        CHECK(memcmp(frozen.data(), &p.state, frozen.size()) == 0);
        std::string second; const auto replay = p.drain(second, term, seq);
        CHECK(second == text); CHECK(term == RemoteTerminal::completed);
        CHECK(replay == original);
    }
}
```

Final QA-only source SHA-256: `ca2da0772208ae044b547669e5bb5be22d163c8f98ed1f06ff9fb15050cf46c8`; binary SHA-256: `234ecee9b47badb788d6c431756b7ad1cb8aaed72975f481eac7c59d79382c45`.

## 7. B375 author follow-through — revision 6, 2026-09-08

After the owner relayed source confirmation of B375 and the missing seal-failure counter, QA folded the corrections into revision 6. Its SHA-256 is **`b68785d099cac9fa85361c4d9c3da62ac8d5b938bfd752d2380391eb93c76b9c`** (`docs/superpowers/plans/2026-09-08-radmin-slice7b1-executor-transcript.md`). The coder records that content binding and the actual preserved status before resuming. The coder's evidence remains at its historical revision-4 checkpoint, unchanged by QA.

- Send failure preserves the frozen transcript and cursor; the next eligible main-loop pass retries the pending frame. An authenticated exact request retry restarts the retained completed transcript from sequence zero. No suspension latch, new timer or replacement terminal.
- All operative allocation/fence references now name three saturating u16 counters. Seal refusal increments only `response_seal_failure`, emits its scalar event and never attempts enqueue; transport refusal after sealing increments only `response_enqueue_failure`. Execution/staging failure is decided at completion. Final ABI/board costs must be remeasured under the existing R-RA-34 ruling.
- Brief §6.6 explicitly authorizes a labelled SYNTHETIC fixture-only epoch mismatch through the existing real Node sender and real encoder. Rechecked source: `remote_transcript_next` selects the pending frame without an epoch check (`remote_session.cpp:550`); the encoder refuses the mismatch (`:568-570`); `Node::radmin_send_frame()` calls it with its real full-size buffer (`node_mac_rx.cpp:2124-2126`). The test must restore the fault before recovery, discriminate both failure counters/emits and preserve a nonzero cursor, then prove both retry forms. This mechanism is specified, not implemented or executed by QA in this revision.
- Both context consumers, the authorized ACL handler exception, preserved partial/QA resume inputs, and next-free B376 are consistent. The standing mutation-union requirement explicitly includes both changed-source and dependency/historical selectors. B375 is folded in place in the register; MEMORY carries the durable resume contract.

Documentation-only validation: `git diff --check` passed. SHA-256 comparison against the 921-input pre-edit manifest at `/tmp/mr-qa-r6-docs-tosf6q95/manifest.json` found changes only in this brief, the maintained register, MEMORY and this QA report; no added or missing inputs. All production, test, tool, coder-evidence and pre-existing rulings-ledger inputs were preserved. MeshRoute HEAD remains `d467787f5e02836ad19b79624d97729c81ebacf8`; simulator HEAD remains `06746a97de5764415d6fcef10b97bca90569b9c7`, clean. The initial comparison helper encountered the directory symlink `spec/docs`; the corrected comparison hashes symlink targets as the manifest does.

No build, test, mutation, simulator or full-gate run was performed for this documentation revision. B374's implementation/proof and both full gates remain pending. Revision 6 is ready for coder source-validation and implementation resume; there is no implementation or QA PASS.
