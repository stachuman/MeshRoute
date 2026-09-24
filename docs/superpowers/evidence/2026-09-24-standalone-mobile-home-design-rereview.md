<!-- Author: OpenAI Codex; independent scoped design re-review, 2026-09-24 -->
# Standalone mobile Home — design re-review

**Verdict: HOLD — 2026-09-24.** Seven original findings close. **DR-4 remains open:** the new caller table contradicts the name-prompt return path and does not cover an alarm arriving after a gate refusal. Its post-execution draft-loss wording also needs to distinguish a failure already known from the declared late-failure residual. Three minor fold-ins are recorded below. The original card-clearing blocker is resolved.

Reviewed revision **2.15** under the [scoped re-review brief](../plans/2026-09-24-standalone-mobile-home-design-rereview-brief.md). This is a design review, not an implementation gate. The [first report](2026-09-24-standalone-mobile-home-design-review.md) and all reviewed inputs remain unchanged. No owner ruling D1–D13/D13b is reopened, and no additional owner ruling is requested by this report.

## 1. Inputs verified

MeshRoute HEAD is `20357578700270d7315dcf86bc3bb5725d8024cb`. Simulator HEAD is `6585649ea5a780f0542b2931853a667be56a5b2b`; its working tree is clean. Both match. No production, test, tool or profile changes are present.

| Input | Observed SHA-256 | Result |
| --- | --- | --- |
| Re-review manifest | `281ca93c0f41e5c68de0e0b9ad374eaac956c456ee845a26364e88632c2fe9a1` | Matches brief |
| Design r2.15 | `2a162dcdfd7ce23d7267a9c8f6e02025f9601b946b22845e870bf09975a253dd` | MATCH |
| Source audit | `9c90f644c1413ddd1faec543d97d8827952a9ed74eedd7466ec9a83a9d3c76ba` | MATCH |
| Register | `03119e87bbe4347128defd57d7fba1f5902262235b8713879e47ad2aae9d2b6f` | MATCH; unchanged since first review |
| First review report | `d7c8ffb02ea64a3fd785c0942e7cb58e9c68a6674c3816dacfd61c3452f984fe` | MATCH |
| First review brief | `8404261e617624dca8f95a1820dcccdc176629653ba928878f498d9f078b3e4b` | MATCH |
| First review manifest | `0e3319fa9fd2df423985a93266135dae90302f9193fe6ce4508afb599e7bfab6` | MATCH |
| `tracker.md` — context | `93a49b3e48315f27ed7ceae3c81588e366bd2a347577b772ffffb028f92a6abe` | MATCH |
| `MEMORY.md` — context | `e95ae0e42a092ee56ca496603c9e80103306f68db011f193c0c835c94bcad487` | MATCH |
| Re-review brief — additional receipt pin | `a4962ede8720ce2ea77b259cceffb4eedfa4df5ed6d29aba4f1f03921b0a4964` | Recorded |

Starting status matches the manifest plus the expected new re-review brief and manifest: `B164.md` deleted; MEMORY, register, design and tracker modified; the September 22 preparation documents and September 24 review documents untracked. No mismatch required a STOP. A preservation inventory covers **1,143** pre-existing tracked/nonignored-untracked paths, including absence for the deleted file. Completion checks confirm unchanged contents/absence throughout; this report is the only added path.

## 2. Original findings

| Finding | Disposition | Independent assessment |
| --- | --- | --- |
| **DR-1** | **CLOSED** | §8 now binds the card to the `arr_ch` value produced by its push and compares it with `read_ch`, separately from persisted record numbering. §6.1 rule 10 accurately includes passive/menu-mode Inbox frames and all arrivals frozen by that frame. Mid-frame arrival, restored sequence, deletion gap and wrap cases are in §14. `FrameGate::step/on_page`, `UiInboxCounters` and `ui_route_recv_push` support this correction. The modular comparison has the usual half-range serial-ordering precondition; an arrival serial of zero after wrap must remain valid. |
| **DR-2** | **CLOSED** | `capacity` remains the slot count; `text_max` separately advertises the byte bound. W6 includes `ios-companion/INBOX_SYNC_CONTRACT.md`, and §14 asserts both quantities. The newly added `bad_text` sentence needs the minor DR-9 correction. |
| **DR-3** | **CLOSED, with minor DR-10 fold-in** | The 244-byte bound reproduces; the full set/list output proof and old-160 control are required, and stack neutrality is withdrawn. The remaining issue is the incomplete user census: four functions allocate this buffer, not three. It is a fence/resource-text correction, not a different reply-size decision. |
| **DR-4** | **NOT CLOSED — MAJOR** | Kind-scoped gates, separate content lock/result ownership and preset returns are corrected. The new table still has the specific return/transition gaps detailed below. |
| **DR-5** | **CLOSED** | §6.8 explicitly carries the 64-bit receive timestamp alongside each staged row until `publish`; the quantized snapshot age is not the key. The static staging seam exists. §11 prices eight 8-byte keys, and §14 covers sub-second ordering, equal timestamps and times above 2^32 ms. Exact layout/padding remains a later measurement, as required. Correct the short D13 cost summary under DR-11. |
| **DR-6** | **CLOSED** | The operational contract now says defaults and the diagnostic recur until a successful replacement or erase, with zero boot writes. Historical quotations of “once” are labelled history. D8 remains unchanged in substance. |
| **DR-7** | **CLOSED, with minor DR-11 fold-in** | §4.3 limits on-air rename propagation to unpinned rows; §4.6 and W1c require a fresh cache for the unnamed-display proof and preserve empty-name/cache behavior. One preceding absolute claim needs qualification to agree with that new precondition. No migration or default-string recognition is requested. |
| **DR-8** | **CLOSED** | Savings are 26/26/27 bytes; the capped card header is 17 cells; EDIT-01 uses uppercase `RETURN TO BASE NOW`, while the saved default remains mixed case. |

### DR-4: remaining required corrections

1. **Name-result return must respect its opener.** The new §7.4.1 “Executed and accepted” cell (line hint 698) sends every acknowledged `NAME SAVED` to My device. §4.4 requires a successful name save entered from the unnamed JOIN/CREATE prompt to continue into the chosen setup step. Name is one editor caller with multiple origins. Make that cell explicitly origin-dependent, and carry both paths into the caller-table acceptance cases. This is a contradiction introduced by the new table, not a request to change D10/D11.

2. **An already-drained gate refusal has no `long_fire` transition.** The definition at §7.4.1 line 685 says “executed” requires passing `send_gate_of` and returning from the executor. Consider: `SEND` → drain → gate rejects → refusal result visible → `long_fire`. The request is no longer queued, so the “queued, not yet executed” row does not apply; it never passed the gate, so “after execution” does not apply either. This is a real distinction in the current seams: `UiModel::take_send_request` clears `_req_pending` before `ui_perform_send` checks the gate (`firmware_ui_model.h:3165–3166`; `firmware_ui_send.h:582`). Add an explicit known-zero-submission refusal state/transition. Preserve the draft and require a fresh review after the alarm; no executor call may be reconstituted by dismissal.

3. **Separate known refusal from the late-failure residual.** “Executed” currently includes a returned synchronous `err_*`, and the new `long_fire after execution` cell releases every written draft. It therefore also drops text after an already-visible synchronous refusal or an already-received `NO KEY`, although the explanation describes only a failure arriving after tracking was abandoned. State the disposition for already-known unsent failures separately from an accepted/unknown submission. Recommended: retain the known-refused draft for fresh review; keep the expressly declared residual for an accepted submission whose outcome is still unknown when pre-empted. If broader loss is intended, describe it explicitly instead of calling all these cases “the submission stands.” Pin the retain/release predicate to typed outcomes in the later brief; `send_failed` alone is not proof of non-delivery (`SendTracker::match_dm` also receives `e2e_ack_timeout`, and core flight failures include lost-ACK cases).

The added §14 requirement to test every caller-table row is useful, but a test cannot infer the missing rows' expected behavior. Reconcile §§5.5–5.6/7.4.1, W7/W8 and the affected acceptance entries before issuing implementation briefs. Only these changed transitions and their consequences need another read.

### Assessment of the two disclosed behavior choices

**Withdrawing an unexecuted written request:** sound as a proposed safety rule. It keeps typed bytes for fresh review and does not modify the saved-phrase or emergency policy. The implementation must clear only that pending ordinary request, unlock its draft and preserve the emergency request. This is a named behavior change for the new manual kinds, not a refactor. The real current `queue` and `take_send_request` already keep emergency and ordinary pending state separate. Saved phrases retain current priority/drain behavior; “behind the alarm” must not be implemented as a new timer or an added wait for the entire overlay to disappear.

**Losing a later outcome after an executed send is pre-empted:** the disclosed observation limit is supported by `close_compose` and `ui_pump_trackers`, which abandon the normal tracker when its view closes. It must never be presented as proof that the message failed or never aired. I do not require a new persistent outbox or another tracker. The remaining DR-4 correction is to avoid conflating this late/unknown case with an already-known refusal. No owner ruling is reopened by distinguishing them.

## 3. New minor fold-ins

Line numbers are navigation hints at the pinned tree; symbols and sections are authoritative.

| Finding | Severity | Design § | Finding | Evidence (`file:symbol`, line hint) | Required correction | OWNER RULING REQUESTED |
| --- | --- | --- | --- | --- | --- | --- |
| **DR-9** | **MINOR** | §7.7 | The new sentence “`bad_text` then means more than `text_max` bytes” narrows a multi-cause reason to length alone, contradicting the retained grammar. | `src/firmware_ui_presets.h:validate_preset_text` (329–340); `ios-companion/INBOX_SYNC_CONTRACT.md` reason table (1268). Empty/all-space text and forbidden bytes also produce `bad_text`. | Say that the **overlength arm** uses `text_max`; retain absent/empty/all-space and forbidden-byte meanings in the companion contract. No parser behavior change. | No |
| **DR-10** | **MINOR** | §§7.7/11.1/13, r2.15 disposition | The claimed complete census of three `kPresetLineMax` buffer users misses `preset_boot_restore`. It also allocates `char b[kPresetLineMax]`; raising the constant reaches the boot-diagnostic path. | `src/firmware_ui_preset_verbs.h:preset_emit_record` (183), `preset_emit_list` (193), `preset_emit_err` (200), **`preset_boot_restore` (234)**. | Add the fourth user to W6's explicit audit/fence and the boot-stack accounting/verification, or explicitly derive a separate boot diagnostic bound if the later brief chooses that implementation. Keep exact call-path stack usage a measurement; the +84 B is per widened buffer, not a demonstrated peak-stack delta. | No |
| **DR-11** | **MINOR** | §§4.6/11.1/12 | Three short summaries lag their corrected bodies: §4.6 still says ignored empty names “cannot leave a stale one behind”; D13's cost cell still says approximately 8 B without distinguishing the new ordering keys; the Home-card resource row still calls its field `seq`. | The new §4.6 fresh-cache precondition; `lib/core/node_hashlocate.cpp:peer_key_set` (343–346); corrected §6.8/§8/§11.1. | Qualify the stale-name sentence to the fresh-cache/new-format behavior; separate the +8 B boot boundary from the estimated +64 B key payload, before padding; call the card field its session arrival serial. These are consistency corrections, not new allocation permission. | No |

## 4. Independently verified

- Re-read the changed sections and their resource, package, acceptance and metal consequences, following references to source where necessary. Rechecked the source audit's added/corrected rows: passive Inbox frames do advance the arrival watermark; published Inbox rows contain seconds of age; the preset capacity field is slots and the current output buffer is 160. These audit corrections are supported.
- Read `UiInboxCounters`, `FrameGate::step/on_page`, `ui_route_recv_push`, `inbox_row_cb`, `InboxRowBudget::add/publish`, and the static staging instance in `fill_inbox_rows`. A disposable Python calculation of the proposed modular card predicate passed **8 explicit cases**, including equal watermark, older frame and wrap to zero. This exercises the specification's arithmetic, not production card code. Record-sequence independence is established by the new contract and source domains, not by a firmware run.
- A disposable model of the proposed 64-bit ordering rule passed **5 cases**: both sub-second kind orders, an exact tie, timestamps above 2^32 ms and the current-boot/old-history split. Eight timestamps require **64 bytes of payload**; actual struct padding and linked RAM were not measured. `UiSnapshot` need not carry these keys because the existing static budget stages rows before publication.
- Re-derived the existing JSON schema with 163-byte valid text: emergency **243 wire bytes / 244 with NUL**, channel8 **242/243**, dm8 **237/238**. An end record with the proposed `text_max`, maximum generation and maximum active counts needs **111 bytes including NUL**, so it does not change the largest bound. No quoting expansion occurs for canonical preset text. A source census found **four** local `kPresetLineMax` arrays, as DR-10 records.
- Read `send_gate_of`, `ui_perform_send`, `take_send_request`, `queue`, `compose_gesture`, `close_compose`, `ui_pump_trackers` and `mr_ui_tick`. These independently establish the distinction between pending, drained-but-refused and executor-returned requests, the emergency priority, and closure of normal tracking with its view. The new manual kinds and editor still have no implementation to exercise.
- Rechecked `peer_key_set`'s pinned early return and nonempty-name update guard. The fresh-cache qualification is correct. Rechecked the 17-cell card example and uppercase metal typing text. No changes to owner choices or earlier PASS evidence were inferred.

## 5. Not verified and preservation

No firmware build, native suite, corpus run, probe, mutation, board ABI measurement, linked RAM/flash measurement or hardware run was performed. None is required by this scoped design-review brief. The arithmetic/order calculations above are disposable specification models, not implementation tests. This review does not claim that the proposed +64 B is an exact allocated delta or that any stack peak has been measured.

Unchanged sections retain the first review's coverage; this is not a new full-design gate or a production gate. The author-supplied owner decision record was not independently authenticated against its originating conversation.

All reviewed hashes were rechecked at completion. The simulator remains clean; all 1,143 preservation entries retain their original content or absence; whitespace and report-link checks pass. No reviewed document, register entry, tracker, memory, production file or instrument was edited. Nothing staged or committed.

**Register rows added: none.** The findings are corrections to the design under review, so they belong here under the review fence. **B335 remains OPEN; next free is B449.** Return DR-4 and DR-9–DR-11 to the author. DR-9–DR-11 are text/fence fold-ins requiring no separate substantive re-review; DR-4 needs the narrow transition/return re-review described above. Commits are not a blocker.
