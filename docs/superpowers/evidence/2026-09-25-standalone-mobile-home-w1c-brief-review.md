<!-- Author: Codex, independent Quality Agent; reviewed Claude's W1c revision 1; no implementation edits -->
# W1c brief review — PASS with fold-ins

**2026-09-25: PASS with two MINOR text fold-ins, WCR-1 and WCR-2 below. No substantive re-review or owner ruling is needed.**
The production change, test strategy, fence and gate selection are accepted. This approves a dispatch contract,
not an implementation or B447 closure. The brief and every input under review remain unchanged.

## Authorization and exact inputs

Reviewed [revision 1](../plans/2026-09-25-standalone-mobile-home-w1c-no-default-name.md):

`fc76a1b95f12091773f709ea331eaee734bede1b4771c3171b4cb699f8f980ec`

MeshRoute is on `main` at `8360802904f7bd0023279d3da844453d61207ede`; simulator is clean at
`6585649ea5a780f0542b2931853a667be56a5b2b`. All **17** brief input hashes match. The **11** fenced source inputs
match the committed base; the four modified preparation documents and the untracked pre-check/brief are
accounted for. The pre-check's **18** evidence checksums and all brief links verify.

The Author can apply the [exact fold-in patch](2026-09-25-standalone-mobile-home-w1c-brief-review/required-fold-ins.patch).
It makes only the two corrections below and changes the header to revision 2, referring to this receipt for
authorization. **The resulting revision-2 bytes are authorized for coder source-validation and dispatch at:**

`39bfd6f0025b37c65826ac71b3effffbf82df25b7fa05c405c79b957992dfce6`

That is a **prospective** hash, independently calculated from the exact patch; revision 1 is still on disk.
The Author must verify the resulting file matches before handing it to the coder. Do not change the header or
the pinned preparation documents afterwards. No commit is required. A different resulting hash needs explicit
reconciliation; this receipt does not authorize unspecified further edits. The coder inventories this review
and its evidence as the explained preparation additions permitted by brief §1.

## Required fold-ins

### WCR-1 — MINOR: the seam list reads as an incomplete whitelist

**Brief:** §2.3, lines 147–149.

“Use only the existing public seams:” precedes a list that omits setup/inspection calls required by the tests:
`on_init`, `set_name`, `set_crypto_identity`, `test_id_bind_set` and `peer_key_find`. These are existing APIs,
not new hooks. For example, the existing INTRO sender fixture at `test_node_hashlocate.cpp:2176` initializes
and provisions the sender and seeds its binding; the receive fixture at `test_dual_layer.cpp:7941–7943`
uses `peer_key_find(..., &conf)` to prove the required authoritative cache result. §2.3(A) itself requires
`set_name`, which the literal list excludes.

**Correction:** say “Use existing public seams only, including the setup and inspection APIs (...) and ...”,
as spelled in the patch. This makes the list illustrative while retaining the existing-public-API constraint,
the expressly allowed `DualLayerTestAccess::drive_post_ack_intro`, and the prohibition on new production hooks.
No new production path, helper or receiver edit is authorized.

### WCR-2 — MINOR: the radio-probe omission reason is false

**Brief:** §5 item 9, line 451.

The statement that both omitted probes “compile or read no fenced file” is incorrect for `probe_device_radio`:

- `tools/probe_device_radio/run.sh:17–19` names `fw_main.cpp` and `firmware_commands.cpp`.
- Its `structural.py:15–18` reads them, checking radio initialization, completion ordering, RF formatting and
  board identification; `mutations.py:103` and its structural controls read/attack those same files.

The planned edits touch the boot-name and `whoami` **comments**; none of those radio predicates or controls
targets these comments. The current stock structural checker passes **25 checks / 0 failed**. That is a
focused source check, not a full radio-probe run or an implementation verdict.

**Correction:** split the omissions. State that the radio probe reads these files but is unaffected by the
specific comment edits, subject to the already-required final D6 reader audit and re-running any affected
controls. `probe_prov_tx` reads only the unfenced `firmware_provisioning_service.h` and `firmware_config.cpp`
(`probe.py:27–28`), so its existing omission remains justified. The patch supplies this wording; it does not
remove any required gate.

## Independently verified and accepted

**Contract and wire bytes.** `Node::effective_name` has four production call sites. Keeping its signature and
counted-copy contract, removing only default synthesis, preserves full 32-byte names and avoids touching its
callers. `intro_attach_prefix`, `send_hash_bind_pubkey_response`, `emit_hash_query` and `pack_h` support the
specified nameless forms: **33-byte INTRO prefix**, **35-byte 0x8B body**, **40/44-byte H**. The home forward
has a **33-byte body / 34-byte inner**, including its canonical origin byte. The brief distinguishes these
units correctly. The INTRO fit boundary is **232 − 33 = 199 bytes**, with the existing plain-send fallback
and telemetry at 200. No codec, wire-version, NV or Node-layout edit is needed.

**Reachability and cache proofs.** The real producer fixtures and `DualLayerTestAccess::drive_post_ack_intro`
exist in the fenced test files. The latter drives the actual post-ACK receiver; adding `test_dual_layer.cpp`
to the fence is correct. `peer_key_set` updates a retained name only under `name && name_len`; `peer_name_set`
with a non-null pointer and count zero clears it. The proposed refresh-guard mutation can therefore fail C2
on routes 2–5: each passes a non-null name pointer even at zero count. INTRO passes null, so the brief correctly
does not attribute route 1 to that mutation. Unpinned fixtures avoid the earlier pinned-row return. Fresh
nodes, named comparisons, old-default retention, push payloads and stripped INTRO delivery are required.

**Instrument shape.** `probe_inbox_verbs` compiles the real command handler and routes TEXT and JSON/LineSink
on ACCEPT and CLIENT arms. Existing X1/X2 do not inspect the name field, so the new exact-name assertions and
the two router controls add relevant coverage. `s6_ctl` provides the single-match guard idiom; per-substitution
guards address the multi-edit vacuity demonstrated by B449. The lab parser already accepts an empty field;
using the actual new probe lines for the compatibility proof is appropriate. No formatter or test hook is needed.

**Native mutation selection.** AST inspection, without importing/running the harness, reproduces selector (a)
at **6 batteries / 23 existing entries**. Adding `b161hash` supplies **6** dependency entries. All **29** existing
entries match their current source **exactly once**. The dependency is concrete: H05 attacks the existing
key-answer producer, H04 the mobile-answer tail. With the minimum four `w1cname` entries and one `w1cretain`
entry, the prospective union is **9 batteries / at least 34 entries**. These are configured counts, not RED
results. The new controls must still compile and fail on the specified assertions at implementation gate.
The poisoned 40-byte accessor buffers make the specified over-copy control observable without an out-of-bounds
write. The bare cache-name guard matches twice; the refresh line with its comment matches once, as the brief says.

**Inventory and line preservation.** Fresh generation matches the tracked **197-row** inventory byte for byte.
There are exactly **48 / 7 / 1** rows below the designated comments in `firmware_commands.cpp`,
`console_parse.cpp` and `fw_main.cpp`. Preserving those files' lines and all non-comment bytes is a valid narrow
way to avoid regenerating the inventory. The generator and tracked table remain outside the fence.

**Gate and measurements.** P6 requires the same complete specified chain on coder and QA sides for this core
change. The brief supplies native binary execution, fresh stock simulator/corpus, full ABI probes, affected
firmware probes and controls, source checkers, tools discovery, warning census, board measurements and the
explicit mutation union. The six-environment census is the stated exception to the two-board rule.
`measure_board.py` uses fixed per-environment build paths below `.pio-measure/`; its `compare` qualifies the
same-source repeatability pairs, including source snapshot and normal `.pio` metadata. The brief correctly
separates those comparisons from base-to-final field attribution. The before/after pairs and QA's own final
measurement remain future work. RAM and flash predictions are not measured outcomes or allowances.

**Corpus and baselines.** The pre-check's checksummed source census and manifests remain intact: **783 named
nodes / 36 scenarios**, pre-check native **2951 / 195777 / 0**, corpus **36/36 identical**, Node pins
**235208 / 122176 / 157304**. This review checked those inputs and the unchanged executable base; it did not
rerun those baselines. Future W1c remains predicted corpus-inert, with comparison against every pre-check stream
and the current `simulation/BASELINE.md`, not a newly accepted anchor table.

**Scope and landing.** B447's unnamed half alone closes after independent implementation QA. Named-peer
precedence stays open. The retired `0x94` push and unpopulated hosted-name fields are correctly excluded;
B450 remains open. The companion contract and address-book rationale are assigned to the QA landing, with
historical evidence and cached-default fixtures preserved. No new metal-only behavior or owner ruling is needed.

## Evidence, preservation and limits

[Checksummed review evidence](2026-09-25-standalone-mobile-home-w1c-brief-review/) contains the input checks,
source-count results, stock radio structural output, exact fold-in patch and prospective authorization record.
The whole-tree starting inventory is retained under the ignored local archive
`artifacts/2026-09-25-standalone-mobile-home-w1c-brief-review/`; scratch calculations are under
`/tmp/meshroute-w1c-brief-review-20260925-shzqkxih/`. These are local artifacts, not off-machine backups.

Every pre-existing nonignored file in both repositories was checked for preservation. Only this review and its
evidence were added. No files under review were edited, and nothing was staged or committed. WCR-1/WCR-2 are
brief wording findings, not newly discovered production bugs; no new B-number or register edit is made.

No firmware implementation, native/probe compilation, simulator run, mutation execution, board build, warning
census, full tools discovery or metal test was performed in this brief review. The prior pre-check remains its
own historical baseline evidence. The implementation gate remains entirely pending.
