<!-- Author: Codex, independent QA; scoped brief review, no implementation -->
# W0 identity-record brief revision 2 — scoped re-review

**Verdict: HOLD on W0R-4. W0R-1, W0R-2 and W0R-3 are CLOSED.** The requested corrections are present and consistent, but the newly required transcript gate does not run on the unchanged base. Its underlying fixture also misrepresents USB admission. **No implementation hash is authorized.**

Reviewed [brief](../plans/2026-09-29-standalone-mobile-home-w0-identity-record.md): revision 2, **562 lines**, SHA-256 **`393af1fca965be40f584833fee460488dcf5801623c24ed3a4e8233b673431ca`**. MeshRoute **`0f23aee8f5eeaa6e176e3ea37b2acaffb33622ec`**; simulator **`6585649ea5a780f0542b2931853a667be56a5b2b`**, clean.

The owner rulings, product fence, console bytes, record format, one-package shape and P6 gate split remain accepted. The defect below is in the proposed gate, not a reason to reopen those decisions.

## 1. Pins, delta and preservation

- At entry, all **22 SHA-256 pins**, **16 line counts**, both absent new paths and **six links** verified.
- The **36 pre-check checksums** and **seven first-review checksums** verified. Neither historical evidence set changed.
- Reconstructed revision 1 by reversing only the declared revision-2 changes. It is **508 lines** and reproduces the exact previously reviewed hash `638f11d693568adbe8eb62d0ca04e623b5cd3ff463860cc12aa44c4f71161235`. The resulting patch contains the specified W0R-1–W0R-3 corrections, additional boundary/control wording, pins and revision history only.
- Reconstructed the previous design and register from their declared edits; both reproduce their first-review hashes. Design changes are the coalesced-publication wording, W0 status and history note; the register change at entry is only W0's §0 status. Tracker and MEMORY remain byte-identical to the first review.
- Entry inventory: **2,169 MeshRoute paths**, **285 simulator paths**. Against the pre-check, only the four preparation documents differ; against the first review's entry inventory, only the brief, design and register differ. Additions are explained pre-check/review evidence. No production, native-test, tool or simulator input changed; nothing staged.
- An initial census classified the `spec/dv_dual_sf.lua` file symlink by its Git index rather than the prior inventory's dereferenced bytes. Rechecking with the original convention proved identical bytes; this was an inventory-method mismatch, not a changed input.

Evidence: [verification.json](2026-09-29-standalone-mobile-home-w0-brief-rereview/verification.json), [revision-delta.patch](2026-09-29-standalone-mobile-home-w0-brief-rereview/revision-delta.patch), [preparation-deltas.json](2026-09-29-standalone-mobile-home-w0-brief-rereview/preparation-deltas.json), and the reconstructed revision-1 brief. Entry and final inventories distinguish the subsequent M1 register additions below.

## 2. Disposition of the original findings

| Finding | Verdict | Rechecked correction |
| --- | --- | --- |
| W0R-1 | **CLOSED** | §2.2 explicitly publishes the requested counted name on both success paths. The already-durable path writes zero times and may update live state; failed saves publish nothing. §2.7 adds the divergent-name case and early-return control. §4.3 and design §4.3 now require durability before publication rather than an unnecessary physical write. |
| W0R-2 | **CLOSED** | §2.7 authorizes the signature-correct stand-in for legacy command-TU builds; the real identity arm excludes it and links the real config adapter/service. The ledger and fence include it. The immutable builder/source boundaries remain. Transcript execution is now explicit in both gates; the newly measured feasibility problem is W0R-4 below. |
| W0R-3 | **CLOSED** | Selector (b) is 78; selector (a) is 46; their existing union is 124 before additions. The selected batteries and gate duties are unchanged. |

The real-router B448 boundaries and rejection of crashed, unbuildable or vacuous controls are also folded in. The legacy stand-in is not evidence for the new identity service: that proof must resolve the production definitions, as §2.7 requires. These are accepted contract corrections, not claims that the future code already passes them.

## 3. W0R-4 — MAJOR: the transcript gate is broken before W0 and cannot be repaired inside the current fence

**Brief anchors:** §2.7 legacy binding and immutable `transcript.py`; §3 OUT; §4.1 step 5; §4.2 affected probes; §4.3's STOP on a `transcript.py` change.

**Source:** `tools/probe_inbox_verbs/transcript.py::{HEADER,default_flags,build_and_run,verify_profile,main}`; `transcript_main.cpp`'s generated-header include and feature-report line; the live command-TU link recipe in `run.sh::build_variant` and `LDWRAP`; `src/fw_main.cpp::service_console`.

The stock command was run on the unchanged product/tool tree, with all outputs outside the repositories:

```text
python3 -B tools/probe_inbox_verbs/transcript.py --out <external-new-directory>
exit 2
TRANSCRIPT REFUSED: transcript driver build failed:
.../mr0c_adapters.h:111:46: error: 'mrble' has not been declared
```

The generated BLE tail now refers to `mrble::kLineStorageBytes`. The transcript build does not bring in its declaration. This is unrelated to the future W0 adapter: there is no W0 implementation yet.

To avoid returning only the first symptom, I used **labelled scratch diagnostics**, preserving every repository source:

1. Forced inclusion of the real `src/device_ble.h` only for the scratch driver. Compilation succeeds, then the existing link command fails on `remote_action_print_status`, `remote_action_prepare`, `__real_crypto_wipe` and `__real__ZN9meshroute4Node10on_commandERKNS_7CommandE`.
2. Added the existing remote actions/client TUs and the inbox runner's two actual `--wrap` flags to the scratch link. The diagnostic binary then links and exits 0, emitting **200 matrix rows**. This is not a repaired stock tool or a gate PASS.
3. Passed that output to the unchanged `verify_profile`. It refuses the three reported macros because the current `full_headless` inventory profile requires five, including `MR_FEAT_RADMIN_ACCEPT` and `MR_FEAT_RADMIN_CLIENT`.
4. Inspected the emitted USB observations: **all 162 rows longer than seven bytes** return `> err bad_line too_long`. The generated wrapper takes `const char* line`; the copied production tail evaluates `sizeof(line)-1`. That is **7** on this host, whereas production's actual array gives **1023**. A separately compiled query using the real `local_command_max_bytes` confirms `wrapper_limit=7 actual_array_limit=1023`. Eight-byte `acl list` is one concrete affected row.

The last problem is not cured by making the tool compile or by printing the extra profile macros. A byte-exact copy of a body is not a faithful execution when its surrounding variable has a different type.

Also, the stock CLI currently has no profile-selection argument; it builds the fixed `full_headless` arm. `build_and_run(profile=...)` changes the generated matrix and validator expectation, but `default_flags()` still supplies its fixed defines. A requirement to run “the transcript profiles” needs a named, executable scope; a larger matrix must not merely be projected over the same binary.

These pre-existing defects are registered as **B487** (transcript build/profile drift) and **B488** (USB capacity fidelity). Stock compilation fails loudly; neither row claims a current false PASS. The diagnostic binary's exit 0 is expressly not the stock tool's verdict.

### Required correction

The brief cannot both require a passing faithful transcript and prohibit the file that builds its dependencies, wrapper and profile check from changing. Settle that instrument contract before dispatch:

- **Recommended:** fence a small instrument-first preparation step for `transcript.py` and the already-fenced driver, with named regression tests and discovery. Restore the real declarations/link dependencies, preserve the actual USB array extent/context, and keep profile validation fail-closed. Define the gate's profile scope explicitly; the existing `full_headless` scope is sufficient unless the author deliberately specifies additional independently compiled profiles.
- Establish the stock transcript baseline after that instrument repair and before W0 production edits. Require the same executable/profile scope in the final coder and QA gates. A command longer than seven bytes must reach the real seam under the real capacity; a control restoring the pointer-capacity mistake must fail. Keep the identity-service proof on the real config TU and preserve the legacy sink/recording contracts.
- If this instrument is deferred instead, explicitly remove the impossible stock-run obligation and name the existing real-router/LineSink and new identity-arm proofs that replace W0's required coverage. Do not silently skip the transcript, claim that an unavailable historical comparator passed, or describe it as unchanged verified coverage. That alternative needs a scoped QA review of the revised gate.

No production change, fake numeric-format change, new resident allocation or reopening of B482/B478 is requested by this finding. The current review does **not** itself authorize either tool repair or a gate reduction. The author must reissue a consistent fence/gate and refresh the register pin.

This corrects **my previous review's omission**: I requested transcript-profile reconciliation without first running the stock transcript on the current base. The author accurately adopted that request; W0R-1–W0R-3 remain closed.

## 4. Measurements and limits

The retained [reproducer](2026-09-29-standalone-mobile-home-w0-brief-rereview/reproduce-transcript-drift.py) was itself run afresh in `/tmp/mr-w0-transcript-rereview-proof` and reproduced the stock failure, missing link inputs, profile refusal and 162/200 capacity result. It compiles the unchanged real sources and confines its declaration/link supplements to scratch. Its source header labels this as diagnostic work, not a repair or gate.

Compact receipts and unique failure captures are in this evidence folder; complete runtime/build logs are under ignored `artifacts/2026-09-29-standalone-mobile-home-w0-brief-rereview/`, indexed and hashed by `raw-logs.json`. Scratch objects remain outside both repositories. The generated adapter body and production files were never edited.

Fresh checks: input hashes and checksum sets, exact prior-content reconstruction, source/reader inspection, stock transcript invocation, diagnostic compile/link/run, real profile validator, compiled capacity query, whitespace and final preservation.

**Not run:** native suite, corpus, board pair, ABI sweep, full inbox/default probes, mutation batteries, warning census, tools discovery or metal. Historical pre-check counts are not presented as new results. No W0 implementation or full implementation gate has begun.

## 5. Handoff and register change

Only this review's evidence and the M1 finding registration changed. The brief, design, tracker, MEMORY, previous evidence and all production/test/tool inputs remain byte-identical to review entry; the simulator remains clean. Nothing staged or committed.

The register matched its brief pin **`6e7200403b4ba1b52ed91964a09aab10eb662d40d8a01bd47aca5c26b8104d94` at entry**. After adding B487/B488 and advancing the next-free marker to **B489**, its hash is **`d4913862c0b828e09d42026b63c26cd70fc815b410e3d86413fb26e35c53a5e8`**. Its W0 dispatch paragraph and earlier findings are otherwise unchanged. This declared QA addition must be included in the author's next pin refresh; it is not an unexplained coder input change.

Return only W0R-4's instrument/gate disposition and associated pin/fence changes for scoped review. **No dispatch authorization is issued for revision 2.** B440/B448/B482 remain open pending implemented, independently gated W0 work.
