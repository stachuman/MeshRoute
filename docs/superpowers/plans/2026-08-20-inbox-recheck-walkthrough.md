<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Inbox recheck WALKTHROUGH — B231/B233 on glass · 2026-08-20 · ~10 minutes, one Heltec + one peer

**Follow top to bottom; every command and expected outcome inline.** Assembled from bench Part 31 (the authority,
M2). Run on the committed revision carrying the inbox pair. Record PASS / FAIL per checkbox; failures with the
console lines and a panel photo.

1. ☐ Flash/confirm the committed revision on H1 (`version` — ⛔ not `nogit`), peer H2 in range, team up if not
   already (⚠ if starting from a factory-reset node: `cfg set sf_list 6,7` first — [[B230]]).
2. ☐ Seed the inbox on H1: from H2 send **two DMs** (`send <H1-id> "dm one"`, then `"dm two"`) and **two channel
   posts** (`send_channel 0 "ch one" -t`, then `"ch two"`).
3. ☐ H1 panel → INBOX. Expected order top to bottom: **`dm two` · `dm one` · `ch two` · `ch one`** — DM block
   first then channel block (unchanged), **newest at the TOP within each block** ([[B231]]).
4. ☐ Move the highlight onto `dm one`. From H2 send a third DM (`"dm three"`). Expected: `dm three` appears at
   the TOP of the DM block and **the highlight is still on `dm one`** (pushed one row down — ⛔ never re-targeted
   onto the newcomer).
5. ☐ **Delete-middle ([[B233]]):** open `dm one` (a middle row), `short` to `delete`, `double`. Back at the list:
   the row is **GONE with NO further press** — time only, within ~1 s — and the highlight sits beside where it
   was. ⛔ FAIL if the deleted row is still drawn until you press something (the pre-fix artefact).
6. ☐ Open the now-deleted-adjacent row and confirm it opens the record its label names (no off-by-one).
7. ☐ **Delete-last:** delete the BOTTOM row of the channel block. Same expectation: gone with no press, highlight
   on its predecessor (the arm that already worked pre-fix — it must still).
8. ☐ Envelope sanity: the strip's unread count matches what remains; one fully drawn INBOX list returns it to 0.
