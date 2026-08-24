#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""The NATIVE mutation battery for the tree's PURE units — §UI-7D slice B's `firmware_ui_model.h`, §UI-13's
`firmware_config_service.h`, and (since §UI-16 N1, 2026-08-22) the first two `lib/core` targets.
⚠ THIS LINE USED TO READ *"the PURE `src/` units"* AND IS CORRECTED IN PLACE: the restriction was never in the
machinery — every guard is keyed by the RESOLVED PATH or by the build directory — it was only in the target table.

★★ WHY IT LIVES IN THE REPOSITORY. A proven instrument kept in a session scratchpad is a lost instrument: this project
has already lost a 33-assert scenario that way ([[meshroute-agent-scratchpad-is-volatile]]). The two UI probes carry
their own controls in `tools/probe_*/run.sh`; the PURE model's cases are driven by the doctest suite, which has no such
place to keep them — so this is it.

★★ TWO TARGETS, ONE FILE PER RUN (§UI-13, 2026-08-13). The batteries are per-SOURCE-FILE because the whole safety
machinery — the sidecar backup, the in-flight marker, the concurrent-run lock — is KEYED BY THE RESOLVED SOURCE PATH
(see `_BK_KEY`), which is the fix that stopped two worktrees from restoring over each other. Adding a second target
therefore adds a second key and changes NOTHING about the three-arm recovery: one run mutates one file, and a run
against `model` cannot see, lock or restore `config`'s backup. ⇒ `--target=` is read BEFORE `H` is resolved, and
everything downstream of `H` is unchanged.
⛔⛔ BUT THE PER-SOURCE LOCK IS NOT ENOUGH, AND THAT WAS THIS RUNNER'S FIFTH DEFECT: both targets compile into the SAME
`.pio/build/native`, so two runs holding DIFFERENT source locks still collided in the build directory and could
produce a FALSE RED. There are now TWO locks with TWO granularities — see `take_build_lock` — because ★ THE QUESTION
IS NOT "what failed last time" BUT "WHAT RESOURCE DOES THIS GUARD PROTECT": the backup and marker protect a FILE (per
resolved path), the build lock protects a TREE (per build directory).

★★★ WHAT A MUTATION HAS TO BE. Each entry is a TEMPTING WRONG FIX (not a deletion), applied to the real source at a
match count of EXACTLY 1, rebuilt, run, and REQUIRED to turn the suite RED. Three ways an entry can be worthless, all
three reported as failures rather than passes:
  1. the pattern matched 0 or >1 times   -> VACUOUS (a multi-site edit is not an attributable mutation);
  2. the mutant does not compile         -> UNUSABLE (the suite never ran against it);
  3. the suite still passes              -> FAIL (nothing measures the property).
⚠ The source is restored and md5-verified at the end; a differing md5 is a hard failure.

★★ AND THE CLEAN BASELINE IS **DERIVED**, NOT HAND-PINNED ([[B217]], 2026-08-21). The clean tree is run FIRST and must
show **0 FAILED** — that half still ABORTS loudly, with the failing output, because a RED verdict is only evidence if
GREEN was the alternative. Its own case/assertion counts then BECOME this run's baseline. ⛔ A stale expectation can no
longer zero a battery: the hand-written figure is a CROSS-CHECK that, when it disagrees, prints an unmissable banner at
both ends of the run while the battery runs for the requested selection — IN FULL when no entry filter was given, and
otherwise the filtered entries, which the banners name. See `PIN_CASES` for the measured incident that forced this.
⛔ A label prefix matching NO entry is likewise refused before any build (exit 8): "ran nothing successfully" is not a
result. Unrecognised arguments are refused too — see `_refuse_argv` ([[B235]]).

⛔⛔⛔ READ `guarded_write` BEFORE CHANGING ANYTHING THAT TOUCHES THE SOURCE FILE. This tool took SIX rounds and SIX
defects to become safe, and the sixth was the fourth one left unfixed in a second code path — the three-arm
original/mutant/FOREIGN check existed only in crash recovery while the normal loop still wrote unconditionally, across
the longest window in the tool (a full build inside `run_suite()`). ★★ EVERY WRITE TO THE SOURCE NOW GOES THROUGH THAT
ONE PRIMITIVE, from FOUR call sites, and the rule that produced it is: A GUARD BELONGS TO THE INVARIANT, NOT TO THE
INCIDENT — a guard installed only where the bug was observed is not a guard. The six defects and the scope each fix was
wrongly given are tabulated at the primitive.

USAGE:  python3 tools/probe_ui_model_mutations.py                    # the model battery (the default target)
        python3 tools/probe_ui_model_mutations.py M07                # one entry, by its label prefix
        python3 tools/probe_ui_model_mutations.py --target=config     # the §UI-13 config-service battery
        python3 tools/probe_ui_model_mutations.py --target=config C05 # one entry of it
        python3 tools/probe_ui_model_mutations.py --where            # print the resolved source + its keyed backup dir, do nothing
"""
import atexit, fcntl, hashlib, json, os, re, shutil, signal, subprocess, sys, tempfile
from pathlib import Path

# ★ THE REPOSITORY ROOT IS DERIVED, NOT HARDCODED. An absolute path baked into a tool is a tool that measures somebody
#   else's tree — or nothing at all — the moment it is run from a worktree or a clone ([[B82]] is the same class: a
#   relative path in a cwd-resetting shell silently measured nothing once already).
ROOT = str(Path(__file__).resolve().parents[1])

# ⛔ THE TARGET IS RESOLVED HERE, ABOVE EVERYTHING KEYED ON `H`, and an unknown name is REFUSED rather than defaulted:
#   silently measuring the wrong file is precisely the failure this tool exists to make impossible.
TARGET_SRC = {
    "model":  "src/firmware_ui_model.h",        # §UI-7D slice B — the pure screen/state model
    "config": "src/firmware_config_service.h",  # §UI-13 — the typed staged-configuration service
    "chrome": "src/firmware_ui_chrome.h",       # §CHROME-1 — the frozen chrome projection + the §5.2 nav mapping
    "icons":  "src/firmware_ui_icons.h",        # §CHROME-1 — the icon assets and their byte-order contract
    "joinprofiles": "src/firmware_join_profiles.h",  # §UI-15 slice 2 — the /mrjoin preset store + its write policy
    # ★★ TWO TARGETS ADDED 2026-08-19 BY THE §UI-15 slice 2 CORRECTIONS, and for the reason the file's own header
    #    gives: a battery is per-SOURCE-FILE because every safety guard is keyed by the resolved path. The two
    #    blockers that were NOT in the service live in these two files, so without their own targets the fixes would
    #    have had no controlled mutation at all — which is the shape ([[B217]]) this run exists to avoid repeating.
    "devicenv": "src/device_nv.h",              # §UI-15 slice 2 correction — the read arms' SlotIo facts (blockers 1+2)
    "cfgparse": "src/firmware_config_parse.h",  # §UI-15 slice 2 correction — the strict positional parse (blocker 3)
    # ★★ ADDED 2026-08-19 BY §UI-15 slice 5, for the reason the two targets above were added: the OWNER's PHY
    #    precondition and plan §2.1's `phy.present = false` live in a file of their own, so without its own target the
    #    ruling would have had no controlled mutation at all — the [[B217]] shape this runner exists to avoid.
    "uiprov": "src/firmware_ui_prov.h",         # §UI-15 slice 5 — the OLED team-create adapter
    # ★★ ADDED 2026-08-20 BY §UI-15 slice 6, and for the reason every target above it was added: plan §2.3's
    #    FOUR-TERM CORRELATION RULE must be attacked TERM BY TERM, and a battery is per-SOURCE-FILE. Left inside the
    #    2600-line model it would have shared `model`'s 80 entries and the four terms would have had no isolated
    #    controls at all — the [[B217]] shape this runner exists to avoid.
    "uijoin": "src/firmware_ui_join.h",         # §UI-15 slice 6 — the static-join screens' pure unit + the rule
    # ★★ ADDED 2026-08-20 BY [[B230]], and for the reason every target above it was added: the incomplete-PHY
    #    CLASSIFICATION (which part is missing, and therefore which remedy the console offers) now lives in this
    #    header, and a battery is per-SOURCE-FILE. Without its own target the split would have had no controlled
    #    mutation at all — the [[B217]] shape this runner exists to avoid.
    "provservice": "src/firmware_provisioning_service.h",  # §PROV-TX — the typed team-provisioning transaction
    # ★★ ADDED 2026-08-21 BY §UI-17 slice 3, and for the reason every target above it was added: the STATUS body's
    #    five rows carry NINE substitutions and one priority order, every one of which is a ruled decision that a
    #    mutation must be able to attack ON ITS OWN — and a battery is per-SOURCE-FILE. Composed in
    #    `src/firmware_ui.cpp` they would have had NO battery at all (that TU is compiled by neither the native
    #    suite nor the simulator, §B115); left in the 2700-line model they would have shared `model`'s entries.
    "uistatus": "src/firmware_ui_status.h",     # §UI-17 S3 — the STATUS body's rows, substitutions and priority
    # ★★ ADDED 2026-08-21 BY §UI-17 slice 4, for the reason every target above it was added: the TEAM row's ruled
    #    format has FIVE fields and the repaint invalidation has four separate rules (bucket, drawn prefix, screen,
    #    raise-never-clear), each of which is a decision a mutation must be able to attack ON ITS OWN — and a battery
    #    is per-SOURCE-FILE. Composed in `src/firmware_ui.cpp` they would have had NO battery at all (§B115).
    "uiteam": "src/firmware_ui_team.h",         # §UI-17 S4 — the TEAM row + the F-8 clock-driven repaint
    # ★★ ADDED 2026-08-21 BY §UI-17 slice 5, for the reason every target above it was added: the location columns'
    #    FOUR-TERM rule, the 600 s freshness bound and the four load-bearing maths rules (int64 longitude, the
    #    antimeridian fold, integer-differences-first, the atan2-free octant) are each a decision that must be
    #    attacked ON ITS OWN — and a battery is per-SOURCE-FILE. Folded into the row formatter they would have shared
    #    `uiteam`'s entries; composed in `src/firmware_ui.cpp` they would have had NO battery at all (§B115).
    "uigeo":  "src/firmware_ui_geo.h",          # §UI-17 S5 — freshness, geometry and the two location tokens
    # ★★ ADDED 2026-08-22 BY §UI-17 slice 8, for the reason every target above it was added, and this one is the
    #    [[B217]] shape at its plainest: the WAKE's whole safety argument is the `pu.enc` gate on ONE arm of
    #    `ui_route_recv_push`, and a battery is per-SOURCE-FILE — so without its own target the gate, the two call
    #    sites and the "⛔ do not copy it onto the DM arm" rule would have had NO controlled mutation at all. The
    #    model's `--target=model` entries (S08-S16) attack the EFFECT; these attack the SCOPE.
    "uisend": "src/firmware_ui_send.h",         # §UI-17 S8 — the recv router's two wake call sites + the `enc` gate
    # ★★ ADDED 2026-08-22 BY §UI-16 K1, and for the reason every target above it was added: the keyring's WRITE
    #    POLICY is a set of ruled clauses — `team_id == 0` never stored, one record per team, identical material
    #    writes nothing, a FULL keyring never evicts, boot install only on an exact active-team match — each of which
    #    must be attacked ON ITS OWN, and a battery is per-SOURCE-FILE. Bound in `src/firmware_config.cpp` they would
    #    have had NO battery at all (that TU is compiled by neither the native suite nor the simulator, §B115).
    "teamkeyring": "src/firmware_team_keyring.h",  # §UI-16 K1 — the /mrteams store, its policy and the boot restore
    # ★★★ THE FIRST TWO `lib/core` TARGETS, ADDED 2026-08-22 BY §UI-16 N1 (QG blocker 1), and the reason is the one
    #    every target above was added for: the spec requires N1's decisions mutated INDIVIDUALLY at match count 1, and
    #    a battery reported in a session transcript is a battery that re-proves nothing next slice
    #    ([[meshroute-agent-scratchpad-is-volatile]] — a proven 33-assert scenario was lost exactly that way).
    # ⓘ THE RUNNER NEEDED NO ADAPTATION BEYOND THESE TWO ROWS AND THEIR TWO ENTRY LISTS, and that is a measurement,
    #    not a hope: every safety mechanism is keyed by `Path(H).resolve()` (`_BK_KEY`) or by the build directory
    #    (`_BUILD_KEY`), neither of which knows or cares that the path used to start with `src/`. Nothing in the tool
    #    tests the extension either, which is why a `.cpp` target works beside twenty `.h` ones. The ONE thing that
    #    changed in behaviour is scope: this file's title line no longer says "the PURE `src/` units".
    # ⚠ WHY **TWO** TARGETS FOR ONE SLICE AND NOT ONE: a battery is per-SOURCE-FILE (see the header — the backup, the
    #    marker and the lock are all keyed by the resolved path), and N1's decisions genuinely live in two files. The
    #    RING POLICY (de-dup key, the EWMA, retention, order) is pure and lives in the header; the ELIGIBILITY RULE and
    #    the READ-ONLY discipline are properties of the CALL SITE and can only be attacked where the call is made.
    # ★★ ADDED 2026-08-23 BY §UI-16 N2, and for the reason every target above was added — plus one this arc had not
    #    met before: the NEARBY screen's pure logic is TWO headers, because `src/firmware_ui_chrome.h:36` includes
    #    `firmware_ui_model.h`, so a header the MODEL includes may not include chrome. The carriers + the OWN-TEAM
    #    FILTER + the row list must be model-includable (`UiSnapshot` publishes the array, `UiState` freezes it);
    #    the two TOKENS need chrome's shared formatters and therefore sit downstream. ⇒ two files, two targets, and
    #    each ruled decision keeps a battery of its own (R-5's filter/order in one, R-4's tier map in the other).
    # ★★ ADDED 2026-08-23 BY §UI-16 N4, and for the reason every target above it was added: the invitation
    #    window's rulings — the TWO snapshot authorities (each of which must be attacked ALONE, or the other
    #    hides it), the AUTHORITATIVE floor that keeps a route-only member off the grantable list, the VOLATILE
    #    handled set, and the name lifecycle that ADDS a column instead of swapping one — are each a decision a
    #    mutation must reach ON ITS OWN, and a battery is per-SOURCE-FILE. ⓘ ONE target, not two: unlike N2's
    #    row this unit needs nothing from `firmware_ui_chrome.h` (its fingerprint is a SEPARATE definition by
    #    ruling and its row is fixed-width by construction), so it is model-includable in full.
    "uiinvite":    "src/firmware_ui_invite.h",      # §UI-16 N4 — the two authorities, the handled set, the row
    "uinearby":    "src/firmware_ui_nearby.h",      # §UI-16 N2 — the own-team filter, the order, the rows, the lexemes
    "uinearbyrow": "src/firmware_ui_nearby_row.h",  # §UI-16 N2 — the `n/3` tier map + the fingerprint/age row (S-6/S-7)
    "teamseen":     "lib/core/team_seen_ring.h",  # §UI-16 N1 — the pure nearby-team ring: de-dup, EWMA, retention, order
    "teamseensite": "lib/core/node_beacon.cpp",   # §UI-16 N1 — the ONE write site: eligibility + the read-only discipline
}
_flags = [a for a in sys.argv[1:] if a.startswith("--")]

# ⛔⛔ AN ARGUMENT THIS TOOL DOES NOT KNOW IS **REFUSED, NOT IGNORED** ([[B235]], 2026-08-21). The argv parse used to
#     scan for the two forms it understood and drop everything else on the floor, so `--taret=config` — one missing
#     character — exited 0 having run the **MODEL** battery in full while the operator read the summary as `config`'s.
#     ★ That is the [[B217]] family seen from the other side: B217 measured NOTHING and looked like a clean battery,
#     this measures THE WRONG FILE and looks like the right one. Neither is detectable from the exit code, and both
#     are cured by the same rule — AN INSTRUMENT MAY NOT ACCEPT AN INSTRUCTION IT DID NOT UNDERSTAND. ⇒ refused here,
#     at the top, BEFORE the target resolves and long before any lock, build or mutation, so a refused run cannot
#     have touched the tree.
_KNOWN_FLAGS = ("--where",)             # value-less flags
_KNOWN_FLAG_PREFIXES = ("--target=",)   # `flag=value` forms
def _refuse_argv(what, why):
    print(f"  ABORT {what} — refused. {why}")
    print(f"  known arguments:  --target=<{'|'.join(sorted(TARGET_SRC))}>")
    print("                    --where                 (print the resolved source + backup dir, do nothing)")
    print("                    <ENTRY-LABEL PREFIX>    (at most ONE, positional, e.g. M07 — runs just those entries)")
    print("  known environment: MR_MUT_BASE=\"cases,asserts\"  (the OPTIONAL clean-baseline cross-check)")
    print("  No mutation was applied; nothing was built.")
    sys.exit(7)
for _a in _flags:
    if _a not in _KNOWN_FLAGS and not any(_a.startswith(p) for p in _KNOWN_FLAG_PREFIXES):
        _refuse_argv(f"unrecognised flag {_a!r}",
                     "An ignored flag is an operator who believes something was measured that was not.")
# ⚠ The label filter is POSITIONAL and the flags are not: `--target=` must not be mistaken for an entry prefix (it
#   would match nothing and the run would silently measure ZERO mutations while reporting success). ⛔ And a SECOND
#   positional was silently dropped by exactly the parse [[B235]] is about — `M07 M08` ran M07 alone and said so
#   nowhere — so it is refused here too, in the one place the argument vector is judged.
_positional = [a for a in sys.argv[1:] if not a.startswith("--")]
if len(_positional) > 1:
    _refuse_argv(f"{len(_positional)} entry-label filters {_positional} — at most ONE is supported",
                 "Only the first would have run, and the summary would not have said so.")

_TARGET = "model"
for _f in _flags:
    if _f.startswith("--target="):
        _TARGET = _f.split("=", 1)[1]
if _TARGET not in TARGET_SRC:
    print(f"  ABORT unknown --target={_TARGET}; known targets: {', '.join(sorted(TARGET_SRC))}")
    sys.exit(6)
H = os.path.join(ROOT, TARGET_SRC[_TARGET])

# ★★ THE CLEAN BASELINE IS **DERIVED FROM THE CLEAN RUN ITSELF**; THE FIGURE BELOW IS ONLY A CROSS-CHECK ([[B217]],
#    owner-ruled 2026-08-20, landed 2026-08-21).
#    ⚠ CORRECTED IN PLACE — this block used to open "THE PINNED CLEAN BASELINE, AND IT IS A GATE RATHER THAN A
#      COMMENT", and the gate it described `sys.exit(2)`-ed on ANY mismatch WITHOUT APPLYING A SINGLE MUTATION. That
#      is the worst shape this arc has recorded: not a false GREEN but NO MEASUREMENT AT ALL, behind an exit code a
#      careless reader takes for "ran, nothing wrong". ★★ MEASURED: §UI-15 slice 1 added a test file (1680/83432 ->
#      1733/84164) and did not re-pin ⇒ from that moment every invocation of ALL FOUR then-existing targets ran ZERO
#      mutations, and nothing said so. ⇒ ⛔ RE-PINNING BY HAND WAS NEVER THE FIX — hand-pinning is what failed, and a
#      guard whose failure mode is "measures nothing, quietly" is not a guard.
#    ★ THE REASON THE GATE EXISTS IS UNCHANGED, AND THAT HALF STILL ABORTS: every entry below is judged by "the suite
#      went RED", so on an ALREADY-RED tree that verdict is meaningless and this runner would report N successes
#      having measured nothing. ⇒ the clean tree is still run FIRST and must still report **0 FAILED**, or the run
#      ABORTS — loudly, printing the failing output — before a single mutation is applied.
#    ⛔ WHAT IS NO LONGER A GATE IS THE CASE/ASSERTION **COUNT**: a suite that legitimately grew is a moved figure,
#      not a broken tree, and the two were conflated into one `exit(2)`. The clean run's OWN counts become this
#      invocation's baseline (printed on the `ok clean baseline` line, and re-required verbatim by the exit gate on
#      the restored source — so the restoration proof is unweakened). The figure below is compared against the
#      derived counts and, when it differs, an UNMISSABLE banner prints at BOTH ends of the run and the battery
#      CONTINUES on the derived value. ⓘ It does NOT move the exit code: the exit status is the MUTATIONS' verdict,
#      and folding a bookkeeping drift into it would re-teach the reader that a stale figure means "did not run".
#    ⓘ MR_MUT_BASE="cases,asserts" still works and still means "the figure the clean tree is expected to show" — it
#      now overrides the CROSS-CHECK rather than the gate, which also makes it the one-command way to exercise the
#      stale-pin banner without editing this file.
PIN_CASES, PIN_ASSERTS = 2043, 90143     # ★★ CROSS-CHECK RE-SYNCED 2026-08-23 by §CHROME-5 (the status
                                         # strip's DUTY GAUGE): **2039 / 89691 -> 2043 / 90143**
                                         # (+4 cases / +452 assertions). DERIVED, and MEASURED block by
                                         # block with `program -tc=`; 446 + 5 + 1 = 452 closes EXACTLY:
                                         #  +4 cases / +446 assertions — the four NEW cases in
                                         #    `test/test_firmware_ui_chrome.cpp` (target `chrome`, with the
                                         #    artwork half attacked through `icons`): the DERIVATION pin —
                                         #    N from the glyph's drawable rows, the enum span, the
                                         #    row-by-row bottom-up decode of all six levels and the
                                         #    all-eight-pictures-distinct sweep (88); the pct -> step map
                                         #    over the whole 0..99 domain against an INDEPENDENTLY stated
                                         #    boundary table (317); the two boundary rulings — disabled
                                         #    beats any pct, 100 is blocked and never fill_N — plus the
                                         #    projection's five arms (23); and the repaint economy: two
                                         #    pcts inside one bucket owe NOTHING while both boundaries owe
                                         #    a frame (18).
                                         #  +5 — `chrome-equality: visible…`, extended in place: the
                                         #    same-bucket/disabled-either-way INVISIBLE pair (2) and three
                                         #    positive terms (a boundary crossing, 100 %, and losing the
                                         #    duty limit).
                                         #  +1 — `chrome-projection:`'s untouched-snapshot row for the
                                         #    gauge (`disabled`, ⛔ not the empty picture).
                                         # ⛔ THE PREVIOUS ENTRY IS KEPT VISIBLE BELOW, unedited.
                                         # ---- (previous) 2026-08-23 by §UI-16 N4's TWO QG
                                         # BLOCKER FIXES: **2039 / 89688 -> 2039 / 89691** (+0 cases /
                                         # +3 assertions). DERIVED, and it is a NET of two edits inside
                                         # two EXISTING cases, which is why no case count moves:
                                         #  −1 — `ui16-invexpire` (target `model`): the exact-deadline
                                         #    tick was pinned OPEN and is now pinned CLOSED, so the
                                         #    at-deadline CHECK and the separate at-`+1` pair collapse
                                         #    into one pair (3 asserts -> 2).
                                         #  +4 — `ui16-snap` (target `uiinvite`): the null-source arm was
                                         #    ONE fail-open pair and is now TWO arms — `(nullptr, 0)`
                                         #    valid+taken (2) and `(nullptr, n>0)` REFUSED with its two
                                         #    consequence checks, `invite_snap_has_id` and
                                         #    `invite_is_new` (4) — i.e. 2 -> 6.
                                         # ⛔ THE PREVIOUS ENTRY IS KEPT VISIBLE BELOW, unedited.
                                         # ---- (previous) 2026-08-23 by §UI-16 N4 (the
                                         # `INVITE MEMBER` window, its two snapshot authorities, its
                                         # handled set and its candidate row):
                                         # **2015 / 89104 -> 2039 / 89688** (+24 cases / +584 assertions).
                                         # DERIVED, not merely observed, and MEASURED block by block with
                                         # `program -tc=`; 372 + 200 + 8 + 2 + 1 + 1 = 584 closes EXACTLY:
                                         #  +16 cases / +201 assertions — the NEW file
                                         #    `test/test_firmware_ui_invite.cpp` (target `uiinvite`): the
                                         #    five-minute constant WITH its derivation pin (3), the
                                         #    snapshot's two authorities incl. both ends of the id space
                                         #    (22), the six diff cases — present-at-open, arrived-after,
                                         #    ★ re-DAD'd, ★ route-only-turned-authoritative, ★ the DRIVEN
                                         #    double-change SAFE FALSE PROMPT and the no-snapshot floor
                                         #    (53), the handled set incl. its full-set refusal and its two
                                         #    same-named members (33), the three row cases at exact bytes
                                         #    (31), the member fingerprint (8), the full-hash identity (9),
                                         #    the no-name-shaped-identity case (3) and the lexeme sweep
                                         #    over the three FORBIDDEN words (39).
                                         #  +8 cases / +171 assertions — the §UI-16 N4 block appended to
                                         #    `test/test_firmware_ui_model.cpp` (target `model`): the
                                         #    fourth child and its RUNTIME predicate (32), the
                                         #    snapshot-at-OPEN with its zero-transaction proof (19), the
                                         #    self-expiry at the exact edge (22), blank/wake with the
                                         #    unfinished confirmation dropped (16), the freeze + REJECT +
                                         #    the volatile set (37), the refresh-cannot-move-the-target
                                         #    case (11), the window-closed quiet case (25) and the alarm's
                                         #    pre-emption (9).
                                         #  +200 assertions — `ui15-close`'s arm sweep, extended in place:
                                         #    it drove TEN arms x 2 confirms x 3 checks and now drives
                                         #    THIRTEEN x 2 x 10, because `provision_reset_on_leave` gained
                                         #    the window as a THIRD fact to retire (65 -> 265, measured).
                                         #  +8 — `ui15-model`: the three new `Provision` values and
                                         #    `provision_is_invite`'s five arms.
                                         #  +2 — `ui15-hide`: the `INVITE MEMBER` label plus one more
                                         #    iteration of its width sweep (`kMaxProvRows` 4 -> 5).
                                         #  +1 — `ui15-parent`: the invite child alone earning the row.
                                         #  +1 — the §CHROME-4 width sweep (`test_firmware_ui_model.cpp`'s
                                         #    second `kMaxProvRows` loop), which gained the same row.
                                         # ⛔ THE PREVIOUS ENTRY IS KEPT VISIBLE BELOW, unedited.
                                         # ---- (previous) 2026-08-23 by §UI-16 N3 (the
                                         # `JOIN <fingerprint>?` confirmation + the act over the existing team
                                         # transaction):
                                         # **1998 / 88655 -> 2015 / 89104** (+17 cases / +449 assertions).
                                         # DERIVED, not merely observed, and MEASURED block-by-block with
                                         # `program -tc=`; 171 + 236 + 15 + 27 = 449 closes exactly:
                                         #  +9 cases / +171 assertions — the §UI-16 N3 block appended to
                                         #    `test/test_firmware_ui_prov.cpp` (target `uiprov`): the control,
                                         #    the `mint = false` + FULL-32-bit request with
                                         #    `phy.present = false`, the inherited PHY precondition over all
                                         #    four fields with its positive arm, the two `ProvPhy` objects
                                         #    driven apart, ★ the KEYLESS proof on a node that really held a
                                         #    key, ★ the RETAINED-keyring record left untouched and
                                         #    uninstalled (P-2b — N3 does not anticipate K5), the failed save
                                         #    that keeps the previous membership AND key, the four refusal
                                         #    arms (staging / unreadable record / zero id / `no_change`), and
                                         #    the three-op dispatch.
                                         #  +7 cases / +236 assertions — the §UI-16 N3 block appended to
                                         #    `test/test_firmware_ui_model.cpp` (target `model`): the
                                         #    confirmation's BACK default and its `short`-then-`double` cost,
                                         #    ★ the act carrying the ROW's full id against a fixture where the
                                         #    cursor, the published index and the fingerprint are all
                                         #    different values, BACK's landing on the LIST, the terminal
                                         #    result under either press, the whole-vocabulary lexeme sweep
                                         #    (155 of the 236 — nine outcomes x nine, plus the three
                                         #    forbidden words), the two fail-closed arms and the alarm's
                                         #    pre-emption of an unfinished confirmation.
                                         #  +1 case / +15 assertions — `ui16-jointitle` in
                                         #    `test/test_firmware_ui_nearby.cpp` (target `uinearbyrow`): S-8's
                                         #    exact bytes, its VALUE RELATION to the shared fingerprint helper
                                         #    over five ids, the low-24-bit collision that makes the token a
                                         #    selection aid and never an authority, and the fail-closed calls.
                                         #  +27 assertions — the RESIDUE (449 - 171 - 236 - 15), and it is
                                         #    measured by subtraction rather than counted by eye: four LANDED
                                         #    cases extended in place — the `Provision` enum's tenth arm (+1),
                                         #    `ui15-reset`'s arm sweep, whose *"ALL EIGHT ARMS"* array was
                                         #    stale at NINE and now drives TEN (+12 = 2 arms x 2 confirm
                                         #    values x 3 checks), the static-join `JOINED` sweep gaining its
                                         #    named exemption (+10), and `ui16-look`, whose double on a team
                                         #    row now OPENS the confirmation and is asserted to still perform
                                         #    nothing (+4).
                                         # ⛔ THE PREVIOUS ENTRY IS KEPT VISIBLE BELOW, unedited.
                                         # ---- (previous) 2026-08-23 by §UI-16 N2 (the `JOIN TEAM` child
                                         # + the read-only NEARBY list):
                                         # **1980 / 88475 -> 1998 / 88655** (+18 cases / +180 assertions).
                                         # DERIVED, not merely observed, and MEASURED case-by-case with
                                         # `program -tc=` per block; 98 + 75 + 7 = 180 closes exactly:
                                         #  +10 cases / +75 assertions — the NEW file
                                         #    `test/test_firmware_ui_nearby.cpp` (targets `uinearby` +
                                         #    `uinearbyrow`): the own-team filter incl. its full-32-bit
                                         #    equality, the teamless-joiner arm, the fail-closed/bounded
                                         #    capture, the first-observed order driven against a fixture
                                         #    where signal and age both disagree with it, BACK as the
                                         #    unconditional last row, a row's identity surviving the
                                         #    filter's re-indexing, the FOUR tier tokens at their shared
                                         #    -12/-4/+4 dB boundaries, the row's exact bytes + the
                                         #    fingerprint VALUE RELATION, the reused age table incl. `--`
                                         #    and the 64-bit range, and every lexeme.
                                         #  +8 cases / +98 assertions — the §UI-16 N2 block appended to
                                         #    `test/test_firmware_ui_model.cpp` (target `model`): the third
                                         #    child + the DIRECT landing (OQ-1), the child's own predicate
                                         #    and the parent row it alone earns, the ONE-SHOT capture
                                         #    (R-10's freeze, driven against a snapshot that changes under
                                         #    the open screen), the own-team filter through the model, the
                                         #    cycling walk with BACK's containment, the "can only look"
                                         #    counters, blank/wake retention and the alarm's pre-emption.
                                         #  +7 assertions — the RESIDUE (180 - 98 - 75), and it is
                                         #    measured by subtraction rather than counted by eye: five
                                         #    LANDED cases extended in place — the `Provision` enum's
                                         #    ninth arm, `ui15-menu`'s walk (the third child joined the
                                         #    cycle, two lines), `ui15-hide`'s label table (S-1) and its
                                         #    label-width loop (`kMaxProvRows` 3 -> 4, one more
                                         #    iteration), and `ui15-parent` gaining *"the nearby child
                                         #    ALONE earns the parent row"*.
                                         # ⛔ THE PREVIOUS ENTRY IS KEPT VISIBLE BELOW, unedited.
                                         # ---- (previous) 2026-08-22 by §UI-16 N1 (the read-only nearby-team
                                         # observation cache — the arc's ONE lib/core slice):
                                         # **1967 / 88323 -> 1980 / 88475** (+13 cases / +152 assertions).
                                         # DERIVED, not merely observed, and MEASURED case-by-case with
                                         # `program -tc="*UI-16 N1*"` — every one of them is the single new file
                                         # `test/test_node_team_seen.cpp`, and 52 + 100 = 152 closes exactly:
                                         #  +7 cases / +52 assertions — PART A, the PURE ring (team_seen_ring.h)
                                         #    driven with no Node, no HAL and no frame: the window/capacity
                                         #    DERIVATION pin (10 min == 2 x the default team_beacon_period_ms),
                                         #    the seeded first observation, de-duplication BY TEAM across senders,
                                         #    the SNR EWMA driven as a SEQUENCE (⛔ not max-seen, ⛔ not
                                         #    last-sample), retention-at-the-READ with its inclusive boundary,
                                         #    first-observed order under a refresh, and overflow shifting the
                                         #    STALEST out while appending the newcomer LAST.
                                         #  +6 cases / +100 assertions — PART B, the ONE write site
                                         #    (node_beacon.cpp) through the existing beacon-injection fixture:
                                         #    the landed record, the eligibility rule (mobile + non-zero id, our
                                         #    own team recorded like any other), cross-sender de-dup + retention
                                         #    at the node's read, the wire-version/parse refusals EACH WITH A
                                         #    SAME-SITE CONTROL, ZERO telemetry on the new path, and the headline
                                         #    READ-ONLY case (rt/rt_team/id_bind/peer_keys/_team_peer/_team_keys/
                                         #    content key/our team id/TX all unmoved across 36 foreign beacons).
                                         # ⛔ THE PREVIOUS ENTRY IS KEPT VISIBLE BELOW, unedited — it is history,
                                         # not a competing figure.
                                         # ---- (previous) 2026-08-22 by §UI-16 K1+K2 (the /mrteams keyring,
                                         # [[B240]]) and its QG correction: **1932 / 87788 -> 1967 / 88323**
                                         # (+35 cases / +535 assertions).
                                         # DERIVED, not merely observed — the arithmetic is written out so it can be
                                         # CHECKED rather than trusted, and 328 + 13 + 123 + 71 = 535:
                                         #  ★ THE QG ROUND (2026-08-22) moved it 1962 / 88211 -> 1967 / 88323
                                         #    (+5 cases / +112 assertions), ALL of them the three blockers' cover:
                                         #    the restore's exact match went from TWO terms to FIVE, so the boot
                                         #    section became one case PER TERM (i…v) plus the governance arm
                                         #    `QG-B1` (a key ALREADY LIVE must not survive a refusal, driven over
                                         #    all SEVEN refusing arms) — +4 cases / +95 in the keyring file — and
                                         #    `QG-B2` (a FAILED same-team re-key must not become active after a
                                         #    reboot, with its positive control) — +1 case / +15, +2 in the
                                         #    round-trip case's new governance assertions.
                                         #  +24 cases / +328 assertions — the NEW
                                         #      `test/test_firmware_team_keyring.cpp`. One case per ruled clause:
                                         #      the record ABI (`reserved` NAMED, 72/296) · the own magic + the
                                         #      `"mr"` factory-reset namespace · the FOUR read states and their
                                         #      order · `team_key_blob_init` · ★ the secret wipe guard ·
                                         #      the zero-first composition · `team_id == 0` refused with ZERO
                                         #      loads · the absent-store seed in ONE write · ★ identical material
                                         #      = ZERO writes, counted over five re-puts · one record per team
                                         #      (re-key in place) · ★★★ P-15 full = loud + four secrets
                                         #      byte-identical · the two unreadable answers · the failed save ·
                                         #      the bit-rot clamp · the exact-match restore · ★★★ P-2b (a
                                         #      cleared binding is not even READ) · the pub/priv rejection ·
                                         #      absent/unreadable = keyless · the three enum name functions ·
                                         #      ★ the WHOLE-RECORD compare (a dirty `reserved` is repaired, ⛔ not
                                         #      read as equal — the case that makes the NAMED padding load-bearing).
                                         #  +0 cases / +13 assertions — `test/test_device_nv.cpp`: the /mrteams ABI
                                         #      joins the existing "record sizes the version policy guards" case
                                         #      (sizeof 1, four offsetofs, two blob identities, offsetof(rec),
                                         #      kTeamKeyRecs, kTeamKeyVersion, the magic + its two `!=` controls),
                                         #      and `kVersion` moves 23 -> 24 in place (⛔ an NV version, never
                                         #      `wire_version`).
                                         #  +11 cases / +123 assertions — `test_firmware_provisioning_service.cpp`,
                                         #      the K2 block: create · import · ★ `team 0` clears the binding and
                                         #      RETAINS the record · a switch installs nothing · the reboot round
                                         #      trip (pins 3+4) · ★ the ORDER (the keyring write has happened when
                                         #      the /mrcfg save fails) · P-15 through the transaction · the three
                                         #      keyring-failure arms · the zero-write re-grant · the relabel.
                                         #  +0 cases / +71 assertions — the same file's `prov_err_name` case, whose
                                         #      loop is `3n + n(n-1)/2` over the arm list: n 13 -> 17 is
                                         #      (51 + 136) - (39 + 78) = +70, plus the one new spelled-out arm.
                                         # ⓘ The mutations these red are the NEW `--target=teamkeyring` (T01-T21),
                                         #   `--target=provservice` P05-P10 and `--target=devicenv` N13-N16.
                                         #
                                         # ⓘ THE PRECEDING RE-SYNC, KEPT VISIBLE — 2026-08-22 by §UI-17 slice 6 (the STATUS mark):
                                         # **1931 / 87754 -> 1932 / 87788** (+1 case / +34 assertions), ONE new case
                                         # in `test/test_firmware_ui_chrome.cpp` — ⛔ no existing case was touched,
                                         # and none could be: S6 is ASSET-ONLY (one new array in
                                         # `firmware_ui_icons.h`, one `draw_rect` -> `draw_bitmap` at the seam), and
                                         # `src/firmware_ui.cpp` is compiled by neither the native suite nor the
                                         # simulator (§B115), so the draw-site swap moves NO native count at all.
                                         # DERIVED, not merely observed — the one case, term by term:
                                         #   `chrome-icons: the 24x24 MeshRoute mark decodes to the INTERIM `MR`…`
                                         #   24 the ASCII-art decode, one CHECK per pixel ROW (the picture IS the
                                         #      specification, and the interim asset's successor re-points here)
                                         #    3 stride_of(24)==3, byte_count_of(24,24)==sizeof, sizeof==72
                                         #    5 negative controls for the decoder on BOTH asymmetric axes
                                         #    2 ⛔ not a mis-copied 7-px strip glyph (vs kIconStatus/kIconBattery)
                                         # ⓘ The mutations these red are I05/I06/I07 (`--target=icons`).
                                         #
                                         # ⓘ THE PRECEDING RE-SYNC, KEPT VISIBLE — 2026-08-22 by §UI-17 slice 8
                                         # (wake-on-receive):
                                         # **1916 / 87621 -> 1931 / 87754** (+15 cases / +133 assertions), all of them
                                         # NEW cases — ⛔ no existing case was touched, and none could be: the wake is
                                         # a new entry point (`UiModel::on_msg_wake`) plus two new call sites, and the
                                         # blank deadline is unmoved (S2's own cases still land on the same edge).
                                         # DERIVED, not merely observed — per case, in file order:
                                         #   test_firmware_ui_model.cpp — 10 cases / 72 assertions (`ui17-wake:`)
                                         #    3 a message lights a BLANKED panel + asks for the repaint (pin 1)
                                         #    3 the window is a FULL kBlankMs from the MESSAGE, not the press (pin 9)
                                         #    4 ...and the same edge from the dark side
                                         #    8 a wake while ALREADY LIT moves only the deadline (pin 10)
                                         #   13 ⛔ the wake NAVIGATES NOTHING — screen/list/cursor/both picks (pin 6)
                                         #    9 ⛔ no emergency field moves, over a RETAINED outcome (pin 7)
                                         #    4 a wake before the first tick does not consume [[B65]]'s seed (pin 8)
                                         #   12 the QUIET node blanks and sleeps as before, at FOUR uptimes —
                                         #      the fourth lands the blank inside the last window before
                                         #      the millis wrap, which is the armed flag's own ground
                                         #    4 ★ a message received 37 days ago does not REVIVE as a live
                                         #      wake (the bound; measured in the probe, not imagined)
                                         #   12 the retained multi-page modal keeps its page through the wake pass
                                         #   test_firmware_ui_send.cpp — 5 cases / 61 assertions (`ui17-wake:`)
                                         #    9 ★ the DISCRIMINATOR: the SAME post sealed wakes / cleartext does not
                                         #    8 a DM wakes SEALED or NOT (two arms — the half-applied shape)
                                         #    4 the FULL PushKind enum: every kind driven, exactly one wakes
                                         #      (pin 4) — 3 + the QG non-vacuity floor added 2026-08-22
                                         #      when the hard-coded bound became a compiler-derived one
                                         #   28 counters/stamps/dirty UNCHANGED on all four arms (pin 5)
                                         #   12 §R1 composes: a sealed reply still replies, a stranger still does not
                                         # ⓘ The mutations these red are S08-S17 (`--target=model`) and U01-U06
                                         #   (the NEW `--target=uisend`).
                                         #
                                         # ★★ AND BEFORE THAT, 2026-08-22, by §UI-17 slice 5's QG correction
                                         # (the integer-first precision rule had NO isolated control, and the case
                                         # offered as its proof stayed GREEN under the defect): **1915 / 87616 ->
                                         # 1916 / 87621** (+1 case / +5 assertions), all in
                                         # test/test_firmware_ui_geo.cpp. DERIVED, not observed:
                                         #   +5 ★ the HIGH-COORDINATE fixture (89 N latitude ULP 64, 170 E
                                         #      longitude ULP 128, against a metre's 89.83 e7 units): both legs
                                         #      render `1m` integer-first and `0m` float-first (2 + 2), plus the
                                         #      52 N vacuity guard that shows why the old walk could not see it (1)
                                         #   +1 case only: the surviving 50 m walk SPLIT into its own case with its
                                         #      scope stated honestly — its 2 assertions moved, they did not grow
                                         # ⓘ The two mutations it reddens are G17 / G18.
                                         #
                                         # ★★ AND BEFORE THAT, 2026-08-21, by §UI-17 slice 5 itself (the location
                                         # projection: the pure geo unit + the TEAM row's two filled columns):
                                         # **1893 / 87517 -> 1915 / 87616** (+22 cases / +99 assertions), all of
                                         # them in the NEW test/test_firmware_ui_geo.cpp (+16 / +61) and in
                                         # test/test_firmware_ui_team.cpp (+6 / +38). ⛔ NO EXISTING CASE WAS
                                         # TOUCHED: S4's row expectations are byte-identical because the default
                                         # fixture carries no fix and no cached position, so both new columns are
                                         # blank exactly as they were. `src/firmware_ui.cpp` compiles in neither the
                                         # native suite nor the simulator, so nothing else could move.
                                         # DERIVED, not merely observed — per case, in file order:
                                         #   test_firmware_ui_geo.cpp (61)
                                         #    7 the 600 s bound: 599 / 600 / 601 + the constant + `ui_geo_fresh`
                                         #    2 `0xFFFFFFFF` is the cache's UNDATEABLE, and it blanks
                                         #    5 each of the four terms blanks on its own (+ the shown arm)
                                         #    3 a cache MISS is BLANK, and `0m` is a real and different answer
                                         #    8 ★ the coincident RULING: `0m`, blank dir, no cardinal, the bucket
                                         #    3 all eight bearings + the ruled S-14 lexemes + their bound
                                         #    3 the octant boundary driven either side of 22.5 degrees
                                         #    2 negative coordinates, all four quadrants (NE and SW)
                                         #    4 ★ the antimeridian pair, read from both ends
                                         #    2 an ORDINARY longitude pair is untouched by the fold
                                         #    2 nearby peers survive the float mantissa (the 50 m walk)
                                         #    4 the ruled distance table + its truncation
                                         #    2 the saturation token and the NaN arm
                                         #    3 the bucket-vs-token `iff` sweep + vacuity + never-zero
                                         #    8 the bucket is what the PANEL draws, not the raw inputs
                                         #    3 the bucket and the columns agree, swept together
                                         #   test_firmware_ui_team.cpp (38)
                                         #    5 a located teammate fills DIST/DIR at exactly 19 columns
                                         #    4 a STALE position blanks (600 shows, 601 and UNDATEABLE blank)
                                         #    2 no own fix ⇒ blank, however fresh the peer is
                                         #    3 a COINCIDENT teammate draws `0m` and a blank direction
                                         #    9 a distance token that turns repaints; a drift inside it does not
                                         #   15 the octant / the freshness bound / our own fix each repaint alone
                                         # ⓘ The last two figures include `team_settle`'s own 3 CHECKs, which is
                                         # why the invalidation cases run higher than their visible count.
                                         #
                                         # ★★ AND BEFORE THAT, by §UI-17 slice 4 (the TEAM row's pure
                                         # unit + the F-8 repaint invalidation): **1877 / 87411 -> 1893 / 87517**
                                         # (+16 cases / +106 assertions), ALL of them in the NEW
                                         # test/test_firmware_ui_team.cpp — no existing case was touched, and
                                         # `src/firmware_ui.cpp` compiles in neither the native suite nor the
                                         # simulator, so nothing else could move. DERIVED, not merely observed —
                                         # per case, in file order:
                                         #    4 the ruled 19-column row, passive and marked
                                         #    6 the `%-6.6s` clamp: a long name, a `0x<hash>`, the widest label
                                         #    6 the label widths: padded / exactly six / empty
                                         #    3 `--` for an unknown route age, and `0s` as its own state
                                         #    3 the two RESERVED columns present and blank (+ the width identity)
                                         #    2 the `BACK` row's one spelling and its fit
                                         #    3 the age token's ruled table, its bound, and ⛔ not `old`
                                         #    5 the bucket-vs-token `iff` sweep (3 sweeps, accumulated) + 3 pins
                                         #    7 the F-8 gap closed: a turned token repaints a LIT screen
                                         #    7 a raw age moving INSIDE its bucket raises nothing
                                         #    9 an equal projection raises nothing and ⛔ clears nothing
                                         #    8 the screen gate: no repaint asked for from INBOX
                                         #    8 ★ the BODY gate: the 10-row visibility matrix (1) + a DM compose
                                         #        opened FROM team, driven by real gestures (4) + the settle's 3
                                         #   15 blanked: raises, never unblanks/clears/opens, survives the dark,
                                         #        and ★ `ui_allows_sleep` stays TRUE across the token turns
                                         #    9 the label's DRAWN PREFIX (a rename past column 6 is invisible)
                                         #   11 positional + bounded by `team_shown` (swap · tail · fewer rows)
                                         # ⓘ Three of those figures include `team_settle`'s own 3 CHECKs (the
                                         # screen the gesture reached, plus lit + clean), which is why the
                                         # invalidation cases run higher than their visible assertion count.
                                         #
                                         # ★★ AND BEFORE THAT, by §UI-17 slice 3's QG frame-freeze
                                         # remedy (row 4 reads the FROZEN snapshot, so `UiSnapshot` gained
                                         # own_fix/own_lat_e7/own_lon_e7): **1876 / 87407 -> 1877 / 87411**
                                         # (+1 case / +4 assertions), all in test/test_firmware_ui_status.cpp.
                                         # DERIVED, not observed: +1 `ui17-status:` the own_fix-is-the-authority
                                         # case — 4 (the two combinations `build_snapshot` cannot produce, i.e.
                                         # a cleared own_fix over live coordinates and the converse · the
                                         # verbatim-fields row · the reboot fact still outranking all of them).
                                         # ⛔ No existing assertion moved: the row-4 cases were re-expressed
                                         # through a `loc()` helper that fills the same three snapshot fields,
                                         # which changes how they are DRIVEN and not how many there are.
                                         #
                                         # ★★ AND BEFORE THAT, by the slice's first pass (the STATUS body):
                                         # **1865 / 87296 -> 1876 / 87407** (+11 cases / +111 assertions), all of
                                         # them in the NEW test/test_firmware_ui_status.cpp — no existing case was
                                         # touched, and `src/firmware_ui.cpp` compiles in neither the native suite
                                         # nor the simulator, so nothing else could move. DERIVED, not observed —
                                         # per case, in file order:
                                         #    8 row 0, the eight uppercase hex + `NO TEAM`
                                         #    9 row 1, `ME T<n>` / blank / `ME NO ID`
                                         #   10 row 2, `KNOWN` + the 0 / 9 / 10 / 255 saturation
                                         #    7 row 2, `NO TEAM KEY` over the count + the two silences
                                         #    9 row 3, the combined count and the 99+ crossing
                                         #    8 row 3, `HOME --` vs the omitted half
                                         #    8 row 4, the RESTART-over-coordinates priority
                                         #   11 row 4, `NO LOCATION` + the five `have_fix` arms
                                         #   14 row 4, truncation, four quadrants, `-0.000`, the widths
                                         #    5 the gateway_heltec shape, all five rows
                                         #   22 the width sweep: 6 direct + a 4-row loop x 4 checks (the
                                         #        no-configuration-text assertions), which is why the file's 99
                                         #        static CHECK sites execute as 111
                                         # ⓘ ⛔ THE PIN DID **NOT** MOVE for QG's BOTH-DUE BOUNDARY fix (same day,
                                         # same slice), and the arithmetic is recorded so nobody reads that as an
                                         # unverified claim: the fix STRENGTHENED two existing assertions rather
                                         # than adding any. In each of the two `ui17-hold:` page cases a
                                         # capture-then-`!= 0` check (which DEFINED whatever the crossing tick left
                                         # behind as correct) was replaced by a LITERAL `detail_page == 2`.
                                         # ⇒ -1 +1 twice = 0; no case was added or removed, and the clean baseline
                                         # gate re-ran at exactly 1865 / 87296 / 0.
                                         # ★★ CROSS-CHECK SYNCED 2026-08-21 by §UI-17 slice 2's QG remedy (the
                                         # RETAINED MODAL'S PAGE — the cadence suspended while dark, and restarted
                                         # WITHOUT moving the page on the wake):
                                         # **1864 / 87281 -> 1865 / 87296** (+1 case / +15 assertions), both in
                                         # test/test_firmware_ui_model.cpp. DERIVED, not observed:
                                         #   +1 `ui17-hold:` the multi-page dark/wake case — 13 (the armed
                                         #        7-page fixture 2 · the nonzero starting page 1 · the blank
                                         #        edge 2 · ★ the DARK half, 3 · ★ the REAL-ORDER wake pass, 3 ·
                                         #        the cadence resuming a full period later, 2)
                                         #   +2 in the REWRITTEN pin-2 case, whose ONE-PAGE fixture was vacuous
                                         #        (`detail_pages == 1` gates the cadence off by its own term, so
                                         #        `detail_page == 0` held whatever the code did): the fixture is
                                         #        now the 7-page body, +`detail_pages == 7`, +the nonzero
                                         #        `page_at_blank` guard, and the page assertion compares against
                                         #        the captured value instead of the literal 0.
                                         #   ⇒ 13 + 2 = +15.
                                         # ⓘ THE PREVIOUS PIN, kept because its derivation is still the record of
                                         # how the figure below it was reached:
                                         # ★★ CROSS-CHECK SYNCED 2026-08-21 by §UI-17 slice 2 (§3.3 retention
                                         # conformance — the two `kBlankMs` modal auto-exits DELETED, §9 R-1):
                                         # **1861 / 87209 -> 1864 / 87281** (+3 cases / +72 assertions), all of
                                         # them in test/test_firmware_ui_model.cpp and
                                         # test/test_firmware_ui_send.cpp. DERIVED, not merely observed —
                                         # the three NEW `ui17-hold:` cases first:
                                         #   +1 `ui17-hold:` compose — 13 (the non-default selection 4 ·
                                         #        across the blank 5 · the consumed wake 3 · ⛔ nothing sent 1)
                                         #   +1 `ui17-hold:` detail — 16 (the opened record 6 · across the
                                         #        blank 6 · the consumed wake 3 · ⛔ the store untouched 1)
                                         #   +1 `ui17-hold:` the emergency exception — 16 (arm (a) the detail
                                         #        modal with `delete` ARMED 7 · arm (b) compose, `long_arm`
                                         #        keeps it and `long_fire` closes it 9)
                                         #   ⇒ 13 + 16 + 16 = +45 in the new cases.
                                         # ⓘ AND +27 IN TEN **REWRITTEN** cases, each of which pinned an
                                         #   auto-exit this slice deletes and now pins the RETENTION plus the
                                         #   unmoved blank deadline (spec S2 pin 5) in its place:
                                         #   `the sub-view SURVIVES inactivity` +3 · `open on BOTH sides of
                                         #   the blank edge` +2 · `a gesture … refreshes the BLANK window` +5 ·
                                         #   `blanking KEEPS the modal` +2 · `the blank is wrap-safe with a
                                         #   modal open` +2 · `ui7-result: the result phase RIDES the blank`
                                         #   +5 · `ui7d-modal: paging does NOT postpone the deadline` +3 ·
                                         #   `ui7d-modal: an armed delete survives the blank` +2 ·
                                         #   `ui7-slot: a late_ack slot is released…` +1 ·
                                         #   `ui7-slot: an UNANSWERED late_ack slot…` +2.
                                         #   ⇒ 45 + 27 = +72.
                                         # ⓘ ⛔ ZERO cases were added or removed: every one of the ten is a
                                         #   REWRITE IN PLACE (the §B101/[[B232]] precedent), each carrying a
                                         #   heading that states what it used to pin.
                                         # ⓘ THE PREVIOUS PIN, kept because its derivation is still the record of
                                         # how the figure below it was reached:
                                         # ★★ RE-PINNED 2026-08-21 by §UI-17 slice 1's QG remedy (the three
                                         # DUPLICATED TEAM/INBOX decisions hoisted into shared pure helpers):
                                         # **1858 / 87183 -> 1861 / 87209** (+3 cases / +26 assertions), all of
                                         # them in test/test_firmware_ui_model.cpp. DERIVED, not observed:
                                         #   +1 `ui17-len:` — 6 (three PASSIVE widths incl. the cap · the empty
                                         #        entered list · a 3-row list · the cap + its exit row)
                                         #   +1 `ui17-act:` — 11 (the passive arm 3 · member/leave 4 · ★ the
                                         #        ORDER, refusal over BACK, 3 · entry outranks a stale refusal 1)
                                         #   +1 `ui17-note:` — 9 ([[B223]]'s unreachable arm 2 · the passive
                                         #        keep 2 · record 2 · retire-on-BACK 3)
                                         #   ⇒ 6 + 11 + 9 = +26 assertions; +3 cases.
                                         # ★★ THE HOIST ITSELF MOVED **NOTHING**, and that is the refactor's own
                                         #   proof rather than a claim: the extracted tree ran 1858 / 87183 / 0 —
                                         #   the previous pin exactly — BEFORE these three cases were added.
                                         # ⓘ THE PREVIOUS PIN, kept because its derivation is still the record of
                                         # how the figure below it was reached:
                                         # ★★ RE-PINNED 2026-08-20 by §UI-17 slice 1 (TEAM/INBOX passive ↔
                                         # interactive): **1843 / 87045 -> 1858 / 87183** (+15 cases / +138
                                         # assertions), all of them in test/test_firmware_ui_model.cpp.
                                         # DERIVED, not merely observed — the fifteen NEW `ui17-` cases first:
                                         #   +1 `ui17-lex:` — 5 (the word · it EQUALS both shipped row tables'
                                         #        spelling, which is what "one spelling" means · the width ·
                                         #        the non-empty floor)
                                         #   +1 `ui17-rowkind:` — 5 (two member rows · the row AFTER the
                                         #        published ones · the FAILS-CLOSED row past the end · the
                                         #        empty list's single row)
                                         #   +1 `ui17-entered:` — 11 (STATUS 1 · SEND 1 · TEAM 2 · INBOX 2 ·
                                         #        the four `Settings` arms 4 · `Screen::count` 1)
                                         #   +1 `ui17-reset:` ([[B223]], driven at the pure helper) — 5
                                         #        (changed+value · idempotent+value · the passive arm)
                                         #   +1 `ui17-passive:` the LANDING — 6 (screen/view/cursor on TEAM ·
                                         #        ONE press to INBOX + its view · ONE press to SEND)
                                         #   +1 `ui17-passive:` the entering double — 11 (TEAM: view 1 ·
                                         #        screen 1 · cursor 1 · 3 negative-space checks · INBOX:
                                         #        screen/view 2 · view 1 · modal 1 · zero storage 1)
                                         #   +1 `ui17-walk:` the CONTAINED walk — 14 (TEAM: 3 walked rows ·
                                         #        the BACK landing 3 · the wrap 3 · nothing performed 1 ·
                                         #        INBOX: the BACK landing 2 · the wrap 2)
                                         #   +1 `ui17-back:` — 8 (the landing 3 · negative space 2 · the
                                         #        press that then passes 1 · the next lap 2)
                                         #   +1 `ui17-empty:` — 13 (TEAM 8 over the entry/one-row/exit ·
                                         #        INBOX 5)
                                         #   +1 `ui17-refuse:` TEAM, the ORDER — 8 (the pick 1 · the row's
                                         #        kind 1 · the refusal 3 · the view NOT closed 1 · the
                                         #        recovery 2)
                                         #   +1 `ui17-refuse:` INBOX — 6 (the row's kind 1 · the refusal 3 ·
                                         #        the view 1 · the recovery 1)
                                         #   +1 `ui17-retain:` blank/wake — 7
                                         #   +1 `ui17-emergency:` — 8 (TEAM 5 over arm+cancel · INBOX 2 ·
                                         #        the modal's own close 1)
                                         #   +1 `ui17-detail:` — 6
                                         #   +1 `ui17-passive:` no pick ⇒ no refusal — 9 (TEAM 4 over
                                         #        replace/empty · INBOX 5)
                                         #   ⇒ 5+5+11+5+6+11+14+8+13+8+6+7+8+6+9 = 122 in the new cases.
                                         # ⓘ AND +16 IN FIVE **REWRITTEN** CASES, whose press prefix moved by
                                         #   exactly one `double` (the entering press) and whose ends now land
                                         #   on the BACK row instead of the next screen:
                                         #   `short press is SCREEN-AWARE` +3 · `INBOX is list-aware too` +3 ·
                                         #   `a full team roster walks…` +3 · `B64 — the refusal is retired…`
                                         #   +6 · `sub-view auto-exits…` +1. ⇒ 122 + 16 = +138.
                                         # ⓘ ⛔ ZERO cases were added or removed for the ~40 OTHER re-pointed
                                         #   call sites (the `to_team` / `to_inbox` / `to_inbox_ticks`
                                         #   prefixes, and the same one-press prefix in
                                         #   test_firmware_ui_send.cpp and test_firmware_ui_chrome.cpp): the
                                         #   subject of each is unchanged and none gained an assertion.
                                         # ⓘ THE PREVIOUS PIN, kept because its derivation is still the record
                                         # of how the figure below it was reached:
                                         # ★★ RE-PINNED 2026-08-20 by [[B232]] (the SETTINGS single entry):
                                         # **1837 / 86986 -> 1843 / 87045** (+6 cases / +59 assertions), all of
                                         # them in test/test_firmware_ui_model.cpp. DERIVED, not merely observed:
                                         #   +1 `b232-entry: SETTINGS LANDS CLOSED …` — 9 (the landing 3 · the
                                         #        one press that passes it 2 · the press-per-LAP measurement,
                                         #        3 arrival checks over three laps + the `on_settings == 3`)
                                         #   +1 `b232-entry: a double ENTERS the menu at its FIRST row` — 9
                                         #        (the landing 1 · the transition 3 · the row BY IDENTITY 2 ·
                                         #         the 3 negative-space checks that the entry press performed
                                         #         nothing at all)
                                         #   +1 `b232-entry: the entry row's label …` — 3
                                         #   +1 `b232-open: the ConfigService is OPENED ON ARRIVAL …` — 6
                                         #        (the closed landing 1 · is_open 1 · zero writes/applies 2 ·
                                         #         the latch raised from the closed view 1 · still closed 1)
                                         #   +1 `b232-remedy: the remedy WORDS stand from the CLOSED view` — 12
                                         #        (arm (a) 2 · arm (b) 5 · arm (c) 4 · the note row 1)
                                         #   +1 `b232-exit: BOTH menu exits …` — 13 (the BACK exit 4 · the
                                         #        re-entry-on-row-0 block 4 · the walk-off exit 3 · the press
                                         #        that only THEN passes the screen 2)
                                         #   +0 `ui14-cycle:` — SAME case, +3: the walk off the last row now
                                         #        lands on the CLOSED view (screen / arm / cursor) before the
                                         #        press that leaves the screen, which it already asserted.
                                         #   +0 `ui14-back:` +1 · `ui15-close:` +2 · `ui14-open:` +1 — SAME
                                         #        cases: BACK's landing is RE-POINTED to the closed view and
                                         #        the press that then leaves the screen is driven as well.
                                         #   ⇒ 6 cases; 9 + 9 + 3 + 6 + 12 + 13 = 52 in the new cases, plus
                                         #     3 + 1 + 2 + 1 = 7 in the four corrected ones ⇒ +59.
                                         # ⓘ ⛔ THE PIN DID **NOT** MOVE FOR QG's UNAVAILABLE-CONFIG CORRECTION
                                         #   (same day, same slice), and the arithmetic is recorded so nobody
                                         #   reads that as an unverified claim: three cases were RETARGETED off
                                         #   the invisible-row walk — `ui14-open` (store) 11 -> 11, `ui14-open`
                                         #   (unattached) 5 -> 6, `ui15-gate` 9 -> 8. ⇒ +1 -1 = 0, and no case
                                         #   was added or removed. The clean baseline gate re-ran at exactly
                                         #   1843 / 87045 / 0.
                                         # ⓘ THE PREVIOUS PIN, kept because its derivation is still the record of
                                         # how the figure below it was reached:
                                         # ★★ RE-PINNED 2026-08-20 by [[B230]] (the `incomplete PHY` remedy text):
                                         # **1835 / 86848 -> 1837 / 86986** (+2 cases / +138 assertions). DERIVED,
                                         # not merely observed:
                                         #   +1 `§B230 the incomplete-PHY refusal names WHICH part is missing`
                                         #        in test/test_firmware_provisioning_service.cpp — 15 assertions
                                         #        over FOUR blocks: pin 1 (empty sf_list + a COMPLETE tail, 4) ·
                                         #        pin 2's named control (sf_list PRESENT, no bw anywhere, 4) ·
                                         #        ★ the ORDER (both broken -> the sf_list arm WINS, 4) · the
                                         #        non-vacuity pair (fill the set and NOTHING else -> applied, 3)
                                         #   +1 `§B230 prov_err_name is total, distinct and panel-sized` — 121
                                         #        assertions, and the figure is arithmetic rather than a count:
                                         #        1 (the arm census, n == 13) + 13x3 = 39 (non-empty · never the
                                         #        `?` fall-through · <= 15 columns for the OLED's §7.3 body) +
                                         #        13x12/2 = 78 (every PAIR distinct) + 2 token spellings + 1 the
                                         #        out-of-enum floor. ⇒ 1 + 39 + 78 + 3 = 121.
                                         #   +1 assertion in the SAME `§PROV-TX an incomplete STAGED PHY refuses`
                                         #        case — arm (b) now asserts `allowed_sf_bitmap != 0`, so "this is
                                         #        the sf_list-PRESENT arm" is measured, not assumed
                                         #   +1 assertion in the SAME `§UI15-PROV a STAGING refusal carries the
                                         #        SERVICE's own typed reason` case — the new arm's token
                                         #   ⇒ 15 + 121 + 1 + 1 = +138 assertions; 1 + 1 = +2 cases.
                                         # ⓘ ⛔ ZERO cases moved for a CHANGED OUTCOME: three existing assertions
                                         #   (§PROV-TX arm (a), §B211 pin 5, §UI15-PROV's incomplete-PHY case)
                                         #   were RE-POINTED from `incomplete_phy` to `sf_list_empty` in place —
                                         #   same verdict, same zero writes, same zero live calls.
                                         # ⓘ THE PREVIOUS PIN, kept because its derivation is still the record of
                                         # how the figure below it was reached:
                                         # ★★ RE-PINNED 2026-08-20 by the [[B231]]/[[B233]] inbox-list pair:
                                         # **1829 / 86790 -> 1835 / 86848** (+6 cases / +58 assertions), all of
                                         # them in test/test_firmware_ui_model.cpp. DERIVED, not merely observed:
                                         #   +2 `ui7-inbox B231:` — the newest-at-top order WITH the untouched
                                         #        block order in the same case (a DM-only check would pass on an
                                         #        implementation that had started interleaving), and the
                                         #        one-row / empty-block edge of the two reversed loops
                                         #   +3 `ui7d-B233:` over a new TICK HARNESS (`InboxTick`, which replays
                                         #        `mr_ui_tick`'s ORDER — build, gesture, tick, serve the erase,
                                         #        freeze): the delete-MIDDLE regression, the delete-LAST arm that
                                         #        already worked, and the FAILED delete that owes no repaint
                                         #   +1 `ui7d-B231:` — §B64's identity rule driven from the new direction
                                         #        (an arrival now pushes the rows DOWN, not up)
                                         #   +0 `ui7-inbox: within a kind the NEWEST rows win` — SAME case, +4
                                         #        assertions: the surviving SET is now asserted row by row, so
                                         #        retention and presentation order cannot be confused
                                         # ⇒ 2 + 3 + 1 = +6 cases; 13 + 7 + 13 + 8 + 5 + 8 + 4 = +58 assertions.
                                         # ⓘ THE PREVIOUS PIN, kept because its derivation is still the record of
                                         # how the figure below it was reached:
                                         # ★★ RE-PINNED 2026-08-20 by §UI-15 slice 6 (the static-join adapter,
                                         # the ASYNC outcome and the four join screens):
                                         # **1800 / 86200 -> 1829 / 86790** (+29 cases / +590 assertions).
                                         # ⚠ THE ASSERTION FIGURE MOVED TWICE DURING THE SLICE — 86781 -> 86789
                                         # -> 86790 — and the moves are recorded because each one is the
                                         # instrument WORKING rather than noise:
                                         #   · the first `--target=uijoin` run reported J02/J13/J20 UNUSABLE —
                                         #     three cases that DESCRIBED a property and MEASURED a neighbouring
                                         #     one (term 2's shape was already separated by term 3; the fault
                                         #     lists were EMPTY, so a dropped verdict gate found nothing anyway;
                                         #     a full-width label truncated to the same answer a `%s` would give).
                                         #     Re-shaped: +8 assertions, and all three then went RED.
                                         #   · the first `--target=model` run reported the slot-vs-index entry
                                         #     UNUSABLE for the same class of reason — its fixture held slots 1
                                         #     and 2, where the ROW INDEX and the SLOT NUMBER coincide. Re-shaped
                                         #     to slots 1 and 3: +1 assertion.
                                         # ⓘ DERIVED, NOT MERELY OBSERVED — which is what proves the slice-5 pin
                                         # below was still exact:
                                         #   +12 test/test_firmware_ui_join.cpp, the NEW pure-unit suite
                                         #        (the correlation CONTROL · the four TERMS one at a time · the
                                         #         kind gate over every PushKind and every JoinRefuseReason ·
                                         #         trap 2 above layer 15 · the four-state store matrix as panel
                                         #         text · the select row list · the label / value / waiting /
                                         #         node strings)
                                         #   +8  `§UI15-JOIN` in test/test_firmware_ui_prov.cpp, the ADAPTER half
                                         #        (control · the ONE integral->double conversion · layer 17's
                                         #         full-byte/nibble split · the empty-slot floor · a domain
                                         #         refusal · a failed save · an unreadable /mrcfg · the
                                         #         four-state profile read)
                                         #   +10 `ui15-entry:` + the nine `ui15-join:` cases in
                                         #        test_firmware_ui_model.cpp (the entry's ONE read · the store
                                         #         matrix on the screen · BACK costs nothing on all four arms
                                         #         while CONFIRM drives exactly one · JOINING-never-JOINED · the
                                         #         refusal/save landings · the four uncorrelated shapes by name ·
                                         #         every JoinRefuseReason ignored · the 60 s word change and
                                         #         BACK-cancels-nothing · close-on-leave + pre-emption over the
                                         #         four new arms · trap 2 through the screen)
                                         #   -1  ...MINUS `ui15-pending`, whose whole content was that activating
                                         #        JOIN NETWORK does NOTHING — the state this slice ends. It is
                                         #        WITHDRAWN IN PLACE (a comment where it stood), and `ui15-entry`
                                         #        carries the property it really held.
                                         #   +0  `chrome4-audit:` widened to the three new outcomes and to the
                                         #        join screens' seven strings — same case, more assertions
                                         #   +0  `ui15-hide:`'s landing moved from `menu` to `join_select` (the
                                         #        supported child now HAS a flow) — same case
                                         #   ⇒ 12 + 8 + 10 - 1 = +29 cases.
if os.environ.get("MR_MUT_BASE"):
    PIN_CASES, PIN_ASSERTS = (int(x) for x in os.environ["MR_MUT_BASE"].split(","))

# ★ THE DERIVED BASELINE, filled in by the baseline gate below from the clean run's own output and used by the exit
#   gate on the restored source. Declared here so the two gates read one pair of names, and so a reader of the exit
#   gate can see WHERE the figure it demands came from — it is this run's measurement, never a literal.
BASE_CASES = BASE_ASSERTS = None
_pin_stale = None      # set to (pinned, derived) by the gate when the cross-check above disagrees; re-announced at the end.

# ★★★ RESTORATION IS GUARANTEED, NOT HOPED FOR. This tool EDITS A REAL SOURCE FILE in an uncommitted tree, so an
#     exception, a Ctrl-C or a SIGTERM between the write and the restore would leave a MUTATION INSTALLED — and the next
#     reader would be looking at a deliberately broken header with no marker saying so. Three layers, because each
#     covers what the others cannot:
#       1. `try/finally` around the whole run       -> exceptions, including the KeyboardInterrupt a SIGINT raises;
#       2. `atexit` + SIGTERM/SIGHUP handlers        -> signals that do not raise on their own;
#       3. a SIDECAR BACKUP + MARKER outside the repo -> a `kill -9` or a power cut, recovered on the NEXT run.
#     ⓘ The backup lives in the system temp dir, never in the repository: this tool must add no file the owner could
#       accidentally commit.
#
# ⛔⛔ AND THE BACKUP PATH IS KEYED BY THE RESOLVED SOURCE PATH, WHICH IS NOT A DETAIL — THE FIRST VERSION USED ONE
#     GLOBAL DIRECTORY AND THAT MADE THE SAFETY MECHANISM A CORRUPTION VECTOR. This repository routinely runs from
#     WORKTREES (`/tmp/mr-arm`, `~/b162-arms/*`, the whole §B162 arm ladder), and with a single shared `INFLIGHT` marker
#     and a single shared backup file, worktree A's run could restore ITS header over worktree B's, and B's
#     recover-on-start path could install A's backup into B. ⇒ the directory name carries a hash of
#     `Path(src).resolve()`, so two trees can never see each other's backup or marker, and the marker RECORDS the path it
#     belongs to so a recovery that somehow reached the wrong tree refuses instead of overwriting.
# ★ AND A CONCURRENT RUN ON THE SAME PATH IS REFUSED, not merely warned about: two runs mutating one file would each
#   restore "the original" the other had already replaced. The lock is `fcntl.flock`, so it is released by the KERNEL if
#   the holder dies — a lock file left behind cannot wedge the tool the way a stale marker would.
_BK_KEY = hashlib.sha1(str(Path(H).resolve()).encode()).hexdigest()[:16]
_BK_DIR = os.path.join(tempfile.gettempdir(), f"mr_ui7d_mutation_backup-{_BK_KEY}")
_BK_FILE = os.path.join(_BK_DIR, os.path.basename(H))
_BK_MARK = os.path.join(_BK_DIR, "INFLIGHT")
_BK_LOCK = os.path.join(_BK_DIR, "LOCK")
_lock_fh = None

# ⛔⛔⛔ AND THE SOURCE LOCK ABOVE IS THE WRONG GRANULARITY FOR THE BUILD — THE FIFTH DEFECT OF THIS RUNNER, AND THE
#      PATTERN BEHIND ALL FIVE IS WORTH MORE THAN THE FIX: each round chose the granularity of the LAST FAILURE
#      instead of the granularity of the RESOURCE BEING PROTECTED. The sidecar backup and the in-flight marker guard a
#      FILE, so keying them per resolved source path is right. ⇒ But every target — `model` and `config` alike —
#      compiles into the SAME `.pio/build/native`, which is a property of the TREE, not of the file. Two runs holding
#      DIFFERENT source locks therefore still collide there: they can compile both mutants into one binary, or race so
#      that one measures the other's build, and either way the verdict is a FALSE RED (or a false green) on a property
#      nothing measured. ⇒ a SECOND lock, keyed by the BUILD DIRECTORY, and this one WAITS rather than refusing: the
#      first run is not doing anything wrong, it just owns the compiler for a while.
#  ★ ASK WHAT RESOURCE EACH GUARD PROTECTS. backup/marker -> the file (per path). build lock -> the build tree (per
#    tree). Order is fixed — source lock first, then build lock — at the single call site, so the pair cannot deadlock.
_BUILD_DIR = os.path.join(ROOT, ".pio/build/native")
_BUILD_KEY = hashlib.sha1(str(Path(_BUILD_DIR).resolve()).encode()).hexdigest()[:16]
_BUILD_LOCK = os.path.join(tempfile.gettempdir(), f"mr_mutation_buildlock-{_BUILD_KEY}")
_build_lock_fh = None
# ⛔⛔ ONLY THE PROCESS THAT ARMED THE BACKUP MAY EVER RESTORE FROM IT, and this flag is that rule. It was ADDED AFTER A
#     MEASURED DEFECT IN THE FIX ABOVE: `atexit` is registered at import time, so a run that ABORTED at the lock (a
#     concurrent second run) still called `restore()` — found the sidecar backup left by the run that DID hold the lock,
#     saw the md5 differ because that run had a mutation installed, and COPIED THE PRISTINE FILE OVER ITS LIVE MUTANT.
#     ⇒ the holder's next build would have compiled the ORIGINAL and reported "the suite still PASSES; nothing measures
#     this" — a FALSE FAIL, i.e. the safety mechanism silently corrupting a measurement. Observed once, in Proof D.
# ⓘ ONE MORE MEASURED PROPERTY OF THE BASELINE GATE, worth knowing because it looks like a failure and is not: after a
#   `kill -9` mid-build the recovery restores the SOURCE, and the gate then legitimately ABORTS on the half-built binary
#   left behind (observed: `1464 / 80934 / 1`). ⇒ the gate catches a corrupted BUILD state as well as a red tree. Clear it
#   with `rm -rf .pio/build/native` and re-run; do not "fix" it by loosening the gate.
_armed = False


def take_lock():
    """Refuse a second concurrent run against THIS source path. Returns False if another run holds it."""
    global _lock_fh
    os.makedirs(_BK_DIR, exist_ok=True)
    _lock_fh = open(_BK_LOCK, "w")
    try:
        fcntl.flock(_lock_fh.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        _lock_fh.close(); _lock_fh = None
        return False
    _lock_fh.write(f"{os.getpid()}\n{Path(H).resolve()}\n"); _lock_fh.flush()
    return True


def take_build_lock():
    """SERIALISE against every other run compiling into this tree's `.pio/build/native` — see the block above.

    ⚠ BLOCKING, deliberately, and it is the one place this tool waits: refusing (as the per-source lock does) would be
    right for two runs mutating ONE FILE — where each would restore what the other wrote — but here the second run is
    perfectly legitimate, it just cannot compile yet. Waiting turns a collision into a queue and loses no measurement.
    The wait is announced, so a run that looks hung says why and names the holder.
    """
    global _build_lock_fh
    _build_lock_fh = open(_BUILD_LOCK, "a+")
    try:
        fcntl.flock(_build_lock_fh.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        _build_lock_fh.seek(0)
        holder = _build_lock_fh.read().strip().replace("\n", " ") or "(unknown)"
        print(f"  WAITING for the shared native build directory {_BUILD_DIR}", flush=True)
        print(f"    another mutation run holds it [{holder}] — every target builds into ONE directory, so running "
              f"both at once would measure the wrong binary. Queuing.", flush=True)
        fcntl.flock(_build_lock_fh.fileno(), fcntl.LOCK_EX)          # ★ wait, do not refuse
        print("  ACQUIRED the build lock — the previous run has finished and restored its source", flush=True)
    _build_lock_fh.seek(0); _build_lock_fh.truncate()
    _build_lock_fh.write(f"pid {os.getpid()} target {_TARGET} src {Path(H).resolve()}\n"); _build_lock_fh.flush()


def _md5(p):
    return hashlib.md5(open(p, "rb").read()).hexdigest()


def _md5_text(s):
    return hashlib.md5(s.encode()).hexdigest()


def _write_mark(**kw):
    """The in-flight marker: which path, which ORIGINAL, and — while one is installed — which MUTANT."""
    m = {"path": str(Path(H).resolve())}
    m.update(kw)
    with open(_BK_MARK, "w") as f:
        json.dump(m, f, indent=1)


def note_mutant(md5_hex, label):
    """Record the hash we are ABOUT TO install, so a crashed run can be told apart from a foreign edit.

    ⚠ CALLED BEFORE THE WRITE, not after, and that ordering is load-bearing: recording afterwards leaves a window in
    which the mutant is in the tree but no marker names it, so a crash there would make our OWN mutant look FOREIGN.
    Recording first is strictly safer — a crash between the record and the write leaves the file at the ORIGINAL,
    which the classifier names correctly and acts on by doing nothing.
    """
    _write_mark(orig_md5=_md5(_BK_FILE), mutant_md5=md5_hex, label=label)


# ===== ★★★ THE ONE GUARDED WRITE, AND WHY IT IS ONE ================================================================
# ⛔⛔⛔ THE SIXTH DEFECT OF THIS RUNNER WAS THE FOURTH ONE LEFT UNFIXED IN THE OTHER CODE PATH. Round 4 added a
#      three-arm hash check (original / mutant / FOREIGN) — but only to CRASH RECOVERY, because that is where the bug
#      had been observed. The NORMAL loop kept a two-state write: it installed each mutant unconditionally and then
#      restored unconditionally from an in-memory copy captured minutes earlier, never looking at what was actually in
#      the file. ⇒ ★ AND THE UNGUARDED WINDOW WAS THE WORST ONE IN THE TOOL: `run_suite()` sits between those two
#      writes — a full native build plus test run — which is exactly when a concurrent agent is most likely to touch
#      the source. An edit made there was silently erased, with no trace and no message.
#
# ★★★ THE RULE THIS PRODUCED, AND IT IS WORTH MORE THAN THE FIX — A GUARD BELONGS TO THE INVARIANT, NOT TO THE
#     INCIDENT. Every path that can violate the invariant must call the SAME primitive. A guard installed only where
#     the bug was observed is not a guard; it is a patch that makes the next occurrence harder to see.
#     ⇒ the invariant here is ⛔ "NEVER OVERWRITE A HASH I CANNOT NAME", and `guarded_write` is its only enforcement
#       point. FOUR callers, no copies: the mutation loop's install, the loop's restore, the `finally`, and
#       `restore()` (which the signal handlers and `atexit` share).
#
# ⓘ THE SIX DEFECTS, AND THE SCOPE EACH FIX WAS WRONGLY GIVEN — kept because the pattern is the finding:
#     1. a backup shared across worktrees   -> fix: key the backup by path      | invariant: one tree's file is never
#     2. `atexit` cross-restore             -> fix: the `_armed` flag           |   touched by another tree's run
#     3. a stale mutant binary left behind  -> fix: an exit gate                | invariant: restoration is proved at
#                                                                              |   the BUILD level, not the file level
#     4. a foreign edit clobbered           -> fix: three arms IN RECOVERY ONLY | invariant: never overwrite a hash I
#     6. the same clobber on the NORMAL path-> fix: THIS PRIMITIVE, 4 callers   |   cannot name  <- #6 IS #4's invariant
#     5. the lock at the wrong granularity  -> fix: a build-directory lock      | invariant: guard the RESOURCE, not
#                                                                              |   the last failure
#   ★ Five of the six fixes were scoped to the incident. Ask what INVARIANT the guard protects, then find every path.
def _marker():
    try:
        return json.load(open(_BK_MARK))
    except (ValueError, OSError):
        return {}


def classify_source(cur=None):
    """Name the current contents of `H`: 'original' | 'mutant' | 'foreign'. ⛔ THE TERNARY, in one place."""
    if cur is None:
        cur = _md5(H)
    m = _marker()
    orig = m.get("orig_md5") or (_md5(_BK_FILE) if os.path.exists(_BK_FILE) else None)
    mut = m.get("mutant_md5")
    if orig and cur == orig:
        return "original"
    if mut and cur == mut:
        return "mutant"
    return "foreign"


def _preserve_and_exit(cur, context):
    """⛔⛔ STATE 3: A HASH WE CANNOT NAME. Preserve BOTH versions, LEAVE THE MARKER UP, exit non-zero.

    ⚠ `os._exit`, deliberately: this runs from the loop, the `finally`, a signal handler AND `atexit`, and only
    `os._exit` ends the process identically from all four — a `sys.exit` inside an `atexit` handler would be swallowed
    and the run would report success. It also skips the remaining `atexit` handlers, which is exactly right: the one
    thing that must NOT happen now is a second write attempt.
    ⚠ AND THE MARKER IS DELIBERATELY NOT REMOVED: it is the only record of which hashes we could name, so the next run
    can still tell our mutant from this edit — and `disarm()` never runs because this call never returns.
    """
    m = _marker()
    foreign = os.path.join(_BK_DIR, f"FOREIGN-{cur[:12]}-{os.path.basename(H)}")
    try:
        shutil.copyfile(H, foreign)
    except OSError as e:                                    # a copy we could not take must not hide the refusal
        foreign = f"(could not be copied: {e})"
    print(f"  ⛔ REFUSED to write {H} ({context}):", flush=True)
    print("    the file is NEITHER our recorded original NOR the mutation we installed, so it is somebody else's",
          flush=True)
    print("    edit and this tool will not choose for you. NOTHING WAS OVERWRITTEN.", flush=True)
    print(f"    in the tree (kept as-is) : {H}   md5 {cur}", flush=True)
    print(f"    a copy of it             : {foreign}", flush=True)
    # ⚠ The marker is gone on the NORMAL-COMPLETION path (`disarm()` removed it), so fall back to hashing the sidecar
    #   backup rather than printing "(none recorded)" for a file that is sitting right there. MEASURED: the first run
    #   of the live-window proof reached here from `atexit` after a clean finish and printed exactly that, which is a
    #   report defect in the one message a human has to act on.
    orig_md5 = m.get("orig_md5") or (_md5(_BK_FILE) if os.path.exists(_BK_FILE) else "(none recorded)")
    print(f"    our recorded ORIGINAL    : {_BK_FILE}   md5 {orig_md5}", flush=True)
    print(f"    the mutation we installed: {m.get('mutant_md5') or '(none — the marker was already cleared)'}  "
          f"label {m.get('label', '(none)')}", flush=True)
    print(f"  Resolve by hand: put the original back from the backup above if the edit was ours, then delete the "
          f"marker {_BK_MARK}", flush=True)
    sys.stdout.flush()
    os._exit(5)


def guarded_write(new_text, context, allow):
    """THE ONLY function in this tool that writes `H`. Returns 'written' or 'nochange'; never returns on foreign.

    `allow` names the current states this caller may legitimately overwrite — install allows only 'original', a
    restore allows 'original' (a no-op) and 'mutant'. Anything else, INCLUDING a named state a caller did not allow,
    goes to `_preserve_and_exit`.
    """
    cur = _md5(H)
    state = classify_source(cur)
    if state not in allow:
        _preserve_and_exit(cur, f"{context}; current state = {state}, allowed = {'/'.join(allow)}")
    if cur == _md5_text(new_text):
        return "nochange"
    with open(H, "w") as f:
        f.write(new_text)
    return "written"


def clear_mutant():
    if os.path.exists(_BK_MARK):
        _write_mark(orig_md5=_md5(_BK_FILE))


def arm_backup():
    """Take the sidecar backup and raise the in-flight marker. Recovers a previous crashed run first.

    ★★★ RECOVERY IS A THREE-STATE DECISION, NOT A TWO-WAY TEST, AND THE FIRST VERSION GOT THAT WRONG:
        `if md5(backup) != md5(source): restore` knows only SAME / DIFFERS, so it treated "differs" as "is the mutant"
        and would SILENTLY OVERWRITE A LEGITIMATE EDIT somebody made after a crashed run — live in this repository,
        which has had concurrent agents editing `src/`. ⇒ the marker now records the ORIGINAL and (while one is
        installed) the EXPECTED MUTANT hash, and only the two states it can NAME are acted on:
          the known ORIGINAL -> nothing to do;
          the known MUTANT   -> restore the original;
          ⛔ ANY THIRD HASH  -> REFUSE, preserve BOTH versions, exit non-zero. A tool that cannot tell whose edit it is
                                 must not choose for the human.
    ⓘ THE PATTERN, named because this is its fourth instance in one session: A BINARY TEST OVER A TERNARY DOMAIN —
      `mobile_op_of_tag` switching on an integer where the enum had five states, the ledger's before/after with no
      ordinal for a same-millisecond tie, `airtime_for`'s observed/unobserved with no CONFLICTING, and this one's
      same/differs with no FOREIGN. ★ When a check has two branches, ask what the third state is.
    """
    os.makedirs(_BK_DIR, exist_ok=True)
    if os.path.exists(_BK_MARK) and os.path.exists(_BK_FILE):
        try:
            mark = json.load(open(_BK_MARK))
        except (ValueError, OSError):
            mark = {}
        # ⛔ ONLY into the tree the marker names. The keyed directory already makes a cross-tree collision impossible;
        #    this is the second guard, because the failure it prevents is overwriting somebody else's source.
        owner = mark.get("path", "")
        if owner and owner != str(Path(H).resolve()):
            print(f"  REFUSED to recover: the marker belongs to {owner}, not {Path(H).resolve()}")
            sys.exit(4)
        # ★ THE SAME PRIMITIVE AS EVERY OTHER WRITE (see `guarded_write`): recovery is not a special case, it is the
        #   same invariant one process earlier. `nochange` IS state 1 (the known ORIGINAL, nothing left installed);
        #   `written` is state 2 (the known MUTANT, put back); state 3 never returns.
        if guarded_write(open(_BK_FILE).read(), "recovering a crashed earlier run",
                         allow=("original", "mutant")) == "written":
            print(f"  RECOVERED the mutation an earlier run left installed ({mark.get('label', '?')}) -> {H} restored")
    shutil.copyfile(H, _BK_FILE)
    _write_mark(orig_md5=_md5(_BK_FILE))
    global _armed
    _armed = True


def restore(reason=""):
    """The `finally` / signal / `atexit` restore. ⛔ NO LOGIC OF ITS OWN — it is a third caller of `guarded_write`.

    ⚠ It used to compare the backup with the file and copy on "differs", which is the SAME two-state test that made
    defect #4 possible, surviving in the paths #4's fix did not visit. A foreign edit reaching here now preserves both
    and exits non-zero exactly as it does everywhere else.
    """
    # ⛔ See `_armed`: a process that never armed the backup has nothing of its own to put back, and the file it would
    #    "restore" may be another run's LIVE MUTANT.
    if not _armed or not os.path.exists(_BK_FILE):
        return
    if guarded_write(open(_BK_FILE).read(), f"restore ({reason or 'no reason given'})",
                     allow=("original", "mutant")) == "written" and reason:
        print(f"  RESTORED {H} ({reason})", flush=True)


def disarm():
    if os.path.exists(_BK_MARK):
        os.remove(_BK_MARK)


def _sig(signum, _frame):
    restore(f"signal {signum}")
    disarm()
    os._exit(128 + signum)


for _s in (signal.SIGTERM, getattr(signal, "SIGHUP", signal.SIGTERM)):
    signal.signal(_s, _sig)
atexit.register(lambda: restore("atexit"))

MUTS_MODEL = [
 # --- identity -----------------------------------------------------------------------------------------------------
 ("M01 sync matches `seq` ONLY (the kind clause dropped)",
  "if (s.inbox[i].kind != _inbox_sel_kind || s.inbox[i].seq != _inbox_sel_seq) continue;",
  "if (s.inbox[i].seq != _inbox_sel_seq) continue;"),
 ("M02 the highlight does NOT follow a moved record (sync is inert)",
  "        if (_st.cursor != i) { _st.cursor = i; _st.dirty = true; }     // the record MOVED -> the highlight follows\n            return;",
  "        return;"),
 ("M03 a vanished pick is KEPT rather than dropped (the tempting clamp)",
  "_inbox_sel_valid = false; _st.inbox_pick_gone = true; _st.dirty = true;\n    }\n    // The WRITE side",
  "_st.dirty = true;\n    }\n    // The WRITE side"),
 ("M04 the GESTURE path stops re-anchoring the cursor (the highlight goes stale)",
  "sync_inbox_cursor(s);            // \u2605 \u00a7UI-7D: the same re-anchoring for the INBOX row, by `(kind, seq)`",
  ";"),
 ("M05 activation CLAMPS instead of refusing a lost selection",
  "if (!_inbox_sel_valid) {\n                if (s.inbox_shown > 0) _st.inbox_pick_gone = true;",
  "if (false) {\n                if (s.inbox_shown > 0) _st.inbox_pick_gone = true;"),
 # ⚠ M06's ANCHOR MOVED 2026-08-21 (the §UI-17 hoist put the INBOX-only identity guard inside the `record` arm);
 #   the property is unchanged — an UNIDENTIFIABLE row must not be selected.
 ("M06 a row with seq 0 is selected anyway",
  "                if (s.inbox[_st.cursor].seq != 0) {",
  "                if (true) {"),
 ("M07 the erase target assumes the DM store ([[B133]]'s exact shape)",
  "_inbox_req = { InboxWhat::erase, _st.detail_kind, _st.detail_seq };",
  "_inbox_req = { InboxWhat::erase, InboxKind::dm, _st.detail_seq };"),
 ("M08 the erase target is the NEIGHBOUR (an off-by-one row)",
  "_inbox_req = { InboxWhat::erase, _st.detail_kind, _st.detail_seq };",
  "_inbox_req = { InboxWhat::erase, _inbox_nb_kind, _inbox_nb_seq };"),
 ("M09 the answer's identity is not checked at all",
  "return _inbox_taken && _inbox_req.what == w && _inbox_req.kind == k && _inbox_req.seq == seq;",
  "(void)k; (void)seq; return _inbox_taken && _inbox_req.what == w;"),
 ("M10 an UNSOLICITED answer is accepted (the `_inbox_taken` guard dropped)",
  "return _inbox_taken && _inbox_req.what == w &&",
  "return _inbox_req.what == w &&"),
 ("M11 the budget's ring is chosen from the WRONG kind",
  "const bool dm = (r.kind == InboxKind::dm);",
  "const bool dm = (r.kind == InboxKind::channel);"),
 # --- body copy / paging -------------------------------------------------------------------------------------------
 ("M12 the body length is found by scanning for NUL (i.e. strlen)",
  "uint8_t n = body ? body_len : 0;                      // no body at all is a legitimate record, not an error",
  "uint8_t n = 0; if (body) { while (n < MESHROUTE_NS::protocol::inbox_max_body && body[n]) ++n; }"),
 ("M13 the sanitizer passes raw bytes through",
  "inline char ui_display_byte(uint8_t b) { return (b >= 0x20 && b < 0x7f) ? char(b) : '.'; }",
  "inline char ui_display_byte(uint8_t b) { return char(b); }"),
 ("M14 pages FLOORS instead of ceiling",
  "const uint8_t p = uint8_t((n + kDetailPageChars - 1) / kDetailPageChars);",
  "const uint8_t p = uint8_t(n / kDetailPageChars);"),
 ("M15 an empty body yields ZERO pages",
  "_st.detail_pages = p ? p : uint8_t(1);", "_st.detail_pages = p;"),
 ("M16 the page advance STOPS at the last page instead of cycling",
  "_st.detail_page = uint8_t((_st.detail_page + 1) % _st.detail_pages);",
  "if (_st.detail_page + 1 < _st.detail_pages) _st.detail_page = uint8_t(_st.detail_page + 1);"),
 ("M17 a page turn also refreshes the inactivity deadline",
  "_detail_page_at_ms = s.now_ms;", "_detail_page_at_ms = s.now_ms; _last_input_ms = s.now_ms;"),
 ("M18 both body rows render the SAME 19 columns",
  "const uint16_t i = uint16_t(off + uint16_t(row) * kDetailCols + n);",
  "const uint16_t i = uint16_t(off + n);"),
 # ⛔⛔ M19 IS **WITHDRAWN IN PLACE** 2026-08-21 (§UI-17 S2, §9 R-1), and it is left standing as a comment rather than
 #     deleted because the reason is the record. It read:
 #       ("M19 the modal has no inactivity timeout",
 #        "if (_st.detail != InboxModal::closed && elapsed(s.now_ms, _last_input_ms) >= kBlankMs) close_detail();",
 #        ";"),
 #     ⇒ ITS PROPERTY IS NOW THE OPPOSITE OF THE RULING. §3.3 (owner-ruled 2026-08-20) makes blanking a POWER action
 #     that preserves the interaction, so "the modal has an inactivity timeout" is the DEFECT rather than the
 #     property, and the line it anchored on no longer exists — this entry would have been VACUOUS at match count 0.
 #     ★ THE PROPERTY DID NOT GO AWAY, IT INVERTED: **S02** below re-instates exactly this statement and REQUIRES the
 #     suite to redden. ⓘ The withdrawn source is quoted in `on_tick` too, deliberately wrapped mid-condition so no
 #     pattern here can anchor on the COMMENT and measure nothing.
 # ("M19 the modal has no inactivity timeout", ...)
 # --- gestures + the three outcomes --------------------------------------------------------------------------------
 ("M20 a freshly opened modal selects `delete`",
  "_st.detail_action = InboxAction::back;                // spec §3.5: deletion costs short -> double, always",
  "_st.detail_action = InboxAction::del;"),
 ("M21 short press does not toggle the action",
  "_st.detail_action = (_st.detail_action == InboxAction::back) ? InboxAction::del : InboxAction::back;",
  ";"),
 ("M22 `io_error` CLOSES the modal (a disappearance without durable success)",
  "                _st.detail_del_failed = true;\n                _st.detail_action = InboxAction::back;",
  "                close_detail();\n                _st.detail_action = InboxAction::back;"),
 ("M23 `io_error` leaves the selection on `delete` (a retry one press away)",
  "                _st.detail_del_failed = true;\n                _st.detail_action = InboxAction::back;",
  "                _st.detail_del_failed = true;"),
 ("M24 `not_found` is treated as a success and closes the modal",
  "_st.detail = InboxModal::gone;        // TERMINAL, and it has no Delete action at all",
  "close_detail();"),
 ("M25 the terminal MESSAGE GONE state still accepts an action press",
  "        if (_st.detail == InboxModal::gone) {\n            if (g == Gesture::short_press || g == Gesture::double_press) close_detail();\n            return;\n        }",
  "        if (false) { return; }"),
 ("M26 a successful delete keeps the DELETED record as the selection",
  "                _inbox_sel_kind  = _inbox_nb_kind; _inbox_sel_seq = _inbox_nb_seq;\n                _inbox_sel_valid = _inbox_nb_valid;",
  "                ;"),
 # --- emergency interplay + the frame gate -------------------------------------------------------------------------
 # ⚠ RE-ANCHORED 2026-08-13 (§UI-14): both used to carry `\n    if (g == Gesture::long_arm)` as part of the pattern,
 #   and §UI-14 inserted the SETTINGS editor's close between the two lines — so both silently fell to match count 0,
 #   i.e. VACUOUS. ★ That is the runner's own guard doing its job, and the lesson is the one this file already carries:
 #   a pattern that spans two statements is a pattern the next slice can break. Anchor on the ONE line that is the
 #   behaviour.
 ("M27 the modal is closed at `long_fire` (compose's rule, copied)",
  "if (_st.detail != InboxModal::closed) close_detail();",
  "if (g == Gesture::long_fire && _st.detail != InboxModal::closed) close_detail();"),
 ("M28 the modal is not closed by a long press at all",
  "if (_st.detail != InboxModal::closed) close_detail();",
  ";"),
 ("M29 a completed DETAIL frame clears the unread counters",
  "st.detail == InboxModal::closed && st.screen == Screen::inbox);",
  "st.screen == Screen::inbox);"),
 # --- the header's bytes -------------------------------------------------------------------------------------------
 ("M30 the DELETE FAILED header is dropped (the failure becomes invisible)",
  'if (del_failed) { snprintf(out, cap, "DELETE FAILED %u/%u", unsigned(page) + 1u, unsigned(pages)); return; }',
  "(void)del_failed;"),
 ("M31 the channel header omits the channel number",
  'else                       snprintf(from, sizeof from, "CH%u from %u", unsigned(channel_id), unsigned(origin));',
  'else                       snprintf(from, sizeof from, "CH from %u", unsigned(origin));'),
 ("M32 the page indicator is 0-based on the panel",
  'snprintf(out, cap, "%-14s %u/%u", from, unsigned(page) + 1u, unsigned(pages));',
  'snprintf(out, cap, "%-14s %u/%u", from, unsigned(page), unsigned(pages));'),
 # --- §UI-14: the SETTINGS screen, the marker, and the save/discard/reboot states ------------------------------------
 # ★ The aim is the same as the §UI-13 block's: the state COLLAPSES. `short`'s two modes with no state to separate
 #   them (M35), the marker read from the wrong one of three predicates (M44/M46), and a fact claimed before the act
 #   that establishes it (M47).
 ("M33 SETTINGS is unreachable on a non-team build (the cycle gate copied from TEAM)",
  "if (s.team_build || cand == Screen::status || cand == Screen::inbox || cand == Screen::settings) return cand;",
  "if (s.team_build || cand == Screen::status || cand == Screen::inbox) return cand;"),
 # ⚠ M34's ANCHOR MOVED 2026-08-20 (§UI-17 S1 gave all three list screens ONE `entered` predicate inside `list_len`);
 #   the property it mutates is unchanged — the SETTINGS MENU losing its list-awareness — and the mutation still
 #   drops that arm whole, so the screen falls through to the one-row answer. ⓘ It shares M98's anchor line and
 #   REPLACES it differently: M98 keeps the menu's length and loses the CLOSED view, this one loses both.
 ("M34 the SETTINGS screen is not list-aware (one press leaves it)",
  "if (_st.screen == Screen::settings) return entered ? settings_row_list(s).n : uint8_t(1);",
  ";"),
 ("M35 `short` WALKS THE ROWS while editing (the double-duty trap)",
  "if (_st.settings == Settings::editing) { settings_edit_gesture(g, s); return; }",
  ";"),
 ("M36 the editor closes at `long_fire` instead of `long_arm` (compose's rule, copied)",
  "if (_st.settings == Settings::editing) { _st.settings = Settings::browsing; _st.dirty = true; }\n    if (g == Gesture::long_arm)",
  "if (g == Gesture::long_fire && _st.settings == Settings::editing) { _st.settings = Settings::browsing; }\n    if (g == Gesture::long_arm)"),
 ("M37 a long press does not leave the editor at all",
  "if (_st.settings == Settings::editing) { _st.settings = Settings::browsing; _st.dirty = true; }\n    if (g == Gesture::long_arm)",
  ";\n    if (g == Gesture::long_arm)"),
 ("M38 `BACK` DISCARDS the draft (the forbidden silent discard, through the door)",
  "if (_cfg) _cfg->on_back();",
  "if (_cfg) _cfg->discard();"),
 ("M39 blanking DISCARDS the draft (the forbidden discard, through the timer)",
  "if (_cfg) _cfg->on_blank();",
  "if (_cfg) _cfg->discard();"),
 ("M40 the RELOAD row stands permanently, not only while it applies",
  "if (conflict) l.row[l.n++] = CfgRow::reload;",
  "(void)conflict; l.row[l.n++] = CfgRow::reload;"),
 ("M41 the BLE row is rendered unconditionally (the transport condition ignored)",
  "if (ble_row) l.row[l.n++] = CfgRow::ble_mode;",
  "(void)ble_row; l.row[l.n++] = CfgRow::ble_mode;"),
 ("M42 an out-of-range row index CLAMPS to row 0 instead of failing closed",
  "bool at(uint8_t i, CfgRow& out) const { if (i >= n) return false; out = row[i]; return true; }",
  "bool at(uint8_t i, CfgRow& out) const { out = row[i >= n ? 0 : i]; return true; }"),
 ("M43 the cursor does NOT follow its row when the list changes under it",
  "            if (_st.cursor != i) { _st.cursor = i; _st.dirty = true; }   // the row MOVED -> the highlight follows\n            return;",
  "            return;"),
 ("M44 a REFUSED save reads as SAVED (the success that isn't)",
  "        case mrfw::CfgSave::invalid:      return \"BAD VALUE\";",
  "        case mrfw::CfgSave::invalid:      return \"SAVED\";"),
 ("M45 the SAVE outcome is recorded BEFORE the service returns it",
  "if (_cfg) { _st.cfg_save = _cfg->save(); _st.cfg_have_save = true; }",
  "if (_cfg) { _st.cfg_save = mrfw::CfgSave::saved; _st.cfg_have_save = true; (void)_cfg->save(); }"),
 ("M46 the unsaved marker is dropped from §3.3's row",
  'return unsaved ? "CFG* UNSAVED" : "";',
  '(void)unsaved; return "";'),
 ("M47 the conflict headline is re-spelled here instead of called from the service",
  "if (conflict) return mrfw::cfg_save_panel(mrfw::CfgSave::conflict);   // \"CFG! RELOAD\" — the SERVICE's string",
  'if (conflict) return "CFG RELOAD!";'),
 ("M48 the transient note is never retired (a stale outcome under a fresh act)",
  "note_settings_cursor(s);\n                                          clear_settings_note(); _st.dirty = true; }",
  "note_settings_cursor(s);\n                                          _st.dirty = true; }"),
 ("M49 the editor SETS instead of cycling (one press can never turn a value off)",
  "(void)_cfg->set(f, cfg_menu_next(f, _cfg->draft().at(f)));",
  "(void)_cfg->set(f, uint8_t(1));"),
 # ⛔⛔ M50 IS **WITHDRAWN IN PLACE** 2026-08-20 (QG's [[B232]] correction), and it is left standing as a comment
 #     rather than deleted because the reason is the record. It read:
 #       ("M50 a value row can be edited while the service is not open",
 #        "if (_cfg && _cfg->is_open()) { _st.settings = Settings::editing; _st.dirty = true; }",
 #        "{ _st.settings = Settings::editing; _st.dirty = true; }"),
 #     ⇒ THE STATE IT MUTATED IS NO LONGER REACHABLE. QG ruled that a `double` on the closed SETTINGS view must
 #     LEAVE IT CLOSED when the service is not open (the renderer draws `CFG UNAVAILABLE` and nothing else), and
 #     `ConfigService::_open` is never cleared once set — so no gesture sequence reaches `settings_activate` with a
 #     shut service, the mutant is behaviourally identical to the clean tree, and this entry would be reported
 #     UNUSABLE. ★ THE PROPERTY DID NOT GO AWAY, IT MOVED OUT: **M105** measures it where it is now decided, and the
 #     inner gate survives as defence in depth with a note in the source saying exactly that ([[B223]]'s lesson:
 #     an unreachable decision gets a statement, never a mutation that cannot fail).
 # --- §CHROME-1: the one snapshot field whose TYPE is the correctness argument ----------------------------------------
 # ⚠ THE FORMATTER'S OWN 32-BIT MUTATION LIVES IN THE `chrome` BATTERY (X01). This entry is the OTHER half and it is
 #   not redundant: the age could be truncated in the CARRIER while the formatter's parameter stayed 64-bit, and a
 #   battery that only mutated the formatter would measure the easy half. `UiSnapshot`'s neighbours (`now_ms`,
 #   `last_dm_age_s`) are both `uint32_t`, so this is the narrowing somebody would make "for consistency".
 ("M51 the home confirmation age is carried as uint32_t (the ~49.7-day wrap, re-created in the snapshot)",
  "    uint64_t home_confirm_age_ms = 0;",
  "    uint32_t home_confirm_age_ms = 0;"),
 # --- §CHROME-4: the ONE edited number of the 19-column body migration, and the derivation that hangs off it -------
 # ⛔⛔ M52 IS THE MIGRATION DONE AT THE DRAW ORIGIN ONLY — design §7.3's named trap. The renderer draws 19 columns
 #    while the MODEL still wraps at 21, so every full detail row loses its last two characters to u8g2's clip AND
 #    `detail_pages` reports a count computed from 42 characters a page over rows that can show 38. ⓘ It also trips
 #    `src/firmware_ui.cpp`'s `static_assert(kBodyCols == mrui::kDetailCols)`, which is the point of that assert —
 #    but the native suite must redden on its own, because nothing native compiles that file.
 ("M52 the inbox detail still wraps at 21 columns (the migration done at the draw origin only)",
  "inline constexpr uint8_t  kDetailCols      = 19;",
  "inline constexpr uint8_t  kDetailCols      = 21;"),
 # ⛔ M53 THE PAGE CAPACITY RE-CLAMPED INSTEAD OF RE-DERIVED — the brief's own wording. The wrap moves to 19 and the
 #    page still claims 42 characters, so the LAST page of a long body is short and the modal's `n/N` lies.
 ("M53 the page capacity is re-clamped to its old 42 instead of derived from kDetailCols",
  "inline constexpr uint8_t  kDetailPageChars = uint8_t(kDetailCols * kDetailBodyRows);",
  "inline constexpr uint8_t  kDetailPageChars = 42;"),
 # --- §UI-15 slice 4: the provisioning state model, the §4 gate and §6's hiding ---------------------------------------
 # ★ THE AIM IS THE ONE PLAN §4 NAMES: **THE STATE COLLAPSE**. `conflict()` and `config_unsaved()` are different
 #   comparisons with different escapes, and v1 of the plan conflated them — so M55/M56/M57/M58 are the four ways that
 #   conflation can be re-introduced (order, either cell dropped, remedies swapped), each on its own. M59 is C2's
 #   COUNTED discriminator (a gate that saves for the operator), M60/M61 are §6's *"do NOT hide static join merely
 #   because MR_FEAT_TEAM is off"* in both directions, and M54/M62..M67 are the sub-view's own invariants.
 # ⛔ EVERY ENTRY HERE AIMS AT BEHAVIOUR THIS SLICE CAN REACH ([[B222]]): the child FLOWS (`create_confirm`,
 #   `join_select` and everything behind them) are slices 5/6's, so no entry mutates a transition into them — there is
 #   none to mutate. The entries that guarded those flows in the held version were re-aimed (M54, M66) or are covered
 #   by the pending-entry pin (`ui15-pending`), and M67 was ADDED for the guard [[B223]] found unmeasured.
 # ⓘ M67 IS OUT OF NUMERIC ORDER DELIBERATELY, placed beside the sibling it pairs with (M54): the numbers of the
 #   entries that survived the correction stay exactly as QG read them, so a re-aim can never be mistaken for a
 #   renumber.
 # ⚠ [[B223]]'s CORRECTION, and it is why there are now TWO close entries rather than one: the close-on-leave reset
 #   used to be written inline in `settings_follow_screen`, where it is UNREACHABLE (the sub-view owns the press, so
 #   the screen cannot leave SETTINGS underneath it) — an unreachable line cannot redden a suite, so the required
 #   guard had NO instrument and M54 measured a DIFFERENT path. The decision is now the pure `provision_reset_on_leave`
 #   (driven over all eight arms by the suite), so ★ M67 mutates THE REQUIRED GUARD ITSELF and M54 stays as the
 #   REACHABLE-PATH control on the gesture half.
 ("M54 leaving the provisioning sub-view keeps the arm (a stale confirmation survives into the next visit)",
  "        _st.settings = Settings::browsing;\n"
  "        provision_reset_on_leave(_st.provisioning, _st.prov_confirm, _st.invite);",
  "        _st.settings = Settings::browsing;"),
 ("M67 the close-on-leave reset only closes the arm a GESTURE can reach (7 of the 8 survive leaving the screen)",
  "    arm = Provision::closed;",
  "    if (arm == Provision::menu) arm = Provision::closed;"),
 ("M55 the §4 gate tests UNSAVED first, so a CONFLICT is told to SAVE (plan §4's conflation, through the ORDER)",
  "                if (_cfg->conflict())               { _st.prov_block = ProvBlock::conflict; break; }\n"
  "                if (_cfg->config_unsaved())         { _st.prov_block = ProvBlock::unsaved;  break; }",
  "                if (_cfg->config_unsaved())         { _st.prov_block = ProvBlock::unsaved;  break; }\n"
  "                if (_cfg->conflict())               { _st.prov_block = ProvBlock::conflict; break; }"),
 ("M56 the UNSAVED cell is dropped — PROVISION opens over an unsaved draft (§3.6.3's precondition gone)",
  "                if (_cfg->config_unsaved())         { _st.prov_block = ProvBlock::unsaved;  break; }",
  "                ;"),
 ("M57 the CONFLICT cell is dropped — the two states collapse into one",
  "                if (_cfg->conflict())               { _st.prov_block = ProvBlock::conflict; break; }",
  "                ;"),
 ("M58 the two remedies are SWAPPED (a conflict is pointed at SAVE, which refuses)",
  '        case ProvBlock::conflict: return "RELOAD OR DISCARD";\n'
  '        case ProvBlock::unsaved:  return "SAVE OR DISCARD";',
  '        case ProvBlock::conflict: return "SAVE OR DISCARD";\n'
  '        case ProvBlock::unsaved:  return "RELOAD OR DISCARD";'),
 ("M59 the gate SAVES on the operator's behalf and then opens (the helpful write C2 forbids)",
  "                if (_cfg->config_unsaved())         { _st.prov_block = ProvBlock::unsaved;  break; }",
  "                if (_cfg->config_unsaved())         { (void)_cfg->save(); enter_provision(Provision::menu); break; }"),
 ("M60 static join is hidden because the TEAM plane is off (plan §6's named defect, verbatim)",
  "    if (join_static) l.row[l.n++] = ProvRow::join_static;",
  "    if (create_team && join_static) l.row[l.n++] = ProvRow::join_static;"),
 ("M61 an unsupported child is rendered anyway (the refusing stub [[B209]] forbids)",
  "    if (create_team) l.row[l.n++] = ProvRow::create_team;",
  "    (void)create_team; l.row[l.n++] = ProvRow::create_team;"),
 ("M62 a confirmation opens with CONFIRM selected (the destructive default §3.6.3 rules out)",
  "        _st.prov_confirm = ProvConfirm::back;\n"
  "        _st.cursor = 0;                        // each arm's list starts at its own first row",
  "        _st.prov_confirm = ProvConfirm::confirm;\n"
  "        _st.cursor = 0;                        // each arm's list starts at its own first row"),
 ("M63 the sub-view does NOT own the press (the SETTINGS menu walks underneath it)",
  "if (_st.settings == Settings::provisioning) { provision_gesture(g, s); return; }",
  ";"),
 ("M64 the SETTINGS cursor is re-anchored while the sub-view owns it (the highlight leaves the child list)",
  "        if (_st.settings == Settings::provisioning) return;\n"
  "        const CfgRowList l = settings_row_list(s);",
  "        const CfgRowList l = settings_row_list(s);"),
 ("M65 the child menu WALKS OFF instead of cycling (a sub-view left by the ordinary screen advance)",
  "            if (l.n) _st.cursor = uint8_t((_st.cursor + 1) % l.n);   // CYCLES — a sub-view is never walked out of",
  "            if (l.n && _st.cursor + 1u < l.n) _st.cursor = uint8_t(_st.cursor + 1);"),
 ("M66 the provisioning sub-view survives the alarm (§3.6.5 rule 1's pre-emption dropped)",
  "    if (_st.settings == Settings::provisioning) close_provisioning();",
  "    ;"),
 # --- §UI-15 slice 5: the CREATE flow, its landing, and the OWNER's parent-row ruling -------------------------------
 # ★ THE AIM IS THE TWO WAYS A CONFIRMATION LIES: it acts on the SAFE choice (M68/M69), or it CLAIMS an outcome the
 #   act has not returned yet (M70/M71). Both are the "a success that isn't" class this project has recorded once.
 ("M68 ★★ the confirmation fires the transaction on BACK too (the safe action performs the destructive one)",
  "        if (_st.prov_confirm == ProvConfirm::back) { enter_provision(Provision::menu); return; }\n"
  "        run_create_team();",
  "        run_create_team();"),
 # ⚠ M69 RETARGETED 2026-08-20 (§UI-15 slice 6), and the retarget is recorded rather than the entry quietly
 #   rewritten: the four-line toggle it used to match was HOISTED into `prov_confirm_toggle()` because the JOIN
 #   confirmation is the same pair with a different landing (U1 — two copies is how one of them stops marking dirty).
 #   ⇒ a sed for the old lines would now match NOTHING and be reported VACUOUS. ★ The SEMANTIC is unchanged: `short`
 #   in the CREATE confirmation acts instead of toggling, so one press reaches CREATE.
 ("M69 `short` in the confirmation ACTS instead of toggling (one press reaches CREATE)",
  "        if (g == Gesture::short_press) { prov_confirm_toggle(); return; }\n"
  "        if (_st.prov_confirm == ProvConfirm::back) { enter_provision(Provision::menu); return; }",
  "        if (g == Gesture::short_press) { run_create_team(); return; }\n"
  "        if (_st.prov_confirm == ProvConfirm::back) { enter_provision(Provision::menu); return; }"),
 # ⛔⛔ M70 IS §8 PIN 2 INVERTED, AND IT IS THE TEMPTING SHAPE: move the screen first "so the panel is already showing
 #    the right state when the answer lands". The act then runs UNDER the result screen — which is precisely the
 #    "claims success before the save returns" the design forbids. The fake records WHERE the model was when it ran,
 #    which is why this is measurable at all.
 ("M70 ★★ the RESULT screen is entered BEFORE the transaction runs (§8 pin 2, inverted)",
  "        UiProvAnswer a{};\n        if (_prov) {",
  "        UiProvAnswer a{};\n        enter_provision(Provision::create_result);\n        if (_prov) {"),
 ("M71 the answer is never retired, so a stale verdict survives into the next screen",
  "        _st.prov_answer = UiProvAnswer{};",
  "        // (the answer is not retired)"),
 ("M72 the result arm RE-RUNS the transaction on the press that leaves it",
  "            case Provision::create_result:  enter_provision(Provision::menu); return;",
  "            case Provision::create_result:  run_create_team(); return;"),
 ("M73 a NULL seam does NOTHING instead of refusing out loud (a dead-button create, C2)",
  "            a.outcome = UiProvOutcome::refused;\n"
  "            a.reason  = \"no service\";",
  "            return;"),
 # ⓘ AIMED AT ONE LINE, deliberately: the two remedies are not adjacent in the source (the `save_failed` arm carries
 #   the transaction's guarantee in a comment between them), so a two-line swap matches nothing — which the runner
 #   reports as VACUOUS rather than passing. This is the same defect from the other end: the OWNER'S RULED REMEDY is
 #   replaced by the other outcome's line, so the operator is never sent to the serial console.
 ("M74 the PHY refusal loses its ruled remedy (it borrows the save failure's line instead)",
  '        case UiProvOutcome::phy_differs: return "USE SERIAL";',
  '        case UiProvOutcome::phy_differs: return "NOTHING CHANGED";'),
 ("M75 the PHY refusal renders as a SUCCESS headline (the two outcomes collapse)",
  '        case UiProvOutcome::phy_differs: return "PHY DIFFERS";',
  '        case UiProvOutcome::phy_differs: return "TEAM CREATED";'),
 # ⛔ M76 IS THE OWNER'S 2026-08-19 RULING, INVERTED: the parent row kept unconditional, i.e. slice 4's shape. A build
 #   with no child then offers a menu whose only entry is BACK — the state the ruling forbids.
 ("M76 the PROVISION row is unconditional again (the parent-row ruling not applied)",
  "    if (provision) l.row[l.n++] = CfgRow::provision;",
  "    l.row[l.n++] = CfgRow::provision;"),
 ("M77 the parent's predicate counts the UNCONDITIONAL back row, so every build shows PROVISION",
  "        if (l.row[i] != ProvRow::back) return true;",
  "        return true;"),
 # --- §UI-15 slice 6: the STATIC-JOIN flow, its landings and the ASYNCHRONOUS outcome ------------------------------
 # ★ THE AIM IS THE FOUR WAYS THIS FLOW LIES: it acts on the SAFE choice (M77/M78), it claims an outcome the act has
 #   not returned (M79/M83), it CANCELS an already-persisted operation the plan says it may not (M81/M82), or it
 #   turns the 60 s word change into the FAILURE plan §2.3 rule 5 forbids (M80).
 ("M78 the JOIN entry never reads the profile store (the list opens on whatever the last visit left)",
  "            case ProvRow::join_static: load_join_profiles(); enter_provision(Provision::join_select); return;",
  "            case ProvRow::join_static: enter_provision(Provision::join_select); return;"),
 ("M79 ★★ the join confirmation fires the transaction on BACK too (the safe action performs the join)",
  "        if (_st.prov_confirm == ProvConfirm::back) { enter_provision(Provision::join_select); return; }\n"
  "        run_join_static(s);",
  "        run_join_static(s);"),
 ("M80 `short` in the join confirmation ACTS instead of toggling (one press starts a join)",
  "        if (g == Gesture::short_press) { prov_confirm_toggle(); return; }\n"
  "        if (_st.prov_confirm == ProvConfirm::back) { enter_provision(Provision::join_select); return; }",
  "        if (g == Gesture::short_press) { run_join_static(s); return; }\n"
  "        if (_st.prov_confirm == ProvConfirm::back) { enter_provision(Provision::join_select); return; }"),
 ("M81 ★★ the RESULT screen is entered BEFORE the join transaction runs (§8 pin 2, inverted, join half)",
  "        UiProvAnswer a{};\n        const uint8_t sel = _st.join_sel;",
  "        UiProvAnswer a{};\n        enter_provision(Provision::join_result);\n        const uint8_t sel = _st.join_sel;"),
 ("M82 ⛔⛔ 60 s becomes a TERMINAL FAILURE (plan §2.3 rule 5's forbidden deadline, and retries are unbounded)",
  "            if (still != _st.join_still) { _st.join_still = still; _st.dirty = true; }",
  "            if (still != _st.join_still) { _st.join_still = still; _st.dirty = true;\n"
  "                if (still) { _join.active = false; enter_provision(Provision::join_result);\n"
  "                    UiProvAnswer f{}; f.outcome = UiProvOutcome::join_refused; f.reason = \"timeout\";\n"
  "                    _st.prov_answer = f; } }"),
 ("M83 ⛔ BACK from the waiting screen CANCELS the persisted join (plan §2.3 rule 4's forbidden rollback)",
  "            case Provision::join_waiting:   enter_provision(Provision::menu); return;",
  "            case Provision::join_waiting:   _join.active = false; enter_provision(Provision::menu); return;"),
 ("M84 ⛔ the ALARM cancels the persisted join (§3.6.5 pre-empts the SCREEN, never the operation)",
  "    if (_st.settings == Settings::provisioning) close_provisioning();",
  "    if (_st.settings == Settings::provisioning) { _join.active = false; close_provisioning(); }"),
 ("M85 ⛔⛔ ANY `join_refused` push FAILS the screen (plan §2.3 rule 6: no reason terminally fails v1)",
  "        if (!join_push_correlates(_join, pu.kind, pu.layer_id, pu.dst, persisted_layer0_id, canonical_node_id))\n"
  "            return;",
  "        if (pu.kind == MESHROUTE_NS::PushKind::join_refused && _st.provisioning == Provision::join_waiting) {\n"
  "            _join.active = false; enter_provision(Provision::join_result);\n"
  "            UiProvAnswer f{}; f.outcome = UiProvOutcome::join_refused; f.reason = \"refused\";\n"
  "            _st.prov_answer = f; return; }\n"
  "        if (!join_push_correlates(_join, pu.kind, pu.layer_id, pu.dst, persisted_layer0_id, canonical_node_id))\n"
  "            return;"),
 ("M86 the session is armed on EVERY verdict, so a refusal leaves a correlation window open for a BOOT DAD",
  "        if (a.outcome == UiProvOutcome::joining) {",
  "        _join.active = true; _join.requested_layer = requested_layer;\n"
  "        if (a.outcome == UiProvOutcome::joining) {"),
 ("M87 a correlated adopt NAVIGATES the panel from whatever screen is up (a push that moves the operator)",
  "        if (_st.provisioning == Provision::join_waiting) {\n"
  "            enter_provision(Provision::join_result);",
  "        if (true) {\n"
  "            enter_provision(Provision::join_result);"),
 ("M88 the adopted answer carries no node id (plan §2.3 rule 2's `resulting node id` never shows)",
  "            a.node_id = pu.dst;                     // ★ the ADOPTED id, read off the push the rule accepted",
  "            a.node_id = 0;"),
 ("M89 the SELECT list activates the row INDEX as a slot number (§B66 — slot 3 is joined as slot 2)",
  "        _st.join_sel = r.slot1;", "        _st.join_sel = uint8_t(_st.cursor + 1);"),
 # ⛔ AN ENTRY WAS **NOT** ADDED FOR `run_join_static`'s EMPTY-PICK FLOOR, and the absence is stated rather than
 #   left to be noticed: `join_confirm` is entered ONLY by `join_select_gesture`, which has just read the slot off a
 #   row built from the `present` flags — so a pick that names no present slot is UNREACHABLE BY CONSTRUCTION and a
 #   mutation dropping the clause would stay GREEN. It is the `no_change`-arm precedent from slice 5, one screen over:
 #   written for C2, marked here, ⛔ and not counted as covered.
 ("M90 the join intent carries no profile, so the adapter is handed an empty slot",
  "            in.join = _st.join_list.rec.prof[sel - 1];   // ★ WHAT WAS SHOWN IS WHAT IS JOINED (U2 — the whole record)",
  "            in.join = mrnv::JoinProfile{};"),
 ("M91 the cached requested layer is the NIBBLE, so term 2 can never hold above layer 15 (trap 2, in the model)",
  "            requested_layer = in.join.layer;             // the FULL byte, cached for the correlation's term 2/3",
  "            requested_layer = uint8_t(in.join.layer & 0x0F);"),
 # --- 2026-08-20: the two INBOX-LIST fixes ---------------------------------------------------------------------------
 # ★ ONE ENTRY EACH, ON THE ONE LINE THAT IS THE BEHAVIOUR (the M27/M28 lesson): a pattern spanning two statements is a
 #   pattern the next slice breaks into a VACUOUS count-0 match.
 ("M92 [[B231]] the DM block is published OLDEST-FIRST again (the newest message back at the BOTTOM)",
  "        for (uint8_t i = _n_dm; i > 0 && k < kMaxInboxRows; --i) s.inbox[k++] = _dm[i - 1];",
  "        for (uint8_t i = 0; i < _n_dm && k < kMaxInboxRows; ++i) s.inbox[k++] = _dm[i];"),
 # ⓘ The channel block gets its OWN entry rather than sharing M92's: the two loops are independent statements, and a
 #   fix that reversed only the block it was looking at is exactly the half-applied shape worth being able to catch.
 ("M93 [[B231]] the CHANNEL block is published OLDEST-FIRST (only half the ruling applied)",
  "        for (uint8_t i = _n_ch; i > 0 && k < kMaxInboxRows; --i) s.inbox[k++] = _ch[i - 1];",
  "        for (uint8_t i = 0; i < _n_ch && k < kMaxInboxRows; ++i) s.inbox[k++] = _ch[i];"),
 ("M94 [[B233]] the serviced-mutation latch asks for NO repaint (the stale row stands for ever)",
  "        if (_inbox_rows_stale) { _inbox_rows_stale = false; _st.dirty = true; }",
  "        if (_inbox_rows_stale) { _inbox_rows_stale = false; }"),
 # ★ THE OPPOSITE DEFECT, and it is the one a "just leave it dirty" fix produces: a latch that is never consumed
 #   repaints at TICK RATE for ever — invisible to any case that only asked "did it repaint at all".
 ("M95 [[B233]] the latch is never consumed (a permanent repaint, not one more frame)",
  "        if (_inbox_rows_stale) { _inbox_rows_stale = false; _st.dirty = true; }",
  "        if (_inbox_rows_stale) { _st.dirty = true; }"),
 # ★ THE SCOPE OF THE LATCH, attacked from the other side: raising it for EVERY erase answer is the tempting
 #   simplification, and it claims a repaint is owed for a store that was never touched (`io_error` / `not_found`).
 ("M96 [[B233]] every erase ANSWER claims the rows are stale, including the ones that deleted nothing",
  "        if (!inbox_answer_is(InboxWhat::erase, kind, seq)) return;",
  "        if (!inbox_answer_is(InboxWhat::erase, kind, seq)) return;\n        _inbox_rows_stale = true;"),
 # --- [[B232]]: the SETTINGS SINGLE ENTRY (owner-ruled 2026-08-20) --------------------------------------------------
 # ★ THE AIM IS THE RULING'S FOUR MOVING PARTS, EACH ON ITS OWN: the LANDING (M97/M98 — the two independent ways the
 #   nine-press walk comes back), the ENTRY (M99), the SERVICE STILL OPENING ON ARRIVAL (M100 — the register names
 #   the defer-to-browsing shape as the tempting WRONG fix, so it gets a control rather than a comment), and the two
 #   EXITS (M101/M102 — either one leaving the SCREEN re-creates the "where am I" jump the ruling removes).
 # ⛔ M97 IS THE REVERSION, LITERALLY: it puts the auto-enter back where `sync_settings` used to hold it.
 ("M97 [[B232]] SETTINGS auto-enters the menu on arrival again (up to 9 presses to pass the screen)",
  "if (_st.settings == Settings::closed) return;",
  "if (_st.settings == Settings::closed) { _st.settings = Settings::browsing; _st.dirty = true; }"),
 # ⛔ M98 IS THE SAME SYMPTOM THROUGH THE OTHER MECHANISM, and that is why both exist: the landing can be right and
 #    the screen still cost a press per row if the CLOSED view reports the menu's length to `advance_or_next`.
 # ⚠ M98's ANCHOR MOVED 2026-08-20 (§UI-17 S1 gave all three list screens ONE `entered` predicate inside
 #   `list_len`); the property it mutates is unchanged — the CLOSED view reporting the MENU's length — and the
 #   mutation still touches the SETTINGS arm and nothing else.
 ("M98 [[B232]] the closed view reports the MENU's length, so `short` walks rows that are not on the panel",
  "if (_st.screen == Screen::settings) return entered ? settings_row_list(s).n : uint8_t(1);",
  "if (_st.screen == Screen::settings) return settings_row_list(s).n;"),
 # ⚠ M99's ANCHOR MOVED 2026-08-20 (QG's correction gave the branch a body); the ENTRY it mutates is unchanged.
 ("M99 [[B232]] the entry row cannot be entered (a screen with one row and no way in)",
  "            if (_st.settings == Settings::closed) {\n"
  "                if (_cfg && _cfg->is_open()) open_settings_menu();\n"
  "                return;\n"
  "            }",
  "            ;"),
 # ⛔⛔ M105 IS QG's BLOCKER, AS A CONTROL. `draw_settings_screen` prints `CFG UNAVAILABLE` and RETURNS while the
 #     service is not open, so a menu opened there is a cursor walking INVISIBLE rows — [[B232]]'s own multi-press
 #     defect re-created one double-press deep. ⓘ It replaces M50 as the reachable measurement of that property; see
 #     M50's withdrawal note below.
 ("M105 [[B232]] the menu opens over an UNAVAILABLE config (a walk over rows nothing draws)",
  "                if (_cfg && _cfg->is_open()) open_settings_menu();",
  "                open_settings_menu();"),
 # ⛔⛔ M100 IS THE REGISTER'S NAMED WRONG FIX: defer `open()` to the menu. The §3.6.1 BASELINE is then never taken
 #     for an operator who only cycled past SETTINGS, so `note_external_write` has nothing to compare against and the
 #     conflict latch — and the rail badge that reads it — stay silent on a companion write.
 ("M100 [[B232]] the ConfigService is opened only when the MENU is entered (the defer-to-browsing fix)",
  "            (void)_cfg->open();",
  "            if (_st.settings != Settings::closed) (void)_cfg->open();"),
 ("M101 [[B232]] the walk off the last row leaves the SCREEN again (the jump the ruling removes)",
  "if (_st.screen == Screen::settings && _st.settings == Settings::browsing) { close_settings_menu(); return; }",
  ";"),
 ("M102 [[B232]] the BACK row jumps to STATUS instead of leaving the MENU",
  "                close_settings_menu();\n                break;",
  "                _st.screen = Screen::status; _st.cursor = 0;\n                settings_follow_screen();\n                break;"),
 # ⓘ M103 is the CLOSED view's own cursor: the single entry row is index 0, and a close that left the menu's index
 #   behind would put the highlight on a row that view does not draw — and `note_settings_cursor` would then read it.
 ("M103 [[B232]] leaving the menu keeps the MENU's cursor, so the single-entry view holds a menu index",
  "        _st.settings = Settings::closed;   _st.cursor = 0; _cfg_sel_valid = false; _st.dirty = true;",
  "        _st.settings = Settings::closed;   _st.dirty = true;"),
 ("M104 [[B232]] the entry row has no label (an entry nobody can read — C2)",
  'inline constexpr const char* kSettingsEnterText = "ENTER SETTINGS";',
  'inline constexpr const char* kSettingsEnterText = "";'),
 # --- §UI-17 slice 1: TEAM/INBOX PASSIVE <-> INTERACTIVE (spec §1.2/§1.3) --------------------------------------------
 # ★ THE AIM IS THE SLICE'S SEVEN MOVING PARTS, EACH ON ITS OWN: the LANDING (M106/M107 — the two independent ways a
 #   preview turns back into a selector), the ROW RESOLVER (M108, §B66), the two CONTAINMENTS (M109 the `BACK` row and
 #   M110 the walk off the last row — either one leaving the SCREEN re-creates the "where am I" jump [[B232]] removed),
 #   the LEAVE RESET (M111, driven at the pure helper because the model path is unreachable — [[B223]]), and the PICK
 #   (M112/M113 — a passive screen that records one can announce a refusal for a choice nobody made), plus the shared
 #   predicate itself (M114).
 # ⛔ M106 IS THE REVERSION, LITERALLY: it puts the auto-enter back, on every arrival, exactly as M97 does one screen
 #    over — and the harm is [[B232]]'s in the plane where it is worse, because the marked row is a SEND TARGET.
 ("M106 [[UI-17]] TEAM/INBOX auto-enter on arrival again (a `>` beside a teammate nobody chose)",
  "        if (_st.screen == Screen::team || _st.screen == Screen::inbox) return;",
  "        if (_st.screen == Screen::team || _st.screen == Screen::inbox) { _st.list_view = ListView::interactive; return; }"),
 # ⛔ M107 IS THE SAME SYMPTOM THROUGH THE OTHER MECHANISM, and that is why both exist: the landing can be right and
 #    the screen still cost a press per teammate if the PASSIVE preview reports the roster's length.
 # ⚠ RE-POINTED 2026-08-21 onto the HOISTED decision (QG): it used to mutate the TEAM arm of `list_len` and left
 #   the INBOX arm — an independent copy of the same rule — unprotected. One site now, so it covers BOTH screens.
 ("M107 [[UI-17]] a passive list reports its rows, so `short` walks a preview nobody entered",
  "    return entered ? uint8_t(shown + 1) : uint8_t(1);",
  "    return shown;"),
 # ⛔⛔ M108 IS §B66 IN THIS SLICE's OWN SHAPE: position is not an identity. One off-by-one and the LAST TEAMMATE is
 #     the row that "leaves" — so the operator's `double` on the person they highlighted closes the list instead of
 #     addressing them, and the row after it is unreachable.
 ("M108 [[UI-17]] the BACK row is resolved one row early, so the LAST teammate becomes the exit",
  "    return (cursor >= shown) ? ListRow::back : ListRow::member;",
  "    return (cursor + 1 >= shown) ? ListRow::back : ListRow::member;"),
 # ⚠ RE-POINTED 2026-08-21 onto the HOISTED dispatch (QG): it used to mutate the TEAM arm and left the INBOX copy
 #   unprotected. One site now, so the "where am I" jump is measured on BOTH screens.
 ("M109 [[UI-17]] the BACK row leaves the SCREEN instead of closing the view (the jump the contract removes)",
  "                case ListAct::leave:  close_list_view(s); return;",
  "                case ListAct::leave:  _st.screen = next_screen(_st.screen, s); _st.cursor = 0;\n"
  "                                      list_follow_screen(); _st.dirty = true; return;"),
 ("M110 [[UI-17]] the walk off the last row leaves the screen again (the containment dropped)",
  "        if (screen_is_entered(_st.screen, _st.settings, _st.list_view)) { _st.cursor = 0; _st.dirty = true; return; }",
  "        ;"),
 # ⛔ M111 IS [[B223]]'s OWN CONTROL, and it is why the reset is a PURE FUNCTION: mutate the assignment where the
 #    decision actually lives. Written inline at its (currently unreachable) call site it would be unmutatable.
 ("M111 [[B223]] the leave reset never resets — an entered list outlives its screen",
  "    const bool changed = view != ListView::passive;\n    view = ListView::passive;\n    return changed;",
  "    const bool changed = view != ListView::passive;\n    return changed;"),
 # ⛔⛔ M112/M113 ARE THE PICK, AND THE HARM IS NOT THE MARKER: a pick recorded on a screen nobody entered lets
 #     `sync_*_cursor` ANNOUNCE ITS LOSS — `TEAMMATE GONE` / `MESSAGE GONE` on a preview, about a choice the operator
 #     never made.
 # ⚠ RE-POINTED 2026-08-21 onto the HOISTED write-side decision (QG): the two `note_*_cursor` functions asked the
 #   same three questions, so this now covers BOTH screens from one site.
 ("M112 [[UI-17]] the pick is recorded on a PASSIVE preview (a refusal for a choice nobody made)",
  "    if (!entered)   return ListNote::keep;",
  "    ;"),
 # ⛔⛔ M113 IS **WITHDRAWN IN PLACE** 2026-08-21, and it is left standing as a comment rather than deleted: it was
 #     the INBOX HALF of M112, and the two halves are ONE decision since the hoist. A second entry over the same
 #     line would report a coverage figure twice for one guard — the opposite of what this battery is for.
 #     ⓘ Its property is M112's; the INBOX-only half that SURVIVES the hoist (the `seq != 0` identity guard) is
 #     M06's, which was re-pointed in the same pass.
 # ("M113 [[UI-17]] the INBOX pick is recorded while the list is PASSIVE", ...)
 # ⓘ M114 attacks the SHARED predicate rather than either screen, which is the point of there being one: a single
 #   wrong answer there turns every passive preview into a selector and every entered list into a preview.
 ("M114 [[UI-17]] `is this screen entered` is inverted for the two list screens",
  "        case Screen::inbox:    return view == ListView::interactive;",
  "        case Screen::inbox:    return view == ListView::passive;"),
 # --- §UI-17 S1, THE HOISTED DECISIONS' OWN CONTROLS (QG-RULED 2026-08-21) ------------------------------------------
 # ★ Each entry below attacks ONE line of ONE pure decision, and therefore BOTH list screens at once. That is the
 #   whole reason the branches were hoisted: five duplicated decisions could only ever be half-protected.
 # ⛔⛔ M115 IS §B64's ORDER, REVERSED — the defect the deviation exists to prevent. A roster that shrank leaves the
 #     lost pick's index sitting ON the `BACK` index, so resolving the row first turns a REFUSAL into a silent
 #     "leave": the operator's `double`, aimed at a teammate, quietly closes the list and says nothing.
 ("M115 [[UI-17]] the BACK row OUTRANKS the refusal, so a lost pick silently LEAVES instead",
  "    if (!entered)  return ListAct::enter;\n    if (pick_gone) return ListAct::refuse;",
  "    if (!entered)  return ListAct::enter;\n"
  "    if (list_row_kind(cursor, shown) == ListRow::back) return ListAct::leave;\n"
  "    if (pick_gone) return ListAct::refuse;"),
 # ⛔ M116 is the row resolution collapsed the other way: the exit row activates as a MEMBER, so `BACK` sends.
 ("M116 [[UI-17]] the BACK row activates as a MEMBER row (the exit SENDS)",
  "    return (list_row_kind(cursor, shown) == ListRow::back) ? ListAct::leave : ListAct::member;",
  "    return ListAct::member;"),
 # ⛔⛔ M117 IS THE PASSIVE ENTRY DROPPED — i.e. the PRE-S1 behaviour, restored: a `double` on a screen the operator
 #     never entered acts on whatever row the cursor happens to be on. That is the mis-send this slice removes.
 ("M117 [[UI-17]] a `double` on a PASSIVE preview acts on the row instead of entering",
  "    if (!entered)  return ListAct::enter;",
  "    ;"),
 # ⛔⛔ M118 IS THE DEAD END: the `BACK` row stops retiring the refusal, so a lost pick whose roster then EMPTIED
 #     leaves the operator inside a list where every `double` refuses and `BACK` is one of them.
 ("M118 [[UI-17]] resting on BACK no longer retires the refusal (the dead end)",
  "    return (list_row_kind(cursor, shown) == ListRow::member) ? ListNote::record : ListNote::retire;",
  "    return (list_row_kind(cursor, shown) == ListRow::member) ? ListNote::record : ListNote::keep;"),
 # ⛔ M119 IS [[B223]]'s ARM, AND IT IS MUTATABLE ONLY BECAUSE THE DECISION WAS HOISTED: through the model nothing can
 #    leave a screen while its list is entered, so this guard is unreachable there — written at the call site it would
 #    have been a guard no suite drives and no mutation reddens. Here the pure case drives it directly.
 ("M119 [[B223]] leaving the screen no longer retires its message (a stale refusal a lap later)",
  "    if (!on_screen) return ListNote::retire;",
  "    ;"),
 # ⛔⛔ M120 IS THE ONE GAP WITH NOTHING TO HOIST — a SINGLE site, so it gets a direct control (QG's option (a) for
 #     exactly this entry). Drop the row-0 identity establishment and the press that ENTERED the list records no
 #     pick, so the very next `double` — the operator's first act inside the list — REFUSES.
 ("M120 [[UI-17]] entering the list records no pick, so the first double inside it refuses",
  "        _st.list_view = ListView::interactive; _st.cursor = 0; _st.dirty = true;\n"
  "        note_team_cursor(s); note_inbox_cursor(s);",
  "        _st.list_view = ListView::interactive; _st.cursor = 0; _st.dirty = true;"),
 # --- §UI-17 slice 2: §3.3 RETENTION CONFORMANCE (spec §1.5's T3/T4 verdict block, ruled §9 R-1) ---------------------
 # ★★★★ THIS BLOCK IS THE ONLY ONE IN THE FILE WHOSE MUTATIONS **ADD** SHIPPED CODE BACK, and that is what a
 #   REVERSAL needs: the tempting wrong fix here is not a deletion but a RESTORATION — "a modal that can outlive the
 #   user's attention is a modal that eventually sends the wrong thing" is a good argument, it is written down in
 #   design §3.2.1 and §3.5, and it is exactly what the owner overruled. ⇒ S01/S02 put each timeout back, one modal
 #   each, and REQUIRE the suite to go red; without them the deletion would be pinned by nothing but its own absence.
 # ⛔ S01/S02 ANCHOR ON `tick_emergency(s);` — the live first statement of `on_tick` — rather than on the withdrawn
 #   text, which is quoted in the source as a comment and deliberately wrapped so it cannot be matched (a mutation
 #   that edits a comment compiles, stays green, and is reported as a measured property: the VACUOUS class).
 ("S01 [[UI-17]] the compose sub-view's kBlankMs auto-exit is re-instated (§3.2.1 restored over §3.3)",
  "        tick_emergency(s);\n",
  "        tick_emergency(s);\n"
  "        if (_st.compose != Compose::none && elapsed(s.now_ms, _last_input_ms) >= kBlankMs) close_compose();\n"),
 ("S02 [[UI-17]] the detail modal's kBlankMs auto-exit is re-instated (§3.5 restored over §3.3)",
  "        tick_emergency(s);\n",
  "        tick_emergency(s);\n"
  "        if (_st.detail != InboxModal::closed && elapsed(s.now_ms, _last_input_ms) >= kBlankMs) close_detail();\n"),
 # ⛔⛔ S03 IS THE HALF THAT **PAYS** FOR S01/S02, and dropping it is the plausible tidy-up now that "modals are
 #     preserved": a hidden `delete` selection then survives underneath an alarm overlay that owns the body and
 #     absorbs a `double` — invisible, unreachable, and still armed the moment the overlay goes away (§UI-7D §3.5).
 #     ⓘ M27/M28 attack the SAME line from the other two directions (wrong gesture · no close at all); this one is
 #     listed with S01/S02 because after the ruling it is the emergency exception's own control.
 ("S03 [[UI-17]] the compose sub-view is no longer closed by a COMMITTED alarm (§B101's close dropped)",
  "    close_compose();\n    retain(s.now_ms);",
  "    retain(s.now_ms);"),
 # ⛔⛔ S04 IS THE RULING TAKEN ONE STEP TOO FAR — the "obvious" companion edit, and the reason the source states the
 #     blank is UNCONDITIONAL. If preserving a modal is right, keeping the panel lit for it looks right too; it is
 #     not. `ui_allows_sleep` requires `blanked`, so a node with a forgotten modal open would never blank AND never
 #     light-sleep again — a power regression with nothing visible on the panel to explain it (spec S2 pin 5).
 # ⛔⛔ S05/S06 ARE THE TWO INDEPENDENT HALVES OF "THE RETAINED MODAL KEEPS ITS PAGE" (QG-ruled 2026-08-21), and they
 #     are separate entries because EITHER ONE ALONE STILL LOSES THE PAGE. They are also the slice's own lesson: the
 #     ruling preserved the modal and the SELECTION, and the page drifted out through the one clock nobody named.
 # ⛔ S05 — the cadence runs on a panel nobody can see, so a modal retained across a 15 s blank comes back on a
 #    DIFFERENT page. Tempting precisely because the gate looks redundant next to `detail_pages > 1`.
 # ⚠ S05's ANCHOR MOVED 2026-08-21 (the QG boundary fix put `!blank_due(s)` on the same condition); the property is
 #   unchanged — the cadence must not run on a DARK panel — and the replacement deliberately KEEPS `!blank_due(s)`,
 #   so this entry still reddens ONLY the dark-phase assertions and stays independent of S07.
 ("S05 [[UI-17]] the detail page cadence keeps running while the panel is DARK",
  "        if (!_st.blanked && !blank_due(s) && _st.detail == InboxModal::body && _st.detail_pages > 1 &&",
  "        if (!blank_due(s) && _st.detail == InboxModal::body && _st.detail_pages > 1 &&"),
 # ⛔⛔ S07 IS THE BOTH-DUE BOUNDARY (QG-ruled 2026-08-21), and it is a THIRD independent way to lose the page: drop
 #     the priority and the tick that CROSSES the blank deadline turns the page and hides it in the same pass — the
 #     operator wakes onto a page they never saw.
 # ⓘ WHAT IT FAILS, ITEMISED AND **MEASURED** (⛔ CORRECTED IN PLACE: this note claimed it fired on *"ONE assertion …
 #   and on nothing else"*, and the runner reports SEVEN — the claim was false as written, so it is replaced by the
 #   list rather than by a narrower control). ★ ALL SEVEN ARE ONE DIVERGENCE PROPAGATING, which is provable from the
 #   values rather than argued: every failure reads `page == expected + 1`, i.e. exactly the ONE extra turn the
 #   crossing tick took, carried forward unchanged.
 #     `ui17-hold: the detail modal survives the blank …`  (2)
 #       :5627  at the blank            3 == 2   the page the dark panel keeps
 #       :5635  after the wake          3 == 2   ...and the page the wake restores
 #     `ui17-hold: a multi-page detail keeps its page …`   (5)
 #       :5669  at the CROSSING tick    3 == 2   ★ the divergence itself
 #       :5673  after 4 dark ticks      3 == 2   carried through the dark (S05's gate still holds)
 #       :5681  after the wake pass     3 == 2   carried through the wake  (S06's restart still holds)
 #       :5685  at wake + period - 1    3 == 2   carried, and still not early
 #       :5687  at wake + period        4 == 3   the resume lands one page late
 # ★ THE SEPARABILITY IS UNAFFECTED, and the list is what shows it: the dark ticks are still gated by `!_st.blanked`
 #   (S05's term) and the wake still restarts the cadence (S06's), so ⛔ NO assertion outside the page property moves
 #   — no modal closes, no record changes, no store request appears. Each of S05/S06/S07 still fails a set the other
 #   two do not.
 ("S07 [[UI-17]] the page turn is no longer outranked by a blank due on the SAME tick",
  "        if (!_st.blanked && !blank_due(s) && _st.detail == InboxModal::body && _st.detail_pages > 1 &&",
  "        if (!_st.blanked && _st.detail == InboxModal::body && _st.detail_pages > 1 &&"),
 # ⛔⛔ S06 — the SUBTLER half, and it is invisible to any harness that does not mirror the real loop's order:
 #     `mr_ui_tick` runs `on_gesture(...)` then `on_tick(s)` on ONE snapshot, so with a stale `_detail_page_at_ms`
 #     the WAKE PASS ITSELF banks the whole dark interval and turns the page before the first frame. ⓘ `(void)` keeps
 #     the mutant warning-clean, so a build failure can never be mistaken for the property.
 ("S06 [[UI-17]] the wake does not restart the page cadence, so the wake pass turns the page",
  "    void unblank(uint32_t now_ms) {\n        _st.blanked = false;\n        _detail_page_at_ms = now_ms;\n    }",
  "    void unblank(uint32_t now_ms) {\n        _st.blanked = false;\n        (void)now_ms;\n    }"),
 # ⚠ S04's ANCHOR MOVED 2026-08-21 (the QG boundary fix hoisted the predicate into `blank_due` so ONE authority
 #   answers it for both the transition and the page cadence). The property is unchanged — the blank deadline may
 #   not become conditional on a modal — and mutating the hoisted predicate now covers BOTH readers at once, which is
 #   strictly stronger than the old single-site anchor.
 # ⚠ S04's ANCHOR MOVED AGAIN 2026-08-22 (§UI-17 S8 added `!wake_active(s.now_ms)` as the predicate's third term and
 #   wrapped it onto two lines). The property is UNCHANGED — the blank deadline may not become conditional on a modal
 #   — and the replacement keeps both existing terms, so this entry stays independent of S10/S11 below.
 ("S04 [[UI-17]] the blank deadline is made CONDITIONAL on no modal being open",
  "        return !_st.blanked && !hold_active(s.now_ms) && !wake_active(s.now_ms) &&\n"
  "               elapsed(s.now_ms, _last_input_ms) >= kBlankMs;",
  "        return !_st.blanked && !hold_active(s.now_ms) && !wake_active(s.now_ms) &&\n"
  "               _st.compose == Compose::none && _st.detail == InboxModal::closed &&\n"
  "               elapsed(s.now_ms, _last_input_ms) >= kBlankMs;"),
 # --- §UI-17 slice 8: WAKE ON RECEIVE — the MODEL half (the EFFECT; the scope is `uisend`'s) ------------------------
 # ★★★★ THE RULING (owner, 2026-08-20, spec §9 R-6/R-7): a received message lights the panel, for a DM addressed to us
 #      and for a SEALED team post — with ⛔ no rate limiter. WHICH push may call `on_msg_wake` is decided in
 #      `src/firmware_ui_send.h` and is attacked by the `uisend` target; every entry HERE attacks the EFFECT, i.e. one
 #      of the five invariants the ruling put around it (the separate deadline, the untouched input clock, no
 #      navigation, no emergency write, the quiet node's sleep).
 # ⛔⛔ THE SPEC's *"the wake writing `_last_input_ms`"* MUTATION IS **NOT LISTED IN ITS BARE FORM, AND THE OMISSION IS
 #     MEASURED RATHER THAN AN OVERSIGHT**: §9 R-1 deleted both modal auto-exits, so `blank_due` is the ONLY remaining
 #     reader of that field, and a `kBlankMs`-from-now wake window is ARITHMETICALLY IDENTICAL to stamping it — the
 #     bare mutant is behaviourally inert and would be reported GREEN, i.e. "nothing measures the property", which is
 #     exactly what this runner refuses to file as a measurement. ⇒ S09 mutates the TEMPTING FULL SHAPE instead (the
 #     `on_gesture` pair, `_last_input_ms` **and** `_seeded`), which re-creates [[B65]] and IS observable.
 ("S08 [[UI-17]] the wake marks the model dirty but leaves the panel DARK (§R1's exact defect, one plane over)",
  "        if (_st.blanked) { unblank(now_ms); _st.dirty = true; }",
  "        if (_st.blanked) { _st.dirty = true; }"),
 ("S09 [[UI-17]] the wake stamps the input clock the way `on_gesture` does (and consumes [[B65]]'s seed)",
  "        _msg_wake_until_ms = now_ms + kBlankMs;\n        _msg_wake_armed = true;",
  "        _msg_wake_until_ms = now_ms + kBlankMs;\n        _msg_wake_armed = true;\n"
  "        _last_input_ms = now_ms; _seeded = true;"),
 # ⛔ S10 IS THE ONE-LINE VERSION OF THIS SLICE — clear `blanked` and rely on the existing deadline, which is what
 #    `on_reply` legitimately does (`kEmgHoldMs > kBlankMs` holds ITS panel lit). A plain message has no hold, so the
 #    very next tick blanks it again: a ONE-FRAME FLASH, and the ruling's attention window never happens.
 ("S10 [[UI-17]] the wake deadline is dropped from the blank condition (the one-frame flash)",
  "        return !_st.blanked && !hold_active(s.now_ms) && !wake_active(s.now_ms) &&",
  "        return !_st.blanked && !hold_active(s.now_ms) &&"),
 ("S11 [[UI-17]] `wake_active` is INVERTED — the window is live everywhere except during it",
  "        return _msg_wake_armed && left != 0 && left <= kBlankMs;            // i.e. now < deadline, at most one window",
  "        return _msg_wake_armed && !(left != 0 && left <= kBlankMs);          // i.e. now < deadline, at most one window"),
 # ⛔⛔ S12 IS THE FLAG THE SPEC's ESTIMATE DID NOT CARRY, and it is not fastidiousness: without it the initial 0 reads
 #     as a deadline 24.8 DAYS AHEAD for every `now_ms > 2^31`, so a node that has been up four weeks and received
 #     NOTHING never blanks and (through `ui_allows_sleep`) never light-sleeps again — spec pin 11 failing on the one
 #     node the ruling promises to leave alone.
 ("S12 [[UI-17]] the ARMED flag is dropped, so a quiet node past 2^31 ms never blanks again",
  "        return _msg_wake_armed && left != 0 && left <= kBlankMs;            // i.e. now < deadline, at most one window",
  "        return left != 0 && left <= kBlankMs;            // i.e. now < deadline, at most one window"),
 # ⛔⛔ S17 IS THE BOUND's OWN CONTROL, and the defect it re-opens was MEASURED IN THE PROBE rather than imagined: with
 #     `hold_active`'s bare half-counter comparison an EXPIRED wake deadline reads as a FUTURE one again once `now` has
 #     run 2^31 ms past it, so a node that received ONE message and then nothing stops blanking ~24.8 days later — for
 #     the next ~24.8 days. ⓘ The armed flag cannot see it (it never clears), which is why this is a SECOND entry.
 ("S17 [[UI-17]] the wake window loses its upper bound, so a month-old wake revives",
  "        return _msg_wake_armed && left != 0 && left <= kBlankMs;            // i.e. now < deadline, at most one window",
  "        return _msg_wake_armed && left != 0 && left < (1u << 31);           // i.e. now < deadline, at most one window"),
 # ⛔ S13 — the unconditional `unblank`, which is how `on_reply` writes it and therefore the plausible copy. On an
 #    ALREADY-LIT panel it restarts the detail page cadence, so a reader mid-page is interrupted by traffic (pin 10).
 ("S13 [[UI-17]] a wake on an ALREADY-LIT panel restarts the display cadence (the unconditional unblank)",
  "        if (_st.blanked) { unblank(now_ms); _st.dirty = true; }",
  "        unblank(now_ms); _st.dirty = true;"),
 ("S14 [[UI-17]] the wake NAVIGATES to INBOX (the [[B233]] class: the operator's place taken by a push)",
  "        _msg_wake_armed = true;\n        if (_st.blanked)",
  "        _msg_wake_armed = true;\n        _st.screen = Screen::inbox;\n        if (_st.blanked)"),
 ("S15 [[UI-17]] the wake clears the emergency, so a message dismisses a retained distress outcome",
  "        _msg_wake_armed = true;\n        if (_st.blanked)",
  "        _msg_wake_armed = true;\n        _emg = Emergency::idle;\n        if (_st.blanked)"),
 # ⛔ S16 — "one clock, surely": measure the window from the last INPUT instead of from the message. A message arriving
 #    late in an attention window then buys almost nothing, and the ruling's *"the standard blank timeout re-applies
 #    after the wake"* becomes "...expires with whatever the press left over" (pin 9).
 ("S16 [[UI-17]] the wake window is measured from the last PRESS, not from the message",
  "        _msg_wake_until_ms = now_ms + kBlankMs;",
  "        _msg_wake_until_ms = _last_input_ms + kBlankMs;"),
 # ===== §UI-16 N2 — the `JOIN TEAM` child, the DIRECT landing and the FROZEN list ================================
 # ★★★ N01 AND N04 ARE THE HEADLINE PAIR. N01 encodes the coincidence `provision_rows` refuses to encode (all three
 #     predicates are true in every env in the tree today, so the fold is INVISIBLE on hardware and shows up only in
 #     the arm the native suite drives). N04 breaks owner ruling R-10's freeze — a per-tick re-read is the shape a
 #     reviewer reaches for when the list "looks stale", and it is precisely what lets a row move under the cursor.
 ("N01 ★★ the JOIN TEAM child is FOLDED into the CREATE predicate — the coincidence encoded as a rule",
  "    if (join_team)   l.row[l.n++] = ProvRow::join_team;",
  "    if (create_team) l.row[l.n++] = ProvRow::join_team;"),
 ("N02 ★★ the parent-row predicate is RE-SPELLED from the old two children instead of derived from the child list "
  "— the third child opens a sub-view no visible row leads to",
  "    const ProvRowList l = provision_rows(create_team, join_static, join_team, invite);\n"
  "    for (uint8_t i = 0; i < l.n; ++i)\n"
  "        if (l.row[i] != ProvRow::back) return true;\n"
  "    return false;",
  "    return create_team || join_static;"),
 ("N03 ★★ the own-team id is not handed to the capture — the team we are already in is offered as a candidate",
  "        _st.nearby = nearby_capture(s.nearby, s.nearby_n, s.team_id);",
  "        _st.nearby = nearby_capture(s.nearby, s.nearby_n, 0);"),
 ("N04 ★★★ the scan is RE-READ EVERY TICK — owner ruling R-10's frozen-per-entry snapshot is gone and a team that "
  "walks into range inserts a row under the operator's cursor",
  "        list_follow_screen();",
  "        if (_st.provisioning == Provision::nearby) load_nearby(s);\n        list_follow_screen();"),
 ("N05 ★★ BACK leaves the SCREEN instead of returning to the PROVISION menu (the containment contract, broken)",
  # ⚠ ANCHORED ON THE LINE ABOVE IT TOO: `join_select_gesture` carries the IDENTICAL `back` landing one screen over,
  #   so the bare line matches TWICE and the runner reports it VACUOUS — which is exactly what it did on the first
  #   full pass. The fail-closed line's comment names `NearbySelList`, so the pair is unique.
  "        if (!l.at(_st.cursor, r)) return;                            // fails closed — see NearbySelList::at\n"
  "        if (r.back) { enter_provision(Provision::menu); return; }",
  "        if (!l.at(_st.cursor, r)) return;                            // fails closed — see NearbySelList::at\n"
  "        if (r.back) { close_settings_menu(); return; }"),
 # ⛔⛔ N06 RE-ANCHORED 2026-08-23 (§UI-16 N3), AND THE WITHDRAWN PATTERN IS KEPT VISIBLE — it read:
 #        "        NearbySelRow r{};\n"
 #        "        if (!l.at(_st.cursor, r)) return;                            // fails closed — …"
 #      ->  "        NearbySelRow r{};\n        if (!l.at(_st.cursor, r)) return;\n        enter_provision(Provision::menu);"
 #      i.e. *"ANY double LEAVES the scan"*, which is what the arm could do wrongly while it did nothing at all.
 # ★ IT WENT DEAD THE MOMENT N3 LANDED, and the full pass of 2026-08-23 MEASURED it: **`FAIL N06 … the suite still
 #   PASSES; nothing measures this`**. The reason is structural, not a missing case — the arm now ENDS in
 #   `enter_provision(Provision::nearby_confirm)`, so an injected landing two lines above it is OVERWRITTEN on every
 #   path (a team row lands on the confirmation either way; BACK lands on the menu either way) and the mutant is
 #   observationally identical to the original. ⚠ An entry that cannot fail is the [[B217]] class, so it is
 #   re-anchored rather than deleted or left standing.
 # ⇒ THE SAME DEFECT CLASS IN THE NEW SHAPE: *"ANY double ACTS, not just the one on a TEAM row"* — the BACK branch
 #   dropped, so leaving the list opens a `JOIN <fingerprint>?` for whatever the BACK row's empty `team` holds.
 #   ⓘ It is ⛔ NOT N10's twin: N10 is the CONFIRMATION's BACK, this is the LIST's.
 ("N06 ★★ ANY double ACTS, not just the one on a TEAM row — leaving the list opens a JOIN confirmation",
  "        if (r.back) { enter_provision(Provision::menu); return; }\n"
  "        // ★★★ THE PICK IS THE ROW'S OWN FULL 32-BIT TEAM ID",
  "        // ★★★ THE PICK IS THE ROW'S OWN FULL 32-BIT TEAM ID"),
 # ===== §UI-16 N3 — the `JOIN <fingerprint>?` confirmation and the act ==========================================
 # ★★★ N08 AND N09 ARE THE SPEC'S OWN HEADLINE PAIR (§4-N3 pin 2, §3 P-7, [[B48]]'s class), and they are the two
 #     ways a selection stops being an identity: the INDEX (a list that is own-team-FILTERED, so the same index is a
 #     different team on a different node) and the TOKEN (the low 24 bits — 255 teams share every fingerprint). Both
 #     are one expression, both leave the panel looking exactly right, and both join the wrong team.
 # ⛔ N07/N12/N13 ARE THE CONFIRMATION'S SAFETY: the default arm, the BACK branch that must not fall through, and the
 #    floor that stops a pick of 0 — which is `team 0`, i.e. a LEAVE — from reaching the transaction.
 ("N07 ★★ the confirmation opens on CONFIRM (P-13's safe default gone: one press from a membership change)",
  "        _st.nearby_sel_id = r.team.team_id;\n"
  "        enter_provision(Provision::nearby_confirm);",
  "        _st.nearby_sel_id = r.team.team_id;\n"
  "        enter_provision(Provision::nearby_confirm);\n"
  "        _st.prov_confirm = ProvConfirm::confirm;"),
 ("N08 ★★★ the act is handed the CURSOR INDEX instead of the row identity (§B66)",
  "        _st.nearby_sel_id = r.team.team_id;",
  "        _st.nearby_sel_id = _st.cursor;"),
 ("N09 ★★★ the id is RE-DERIVED from the six-hex fingerprint the panel shows (the low 24 bits, [[B48]])",
  "        _st.nearby_sel_id = r.team.team_id;",
  "        _st.nearby_sel_id = r.team.team_id & 0x00FFFFFFu;"),
 ("N10 ★★ the confirmation BACK returns to the MENU instead of the NEARBY list (the containment contract)",
  "        if (_st.prov_confirm == ProvConfirm::back) { enter_provision(Provision::nearby); return; }",
  "        if (_st.prov_confirm == ProvConfirm::back) { enter_provision(Provision::menu); return; }"),
 ("N11 ★★ `team_joined` is mapped onto CREATE's string — a JOIN reports TEAM CREATED (F-4, at its source)",
  "        case UiProvOutcome::team_joined: return \"TEAM JOINED\";",
  "        case UiProvOutcome::team_joined: return \"TEAM CREATED\";"),
 ("N12 ★★ a double on BACK falls through into the act (one press means the other)",
  "        if (_st.prov_confirm == ProvConfirm::back) { enter_provision(Provision::nearby); return; }\n"
  "        run_join_team();",
  "        run_join_team();"),
 ("N13 the zero-pick floor is dropped, so a row naming no team performs `team 0` — a LEAVE",
  "        if (_prov && _st.nearby_sel_id != 0) {",
  "        if (_prov) {"),
 # ===== §UI-16 N4 — the INVITATION WINDOW's MODEL HALF (the pure unit's own rulings are `--target=uiinvite`) =====
 # ★★★ V01-V03 ARE THE **SNAPSHOT's THREE WAYS TO BE WRONG**, and they are three entries because they fail
 #     differently: keyed on the wrong thing, taken at the wrong TIME, or never taken at all. ⓘ V01 is the one the
 #     spec names by its temptation — `last_seen_ms` is RIGHT THERE on the TEAM row this loop already walks, and it
 #     means *last heard*, so a diff keyed on it announces the whole team on its next beacon (§1.3).
 # ★★ V05 IS THE POWER DEFECT: a window that stamps `_last_input_ms` keeps a safety device's panel lit for five
 #    minutes and, through `ui_allows_sleep` (which requires `blanked`), stops it light-sleeping at all — ✅ OQ-3
 #    ruled the window does NOT hold the panel lit, and this is that ruling's control.
 ("V01 ★★ the diff is keyed on the row's FRESHNESS (`last_heard_s`) instead of the opening snapshot — the "
  "`last_seen_ms` defect §1.3 names, at the one site where that field is in scope",
  "        const InviteSelList l = invite_sel_rows(_st.invite, s.member, s.team_shown);",
  "        InviteSelList l{};\n"
  "        for (uint8_t i = 0; i < s.team_shown && i < kMaxInviteRows; ++i)\n"
  "            if (s.team[i].last_heard_s < 60u && s.member[i].key_hash32 != 0) {\n"
  "                l.row[l.n].cand = s.member[i]; l.row[l.n].back = false; ++l.n; }\n"
  "        l.row[l.n].back = true; ++l.n;"),
 ("V02 ★★★ the snapshot is re-taken at every RENDER instead of at the window's OPEN — so the candidate the window "
  "exists to surface is inside the snapshot by the time it is drawn",
  "    void tick_invite(const UiSnapshot& s) {\n"
  "        if (!provision_is_invite(_st.provisioning)) return;",
  "    void tick_invite(const UiSnapshot& s) {\n"
  "        if (!provision_is_invite(_st.provisioning)) return;\n"
  "        _st.invite = invite_snapshot_take(s.member, s.team_shown);"),
 ("V03 ★★ the snapshot is NEVER taken — the window opens with no authorities at all",
  "            case ProvRow::invite:      load_invite(s);       enter_provision(Provision::invite);      return;",
  "            case ProvRow::invite:      enter_provision(Provision::invite);      return;"),
 ("V04 ★★★ the expiry RENEWS the window instead of closing it — the bounded window is unbounded and the panel "
  "never says WINDOW CLOSED (P-11)",
  "        enter_provision(Provision::invite_closed);",
  "        _invite_until_ms = s.now_ms + kInviteWindowMs;"),
 ("V05 ★★★ the open window WRITES `_last_input_ms` on every tick — the panel never blanks and the node never "
  "light-sleeps while it is up (OQ-3's refusal)",
  "        if (!provision_is_invite(_st.provisioning)) return;\n"
  "        if (window_active(s.now_ms)) return;",
  "        if (!provision_is_invite(_st.provisioning)) return;\n"
  "        _last_input_ms = s.now_ms;\n"
  "        if (window_active(s.now_ms)) return;"),
 ("V06 ★★★ the handled set is made PERSISTENT — a candidate rejected in one window stays suppressed in the next, "
  "which the ruling forbids (F-13: it is discarded when the window closes)",
  "        if (!provision_is_invite(p)) _st.invite = InviteWindow{};",
  "        if (!provision_is_invite(p)) { const InviteWindow keep = _st.invite; _st.invite = InviteWindow{};\n"
  "            _st.invite.handled_n = keep.handled_n;\n"
  "            for (uint8_t i = 0; i < kMaxInviteRows; ++i) _st.invite.handled[i] = keep.handled[i]; }"),
 ("V07 ★★★ the confirmation does NOT freeze the selection — every refresh re-points it at the row under the "
  "cursor, so a member arriving between the two presses moves the target (F-14)",
  "        if (window_active(s.now_ms)) return;",
  "        if (_st.provisioning == Provision::invite_confirm) {\n"
  "            InviteSelRow rr{};\n"
  "            if (invite_sel_rows(_st.invite, s.member, s.team_shown).at(_st.cursor, rr) && !rr.back) {\n"
  "                _st.invite.sel_hash = rr.cand.key_hash32; _st.invite.sel_id = rr.cand.id; }\n"
  "        }\n"
  "        if (window_active(s.now_ms)) return;"),
 ("V08 ★★★ the row is keyed by the DISPLAY NAME instead of the hash — the act carries a name-derived value, and "
  "the name is MUTABLE (P-7d, node_hashlocate.cpp:346)",
  "        _st.invite.sel_hash = r.cand.key_hash32;",
  "        { uint32_t k = 0; for (const char* p = r.cand.name; *p; ++p) k = uint32_t(k * 31u + uint8_t(*p));\n"
  "          _st.invite.sel_hash = k; }"),
 # ★★★ V09 IS `hold_active`'s BARE COMPARISON, RESTORED — and it is the entry the QG blocker had no control for.
 #     `elapsed(deadline, now)` is ZERO **at** the deadline, so a bare `< 2^31` reads the arrival instant as "still
 #     open" and the ruled FIVE MINUTES runs for five minutes and ONE MILLISECOND. It is the most tempting wrong fix
 #     in the file — the neighbouring `hold_active` is written exactly this way — which is precisely why the edge
 #     needs a control rather than a comment.
 ("V09 ★★★ the window's `left != 0` term is dropped — at the EXACT deadline the window is still open, so the "
  "five-minute bound is five minutes and one millisecond (`wake_active`'s edge, lost)",
  "               left != 0 && left < (1u << 31);                       // i.e. now < deadline, STRICTLY",
  "               left < (1u << 31);"),
]

# ===== §UI-13 — src/firmware_config_service.h =====================================================================
# ★ EVERY ENTRY IS A TEMPTING WRONG FIX, and most of them are a state COLLAPSE: comparing two of the three states
#   where the contract names a different two (C03/C12/C18/C24), or dropping one of `save()`'s four ordered gates
#   (C01/C02/C04/C07). That is the defect this slice was pre-registered against, so it is what the battery is aimed at.
MUTS_CONFIG = [
 # --- save()'s four gates, and their ORDER -------------------------------------------------------------------------
 ("C01 the no-op gate is dropped (every SAVE writes)",
  "if (_draft == persisted) return CfgSave::no_change;               // (3) ZERO writes",
  ";"),
 ("C02 the no-op gate is checked BEFORE the conflict gate (a moved record reads as 'nothing to do')",
  "if (persisted != _baseline) { _conflict = true; return CfgSave::conflict; }   // (2b) ZERO writes\n"
  "        if (_draft == persisted) return CfgSave::no_change;               // (3) ZERO writes",
  "if (_draft == persisted) return CfgSave::no_change;\n"
  "        if (persisted != _baseline) { _conflict = true; return CfgSave::conflict; }"),
 ("C03 the conflict gate compares PERSISTED with the DRAFT instead of with the BASELINE",
  "if (persisted != _baseline) { _conflict = true; return CfgSave::conflict; }",
  "if (persisted != _draft) { _conflict = true; return CfgSave::conflict; }"),
 ("C04 LAST-WRITER-WINS: the conflict gate is dropped entirely",
  "if (persisted != _baseline) { _conflict = true; return CfgSave::conflict; }   // (2b) ZERO writes",
  ";"),
 ("C32 the LATCH is ignored at SAVE: only the bytes are compared (change -> notify -> revert -> SAVE)",
  "if (_conflict) return CfgSave::conflict;                          // (2a) THE LATCH, ZERO writes",
  ";"),
 ("C05 the marker is cleared BEFORE the durable write returns (a fact not established by the act)",
  "if (!_store.save(now)) return CfgSave::nv_failed;                //     EXACTLY ONE write attempt\n"
  "        _baseline = _draft;                                              //     no longer unsaved",
  "_baseline = _draft;\n"
  "        if (!_store.save(now)) return CfgSave::nv_failed;"),
 ("C06 the live apply happens BEFORE the durable write",
  "if (!_store.save(now)) return CfgSave::nv_failed;                //     EXACTLY ONE write attempt\n"
  "        _baseline = _draft;                                              //     no longer unsaved\n"
  "        _live.apply_live(cfg_live_fields(_draft));                       //     live ONLY after durable success",
  "_live.apply_live(cfg_live_fields(_draft));\n"
  "        if (!_store.save(now)) return CfgSave::nv_failed;\n"
  "        _baseline = _draft;"),
 ("C07 SAVE trusts the per-field setter and skips whole-candidate validation",
  "if (!cfg_values_valid(_draft, bad)) return CfgSave::invalid;      // (1) ZERO writes",
  "(void)bad;"),
 ("C08 an unreadable record is treated as 'first provision — just write a fresh one'",
  "if (!_store.load(now)) return CfgSave::nv_failed;                 // cannot preserve the rest -> refuse (0 writes)",
  "if (!_store.load(now)) now = mrnv::Blob{};"),
 ("C09 the write starts from a CLEAN record instead of the reloaded one (every non-covered field is lost)",
  "cfg_values_into_blob(_draft, now);                               // (4) covered fields only",
  "now = mrnv::Blob{}; cfg_values_into_blob(_draft, now);"),
 ("C10 the reboot-class outcome is collapsed into `saved` (the two flags become one)",
  "return reboot_required() ? CfgSave::saved_reboot : CfgSave::saved;",
  "return CfgSave::saved;"),
 # --- the carriers: the field-drop rot -----------------------------------------------------------------------------
 ("C11 cfg_values_into_blob drops one field (S1/L9 field-drop rot)",
  "b.intro_attach        = c.at(CfgField::intro_attach);",
  ";"),
 ("C12 cfg_values_from_blob crosses two fields",
  "c.at(CfgField::intro_attach)        = b.intro_attach;",
  "c.at(CfgField::intro_attach)        = b.e2e_dm;"),
 ("C13 cfg_live_fields drops a live-class field (it is applied as OFF)",
  "f.mobile_autoregister = c.at(CfgField::mobile_autoregister) != 0;",
  "f.mobile_autoregister = false;"),
 ("C14 CfgValues equality compares only the first field",
  "inline bool operator==(const CfgValues& a, const CfgValues& b) { return memcmp(a.v, b.v, sizeof a.v) == 0; }",
  "inline bool operator==(const CfgValues& a, const CfgValues& b) { return a.v[0] == b.v[0]; }"),
 # --- typed validation ---------------------------------------------------------------------------------------------
 ("C15 `set` COERCES an out-of-domain value instead of refusing it",
  "if (!cfg_field_valid(f, val)) return CfgSet::bad_value;   // fail closed: the draft is not half-written\n"
  "        _draft.at(f) = val;",
  "_draft.at(f) = (val != 0) ? 1 : 0;"),
 ("C16 the MENU's off/on narrowing leaks into the SHARED service's ble_mode domain",
  "case CfgField::ble_mode:            return val <= 2;",
  "case CfgField::ble_mode:            return val <= 1;"),
 ("C17 the whole-candidate check stops after the first field",
  "for (uint8_t i = 0; i < kCfgFieldCount; ++i) {\n"
  "        const CfgField f = static_cast<CfgField>(i);\n"
  "        if (!cfg_field_valid(f, c.at(f))) { first_bad = f; return false; }",
  "for (uint8_t i = 0; i < 1; ++i) {\n"
  "        const CfgField f = static_cast<CfgField>(i);\n"
  "        if (!cfg_field_valid(f, c.at(f))) { first_bad = f; return false; }"),
 # --- the three predicates -----------------------------------------------------------------------------------------
 ("C18 `config_unsaved` compares the DRAFT with EFFECTIVE (the third state, wrongly chosen)",
  "bool config_unsaved() const { return _open && _draft != _baseline; }",
  "bool config_unsaved() const { return _open && _draft != _live.effective(); }"),
 ("C19 `reboot_required` reads the DRAFT, so an unsaved edit already demands a reboot",
  "if (_baseline.at(f) != eff.at(f)) return true;",
  "if (_draft.at(f) != eff.at(f)) return true;"),
 ("C20 `reboot_required` ignores the apply class (a live-class difference demands a reboot too)",
  "if (cfg_apply_class(f) != CfgApplyClass::reboot_at) continue;",
  ";"),
 ("C21 ble_mode is classified LIVE (the tempting 'it is only a flag')",
  "case CfgField::ble_mode:            return CfgApplyClass::reboot_at;",
  "case CfgField::ble_mode:            return CfgApplyClass::live_now;"),
 # --- open / discard / reload / the external-write hook -------------------------------------------------------------
 ("C22 re-entering SETTINGS re-snapshots, silently discarding the draft",
  "if (_open) return CfgOpen::already_open;",
  ";"),
 ("C23 DISCARD clears the marker but keeps the draft",
  "_baseline = cfg_values_from_blob(b);\n"
  "        _draft    = _baseline;\n"
  "        _conflict = false;\n"
  "        return CfgRefresh::ok;",
  "_baseline = cfg_values_from_blob(b);\n"
  "        _conflict = false;\n"
  "        return CfgRefresh::ok;"),
 ("C24 RELOAD keeps the WHOLE draft (the last-writer-wins resurrection §3.6.1 forbids)",
  "if (_draft.at(f) == _baseline.at(f)) _draft.at(f) = now.at(f);   // untouched -> adopt theirs",
  ";"),
 ("C25 RELOAD overwrites the operator's edits, i.e. it becomes a second DISCARD",
  "            if (_draft.at(f) == _baseline.at(f)) _draft.at(f) = now.at(f);   // untouched -> adopt theirs\n"
  "        }",
  "            _draft.at(f) = now.at(f);\n"
  "        }"),
 ("C26 a failed reload/discard wipes the draft anyway",
  "if (!_store.load(b)) return CfgRefresh::nv_failed;   // the draft SURVIVES an unreadable store (C2)",
  "if (!_store.load(b)) { _draft = CfgValues{}; _conflict = false; return CfgRefresh::nv_failed; }"),
 ("C27 the external-write hook compares against the DRAFT, so an untouched field raises a false conflict",
  "if (cfg_values_from_blob(persisted_now) != _baseline) _conflict = true;",
  "if (cfg_values_from_blob(persisted_now) != _draft) _conflict = true;"),
 ("C28 `BACK` discards the draft (the forbidden attention-timeout discard)",
  "void on_back()  {}",
  "void on_back()  { discard(); }"),
 ("C29 blanking discards the draft",
  "void on_blank() {}",
  "void on_blank() { discard(); }"),
 # --- the two ruled panel headlines ---------------------------------------------------------------------------------
 ("C30 the conflict headline is dropped (the refusal becomes invisible)",
  'case CfgSave::conflict:  return "CFG! RELOAD";',
  'case CfgSave::conflict:  return "";'),
 ("C31 the two ruled headlines are swapped",
  'case CfgSave::conflict:  return "CFG! RELOAD";\n        case CfgSave::nv_failed: return "SAVE FAILED";',
  'case CfgSave::conflict:  return "SAVE FAILED";\n        case CfgSave::nv_failed: return "CFG! RELOAD";'),
]

# ===== §CHROME-1 — src/firmware_ui_chrome.h ========================================================================
# ★ THE SHAPE THIS BATTERY IS AIMED AT: a projection whose job is to CLASSIFY has two failure modes, and both look
#   correct in review. (a) it carries the RAW authority instead of the classified one — every value still "right",
#   but the panel's equality now changes when the pixels do not, which §8.3 turns into a repaint per tick; (b) it
#   COLLAPSES a third state into one of the other two — no team read as a team with zero peers, "not applicable"
#   rendered as a fault, "never confirmed" rendered as "just now". X01 is the exception and is the slice's headline
#   trap: a 32-bit millisecond age, which re-creates the ~49.7-day wrap this project already fixed once.
MUTS_CHROME = [
 # --- the 64-bit age, and the 32-bit cast the snapshot's own idiom invites -------------------------------------------
 ("X01 the compact age takes a 32-bit millisecond parameter (the ~49.7-day wrap, re-created)",
  "inline void ui_fmt_home_age(char* out, std::size_t cap, bool ever, uint64_t age_ms) {",
  "inline void ui_fmt_home_age(char* out, std::size_t cap, bool ever, uint32_t age_ms) {"),
 ("X02 `ever` is ignored, so a never-confirmed home renders `0s` instead of `--`",
  "if (!ever) {", "if (false) {"),
 ("X03 the seconds bucket is inclusive (60 s renders `60s`, not `1m`)",
  "if      (s <  60u) n = snprintf(out, cap, \"%us\", unsigned(s));",
  "if      (s <= 60u) n = snprintf(out, cap, \"%us\", unsigned(s));"),
 ("X04 the 100-day cutoff is inclusive (100 d renders a day count instead of `old`)",
  "else if (d < 100u) n = snprintf(out, cap, \"%ud\", unsigned(d));",
  "else if (d <= 100u) n = snprintf(out, cap, \"%ud\", unsigned(d));"),
 ("X05 the tokens are not NUL-padded (equality then depends on what the buffer held before)",
  "    for (std::size_t i = used; i < cap; ++i) out[i] = '\\0';",
  "    (void)out; (void)cap; (void)used;"),
 # --- the classification: raw values instead of drawn ones ----------------------------------------------------------
 ("X06 the mail slot carries the RAW combined count instead of the drawn digits",
  "    c.mail          = c.mail_overflow ? kMailMax : uint8_t(mail_total);",
  "    c.mail          = uint8_t(mail_total);"),
 # ⚠ RETARGETED at QG round 2: R2.2 rewrote the millivolt->decivolt line, and the OLD pattern went VACUOUS (match
 #   count 0) rather than silently passing — which is the runner's own instrument-that-cannot-fail guard doing its
 #   job, and the reason a mutation battery must be re-run after every edit to the file it targets.
 ("X07 the battery ROUNDS instead of truncating (4199 mV becomes 4.2V)",
  "    const int32_t dv = (s.batt_mv < 0) ? int32_t(-1) : (s.batt_mv / 100);",
  "    const int32_t dv = (s.batt_mv < 0) ? int32_t(-1) : ((s.batt_mv + 50) / 100);"),
 ("X08 equality is a `memcmp` over the struct, padding included (§8.2 forbids it in as many words)",
  "    for (std::size_t i = 0; i < kAgeTokenCap; ++i)\n"
  "        if (a.home_age[i] != b.home_age[i]) return false;\n"
  "    return a.mail            == b.mail",
  "    return __builtin_memcmp(&a, &b, sizeof a) == 0;\n"
  "    return a.mail            == b.mail"),
 # --- the third state, collapsed ------------------------------------------------------------------------------------
 ("X09 NO TEAM is collapsed into a team with zero teammates (`--` becomes `0`)",
  "    c.team_configured = s.team_build && s.team_id != 0;",
  "    c.team_configured = s.team_build;"),
 ("X10 the key slot is filled with no team configured (`blank` collapses into `crossed`)",
  "    if (c.team_configured) {", "    if (true) {"),
 ("X11 the people count is the UI's row capacity, not the true total (the retired `T8/12`)",
  "        c.team_overflow = s.team_total > kTeamMax;\n"
  "        c.team_count    = c.team_overflow ? kTeamMax : s.team_total;",
  "        c.team_overflow = s.team_shown > kTeamMax;\n"
  "        c.team_count    = c.team_overflow ? kTeamMax : s.team_shown;"),
 ("X12 a build with no mobile plane draws the CROSSED house (§4.2's forbidden fault icon)",
  "        c.home = HomeIcon::blank;", "        c.home = HomeIcon::lost;"),
 ("X13 the home slot is classified without asking whether the plane exists at all",
  "    if (!s.mobile_build) {", "    if (false) {"),
 ("X14 a blank home slot still prints `--` ('not applicable' rendered as 'never confirmed')",
  "        ui_pad_token(c.home_age, kAgeTokenCap, 0);",
  "        ui_fmt_home_age(c.home_age, kAgeTokenCap, false, 0);"),
 # --- §6's badge priority -------------------------------------------------------------------------------------------
 ("X15 UNSAVED outranks CONFLICT (the operator is not told their draft is stale)",
  "    if (conflict)         return CfgBadge::conflict;\n"
  "    if (unsaved)          return CfgBadge::unsaved;",
  "    if (unsaved)          return CfgBadge::unsaved;\n"
  "    if (conflict)         return CfgBadge::conflict;"),
 ("X16 RESTART-REQUIRED outranks UNSAVED (an unsaved draft hides behind a reboot notice)",
  "    if (unsaved)          return CfgBadge::unsaved;\n"
  "    if (restart_required) return CfgBadge::restart;",
  "    if (restart_required) return CfgBadge::restart;\n"
  "    if (unsaved)          return CfgBadge::unsaved;"),
 # --- §5.2/§5.3's navigation mapping ---------------------------------------------------------------------------------
 ("X17 the emergency exception is dropped (the rail is drawn over a 128-px headline)",
  "    if (emg != Emergency::idle) return NavSlot::none;", "    ;"),
 ("X18 the rail is always visible",
  "inline bool ui_rail_visible(Emergency emg) { return emg == Emergency::idle; }",
  "inline bool ui_rail_visible(Emergency emg) { (void)emg; return true; }"),
 ("X19 the compose modal does not claim the rail (a DM compose from TEAM reads TEAM)",
  "    switch (st.compose) {\n"
  "        case Compose::dm:\n"
  "        case Compose::channel: return NavSlot::send;\n"
  "        case Compose::none:    break;\n"
  "    }",
  "    ;"),
 ("X20 the send RESULT phase falls back to the screen underneath it",
  "        case Compose::channel: return NavSlot::send;",
  "        case Compose::channel: if (!st.compose_result) return NavSlot::send; break;"),
 ("X21 a body-replacing inbox modal loses its slot (precedence dropped)",
  "        case InboxModal::gone:   return NavSlot::inbox;",
  "        case InboxModal::gone:   break;"),
 ("X22 a non-team build still exposes the TEAM/SEND rail slots (misleading dead icons)",
  "        if (s.team_build) c.slots = uint8_t(c.slots | slot_bit(NavSlot::team) | slot_bit(NavSlot::send));",
  "        c.slots = uint8_t(c.slots | slot_bit(NavSlot::team) | slot_bit(NavSlot::send));"),
 ("X23 the rail fields are published while the rail is SUPPRESSED (visible equality stops being visible)",
  "    if (c.rail_visible) {", "    if (true) {"),
 # --- QG round 2: the two entries that would have caught round 1's own defects ----------------------------------------
 # ★★ X24 IS THE ROUND-1 DEFECT ITSELF, INSTALLED ON PURPOSE. The mapping tested `st.detail` BEFORE `st.compose`
 #    while `draw_frame` (src/firmware_ui.cpp:949-953) draws compose FIRST ⇒ with both open the renderer drew COMPOSE
 #    and the rail said INBOX. ⛔ It survived a full gate because the TEST PINNED THE WRONG ANSWER — the fifth
 #    instrument in this arc to enforce the defect it was written against. This entry is what makes the corrected
 #    order measured rather than merely written down.
 ("X24 the inbox-detail clause is tested BEFORE compose (the rail names a body the renderer is not drawing)",
  "    switch (st.compose) {\n"
  "        case Compose::dm:\n"
  "        case Compose::channel: return NavSlot::send;\n"
  "        case Compose::none:    break;\n"
  "    }",
  "    switch (st.detail) {\n"
  "        case InboxModal::body:\n"
  "        case InboxModal::gone:   return NavSlot::inbox;\n"
  "        case InboxModal::closed: break;\n"
  "    }\n"
  "    switch (st.compose) {\n"
  "        case Compose::dm:\n"
  "        case Compose::channel: return NavSlot::send;\n"
  "        case Compose::none:    break;\n"
  "    }"),
 # ★★ X25 IS ROUND 1'S BATTERY GUARD. It clamped an unrenderable reading to a PLAUSIBLE-LOOKING voltage instead of
 #    declaring it unavailable — the substitution the battery path forbids — and the clamped token was six characters
 #    wide against a 35-px slot that fits four.
 ("X25 an unrenderable battery reading is CLAMPED to a plausible voltage instead of `--`",
  "    c.batt_dv = (dv < 0 || dv > int32_t(kBattMaxDv)) ? int16_t(-1) : int16_t(dv);",
  "    c.batt_dv = (dv < 0) ? int16_t(-1) : (dv > int32_t(kBattMaxDv) ? kBattMaxDv : int16_t(dv));"),
 ("X26 the formatter's own width guard is dropped (it can emit a token wider than the frozen slot)",
  "    const bool renderable = decivolts >= 0 && decivolts <= kBattMaxDv;",
  "    const bool renderable = decivolts >= 0;"),
 # --- §CHROME-3 / §8.3.1: the repaint invalidation. Three wrong answers, and X27 is the WITHDRAWN INSTRUCTION -------
 # ⛔⛔ X27 IS §8.3.1's WITHDRAWN TEST, INSTALLED AS CODE. An earlier version of the amendment required a blanked
 #    chrome change to mark the model CLEAN; it was withdrawn because clearing a dirty bit while dark ERASES A
 #    LEGITIMATE PENDING REDRAW that §B107 exists to preserve. This entry is what stops the withdrawal being quietly
 #    re-adopted by a later reader who finds the tidier shape obvious. ⓘ Its structural twin is `probe_board_ui`'s W3,
 #    which forbids `src/firmware_ui.cpp` from naming `clear_dirty` at all.
 ("X27 the invalidation marks the model CLEAN when nothing changed (§8.3.1's WITHDRAWN instruction)",
  "    if (ui_chrome_equal(live, frozen)) return false;   // ⛔ NOTHING is cleared on this arm either",
  "    if (ui_chrome_equal(live, frozen)) { m.clear_dirty(); return false; }"),
 ("X28 the invalidation's sense is inverted (it raises when nothing moved, and misses what did)",
  "    if (ui_chrome_equal(live, frozen)) return false;   // ⛔ NOTHING is cleared on this arm either",
  "    if (!ui_chrome_equal(live, frozen)) return false;"),
 ("X29 nothing ever invalidates (the strip goes stale on a lit panel)",
  "    m.mark_dirty();\n    return true;", "    return true;"),
 # --- §CHROME-4: the rail's slot IDENTITY, which the renderer indexes by ---------------------------------------------
 # ⛔⛔ X30 TWO SLOTS SHARE ONE MASK BIT. `draw_rail` walks `NavSlot(i + 1)` and asks the mask whether to draw slot i,
 #    so a bit that is not one-per-slot makes an unavailable TEAM blank the INBOX icon as well — §3.2's "the remaining
 #    icons keep the same locations" broken in the one direction no team-enabled build can show. ⓘ The arithmetic
 #    still looks derived rather than hand-written, which is exactly why it needs a case that can see it.
 ("X30 the slot mask bit is not one-per-slot (two rail slots share a bit)",
  "    return (s == NavSlot::none) ? uint8_t(0) : uint8_t(1u << (uint8_t(s) - 1u));",
  "    return (s == NavSlot::none) ? uint8_t(0) : uint8_t(1u << (uint8_t(s) / 2u));"),
 # --- §UI-15 §7: the team-id fingerprint. ★ ALL THREE ARE THE SAME DEFECT CLASS — a token that still LOOKS like a
 #     fingerprint but is not the SHARED one, which is the only failure this helper exists to prevent. The plan
 #     rejected "six digits derived from the id" because independent implementations would disagree; these entries
 #     install three of the disagreements it admitted, so "they can never disagree" is measured rather than asserted.
 # ⛔ X31 IS THE MASK ITSELF, WIDENED TO THE WHOLE ID — the tempting "why throw a byte away?" fix. The high byte then
 #    leaks into the token: `0x12A1B2C3` draws `12A1B2` (truncated at the slot's capacity) instead of `A1B2C3`, and
 #    two ids that §7 says fingerprint IDENTICALLY stop doing so. ⓘ It is aimed at the CONSTANT rather than the
 #    snprintf line so that the two format-string entries below stay independently attributable.
 ("X31 the mask is widened to the whole id (the top byte leaks into the shared token)",
  "inline constexpr uint32_t    kTeamFpMask     = 0x00FFFFFFu;",
  "inline constexpr uint32_t    kTeamFpMask     = 0xFFFFFFFFu;"),
 # ⛔ X32 THE ZERO OF THE WIDTH DROPPED, WHICH IS THE SUBTLEST OF THE THREE: `%6lX` is still six columns wide, so the
 #    field still LINES UP on the panel — it is SPACE-padded, so `0x00000001` draws `     1`. Every id with a
 #    non-zero top nibble of the masked value passes it, which is most of them, and a hand-typed sample would miss it.
 ("X32 the width loses its ZERO (a low id renders SPACE-padded, `     1` for `000001`)",
  'const int n = snprintf(out, cap, "%06lX", (unsigned long)(team_id & kTeamFpMask));',
  'const int n = snprintf(out, cap, "%6lX", (unsigned long)(team_id & kTeamFpMask));'),
 # ⛔ X33 LOWERCASE. §7 says uppercase, and the reason is human: the inviter's panel and the joiner's candidate list
 #    must show the SAME string. `a1b2c3` beside `A1B2C3` reads as two different teams to the person the token exists
 #    for. ⓘ The `X`/`x` difference is one character in a format string — exactly the drift a shared definition ends.
 ("X33 the hex digits are LOWER case (the two ends of a join render the same team differently)",
  'const int n = snprintf(out, cap, "%06lX", (unsigned long)(team_id & kTeamFpMask));',
  'const int n = snprintf(out, cap, "%06lx", (unsigned long)(team_id & kTeamFpMask));'),
 # --- §UI-15 slice 5: the TWO id lines §3.6.3's screens draw. ★ X34/X35 are the SAME defect class as X31-X33 — a
 #     token that still LOOKS right and is not the shared/authority value — and X36/X37 are the WARNING LINE's own
 #     condition, which design §3.6.3 makes conditional on membership.
 ("X34 the FULL id loses its zero padding (a low id renders short, and it is the AUTHORITY value)",
  'const int n = snprintf(out, cap, "0x%08lX", (unsigned long)team_id);',
  'const int n = snprintf(out, cap, "0x%lX", (unsigned long)team_id);'),
 ("X35 the success screen's 'full id' is the MASKED one (the fingerprint drawn twice, no authority value)",
  'const int n = snprintf(out, cap, "0x%08lX", (unsigned long)team_id);',
  'const int n = snprintf(out, cap, "0x%08lX", (unsigned long)(team_id & kTeamFpMask));'),
 ("X36 a TEAMLESS node is warned that its membership will be replaced (a warning about nothing)",
  "    if (team_id == 0) { ui_pad_token(out, cap, 0u); return false; }",
  "    ;"),
 ("X37 the replacement warning names the team by a PRIVATE truncation of the shared token",
  '    const int n = snprintf(out, cap, "REPLACES %s", fp);',
  '    const int n = snprintf(out, cap, "REPLACES %s", fp + 1);'),
 # --- §CHROME-5, the duty gauge. ★★ THE TWO BOUNDARY FACTS ARE ATTACKED SEPARATELY (X38/X39/X40) because they are
 #     separate rulings: 100 % is its own picture, and `enabled == false` outranks every pct. A single entry that
 #     broke both would prove only that something is wrong. ⚠ X38 REPLACES the `blocked` answer rather than removing
 #     the guard, and that is MEASURED not stylistic: at exactly pct = 100 the fill map computes
 #     `fill_0 + 100*6/100` = the `blocked` enumerator by ARITHMETIC COINCIDENCE, so a mutant that merely widened the
 #     comparison would still return the right picture and the entry would report a FAIL having measured nothing.
 ("X38 100 % is drawn as the full gauge (the warning mark never appears — right about the level, silent about the "
  "consequence)",
  "    if (pct >= 100) return DutyGauge::blocked;",
  "    if (pct >= 100) return DutyGauge::fill_5;"),
 ("X39 the pct is tested BEFORE `enabled` (a node with NO duty limit reports itself duty-blocked at 100)",
  "    if (!enabled)   return DutyGauge::disabled;\n"
  "    if (pct >= 100) return DutyGauge::blocked;",
  "    if (pct >= 100) return DutyGauge::blocked;\n"
  "    if (!enabled)   return DutyGauge::disabled;"),
 ("X40 `enabled` is ignored, so an UNLIMITED node draws an empty gauge (`0 % used`, which it never measured)",
  "    if (!enabled)   return DutyGauge::disabled;",
  "    if (false)      return DutyGauge::disabled;"),
 ("X41 the step map divides by the gauge's ROW count, not its LEVEL count (the full gauge becomes unreachable)",
  "                             + unsigned(pct) * unsigned(icons::kDutyFillLevels) / 100u));",
  "                             + unsigned(pct) * unsigned(icons::kDutyGaugeRows) / 100u));"),
 ("X42 every step boundary begins one percent early (the off-by-one a re-derived table would agree with)",
  "                             + unsigned(pct) * unsigned(icons::kDutyFillLevels) / 100u));",
  "                             + (unsigned(pct) + 1u) * unsigned(icons::kDutyFillLevels) / 100u));"),
 ("X43 the gauge is dropped from the equality (a visible bucket change owes no repaint — the strip goes stale)",
  "        && a.duty            == b.duty\n"
  "        && a.badge           == b.badge",
  "        && a.badge           == b.badge"),
 ("X44 the projection hard-codes the gauge, so the slot always claims there is no duty limit",
  "    c.duty = ui_duty_bucket(s.duty_enabled, s.duty_pct);",
  "    c.duty = DutyGauge::disabled;"),
]

# ===== §CHROME-1 — src/firmware_ui_icons.h =========================================================================
# ★ §8.1's amendment defines the bitmap byte format IN THE DESIGN, "because leaving it to the board is how the same
#   asset renders MIRRORED or BIT-REVERSED on the V4 port". These four entries are that sentence made falsifiable:
#   two authoring errors (a mirror and an MSB-first byte), one stride error, and one collapsed badge state.
MUTS_ICONS = [
 ("I01 the SEND arrow is authored MIRRORED (send drawn as receive — an error that reads as a feature)",
  "inline constexpr uint8_t kIconSend[7] = { 0x01, 0x07, 0x1F, 0x7F, 0x1F, 0x07, 0x01 };",
  "inline constexpr uint8_t kIconSend[7] = { 0x40, 0x70, 0x7C, 0x7F, 0x7C, 0x70, 0x40 };"),
 ("I02 the KEY is authored MSB-first (each byte bit-reversed — the V4-port hazard §8.1 names)",
  "inline constexpr uint8_t kIconKey[7] = { 0x00, 0x06, 0x09, 0x79, 0x49, 0x06, 0x00 };",
  "inline constexpr uint8_t kIconKey[7] = { 0x00, 0x60, 0x90, 0x9E, 0x92, 0x60, 0x00 };"),
 ("I03 the battery is declared 8 px wide, so its rows stop being 2 bytes (a 7x14 smear)",
  "inline constexpr uint8_t kBatteryW = 11;", "inline constexpr uint8_t kBatteryW = 8;"),
 ("I04 two SETTINGS badge states share one picture (§6's priority becomes unobservable)",
  "inline constexpr uint8_t kIconSettingsUnsaved[7] = { 0x0E, 0x1F, 0x1B, 0x1F, 0x0E, 0x60, 0x60 };",
  "inline constexpr uint8_t kIconSettingsUnsaved[7] = { 0x0E, 0x1F, 0x1B, 0x1F, 0x0E, 0x00, 0x00 };"),
 # --- §UI-17 S6, the 24x24 STATUS mark. ★★ THE SAME THREE AUTHORING ERRORS ONE SIZE UP, and the size is the point:
 #     this is the FIRST 3-byte-stride asset in the tree, so a mirror, an MSB-first byte and a wrong width each
 #     produce a PICTURE rather than a compile error. ⛔ The asset is INTERIM (owner ruling 2026-08-22) — when the
 #     final artwork lands, RE-POINT these two tables at it; ⛔ never drop them, or the swap ships unmeasured.
 ("I05 the MARK is authored MIRRORED (the R comes first and both letters are reversed)",
  "inline constexpr uint8_t kMarkMeshRoute[72] = {\n"
  "    0x00, 0x00, 0x00,\n"
  "    0x00, 0x00, 0x00,\n"
  "    0x00, 0x00, 0x00,\n"
  "    0x03, 0xE6, 0x3F,\n"
  "    0x07, 0xE7, 0x7F,\n"
  "    0x8F, 0x67, 0x60,\n"
  "    0xDB, 0x66, 0x60,\n"
  "    0x73, 0x66, 0x60,\n"
  "    0x23, 0x66, 0x60,\n"
  "    0x03, 0x66, 0x60,\n"
  "    0x03, 0xE6, 0x7F,\n"
  "    0x03, 0xE6, 0x3F,\n"
  "    0x03, 0x66, 0x06,\n"
  "    0x03, 0x66, 0x0C,\n"
  "    0x03, 0x66, 0x0C,\n"
  "    0x03, 0x66, 0x18,\n"
  "    0x03, 0x66, 0x30,\n"
  "    0x03, 0x66, 0x60,\n"
  "    0x03, 0x66, 0xC0,\n"
  "    0x03, 0x66, 0xC0,\n"
  "    0x03, 0x66, 0xC0,\n"
  "    0x00, 0x00, 0x00,\n"
  "    0x00, 0x00, 0x00,\n"
  "    0x00, 0x00, 0x00,\n"
  "};",
  "inline constexpr uint8_t kMarkMeshRoute[72] = {\n"
  "    0x00, 0x00, 0x00,\n"
  "    0x00, 0x00, 0x00,\n"
  "    0x00, 0x00, 0x00,\n"
  "    0xFC, 0x67, 0xC0,\n"
  "    0xFE, 0xE7, 0xE0,\n"
  "    0x06, 0xE6, 0xF1,\n"
  "    0x06, 0x66, 0xDB,\n"
  "    0x06, 0x66, 0xCE,\n"
  "    0x06, 0x66, 0xC4,\n"
  "    0x06, 0x66, 0xC0,\n"
  "    0xFE, 0x67, 0xC0,\n"
  "    0xFC, 0x67, 0xC0,\n"
  "    0x60, 0x66, 0xC0,\n"
  "    0x30, 0x66, 0xC0,\n"
  "    0x30, 0x66, 0xC0,\n"
  "    0x18, 0x66, 0xC0,\n"
  "    0x0C, 0x66, 0xC0,\n"
  "    0x06, 0x66, 0xC0,\n"
  "    0x03, 0x66, 0xC0,\n"
  "    0x03, 0x66, 0xC0,\n"
  "    0x03, 0x66, 0xC0,\n"
  "    0x00, 0x00, 0x00,\n"
  "    0x00, 0x00, 0x00,\n"
  "    0x00, 0x00, 0x00,\n"
  "};"),
 ("I06 the MARK is authored MSB-first (every byte bit-reversed — the V4-port hazard §8.1 names, at stride 3)",
  "    0x03, 0xE6, 0x3F,\n"
  "    0x07, 0xE7, 0x7F,\n"
  "    0x8F, 0x67, 0x60,\n"
  "    0xDB, 0x66, 0x60,\n"
  "    0x73, 0x66, 0x60,\n"
  "    0x23, 0x66, 0x60,\n"
  "    0x03, 0x66, 0x60,\n"
  "    0x03, 0xE6, 0x7F,\n"
  "    0x03, 0xE6, 0x3F,\n"
  "    0x03, 0x66, 0x06,\n"
  "    0x03, 0x66, 0x0C,\n"
  "    0x03, 0x66, 0x0C,\n"
  "    0x03, 0x66, 0x18,\n"
  "    0x03, 0x66, 0x30,\n"
  "    0x03, 0x66, 0x60,\n"
  "    0x03, 0x66, 0xC0,\n"
  "    0x03, 0x66, 0xC0,\n"
  "    0x03, 0x66, 0xC0,",
  "    0xC0, 0x67, 0xFC,\n"
  "    0xE0, 0xE7, 0xFE,\n"
  "    0xF1, 0xE6, 0x06,\n"
  "    0xDB, 0x66, 0x06,\n"
  "    0xCE, 0x66, 0x06,\n"
  "    0xC4, 0x66, 0x06,\n"
  "    0xC0, 0x66, 0x06,\n"
  "    0xC0, 0x67, 0xFE,\n"
  "    0xC0, 0x67, 0xFC,\n"
  "    0xC0, 0x66, 0x60,\n"
  "    0xC0, 0x66, 0x30,\n"
  "    0xC0, 0x66, 0x30,\n"
  "    0xC0, 0x66, 0x18,\n"
  "    0xC0, 0x66, 0x0C,\n"
  "    0xC0, 0x66, 0x06,\n"
  "    0xC0, 0x66, 0x03,\n"
  "    0xC0, 0x66, 0x03,\n"
  "    0xC0, 0x66, 0x03,"),
 ("I07 the MARK is declared 8 px wide, so its rows stop being 3 bytes (I03's smear, one asset up)",
  "inline constexpr uint8_t kMarkW = 24;", "inline constexpr uint8_t kMarkW = 8;"),
 # --- §CHROME-5's duty gauge. ★★ THE ARTWORK IS PART OF THE CONTRACT HERE IN A WAY NO OTHER ASSET'S IS: the FILL
 #     LEVEL COUNT IS DERIVED FROM THE PICTURE (`kDutyGaugeRows`), so an authoring error does not merely look wrong —
 #     it makes a percentage range unreachable or turns the ramp upside down. I08-I11 are the four ways that happens.
 ("I08 the DISABLED gauge is authored as the EMPTY one (`no duty limit` collapses into `0 % used`)",
  "inline constexpr uint8_t kIconDutyDisabled[7] = { 0x7F, 0x61, 0x51, 0x49, 0x45, 0x43, 0x7F };",
  "inline constexpr uint8_t kIconDutyDisabled[7] = { 0x7F, 0x41, 0x41, 0x41, 0x41, 0x41, 0x7F };"),
 ("I09 the BLOCKED gauge loses its warning mark (100 % becomes indistinguishable from 99 %)",
  "inline constexpr uint8_t kIconDutyBlocked[7] = { 0x7F, 0x77, 0x77, 0x77, 0x7F, 0x77, 0x7F };",
  "inline constexpr uint8_t kIconDutyBlocked[7] = { 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F };"),
 ("I10 one fill level inks the wrong row count (two adjacent levels draw the same picture)",
  "    { 0x7F, 0x41, 0x41, 0x7F, 0x7F, 0x7F, 0x7F },",
  "    { 0x7F, 0x41, 0x41, 0x41, 0x7F, 0x7F, 0x7F },"),
 ("I11 a fill level is authored TOP-DOWN (the gauge drains as utilization rises)",
  "    { 0x7F, 0x41, 0x41, 0x41, 0x41, 0x7F, 0x7F },",
  "    { 0x7F, 0x7F, 0x41, 0x41, 0x41, 0x41, 0x7F },"),
]

MUTS_JOINPROFILES = [
 # --- the UNITS. ★ J02/J03/J04 are the three ways this record could quietly join the wrong carrier. -----------------
 ("J02 MHz -> Hz becomes MHz -> kHz (every stored frequency is 1000x low)",
  "inline constexpr uint32_t mhz_to_hz(double mhz) { return static_cast<uint32_t>(mhz * 1000000.0 + 0.5); }",
  "inline constexpr uint32_t mhz_to_hz(double mhz) { return static_cast<uint32_t>(mhz * 1000.0 + 0.5); }"),
 ("J03 ★ THE TEMPTING U1 'REUSE': MHz->Hz composed from the two EXISTING helpers (rounds TWICE: 869462500 -> 869463000)",
  "p.freq_hz    = mhz_to_hz(req.freq_mhz);",
  "p.freq_hz    = meshroute::protocol::khz_to_hz(meshroute::protocol::mhz_to_khz(req.freq_mhz));"),
 ("J04 the bandwidth is stored in kHz, not Hz (the units-swap mutation the brief names)",
  "p.bw_hz      = meshroute::protocol::khz_to_hz(req.bw_khz);",
  "p.bw_hz      = static_cast<uint32_t>(req.bw_khz);"),
 # --- the NaN boundary ([[B216]]) ----------------------------------------------------------------------------------
 ("J01 ★★ the non-finite guard is dropped, so a NaN reaches the INTEGRAL cast (UB) exactly as B216 lets it",
  "if (!std::isfinite(req.freq_mhz) || !std::isfinite(req.bw_khz)) return ProfileErr::not_finite;",
  ";"),
 ("J01b the non-finite guard runs AFTER validate_join, so a NaN is reported as an out-of-range number",
  "if (!std::isfinite(req.freq_mhz) || !std::isfinite(req.bw_khz)) return ProfileErr::not_finite;\n"
  "    // ⛔ ONE AUTHORITY, NEVER A SECOND RANGE TABLE (U1)",
  "// ⛔ ONE AUTHORITY, NEVER A SECOND RANGE TABLE (U1)"),
 # --- the absent/corrupt matrix. ⛔ Each cell that must NOT write gets its own entry. -------------------------------
 # ⓘ J05/J11's patterns were RE-POINTED 2026-08-19 when the guard became `join_read_unreadable` — ⛔ the entries were
 # NOT deleted for going vacuous, they were aimed at the line that now carries the rule (and they cover BOTH unreadable
 # states as a result). A vacuous entry silently measures nothing, which is the whole failure this runner reports.
 ("J05 ⛔⛔ `clear` BECOMES A BACKDOOR REPAIR: an unreadable store is reseeded and three unread slots are destroyed",
  "if (join_read_unreadable(st)) { r.err = profile_err_of_unreadable(st); return r; }   // ⛔⛔ NOT a repair",
  "if (join_read_unreadable(st)) mrnv::join_blob_init(cur);"),
 ("J11 `set` silently reseeds an UNREADABLE store (corruption becomes indistinguishable from a fresh device)",
  "if (join_read_unreadable(st)) { r.err = profile_err_of_unreadable(st); return r; }   // ⛔ 0 writes",
  "if (join_read_unreadable(st)) mrnv::join_blob_init(cur);"),
 ("J06 `clear` on an ABSENT store seeds a record (a write where the contract allows ZERO)",
  "if (st == mrnv::JoinRead::absent) { r.verdict = ProfileVerdict::empty; return r; }\n"
  "        mrnv::JoinBlob want = cur;\n"
  "        want.prof[slot1 - 1] = mrnv::JoinProfile{};",
  "if (st == mrnv::JoinRead::absent) mrnv::join_blob_init(cur);\n"
  "        mrnv::JoinBlob want = cur;\n"
  "        want.prof[slot1 - 1] = mrnv::JoinProfile{};"),
 ("J07 `reset confirm` on an ABSENT store writes an empty record (ZERO writes is the contract)",
  "if (st == mrnv::JoinRead::absent) { r.verdict = ProfileVerdict::empty; return r; }\n"
  "        mrnv::JoinBlob want{};",
  "if (st == mrnv::JoinRead::absent) mrnv::join_blob_init(cur);\n"
  "        mrnv::JoinBlob want{};"),
 ("J12 the CORRUPT reset goes through the byte compare, so a corrupt store whose bytes look empty is left corrupt",
  "if (st == mrnv::JoinRead::invalid) return commit_forced(want);",
  "if (st == mrnv::JoinRead::invalid) return commit(cur, want);"),
 ("J15 the ABSENT seed is dropped, so a partial read's GARBAGE is written back as the record",
  "if (st == mrnv::JoinRead::absent) mrnv::join_blob_init(cur);",
  ";"),
 # --- the write policy ---------------------------------------------------------------------------------------------
 ("J08 the byte-identical coalescing guard is dropped (every re-set writes flash)",
  "if (memcmp(&want, &cur, sizeof want) == 0) { r.verdict = ProfileVerdict::unchanged; return r; }",
  ";"),
 ("J13 `reset` no longer requires `confirm` (all four slots discarded on a typo)",
  "if (!confirmed) { r.err = ProfileErr::needs_confirm; return r; }",
  ";"),
 # --- the index and the name ---------------------------------------------------------------------------------------
 ("J09 the slot bound is off by one at the TOP end (slot 4 refuses)",
  "inline bool valid_profile_slot(long slot1) { return slot1 >= 1 && slot1 <= static_cast<long>(mrnv::kJoinProfiles); }",
  "inline bool valid_profile_slot(long slot1) { return slot1 >= 1 && slot1 < static_cast<long>(mrnv::kJoinProfiles); }"),
 ("J14 an over-long name is TRUNCATED instead of refused (a 'success that isn't')",
  "if (name_len > sizeof(mrnv::JoinProfile::name)) return ProfileErr::name_too_long;   // C2: refuse, ⛔ never truncate",
  ";"),
 ("J10 the slot is not zeroed before composition, so a shorter name leaves a stale tail (coalescing loses meaning)",
  "p = mrnv::JoinProfile{};\n    p.present    = 1;",
  "p.present    = 1;"),
 # --- the ORDER ----------------------------------------------------------------------------------------------------
 # --- the FOURTH store state (2026-08-19 correction). ⛔ Every one of these is a plausible "simplification". -------
 ("J17 ★★★ `reset confirm` REPAIRS a store it could not read — four possibly-intact profiles destroyed by a mount fault",
  "        if (st == mrnv::JoinRead::io_failed) { r.err = ProfileErr::store_io_failed; return r; }   // ⛔ 0 writes",
  "        ;"),
 ("J18 ★★★ `list` reports a STORAGE FAILURE as an empty store (the `NO PROFILES` lie, one layer up)",
  "        if (st == mrnv::JoinRead::absent) { r.verdict = ProfileVerdict::empty; return r; }\n"
  "        r.err = profile_err_of_unreadable(st);  // verdict stays `refused` (the default)",
  "        if (st != mrnv::JoinRead::invalid) { r.verdict = ProfileVerdict::empty; return r; }\n"
  "        r.err = profile_err_of_unreadable(st);  // verdict stays `refused` (the default)"),
 ("J19 the two unreadable answers are COLLAPSED, so a dead flash is told to type `reset confirm`",
  "    return st == mrnv::JoinRead::io_failed ? ProfileErr::store_io_failed : ProfileErr::store_invalid;",
  "    return ProfileErr::store_invalid;"),
 ("J20 `io_failed` stops counting as unreadable, so `set`/`clear` write onto a record they never read",
  "    return st == mrnv::JoinRead::invalid || st == mrnv::JoinRead::io_failed;",
  "    return st == mrnv::JoinRead::invalid;"),
 ("J16 validation runs AFTER the load, so a refused request still costs a flash READ",
  "const ProfileErr ve = validate_profile(req, name_len);\n"
  "        if (ve != ProfileErr::none) { r.err = ve; return r; }                          // ⛔ 0 loads, 0 writes\n"
  "\n"
  "        mrnv::JoinBlob cur{};\n"
  "        const mrnv::JoinRead st = _store.load(cur);",
  "mrnv::JoinBlob cur{};\n"
  "        const mrnv::JoinRead st = _store.load(cur);\n"
  "        const ProfileErr ve = validate_profile(req, name_len);\n"
  "        if (ve != ProfileErr::none) { r.err = ve; return r; }"),
]


# ===== §UI-15 slice 2 CORRECTIONS — src/device_nv.h (blockers 1 and 2) ==============================================
# ★★★ WHAT THESE MEASURE. Until 2026-08-19 the two live read arms folded "the BACKEND would not open" into the same
#     `-1` they use for "there is no such record", and `join_blob_state` mapped that to `absent` — so a filesystem
#     that would not mount announced NO PROFILES. And neither arm looked at the stored SIZE, so an over-length file
#     was accepted as a valid PREFIX. Both fixes are now in HOISTED sequences the native suite drives; these entries
#     are the proof that driving them MEASURES something. ⛔ Each is a TEMPTING WRONG FIX, not a deletion.
MUTS_DEVICENV = [
 # --- blocker 1: the storage failure ------------------------------------------------------------------------------
 ("N01 ★★★ THE DEFECT ITSELF: the mount result is discarded again, so a dead FS reads as a fresh device",
  "    if (!fs.mount())    { if (io) io->backend_failed = true; return kSlotAbsent; }",
  "    fs.mount();"),
 ("N02 the mount failure is recorded but the read continues (a half-fix: the fact is set, the early return dropped)",
  "    if (!fs.mount())    { if (io) io->backend_failed = true; return kSlotAbsent; }",
  "    if (!fs.mount() && io) io->backend_failed = true;"),
 ("N03 ★★ THE ESP32 CLASSIFIER IS INVERTED: a first boot is called a STORAGE FAILURE (the same lie, other way)",
  "    if (!nvs.open(ns)) { if (io && !nvs.ns_absent(ns)) io->backend_failed = true; return kSlotAbsent; }",
  "    if (!nvs.open(ns)) { if (io) io->backend_failed = true; return kSlotAbsent; }"),
 ("N04 the ESP32 classifier is dropped, so 'NVS would not open' is a fresh device again",
  "    if (!nvs.open(ns)) { if (io && !nvs.ns_absent(ns)) io->backend_failed = true; return kSlotAbsent; }",
  "    if (!nvs.open(ns)) return kSlotAbsent;"),
 ("N05 ★★ the backend fact is consulted AFTER the absent test — the ordering that loses it entirely",
  "    if (io.backend_failed) return JoinRead::io_failed;\n"
  "    // ★ AN OVER-LENGTH RECORD IS `invalid`, ⛔ never `ok`. `n` alone cannot see it: nRF52 reads `len` bytes out of a\n"
  "    //   longer file and returns EXACTLY `len`, so a valid PREFIX would pass every check below.\n"
  "    if (io.oversize) return JoinRead::invalid;\n"
  "    if (n == kSlotAbsent) return JoinRead::absent;",
  "    if (n == kSlotAbsent) return JoinRead::absent;\n"
  "    if (io.backend_failed) return JoinRead::io_failed;\n"
  "    if (io.oversize) return JoinRead::invalid;"),
 ("N06 a storage failure is folded into `invalid` (the tempting collapse — it prints the WRONG remedy)",
  "    if (io.backend_failed) return JoinRead::io_failed;",
  "    if (io.backend_failed) return JoinRead::invalid;"),
 # --- [[B218]] REOPENED (2026-08-19): the tri-state nRF52 lookup --------------------------------------------------
 # ★★★ `File::open() == false` carries FOUR facts and only LFS_ERR_NOENT is a fresh device; the fix classifies the
 #     adapter's raw `lfs_stat` rc IN THE TEMPLATE. Each entry is the collapse someone would make "to simplify".
 ("N13 ★★★ THE REOPENED DEFECT: ANY lookup error is absent again — a metadata error reads as a fresh device",
  "        if (rc != FsT::kFoundRc)  { io->backend_failed = true; return kSlotAbsent; }  // (1) metadata error — ⛔ the open is NOT attempted",
  "        if (rc != FsT::kFoundRc)  return kSlotAbsent;"),
 ("N14 ★★★ present-but-unopenable collapses to absent — four possibly-intact profiles read as NO PROFILES",
  "        if (!fs.open(path)) { io->backend_failed = true; return kSlotAbsent; }",
  "        if (!fs.open(path)) return kSlotAbsent;"),
 ("N15 ⛔ the lookup is issued for EVERY record ('compute first, gate later') — the four bool records pay a stat each",
  "    if (io) {\n        // ★ ONLY",
  "    { const int probe = fs.lookup(path); (void)probe; }   // the stat asked unconditionally\n    if (io) {\n        // ★ ONLY"),
 ("N07 ★★★ THE OVER-LENGTH CHECK IS DROPPED, so a longer file is accepted on its valid PREFIX",
  "    if (io && fs.size() > len) io->oversize = true;          // ★ a longer file would otherwise read as a valid PREFIX",
  ";"),
 ("N08 the size comparison is `>=`, so an EXACTLY-sized record is rejected too (the off-by-one wrong fix)",
  "    if (io && fs.size() > len) io->oversize = true;          // ★ a longer file would otherwise read as a valid PREFIX",
  "    if (io && fs.size() >= len) io->oversize = true;"),
 ("N09 the ESP32 over-length check is dropped",
  "    if (io && nvs.blob_len(key) > len) io->oversize = true;       // ★ the same PREFIX hazard, seen from the other arm",
  ";"),
 ("N10 an over-length record is accepted anyway (the fact is gathered and then ignored)",
  "    if (io.oversize) return JoinRead::invalid;",
  ";"),
 # --- the shared contract: the four OTHER records must not start paying for /mrjoin's extra facts ------------------
 ("N11 ⛔ the extra FS question is asked for EVERY record (⛔ /mrcfg, /mrid, /mrpeers, /mrfault must not move)",
  "    if (io && fs.size() > len) io->oversize = true;          // ★ a longer file would otherwise read as a valid PREFIX",
  "    const bool over = fs.size() > len;\n    if (io) io->oversize = over;"),
 ("N12 ⛔ the ESP32 nvs_open classification runs for every record, on every open failure",
  "    if (!nvs.open(ns)) { if (io && !nvs.ns_absent(ns)) io->backend_failed = true; return kSlotAbsent; }",
  "    if (!nvs.open(ns)) { const bool miss = nvs.ns_absent(ns); if (io && !miss) io->backend_failed = true; return kSlotAbsent; }"),
 # --- §UI-16 K1: the /mrteams read state, which repeats the SAME three traps one record over ------------------------
 ("N17 ★★ the keyring's backend-failure arm is ordered AFTER `absent`, so a dead flash reports NO STORED KEYS",
  "    if (io.backend_failed) return TeamKeyRead::io_failed;\n"
  "    if (io.oversize)       return TeamKeyRead::invalid;\n"
  "    if (n == kSlotAbsent)  return TeamKeyRead::absent;",
  "    if (n == kSlotAbsent)  return TeamKeyRead::absent;\n"
  "    if (io.backend_failed) return TeamKeyRead::io_failed;\n"
  "    if (io.oversize)       return TeamKeyRead::invalid;"),
 ("N18 an OVER-LENGTH keyring is accepted as a valid PREFIX (the nRF52 short-read hazard, one record over)",
  "    if (io.oversize)       return TeamKeyRead::invalid;",
  "    ;"),
 ("N19 ★ the keyring's version policy is RELAXED from equality to a range — an unknown layout is parsed as keys",
  "    return blob_valid_exact(b, n, kTeamKeyMagic, kTeamKeyVersion) ? TeamKeyRead::ok : TeamKeyRead::invalid;",
  "    return blob_valid_range(b, n, kTeamKeyMagic, 1, 0xFFFFu) ? TeamKeyRead::ok : TeamKeyRead::invalid;"),
 # ⓘ AND ONE MUTATION IS DELIBERATELY ABSENT: `b.count = 0;` in `team_key_blob_init` cannot be attacked — `b =
 #   TeamKeyBlob{}` one line above has already zeroed it, so deleting the assignment changes nothing a test can see.
 #   It is kept in the source for symmetry with `peers_blob_init` and is marked there as redundant-by-construction,
 #   ⛔ not left to look like coverage. (It was written as an entry, measured UNUSABLE, and removed.)
 ("N20 the keyring seed does not STAMP the version, so a fresh record is rejected by its own read policy",
  "    b.version = kTeamKeyVersion;",
  "    ;"),
]

# ===== §UI-16 K1 — src/firmware_team_keyring.h =====================================================================
# ★★★ WHAT THESE MEASURE: every clause of the OWNER'S RULED WRITE POLICY, attacked ON ITS OWN. The stakes are why the
#     battery exists — a team content key is UNRECOVERABLE (no seed derives it), so each of these mutations is a way
#     to lose one silently, and every one of them is a plausible simplification rather than a deletion. ⛔ P-15's
#     entry is the headline: "evict the oldest" is the idiom used EVERYWHERE ELSE in this tree, and here it destroys
#     a secret.
MUTS_TEAMKEYRING = [
 # --- the write policy -------------------------------------------------------------------------------------------
 ("T01 ★★★ P-15 BROKEN: a FULL keyring EVICTS THE OLDEST record — the tree's own idiom, destroying a secret",
  "            if (cur.count >= mrnv::kTeamKeyRecs) { r.err = KeyringErr::keyring_full; return r; }\n"
  "            cur.rec[cur.count++] = want;",
  "            if (cur.count >= mrnv::kTeamKeyRecs) {\n"
  "                for (uint8_t i = 0; i + 1u < mrnv::kTeamKeyRecs; ++i) cur.rec[i] = cur.rec[i + 1];\n"
  "                cur.rec[mrnv::kTeamKeyRecs - 1] = want;\n"
  "            } else cur.rec[cur.count++] = want;"),
 ("T02 ★ the byte-identical coalescing guard is dropped — every re-grant of the SAME key writes flash",
  "            if (memcmp(&want, &cur.rec[idx], sizeof want) == 0) {\n"
  "                r.verdict = KeyringVerdict::unchanged;                     // ★ ZERO writes\n"
  "                return r;\n"
  "            }",
  ";"),
 ("T03 the compare covers only the KEY, not the whole record (a moved id or a dirty `reserved` reads as unchanged)",
  "            if (memcmp(&want, &cur.rec[idx], sizeof want) == 0) {",
  "            if (memcmp(want.team_ch_priv, cur.rec[idx].team_ch_priv, 32) == 0) {"),
 ("T04 ★★ `team_id == 0` IS STORED — a zero-keyed row that matches every teamless node's binding",
  "        if (team_id == 0) { r.err = KeyringErr::zero_team; return r; }     // ⛔ 0 loads, 0 writes",
  ";"),
 ("T05 ★★ ONE RECORD PER TEAM BROKEN: a re-key APPENDS a second row for the same team_id",
  "        const int idx = team_key_find(cur, team_id);\n"
  "        if (idx >= 0) {",
  "        const int idx = -1;\n"
  "        if (idx >= 0) {"),
 ("T06 the composition path no longer zeroes the record first — a stale tail defeats the coalescing compare",
  "    r = mrnv::TeamKeyRecord{};\n    r.team_id = team_id;",
  "    r.team_id = team_id;"),
 ("T07 an UNREADABLE store is silently RESEEDED — up to four intact keys destroyed by a transient mount fault",
  "        if (team_key_read_unreadable(st)) { r.err = keyring_err_of_unreadable(st); return r; }   // ⛔ 0 writes",
  "        if (team_key_read_unreadable(st)) mrnv::team_key_blob_init(cur);"),
 ("T08 the two unreadable answers are COLLAPSED, so a dead store and a corrupt record read alike",
  "    return st == mrnv::TeamKeyRead::io_failed ? KeyringErr::store_io_failed : KeyringErr::store_invalid;",
  "    return KeyringErr::store_invalid;"),
 ("T09 `io_failed` stops counting as unreadable, so `put` writes onto a record it never read",
  "    return st == mrnv::TeamKeyRead::invalid || st == mrnv::TeamKeyRead::io_failed;",
  "    return st == mrnv::TeamKeyRead::invalid;"),
 ("T10 the ABSENT seed is dropped, so a partial read's GARBAGE is written back as the keyring",
  "        if (st == mrnv::TeamKeyRead::absent) mrnv::team_key_blob_init(cur);",
  ";"),
 ("T11 a failed save is reported as a success (the 'success that isn't', over a key nothing stored)",
  "        if (!_store.save(cur)) { r.verdict = KeyringVerdict::nv_failed; r.err = KeyringErr::nv_save_failed; return r; }",
  "        _store.save(cur);"),
 # --- the SECRET WIPE. ★ It is measurable at all only because the guard is a NAMED type (see the header). ----------
 # ⛔ AND ONE MUTATION IS DELIBERATELY ABSENT, STATED RATHER THAN LEFT AS A GAP: `crypto_wipe` -> `memset` cannot be
 #    an entry. It zeroes the bytes just as well, so the SUITE CANNOT TELL THEM APART — the difference is a
 #    compiler's licence to ELIDE a dead store, which no host assertion can observe. It was written, measured
 #    UNUSABLE, and removed; the rule lives in the header's comment instead. (Adding it back would report a green
 #    battery entry that measures nothing, which is the [[B217]] family.)
 ("T12 ★★ THE SECRET BUFFER IS LEFT UNWIPED — the transient key material outlives the call",
  "    ~SecretWipeGuard() { crypto_wipe(&ref, sizeof(T)); }",
  "    ~SecretWipeGuard() { }"),
 # --- the BOOT RESTORE: ★★★ ONE MUTATION PER TERM OF THE FIVE-TERM EXACT MATCH (QG, 2026-08-22) -------------------
 # The four-term-correlation precedent: a predicate whose terms are only ever attacked TOGETHER is a predicate whose
 # terms are not measured. Each entry below breaks EXACTLY ONE term, and the case that reddens it breaks the same one.
 ("T14 ★★★ TERM (i) — P-2b BROKEN: the ACTIVE flag is dropped, so mere knowledge of the team id reactivates the key",
  "        if (!bind.key_active || bind.binding_team_id == 0) return refuse(live, KeyringRestore::no_binding);  // ⛔ 0 loads",
  "        if (bind.binding_team_id == 0) return refuse(live, KeyringRestore::no_binding);"),
 ("T22 ★★★ TERM (ii) — MEMBERSHIP DROPPED: a stale binding installs ANOTHER team's key on this node",
  "        if (bind.membership_team_id != bind.binding_team_id) return refuse(live, KeyringRestore::team_mismatch);",
  ";"),
 ("T15 ★★ TERM (iii) — the restore falls back to the FIRST record when the bound team has none",
  "        const int idx = team_key_find(cur, bind.binding_team_id);\n"
  "        if (idx < 0) return refuse(live, KeyringRestore::no_record);",
  "        int idx = team_key_find(cur, bind.binding_team_id);\n"
  "        if (idx < 0) idx = 0;"),
 ("T23 ★★★ TERM (iv) — THE COMMITTED WITNESS DROPPED: a FAILED re-key becomes effective at the next boot",
  "        if (!bind.committed_present || !bind.committed_pub\n"
  "            || memcmp(cur.rec[idx].team_ch_pub, bind.committed_pub, 32) != 0)\n"
  "            return refuse(live, KeyringRestore::not_committed);",
  ";"),
 ("T25 TERM (iv) HALF-APPLIED: the witness is consulted only when /mrcfg happens to carry one",
  "        if (!bind.committed_present || !bind.committed_pub\n"
  "            || memcmp(cur.rec[idx].team_ch_pub, bind.committed_pub, 32) != 0)\n"
  "            return refuse(live, KeyringRestore::not_committed);",
  "        if (bind.committed_present && bind.committed_pub\n"
  "            && memcmp(cur.rec[idx].team_ch_pub, bind.committed_pub, 32) != 0)\n"
  "            return refuse(live, KeyringRestore::not_committed);"),
 ("T16 ★★ TERM (v) — THE CORRUPTION CHECK IS IGNORED: a record whose pub does not verify is reported as installed",
  "        if (!live.adopt_key(cur.rec[idx].team_ch_pub, cur.rec[idx].team_ch_priv))\n"
  "            return refuse(live, KeyringRestore::rejected);",
  "        live.adopt_key(cur.rec[idx].team_ch_pub, cur.rec[idx].team_ch_priv);"),
 # --- the GOVERNANCE. ★ QG blocker 1: a verdict that is reported but not APPLIED left the old key live. -----------
 ("T24 ★★★ THE VERDICT IS REPORTED BUT NOT APPLIED — a key installed by an earlier boot step SURVIVES a refusal",
  "    static KeyringRestore refuse(ITeamKeyLive& live, KeyringRestore why) {\n"
  "        live.clear_key();\n"
  "        return why;\n"
  "    }",
  "    static KeyringRestore refuse(ITeamKeyLive& live, KeyringRestore why) {\n"
  "        (void)live;\n"
  "        return why;\n"
  "    }"),
 ("T26 the governance is HALF-APPLIED: the store arms clear, the BINDING arms do not (the stale-binding hole)",
  "        if (!bind.key_active || bind.binding_team_id == 0) return refuse(live, KeyringRestore::no_binding);  // ⛔ 0 loads",
  "        if (!bind.key_active || bind.binding_team_id == 0) return KeyringRestore::no_binding;"),
 ("T17 an UNREADABLE store is reported as 'no key stored' (the honesty this record's four states exist for)",
  "        if (st != mrnv::TeamKeyRead::ok)     return refuse(live, KeyringRestore::store_failed);   // invalid / io_failed",
  "        if (st != mrnv::TeamKeyRead::ok)     return refuse(live, KeyringRestore::no_record);"),
 ("T18 the restore WRITES (a 'repair' on the read path — flash wear, and a corrupt store overwritten unread)",
  "        if (st == mrnv::TeamKeyRead::absent) return refuse(live, KeyringRestore::no_record);",
  "        if (st == mrnv::TeamKeyRead::absent) { mrnv::team_key_blob_init(cur); _store.save(cur); return refuse(live, KeyringRestore::no_record); }"),
 # --- the lookup -------------------------------------------------------------------------------------------------
 ("T19 `team_key_find` answers for team 0, so a bit-rotted zero row can satisfy a binding",
  "    if (team_id == 0) return -1;                      // ⛔ 0 is never stored, so it can never be FOUND either",
  ";"),
 ("T20 a bit-rotted count is PERSISTED FORWARD instead of repaired by the write that follows it",
  "        team_key_clamp_count(cur);\n"
  "\n"
  "        mrnv::TeamKeyRecord want{};",
  "        mrnv::TeamKeyRecord want{};"),
 ("T21 the ruled `KEYRING FULL` lexeme is re-spelled (a string declared once, changed in one place — or not)",
  'inline constexpr const char* kKeyringFullText = "KEYRING FULL";',
  'inline constexpr const char* kKeyringFullText = "KEYRING IS FULL";'),
]

# ===== §UI-16 N1 — lib/core/team_seen_ring.h: THE PURE RING POLICY ==================================================
# ★★ Every entry below is one of the owner's 2026-08-22 rulings turned into its TEMPTING WRONG FIX. The ring is the
#    half of N1 that decides things — what counts as the same team, how signal is smoothed, what "recently" means, and
#    what order the operator sees — so it is the half a reviewer's reflex would "improve".
# ⚠ THE WINDOW ENTRIES (S06/S07) ARE ANCHORED WHERE THE WINDOW IS **APPLIED**, NOT AT THE LITERAL IN
#   `protocol_constants.h`, and that placement is deliberate rather than a compromise: a battery is per-source-file,
#   the applied window is what any reader actually experiences, and the LITERAL is separately guarded by a committed
#   native case that pins it AT ITS DERIVATION (`team_seen_retain_ms == 2 x NodeConfig{}.team_beacon_period_ms ==
#   600000`) — so the number and the rule it comes from cannot drift apart unnoticed either.
# ===== §UI-16 N2 — src/firmware_ui_nearby.h: THE OWN-TEAM FILTER, THE ORDER AND THE ROWS ============================
# ★★★ THE FIRST TWO ARE THE SLICE'S STRUCTURAL HEADLINE (spec §4-N2 pins 3 and 6 / owner rulings R-5): the filter is
#     the READER's by ruling — N1 records our own team like any other, deliberately — and the order is the ring's own
#     first-observed one, which is what stops a row moving under the operator's cursor.
MUTS_UINEARBY = [
 ("Y01 ★★★ the OWN-TEAM filter is DROPPED — the team we are already in is offered as a candidate",
  "        if (src[i].team_id == own_team_id) continue;       // ★ the team we are ALREADY in is not a candidate",
  "        // (filter removed)"),
 ("Y02 ★★ the own-team filter is INVERTED — the ONLY team offered is the one we cannot join",
  "        if (src[i].team_id == own_team_id) continue;       // ★ the team we are ALREADY in is not a candidate",
  "        if (src[i].team_id != own_team_id) continue;"),
 ("Y03 ★★ the filter compares the DISPLAY FINGERPRINT instead of the full id — a different team with the same low "
  "24 bits silently vanishes from the list",
  "        if (src[i].team_id == own_team_id) continue;       // ★ the team we are ALREADY in is not a candidate",
  "        if ((src[i].team_id & 0x00FFFFFFu) == (own_team_id & 0x00FFFFFFu)) continue;"),
 ("Y04 ★★★ the captured list is SORTED BY SIGNAL ('show the best first') — the ruled first-observed order is gone "
  "and the list re-orders under the cursor",
  "    return out;\n"
  "}\n"
  "\n"
  "// ------------------------------------------------------------------------- the rows, AS IDENTITIES",
  "    for (uint8_t i = 0; i + 1 < out.n; ++i)\n"
  "        for (uint8_t j = uint8_t(i + 1); j < out.n; ++j)\n"
  "            if (out.row[j].snr_q4 > out.row[i].snr_q4) { const NearbyRow sw = out.row[i]; out.row[i] = out.row[j]; out.row[j] = sw; }\n"
  "    return out;\n"
  "}\n"
  "\n"
  "// ------------------------------------------------------------------------- the rows, AS IDENTITIES"),
 ("Y05 the capture's BOUND is dropped — a publisher that overran would write past the fixed array",
  "    if (n > kMaxNearbyRows) n = kMaxNearbyRows;            // the publisher's bound, restated where the copy happens",
  "    // (bound removed)"),
 ("Y06 a null source is answered with a FULL-LOOKING list instead of failing closed",
  "    if (!src) return out;                                  // ⛔ FAILS CLOSED: no source is an EMPTY list, never a guess",
  "    if (!src) { out.n = n; return out; }"),
 ("Y07 ★★ BACK becomes CONDITIONAL — an empty scan offers no way out at all",
  "    out.row[out.n].back = true;\n"
  "    ++out.n;",
  "    if (l.n) { out.row[out.n].back = true; ++out.n; }"),
 ("Y08 ★ BACK is put FIRST instead of last — the row one press from the top is now the exit, and the last row is a "
  "team (the shape §B66 forbids reasoning about positionally)",
  "    for (uint8_t i = 0; i < l.n && i < kMaxNearbyRows; ++i) {\n"
  "        out.row[out.n].team = l.row[i];\n"
  "        out.row[out.n].back = false;\n"
  "        ++out.n;\n"
  "    }\n"
  "    out.row[out.n].back = true;\n"
  "    ++out.n;",
  "    out.row[out.n].back = true;\n"
  "    ++out.n;\n"
  "    for (uint8_t i = 0; i < l.n && i < kMaxNearbyRows; ++i) {\n"
  "        out.row[out.n].team = l.row[i];\n"
  "        out.row[out.n].back = false;\n"
  "        ++out.n;\n"
  "    }"),
 ("Y09 the row list stops FAILING CLOSED past its end — an out-of-range cursor is handed a plausible row",
  "    bool at(uint8_t i, NearbySelRow& out) const { if (i >= n) return false; out = row[i]; return true; }",
  "    bool at(uint8_t i, NearbySelRow& out) const { out = row[(i < n) ? i : 0]; return true; }"),
 ("Y10 the empty-state note is INVERTED — a list WITH teams says NO TEAMS NEARBY",
  "inline const char* nearby_note(const NearbyList& l) { return l.n == 0 ? kNearbyEmpty : \"\"; }",
  "inline const char* nearby_note(const NearbyList& l) { return l.n == 0 ? \"\" : kNearbyEmpty; }"),
 ("Y11 F-1's honest second line is dropped to a copy of the first (the panel stops naming the LEAF gate)",
  'inline constexpr const char* kNearbyLeafLine = "SAME RADIO + LEAF";  // spec S-4 — F-1\'s second line',
  'inline constexpr const char* kNearbyLeafLine = "CURRENT PHY ONLY";'),
]

# ===== §UI-16 N2 — src/firmware_ui_nearby_row.h: THE TIER MAP AND THE ROW =========================================
# ★★★ Z01 IS THE NAMED FORBIDDEN SHAPE (owner ruling R-4): a SECOND definition of signal quality. It is written as a
#     local threshold ladder that AGREES WITH THE TIERS TODAY at two of the four boundaries and disagrees at the
#     others — which is exactly how such a fork ships and exactly why the boundaries are driven individually.
# ★★ Z02 IS U1's: the six-hex fingerprint re-spelled locally instead of calling the ONE shared helper. It also agrees
#    today (`%06lX` of the same mask), so only the VALUE RELATION in the native case and the probe's exact bytes can
#    tell them apart — a lowercase `x` is the version of it that ships.
MUTS_UINEARBYROW = [
 ("Z01 ★★★ the tier is RE-DERIVED from raw q4 thresholds instead of calling presence_quality_tier — the SECOND "
  "definition of signal quality the ruling forbids",
  "    const unsigned tier = MESHROUTE_NS::protocol::presence_quality_tier(snr_q4);",
  "    const unsigned tier = (snr_q4 >= 0) ? 3u : (snr_q4 >= -96) ? 2u : (snr_q4 >= -224) ? 1u : 0u;"),
 ("Z02 ★★ the fingerprint is re-spelled locally instead of calling the ONE shared helper (U1)",
  "    char fp[kTeamFpTokenCap];   ui_fmt_team_fingerprint(fp, sizeof fp, r.team_id);",
  "    char fp[kTeamFpTokenCap];   snprintf(fp, sizeof fp, \"%06lx\", (unsigned long)(r.team_id & 0x00FFFFFFu));"),
 ("Z03 ★ the fingerprint is drawn from the WHOLE 32-bit id — a second, wider token beside the shared six-hex one",
  "    char fp[kTeamFpTokenCap];   ui_fmt_team_fingerprint(fp, sizeof fp, r.team_id);",
  "    char fp[kTeamFpTokenCap];   snprintf(fp, sizeof fp, \"%08lX\", (unsigned long)r.team_id);"),
 ("Z04 ★★ an UNDATEABLE observation is rendered as `0s` instead of `--` — an age the surface cannot know reads as "
  "'just now' (the fabricated-freshness class)",
  "    char age[kAgeTokenCap];     ui_fmt_home_age(age, sizeof age, r.age_valid, r.age_ms);",
  "    char age[kAgeTokenCap];     ui_fmt_home_age(age, sizeof age, true, r.age_ms);"),
 ("Z05 the 64-bit age is CAST to 32 bits on the way into the one bucketing (the ~49.7-day wrap, re-created)",
  "    char age[kAgeTokenCap];     ui_fmt_home_age(age, sizeof age, r.age_valid, r.age_ms);",
  "    char age[kAgeTokenCap];     ui_fmt_home_age(age, sizeof age, r.age_valid, uint32_t(r.age_ms));"),
 ("Z06 the row's three tokens are drawn in the WRONG ORDER (age where the signal belongs)",
  '    const int n = snprintf(out, cap, "%s %s %s", fp, sig, age);',
  '    const int n = snprintf(out, cap, "%s %s %s", fp, age, sig);'),
 ("Z07 the tier DENOMINATOR is hardcoded — a token that names a scale the tier map no longer has",
  '    const int n = snprintf(out, cap, "%u/%u", tier, unsigned(kNearbyTierMax));',
  '    const int n = snprintf(out, cap, "%u/9", tier);'),
 # ===== §UI-16 N3 — the confirmation's title (S-8), which lives here for the same include-order reason ==========
 # ★ Z08 IS Z02's TWIN ON THE NEW TOKEN, and it needs its own entry for the reason every twin in this file does: a
 #   control that reddens the ROW says nothing about the TITLE, and the two are what the operator compares when the
 #   confirmation opens over the list. A locally re-spelled fingerprint agrees today and drifts the day the shared
 #   helper's width or case is ruled again.
 ("Z08 ★★ the join title re-spells the fingerprint locally instead of calling the ONE shared helper (U1)",
  "    char fp[kTeamFpTokenCap]; ui_fmt_team_fingerprint(fp, sizeof fp, team_id);",
  "    char fp[kTeamFpTokenCap]; snprintf(fp, sizeof fp, \"%06lx\", (unsigned long)(team_id & 0x00FFFFFFu));"),
 ("Z09 the join title asks about NO team at all (the id is dropped from the question)",
  '    const int n = snprintf(out, cap, "JOIN %s?", fp);',
  '    const int n = snprintf(out, cap, "JOIN?");'),
]

# ===== §UI-16 N4 — src/firmware_ui_invite.h: THE TWO AUTHORITIES, THE HANDLED SET AND THE NAME LIFECYCLE =========
# ★★★ I01 AND I02 ARE THE CORRECTION'S OWN CONTROLS (F-11), AND THEY MUST BE SEPARATE ENTRIES: the draft's
#     single-authority diff is exactly "I01 applied", and the two authorities cover DIFFERENT cases — drop (b) and
#     the route-only member is announced as NEW, drop (a) and the re-DAD'd one is. An entry that dropped both would
#     redden and prove only that the pair matters.
# ★★ I07-I09 ARE THE NAME LIFECYCLE (F-15 rules 2-3): the name is an ADDED COLUMN, from ONE source, CLAMPED. Each
#    of the three ways to get it wrong is a separate, plausible edit — and I08 is the one that looks most like a
#    fix, because `label_from_hash` is the resolver the TEAM screen next door really does use.
MUTS_UIINVITE = [
 ("I01 ★★★ AUTHORITY (b) IS DROPPED — a route-only member whose hash turns authoritative mid-window is announced "
  "as NEW MEMBER although it was present all along (F-11's own defect)",
  "    if (invite_snap_has_id(w, m.id)) return false;",
  "    // (authority (b) dropped)"),
 ("I02 ★★★ AUTHORITY (a) IS DROPPED — a member that re-ran team-DAD (same hash, new id) is announced as NEW",
  "    if (invite_snap_has_hash(w, m.key_hash32)) return false;",
  "    // (authority (a) dropped)"),
 ("I03 ★★ the AUTHORITATIVE floor is lowered — a member with NO key binding is listed as grantable, with an "
  "invented all-zero fingerprint (F-7 / C2)",
  "    if (m.key_hash32 == 0) return false;",
  "    // (floor lowered)"),
 ("I04 ★★ the candidate list is produced WHILE THE WINDOW IS CLOSED — the `taken` guard is ignored, so a state "
  "with no snapshot treats every member as new (P-12)",
  "    if (!w.taken) return false;",
  "    // (the snapshot's existence is no longer consulted)"),
 ("I05 ★★ `NEW MEMBER` is re-spelled as the design's FORBIDDEN word (S-33)",
  'inline constexpr const char* kInviteNew    = "NEW MEMBER";      // S-14 — the design\'s own word (§3.6.4 :815)',
  'inline constexpr const char* kInviteNew    = "KEYLESS";'),
 ("I06 ★★★ THE HANDLED SET IS DROPPED — a REJECTed candidate comes back on the very next refresh (F-13's "
  "self-contradiction, restored)",
  "            if (invite_handled_has(w, mem[i].key_hash32)) continue;",
  "            // (the handled set is no longer consulted)"),
 ("I07 ★★ the cached name REPLACES the member fingerprint instead of filling the column beside it — the identity "
  "aid vanishes the moment a name arrives (F-15 rule 2)",
  '    snprintf(out, cap, "%c%-6.6s T%-3u %6s", marker, m.name, unsigned(m.id), fp);',
  '    snprintf(out, cap, "%c%-6.6s T%-3u %6s", marker, "", unsigned(m.id), m.name[0] ? m.name : fp);'),
 ("I08 ★★ the name column falls back to `label_from_hash`'s `0x` spelling when no name is cached — the TRUNCATED "
  "`0x` third spelling of the hash, in six columns (F-15's named refusal)",
  '    snprintf(out, cap, "%c%-6.6s T%-3u %6s", marker, m.name, unsigned(m.id), fp);',
  '    char nm[12]; snprintf(nm, sizeof nm, "0x%08lx", (unsigned long)m.key_hash32);\n'
  '    snprintf(out, cap, "%c%-6.6s T%-3u %6s", marker, m.name[0] ? m.name : nm, unsigned(m.id), fp);'),
 ("I09 ★ the name is rendered WITHOUT the six-column clamp — a long name pushes the id and the fingerprint off "
  "the 19-column row",
  '    snprintf(out, cap, "%c%-6.6s T%-3u %6s", marker, m.name, unsigned(m.id), fp);',
  '    snprintf(out, cap, "%c%s T%-3u %6s", marker, m.name, unsigned(m.id), fp);'),
 ("I10 ★★★ THE CONFIRMATION DROPS THE FULL HASH — the irreversible act's screen is downgraded to the six-column "
  "selection aid, which 255 other peers answer to (P-7c / [[B48]])",
  '    snprintf(out, cap, "0x%08lX", (unsigned long)key_hash32);',
  '    snprintf(out, cap, "%06lX", (unsigned long)(key_hash32 & kMemberFpMask));'),
 # ★★★ I11 IS THE FAIL-**OPEN** SNAPSHOT, AND IT IS A DIFFERENT DEFECT FROM I04 — which is why it is its own entry.
 #     I04 ignores an honestly-absent snapshot at the READ; I11 manufactures a snapshot at the WRITE, recording a
 #     TAKEN-but-EMPTY window for a publisher that claimed `n` members and handed over no source. The read then
 #     answers *new* for EVERY member observed, because the authorities really are empty — a whole team announced as
 #     candidates on a caller's bug, wearing a comment that says "fails closed". ⇒ the refusal needs its own control.
 ("I11 ★★★ THE NULL SOURCE FAILS **OPEN** — `(nullptr, n > 0)` is recorded as a TAKEN, EMPTY snapshot, so every "
  "member the window then observes is announced as NEW MEMBER (C2, inverted)",
  "    if (!mem && n != 0) return w;          // ⛔ REFUSED: members claimed, no source — `taken` stays false\n"
  "    w.taken = true;",
  "    w.taken = true;"),
]

MUTS_TEAMSEEN = [
 ("S01 ★★ the de-dup key becomes the SENDER, not the TEAM — one team with four advertisers fills the ring",
  "        if (ring[i].team_id != team_id) continue;",
  "        if (ring[i].src_id != src_id) continue;"),
 ("S02 ★★★ the SNR EWMA replaced by MAX-SEEN — the ruling's named refusal: it latches the best moment and never decays",
  "        ring[i].snr_q4  = protocol::snr_ewma_update(ring[i].snr_q4, sample_q4);",
  "        ring[i].snr_q4  = (sample_q4 > ring[i].snr_q4) ? sample_q4 : ring[i].snr_q4;"),
 ("S03 ★★ the SNR EWMA replaced by the RAW LAST SAMPLE — no smoothing at all, so one deep fade reads as the link",
  "        ring[i].snr_q4  = protocol::snr_ewma_update(ring[i].snr_q4, sample_q4);",
  "        ring[i].snr_q4  = sample_q4;"),
 ("S04 ★★ a REFRESH MOVES THE ENTRY TO THE END — first-observed order stops being structural and the list re-orders "
  "under the operator's cursor",
  "        ring[i].snr_q4  = protocol::snr_ewma_update(ring[i].snr_q4, sample_q4);\n"
  "        ring[i].last_ms = now_ms;\n"
  "        ring[i].src_id  = src_id;\n"
  "        return;",
  "        const TeamSeen moved{ now_ms, team_id, protocol::snr_ewma_update(ring[i].snr_q4, sample_q4), src_id, 0 };\n"
  "        for (uint8_t k = i; k + 1 < n; ++k) ring[k] = ring[k + 1];\n"
  "        ring[n - 1] = moved;\n"
  "        return;"),
 ("S05 ★★ OVERFLOW EVICTS THE NEWEST instead of the stalest — the team that just walked into range is the one dropped",
  "    for (uint8_t i = 1; i < n; ++i) if (ring[i].last_ms < ring[o].last_ms) o = i;",
  "    for (uint8_t i = 1; i < n; ++i) if (ring[i].last_ms > ring[o].last_ms) o = i;"),
 ("S06 the retention boundary loses its INCLUSIVE form (>= becomes >), so an entry exactly one window old vanishes "
  "— the boundary recent_ring.h unified for the whole tree",
  "    const uint64_t cutoff = recent_ring_cutoff(now_ms, retain_ms);\n"
  "    uint8_t live = 0;\n"
  "    for (uint8_t i = 0; i < n; ++i) if (ring[i].last_ms >= cutoff) ++live;",
  "    const uint64_t cutoff = recent_ring_cutoff(now_ms, retain_ms);\n"
  "    uint8_t live = 0;\n"
  "    for (uint8_t i = 0; i < n; ++i) if (ring[i].last_ms > cutoff) ++live;"),
 ("S07 ★★ THE WINDOW ITSELF IS HALVED at the read (600000 -> a hardcoded 300000) — 'one beacon period is surely "
  "enough', which is exactly the reasoning the two-periods ruling refuses",
  "    const uint64_t cutoff = recent_ring_cutoff(now_ms, retain_ms);\n"
  "    uint8_t live = 0;\n"
  "    for (uint8_t i = 0; i < n; ++i) if (ring[i].last_ms >= cutoff) ++live;",
  "    const uint64_t cutoff = recent_ring_cutoff(now_ms, 300000u);\n"
  "    uint8_t live = 0;\n"
  "    for (uint8_t i = 0; i < n; ++i) if (ring[i].last_ms >= cutoff) ++live;"),
]

# ===== §UI-16 N1 — lib/core/node_beacon.cpp: THE ONE WRITE SITE =====================================================
# ★★★ THE FIRST THREE ARE THE SLICE'S HEADLINE CONTROLS (spec §3 P-3), and they are written as the "while we're here"
#     shape rather than as a random extra call, because that IS the defect: the advertiser's id, key hash and SNR are
#     all in scope at the observation site, so caching them "since we already have them" is a one-line edit a reviewer
#     would wave through — and it would turn a PASSIVE SCAN into an ingest that binds identities and learns routes
#     from a team we are not in.
# ⚠ W06 IS THE GATE FALLING THROUGH, NOT THE WRITE BEING HOISTED, and the substitution is deliberate: a battery entry
#   is ONE replacement, while hoisting the write above the version gate also requires moving `parse_beacon` (the id
#   does not exist before it). "The gate merely warns and lets the frame through — looking at it is harmless" is the
#   same defect from the reachable side, at match count 1, and it is a far more tempting wrong fix than a relocation.
MUTS_TEAMSEENSITE = [
 ("W01 ★★★ READ-ONLY BROKEN: the observation ALSO caches the advertiser's key hash in _team_keys ('we have it anyway')",
  "    if (b.is_mobile && peer_team != 0) team_seen_observe(peer_team, protocol::db_to_q4(meta.snr_db), b.src);",
  "    if (b.is_mobile && peer_team != 0) {\n"
  "        team_seen_observe(peer_team, protocol::db_to_q4(meta.snr_db), b.src);\n"
  "        team_key_set(b.src, b.key_hash32, IdBindSource::bcn, IdBindConf::authoritative);\n"
  "    }"),
 ("W02 ★★★ READ-ONLY BROKEN: the observation ALSO writes the STATIC id->hash binding plane",
  "    if (b.is_mobile && peer_team != 0) team_seen_observe(peer_team, protocol::db_to_q4(meta.snr_db), b.src);",
  "    if (b.is_mobile && peer_team != 0) {\n"
  "        team_seen_observe(peer_team, protocol::db_to_q4(meta.snr_db), b.src);\n"
  "        id_bind_set(b.src, b.key_hash32, IdBindSource::bcn, IdBindConf::authoritative);\n"
  "    }"),
 ("W03 ★★★ READ-ONLY BROKEN: the observation ALSO learns a TEAM-plane route to a team we are not in",
  "    if (b.is_mobile && peer_team != 0) team_seen_observe(peer_team, protocol::db_to_q4(meta.snr_db), b.src);",
  "    if (b.is_mobile && peer_team != 0) {\n"
  "        team_seen_observe(peer_team, protocol::db_to_q4(meta.snr_db), b.src);\n"
  "        learn_direct_neighbor(b.src, protocol::db_to_q4(meta.snr_db), false, /*team_plane=*/true);\n"
  "    }"),
 ("W04 ★★ the `peer_team != 0` term is dropped — every teamless mobile's beacon records a phantom team 0",
  "    if (b.is_mobile && peer_team != 0) team_seen_observe(peer_team, protocol::db_to_q4(meta.snr_db), b.src);",
  "    if (b.is_mobile) team_seen_observe(peer_team, protocol::db_to_q4(meta.snr_db), b.src);"),
 ("W05 ★★ the `b.is_mobile` term is dropped — a STATIC advertiser's TLV is recorded as a nearby team",
  "    if (b.is_mobile && peer_team != 0) team_seen_observe(peer_team, protocol::db_to_q4(meta.snr_db), b.src);",
  "    if (peer_team != 0) team_seen_observe(peer_team, protocol::db_to_q4(meta.snr_db), b.src);"),
 ("W06 ★★ the WIRE-VERSION gate merely warns and falls through ('observing is harmless'), so a wire-incompatible "
  "frame is parsed and recorded",
  "    if (their_wire_ver != protocol::wire_version) {                     // incompatible wire -> refuse + tell the operator (Push, not telemetry)\n"
  "        push_join_refused_wire(their_wire_ver);\n"
  "        return;                                                        // don't peer, don't parse a foreign-version format\n"
  "    }",
  "    if (their_wire_ver != protocol::wire_version) {\n"
  "        push_join_refused_wire(their_wire_ver);\n"
  "    }"),
 # ★★ AND THIS ONE'S EVIDENCE IS IN THE REPOSITORY, BY PATH — not in a session report. Applying W07 and rebuilding
 #    `lus` moves `s38_team_origin_learn_meshroute` OFF its anchor (`1d0bb046`/526 -> `b850daba`/550, +24 events, all
 #    of them the mutant's own emit and nothing else in the histogram): the captured three-step clean/mutant/restored
 #    comparison, with the anchor quoted from simulation/BASELINE.md, is
 #        docs/superpowers/evidence/2026-08-22-ui16-n1-evidence.md  (§2)
 #    ⇒ the ABSENCE of telemetry on the N1 path is a MEASURED decision, and the proof outlives the session.
 ("W07 ★★★ an MR_EMIT is added on the new path — the control that makes the ABSENCE of telemetry a measured "
  "decision rather than an omission (it also MOVES a team corpus stream: see "
  "docs/superpowers/evidence/2026-08-22-ui16-n1-evidence.md §2)",
  "    if (b.is_mobile && peer_team != 0) team_seen_observe(peer_team, protocol::db_to_q4(meta.snr_db), b.src);",
  "    if (b.is_mobile && peer_team != 0) team_seen_observe(peer_team, protocol::db_to_q4(meta.snr_db), b.src);\n"
  '    if (b.is_mobile && peer_team != 0) MR_EMIT("team_seen", EF_I("team", static_cast<int64_t>(peer_team)));'),
]

# ===== §UI-15 slice 2 CORRECTIONS — src/firmware_config_parse.h (blocker 3) =========================================
# ★★ `handle_joinprofile` read its slot with `atol`, which PARSES A PREFIX: `clear 2junk` cleared slot 2 and
#    `set 1x …` overwrote slot 1. The strict parse lives here because `src/firmware_config.cpp` is compiled by no
#    automated build; these entries are what makes "strict" a measurement instead of an adjective.
MUTS_CFGPARSE = [
 ("P01 ★★★ THE DEFECT ITSELF: the parse falls back to atol, so a prefix is accepted again",
  "inline bool parse_index_strict(const char* tok, long& out) {\n"
  "    if (!tok || !*tok) return false;\n"
  "    long v = 0;\n"
  "    for (const char* c = tok; *c; ++c) {\n"
  "        if (*c < '0' || *c > '9') return false;",
  "inline bool parse_index_strict(const char* tok, long& out) {\n"
  "    if (!tok || !*tok) return false;\n"
  "    long v = atol(tok);\n"
  "    for (const char* c = tok; *c; ++c) {\n"
  "        if (*c < '0' || *c > '9') break;"),
 ("P02 the loop stops at the first non-digit instead of refusing (atol's exact leniency, hand-written)",
  "        if (*c < '0' || *c > '9') return false;",
  "        if (*c < '0' || *c > '9') break;"),
 ("P03 an EMPTY token parses as slot 0 rather than refusing (the 'unparsable == zero' conflation)",
  "    if (!tok || !*tok) return false;",
  "    if (!tok) return false;\n    if (!*tok) { out = 0; return true; }"),
 ("P04 `out` is written on a REFUSAL too (\"report how far we got\") — a caller that skips the bool gets a partial index",
  "        if (*c < '0' || *c > '9') return false;",
  "        if (*c < '0' || *c > '9') { out = v; return false; }"),
 ("P05 overflow WRAPS instead of refusing (a 20-digit token becomes a plausible small slot)",
  "        if (v > (2147483647L - d) / 10) return false;   // ⛔ refuse, never wrap. 2^31-1 because `long` is 32 bits on",
  "        if (false) return false;                        // ⛔ refuse, never wrap. 2^31-1 because `long` is 32 bits on"),
 ("P06 ★★ A LEADING SIGN IS ACCEPTED, so `-1` and `-0` become indices",
  "        if (*c < '0' || *c > '9') return false;\n"
  "        const long d = *c - '0';",
  "        if (c == tok && (*c == '-' || *c == '+')) continue;\n"
  "        if (*c < '0' || *c > '9') return false;\n"
  "        const long d = *c - '0';"),
 ("P07 ★★ a trailing token is IGNORED again (`list extra` runs a plain list and drops the word)",
  "inline bool arg_tail_empty(const char* p) {\n"
  "    if (!p) return true;\n"
  "    while (*p == ' ') ++p;\n"
  "    return *p == '\\0';\n}",
  "inline bool arg_tail_empty(const char*) { return true; }"),
 ("P08 the tail check stops at the first space, so ` extra` reads as empty",
  "    while (*p == ' ') ++p;\n    return *p == '\\0';",
  "    return *p == '\\0' || *p == ' ';"),
]

# ===== §UI-15 slice 5 — src/firmware_ui_prov.h =====================================================================
# ★★★ WHAT THIS BATTERY IS AIMED AT: **THE PRECONDITION THAT CAN SILENTLY BECOME A NO-OP.** `live_phy_matches` EARLY-
#     RETURNS TRUE on `!phy.present`, so three of the entries below (V01/V02/V05) are the SAME defect wearing three
#     faces — a create that passes the owner's PHY check without ever comparing anything. ⛔ None of them is a
#     deletion: each is a tempting wrong fix (drop the guard, invert it, "reuse" the request's phy object).
# ★ V03 is plan §2.1's OTHER half — `present = true` on the REQUEST, which re-introduces the [[B209]] retune path —
#   and V04 is [[B211]] one file over: the `sf_list` field left out of the comparison, which is exactly the field a
#   hand-written equality forgets.
MUTS_UIPROV = [
 # --- the precondition ---------------------------------------------------------------------------------------------
 ("V01 the PHY precondition is dropped (the owner's refusal never fires)",
  "    if (!live_phy_matches(persisted, snap)) {\n"
  "        a.outcome = mrui::UiProvOutcome::phy_differs;   // the panel says `PHY DIFFERS` / `USE SERIAL`\n"
  "        return a;                                       // ⛔ 0 transaction calls, 0 writes, 0 airtime, 0 retunes\n"
  "    }",
  "    ;"),
 ("V02 the precondition is INVERTED (a converged node is refused, a divergent one creates)",
  "    if (!live_phy_matches(persisted, snap)) {",
  "    if (live_phy_matches(persisted, snap)) {"),
 ("V05 ★★ the comparison's ProvPhy is left `present = false` — `live_phy_matches` then ALWAYS returns true",
  "    persisted.present           = true;",
  "    persisted.present           = false;"),
 ("V04 [[B211]]: the `sf_list` is left out of the comparison (the field a hand-written equality forgets)",
  "    persisted.allowed_sf_bitmap = rec.allowed_sf_bitmap;",
  "    persisted.allowed_sf_bitmap = snap.live_allowed_sf_bitmap;"),
 ("V06 the persisted PHY is read from the LIVE snapshot instead of the record (the comparison compares live to live)",
  "    persisted.freq_mhz          = rec.freq_mhz;",
  "    persisted.freq_mhz          = snap.live_freq_mhz;"),
 ("V07 an unreadable record is treated as a satisfied precondition (the create proceeds on no evidence)",
  "    if (!dev.load_record(rec)) {",
  "    if (false) {"),
 # --- plan §2.1's request ------------------------------------------------------------------------------------------
 ("V03 ★★ `phy.present = TRUE` on the REQUEST — the retune [[B209]] forbids, smuggled in as 'capture the PHY'",
  "    rq.mint  = true;",
  "    rq.mint  = true;\n    rq.phy = persisted;"),
 ("V08 the create is not a MINT (it becomes `team 0`, i.e. a LEAVE)",
  "    rq.mint  = true;", "    rq.mint  = false;"),
 ("V09 the build floor is dropped, so a 0 in the record refuses for the wrong reason",
  "    rq.floor = floor;", "    ;"),
 # --- the verdict mapping + the post-save bookkeeping ---------------------------------------------------------------
 ("V10 the notification fires on EVERY verdict ([[B194]] inverted: a claim on a write that never happened)",
  "    const ProvResult r = dev.apply(rq, snap);\n    switch (r.verdict) {",
  "    const ProvResult r = dev.apply(rq, snap);\n    dev.on_applied(r);\n    switch (r.verdict) {"),
 # ⚠ V11 RE-ANCHORED 2026-08-20 (§UI-15 slice 6): `save_failed` is now written by BOTH halves of this file, so the
 #   bare assignment matches TWICE and the runner would report it ambiguous. The verdict label above it is what makes
 #   the CREATE half's arm unique; the join half's twin is V16.
 ("V11 a FAILED save is reported as a refusal (the operator is not told the write failed)",
  "        case ProvVerdict::nv_failed:\n            a.outcome = mrui::UiProvOutcome::save_failed;",
  "        case ProvVerdict::nv_failed:\n            a.outcome = mrui::UiProvOutcome::refused;"),
 ("V12 a staging refusal loses the service's typed reason (the panel says nothing actionable)",
  "            a.reason  = prov_err_name(r.err);           // the SERVICE's own token (U1) — never a second table",
  "            a.reason  = \"\";"),
 ("V13 the created answer carries no id, so the success screen cannot show one",
  "            a.team_id = r.team_id;", "            a.team_id = 0;"),
 ("V14 the intent dispatch performs a create for the INERT `none` op",
  "            case mrui::UiProvOp::none:        break;",
  "            case mrui::UiProvOp::none:        return ui_prov_create_team(_dev);"),
 # ================================================== §UI-15 slice 6 — the STATIC-JOIN half of the SAME adapter file
 # ★ THE AIM IS THE THREE WAYS A JOIN ADAPTER LIES: it claims membership the transaction never established (V19),
 #   it notifies for a write that did not happen (V15/V16), or it converts the units wrong and joins another carrier
 #   (V18) — the defect the Hz-not-kHz ruling exists for, arriving at the last place that could still commit it.
 ("V15 the join notification fires on EVERY verdict ([[B194]] inverted, one feature over)",
  "    const JoinResult  r  = dev.apply(rq);\n    switch (r.verdict) {",
  "    const JoinResult  r  = dev.apply(rq);\n    dev.on_started(r);\n    switch (r.verdict) {"),
 ("V16 a FAILED join save is reported as a refusal (the operator is not told the write failed)",
  "            a.outcome = mrui::UiProvOutcome::save_failed;\n            return a;\n        case JoinVerdict::refused:",
  "            a.outcome = mrui::UiProvOutcome::join_refused;\n            return a;\n        case JoinVerdict::refused:"),
 ("V17 the EMPTY-slot floor is dropped — an unwritten slot runs the transaction",
  "    if (!p.present) {\n        a.outcome = mrui::UiProvOutcome::join_refused;\n"
  "        a.reason  = \"empty slot\";\n        return a;",
  "    if (false) {\n        a.outcome = mrui::UiProvOutcome::join_refused;\n"
  "        a.reason  = \"empty slot\";\n        return a;"),
 ("V18 ★★ the units conversion is re-derived here in kHz — 869462500 Hz joins 869462.5 MHz",
  "    const JoinRequest rq = join_request_from_profile(p);",
  "    JoinRequest rq{}; rq.layer = p.layer; rq.routing_sf = p.routing_sf;\n"
  "    rq.freq_mhz = double(p.freq_hz) / 1000.0; rq.bw_khz = double(p.bw_hz) / 1000.0;"),
 ("V19 ⛔⛔ a STARTED transaction claims ADOPTION — the `JOINED`-before-a-correlated-adopt defect, at its source",
  "            a.outcome = mrui::UiProvOutcome::joining;",
  "            a.outcome = mrui::UiProvOutcome::adopted;"),
 ("V20 a join refusal loses the transaction's typed reason (the screen cannot say WHICH field is wrong)",
  "            a.reason  = join_err_name(r.err);        // the TRANSACTION's own token (U1) — never a second table",
  "            a.reason  = \"\";"),
 ("V21 the SELECT read never marks itself served (every store reads as a missing seam)",
  "    l.served = true;", "    ;"),
 ("V22 the intent dispatch sends a JOIN to the TEAM-CREATE act (the two ops collapse)",
  "            case mrui::UiProvOp::join_static: return ui_prov_join_static(_join, intent.join);",
  "            case mrui::UiProvOp::join_static: return ui_prov_create_team(_dev);"),
 # ============================================ §UI-16 N3 — the NEARBY-TEAM JOIN half of the SAME adapter file
 # ★★★★ V23 IS THE SLICE'S HEADLINE CONTROL AND IT IS **ONE WORD**: a "join" that MINTS. The operator presses JOIN on
 #      a team they can see, and the transaction draws a NEW random id and a NEW keypair — so they end up ALONE, in a
 #      team that never existed, holding a key nobody else has, with `TEAM JOINED` on the panel. ⓘ Nothing about the
 #      screen changes; only the request does.
 # ⛔ V24/V25 ARE THE PRECONDITION'S TWO HALVES ON THIS ARM — the refusal deleted, and the comparison turned into the
 #    no-op `live_phy_matches` early-returns for a `present = false` phy. They are V01/V05's twins, and they need
 #    their own entries because a control that reddens the CREATE arm says nothing about the JOIN one.
 ("V23 ★★★ `mint` is left TRUE — a JOIN that MINTS A NEW TEAM (the headline control)",
  "    rq.mint    = false;            // ★★★ A JOIN, ⛔ NEVER A MINT — see difference 1 in the block above",
  "    rq.mint    = true;"),
 ("V24 the join arm's PHY precondition never refuses (the owner ruling applies to creates only)",
  "    if (!phy_ok) {", "    if (false) {"),
 ("V25 ★★ the join precondition is built with `present = false`, so `live_phy_matches` ALWAYS passes",
  "    persisted.present = true;      // ⚠ THE COMPARISON's object", "    persisted.present = false;      // ⚠ THE COMPARISON's object"),
 ("V26 ★★ `rq.phy.present` is set TRUE on the request — a membership join that RETUNES ([[B209]])",
  "    rq.team_id = team_id;", "    rq.team_id = team_id;\n    rq.phy = persisted;"),
 ("V27 ★★ a FAILED save is rendered as a SUCCESS — `TEAM JOINED` for a write that did not land",
  "            a.outcome = mrui::UiProvOutcome::save_failed;\n"
  "            return a;\n"
  "        case ProvVerdict::refused:\n"
  "            a.outcome = mrui::UiProvOutcome::join_refused;",
  "            a.outcome = mrui::UiProvOutcome::team_joined;\n"
  "            return a;\n"
  "        case ProvVerdict::refused:\n"
  "            a.outcome = mrui::UiProvOutcome::join_refused;"),
 ("V28 `no_change` is rendered as a success — the screen claims a membership nothing wrote",
  "            a.outcome = mrui::UiProvOutcome::join_refused;\n"
  "            a.reason  = \"no change\";",
  "            a.outcome = mrui::UiProvOutcome::team_joined;\n"
  "            a.reason  = \"no change\";"),
 ("V29 the zero-id floor is dropped — `team_id = 0` reaches the transaction, where it means LEAVE",
  "    if (team_id == 0) {", "    if (false) {"),
 ("V30 the join notification fires on every verdict ([[B194]] inverted, a third time)",
  "    const ProvResult res = dev.apply(rq, snap);\n    switch (res.verdict) {",
  "    const ProvResult res = dev.apply(rq, snap);\n    dev.on_applied(res);\n    switch (res.verdict) {"),
 ("V31 the joined answer carries no id, so the success screen can show neither identity",
  "            a.team_id = res.team_id;", "            a.team_id = 0;"),
 ("V32 the intent dispatch sends the NEARBY JOIN to the create act (a JOIN that mints, by routing)",
  "            case mrui::UiProvOp::join_team:   return ui_prov_join_team(_dev, intent.team_id);",
  "            case mrui::UiProvOp::join_team:   return ui_prov_create_team(_dev);"),
]

# ==================================================================== §UI-15 slice 6 — src/firmware_ui_join.h
# ★★★★ THE FOUR TERMS ARE MUTATED **SEPARATELY**, which is plan §2.3 rule 7's own requirement (*"⛔ Mutation-test ALL
#      FOUR terms separately"*) and the reason this file has a target of its own. Each single-term drop makes the
#      rule ACCEPT an uncorrelated push, and the case that then reddens is the one NAMED after the event it would
#      have completed: a boot DAD, a heal re-adopt, a wrong nibble, a foreign or zero dst.
# ⛔ J07/J08 ARE THE **TRAP 2** PAIR AND NEITHER IS A DROP: they are the two ways to write the comparison WRONG WHILE
#    IT STILL LOOKS RIGHT — full-against-nibble and nibble-against-full. v3 of the plan shipped the first and it was
#    unsatisfiable above layer 15, which is why it is a control here rather than a note.
MUTS_UIJOIN = [
 ("J01 TERM 1 dropped — a BOOT DAD completes a screen nobody opened (`join_adopted` fires at every boot)",
  "    if (!sess.active) return false;", "    ;"),
 ("J02 TERM 2 dropped — a HEAL RE-ADOPT on the record's CURRENT layer completes the operator's join",
  "    if (sess.requested_layer != persisted_layer0_id) return false;", "    ;"),
 ("J03 TERM 3 dropped — an adopt on ANY leaf completes the screen (the nibble is never checked)",
  "    if (push_layer_id != mrfw::join_leaf_of_layer(sess.requested_layer)) return false;", "    ;"),
 ("J04 TERM 4a dropped — somebody ELSE's adoption completes our screen (the id is never compared)",
  "    if (push_dst != canonical_node_id) return false;", "    ;"),
 ("J05 TERM 4b dropped — a ZERO dst completes the screen (a node that adopted NOTHING reports success)",
  "    if (push_dst == 0) return false;", "    ;"),
 ("J06 ★★ the KIND GATE widened to `join_refused` — a wire-version OBSERVATION ABOUT ANOTHER PEER reaches the screen",
  "    if (kind != MESHROUTE_NS::PushKind::join_adopted) return false;",
  "    if (kind != MESHROUTE_NS::PushKind::join_adopted && kind != MESHROUTE_NS::PushKind::join_refused) return false;"),
 ("J07 ★★ TRAP 2: term 3 compares the push's NIBBLE against the FULL byte (plan v3's unsatisfiable rule, restored)",
  "    if (push_layer_id != mrfw::join_leaf_of_layer(sess.requested_layer)) return false;",
  "    if (push_layer_id != sess.requested_layer) return false;"),
 ("J08 ★★ TRAP 2, the mirror: term 2 compares the NIBBLE against the persisted FULL byte",
  "    if (sess.requested_layer != persisted_layer0_id) return false;",
  "    if (mrfw::join_leaf_of_layer(sess.requested_layer) != persisted_layer0_id) return false;"),
 # --- §3's store matrix, as PANEL TEXT ------------------------------------------------------------------------------
 ("J09 ⛔ `io_failed` is collapsed into `invalid` — a mount failure invites `reset confirm`, discarding 4 presets",
  '            return l.res.err == mrfw::ProfileErr::store_io_failed ? "STORAGE FAILURE" : "PROFILE STORE";',
  '            return "PROFILE STORE";'),
 ("J10 ⛔ an UNREADABLE store reads as an EMPTY one (the [[B218]] collapse, i.e. corruption as a fresh device)",
  "        case mrfw::ProfileVerdict::refused:\n"
  "            return l.res.err == mrfw::ProfileErr::store_io_failed ? \"STORAGE FAILURE\" : \"PROFILE STORE\";",
  "        case mrfw::ProfileVerdict::refused: return \"NO PROFILES\";"),
 ("J11 a MISSING SEAM reads as an ordinary empty store (the panel claims a store it never asked)",
  '    if (!l.served) return "NO JOIN SERVICE";', '    if (!l.served) return "NO PROFILES";'),
 ("J12 the two remedies are swapped (a corrupt record is sent to `faults`, a dead store to `reset`)",
  '    return l.res.err == mrfw::ProfileErr::store_io_failed ? "CHECK faults" : "INVALID";',
  '    return l.res.err == mrfw::ProfileErr::store_io_failed ? "INVALID" : "CHECK faults";'),
 # --- the SELECT list ----------------------------------------------------------------------------------------------
 ("J13 ⛔ a store that could not be READ still offers its slots (a join from a record nobody could load)",
  "    if (l.served && l.res.verdict == mrfw::ProfileVerdict::ok) {",
  "    if (true) {"),
 ("J14 the row carries its POSITION instead of its SLOT NUMBER (§B66 — slot 3 becomes slot 2)",
  "            out.row[out.n].slot1 = uint8_t(i + 1);", "            out.row[out.n].slot1 = uint8_t(out.n + 1);"),
 ("J15 an EMPTY slot becomes a row, so a `double` can land on one",
  "            if (!l.rec.prof[i].present) continue;", "            ;"),
 ("J16 BACK becomes CONDITIONAL on the store (leaving depends on a record)",
  "    out.row[out.n].back = true;\n    ++out.n;",
  "    if (out.n) { out.row[out.n].back = true; ++out.n; }"),
 # --- the strings --------------------------------------------------------------------------------------------------
 ("J17 ⛔⛔ the waiting screen says `JOINED` before any adopt (plan §2.3 rule 1, verbatim)",
  'inline const char* join_wait_head(bool still) { return still ? "STILL JOINING" : "JOINING"; }',
  'inline const char* join_wait_head(bool still) { return still ? "STILL JOINING" : "JOINED"; }'),
 ("J18 the 60 s threshold is 0, so the panel opens on `STILL JOINING`",
  "inline constexpr uint32_t kJoinStillMs = 60000;", "inline constexpr uint32_t kJoinStillMs = 0;"),
 ("J19 an empty NAME renders nothing instead of plan §11's `PROFILE n` default",
  '    if (n == 0) { snprintf(out, cap, "PROFILE %u", unsigned(slot1)); return; }', "    ;"),
 ("J20 ⛔ the stored label is read as a C string (it is NOT NUL-terminated — the read runs into `freq_hz`)",
  "    if (n > cap - 1) n = cap - 1;\n    memcpy(out, p.name, n);\n    out[n] = '\\0';",
  "    snprintf(out, cap, \"%s\", p.name);"),
 ("J21 the frequency loses its four decimals — 869.4625 MHz renders (and reads) as 869",
  '    snprintf(out, cap, "%lu.%04lu MHz", mhz, frac);', '    snprintf(out, cap, "%lu MHz", mhz);'),
 ("J22 the confirmation shows the LEAF NIBBLE instead of the full layer byte (17 reads as 1)",
  '    snprintf(out, cap, "L%u SF%u BW%lu.%02lu", unsigned(p.layer), unsigned(p.routing_sf), khz, cen);',
  '    snprintf(out, cap, "L%u SF%u BW%lu.%02lu", unsigned(p.layer & 0x0F), unsigned(p.routing_sf), khz, cen);'),
 ("J23 the bandwidth is rendered in Hz (500000.00 — the units mix plan §3's mutation controls name)",
  "    const unsigned long khz = (unsigned long)(p.bw_hz / 1000u);",
  "    const unsigned long khz = (unsigned long)p.bw_hz;"),
 ("J24 the RESULT's node line drops the id (the one thing plan §2.3 rule 2 requires it to show)",
  '    snprintf(out, cap, "node %u", unsigned(node_id));', '    snprintf(out, cap, "node");'),
 ("J25 the confirmation's two actions are swapped, so `>BACK` performs the join",
  'inline const char* join_confirm_label(bool confirm) { return confirm ? "JOIN" : "BACK"; }',
  'inline const char* join_confirm_label(bool confirm) { return confirm ? "BACK" : "JOIN"; }'),
]

# ===== [[B230]] — src/firmware_provisioning_service.h ==============================================================
# ★★★ WHAT THIS BATTERY IS AIMED AT, AND IT IS DELIBERATELY NARROW: **A REFUSAL WHOSE OWN REMEDY CANNOT WORK.** The
#     transaction's BEHAVIOUR (one save, nothing applied on failure, the `no_change` rule, [[B211]]'s resolution) is
#     already covered by `test/test_firmware_provisioning_service.cpp` and by `tools/probe_prov_tx`; what [[B230]]
#     added is a CLASSIFICATION — an empty `sf_list` is a different refusal from an incomplete freq/sf/bw triplet,
#     because the first is repaired by `cfg set sf_list …` and the second by the verb's own PHY tail.
# ⛔ NONE OF THE FOUR IS A DELETION. Each is the tempting wrong fix: fold the arm back in (P01, the pre-[[B230]] source
#    VERBATIM), invert the test (P02), reorder the two checks so the tail complaint wins (P03 — which re-creates the
#    dead end exactly: the operator is handed `team new freq=… sf=… bw=…`, the command that just failed), or let the
#    new arm answer with its neighbour's token (P04, the panel/console saying the wrong thing while the enum is right).
MUTS_PROVSERVICE = [
 ("P01 the sf_list arm is folded back into the generic refusal (the pre-[[B230]] source, verbatim)",
  "        if (cand.allowed_sf_bitmap == 0) return ProvErr::sf_list_empty;\n"
  "        if (eff_freq <= 0.0 || cand.routing_sf < 5 || cand.routing_sf > 12\n"
  "            || eff_bw == 0) return ProvErr::incomplete_phy;",
  "        if (eff_freq <= 0.0 || cand.routing_sf < 5 || cand.routing_sf > 12\n"
  "            || cand.allowed_sf_bitmap == 0 || eff_bw == 0) return ProvErr::incomplete_phy;"),
 ("P02 the classification is INVERTED (a PRESENT sf_list is reported as the empty one)",
  "        if (cand.allowed_sf_bitmap == 0) return ProvErr::sf_list_empty;",
  "        if (cand.allowed_sf_bitmap != 0) return ProvErr::sf_list_empty;"),
 ("P03 ★★ the ORDER is reversed — the tail complaint wins, so the failing command is suggested again",
  "        if (cand.allowed_sf_bitmap == 0) return ProvErr::sf_list_empty;\n"
  "        if (eff_freq <= 0.0 || cand.routing_sf < 5 || cand.routing_sf > 12\n"
  "            || eff_bw == 0) return ProvErr::incomplete_phy;",
  "        if (eff_freq <= 0.0 || cand.routing_sf < 5 || cand.routing_sf > 12\n"
  "            || eff_bw == 0) return ProvErr::incomplete_phy;\n"
  "        if (cand.allowed_sf_bitmap == 0) return ProvErr::sf_list_empty;"),
 ("P04 the new arm answers with its NEIGHBOUR's token (the enum splits, the words do not)",
  '        case ProvErr::sf_list_empty:   return "sf_list_empty";',
  '        case ProvErr::sf_list_empty:   return "incomplete_phy";'),
 # --- §UI-16 K2: the keyring step and the ACTIVE BINDING. ⛔ Each is a plausible "simplification", not a deletion. --
 ("P05 ★★★ [[B240]] RESTORED: the created/imported key is never written to the keyring (only /mrcfg moves)",
  "        if (plan.key_action == KeyAction::install) {\n"
  "            const KeyringResult kr = _keyring.put(plan.team_id, plan.key_pub, plan.key_priv);",
  "        if (false) {\n"
  "            const KeyringResult kr = _keyring.put(plan.team_id, plan.key_pub, plan.key_priv);"),
 ("P06 ★★ THE ORDER IS REVERSED: the /mrcfg record that ACTIVATES the key is written BEFORE the key is durable",
  "        if (plan.key_action == KeyAction::install) {\n"
  "            const KeyringResult kr = _keyring.put(plan.team_id, plan.key_pub, plan.key_priv);\n"
  "            if (kr.verdict == KeyringVerdict::refused || kr.verdict == KeyringVerdict::nv_failed) {\n"
  "                r.err     = prov_err_of_keyring(kr.err);\n"
  "                r.verdict = (kr.verdict == KeyringVerdict::nv_failed) ? ProvVerdict::nv_failed : ProvVerdict::refused;\n"
  "                return r;                                 // ⛔ 0 /mrcfg writes, 0 live calls, 0 airtime\n"
  "            }\n"
  "        }\n"
  "\n"
  "        if (!_store.save(cand)) {                         // ★ EXACTLY ONE save ATTEMPT\n"
  "            r.err     = ProvErr::nv_save_failed;\n"
  "            r.verdict = ProvVerdict::nv_failed;\n"
  "            return r;                                     // ⛔ 0 live calls — live PHY/role/team/keys untouched, 0 airtime\n"
  "        }\n",
  "        if (!_store.save(cand)) {                         // ★ EXACTLY ONE save ATTEMPT\n"
  "            r.err     = ProvErr::nv_save_failed;\n"
  "            r.verdict = ProvVerdict::nv_failed;\n"
  "            return r;                                     // ⛔ 0 live calls — live PHY/role/team/keys untouched, 0 airtime\n"
  "        }\n"
  "\n"
  "        if (plan.key_action == KeyAction::install) {\n"
  "            const KeyringResult kr = _keyring.put(plan.team_id, plan.key_pub, plan.key_priv);\n"
  "            if (kr.verdict == KeyringVerdict::refused || kr.verdict == KeyringVerdict::nv_failed) {\n"
  "                r.err     = prov_err_of_keyring(kr.err);\n"
  "                r.verdict = (kr.verdict == KeyringVerdict::nv_failed) ? ProvVerdict::nv_failed : ProvVerdict::refused;\n"
  "                return r;                                 // ⛔ 0 /mrcfg writes, 0 live calls, 0 airtime\n"
  "            }\n"
  "        }\n"),
 ("P07 ★★★ P-15 SOFTENED: a keyring REFUSAL (full/corrupt) no longer refuses the transaction — /mrcfg claims an active key nothing stored",
  "            if (kr.verdict == KeyringVerdict::refused || kr.verdict == KeyringVerdict::nv_failed) {",
  "            if (kr.verdict == KeyringVerdict::nv_failed) {"),
 ("P08 ★★ `team 0` LEAVES THE BINDING ARMED — a re-join silently reactivates the retained key (P-2b)",
  "        if (cand.team_key_team_id != 0 || cand.team_key_active != 0) differs = true;\n"
  "        cand.team_key_team_id = 0;\n"
  "        cand.team_key_active  = 0;",
  "        if (cand.team_key_team_id != 0 || cand.team_key_active != 0) differs = true;"),
 ("P09 the key is STORED but never ACTIVATED — a reboot finds a keyring record no binding points at",
  "        cand.team_key_team_id = plan.team_id;\n"
  "        cand.team_key_active  = 1;",
  "        cand.team_key_team_id = plan.team_id;\n"
  "        cand.team_key_active  = 0;"),
 ("P10 the two unreadable keyring answers are COLLAPSED, so a dead store is reported as a corrupt record",
  "        case KeyringErr::store_io_failed: return ProvErr::keyring_io_fail;",
  "        case KeyringErr::store_io_failed: return ProvErr::keyring_invalid;"),
]


# ★★★ §UI-17 slice 3 — THE STATUS BODY. Every entry is a substitution the spec RULED, attacked on its own: six of
#     them are the ones spec §4's S3 "Mutations" bullet names verbatim, and the rest are the ruled WORDS and the two
#     silences (row 1 with no team, row 2 with no team configured), because a silence nothing can break is a silence
#     nothing measures. ⓘ Each is the TEMPTING WRONG FIX rather than a deletion: the plausible id, the withdrawn
#     word, the raw sum, the rounded coordinate — every one of them looks like tidier code.
MUTS_UISTATUS = [
 # --- row 4: the priority and the fix predicate (the two the spec names first) ------------------------------------
 ("S01 ★★ row 4's priority is INVERTED — RESTART NEEDED only when there is nothing else to show",
  "    if (reboot_required) {",
  "    if (reboot_required && !s.own_fix) {"),
 ("S02 the fix predicate becomes AND — a node on the equator or the meridian loses its position",
  "inline bool ui_status_have_fix(int32_t lat_e7, int32_t lon_e7) { return lat_e7 != 0 || lon_e7 != 0; }",
  "inline bool ui_status_have_fix(int32_t lat_e7, int32_t lon_e7) { return lat_e7 != 0 && lon_e7 != 0; }"),
 ("S03 the fix predicate reads ONE coordinate only (the core's refusal reads both)",
  "inline bool ui_status_have_fix(int32_t lat_e7, int32_t lon_e7) { return lat_e7 != 0 || lon_e7 != 0; }",
  "inline bool ui_status_have_fix(int32_t lat_e7, int32_t lon_e7) { (void)lon_e7; return lat_e7 != 0; }"),
 ("S04 ★★ the coordinate ROUNDS instead of truncating (a position further along than the stored one)",
  '                     lat_neg ? "-" : "", (long)(la / 10000000), (unsigned long)((la / 10000) % 1000),',
  '                     lat_neg ? "-" : "", (long)(la / 10000000), (unsigned long)(((la + 5000) / 10000) % 1000),'),
 # --- row 0 / row 1: the two identity rows and their silences ------------------------------------------------------
 ("S05 ⛔ NO TEAM is replaced by a zero id — `TEAM 00000000`, a plausible team nobody is in",
  "    const int n = (s.team_id == 0) ? snprintf(out, cap, \"NO TEAM\")\n"
  "                                   : snprintf(out, cap, \"TEAM %08lX\", (unsigned long)s.team_id);",
  "    const int n = snprintf(out, cap, \"TEAM %08lX\", (unsigned long)s.team_id);"),
 ("S06 row 1 says NO TEAM too (note a: two of five body rows spent on ONE fact)",
  "    if (s.team_id == 0) { ui_pad_token(out, cap, 0); return; }    // note a: row 0 already said it",
  "    if (s.team_id == 0) { snprintf(out, cap, \"NO TEAM\"); return; }    // note a: row 0 already said it"),
 ("S07 ⛔ a node before team-DAD renders `ME T0` — a PLAUSIBLE id for a node that has none",
  "    const int n = (s.my_team_id == 0) ? snprintf(out, cap, \"ME NO ID\")\n"
  "                                      : snprintf(out, cap, \"ME T%u\", unsigned(s.my_team_id));",
  "    const int n = snprintf(out, cap, \"ME T%u\", unsigned(s.my_team_id));"),
 # --- row 2: the ruled word, the priority and the silence ----------------------------------------------------------
 ("S08 ★★★ the WITHDRAWN word returns — `4 HEARD` for a count that is ROUTE EVIDENCE",
  '        n = snprintf(out, cap, "%s KNOWN", tok);',
  '        n = snprintf(out, cap, "%s HEARD", tok);'),
 ("S09 NO TEAM KEY loses its priority to the count (the actionable half stops being said)",
  "    if (!s.team_key_present) {",
  "    if (false) {"),
 ("S10 row 2's `team_id != 0` half is dropped, so a teamless node reads `-- KNOWN`",
  "    const bool configured = s.team_build && s.team_id != 0;",
  "    const bool configured = s.team_build;"),
 # --- row 3: the token and the applicability split -----------------------------------------------------------------
 ("S11 the unread token becomes the RAW SUM (the strip's 99+ and this row stop agreeing)",
  "    ui_fmt_mail(mail, sizeof mail, overflow ? kMailMax : uint8_t(mail_total), overflow);",
  '    snprintf(mail, sizeof mail, "%u", unsigned(mail_total));'),
 ("S12 the HOME half is drawn on a build with NO mobile plane (`--` for not-applicable)",
  "    if (!s.mobile_build) {",
  "    if (false) {"),
 # --- the frame-freeze remedy's own control (QG, 2026-08-21) -------------------------------------------------------
 ("S13 ★★ row 4 RE-DERIVES the fix instead of trusting the published `own_fix` (a second definition)",
  "    } else if (!s.own_fix) {",
  "    } else if (!ui_status_have_fix(s.own_lat_e7, s.own_lon_e7)) {"),
]

MUTS_UITEAM = [
 # --- the row's five fields (spec §3.2's ruled format, string S-11) ------------------------------------------------
 ("T01 ★★ the label loses its PRECISION — a long name pushes the age and both reserved columns off the row",
  '    const int n = snprintf(out, cap, "%c%-6.6s %3s %4s %2s",',
  '    const int n = snprintf(out, cap, "%c%-6s %3s %4s %2s",'),
 ("T02 ★★ the two location columns are dropped from the format, so the row loses them silently",
  '    const int n = snprintf(out, cap, "%c%-6.6s %3s %4s %2s",\n'
  "                           marked ? '>' : ' ', t.label, age, geo.dist, geo.dir);",
  '    const int n = snprintf(out, cap, "%c%-6.6s %3s",\n'
  "                           marked ? '>' : ' ', t.label, age);"),
 ("T03 the marker is INVERTED — a passive preview marks every teammate it never picked",
  "                           marked ? '>' : ' ', t.label, age, geo.dist, geo.dir);",
  "                           marked ? ' ' : '>', t.label, age, geo.dist, geo.dir);"),
 ("T04 ⛔ an UNKNOWN route age is rendered as an AGE (`old`) instead of `--`",
  "    ui_fmt_home_age(out, cap, /*ever=*/age_s != UINT32_MAX, uint64_t(age_s) * 1000u);",
  "    ui_fmt_home_age(out, cap, /*ever=*/true, uint64_t(age_s) * 1000u);"),
 # --- the bucket: the second expression of the token's boundaries (the sweep is what keeps it honest) --------------
 ("T05 ★★ the bucket's minute boundary is widened, so it stops agreeing with the drawn token",
  "    if (m < 60u)  return 0x02000000u | m;",
  "    if (m <= 60u) return 0x02000000u | m;"),
 ("T06 the UNKNOWN age shares `0s`'s bucket — a `--` row that turns to `0s` never repaints",
  "    if (age_s == UINT32_MAX) return kTeamAgeUnknown;      // the published \"unknown\", the token's `--`",
  "    if (age_s == UINT32_MAX) return 0x01000000u;          // the published \"unknown\", the token's `--`"),
 # --- the invalidation's four rules (spec §1.9 F-8 / §CHROME-3's idiom) --------------------------------------------
 ("T07 ★★★ the invalidation CLEARS on the equal arm (§8.3.1's WITHDRAWN instruction, restored)",
  "    if (ui_team_rows_equal(live, frozen)) return false;    // ⛔ nor on this one — an equal projection asks for nothing",
  "    if (ui_team_rows_equal(live, frozen)) { m.clear_dirty(); return false; }"),
 ("T08 ★★★ the comparison takes the RAW ages, so a lit TEAM screen repaints every second",
  "        if (ui_team_age_bucket(a.team[i].last_heard_s) != ui_team_age_bucket(b.team[i].last_heard_s)) return false;",
  "        if (a.team[i].last_heard_s != b.team[i].last_heard_s) return false;"),
 ("T09 the label is compared over its WHOLE array — a rename nobody can see repaints the panel",
  "        for (std::size_t c = 0; c < kTeamLabelCols; ++c)",
  "        for (std::size_t c = 0; c < sizeof(a.team[i].label); ++c)"),
 ("T10 the visibility gate is dropped entirely — every screen asks for TEAM's repaints",
  "    if (!ui_team_rows_visible(m.state().screen, m.compose_open(), m.state().detail, m.emergency()))\n"
  "        return false;                                      // ⛔ NOTHING is cleared on this arm either",
  "    if (false)\n"
  "        return false;                                      // ⛔ NOTHING is cleared on this arm either"),
 # --- the visibility gate, term by term (each is one arm of `draw_frame`'s own precedence) -------------------------
 ("T13 ★★ the COMPOSE term is dropped — a DM composed FROM team keeps asking for row repaints",
  "        && !compose_open                   // ...then a compose sub-view, which TEAM itself opens",
  "        && true                            // ...then a compose sub-view, which TEAM itself opens"),
 ("T14 the EMERGENCY term is dropped, so an alarm's body pays for the roster's repaints",
  "    return emg == Emergency::idle          // the alarm owns the body, from any screen",
  "    return true                            // the alarm owns the body, from any screen"),
 ("T15 the DETAIL term is dropped (the third body-replacing view stops being one)",
  "        && detail == InboxModal::closed    // ...then the inbox detail modal",
  "        && true                            // ...then the inbox detail modal"),
 ("T16 the SCREEN term is dropped, so INBOX and SETTINGS ask for TEAM's repaints too",
  "        && screen == Screen::team;         // ...and only then does the screen decide",
  "        && true;                           // ...and only then does the screen decide"),
 ("T11 the row loop runs to the ARRAY's end, so rows the panel never draws ask for paints",
  "    for (uint8_t i = 0; i < a.team_shown; ++i) {",
  "    for (uint8_t i = 0; i < kMaxTeamRows; ++i) {"),
 ("T12 the roster SIZE stops being compared — a teammate joining or leaving never repaints",
  "    if (a.team_shown != b.team_shown) return false;",
  "    if (false) return false;"),
 # --- §UI-17 S5: the row's HANDOFF to the location unit (the decisions themselves are `--target=uigeo`'s) ----------
 ("T17 ★★★ the row hard-wires the cache age to ZERO, so a STALE position renders as a current one",
  "    ui_geo_columns(geo, own, t.peer_loc_valid, t.peer_loc_age_s, t.peer_lat_e7, t.peer_lon_e7);",
  "    ui_geo_columns(geo, own, t.peer_loc_valid, 0, t.peer_lat_e7, t.peer_lon_e7);"),
 ("T18 the two location columns are composed and DROPPED (the row never shows a distance)",
  "    ui_geo_columns(geo, own, t.peer_loc_valid, t.peer_loc_age_s, t.peer_lat_e7, t.peer_lon_e7);",
  "    ui_geo_columns(geo, own, false, t.peer_loc_age_s, t.peer_lat_e7, t.peer_lon_e7);"),
 ("T19 ★★ the own fix is RE-DERIVED from the coordinates instead of the published own_fix (a second definition)",
  "    return GeoFix{ s.own_fix, s.own_lat_e7, s.own_lon_e7 };",
  "    return GeoFix{ true, s.own_lat_e7, s.own_lon_e7 };"),
 ("T20 ★★ the geo bucket leaves the repaint comparison, so a distance that turns never repaints",
  "        if (ui_team_geo_bucket(a, a.team[i]) != ui_team_geo_bucket(b, b.team[i])) return false;",
  "        if (false) return false;"),
]

# ★★★ §UI-17 slice 5 — THE LOCATION COLUMNS' DECISIONS. Every entry is the TEMPTING WRONG FIX rather than a deletion,
#     and each attacks ONE ruled decision: the freshness bound and its `<=`, the four terms of the show/blank rule,
#     the four maths rules spec §3.4 calls load-bearing, the two token tables, and the coincident-point RULING.
#     ⛔ Six of them are the ones spec §4's S5 "Mutations" bullet names verbatim; the rest are the terms and the
#     boundaries, because a rule whose terms have no isolated control is a rule measured by one case at best.
MUTS_UIGEO = [
 # --- the freshness bound (spec §3.4 term 4) -----------------------------------------------------------------------
 ("G01 ★★ the freshness comparison narrows to `<`, so a position AT the bound blanks",
  "inline bool ui_geo_fresh(uint32_t age_s) { return age_s <= kPeerLocMaxAgeS; }",
  "inline bool ui_geo_fresh(uint32_t age_s) { return age_s < kPeerLocMaxAgeS; }"),
 ("G02 ★★ the bound itself is widened to an hour (a ten-minute ruling, quietly re-ruled)",
  "inline constexpr uint32_t kPeerLocMaxAgeS = 600;",
  "inline constexpr uint32_t kPeerLocMaxAgeS = 3600;"),
 # --- the four terms of the show/blank rule --------------------------------------------------------------------
 ("G03 the OWN-FIX term is dropped — a node with no position of its own still draws distances",
  "    if (!own.have)                  return a;   // (1) we do not know where WE are",
  "    if (false)                      return a;   // (1) we do not know where WE are"),
 ("G04 ⛔ the CACHE-MISS term is dropped, so an uncached peer renders from (0,0)",
  "    if (!peer_valid)                return a;   // (2)+(3) no hash for that id, or nothing cached under it",
  "    if (false)                      return a;   // (2)+(3) no hash for that id, or nothing cached under it"),
 ("G05 ★★★ the FRESHNESS term is dropped — a week-old position renders as a current one",
  "    if (!ui_geo_fresh(peer_age_s))  return a;   // (4) too old to be a fact — ⛔ never rendered as a current one",
  "    if (false)                      return a;   // (4) too old to be a fact — ⛔ never rendered as a current one"),
 # --- the four maths rules (spec §3.4, all four load-bearing) ------------------------------------------------------
 ("G06 ★★★ the ANTIMERIDIAN FOLD is removed (two neighbours read as half a planet apart)",
  "    if      (dlon >  kGeoLonHalfE7) dlon -= kGeoLonSpanE7;\n"
  "    else if (dlon < -kGeoLonHalfE7) dlon += kGeoLonSpanE7;",
  "    (void)kGeoLonSpanE7;"),
 ("G07 ★★★ the longitude difference is taken in `int32_t`, where it OVERFLOWS",
  "    int64_t       dlon = int64_t(peer_lon_e7) - int64_t(own.lon_e7);",
  "    int64_t       dlon = int64_t(int32_t(peer_lon_e7 - own.lon_e7));"),
 ("G08 ★★ the octant thresholds are swapped (every cardinal still looks right)",
  "    if      (ax <= kGeoTan22_5 * ay) v.octant = (dy >= 0.0f) ? GeoOctant::n  : GeoOctant::s;",
  "    if      (ay <= kGeoTan22_5 * ax) v.octant = (dy >= 0.0f) ? GeoOctant::n  : GeoOctant::s;"),
 ("G09 ★★ `cos(mid_lat)` is dropped, so the longitude leg is unscaled (equator arithmetic everywhere)",
  "    const float dx = float(dlon) * kGeoMetresPerE7 * std::cos(mid_lat_deg * kGeoDegToRad);",
  "    const float dx = float(dlon) * kGeoMetresPerE7 * std::cos(0.0f * mid_lat_deg * kGeoDegToRad);"),
 # --- rule 3: the difference is taken as an INTEGER FIRST (the mantissa-loss boundary) ------------------------------
 # ⛔⛔ THESE TWO WERE MISSING WHEN S5 FIRST LANDED (QG, 2026-08-22) while the header claimed all four maths rules had
 #    an isolated control — and the case then offered as the rule's proof stayed GREEN under exactly this defect,
 #    because a 50 m step at 52 N is far above the quantisation there. ⇒ they are reddened by the HIGH-COORDINATE
 #    fixture (89 N / 170 E), where a metre is 89.83 e7 units against a float ULP of 64 and 128: the correct path
 #    renders `1m` and the float-first one `0m` — the panel telling a searcher their teammate is AT THEM.
 ("G17 ★★★ the LATITUDE difference is taken between two FLOATS (a metre lost to the mantissa)",
  "    const float dy = float(dlat) * kGeoMetresPerE7;",
  "    const float dy = (float(peer_lat_e7) - float(own.lat_e7)) * kGeoMetresPerE7;"),
 ("G18 ★★★ the LONGITUDE difference is taken between two FLOATS (and the fold goes with it)",
  "    const float dx = float(dlon) * kGeoMetresPerE7 * std::cos(mid_lat_deg * kGeoDegToRad);",
  "    const float dx = (float(peer_lon_e7) - float(own.lon_e7)) * kGeoMetresPerE7 * std::cos(mid_lat_deg * kGeoDegToRad);"),
 # --- the coincident-point ruling, from both sides -----------------------------------------------------------------
 ("G10 ★★★★ `has_bearing` is forced TRUE for a zero-length vector (a FABRICATED `N` at one campsite)",
  "    if (dlat == 0 && dlon == 0) return v;",
  "    if (dlat == 0 && dlon == 0) { v.has_bearing = true; return v; }"),
 ("G11 ★★★ the direction column is written whether the vector has a bearing or not",
  "    if (!a.v.has_bearing) return;",
  "    if (false) return;"),
 # --- the two token tables (string inventory S-13) -----------------------------------------------------------------
 ("G12 ★★ the one-decimal arm ROUNDS instead of truncating (and overruns four columns)",
  "        else if (m < 10000u) n = snprintf(out, cap, \"%lu.%luk\", (unsigned long)(m / 1000u),\n"
  "                                                                (unsigned long)((m % 1000u) / 100u));",
  "        else if (m < 10000u) n = snprintf(out, cap, \"%lu.%luk\", (unsigned long)(m / 1000u),\n"
  "                                                                (unsigned long)((m % 1000u + 50u) / 100u));"),
 ("G13 the metres arm runs to 10 km, so a five-column `1000m` is drawn in a four-column field",
  "        if      (m < 1000u)  n = snprintf(out, cap, \"%lum\", (unsigned long)m);",
  "        if      (m < 10000u) n = snprintf(out, cap, \"%lum\", (unsigned long)m);"),
 ("G14 ★★ the saturation arm is spelled `>=`, so a NaN falls through to an UNDEFINED cast",
  "    if (!(dist_m < 1000000.0f)) {",
  "    if (dist_m >= 1000000.0f) {"),
 # --- the repaint bucket (spec §1.9 F-8, extended by this slice) ---------------------------------------------------
 ("G15 the bucket's decimal arm turns ten times per token, so it stops agreeing with what is drawn",
  "    if (m < 10000u) return 0x02000000u | (m / 100u);                 // `%u.%uk` — ...every hundred metres",
  "    if (m < 10000u) return 0x02000000u | (m / 10u);                  // `%u.%uk` — ...every hundred metres"),
 ("G16 ★★ the bucket forgets the DIRECTION, so a bearing that turns never repaints the panel",
  "    return (ui_geo_dist_key(a.v.dist_m) << 4) | dir;",
  "    return (ui_geo_dist_key(a.v.dist_m) << 4) | (dir * 0u);"),
]

# ===== §UI-17 S8 — src/firmware_ui_send.h ==========================================================================
# ★★★★ THE SCOPE OF THE WAKE, AND NOTHING ELSE. The ruling (owner, 2026-08-20, spec §9 R-7) is that exactly two things
#      light the panel: a DM delivered to us (`msg_recv`, **sealed or not** — it is addressed to us) and a channel post
#      that arrived **SEALED** (`channel_recv` **and `pu.enc`**). ⛔ A CLEARTEXT post must not wake, which is what
#      keeps §R1/[[B109]]'s *"a stranger's post does not light a dark panel"* (bench §8.15) true BY CONSTRUCTION.
# ★★ EVERY ENTRY BELOW IS A **PLAUSIBLE** SHAPE OF THAT GATE, not a deletion: dropped (the headline), inverted,
#    copied onto the DM arm (the HALF-APPLIED shape, which an `enc == true` fixture alone would survive), hoisted above
#    the kind gate, or missing from one arm. ⓘ The EFFECT of the wake — the deadline, the input clock, navigation, the
#    emergency fields, the quiet node's sleep — belongs to `--target=model` (S08-S16).
MUTS_UISEND = [
 ("U01 ★★★ the `enc` GATE IS DROPPED — a CLEARTEXT stranger's post lights a dark panel (§8.15 broken)",
  "    if (pu.enc) m.on_msg_wake(now_ms);",
  "    m.on_msg_wake(now_ms);"),
 ("U02 ★★ the gate is INVERTED — only the posts we could NOT open wake the panel",
  "    if (pu.enc) m.on_msg_wake(now_ms);",
  "    if (!pu.enc) m.on_msg_wake(now_ms);"),
 # ⛔⛔ U03 IS THE HALF-APPLIED SHAPE, and it is the one a single sealed fixture cannot see: the channel arm's gate
 #     looks like a rule about messages, so it gets "applied consistently" to the DM arm — and an UNSEALED DM, which is
 #     ADDRESSED TO US, silently stops waking the panel.
 ("U03 ★★★ the `enc` gate is COPIED onto the msg_recv arm, so an UNSEALED DM stops waking",
  "        m.on_msg_wake(now_ms);\n        return true;",
  "        if (pu.enc) m.on_msg_wake(now_ms);\n        return true;"),
 ("U04 ★★ the wake is hoisted ABOVE the kind gate — every push kind wakes the panel",
  "                               const char* who, uint32_t now_ms) {\n    using PK = MESHROUTE_NS::PushKind;",
  "                               const char* who, uint32_t now_ms) {\n    using PK = MESHROUTE_NS::PushKind;\n"
  "    m.on_msg_wake(now_ms);"),
 ("U05 the wake is dropped from the channel_recv arm only (a sealed team post no longer wakes)",
  "    if (pu.enc) m.on_msg_wake(now_ms);",
  "    (void)pu.enc;"),
 ("U06 the wake is dropped from the msg_recv arm only (a DM addressed to us no longer wakes)",
  "        m.on_msg_wake(now_ms);\n        return true;",
  "        return true;"),
]

MUTS_BY_TARGET = {"model": MUTS_MODEL, "config": MUTS_CONFIG, "chrome": MUTS_CHROME, "icons": MUTS_ICONS,
                  "joinprofiles": MUTS_JOINPROFILES, "devicenv": MUTS_DEVICENV, "cfgparse": MUTS_CFGPARSE,
                  "uiprov": MUTS_UIPROV, "uijoin": MUTS_UIJOIN, "provservice": MUTS_PROVSERVICE,
                  "uistatus": MUTS_UISTATUS, "uiteam": MUTS_UITEAM, "uigeo": MUTS_UIGEO,
                  "uisend": MUTS_UISEND, "teamkeyring": MUTS_TEAMKEYRING,
                  "teamseen": MUTS_TEAMSEEN, "teamseensite": MUTS_TEAMSEENSITE,
                  "uinearby": MUTS_UINEARBY, "uinearbyrow": MUTS_UINEARBYROW,
                  "uiinvite": MUTS_UIINVITE}
MUTS = MUTS_BY_TARGET[_TARGET]

# ⓘ `_positional` is built (and judged: at most one) in the argv block at the top of the file — see `_refuse_argv`.
only = _positional[0] if _positional else None

# ⛔⛔ AND A LABEL PREFIX THAT SELECTS **NOTHING** IS REFUSED — HERE, the first moment the entry list is known, and so
#     BEFORE any lock, backup, build or mutation ([[B217]] follow-up, 2026-08-21). A mistyped `... ZZZ` used to select
#     zero entries, apply zero mutations and exit **0** reporting `mutations: 0 RED / 0 unusable` — ★ A GREEN VERDICT
#     ABOUT AN EMPTY RUN, which is precisely the silent-zero class the derived baseline above retires, arriving
#     through the other door: B217 zeroed the battery from the BASELINE side, this zeroed it from the SELECTION side,
#     and both exit like a run with nothing to report. ⇒ same rule as [[B235]]'s argv refusal: an instrument may not
#     accept an instruction it did not understand, and "successfully ran nothing" is not a result.
_sel = [_l for _l, _p, _r in MUTS if not only or _l.startswith(only)]
_sel_n = len(_sel)
if only and _sel_n == 0:
    _heads = sorted({_l.split()[0] for _l, _p, _r in MUTS})
    print(f"  ABORT no entry of the '{_TARGET}' battery matches the label prefix {only!r} — refused. A prefix that "
          f"matches nothing applies ZERO mutations, and exiting 0 on that reads as a battery with nothing to find.")
    print(f"  the {len(MUTS)} entry labels of '{_TARGET}' begin: {', '.join(_heads[:14])}"
          f"{' …' if len(_heads) > 14 else ''}")
    print("  No mutation was applied; nothing was built.")
    sys.exit(8)

# ★★ AND THE STALE-PIN BANNERS SAY WHICH RUN THEY ARE TALKING ABOUT ([[B217]] follow-up, 2026-08-21). ⚠ CORRECTED:
#    the first version of those banners said the battery "RUNS anyway, in full" UNCONDITIONALLY, so a supported
#    focused run (`MR_MUT_BASE=1,1 ... --target=model M115`) applied ONE mutation while the banner claimed a full
#    battery. ⛔ A gate banner that overstates its own coverage is the exact honesty class this whole fix serves —
#    the reader of a filtered run must not have to know the invocation to interpret the output. ⇒ the scope is
#    computed ONCE here, from `only` and the SELECTED entry count, and both banners quote it.
_SCOPE_NOW = (f"IN FULL (all {len(MUTS)} entries)" if not only else
              f"as a FILTERED run — only the {_sel_n} entr{'y' if _sel_n == 1 else 'ies'} matching '{only}', "
              f"of {len(MUTS)}")
_SCOPE_PAST = (f"RAN IN FULL ({len(MUTS)} entries)" if not only else
               f"ran ONLY THE REQUESTED SELECTION ({_sel_n} of {len(MUTS)} entries, matching '{only}')")

def md5(p):
    return _md5(p)

def run_suite():
    b = subprocess.run(["pio", "test", "-e", "native"], cwd=ROOT, capture_output=True, text=True)
    if "error:" in b.stdout + b.stderr:
        return None, (b.stdout + b.stderr)
    r = subprocess.run([os.path.join(ROOT, ".pio/build/native/program")], cwd=ROOT, capture_output=True, text=True)
    m = re.search(r"assertions: *(\d+) \| *(\d+) passed \| *(\d+) failed", r.stdout)
    c = re.search(r"test cases: *(\d+) \| *(\d+) passed \| *(\d+) failed", r.stdout)
    if not m or not c:
        return None, r.stdout[-800:]
    # (failed assertions, total cases, total assertions) — the FIRST is what the baseline gate gates on; the last two
    # are what it DERIVES this run's baseline from ([[B217]]; they used to be checked against a literal).
    return (int(m.group(3)), int(c.group(1)), int(m.group(1))), r.stdout

# ★ `--where` is a NO-OP DIAGNOSTIC: it prints the resolved source, its key and the keyed backup directory, and touches
#   nothing. It exists so "two worktrees do not share a backup" is a thing anyone can CHECK in one command rather than
#   read in a comment.
if "--where" in sys.argv[1:]:
    print(f"target     {_TARGET}   ({len(MUTS)} entries)")
    print(f"source     {Path(H).resolve()}")
    print(f"key        {_BK_KEY}")
    print(f"backup dir {_BK_DIR}")
    sys.exit(0)

if not take_lock():
    print(f"  ABORT another run already holds the lock for {Path(H).resolve()} ({_BK_LOCK}).")
    print("  Two runs mutating one file would each 'restore' what the other had just written. No mutation was applied.")
    sys.exit(3)
take_build_lock()      # ★ SECOND lock, different resource: the shared .pio/build/native (see its block above).
                       #   Taken AFTER the source lock, always in this order, so the pair cannot deadlock.
arm_backup()
orig = open(H).read()
base_md5 = md5(H)
ok = bad = 0
try:
    # ★★ THE BASELINE GATE, AND IT GATES ON **0 FAILED** — NOT ON A LITERAL (see PIN_CASES for why, [[B217]]).
    #    ⛔ Nothing below may run on a tree whose clean suite is RED: a RED verdict is only evidence if GREEN was the
    #    alternative. But the case/assertion COUNTS are this run's OWN measurement: they are derived here, printed,
    #    and then required again by the exit gate on the restored source.
    print(f"-- target {_TARGET}: {Path(H).resolve()} ({len(MUTS)} entries)", flush=True)
    print("-- baseline gate: the CLEAN tree must run 0 FAILED; its own counts become this run's baseline "
          f"(cross-check pin: {PIN_CASES} / {PIN_ASSERTS})", flush=True)
    base, base_out = run_suite()
    if base is None:
        print("  ABORT the clean tree does not build / did not run — no mutation was applied")
        for line in base_out.splitlines():
            if "error:" in line:
                print("        " + line[:160]); break
        sys.exit(2)
    base_failed, base_cases, base_asserts = base
    if base_failed != 0:
        # ⛔ THE LOUD ARM. It prints the FAILING OUTPUT and not merely a count, because the reader's next question is
        #   always "failing WHERE" and a run that answers it is a run nobody has to repeat.
        print(f"  ABORT the clean tree is RED — {base_cases} / {base_asserts} / {base_failed} FAILED. Every mutation "
              f"below would read RED for the wrong reason. No mutation was applied.")
        _fails = [ln for ln in base_out.splitlines()
                  if "ERROR:" in ln or "FATAL ERROR:" in ln or ln.startswith("TEST CASE:")]
        for line in (_fails[:24] or base_out.splitlines()[-24:]):
            print("        " + line[:200])
        sys.exit(2)
    BASE_CASES, BASE_ASSERTS = base_cases, base_asserts      # ★ DERIVED — this is the baseline, from here on.
    print(f"  ok   clean baseline {base_cases} / {base_asserts} / {base_failed}   (DERIVED from this run)", flush=True)
    if (PIN_CASES, PIN_ASSERTS) != (base_cases, base_asserts):
        # ★★ THE STALE CROSS-CHECK ARM — LOUD, AND IT CONTINUES. This is the whole of [[B217]]: the old code
        #    `sys.exit(2)`-ed here, having applied nothing, and that reads exactly like a battery with nothing to
        #    find. A moved figure is bookkeeping; it is not a reason to stop measuring.
        _pin_stale = ((PIN_CASES, PIN_ASSERTS), (base_cases, base_asserts))
        print("  " + "!" * 112, flush=True)
        print("  !!  STALE CROSS-CHECK PIN — the hand-pinned figure is NOT what this clean tree runs.", flush=True)
        print(f"  !!      pinned   {PIN_CASES} / {PIN_ASSERTS}"
              f"{'   (from MR_MUT_BASE)' if os.environ.get('MR_MUT_BASE') else '   (PIN_CASES, this file)'}", flush=True)
        print(f"  !!      derived  {base_cases} / {base_asserts}   <-- THE BATTERY BELOW USES THIS", flush=True)
        print(f"  !!  The battery RUNS anyway, {_SCOPE_NOW}: per [[B217]] a stale figure must NEVER zero a run.", flush=True)
        print("  !!  Then re-pin PIN_CASES / PIN_ASSERTS with the derivation (or unset MR_MUT_BASE).", flush=True)
        print("  " + "!" * 112, flush=True)

    for label, pat, rep in MUTS:
        if only and not label.startswith(only):
            continue
        hits = orig.count(pat)
        if hits != 1:
            print(f"  VACUOUS {label} — match count {hits} (must be exactly 1)", flush=True); bad += 1; continue
        # ★★ CALLERS 1 AND 2 OF THE ONE GUARDED WRITE (`guarded_write`). The INSTALL may only overwrite the recorded
        #    ORIGINAL — if the file is anything else, somebody edited it and this run stops rather than erasing them.
        #    The RESTORE may only overwrite our own MUTANT (or no-op on the original) — and the window it protects is
        #    the whole of `run_suite()` below, a full build plus test run, which is the longest window in the tool.
        mutant_text = orig.replace(pat, rep)
        note_mutant(_md5_text(mutant_text), label)   # record BEFORE installing — see note_mutant's ordering note
        guarded_write(mutant_text, f"installing {label}", allow=("original",))
        res, out = run_suite()
        guarded_write(orig, f"restoring after {label}", allow=("original", "mutant"))
        clear_mutant()
        if res is None:
            print(f"  UNUSABLE {label} — the mutant does not compile / did not run", flush=True)
            for line in out.splitlines():
                if "error:" in line:
                    print("        " + line[:160]); break
            bad += 1; continue
        failed, cases, asserts = res
        if failed > 0:
            print(f"  ok   {label} -> RED ({failed} assertion(s) failed, match count 1)", flush=True); ok += 1
        else:
            print(f"  FAIL {label} — the suite still PASSES; nothing measures this", flush=True); bad += 1
except KeyboardInterrupt:
    # A bare traceback here would bury the one fact a reader needs — WAS THE SOURCE PUT BACK? — so say it plainly.
    # ⚠ CORRECTED: this comment used to claim *"the `finally` below has already run by the time this prints"*. IT HAS
    #   NOT — Python runs this `except` body FIRST and the `finally` only afterwards, which is exactly why the explicit
    #   `restore()` below is here and not redundant: it is what makes the md5 line printed at the end of this block TRUE
    #   at the moment it is printed. `restore()` is idempotent (it no-ops when the md5 already matches), so the `finally`
    #   running second changes nothing. ⓘ Same family as a comment asserting a `-Wswitch` guard the code did not have.
    print("\n  INTERRUPTED — restoring the real source now, before this exits", flush=True)
    restore("SIGINT")
    disarm()
    print(f"source restored: md5 {md5(H)} ({'MATCHES' if md5(H) == base_md5 else 'DIFFERS — FAIL'})")
    sys.exit(130)
finally:
    # ⛔ THE ONE THING THIS BLOCK EXISTS FOR: the real source goes back whatever happened above.
    restore("finally")
    disarm()

after = md5(H)
print(f"source restored: md5 {after} ({'MATCHES' if after == base_md5 else 'DIFFERS — FAIL'})")

# ★★ THE EXIT GATE, AND IT EXISTS BECAUSE RESTORING THE SOURCE IS NOT THE WHOLE JOB: the binary left in
#    `.pio/build/native/` is the LAST MUTANT'S, so anyone running `./.pio/build/native/program` immediately after this
#    tool saw a spurious failure on a correct tree. (Observed — it fooled its own author once.) ⇒ rebuild once on the
#    restored source and require THIS RUN'S DERIVED baseline again (it was the pinned literal before [[B217]]; the
#    check is strictly stronger derived, because the figure it demands was measured on this very tree minutes ago
#    rather than typed by hand weeks ago). That refreshes the artefact AND proves the restoration is effective at the
#    BUILD level, not merely at the file level.
exit_ok = True
if after == base_md5:
    res, out = run_suite()
    if res is None or res != (0, BASE_CASES, BASE_ASSERTS):
        got = "did not build/run" if res is None else f"{res[1]} / {res[2]} / {res[0]}"
        print(f"  FAIL the restored tree does not rebuild to {BASE_CASES} / {BASE_ASSERTS} / 0 (got {got})")
        exit_ok = False
    else:
        print(f"exit gate: the restored tree rebuilds to {res[1]} / {res[2]} / {res[0]} — the stale mutant binary is gone")
print(f"mutations: {ok} RED / {bad} unusable")
# ★★ AND THE STALE-PIN BANNER IS RE-ANNOUNCED HERE, because a warning printed before ~300 lines of build output has
#    scrolled out of the reader's window by the time the verdict lands — "loud" that only holds at the top of a long
#    run is not loud. ⛔ It still does not touch the exit code: see PIN_CASES.
if _pin_stale:
    print("  " + "!" * 112)
    print(f"  !!  REMINDER — the cross-check pin is STALE: pinned {_pin_stale[0][0]} / {_pin_stale[0][1]}, this tree "
          f"runs {_pin_stale[1][0]} / {_pin_stale[1][1]}.")
    print(f"  !!  The battery above {_SCOPE_PAST} on the derived figure ([[B217]]); re-pin PIN_CASES / PIN_ASSERTS.")
    print("  " + "!" * 112)
sys.exit(0 if bad == 0 and after == base_md5 and exit_ok else 1)
