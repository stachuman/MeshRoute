<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com>; re-review brief: Claude, specification author, 2026-09-24 -->
# Standalone mobile Home design — re-review brief (changed sections)

**2026-09-24 · READY FOR RE-REVIEW.** The [first review](../evidence/2026-09-24-standalone-mobile-home-design-review.md)
returned HOLD with findings DR-1–DR-8. Revision 2.15 of the
[design](../specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md) folds in all eight; none is
disputed. Under the [agent roles](../../2026-09-02-agent-roles.md) verdict rules, a HOLD gets **one re-review of
the changed sections only**. Role, authority and fence are those of the
[original brief](2026-09-24-standalone-mobile-home-design-review-brief.md) §§1 and 5, unchanged. This brief
authorizes no implementation.

## 1. Pinned inputs

| Item | Pin |
| --- | --- |
| Repository | `/home/staszek/MeshRoute`, base commit `20357578700270d7315dcf86bc3bb5725d8024cb` |
| Simulator | `/home/staszek/lora-universal-simulator`, `6585649ea5a780f0542b2931853a667be56a5b2b`, clean, read-only |
| Re-reviewed texts and receipts | SHA-256 in the [re-review manifest](../evidence/2026-09-24-standalone-mobile-home-rereview-inputs.json), itself SHA-256 `281ca93c0f41e5c68de0e0b9ad374eaac956c456ee845a26364e88632c2fe9a1` |
| Unchanged since the first review | the register (`03119e87…`), the first brief (`8404261e…`), the first manifest (`0e3319fa…`) |
| Expected extra untracked files | this brief and the re-review manifest |

Verify the commits and every hash first. A mismatch in the design, the audit or the first report is a STOP to the
owner. For the register, apply the original brief's rule.

## 2. What changed, and what to check

The design's §16 entry **r2.15** lists each finding's disposition. For each one, confirm that the correction is
complete and correct **against the source**, not merely present. Also check that it introduces no new
contradiction in the touched sections or in their resource (§11.1), package (§13), acceptance (§14) and metal
(§15) entries.

| Finding | Changed sections | Check in particular |
| --- | --- | --- |
| DR-1 | §6.1 rule 10; §8 State, Display, Clearing; §14 Home card row; §15 UI-14 | Is the card bound to the session arrival serial, with the modular clearing test? Is the existing read rule stated exactly as `FrameGate` has it, passive screen and menu-mode preview included? Are the arrival-during-frame, restored-sequence, gap and wrap cases present? |
| DR-2 | §7.7 NDJSON bullet; W6; §14 Catalog version 2 row | Does `capacity` stay the slot count, with a separate `text_max`? Is the companion contract section in the fence? |
| DR-3 | §7.7 reply-line bullet; §11.1 Preset reply line row and stack sentence; W6 gate; §14 Catalog version 2 row | Is the 243/244 derivation right? Are all three emitters fenced? Is no stack neutrality claimed, and is there a control restoring 160? |
| DR-4 | §5.5 rows `long_fire`, team change, recipient change, refusal, emergency pending; §5.6; §7.4 gate bullet; new §7.4.1; W8; §14 Context and lifetime row | Is every caller × event cell safe and consistent? Are the gates scoped by kind? Is the content lock distinct from result ownership? See also the withdrawal of an unexecuted written message at `long_fire` and the declared post-alarm residual. |
| DR-5 | §6.8 rule 1 and the new ordering-key bullet; §11.1 ordering key row; W4d; §14 Inbox order row | Does the full-precision key reach the merge without growing `UiSnapshot`? Are the sub-second, equal-time and >2^32 ms cases present? |
| DR-6 | §1 D8 paraphrase; §7.8 heading, text and table; §10 `ui_preset_blob_state` row; §12 D8; W6; §15 UI-12 | Does the text say "at every boot until the first change" with zero boot writes, and nowhere "once"? |
| DR-7 | §4.3 point 4; §4.6 precondition; W1c gate | Is propagation qualified for pinned rows? Is the fresh-peer-cache precondition stated, with no default-string recognition added? |
| DR-8 | §4.6 Gains; §8 wireframe; §12 D10 row; §14 Home card row; §15 EDIT-01 | Are the savings 26/26/27? Is the widest header `FROM T254 59m +9+` at 17 columns? Does EDIT-01 type `RETURN TO BASE NOW`? |

**Author changes beyond the findings,** to be checked as well:
- the design's §2 "Inbox detail" baseline row now states the read rule as DR-1 found it;
- the [source audit](../evidence/2026-09-22-standalone-mobile-home-source-audit.md) gains rows marked "added 2026-09-24" in §§3.8–3.9, and one row marked "corrected 2026-09-24";
- the §7.4.1 note limits the withdrawal to written messages.

Sections outside these lists are unchanged since the first review and need not be re-read. Follow a reference out
of a changed section only as far as needed to judge it.

## 3. Deliverable

Write `docs/superpowers/evidence/2026-09-24-standalone-mobile-home-design-rereview.md` (author line on line 1)
containing:

1. **Verdict:** PASS, PASS with fold-ins, or HOLD, with the date.
2. **Inputs verified.**
3. **Per finding:** closed, or not closed with the reason.
4. **Any new findings,** numbered from DR-9 in the first report's table format.
5. **Verified independently,** and **not verified**.

Leave the first report unchanged. New register rows are for code or document defects outside the design only, at
the next free number (B449 at the pin). **B335 stays OPEN.**
