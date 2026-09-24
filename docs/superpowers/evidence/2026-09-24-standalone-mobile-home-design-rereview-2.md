<!-- Author: OpenAI Codex; independent narrow design re-review, 2026-09-24 -->
# Standalone mobile Home — second design re-review

**Verdict: PASS with fold-ins — 2026-09-24.** **DR-4 is CLOSED. DR-9–DR-11 are present and CLOSED.** One minor terminology correction, DR-12 below, needs folding into §7.4.1 before implementation briefs; it needs no further substantive re-review. No design HOLD remains.

This reviews revision **2.16** under the [second re-review brief](../plans/2026-09-24-standalone-mobile-home-design-rereview-2-brief.md), limited to the remaining transitions and their consequences. Earlier findings retain their recorded closure. This is a specification verdict, not authorization to bypass package pre-checks, allocation measurements, implementation briefs or implementation QA.

**Owner confirmation received in this review turn:** “I do confirm claude decisions.” This includes the queued-written-message press rule, the Home return when an alarm interrupts a successful setup-origin name save, and the disclosed loss of later outcomes after an accepted request's view is closed. These choices are settled; this report does not reopen them or D1–D13/D13b.

## 1. Inputs verified

MeshRoute HEAD: `20357578700270d7315dcf86bc3bb5725d8024cb`. Simulator HEAD: `6585649ea5a780f0542b2931853a667be56a5b2b`, clean. Both match the brief. No production, test, tool or profile changes are present.

| Input | Observed SHA-256 | Result |
| --- | --- | --- |
| Second re-review manifest | `dd4becdd3a1955e953e053d1b2281db0ebbdaa96efd5cadfbbbc8fc83ea10e3c` | Matches brief |
| Design r2.16 | `0dcffc6904ade5a843bb94474c792858d8b75e1cbe3f403911d9dee60423ebaa` | MATCH |
| Source audit | `9c90f644c1413ddd1faec543d97d8827952a9ed74eedd7466ec9a83a9d3c76ba` | MATCH |
| Register | `03119e87bbe4347128defd57d7fba1f5902262235b8713879e47ad2aae9d2b6f` | MATCH |
| First review report | `d7c8ffb02ea64a3fd785c0942e7cb58e9c68a6674c3816dacfd61c3452f984fe` | MATCH |
| First re-review report | `01feeb8423660e500def1b19e6f77d4a3944ec0b7f960ec9085172c60605bff6` | MATCH |
| First review brief | `8404261e617624dca8f95a1820dcccdc176629653ba928878f498d9f078b3e4b` | MATCH |
| First re-review brief | `a4962ede8720ce2ea77b259cceffb4eedfa4df5ed6d29aba4f1f03921b0a4964` | MATCH |
| First review manifest | `0e3319fa9fd2df423985a93266135dae90302f9193fe6ce4508afb599e7bfab6` | MATCH |
| First re-review manifest | `281ca93c0f41e5c68de0e0b9ad374eaac956c456ee845a26364e88632c2fe9a1` | MATCH |
| `tracker.md` — context | `3b46937618200c1813e5915edbaaeec68411572fcb6371b0e99117d79f598efb` | MATCH |
| `MEMORY.md` — context | `28329bd70ed33c7f79b47b8f89649f7d82a2887fe576cbb27b122a0ae507f823` | MATCH |
| Second re-review brief — additional receipt pin | `2448d895affcf32fdf4c09639453e52a10e9521eb2666859faa045fa1bf7e81c` | Recorded |

Starting status matches the manifest plus its expected new brief and manifest. The `B164.md` deletion, modified MEMORY/register/design/tracker and all existing untracked documents were preserved. No mismatch required a STOP. Completion checks confirm **1,146** pre-existing tracked/nonignored-untracked paths retain their contents or absence; this report is the only added path.

## 2. DR-4 closure

| Remaining point | Verdict | Basis |
| --- | --- | --- |
| Name returns follow the opener | **CLOSED** | §§4.3–4.4 now record `my_device`, `setup_join` or `setup_create`; §7.4.1's name-result table uses that origin. Successful setup-origin acknowledgement re-asks the same settings gate; refusal shows its note and returns Home on acknowledgement. W7 and §14 carry both return paths. A failed save retains the draft. |
| Drained-and-refused request under `long_fire` | **CLOSED** | “Known refused” explicitly includes the state after `take_send_request` has cleared pending and `send_gate_of` refuses without submitting. Its alarm row closes the view, keeps/unlocks the draft and returns to the editor for fresh review, with no re-submission. It no longer depends on being either pending or executor-returned. |
| Known refusal versus late/uncertain outcome | **CLOSED** | Typed proof of no airing controls retention. Synchronous refusal and an attributable, proven-unsent failure retain the draft; an accepted request with an uncertain later result may release it on acknowledgement or alarm, with the residual disclosed. The W8 brief must enumerate `SendFailReason` producers, and an unclassified failure is conservatively possibly sent. `send_failed` alone is expressly insufficient. |

The state/event table now supplies both acknowledgement and alarm behavior for queued, known-refused, accepted-open and accepted-final requests. §5.5 delegates to this table; §5.6 separates content immutability from the result view's ownership. Saved phrases still create no editable draft. The acceptance matrix requires the per-caller/per-state cases, the preserved emergency request and the typed retain/release classification.

The two additionally confirmed choices fit those rules:

- **Queued written request:** ordinary acknowledgement presses are consumed without releasing bytes still needed by the executor. The separately specified `long_fire` path withdraws only the ordinary pending request and keeps the draft. Blanking, wake consumption and emergency gesture precedence retain §5.5's rules. The duration is bounded by actual service eligibility; the implementation must use pending state, not assume a fixed one-loop timer. `mr_ui_tick` still owns the drain priority.
- **Successful name save interrupted during setup:** the save stands, the draft is released, and the device returns Home after the alarm. Setup is not resumed automatically. This is distinct from a failed save, which returns to its retained draft, and from a successful non-setup save, which returns to My device.

No new tracker, durable outbox or automatic resend is implied. The exact allocation and every failure-type classification remain obligations of the appropriate measured package brief, not verified implementation results here.

## 3. Minor fold-ins confirmed

| Finding | Verdict | Confirmation |
| --- | --- | --- |
| **DR-9** | **Present; CLOSED** | §7.7 applies `text_max` only to the overlength arm of `bad_text`; absent/empty/all-space and forbidden bytes remain errors. |
| **DR-10** | **Present; CLOSED** | §7.7 and W6 name all four `kPresetLineMax` users, including `preset_boot_restore`. §11.1 distinguishes per-buffer +84 B from measured peak stack; §14 includes the boot-diagnostic path. |
| **DR-11** | **Present; CLOSED** | §4.6 qualifies the fresh-cache assertion; D13 separates the approximately +8 B boundary from the +64 B ordering-key payload before padding. §8 and §11.1 use the session arrival serial and a separate present flag, so zero after wrap is valid. |

## 4. New finding — text fold-in only

| Finding | Severity | Design § | Finding | Evidence (`file:symbol`, line hint) | Required correction | OWNER RULING REQUESTED |
| --- | --- | --- | --- | --- | --- | --- |
| **DR-12** | **MINOR** | §7.4.1 Request states | `SENT, waiting` is listed under “Accepted, final,” although it is the nonterminal airing observation. The “Known refused” definition also opens with “the core never accepted it” before explicitly including accepted-then-proven-unsent failures. These labels should agree with the otherwise-correct state policy. | `src/firmware_ui.cpp:draw_compose_result` renders `DmState::aired_waiting` / `ChanState::aired` as `SENT, waiting` (2196/2228); `UiModel::on_send_aired` promotes to those states (3481/3493); `src/firmware_ui_send.h:SendTracker::match_aired` (175–179) is non-consuming and leaves the final matcher live. | Put `SENT, waiting` under **Accepted, open**, retaining tracking until an outcome or the existing view-close event. Start “Known refused” with **known not to have aired**, covering both pre-admission refusal and the explicitly enumerated post-admission cases. Preserve the existing late-ACK upgrade after `NO CONFIRM` while the view remains open. No retain/release choice, owner decision or tracker behavior changes. | No |

This is a direct wording correction: accepted-open and accepted-final already have the same release policy on acknowledgement/pre-emption, and §7.4 retains the existing tracker behavior. Fold it in before deriving the concrete state tests. **No further substantive re-review is required.**

## 5. Verified independently and limits

Re-read the changed passages and their W7/W8 and acceptance consequences against executable source:

- `take_send_request`, `queue`, `send_gate_of` and `ui_perform_send`: ordinary pending clears before the gate, emergency pending is separate, pre-submission refusal differs from executor acceptance, and a zero counter is not proof of failure.
- `compose_gesture`, `close_compose`, `ui_pump_trackers`, `mr_ui_tick`: ordinary acknowledgement closes the existing result view; normal tracking ends with that view; emergency drain has priority. The proposed manual-only protection is explicitly a future behavior change.
- `SendTracker::match_dm`, `match_aired`, `UiModel::on_send_aired`, the actual render arms and `SendFailReason`: airing is nonterminal, timeout is not proof of non-delivery, and the retained late-ACK path must remain. This was not an exhaustive new census of every failure producer; that is expressly required of the W8 brief.
- `settings_activate`'s provisioning arm: it checks service availability, conflict and unsaved state before entry. Re-asking the factored gate after naming prevents that intervening flow from bypassing the live checks.

DR-9–DR-11 received the requested presence/consistency check. The earlier full source and arithmetic review is not repeated or recounted as fresh execution.

**Not run:** firmware/native tests, simulator/corpus, probes, mutations, board builds, ABI/resource measurements or metal checks. None is required for this narrow specification review. No manual-message/editor implementation exists to validate yet; no new allocation is approved by this verdict.

All pinned hashes and the preservation inventory were checked again at completion; the simulator remains clean. Report links and whitespace checks pass. No file under review was edited, and nothing was staged or committed.

**Register rows added: none.** DR-12 is a design-text finding under the review fence. **B335 stays OPEN; next free remains B449.** The author can fold DR-12 into the design and record the specification verdict, then proceed to the required package pre-checks and briefs. Owner confirmation above is settled input for that work; commits are not a blocker.
