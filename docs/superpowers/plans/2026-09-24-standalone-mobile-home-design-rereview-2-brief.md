<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com>; second re-review brief: Claude, specification author, 2026-09-24 -->
# Standalone mobile Home design — second re-review brief (DR-4)

**2026-09-24 · READY FOR NARROW RE-REVIEW.** The [re-review](../evidence/2026-09-24-standalone-mobile-home-design-rereview.md)
closed DR-1–DR-3 and DR-5–DR-8. It kept **DR-4** open and added the minor DR-9–DR-11. Revision 2.16 of the
[design](../specs/2026-09-06-standalone-mobile-home-and-team-messaging-design.md) folds all of these in; none is
disputed. The re-review asked for another read of **DR-4's transitions and their consequences only**. DR-9–DR-11
are text fold-ins that need no substantive re-review; confirm they are present.

Role, authority and fence are those of the [original brief](2026-09-24-standalone-mobile-home-design-review-brief.md)
§§1 and 5, unchanged. This brief authorizes no implementation.

## 1. Pinned inputs

| Item | Pin |
| --- | --- |
| Repository | `/home/staszek/MeshRoute`, base commit `20357578700270d7315dcf86bc3bb5725d8024cb` |
| Simulator | `/home/staszek/lora-universal-simulator`, `6585649ea5a780f0542b2931853a667be56a5b2b`, clean, read-only |
| Re-reviewed text and receipts | SHA-256 in the [second re-review manifest](../evidence/2026-09-24-standalone-mobile-home-rereview-2-inputs.json), itself SHA-256 `dd4becdd3a1955e953e053d1b2281db0ebbdaa96efd5cadfbbbc8fc83ea10e3c` |
| Unchanged since the re-review | the source audit (`9c90f644…`) and the register (`03119e87…`); both reports, both briefs and both earlier manifests |
| Expected extra untracked files | this brief and its manifest |

Verify the commits and every hash first. A mismatch in the design, the audit or either report is a STOP to the
owner.

## 2. What to read

The design's §16 entry **r2.16** records the dispositions. Read these changed passages against the re-review's
three DR-4 points and against the source (`take_send_request`, `send_gate_of`, `ui_perform_send`,
`compose_gesture`/`close_compose`, `ui_pump_trackers`, `SendTracker::match_dm`, `SendFailReason`):

| DR-4 point | Changed passages |
| --- | --- |
| 1. Name returns follow the opener | §4.3 (the origin paragraph); §4.4 (continuing into setup re-asks the gate); §7.4.1 "Name results" table; W7; §14 "Name save" row |
| 2. Drained-and-refused request under `long_fire` | §7.4.1 "Request states" and the "After `SEND`" rows *Known refused*; §5.5 `long_fire` row |
| 3. Known refusal versus the late-outcome residual | §7.4.1 *Known refused* definition (typed; `send_failed` alone is not proof; unclassified reasons count as accepted, final) and rows *Accepted, open* / *Accepted, final*; §14 "Context and lifetime" row |

**New choices to judge** (not requested by the re-review):
- **A press while a written request is still queued** is consumed and ignored until the request executes on the
  next service pass. Without this, a press on `SENDING...` would release a draft the queued request still reads.
- **A name saved from the setup prompt:** after an alarm, the flow lands on Home and setup is not resumed
  automatically.

**Also confirm present:**
- DR-9 in §7.7;
- DR-10 in §7.7, §11.1, W6 and §14;
- DR-11 in §4.6, §8, §11.1 and the D13 row.

## 3. Deliverable

Write `docs/superpowers/evidence/2026-09-24-standalone-mobile-home-design-rereview-2.md` (author line on line 1)
containing:

1. **Verdict:** PASS, PASS with fold-ins, or HOLD, with the date.
2. **Inputs verified.**
3. **DR-4:** closed, or not closed with the reason.
4. **DR-9–DR-11:** present, or not.
5. **Any new findings,** numbered from DR-12.
6. **Verified independently,** and **not verified**.

Leave the earlier reports unchanged. New register rows are for code or document defects outside the design only
(B449 at the pin). **B335 stays OPEN.**
