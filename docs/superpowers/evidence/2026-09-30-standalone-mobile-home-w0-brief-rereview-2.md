<!-- Author: Codex (independent QA); 2026-09-30; scoped brief review only -->
# W0 revision 3 — scoped QA re-review

**PASS — authorized for coder source-validation and implementation. W0R-4 is closed.** W0R-1–W0R-3 remain closed; no further fold-in or owner ruling is required.

Authorized [brief](../plans/2026-09-29-standalone-mobile-home-w0-identity-record.md): revision **3**, **585 lines**, SHA-256:

```text
2197165149e58a25b3eeccfc88906e652e69641734d8a532a123ecc42e56a6d8
```

The base remains MeshRoute **`0f23aee8f5eeaa6e176e3ea37b2acaffb33622ec`**, plus the brief's pinned preparation documents and inventoried QA evidence. Simulator **`6585649ea5a780f0542b2931853a667be56a5b2b`** is unchanged and clean. No implementation is present. This is approval of the brief, not an implementation gate or a closure of B440/B448/B482.

## Scope and verified inputs

This review covers only revision 3's disposition of W0R-4 and the associated fence, gate and pin changes. The [previous scoped review](2026-09-29-standalone-mobile-home-w0-brief-rereview.md) already closed W0R-1–W0R-3. The owner's 2026-09-30 ruling chooses its deferral alternative: the broken transcript comparator leaves W0's gates; B487/B488 join B478's tool package after W0 and before W7.

Fresh verification:

| Check | Result |
| --- | --- |
| Brief hash and length | Exact authorized hash above; 585 lines |
| Explicit file hashes / line counts | **22/22 / 16/16** match |
| Earlier evidence checksum sets | Pre-check **36/36**, first review **7/7**, first scoped review **17/17** |
| Inventory at review entry | **2,187 MeshRoute paths; 285 simulator paths** |
| Against the pre-check inventory | Four declared preparation documents changed; **0 missing**; 64 additions, all this brief or W0 pre-check/review evidence |
| Against the previous review's entry | Brief plus four preparation documents changed; **0 missing**; 18 additions from that review's evidence |
| New identity-arm paths | Both absent, as required |
| Brief's local Markdown links | **6/6** resolve |
| Staged changes / simulator changes | None / none |

I reconstructed revision 2 from the previous review's retained revision-1 snapshot and patch, and verified its full hash **`393af1fca965be40f584833fee460488dcf5801623c24ed3a4e8233b673431ca`** before comparing it. The retained [revision delta](2026-09-30-standalone-mobile-home-w0-brief-rereview-2/revision-delta.patch) contains only the declared deferral, dependent wording and refreshed pins. Product sections **§2.1–§2.6** and the identity-arm specification are byte-identical to revision 2. The prior success/publication correction, real-adapter exclusion, owner rulings and selector counts remain intact.

The four preparation hashes match the request and brief. The design, register, tracker and MEMORY consistently assign B487/B488 to the later tool package. No production, test, tool, inventory, platform or simulator input changed from the pre-check base. Detailed hashes, paths and comparisons are in [verification.json](2026-09-30-standalone-mobile-home-w0-brief-rereview-2/verification.json) and [review-inputs.json](2026-09-30-standalone-mobile-home-w0-brief-rereview-2/review-inputs.json).

## W0R-4 disposition

**Resolved by the owner's explicit deferral, with adequate W0-specific replacement obligations.** The revised brief removes the impossible transcript execution from both §4.1 and §4.2. `transcript_main.cpp` is read-only; its actual full pin is `3914de10acddb6a6e39384fa4f512461133e1d7ca538f50cb66939a8cd828163`. Both it and `transcript.py` are excluded from edits by §3/§4.3. The driver's live-name fixture correction moves to the later tool package. The unchanged revision-2 history entry records what that revision required; it is not an active gate obligation.

The replacement proofs are executable at the intended seams and bounded honestly:

- **Identity arm:** §2.7 requires the real `firmware_config.cpp` and `firmware_commands.cpp`, under ACCEPT and CLIENT, excluding both the eight recording stubs and the legacy adapter stand-in. §4.2 explicitly requires byte-for-byte outputs for `cfg set name|lat|lon` and `regen` through the real router, plus `regen` through a supplied `LineSink`. Source confirms dispatch reaches `do_regen(out)` and `handle_cfg_set(line + 8, out)` at `src/firmware_commands.cpp:1590` and `:1617`. The new arm remains work the coder must implement and prove; no existing stub-based check is credited for real config behavior.
- **Legacy regen proofs:** `tools/probe_inbox_verbs/probe_main.cpp::route_ble` (`:391`) constructs the actual `LineSink`, dispatches and flushes it. R2/R9 check exact refusal bytes; R12/R23 check exact success bytes and the other sink's silence; R25/R26 cover absent and full-length names. These are real existing assertions, not merely names in the brief. The live-fixture corrections remain fenced without changing their expected bytes or making `seed_id` install crypto.
- **Controls and linkage:** existing C8–C11 in `tools/probe_inbox_verbs/run.sh:690` onward attack the router's sink choice, regen's own writes, its shared formatter and sink delivery. The retained gate runs their affected legacy arms. The new arm's controls must perturb production code. The ordinary inbox runner does not invoke the deferred transcript; its `build_variant` and the single `rc=0` / `if ! build_support;` boundary still support the unchanged deferred-actions consumer.

These proofs cover W0's changed identity writers and output contract. They do **not** substitute for an all-command comparison of the extracted USB/BLE callers, repair B487/B488, or prove the real BLE `cfg set` adapter (B483 remains outside W0). The future transcript consumer of the stand-in is acknowledged without claiming it currently builds or passes. Fresh source anchors and hashes are retained in [source-review.json](2026-09-30-standalone-mobile-home-w0-brief-rereview-2/source-review.json).

For the coder's §5 “Not run” report, the transcript's reason is the explicit owner ruling in §4.2/§6, **not** an assertion that its predicates are unaffected. That named exception governs the generic reader-audit wording. The freeze must state the deferral and make no claim of transcript PASS or unchanged verified transcript coverage.

## Dispatch and limits

The coder may source-validate and implement revision 3 at the authorized hash. The **product implementation contract is unchanged** from revision 2; the **instrument fence and gate contract change** only by the reviewed transcript deferral and its explicit replacement proofs. The rest of the coder and independent QA gates remain required.

Keep the brief, design, register, tracker and MEMORY byte-identical through the coder's freeze. **Do not flip the brief's review-status header:** this receipt holds the authorization, and such an edit would invalidate its approved hash. No commit is needed to proceed. The coder inventories this receipt and its evidence as additional QA preparation artefacts.

Fresh work in this review: hash and line verification, both inventories, previous evidence verification, exact revision reconstruction/diff, source inspection, links, whitespace and final preservation. **Not run:** transcript, native, corpus, board builds, ABI, probes, mutations, discovery, warning census or metal. This is a scoped document review; no old runtime figure is presented as a fresh gate result.

No new finding was discovered or registered. B478/B487/B488 remain open for their ruled package; B440/B448/B482 remain open until W0's implementation passes independent QA. Only this report and its checksummed evidence folder were added. All pre-existing tracked and untracked inputs are preserved, nothing is staged or committed, and the simulator is untouched.
