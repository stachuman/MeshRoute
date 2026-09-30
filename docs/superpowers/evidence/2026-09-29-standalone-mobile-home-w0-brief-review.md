<!-- Author: Codex, independent QA; brief review only, no implementation -->
# W0 identity-record brief revision 1 — independent review

**Verdict: HOLD.** W0R-1 and W0R-2 require contract/fence clarification before dispatch. W0R-3 is an arithmetic fold-in. Re-review the changed sections and refreshed pins only. **No implementation hash is authorized.**

Reviewed [brief](../plans/2026-09-29-standalone-mobile-home-w0-identity-record.md): revision 1, **508 lines**, SHA-256 **`638f11d693568adbe8eb62d0ca04e623b5cd3ff463860cc12aa44c4f71161235`**.

MeshRoute base **`0f23aee8f5eeaa6e176e3ea37b2acaffb33622ec`**; simulator **`6585649ea5a780f0542b2931853a667be56a5b2b`**, clean. The owner's B482 fold-in and separate B478 package are accepted. Neither needs another ruling. One W0 fix package remains appropriate; no commit prerequisite is introduced.

## 1. Input verification

- All **22 SHA-256 pins** and **16 line counts** in §1 match. Both proposed new tool files are absent.
- All **36 pre-check checksum entries** verify, including its receipt and whole-tree inventory.
- Against the pre-check's **2,123-path** MeshRoute inventory, exactly the four declared preparation documents changed: design, register, tracker and MEMORY. No path is missing. The **38 additions** are the pre-check report/folder and this brief; none is unexplained. This review's outputs are added afterwards.
- All **285 simulator inputs** match; its HEAD and clean status match. Nothing is staged in MeshRoute.
- All five relative Markdown links in the brief resolve.
- Read the rules, r2.24 changes, relevant source and probe consumers independently. The source—not historical test figures—determines the findings below.

Evidence: [verification.json](2026-09-29-standalone-mobile-home-w0-brief-review/verification.json), [review-inputs.json](2026-09-29-standalone-mobile-home-w0-brief-review/review-inputs.json), [source-audit.json](2026-09-29-standalone-mobile-home-w0-brief-review/source-audit.json), and [source-excerpts.txt](2026-09-29-standalone-mobile-home-w0-brief-review/source-excerpts.txt). Final preservation and checksums accompany them.

## 2. Required corrections

### W0R-1 — MAJOR: durable equality does not imply that the requested name is already live

**Brief anchors:** §2.2 steps 3–5 (:164–170), §2.7's live-versus-durable and no-op proofs (:243–261), and the §4.3 publication STOP. **Design:** §4.3 transaction and its new W0 paragraph.

The brief says an exactly equal durable candidate returns `unchanged` with zero writes. Publication is specified only in the **otherwise/save** branch. Those rules omit a successful rename whose requested bytes already exist in NV but differ from the running name.

Concrete counterexample within the brief's explicit live-versus-durable disagreement test domain:

| Fact | Value |
| --- | --- |
| Live name | `Live Name` |
| Successfully loaded durable name | `Saved Name` |
| Requested name | `Saved Name` |
| Seed and both coordinates | Identical in live state and the durable record |
| Unused name bytes | Zero in both complete records |
| Candidate versus durable record | All **80 bytes equal** |

Step 3 therefore selects `unchanged`, and the console prints success, but step 4's `Node::set_name` is not reached. The running name stays `Live Name`. `Node::effective_name` reads the Node's stored name, not NV; reading/comparing an IdBlob does not publish it. The current console arm at `firmware_config.cpp:274–282` does publish the requested name after a successful write.

This is a **synthetic contract counterexample**, not an observed failure of an implemented W0 service. No W0 service exists yet. The independent byte calculation in `source-audit.json` establishes the equality branch; it does not claim firmware execution or that ordinary successful console commands alone produce the divergence.

**Required revision:** define both success paths explicitly:

1. A successful load proving that the complete candidate is already durable may coalesce the write.
2. Both successful paths—already durable or newly saved—must ensure that the requested counted name is live before returning success. `unchanged` describes the durable write decision, not necessarily the prior live name.
3. A failed save publishes nothing. Update the publication STOP and the design's “only after save succeeds” shorthand to recognize the already-durable success path, without permitting publication before a required write succeeds.
4. Require a real-service/router case with the table's divergent names, zero writes and the requested live name afterwards, plus a control that returns early from the equal-record branch before publication. Keep the existing live-equals-request but missing/bad/different-record repair cases.

This clarifies the requested rename/no-wear behavior; it does not need a new owner product ruling. It also tightens **my pre-check's** shorthand “identical durable candidate = successful no-op,” which did not spell out publication when the live name differs. The sealed pre-check is left intact.

### W0R-2 — MAJOR: the new cross-TU adapter lacks a declared binding for the existing probe builds

**Brief anchors:** §2.1's adapter in `firmware_config.cpp` (:144–154), §2.5 regen, §2.7's narrow stub exclusion (:231–240) and reused builder (:270–275), and §3's restricted probe fence.

The production shape is sound: the config TU owns the live snapshot adapter, and `do_regen` in the command TU consumes it. But adding that external call changes the link requirements of every existing build of the command TU, not just the new identity arm:

| Consumer | Current construction |
| --- | --- |
| Inbox ACCEPT, CLIENT and OLED | `run.sh::build_variant` (:346–359) links commands, inbox, remote actions/client, probe driver and support objects; **no config TU** |
| Transcript profiles | `transcript.py` (:332–365) independently compiles commands/inbox and a driver including `probe_main.cpp`; **no config TU** |
| Deferred actions | `run.py` (:140–148) reuses `build_variant`; its `probe.cpp:4` includes `probe_main.cpp`; **no config TU** |

`probe_main.cpp:270–279` currently supplies eight recording config-handler stubs. It has no binding for the proposed new adapter. Therefore the requested external call needs a new link provider on those legacy arms. This is a source/link-dependency conclusion about the proposed change, **not a claim that the current unmodified probes fail**.

The brief authorizes only a narrow exclusion of the existing handler stubs for the identity arm. That does not explicitly authorize an adapter stand-in for the other arms. Linking the real config TU indiscriminately instead collides with the handler stubs and changes their dispatch-ownership measurement. Avoid forcing the coder to choose an unmentioned test boundary or duplicate the production snapshot logic in `do_regen`.

**Required revision:** choose and fence the legacy binding. A small option within the already named files is:

- Explicitly allow one labelled, signature-correct adapter stand-in in the shared fixture for **legacy arms only**, using live fixture fields and the real pure conversion; no NV read, identity/crypto installation or new production hook.
- Exclude that stand-in, together with the eight config-handler stubs, in the real-config identity arm. That arm must link and exercise the actual adapter/service definitions from `firmware_config.cpp`; it must never satisfy the new proof through the stand-in.
- Keep the legacy handler recording behavior, transcript sink contract and deferred-actions builder/source unchanged. Name the binding in the disposition ledger and include the legacy ACCEPT/CLIENT/OLED, transcript profiles and deferred-actions consumer in the link/proof reconciliation.
- Make the new identity-arm controls perturb the **real production adapter/callers**, not the fixture stand-in. A native helper test alone cannot prove which live fields the firmware adapter gathered.

Another explicitly fenced approach is acceptable if it preserves those boundaries. The product helper/service need not move, and no new production TU, shared-fake edit or deferred-actions source edit is requested. This is also a gap in **my pre-check**: it verified the current real-config arm's feasibility without accounting for the additional external symbol once regen consumes the new adapter.

### W0R-3 — MINOR: 124 is the union, not selector (b)

**Brief anchor:** §2.8 mutation list, :286–290.

Independent AST-only census, without importing/executing the mutation harness:

| Selector | Batteries | Existing entries |
| --- | --- | ---: |
| (a) | devicenv | 46 |
| (b) | config 32 + w1cname 4 + consoleline 12 + radmin4verbs 30 | **78** |
| Union | All five, before W0 additions | **124** |

Correct the trailing “— 124” on selector (b) and state the union separately. The chosen batteries, both gate duties and the native/addition derivation remain unchanged.

## 3. Accepted scope, contract and gates

- **One conversion:** IdBlob is 80 bytes with the pinned field offsets; the helper can live beside it above the platform branches. It must validate the requested `size_t` before narrowing, copy all live fields and deterministically zero unused name bytes. An empty live name is needed for unnamed-device snapshots. This adds no NV-layout or wire change.
- **Live authorities:** the running seed, counted `effective_name`, and global coordinates are the right snapshot sources. The two position mirrors are explicitly published together after a successful coordinate save under B482. Coordinate parsing and their unconditional write policy remain intact.
- **Rename contract:** the typed service belongs in W0 for W7 to consume. Full-record equality after a successful load is the correct coalescing predicate, subject to W0R-1. Live-name equality alone must never suppress record repair. Raw `save_id` stays unconditional.
- **Console bytes:** the proposed success, `too_long`, `bad_args` and `nv_save_failed` spellings agree with the current cfg family. Preserve raw spaces and literal quotes. Do not add Unicode/repertoire validation. Carry the pre-check's 33-byte UTF-8 boundary and >255-byte counted-input cases into the real-path coverage as well as the helper tests; diagnostics must remain byte/hex-safe while B478 is open.
- **Regen:** keep remote-debt admission before entropy, the checked `save_id` inside `do_regen`, installs after it, and both sink/warning properties. The structural S37/S48 readers really require the save there. The fixture corrections must install live name/position without changing `seed_id` into a global crypto installer.
- **Real-path proof:** compiling the two unedited production TUs under ACCEPT and CLIENT is the appropriate way to prove the service and the console wiring. Existing NV storage holds an 80-byte record and exposes separate readable/writable facts. The new arm is not evidence about the out-of-scope BLE adapter. Preserve crash/build/vacuous-control rejection, not just a nonzero exit.
- **Fence:** the four production files, existing native test, new local driver/shim, builder/fixtures, native mutation additions and generated inventory are appropriate, subject to W0R-2. The new files must be included in the final freeze inventory. Boot, nRF read policy, BLE adapters, leaf-name setters, shared fakes, core and simulator remain outside.
- **Readers:** retain board-UI's 592 identities and real wiring predicates; console-sink's write-set predicates; radio/provisioning/ownership checks; and the inbox/deferred/transcript builder contracts. Both config and command TUs contribute inventory anchors. A stock regeneration with 197 unchanged semantic rows and only line movements is preferable to contorting code to retain old line numbers.
- **Allocation:** zero owned resident state is a reasonable constraint; IdBlob size/alignment stay 80/4. Stack scratch is not a peak-stack assertion. Compiler-frame measurements and linked RAM/flash attribution on both boards are required; gateway is not predicted byte-identical.
- **Gate mechanics:** the stock CMake build emits `<build>/orchestrator/lus`, matching the brief. Native binary execution, full per-stream corpus comparison, stock ABI plus the separate IdBlob overlay, default affected probes, discovery, checkers, warning census and justified mutation union are present. The two per-source board runs and per-board manifest comparisons match the tool's repeatability contract. Base-to-final attribution is correctly distinct from repeatability comparison.
- **Independent gate:** P6's src-only split is appropriate: native, corpus, boards, touched devicenv and affected probes/ABI are rerun independently. Dependency union, full discovery and warning census remain coder duties. No metal, full feature-matrix build or unrelated production change is added by this review.

## 4. Work performed and limits

Fresh work for this review: both repository/pin checks; all pre-check checksum checks; full inventory reconciliation; preparation diff and link checks; source and reader inspection; AST-derived mutation counts; and the explicitly synthetic 80-byte equality counterexample.

**Not rerun:** native, corpus, board builds/measurements, ABI compilers, full probes or their controls, mutation batteries, tools discovery, warning census, or hardware. They were run as identified in the sealed pre-check and are future implementation-gate obligations; this review does not claim their historical figures as fresh results. No implementation was attempted. No linker failure was manufactured or presented as a failure of the present tree.

No new existing production/tool defect was found requiring another B-number. W0R-1–W0R-3 are findings against the proposed brief, returned to its author here. The pinned register remains unchanged; next free remains **B487**. B440/B448/B482 are not closed by this review.

## 5. Handoff

Claude should fold in W0R-1–W0R-3, align the affected design wording for coalesced success, and refresh the declared preparation pins. QA's next review can be scoped to those changes and their hashes. The owner rulings, one-package shape, existing console grammar, record format and gate split are accepted.

**Revision 1 is not authorized for dispatch.** All reviewed files and existing uncommitted work are preserved; only this review's receipt and evidence are added. No files staged or committed; simulator unchanged.
