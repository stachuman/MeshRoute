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

★★★ AND SINCE 2026-08-24 THE BATTERY RUNS IN **SCRATCH TREES, IN PARALLEL — THE REAL TREE IS NEVER TOUCHED.**
⛔⛔ THAT IS THE HEADLINE, AND IT IS A SAFETY CHANGE BEFORE IT IS A SPEED ONE. Every defect tabulated at
`guarded_write` shares one precondition: THIS TOOL USED TO EDIT AND BUILD THE OWNER'S LIVE WORKING TREE. Remove the
precondition and the whole family stops being reachable:
  · defect 3 (a stale MUTANT BINARY left in `.pio/build/native`) is **DELETED, NOT MOVED** — the real tree is never
    built by this runner at all, so its binary is never a mutant's, and the exit-gate rebuild that used to refresh it
    has nothing left to refresh. (In a scratch tree the same gate would prove restoration at the BUILD level — but the
    tree is deleted seconds later and nothing ever runs its binary, so the proof would be about a corpse. What
    replaces it is the REAL-TREE FINGERPRINT: every target file's md5 taken at launch and required again at the end.)
  · the edit-a-target-mid-battery incident, and the sibling session whose board build wiped `.pio/build/native`
    underneath a running battery, both become impossible: neither the source nor the build directory is shared.
★ THE SHAPE. A parent ORCHESTRATOR (no lock, no backup, no build — it never writes the repository) rsyncs the WORKING
  tree — ⛔ never `git archive`: this tree is DIRTY BY DESIGN and an uncommitted slice is the normal state — into one
  scratch tree per worker, shards the selected entries across them, and re-execs THIS SAME FILE inside each tree as a
  WORKER. A worker is the serial runner, unchanged: `ROOT` derives from `__file__`, so a worker's every guard —
  the per-path backup, the INFLIGHT marker, the source lock, the build lock, `guarded_write`'s three arms, the derived
  baseline, the md5-verified restore — keys itself to ITS OWN tree with no code change at all. ⇒ `--workers 1` is the
  EXACT pre-2026-08-24 serial path (in a scratch tree), and is kept as the reference implementation the parallel
  default is proved against.
⛔ A SHARD THAT DOES NOT REPORT IS A FAILURE, NEVER A PASS ([[B227]]/[[B237]]: a harness whose own plumbing launders
  "did not measure" into "measured"). The parent knows the exact index list it handed out; every index must come back
  named with a verdict, or the run prints the missing ones and exits 9. A worker writes its result file after EVERY
  entry (atomically), so a `kill -9` mid-entry loses one entry and reports the rest — and the parent says which.
⛔⛔ COMPILER CACHE — AND THE MEASUREMENT SAYS IT DOES NOTHING FOR THIS RUNNER, WHICH IS NOT WHAT THE PLAN ASSUMED.
  `tools/ccache_native.py` routes the native env's host compiles through ccache in the user-default `CCACHE_DIR`, and
  the assumption was that scratch trees after the first would build warm. MEASURED 2026-08-24, on this tree:
    · WITHIN one tree it works exactly as hoped — a mutate-build is 10.3 s / 0-of-8 hits (novel content, nothing to
      hit), and the following build of RESTORED or ALREADY-SEEN content is 2.0 s / 8-of-8 hits.
    · ACROSS trees it is **0 % warm, always**: a second scratch tree cold-builds in 17.0 s with 74/74 MISSES, and a
      REPEAT of the same 10-entry battery scores 0/524 hits. Cause, verified rather than guessed: `-g` (the native
      env is `build_type = debug`) makes ccache hash the build directory (`hash_dir`), and the absolute `-I`/source
      paths differ per tree. `CCACHE_BASEDIR` ALONE DOES NOT FIX IT (0/74, still 16.3 s).
  ⇒ ★ SINCE EVERY RUN NOW BUILDS IN FRESH SCRATCH PATHS, THE CACHE NEVER HITS DURING A BATTERY. The wiring still
    earns its keep for the owner's OWN `pio test -e native` in the real tree, whose path is stable — but no battery
    run, first or repeat, is faster for it.
  ⓘ THE FIX WAS MEASURED FIRST, THEN ★ APPLIED BY OWNER RULING 2026-08-24 (the safe shape exactly): a
    BATTERY-PRIVATE persistent cache (`~/.cache/meshroute-battery-ccache` — ⛔ never `~/.ccache`, so
    NOHASHDIR-relativized objects can never reach the owner's real builds' debug info) + `CCACHE_BASEDIR=<worker
    tree>` + `CCACHE_NOHASHDIR=1`, injected ONLY into the worker environment at the spawn site (search
    `BATTERY-PRIVATE COMPILER CACHE`). Measured before ruling: 74/74 cross-tree hits, a repeat of the 10-entry
    sample 75.8 s -> **11.5 s (524/524 hits, byte-identical verdicts)**. The owner's own `pio test -e native`
    in the real tree still uses the default `~/.ccache` via `tools/ccache_native.py`, untouched.

USAGE:  python3 tools/probe_ui_model_mutations.py                    # the model battery (the default target)
        python3 tools/probe_ui_model_mutations.py M07                # one entry, by its label prefix
        python3 tools/probe_ui_model_mutations.py --target=config     # the §UI-13 config-service battery
        python3 tools/probe_ui_model_mutations.py --target=config C05 # one entry of it
        python3 tools/probe_ui_model_mutations.py --workers=1         # the serial reference path (one scratch tree)
        python3 tools/probe_ui_model_mutations.py --where            # print the resolved source + its keyed backup dir, do nothing
  env   MR_MUT_SCRATCH=<dir>   put the scratch trees under <dir> (default: `TMPDIR`); they are removed on exit
        MR_MUT_KEEP_SCRATCH=1  keep them instead, for post-mortem
"""
import atexit, fcntl, hashlib, json, os, re, shutil, signal, subprocess, sys, tempfile, threading, time
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
    # ★★ ADDED 2026-08-25 BY §UI-10/11 P1, and for the reason every target above it was added: the preset catalog's
    #    OWNER-RULED policy is a set of clauses — the four storage states, the canonical record bytes, the coalescing
    #    limit, the emergency invariants, the `busy` table, the candidate-then-live order — each of which must be
    #    attacked ON ITS OWN, and a battery is per-SOURCE-FILE. Bound in `src/firmware_commands.cpp` (P2's verb) or in
    #    `src/fw_main.cpp` (the boot line) they would have had NO battery at all: neither TU is compiled by the native
    #    suite or the simulator (§B115). ⓘ The RECORD's own five entries live in `--target=devicenv` (N21-N25), where
    #    the record is — including the only reddenable form of *"the catalog written through `/mrcfg`"*.
    "uipresets":   "src/firmware_ui_presets.h",     # §UI-10/11 P1 — the /mrui catalog service and its whole policy
    # ★★ ADDED 2026-08-25 BY §UI-10/11 P2, and for the reason every target above it was added: the VERB family's
    #    decisions — the three NDJSON records' BYTES, the six reason spellings, `list` = 17-including-disabled, the
    #    result->output rule, the `loc=` domain and the boot diagnosis — are each a published contract clause that
    #    must be attacked ON ITS OWN, and a battery is per-SOURCE-FILE. Bound in `src/firmware_commands.cpp` (the
    #    dispatch arm, the Print adapter, the ONE instance) they would have had NO battery at all: that TU is
    #    compiled by neither the native suite nor the simulator (§B115). ⓘ The `busy` CLASSIFICATION is deliberately
    #    NOT here — it lives in `src/firmware_ui.cpp` and its controls are `tools/probe_firmware_ui/run.sh`'s
    #    C134-C136, because that is the only instrument that compiles that file.
    "uipresetverbs": "src/firmware_ui_preset_verbs.h",  # §UI-10/11 P2 — the grammar, the three records, the boot line
    "uiinvite":    "src/firmware_ui_invite.h",      # §UI-16 N4 — the two authorities, the handled set, the row
    "uinearby":    "src/firmware_ui_nearby.h",      # §UI-16 N2 — the own-team filter, the order, the rows, the lexemes
    "uinearbyrow": "src/firmware_ui_nearby_row.h",  # §UI-16 N2 — the `n/3` tier map + the fingerprint/age row (S-6/S-7)
    "teamseen":     "lib/core/team_seen_ring.h",  # §UI-16 N1 — the pure nearby-team ring: de-dup, EWMA, retention, order
    "teamseensite": "lib/core/node_beacon.cpp",   # §UI-16 N1 — the ONE write site: eligibility + the read-only discipline
    # ★★ ADDED 2026-08-24 BY §UI-16 N6b, and for the reason every target above it was added: the grant's DISPATCH
    #    RESULT — *actually queued* vs *actually parked* vs *an admission refusal* vs *a pre-admission failure* — is
    #    a four-way mapping of facts the send path computes, and EVERY ONE of the four used to be returned as
    #    `queued`. Each collapse must be attackable ON ITS OWN, and a battery is per-SOURCE-FILE. ⓘ ONE target and
    #    not three: the two FACTS (`node_mac.cpp`'s admission, `node_hashlocate.cpp`'s park) are single-expression
    #    reports of an existing `if`, while the DECISIONS — what each fact is called, and whether the correlation
    #    terms are published at all — all live in `team_key_grant_send`.
    "teamgrant":    "lib/core/node.cpp",          # §UI-16 N6b — the grant's explicit dispatch result + its two terms
    # ⓘ ...AND THE TWO **FACT** SITES GET ONE ENTRY EACH, in their own files, because a battery is per-SOURCE-FILE
    #   and the facts are where the collapse ORIGINATES: `teamgrant` above attacks what each fact is CALLED, these
    #   two attack whether the fact is TOLD AT ALL. One `if`'s own answer each, at match count 1.
    "grantadmit":   "lib/core/node_mac.cpp",      # §UI-16 N6b — enqueue_data's TX-queue admission fact
    "grantpark":    "lib/core/node_hashlocate.cpp",  # §UI-16 N6b — park_send's stored-or-dropped fact
    # B161 spans the two raw-answer producers, their deferred consumers and the one hybrid identity authority.
    # Keep one target per source file: the runner's mutation/restore/build guards are file-keyed.
    "b161hash":     "lib/core/node_hashlocate.cpp",
    "b161rx":       "lib/core/node_mac_rx.cpp",
    "b161mac":      "lib/core/node_mac.cpp",
    # B251 spans receive-flight authority/admission and the bounded reverse-ACK ledger. Kept separate from B161 so
    # each slice can be re-proved independently even though two targets share the same production files.
    "b251rx":       "lib/core/node_mac_rx.cpp",
    "b251hash":     "lib/core/node_hashlocate.cpp",
    # ★★ ADDED 2026-08-28 BY §B20/B21, and for the reason every target above it was added: the slice's decisions —
    #    WHICH bound sizes a sealed inner, WHICH of the two bounds wins when they disagree, WHICH of the two
    #    conditions the DST_HASH guard is looking at, and WHETHER each arm reports at all — are each a ruled
    #    decision a mutation must reach ON ITS OWN, and a battery is per-SOURCE-FILE. Kept separate from
    #    `b161mac`/`grantadmit` (same production file) so this slice can be re-proved independently.
    # ⚠ TWO targets because the LENGTH AUTHORITY and its CONSUMER genuinely live in two files: `frame_codec.h`
    #   owns the formula (what a DATA frame costs, what an inner may be), `node_mac.cpp` owns the decision to ask
    #   it. A mutation of the formula and a mutation of the asking are different defects and must not share entries.
    "b20mac":       "lib/core/node_mac.cpp",     # §B20/B21 — the seal's cap + the DST_HASH guard's two conditions
    "b20codec":     "lib/core/frame_codec.h",    # §B20 — data_frame_len/data_inner_cap, the one length authority
    # ★★ ADDED 2026-08-28 BY [[B159]] (the dedup-vs-retry-horizon correction round). FOUR targets because the
    #    slice's decisions genuinely live in four files and a battery is per-SOURCE-FILE: the RETENTION derivation
    #    (`protocol_constants.h`), the DEADLINE PREDICATE and its doorstep call site (`node_cascade.cpp`), the two
    #    scheduler call sites that made the horizon unbounded (`node_mac.cpp`), and the DEDUP MECHANISM the
    #    retention feeds (`node_mac_rx.cpp`). Kept separate from `b251rx`/`b20mac` (same production files) so this
    #    slice can be re-proved independently, exactly as those entries were.
    "b159const":    "lib/core/protocol_constants.h",   # [[B159]] — retention = enforced deadline + measured margin
    "b159dl":       "lib/core/node_cascade.cpp",       # [[B159]] — gateway_deadline_expired + the doorstep call
    "b159mac":      "lib/core/node_mac.cpp",           # [[B159]] — the start boundary + the window-defer arm
    "b159rx":       "lib/core/node_mac_rx.cpp",        # [[B159]] — the _seen_origins dedup the retention feeds
    "b159hal":      "lib/hal/device_hal.cpp",          # [[B159]] — the PHYSICAL-START deadline in pump_tx
    "b159map":      "lib/core/node.cpp",               # [[B159]] — the expired->terminal give-up mapping
    # ★★★ ADDED 2026-08-29 BY §A0 (the custody spec's characterization + audit slice), for the reason every target
    #    above it was added, and with one extra constraint this slice does NOT share with them: **A0 changes no
    #    production code at all** (C1 — the slice IS the no-change). So these three targets exist purely to prove
    #    that `test/test_data_type_audit_a0.cpp`'s characterization can FAIL for the behaviour it claims — i.e.
    #    that the pins are instruments and not decoration. Spec §18.0.4.
    # ⚠ THE MUTATED LINES ARE EXACTLY THE ONES SLICES A/B WILL REWRITE. When those slices land, these entries go
    #   VACUOUS (match count 0) BY DESIGN — that is the transition being visible, not the battery rotting. Re-anchor
    #   them onto the trait authority in the slice that introduces it; do not delete them silently.
# ⛔⛔ RETIRED 2026-08-30 BY **§CUSTODY-B**, AND THE RECORD IS KEPT RATHER THAN THE ENTRIES DELETED SILENTLY.
#     A01/A02/A03 attacked the TWO HAND-COPIED exemption lists in `become_free` / `issue_send`. Those lists no
#     longer exist: §CUSTODY-B replaced both with the single `data_type_traits(t).internal` authority, so all
#     three anchors match ZERO times and the harness correctly calls them VACUOUS.
#     ⇒ THE THREE DECISIONS DID NOT LOSE THEIR CONTROLS; THEY MOVED, and each one maps:
#         A01 (drop REMOTE_CMD from the CHECK list)     -> `sliceBmac` B01 (revert the CHECK half wholesale)
#         A03 (drop E2E_ACK from the STAMP list only)   -> `sliceBmac` B02 (revert the STAMP half wholesale)
#         A02 (widen the exempt set to a hash answer)   -> ★ NOT a mutation any more: it IS the shipped
#                                                          behaviour (§6.2(4) widened the set to 0x80..0xBF),
#                                                          and `test_data_type_audit_a0.cpp` §A0-2 was
#                                                          re-anchored to assert exactly that.
#     ⚠ AND B02 REPEATED A01'S OWN LESSON ONE SLICE LATER: it measured NOTHING on its first run, because §A0-2b
#       probed only types that BOTH list versions exempt. The fix was to the TEST (§A0-2b now loops the
#       newly-exempt types too), never to the mutation — the second time this exact hole opened in this exact
#       policy, and the reason the note above it is written at that length.
    "a0rx":         "lib/core/node_mac_rx.cpp",        # §A0 — the addressed if-chain's MISSING default arm
    "a0codec":      "lib/core/frame_codec.cpp",        # §A0 — the codec's total permissiveness about the TYPE byte
    # ★★ ADDED 2026-08-29 BY **§CUSTODY-A** (the DATA-namespace transition). Four targets, one decision each:
    #    the range/trait AUTHORITY, the one renumbered value a consumer names, the store's semantic version, and
    #    the companion encoder's [[B265]] literal.
    # ⛔⛔ TWO OF THE BRIEF'S CONTROLS ARE **DELIBERATELY ABSENT FROM THIS TABLE**, and their absence is the
    #    stronger outcome rather than a gap — recorded here so it is never read as an omission:
    #      · REVERTING AN ENUM MEMBER to its old ordinal (e.g. E2E_ACK 0x80 -> 3) does not COMPILE. §18.1.1's
    #        static assertions in `test/test_data_type_namespace.cpp` refuse it, and `data_type_traits`'s own
    #        switch refuses it a second time as a DUPLICATE CASE VALUE (3 is SEALED_RELAY now). A mutant that
    #        cannot build is "unusable" to this battery — so the control lives at the COMPILER, where it is
    #        absolute, and `sliceAinbox`/S05 below carries the runtime-measurable half (a CONSUMER reverting to
    #        the literal 3).
    #      · BUMPING `protocol::wire_version` likewise does not compile (§18.1.6's static_assert names the owner
    #        ruling in its failure text). Same trade, same reason.
    #      · A `frame_trace.h` CASE LABEL reverted to numeric compiles AND runs green everywhere: that header is
    #        `#if defined(ARDUINO)`, so NEITHER native NOR the simulator ever sees it (the A0-F2 blindness class,
    #        one file along). Its standing control is the structural search `tools/check_data_type_literals.py`,
    #        whose `--selftest` reintroduces exactly that form and requires the search to reject it.
    "sliceAcodec":  "lib/core/frame_codec.h",          # §CUSTODY-A — the range predicate + the ONE trait authority
    "sliceAinbox":  "lib/core/inbox.cpp",              # §CUSTODY-A — the one production site that names E2E_ACK's value
    "sliceAstore":  "lib/core/segmented_inbox_store.h",# §CUSTODY-A — the v4->v5 semantic bump and its wipe arm
    "sliceAjson":   "lib/console/console_json.cpp",    # §CUSTODY-A — [[B265]]'s numeric literal, closed by this slice
    # ★★ ADDED 2026-08-30 BY **§CUSTODY-B** (common internal behaviour). THREE targets, because a battery is
    #    per-SOURCE-FILE and the slice's decisions live in three files: the DM-floor authority + the origination
    #    lifecycle gate (node_mac.cpp), the fail-closed guard and its PLACEMENT relative to the three forwarding
    #    roles (node_mac_rx.cpp), and the terminal give-up (node_cascade.cpp).
    # ⛔ THE PLACEMENT MUTATIONS ARE THE POINT AND THEY ARE NOT SUBSTITUTABLE BY A "GUARD DROPPED" ENTRY: a guard
    #    that exists but sits one branch too early is a DIFFERENT defect from a missing one, it passes every
    #    "unknown internal is dropped" assertion, and it silently eats a hosted mobile's traffic. B06/B07 attack
    #    exactly that by INSERTING a correctly-written guard at a wrong place.
    "sliceBmac":    "lib/core/node_mac.cpp",           # §CUSTODY-B — both DM-floor halves + the origination gate
    "sliceBrx":     "lib/core/node_mac_rx.cpp",        # §CUSTODY-B — the fail-closed guard, its predicate + placement
    "sliceBcascade":"lib/core/node_cascade.cpp",       # §CUSTODY-B — the terminal give-up's lifecycle gate
    # ★★ ADDED 2026-08-30 BY [[B268]] (owner ruling (b)): the grant's OWN outcome pushes live in `node.cpp`'s
    #    TxDone attribution, and a battery is per-SOURCE-FILE. Without its own target the ruling's four
    #    required controls would have had nowhere to live.
    "sliceBnode":   "lib/core/node.cpp",               # [[B268]] — team_key_grant_aired + its correlation
    "sliceBchannel":"lib/core/node_channel.cpp",       # [[B268]] blocker-1 — the two reprovision-purge deaths
    # ★★ ADDED 2026-08-28 BY [[B134]] (the durable ESP32/Heltec inbox), for the reason every target above it was
    #    added: the slice's decisions must be attacked ONE AT A TIME and a battery is per-SOURCE-FILE.
    # ⓘ `b134seam` is a `src/` HEADER that the native suite compiles because `test/test_device_inbox_fs_esp32.cpp`
    #    includes it (the env carries `-I src`) — its ESP32 arm compiles out off-Arduino while all five DECISIONS,
    #    and the `SegmentStoreOver` template that composes them, stay host-reachable. That is exactly why the
    #    store is a template: the battery attacks the SHIPPED class, not a lookalike.
    # ⚠ THREE targets and not one: the seam's own decisions, the two behaviours the REUSED `lib/core` ring logic
    #   had to gain to become a device store, and the platform-neutral delete contract this slice CLAIMS holds
    #   unchanged across the new backend. Kept separate from every other target on the same files.
    # ⛔ RE-AIMED 2026-08-29 BY [[B260]], NOT REWRITTEN. `SegmentStoreOver` + D1..D6 moved VERBATIM out of
    #   `src/device_inbox_fs_esp32.h` into `src/device_inbox_seam.h` when the nRF52 twin was retired onto the same
    #   seam (two platforms, so the shared half stopped belonging to either file). The entries below are UNCHANGED;
    #   only the file they are applied to moved with them — which is what keeps [[B134]]'s eight rounds MEASURED
    #   across the move instead of re-asserted. `b134nvs` is the ESP32-only residue that stayed behind (D7).
    "b134seam":     "src/device_inbox_seam.h",         # [[B134]] — the six shared seam decisions + the shared mount
    "b134nvs":      "src/device_inbox_fs_esp32.h",     # [[B134]] — D7, the NVS blob lookup's esp_err_t classifier
    # ★★ ADDED 2026-08-29 BY [[B260]] (retiring the hand-maintained nRF52 inbox twin). ONE new target, and only
    #    one, because the retirement means the nRF52 path adds exactly TWO decisions of its own — the InternalFS
    #    meta load's three-valued verdict (N1) and its checked commit (N2) — while every other property it now has
    #    is the SHARED code `b134seam`/`b134store`/`b134inbox` already attack. ⓘ That is the measurement of the
    #    slice: a platform that used to need its own copy of the ring now needs two classifiers.
    "b260":         "src/device_inbox_fs_nrf52.h",     # [[B260]] — N1 the meta verdict, N2 the meta commit
    "b134store":    "lib/core/segmented_inbox_store.h",# [[B134]] — wipe() and the read-cursor wear coalescing
    "b134inbox":    "lib/core/inbox.cpp",              # [[B134]] — the tombstone contract, re-proved on the new backend
    # ⓘ ADDED 2026-08-29 (QG round 2): the RAM store gained a REAL `wipe()`, because inheriting the base's
    #   successful no-op was the same data-retention lie in miniature — `prep-restart` HALTS without rebooting,
    #   so between the verb and the power cycle a "cleared" inbox still streamed every record.
    "b134ram":      "lib/core/fixed_inbox_store.h",   # [[B134]] — the volatile ring's wipe contract
    # ⓘ ADDED 2026-08-29 (QG round 7): `handle_mark_read` lives in a TU neither the native suite nor the simulator
    #   compiles (§B115), so the bool -> lexeme decision it makes was hoisted into `console_json.h` — where a
    #   battery CAN reach it. The target exists so "the ack can only ever say success" is a reddenable claim.
    "b134ack":      "lib/console/console_json.h",    # [[B134]] — the mark_read ack's result mapping
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
# ⓘ The last three are INTERNAL: the orchestrator's own re-exec of this file inside a scratch tree. They are listed
#   here (rather than special-cased) because [[B235]]'s rule is about the ARGUMENT VECTOR, not about who typed it —
#   a worker invocation this parse could not name would be as unmeasurable as an operator's typo.
_KNOWN_FLAG_PREFIXES = ("--target=", "--workers=", "--shard-id=", "--shard-entries=", "--shard-result=")
def _refuse_argv(what, why):
    print(f"  ABORT {what} — refused. {why}")
    print(f"  known arguments:  --target=<{'|'.join(sorted(TARGET_SRC))}>")
    print("                    --workers=<N>           (parallel scratch-tree workers; 1 = the serial reference)")
    print("                    --where                 (print the resolved source + backup dir, do nothing)")
    print("                    <ENTRY-LABEL PREFIX>    (at most ONE, positional, e.g. M07 — runs just those entries)")
    print("                    --shard-id/--shard-entries/--shard-result   (INTERNAL — the orchestrator's workers)")
    print("  known environment: MR_MUT_BASE=\"cases,asserts\"  (the OPTIONAL clean-baseline cross-check)")
    print("                     MR_MUT_SCRATCH=<dir>          (where the scratch trees go; default TMPDIR)")
    print("                     MR_MUT_KEEP_SCRATCH=1         (keep them for post-mortem)")
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


# ===== ★★ THE PARALLEL LAYER'S ARGUMENTS, AND WHICH SIDE OF THE FORK THIS PROCESS IS ON ============================
# ⛔ EXACTLY ONE OF TWO ROLES, decided here and never ambiguous: with `--shard-entries` this process is a WORKER (it
#   owns the tree it was started in, and everything below `take_lock()` is its unchanged serial body); without it, it
#   is the ORCHESTRATOR (it owns no lock, arms no backup, compiles nothing, and never writes the repository).
def _usable_cores():
    """The CPUs this process may actually be scheduled on — `sched_getaffinity`, not `cpu_count`.

    ⚠ MEASURED 2026-08-24, and it is why this is not the `(physical id, core id)` dedup it was first written as:
      this host is a VM whose topology is PARTLY SYNTHETIC. `lscpu` claims 48 CPUs / 24 cores / 2 threads, while
      `/proc/cpuinfo` lists 24 processors carrying only **18 distinct `(physical id, core id)` pairs**. A dedup over
      those pairs therefore answers 18 for a 24-vCPU machine — it UNDERSTATES the box, and it would understate a
      cgroup-limited container by more. ⇒ ★ ask the scheduler what it will actually give us, and let the hard cap
      below (6) do the reserving; the hyperthread question this dedup was trying to answer is one no VM answers
      honestly, and at a cap of 6 it cannot change the result anyway.
    """
    try:
        return len(os.sched_getaffinity(0))
    except (AttributeError, OSError):
        return os.cpu_count() or 1


# ★ THE DEFAULT IS `min(usable cores - 2, 6)`, and the cap is deliberate rather than shy: each worker's `pio` build is
#   ITSELF parallel, so the workers do not divide an idle machine between them — two cores are left for the session
#   that launched the run, and 6 is the point past which this tree's 8-TU native build stops being the bottleneck.
#   ⇒ raise it only on a MEASUREMENT, never on the core count alone.
_WORKERS_DEFAULT = max(1, min(_usable_cores() - 2, 6))
_WORKERS = _WORKERS_DEFAULT
_SHARD_ID = None
_SHARD_ENTRIES = None
_SHARD_RESULT = None
for _f in _flags:
    if _f.startswith("--workers="):
        _v = _f.split("=", 1)[1]
        if not _v.isdigit() or int(_v) < 1:
            _refuse_argv(f"--workers={_v!r} is not a positive integer",
                         "A worker count this tool cannot read is a shard layout nobody can check.")
        _WORKERS = int(_v)
    elif _f.startswith("--shard-id="):
        _SHARD_ID = _f.split("=", 1)[1]
    elif _f.startswith("--shard-entries="):
        _SHARD_ENTRIES = [int(x) for x in _f.split("=", 1)[1].split(",") if x != ""]
    elif _f.startswith("--shard-result="):
        _SHARD_RESULT = _f.split("=", 1)[1]
_IS_WORKER = _SHARD_ENTRIES is not None
if _IS_WORKER and (_SHARD_ID is None or _SHARD_RESULT is None):
    _refuse_argv("--shard-entries without --shard-id/--shard-result",
                 "A shard that cannot report where its verdicts go would be a silently missing shard.")

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
PIN_CASES, PIN_ASSERTS = 2376, 100740    # ★★ CROSS-CHECK RE-SYNCED 2026-08-30 by **§CUSTODY-B**, and the
                                         # DERIVATION IS RECORDED RATHER THAN THE NUMBER PASTED: a pristine
                                         # `git archive HEAD` build measured 2351/100374 (reproducing the
                                         # value below EXACTLY); the slice adds +11 cases / +220 assertions =
                                         # +216 from the new TU `test/test_custody_internal_b.cpp` (12 cases) and the four [[B266]] coverage cases in test_dual_layer.cpp
                                         # and +20 from re-anchoring three A0 characterizations in place
                                         # (+15 §A0-2 derivation cross-check, +5 §A0-2c's new INTRO row incl.
                                         # its fixture's two on_init CHECKs, +3 §A0-4's range split) minus
                                         # the four grant-side push assertions that INVERTED (-3, net).
                                         # PIN_CASES, PIN_ASSERTS = 2351, 100374 — ★★ RE-SYNCED 2026-08-29 by **§CUSTODY-A** (the
                                         # DATA-namespace transition), ON TOP OF the §A0 re-sync recorded
                                         # below and WITHOUT disturbing it. DERIVATION, written out:
                                         #   base (§A0 clean)          2343 /  98966
                                         #   §A0 cases RE-ANCHORED       +0 /     -1
                                         #     · §A0-1  rewritten for the range contract   96 ->  81 (-15)
                                         #     · §A0-1b kAllocatedTypes gained APP_MESSAGE 20 ->  21 (+1)
                                         #     · §A0-4  0x80 left the unknown list (it is
                                         #              E2E_ACK now); 0x81 + APP_MESSAGE
                                         #              joined it: 10 -> 11 cases         130 -> 143 (+13)
                                         #   test_data_type_namespace.cpp (NEW)  +6 cases / +1368 asserts
                                         #     786 + 256 + 155 + 4 + 165 + 2 = 1368
                                         #   test_segmented_inbox_store.cpp v4->v5  +2 cases / +41 asserts
                                         #     (the CONTROL 10 + the upgrade 31)
                                         #   => 2343 + 8 = 2351 · 98966 - 1 + 1368 + 41 = 100374 ✓
                                         # ⓘ the §A0 note below is the PRIOR link in the chain, preserved:
                                         # ★★ CROSS-CHECK RE-SYNCED 2026-08-29 by **§A0** (the custody spec's
                                         # characterization + audit slice), ON TOP OF the [[B260]] re-sync
                                         # recorded immediately below and WITHOUT disturbing it:
                                         # **2333 / 98448 -> 2343 / 98966 = +10 cases / +518 assertions**.
                                         # ⓘ RE-SYNCED A SECOND TIME THE SAME DAY, assertions only
                                         #   (98939 -> 98966, +27, cases UNCHANGED at 2343): the QG A0
                                         #   review's blocker 1 rewrote §A0-4c in place — it gained a real
                                         #   POSITIVE arm (an unknown type WITH E2E_ACK_REQ is delivered AND
                                         #   acked) beside the retained flagless negative control — and the
                                         #   matrix correction added outer type 18 to §A0-4's probes.
                                         # ⛔ with NO existing case edited and NO existing case removed — A0 adds
                                         # ONE new file (`test/test_data_type_audit_a0.cpp`) and changes zero
                                         # production code (C1: the slice IS the no-change).
                                         # DERIVED, all ten in that one new file:
                                         #   §A0-1  the 19-member numbering pinned contiguous + member-by-member
                                         #   §A0-1b the 0xFE tombstone collides with no allocated DataType
                                         #   §A0-2  the DM floor's exempt set is EXACTLY three types (15 rows)
                                         #   §A0-2b an exempt origination lays no stamp (the STAMP half alone)
                                         #   §A0-2c with the floor ARMED, an exempt type still flies (the CHECK
                                         #          half alone) — ★ ADDED BECAUSE MUTATION A01 MEASURED NOTHING
                                         #          WITHOUT IT; see that entry's note in MUTS_A0MAC
                                         #   §A0-3  pack/parse round-trip 14 type bytes incl. 0xFE / 0xFF
                                         #   §A0-3b APP=1 with a 0x00 TYPE byte parses as type 0
                                         #   §A0-4  an addressed UNKNOWN type is delivered as an ordinary DM
                                         #   §A0-4b control — a CONSUMED type is not delivered
                                         #   §A0-4c no E2E_ACK_REQ => no ack, whatever the type
                                         # ⓘ The §A0-4* cases drive the REAL two-node RTS/CTS/DATA/ACK exchange
                                         #   through the PUBLIC API only (`on_recv` + `on_timer`), because adding
                                         #   a `friend` to `node.h` would have been the production edit C1 forbids.
                                         # ⓘ THE PRE-A0 VALUE, retained for the chain:
                                         # PIN_CASES, PIN_ASSERTS = 2333, 98448 — ★★ RE-SYNCED 2026-08-29 by **[[B260]]** (retiring the
                                         # hand-maintained nRF52 inbox twin), ON TOP OF [[B134]]'s re-sync
                                         # recorded below and WITHOUT disturbing it: **2314 / 98383 -> 2333 /
                                         # 98448 = +19 cases / +65 assertions**, ⛔ with NO existing case edited
                                         # and NO existing case removed — the slice DELETES a source file
                                         # (`src/device_inbox_store.h`) that no host test ever compiled, so
                                         # nothing it carried was being measured to begin with.
                                         # DERIVED — all of it in the NEW `test/test_device_inbox_fs_nrf52.cpp`:
                                         #   +9 / 24 for N1, the InternalFS meta load's THREE-VALUED verdict —
                                         #     a backend refusal is never `absent` (and the ORDER that makes
                                         #     that true), the one real absence, the exact-length `loaded`, the
                                         #     ★ MIGRATION verdict (the retired twin's 24-byte v6 blob under
                                         #     the shared 28-byte v4 Meta = `error`), the over-length prefix
                                         #     hazard, a negative CTZ rc, and the SIX-arm composition with
                                         #     `mrnv::fs_read_slot` that proves the [[B218]] branch order and
                                         #     N1 compose (mount fail / metadata error / open fail / NOENT /
                                         #     short / long).
                                         #   +8 / 17 for N2, the checked meta COMMIT — a clean save, the
                                         #     ★★ COMPLETE-write-whose-SYNC-FAILED fault that a void
                                         #     `File::flush()`/`close()` cannot report, a short write, a write
                                         #     that REPORTS n while committing less, a ★★★ STALE blob of the
                                         #     RIGHT LENGTH (the one fault only `w == n` sees — this case exists
                                         #     BECAUSE the battery said N10 measured nothing), an over-long
                                         #     file, a failed close, a failed open, and the truncate proof.
                                         #   +2 / 24 END-TO-END through the REAL `SegmentedInboxStore` over the
                                         #     fake flash: the ★★★ DESTRUCTIVE MIGRATION (a node carrying the
                                         #     twin's meta refuses to mount with `meta_corrupt`, and the store
                                         #     is measured asking for exactly 28 bytes) and its RECOVERY (fresh
                                         #     mount, baseline PERSISTED before begin() returns, high-water and
                                         #     epoch surviving a power cut).
                                         #   ⓘ Plus the two defect-⑤/① regressions driven through the nRF52
                                         #     seam (an uncommittable meta REFUSES the append and then
                                         #     recovers; `set_read_cursor` rolls back and stays repairable),
                                         #     counted in the figures above.
                                         # ---- the [[B134]] re-sync this one sits on top of ----
                                         # PIN was 2314, 98383 — ★★ RE-SYNCED 2026-08-29 by **[[B134]]** (the durable
                                         # ⛔ THE PREVIOUS VALUE WAS **WRONG, NOT MERELY STALE**: it read
                                         #   `2307, 98306` while the clean tree measured `2307, 98308`. Two
                                         #   assertions were added (the wipe-an-already-empty-store control) AFTER
                                         #   the figure was written, and the round-6 report CLAIMED the pin had
                                         #   been re-synced when it had not. ⓘ The cross-check is a cross-check
                                         #   precisely so a wrong figure prints a banner instead of zeroing a
                                         #   battery — it did its job; the report's claim was the defect.
                                         # ESP32/Heltec inbox), ON TOP OF [[B159]]'s re-sync recorded below and
                                         # WITHOUT disturbing it: **2262 / 97982 -> 2314 / 98383 = +52 cases /
                                         # +401 assertions**, ⛔ with NO existing case edited except one
                                         # EXPECTATION that was simply wrong on first writing (see below).
                                         # ⓘ TWO ROUNDS. The first landed the backend (+16 / +98, detailed
                                         #   below). The QG round then added **+10 cases / +44 assertions** for
                                         #   three PERSISTENCE FAILURE PATHS the first fault model could not
                                         #   reach — all three of the "reported success without durable success"
                                         #   class. It is **+14 cases / +65 assertions**, and THREE of those
                                         #   cases (§B134b/2b, /4b, /4c) exist because the battery said so: their
                                         #   mutations came back FAIL ("the suite still passes; nothing measures
                                         #   this"), which is the runner doing its job — an entry that cannot go
                                         #   RED is a property nothing was measuring.
                                         #     +9 / 49 in `test/test_segmented_inbox_store.cpp` (§B134b/1..6) —
                                         #       a rotation whose meta save fails REFUSES the append; the refusal
                                         #       RECOVERS on a later successful save; a tombstone under that
                                         #       failure reports io_error and the message stays visible ACROSS a
                                         #       reboot; begin() fails loud when the baseline will not persist
                                         #       (and on_init then disables the inbox); a roll onto a segment
                                         #       that will not erase is refused; and wipe() reports failure in
                                         #       BOTH its arms while still erasing everything it can.
                                         #     +5 / 16 in `test/test_device_inbox_fs_esp32.cpp` — a COMPLETE
                                         #       write whose SYNC failed, a failed CLOSE, that fault sealed
                                         #       end-to-end through the real store, the mount's folder arm in
                                         #       both directions, and a segment the filesystem will not remove
                                         #       making `wipe()` report failure THROUGH the seam.
                                         # DERIVED:
                                         #   +14 / 75 in the NEW `test/test_device_inbox_fs_esp32.cpp` — D1 the
                                         #     segment path incl. the three no-overrun forms (1), D2 the mount
                                         #     policy in all four arms incl. the ONE-mount-attempt vacuity
                                         #     control and the shared-mount/both-stores-told case (4), D3 the
                                         #     append verdict incl. the reports-n-but-commits-less tear (4), D4
                                         #     the whole-segment read (1), D5 the meta length (1), and the three
                                         #     END-TO-END cases through the real store over a fake flash: the
                                         #     record+tombstone reboot survival with an UNCHANGED epoch, the
                                         #     partly-committed append sealed across the seam, and §10.1's
                                         #     format-bumps-epoch/preserves-next_seq (3).
                                         #   +2 / 23 in `test/test_segmented_inbox_store.cpp` — the two
                                         #     behaviours the reused ring logic gained: `wipe()` (records gone,
                                         #     ring position reset, next_seq preserved, next boot bumps once)
                                         #     and the read-cursor wear coalescing driven both ways.
                                         #   ⓘ ONE assertion in the new file was CORRECTED BEFORE CLOSURE rather
                                         #     than the code: it expected `persisted_next_seq() == 4` after three
                                         #     records and a delete. A tombstone takes a seq of its OWN
                                         #     (`inbox.h`), so 5 is right and the test was wrong.
                                         # ---- the [[B159]] re-sync this one sits on top of ----
                                         # PIN was 2262, 97982 — ★★ RE-SYNCED 2026-08-28 by **[[B159]]** (the dedup-vs-retry-
                                         # horizon correction rounds), ON TOP OF §B20/B21's re-sync recorded below
                                         # and WITHOUT disturbing it: **2247 / 97916 -> 2262 / 97982 = +15 cases /
                                         # +66 assertions**, ⛔ with no existing case edited except [[B159]]'s own
                                         # derivation case (its first cut asserted retention == the give-up value;
                                         # QG disproved that, so the case now pins deadline + margin). DERIVED:
                                         #   +6 / 21 in `test/test_node_r3.cpp` — the two repros (time, and
                                         #     cache-evicted same-prev-hop), the retention boundary driven both
                                         #     sides, both retry horizons, the corrected derivation (7), and the
                                         #     prune/occupancy hygiene case (4).
                                         #   ⓘ A VERBATIM DUPLICATE of the duty-deferred handoff case (same
                                         #     TEST_CASE name, a leftover of a comment sweep) was REMOVED at
                                         #     closure; it had inflated this line by +1 case / +4 assertions.
                                         #   +3 / 13 in `test/test_dual_layer.cpp` — the PRODUCTION-PATH deadline
                                         #     case (drives origination -> RTS timeout -> doorstep hold -> queue
                                         #     drain, asserting on REAL transmission TIMES, not an injected
                                         #     duplicate); the deadline EDGE on the predicate; and the TWO HAL-
                                         #     handoff cases (a DUTY-deferred and an LBT-deferred RTS each carried
                                         #     across the bound must be cancelled loudly, never aired).
                                         #   +3 / 15 in `test/test_device_hal.cpp` — the PHYSICAL-START deadline
                                         #     in `DeviceHal::pump_tx`: a frame accepted before its deadline but
                                         #     still QUEUED past it is refused WITHOUT start_transmit and reports
                                         #     exactly one correlated `expired`; the 0 sentinel never expires
                                         #     ordinary traffic; a frame inside its deadline still flies.
                                         #   +1 /  8 in `test/test_dual_layer.cpp` — the expiry->give-up mapping:
                                         #     ZERO failures for a superseded seq, EXACTLY ONE for the matching
                                         #     flight, and still one on a repeat.
                                         #   +1 /  6 in `test/test_protocol_constants.cpp` — the start->arrival
                                         #     margin recomputed from `airtime_ms()` at the worst SUPPORTED PHY
                                         #     (SF12/BW7800/CR8/255 B = 279 765 ms), so a PHY widening breaks the
                                         #     build instead of silently re-opening [[B159]].
                                         # ⓘ SUPERSEDED NOTE, kept because its arithmetic is still the basis of
                                         #   the 2247 / 97916 figure this line builds on:
                                         # §B20/B21 (the silent DM length
                                         # boundary): **2240 / 96469 -> 2247 / 97916 = +7 cases / +1447 assertions**,
                                         # all in `test/test_node_r3.cpp`, ⛔ with no existing case edited. DERIVED —
                                         # the figure below is the arithmetic, not a copy of the runner's output, and
                                         # the two agree:
                                         #   +1 /    9 — `§B20 — data_frame_len/data_inner_cap ARE pack_data's
                                         #               arithmetic`: 4 shape rows + the CRYPTED delta + 2 round-trips
                                         #               (7) and the real-packer both-sides loop (2).
                                         #   +1 / 1362 — `§B20/B21 — every body length lands exactly ONE of two
                                         #               honest outcomes`: 6 carrier shapes x 40 lengths. An AIRING
                                         #               length asserts 5 (code, aired, crypted, not-failed,
                                         #               not-pack-failed); a REFUSING one asserts 7 (code, not-aired,
                                         #               failed, reason, tx_calls, queue depth, not-pack-failed).
                                         #               3 plaintext shapes cap at 239 -> 40x5 = 200 each = 600;
                                         #               CRYPTED and CRYPTED+`-a` cap at 214 -> 15x5 + 25x7 = 250
                                         #               each = 500; CRYPTED+`-l` caps at 208 -> 9x5 + 31x7 = 262.
                                         #               600 + 500 + 262 = 1362. ✓
                                         #   +1 /   27 — `§B20 — a sealed DM in the 215-216 band refuses too_large`:
                                         #               2 lengths x 6 + the 214-B control (4) + the `-l` twin
                                         #               (1 + 2x5 = 11).
                                         #   +1 /   18 — `§B21 — an oversized sealed DM reports too_large`: 3 lengths
                                         #               x 6.
                                         #   +1 /    8 — `§B21 — ...still reports no_pubkey, and now pushes it`:
                                         #               5 on the unbound arm + 3 on the bound control.
                                         #   +1 /    8 — `§B20 — a sealed DM AT the cap (214 B) delivers`: 4 on the
                                         #               origination (aired, crypted, not-failed, frame == 255 B) and
                                         #               4 on the peer's open (got, enc, body_len, body bytes).
                                         #   +1 /   15 — `§B20 — a TX-time pack refusal on a RELAYED frame tears
                                         #               the flight down`: the packer accepts the legal pair and
                                         #               refuses the flipped one (2), the ACK (1), the own DM
                                         #               queued-and-waiting (2), the backstop firing loudly with
                                         #               nothing aired (3), and the RECOVERY — queue drained, its
                                         #               RTS started, `data_tx` incremented, a frame present,
                                         #               parseable, dst 5, not CRYPTED (7).
                                         #   9 + 1362 + 27 + 18 + 8 + 8 + 15 = 1447. ✓
                                         # Thirteen B20/B21 mutations live under `b20mac` (8) and `b20codec` (5);
                                         # b20mac's B07/B08 are the two teardown halves, and the follow-up DM in
                                         # the relayed-backstop case is the ONLY assertion that measures them.
                                         # Superseded [[B251 QG correction]] pin follows, kept visible:
                                         # PIN_CASES, PIN_ASSERTS = 2240, 96469 — the hosted-mobile
                                         # counter boundary adds twelve cases / 424 assertions: exact s22 collision,
                                         # two-mobile receive identity, retry, queue/ring admission, reverse-key
                                         # destination/hash/layer separation, no-live-eviction, static/team/encrypted
                                         # exclusions and the cryptographic counter-negative. Thirteen B251
                                         # mutations live under b251rx/b251hash; D05 pins SendDispatch as the
                                         # sole admission authority for delegated evidence/correlation.
                                         # Superseded [[B161]] pin follows, kept visible:
                                         # PIN_CASES, PIN_ASSERTS = 2227, 95998 — canonical typed
                                         # answer origin adds nine cases / 411 assertions: exact complete DATA
                                         # bytes, both producer planes, type-13 N=0/N=32, type-5 preservation,
                                         # destination/relay/raw refusal semantics and typed hybrid identity.
                                         # The 15 B161 mutations live under b161hash/b161rx/b161mac.
                                         # Superseded [[B250]] pin follows, kept visible:
                                         # PIN_CASES, PIN_ASSERTS = 2218, 95587 — the roster grant's
                                         # explicit caller context adds five model cases and 291 assertions: both
                                         # parents' cancel/terminal/blank/push/expiry/emergency landings, all eleven
                                         # outcomes, the stale-caller contamination path, and B64's
                                         # unchanged/moved/gone identity arms. Derived baseline
                                         # remains authoritative ([[B217]]); this is the stale-pin warning only.
                                         # Superseded [[B247]] pin follows, kept visible:
                                         # PIN_CASES, PIN_ASSERTS = 2213, 95276 — corrupt stored
                                         # join-profile PHY values now render one bounded `PROFILE INVALID` state
                                         # through the transaction's existing validation authority. Native moved
                                         # **2211 / 95231 -> 2213 / 95276**: exactly +2 cases / +45 assertions,
                                         # measured both filtered (`program -tc='B247*'` = 2 / 45 / 0) and whole.
                                         # One `uijoin` mutation (J26) restores the unvalidated path; no existing
                                         # case or target was removed.
                                         #
                                         # The superseded §UI-10/11 P3 sync follows, kept visible:
                                         # PIN_CASES, PIN_ASSERTS = 2211, 95231 — THE CATALOG REACHES THE PANEL:
                                         # the compose lists become a projection of the
                                         # live `/mrui` record, `SendReq` carries `{slot, generation}`, and the
                                         # fixed `kDmTexts`/`kChannelTexts`/`kEmergencyText` tables RETIRE.
                                         # **2186 / 94712 -> 2211 / 95231** (+25 cases / **+519 assertions**),
                                         # DERIVED from the clean run (six worker trees agreed).
                                         # ⛔ NO battery TARGET gained or lost an entry list; two landed `model`
                                         #   entries (W10) and two probe controls (R1/R2) were RE-ANCHORED because
                                         #   the expressions they mutated moved — recorded at each site.
                                         # ---- +290 — 25 NEW cases, MEASURED with `program -tc='<prefix>*'`:
                                         #   `ui10-p3-slot*`       4 cases   49
                                         #   `ui10-p3-row*`        1 case    29
                                         #   `ui10-p3-empty*`      1 case    12
                                         #   `ui10-p3-modal*`      4 cases   22
                                         #   `ui10-p3-changed*`    1 case    12
                                         #   `ui10-p3-freeze*`     7 cases   58   (1 in the model suite, 6 in send)
                                         #   `ui10-p3-r1*`         1 case    57
                                         #   `ui10-p3-resources*`  1 case    13
                                         #   `ui10-p3-emergency*`  2 cases    7
                                         #   `ui10-p3-loc*`        3 cases   31
                                         #   4+1+1+4+1+7+1+1+2+3 = 25 cases ✓   49+29+12+22+12+58+57+13+7+31 = 290
                                         # ---- +229 — the NET movement of the landed cases this slice REWROTE.
                                         #   ⛔ NOTHING WAS DELETED: 2186 + 25 = 2211 exactly, so every landed case
                                         #   still runs. The rewrites and their CURRENT measured counts:
                                         #     `ui7-B66*`  6 -> **199** (+193) — the case that used to read the two
                                         #        counts back out of the two tables now proves `back` is the DERIVED
                                         #        last row at EVERY catalog size (0..8 enabled, both kinds, grant on
                                         #        and off). The old figure is exact: the withdrawn case is kept in
                                         #        the comment and is six straight CHECKs with no loop.
                                         #     `chrome4-audit*` **148** — its compose block now proves the 19-column
                                         #        budget against OQ-A's BOUND (17) instead of the five compiled
                                         #        strings, over both location states.
                                         #     `ui10-p1-defaults: THE DRIFT FENCE …` -> `… IS DISCHARGED` **23** —
                                         #        the duplication it fenced no longer exists (P3 retired the tables),
                                         #        so it asserts the defaults through the SHIPPED projection instead.
                                         #     `ui7-line: a DISABLED slot …` **6**, `ui7-send: a request the composer
                                         #        refuses …` **5** — re-pointed off the retired row-index identity.
                                         #     `ui16-k7-act: pin 1*` **33**, `ui16-k7-self*` **13**,
                                         #        `ui-model: the model's declared bounds*` **16**,
                                         #        `ui16-k7-resources*` **15** — the same K7 / resource properties over the
                                         #        projection and the re-measured `sizeof(UiSnapshot)` (1008 -> 1336).
                                         #   290 + 229 = 519 ✓
                                         #
                                         # The superseded §UI-10/11 P2 sync follows, kept visible:
                                         # PIN_CASES, PIN_ASSERTS = 2186, 94712 — the `ui preset`
                                         # VERB FAMILY: the pure grammar/records/boot unit
                                         # (`src/firmware_ui_preset_verbs.h`) + its suite. NEW battery target
                                         # `uipresetverbs` (19 entries, V01-V19).
                                         # **2176 / 94153 -> 2186 / 94712** (+10 cases / **+559 assertions**),
                                         # DERIVED from the clean run. ⛔ NOTHING WAS SUBTRACTED and ⛔ NO existing
                                         # case's count moved: P2 ADDS a header and a suite, consumes P1's service
                                         # unchanged, and its ONE edit to `src/firmware_ui_presets.h` is COMMENT-ONLY
                                         # (the `bad_location` done-vs-missing marker, closed) — so the `uipresets`
                                         # battery's own cases survive byte-identical.
                                         # MEASURED case by case with `program -tc='<prefix>*'`:
                                         #  +559 — 10 NEW cases, `test/test_firmware_ui_preset_verbs.cpp`:
                                         #    `P2 the three NDJSON records are byte-exact…`         15
                                         #    `P2 list emits all 17 records…`                       66
                                         #    `P2 a mutating verb answers with the RESULTING…`      53
                                         #    `P2 the busy table (spec §2)…`                        70
                                         #    `P2 loc= takes EXACTLY on|off…`                       46
                                         #    `P2 the remaining reasons: bad_slot, bad_text…`       71
                                         #    `P2 the grammar: an unknown sub-verb…`                57
                                         #    `P2 USB and BLE byte-agree…`                         142
                                         #    `P2 the boot restore: the four storage states…`       33
                                         #    `P2 the resident cost of the ONE live catalog…`        6
                                         #    15+66+53+70+46+71+57+142+33+6 = 559. ✓
                                         # ⓘ `--target=uipresets`'s TARGET FILE moved (the comment-only marker
                                         #   update), so that battery re-runs IN FULL even though not one of its
                                         #   entries or cases changed.
                                         #
                                         # The superseded §UI-10/11 P1 sync follows, kept visible:
                                         # PIN_CASES, PIN_ASSERTS = 2176, 94153 — the `/mrui`
                                         # PRESET CATALOG: the pure service (`src/firmware_ui_presets.h`) + the
                                         # `'MRU1'` record (`src/device_nv.h`). NEW battery target `uipresets`.
                                         # **2148 / 93187 -> 2176 / 94153** (+28 cases / **+966 assertions**),
                                         # DERIVED from the clean run. ⛔ NOTHING HERE IS A SUBTRACTION, and ⛔ NO
                                         # EXISTING case's count moved: this slice ADDS a header and a suite and
                                         # touches no landed assertion (`src/firmware_ui_model.h`'s fixed tables are
                                         # deliberately UNCHANGED — P3 consumes the catalog, P1 does not).
                                         # MEASURED case by case with `program -tc=<prefix>*` (the names carry
                                         # commas, which doctest's `-tc` treats as a separator — hence the prefix):
                                         #  +966 — 28 NEW cases, `test/test_firmware_ui_presets.cpp`:
                                         #    `ui10-p1-abi        …the record's ABI…`                26
                                         #    `ui10-p1-abi        …the storage-level four states…`   11
                                         #    `ui10-p1-defaults   …§3.2.2's table verbatim…`         67
                                         #    `ui10-p1-defaults   …THE DRIFT FENCE…`                 13
                                         #    `ui10-p1-states     …ABSENT, silent…`                   9
                                         #    `ui10-p1-states     …INVALID, counted…`                 8
                                         #    `ui10-p1-states     …IO_FAILED, distinct…`              6
                                         #    `ui10-p1-states     …VALID is LOADED…`                 10
                                         #    `ui10-p1-states     …the two boot lines…`               5
                                         #    `ui10-p1-semantic   …ANY violation -> invalid…`        140
                                         #    `ui10-p1-canonical  …the written record's bytes…`      32
                                         #    `ui10-p1-canonical  …the composition zeroes whole…`    19
                                         #    `ui10-p1-generation …starts 1, skips zero…`            14
                                         #    `ui10-p1-coalescing …valid store…`                     20
                                         #    `ui10-p1-coalescing …ABSENT store…`                     7
                                         #    `ui10-p1-coalescing …⛔ NOT over invalid…`             11
                                         #    `ui10-p1-coalescing …★ LATE-ABSENT durability…`         13
                                         #    `ui10-p1-emergency  …never cleared…`                   10
                                         #    `ui10-p1-emergency  …text-editable…`                   17
                                         #    `ui10-p1-busy       …every verb, incl. a no-op…`       19
                                         #    `ui10-p1-order      …candidate then live…`             13
                                         #    `ui10-p1-io         …every mutation, zero writes…`     17
                                         #    `ui10-p1-validation …1..17, both loc states…`         285
                                         #    `ui10-p1-validation …★ the 273-BYTE narrowing…`         19
                                         #    `ui10-p1-slots      …the stable slot identity…`       100
                                         #    `ui10-p1-slots      …zero enabled per kind…`           11
                                         #    `ui10-p1-words      …the three enum inventories…`      59
                                         #    `ui10-p1-resources  …the STACK GATE, measured…`         5
                                         #    26+11+67+13+9+8+6+10+5+140+32+19+14+20+7+11+13+10+17
                                         #    +19+13+17+285+19+100+11+59+5 = 966. ✓
                                         # ⛔ RE-SYNCED AGAIN 2026-08-25 AFTER THE **QG HOLD** (three
                                         #   corrections): +2 cases / +40 assertions on 2174 / 94113 —
                                         #   the 273-byte narrowing regression (19), the LATE-ABSENT
                                         #   durability case (13), and `words` 51 -> 59 (+8) for the
                                         #   `bad_location` arm now in the inventory sweep. ⛔ Nothing
                                         #   was subtracted and no other case's count moved.
                                         # ⓘ `--target=devicenv` gains FIVE entries (N21-N25) for the record itself;
                                         #   its target file moved, so that battery re-runs in full.
                                         #
                                         # The superseded §UI-16 K7 sync follows, kept visible:
                                         # 2148 / 93187 — ★★ CROSS-CHECK RE-SYNCED 2026-08-25 by **§UI-16 K7 ([[B245]])** — the
                                         # ROSTER GRANT: an operator-initiated per-member act on the ENTERED
                                         # TEAM screen that opens the LANDED N5/N6 chain verbatim.
                                         # **2139 / 92925 -> 2148 / 93187** (+9 cases / **+262 assertions**),
                                         # DERIVED from the clean run. ⛔ NOTHING HERE IS A SUBTRACTION, and ⛔ no
                                         # EXISTING case's count moved — the three landed resource cases had
                                         # figures RE-MEASURED in place (`UiState` 496 -> 504, `UiModel`
                                         # 920 -> 928, and the +8 shift of every `UiState` offset past the head),
                                         # which changes what they assert, ⛔ not how many times.
                                         # MEASURED case by case with `program -tc=`:
                                         #  +262 — 9 NEW cases, `test/test_firmware_ui_model.cpp`:
                                         #    `ui16-k7-act       …pin 1, the act on a member row…`     32
                                         #    `ui16-k7-b245      …pin 2, THE REPRO END TO END…`        26
                                         #    `ui16-k7-window    …pin 3, the window UNDISTURBED…`      17
                                         #    `ui16-k7-silent    …pin 4, counted, not read…`           15
                                         #    `ui16-k7-self      …pin 5, the SELF row…`                13
                                         #    `ui16-k7-keyless   …pin 6, four vetoes, each alone…`     21
                                         #    `ui16-k7-identity  …pin 7, P-7c/P-7d…`                   14
                                         #    `ui16-k7-words     …pin 8, the WHOLE enum…`             110
                                         #    `ui16-k7-resources …the two frozen fields…`              14
                                         #    32+26+17+15+13+21+14+110+14 = 262. ✓
                                         # ⓘ `src/firmware_ui_invite.h` is ⛔ NOT TOUCHED by K7, so the whole
                                         #   `--target=uiinvite` battery re-runs against an identical file.
                                         #
                                         # The superseded K6 round-2 GATE-INTEGRITY sync follows, kept visible:
                                         # 2139 / 92925 — ★★ CROSS-CHECK RE-SYNCED 2026-08-25 by **§UI-16 K6 round 2's GATE-
                                         # INTEGRITY FIX (C1)** — QG passed the implementation and HELD on one
                                         # item: the native "exact drain-loop" fixture in
                                         # `test/test_firmware_team_keyring.cpp` still switched on `GrantUiRoute`
                                         # ALONE and threw the `KeyringErr` away at the seam, so it could not
                                         # carry `keyring_full` — while `src/fw_main.cpp` routes a two-fact
                                         # `mrfw::GrantUiVerdict`. A fixture whose comment claims the real loop is
                                         # reproduced EXACTLY, and models the PREVIOUS shape, pins the old gate.
                                         # **2138 / 92893 -> 2139 / 92925** (+1 case / **+32 assertions**),
                                         # DERIVED from the clean run. ⛔ NOTHING HERE IS A SUBTRACTION.
                                         # MEASURED case by case with `program -tc=`:
                                         #  +27 — 1 NEW case, `test/test_firmware_team_keyring.cpp`:
                                         #    `ui16-k6-grantfull-drain …the fifth RECEIVED grant…`  27
                                         #       (the same full-4-record store as `ui16-k6-grantfull`,
                                         #        driven THROUGH the drain-loop fixture: the unsaved
                                         #        door WITH `keyring_full`, nothing forwarded, blob
                                         #        byte-identical, zero saves — plus the retry)
                                         #  +5 — the flag asserted in 5 EXISTING drain-loop cases that had
                                         #       only ever read `ui.unsaved == 1` (the withdrawn shape is kept
                                         #       visible at each), all in the same file:
                                         #    `ui16-K3: a FAILED persist…`        43 -> 47   (+4, its 4 subcases)
                                         #    `ui16-K3: each of the four…`        39 -> 40   (+1, re-check (4))
                                         #    27 + 5 = 32. ✓
                                         # ⓘ `drain_one` now routes `grant_ui_verdict_of(svc.receive(g))` and its
                                         #   `UiSink` gained an `unsaved_full` counter; the ROUTE it switches on is
                                         #   unchanged, so ⛔ no existing route assertion moved.
                                         #
                                         # The superseded K6 round-2 sync follows, kept visible as the base:
                                         # 2138 / 92893 — ★★ CROSS-CHECK RE-SYNCED 2026-08-25 by **§UI-16 K6 round 2** — QG's
                                         # blocker: a `KEYRING FULL` refusal of a **RECEIVED grant** was collapsed
                                         # into the generic `active_unsaved`, so the fifth received grant showed the
                                         # three correct rows and then acknowledged into a menu that says nothing,
                                         # while the `team new` refusal of the SAME store state reached SAVED KEYS.
                                         # **2133 / 92724 -> 2138 / 92893** (+5 cases / **+169 assertions**),
                                         # DERIVED from the clean run. ⛔ NOTHING HERE IS A SUBTRACTION.
                                         # MEASURED case by case with `program -tc=`:
                                         #  +128 — 2 NEW cases, `test/test_firmware_team_keyring.cpp`:
                                         #    `ui16-k6-grantfull …a fifth RECEIVED grant…`          28
                                         #    `ui16-k6-grantverdict …the LANDED route plus ONE…`   100
                                         #       (the `GrantSave` x `KeyringErr` sweep: the route is
                                         #        ALWAYS the landed classifier's, and the fact is true
                                         #        for exactly one pair)
                                         #  +41 — 3 NEW cases, `test/test_firmware_ui_model.cpp`:
                                         #    `ui16-k6-grantack …three ruled rows + the ACK…`       20
                                         #    `ui16-k6-grantack-menu …NON-full still the MENU…`     13
                                         #    `ui16-k6-grantack-join …from the JOIN result too…`     8
                                         #    128 + 41 = 169. ✓
                                         # ⓘ The landed `on_team_key_note` call sites gained a SPELLED-OUT second
                                         #   argument (no default — see the model's block for the measured reason);
                                         #   that moved ⛔ no assertion count anywhere.
                                         #
                                         # The superseded K6 round-1 sync follows, kept visible as the base:
                                         # 2133 / 92724 — ★★ RE-SYNCED 2026-08-25 by **§UI-16 K6** (saved-key
                                         # RETENTION MANAGEMENT — ⛔ never "key rotation": `team keys` /
                                         # `team forgetkey`, and the OLED `SAVED KEYS` screens).
                                         # **2107 / 92038 -> 2133 / 92724** (+26 cases / **+686 assertions**),
                                         # DERIVED from the clean run. ⛔ NOTHING HERE IS A SUBTRACTION EXCEPT
                                         # the ONE withdrawn line named below, which is stated rather than netted.
                                         # MEASURED case by case with `program -tc=`:
                                         #  +356 — 11 NEW cases, `test/test_firmware_team_keyring.cpp`:
                                         #    `ui16-k6-pin1  …FULL + UNCONFIRMED = zero writes…`     24
                                         #    `ui16-k6-pin2  …the ACTIVE record is PROTECTED…`       19
                                         #    `ui16-k6-pin3  …ONE save, survivors byte-identical…`   28
                                         #    `ui16-k6-pin4  …not-found / 0 / unreadable…`           64
                                         #    `ui16-k6-pin5  …a FAILED save reported as failed…`     13
                                         #    `ui16-k6-pin6  …the LIST exposes ids and status ONLY…` 65
                                         #    `ui16-k6-pin7  …a SHORT-FINGERPRINT COLLISION…`        16
                                         #    `ui16-k6-pin9  …a re-key is an in-place REPLACE…`      19
                                         #    `ui16-k6-two-transactions …never a side effect…`       24
                                         #    `ui16-k6-active-predicate …ONE authority…`              6
                                         #    `ui16-k6-inventory …the count fence…`                  78
                                         #  +229 — 10 NEW cases, `test/test_firmware_ui_model.cpp`:
                                         #    `ui16-k6-menu …the FIFTH child…`                       15
                                         #    `ui16-k6-open …read ONCE, performs NOTHING…`           15
                                         #    `ui16-k6-confirm …the FULL 32-bit id…`                 24
                                         #    `ui16-k6-active …no destructive action…`               15
                                         #    `ui16-k6-result …the REFRESHED list…`                  10
                                         #    `ui16-k6-failure …KEY NOT FORGOTTEN…`                  48
                                         #    `ui16-k6-pin8 …KEYRING FULL's ack…`                    21
                                         #    `ui16-k6-rows …IDENTITIES, BACK unconditional…`        16
                                         #    `ui16-k6-lexemes …the SEVEN K6 words…`                 46
                                         #    `ui16-k6-resources …costs exactly themselves…`         19
                                         #  +100 — 5 NEW cases, `test/test_firmware_ui_prov.cpp`
                                         #    (`§UI16-K6 …` — the list, the act, the six failing arms,
                                         #     the typed `KEYRING FULL` flag, the fifth-op dispatch)
                                         #  +2 — TWO EXISTING width loops gained one iteration each:
                                         #    `…every row's label fits the rail…`  x2, because
                                         #    `kMaxProvRows` moved 5 -> 6 with the `SAVED KEYS` child
                                         #  −1 — ONE existing CHECK WITHDRAWN IN PLACE, and it is named rather
                                         #    than netted: `ui16-k5-resources`'s
                                         #    `offsetof(UiState, invite) == 344` — K6's two carriers now sit
                                         #    between K5's field and the window, so that absolute offset is
                                         #    K6's arithmetic and is asserted (as 384) in `ui16-k6-resources`.
                                         #    K5's OWN claim (its field is FREE and UNMOVED at 340) is intact.
                                         #    356 + 229 + 100 + 2 − 1 = 686. ✓
                                         #
                                         # The superseded §UI-17 keyrecv sync follows, kept visible as the base:
                                         # 2107 / 92038 — ★★ RE-SYNCED 2026-08-25 by **§UI-17 keyrecv** (the
                                         # owner-ruled shape (a): acknowledging `TEAM KEY RECEIVED` lands on
                                         # the PASSIVE STATUS screen).
                                         # **2102 / 91886 -> 2107 / 92038** (+5 cases / **+152 assertions**),
                                         # DERIVED from the clean run. ⛔ NOTHING HERE IS A SUBTRACTION.
                                         # MEASURED case by case with `program -tc=`:
                                         #  +147 — 5 NEW cases, `test/test_firmware_ui_model.cpp`:
                                         #    `…EITHER press … PASSIVE STATUS … EITHER result`   56
                                         #       (2 presses x 2 result arms, each asserting the
                                         #        arrival moved nothing BEFORE the press)
                                         #    `…the note's ARRIVAL still navigates nothing…`     24
                                         #    `…the FAILURE pair's acknowledgement stays…`       28
                                         #    `…every NEIGHBOURING terminal's … untouched…`      20
                                         #    `…keeps the LANDED blank/wake rules…`              19
                                         #  +5 — ONE existing case corrected in place:
                                         #    `ui16-K4: the note occupies…`               15 -> 20 (+5)
                                         #       (the withdrawn "ack lands on the menu" block kept
                                         #        VISIBLE in comment; the new landing's five fields
                                         #        asserted, and the RETIREMENT re-proven on the next
                                         #        ENTRY where it has always belonged)
                                         #    147 + 5 = 152. ✓
                                         #
                                         # The superseded K5-round-2 sync follows, kept visible as the base:
                                         # 2102 / 91886 — ★★ RE-SYNCED 2026-08-25 by §UI-16 **K5 round 2** (the QG
                                         # blockers: the A -> B membership race, the SURGICAL refusal, the
                                         # writer's second authority, and the `SavedKeyUse` count fence).
                                         # **2101 / 91787 -> 2102 / 91886** (+1 case / **+99 assertions**),
                                         # DERIVED from the clean run. ⛔ NOTHING HERE IS A SUBTRACTION.
                                         # MEASURED case by case with `program -tc=`:
                                         #  +13 — NEW case, `test/test_firmware_team_keyring.cpp`:
                                         #    `…the WRITER refuses an ACTIVE BINDING for a team its own
                                         #     record does not name…`                     0 -> 13
                                         #       (the pure predicate's five arms, plus the FAKE enforcing it:
                                         #        the refusal counted, nothing written, and the positive arm)
                                         #  +86 — FOUR existing cases rewritten/extended in place:
                                         #    `…EVERY failing arm…`                      49 -> 77 (+28)
                                         #       (the A -> B race with a REAL live key for B that must
                                         #        survive, the `record_unreadable` fail-closed arm, and every
                                         #        arm's `clear_calls == 0` where it used to demand == 1)
                                         #    `…the SavedKeyUse inventory…`              56 -> 95 (+39)
                                         #       (the sweep is now `0 .. count-1` over EIGHT arms instead of a
                                         #        hand-typed six, so the n x n distinctness grid grew with it)
                                         #    `§UI16-K5 …a FAILED activation…`           32 -> 48 (+16)
                                         #       (the adapter-level A -> B race arm, and the withdrawn
                                         #        `NO TEAM KEY` expectation replaced by S-39's word)
                                         #    `…the two ruled lexemes…`                  35 -> 38  (+3)
                                         #       (`kSavedKeyFailedText` declared once, its 17 columns, and
                                         #        ⛔ that it is NOT `NO TEAM KEY` any more)
                                         #    13 + 28 + 39 + 16 + 3 = 99. ✓
                                         #
                                         # The superseded ROUND-1 sync follows, kept visible as the base:
                                         # 2101 / 91787 — ★★ RE-SYNCED 2026-08-25 by §UI-16 **K5** (`SAVED KEY FOUND`
                                         # / `USE SAVED KEY`). **2084 / 91382 -> 2101 / 91787**
                                         # (+17 cases / **+405 assertions**), DERIVED from the clean run.
                                         # ⛔ NOTHING HERE IS A SUBTRACTION.
                                         # MEASURED case by case with `program -tc=`, ⛔ not estimated:
                                         #  +139 — 4 NEW cases, `test/test_firmware_team_keyring.cpp`:
                                         #    `…has_record answers a BOOLEAN…`                    17
                                         #       (14 at first, +3 when the T42 fail-open control came back
                                         #        GREEN: the unreadable loop needed the store to DEPOSIT a
                                         #        plausible record, or its `false` was the fixture's and not
                                         #        the rule's — the [[B217]] shape, caught by its own battery)
                                         #    `…USE SAVED KEY installs … BOOT-DURABLE…`           17
                                         #    `…EVERY failing arm leaves the node KEYLESS…`       49
                                         #    `…the six SavedKeyUse arms are six DIFFERENT words…` 56
                                         #  +90 — 5 NEW cases, `test/test_firmware_ui_prov.cpp`:
                                         #    `…a join … with NO retained record…`                11
                                         #    `…the QUESTION is asked ONLY on the applied arm…`    12
                                         #    `…USE SAVED KEY installs … BOOT-DURABLE afterwards`  25
                                         #    `…a FAILED activation says NO TEAM KEY…`             32
                                         #    `…the dispatch routes the FOURTH op…`                10
                                         #  +129 — 8 NEW cases, `test/test_firmware_ui_model.cpp`:
                                         #    `…the offer opens on the ACKNOWLEDGEMENT…`           24
                                         #    `…⛔ NO offer without a retained record…`            10
                                         #    `…BACK performs NOTHING…`                            11
                                         #    `…reaching USE SAVED KEY costs short THEN double…`   18
                                         #    `…a REFUSED join opens no offer …two floors MARKED`   7
                                         #    `…the UNFINISHED offer does not survive BLANKING…`   13
                                         #    `…the two ruled lexemes are VERBATIM…`               35
                                         #    `ui16-k5-resources…`                                 11
                                         #  +47 — THREE existing cases extended in place:
                                         #    `§UI16-N3/K5 …a RETAINED record is NOT installed…`  12 -> 16  (+4)
                                         #       (the FLIPPED N3 case: the answer now REPORTS the record —
                                         #        `saved_key`, the ask count, the asked id, and ⛔ that the
                                         #        ACT was never entered; the two withdrawn "no K5 lexeme"
                                         #        assertions are RETAINED and still pass)
                                         #    `ui15-model: the Provision enum…`                   27 -> 30  (+3)
                                         #       (`invite_result` == 15, `saved_key` == 16, and that the
                                         #        new arm is ⛔ not a window arm)
                                         #    `…the close-on-leave reset…`                       305 -> 345 (+40)
                                         #       (TWO arms joined the sweep — `saved_key` AND the
                                         #        `invite_result` §UI-16 N6 appended without extending it —
                                         #        at 2 confirm values x 10 assertions each)
                                         #    139 + 90 + 129 + 47 = 405. ✓
                                         #
                                         # The superseded slice-8b round-2 sync follows, kept visible as the base:
                                         # 2084 / 91382 — ★★ RE-SYNCED 2026-08-25 by §UI-16 slice 8b **round 2**
                                         # (the QG blocker: [[B243]]'s seam was a BOOLEAN, so every refusal took
                                         # the failed-save door and the panel claimed `TEAM KEY ACTIVE` for
                                         # receipts where nothing is active). **2083 / 91336 -> 2084 / 91382**
                                         # (+1 case / **+46 assertions**), DERIVED from the clean run.
                                         # ⛔ NOTHING HERE IS A SUBTRACTION.
                                         # MEASURED case by case with `program -tc=`, ⛔ not estimated (the
                                         # first draft of this block guessed 37 for the new case and was
                                         # wrong by 15 — the totals below are the tool's own figures):
                                         #  +22 — NEW case, `test/test_firmware_team_keyring.cpp`:
                                         #    `…every GrantSave arm is classified, and ⛔ no arm claims a key
                                         #     that is not live…`                            0 -> 22
                                         #       (the 8-wide enum sweep asserts `r != count` once per arm = 8,
                                         #        plus the two tallies, the three SUPPRESSED arms named,
                                         #        `silent==3`, the four `active_unsaved` arms named,
                                         #        `unsaved==4`, `saved`, and the sentinel's two pins)
                                         #  +24 — the RE-PINNED existing cases, three counters instead of one:
                                         #    `…a successful persist writes the KEY FIRST…`   17 -> 18  (+1)
                                         #       (`silent == 0` — exactly ONE of the three routes is taken)
                                         #    `…a FAILED persist is NOT forwarded…`           33 -> 43 (+10)
                                         #       (all four subcases are AFTER-re-check-(3) arms ⇒ each gains
                                         #        its `unsaved`/`silent` pin and, where the wording is the
                                         #        claim, `live_key_really_active`)
                                         #    `…each of the four handling-time re-checks…`    26 -> 39 (+13)
                                         #       (re-checks 1-3 are the SUPPRESSED arms and each gains
                                         #        `calls==0 / unsaved==0 / silent==1`; re-check (4) gains the
                                         #        `active_unsaved` pin plus the live-key precondition)
                                         #    22 + 1 + 10 + 13 = 46. ✓
                                         #
                                         # The superseded ROUND-1 sync follows, kept visible as the base:
                                         # 2083 / 91336 — ★★ RE-SYNCED 2026-08-25 by §UI-16 slice 8b (the K3+K4
                                         # corrections — [[B243]]'s hook, [[B244]]'s re-anchor, the GrantSave
                                         # count fence): **2083 / 91317 -> 2083 / 91336** (+0 cases /
                                         # **+19 assertions**), DERIVED from the clean run and MEASURED case by
                                         # case with `program -tc=`. ⛔ NOTHING HERE IS A SUBTRACTION, and no
                                         # case was added — every assertion lands inside a case that already
                                         # existed, because both fixes are about a path that was already driven.
                                         #  +4 — `test/test_firmware_team_keyring.cpp`, [[B243]]'s second door:
                                         #    `…a successful persist writes the KEY FIRST…`        +1
                                         #       (`ui.unsaved == 0` — a forwarded receipt ⛔ never ALSO raises
                                         #        the failure note; the two doors are mutually exclusive)
                                         #    `…a FAILED persist is NOT forwarded…`                +1
                                         #       (`ui.unsaved == 1` — K4 pin 2's DEVICE half, which was the
                                         #        registered gap: the panel was silent, ⛔ never wrong)
                                         #    `…the verdict enum renders in full…`                 +2
                                         #       (the sentinel answers `?`, and it is GREATER than the last
                                         #        real outcome — so the `<` bound cannot be "fixed" to `<=`)
                                         #    1+1+2 = 4.
                                         #  +15 — `test/test_firmware_ui_send.cpp`, ONE case:
                                         #    `…⛔ the note NEVER navigates, moves no cursor, writes no
                                         #     emergency field and does NOT wake…`                +15
                                         #       (a SECOND dark model driven through the FAILURE door: the arm
                                         #        ran, a repaint is owed, the panel stays DARK, eight state
                                         #        fields unmoved, and the three emergency fields unmoved)
                                         #    4 + 15 = 19. ✓
                                         #
                                         # The superseded K3+K4 sync follows, kept visible as the base:
                                         # 2083 / 91317 — ★★ RE-SYNCED 2026-08-24 by §UI-16 K3+K4 (the
                                         # persistence-FIRST grant receive and its durable note):
                                         # **2071 / 90950 -> 2083 / 91317** (+12 cases / +367 assertions),
                                         # DERIVED from the clean run and MEASURED case by case with
                                         # `program -tc=` (the totals below add up to the delta exactly —
                                         # ⛔ nothing here is a subtraction).
                                         # ★ K3 — `test/test_firmware_team_keyring.cpp`, +7 cases / +122:
                                         #    `…a successful persist writes the KEY FIRST…`            16
                                         #    `…a FAILED persist is NOT forwarded…`                    32
                                         #    `…each of the four handling-time re-checks…`             26
                                         #    `…a re-key REPLACES this team's record in place…`        12
                                         #    `…a foreign team's grant never gets here…`                5
                                         #    `…a re-grant of IDENTICAL material writes NOTHING…`      19
                                         #    `…the verdict enum renders in full…`                     12
                                         #    16+32+26+12+5+19+12 = 122.
                                         # ★ K4 — `test/test_firmware_ui_send.cpp` (+3 / +63) and
                                         #   `test/test_firmware_ui_model.cpp` (+2 / +182):
                                         #    `…a forwarded grant receipt shows TEAM KEY RECEIVED…`    23
                                         #    `…the note NEVER navigates … and does NOT wake`          20
                                         #    `…the FULL PushKind enum — only team_key_received…`      20
                                         #    `…the note occupies the panel's ONE transient answer…`   15
                                         #    `…the three result rows are TOTAL over the whole enum…`  167
                                         #    23+20+20+15+167 = 245.  122+245 = 367.
                                         # ⓘ THE 167 IS NOT A TYPO AND IT IS NOT PADDING: that case
                                         #   sweeps ELEVEN outcomes x (3 rows x {non-null, <= 19 cols})
                                         #   plus the third-row exclusivity count plus the THREE
                                         #   forbidden lexemes x 3 rows x 11 outcomes — the "drive the
                                         #   full enum, ⛔ not a sample" rule applied to a 3-row renderer.
                                         # ⛔ `src/` + `test/` + `tools/` only: `git diff -- lib/` is
                                         # EMPTY, so the corpus and the board builds are NOT re-run
                                         # (nothing they measure can have moved).
                                         #
                                         # The superseded N6b sync follows, kept visible as the base:
                                         # 2071 / 90950 — ★★ RE-SYNCED 2026-08-24 by §UI-16 N6b (the grant's
                                         # EXPLICIT dispatch result), then TWICE MORE the same day by its two
                                         # QG evidence rounds: **2066 / 90717 -> 2070 / 90891 -> 2071 / 90923
                                         # -> 2071 / 90950** (+5 cases / +233 assertions overall).
                                         # ★ QG ROUND 2's DELTA (+0 cases / +27 assertions), MEASURED:
                                         #  +22 — `ui16-grant-words` 87 -> 109: the hand-written array is
                                         #    GONE. The sweep now walks `0 .. InviteGrantState::count - 1`,
                                         #    so it visits `none` too (asserted to render "", while every
                                         #    other state is asserted NON-empty) and the per-state checks
                                         #    apply to 13 values instead of 12. ⛔ There is no literal count
                                         #    left to keep in sync: a new state is swept BY CONSTRUCTION,
                                         #    and one added without a word is a -Werror=switch BUILD
                                         #    FAILURE in `invite_grant_word`.
                                         #  +5 — `ui16-grant-parkfull-air` 23 -> 28: the aired frame is now
                                         #    DECODED with the shipped `parse_h` (U1) and pinned as an H
                                         #    query FOR THE UNRESOLVED HASH, with the successful-park
                                         #    control decoded the same way — "one transmission" became
                                         #    "one H query, and nothing else aired".
                                         # ⛔ Round 2 changed only COMMENTS under `lib/` (three false
                                         # "nothing will air" descriptions) plus `src/` + `test/`, so the
                                         # corpus and the board builds are again NOT re-run.
                                         # ★ THE QG ROUND's OWN DELTA (+1 case / +32 assertions), MEASURED:
                                         #  +23 / +1 case — `ui16-grant-parkfull-air`, the H-lookup PIN: a
                                         #    refused park stores no grant DATA but is ⛔ NOT radio-silent —
                                         #    the arm's UNCHANGED `emit_hash_query` still airs EXACTLY ONE
                                         #    H frame, counted on the HAL and pinned, with a successful park
                                         #    measured as its control (the same one H).
                                         #  +9 — `ui16-grant-words` 78 -> 87: the sweep was missing
                                         #    `queue_full`, so the 19-column bound and the
                                         #    forbidden-completion sweep had never been applied to
                                         #    `GRANT QUEUE FULL`; the array's SIZE is now asserted too
                                         #    (⚠ that assertion went RED on its own first run at `11u` —
                                         #    the real figure is 12 word-bearing states — which is exactly
                                         #    why the count lives in a CHECK and not in a comment).
                                         # ⛔ Test/comment-only: no `lib/` line changed in this round, so
                                         # the corpus and the board builds are NOT re-run (nothing they
                                         # measure can have moved).
                                         #
                                         # The first N6b sync follows, kept visible as the delta's base:
                                         # 2070 / 90891 — **2066 / 90717 -> 2070 / 90891**
                                         # (+4 cases / +174 assertions), DERIVED from the clean run.
                                         # ★ THE ARITHMETIC, and each half says how it was obtained:
                                         #  +4 cases / +119 assertions — MEASURED case by case with
                                         #    `program -tc=`: the four NEW real-core scenario cases in
                                         #    `test/test_firmware_ui_invite.cpp` (target `uiinvite`) —
                                         #    `ui16-grant-queuefull` **53** (the TX queue filled with
                                         #    EIGHT real admitted grants, then the refusal, then the
                                         #    proof that nothing aired and no push can promote it),
                                         #    `ui16-grant-parkfull` **38** (the parked ring filled with
                                         #    eight real parks, then the refusal) and
                                         #    `ui16-grant-redad` **12** (the binding moves between
                                         #    selection and send; the frozen id does NOT correlate and
                                         #    the real one promotes), and `ui16-grant-noroute` **16**
                                         #    (a send that reaches NO admission point at all: the core
                                         #    already pushed `send_failed`, so the panel says GRANT
                                         #    FAILED and ⛔ never the PARKED word the withdrawn
                                         #    inference put there). 53+38+12+16 = 119.
                                         #  +55 assertions — the IN-PLACE re-anchors, and this figure is
                                         #    ⚠ DERIVED BY SUBTRACTION (174-119), ⛔ not measured per
                                         #    case, because the pre-slice binary no longer exists. The
                                         #    six cases that moved, at their MEASURED new totals:
                                         #    `ui16-grant-arms` 137 (the sweep went from EIGHT arms x2
                                         #    handles to ELEVEN, and S-38 gained three
                                         #    distinct-from-{failed,queued,parked} controls),
                                         #    `ui16-grant-perform` 15, `ui16-grant-correlate` 70,
                                         #    `ui16-grant-equiv` 18, `ui16-grantact` 33 and
                                         #    `ui16-grantpush` 25 (both re-anchored onto the SEND-TIME
                                         #    resolved dst, with the new ⛔-the-frozen-id-does-not-
                                         #    promote control), plus `ui16-route` 21 (`uisend`).
                                         # ⛔ NO case was deleted, and no case count fell.
                                         # The superseded pin follows, kept visible because the
                                         # derivation above is a DELTA on it:
                                         #
                                         # 2066 / 90717 — ★★ RE-SYNCED 2026-08-24 by §UI-16 N6 (the
                                         # `GRANT KEY` / `REJECT` act, its EIGHT-arm outcome mapping and the
                                         # `{dst, ctr}` `send_aired` correlation):
                                         # **2052 / 90330 -> 2066 / 90717** (+14 cases / +387 assertions).
                                         # DERIVED, and MEASURED case by case with `program -tc=`;
                                         # 293 + 70 + 21 + 2 + 1 = 387 closes EXACTLY:
                                         #  +8 cases / +293 assertions — the §UI-16 N6 block appended to
                                         #    `test/test_firmware_ui_invite.cpp` (target `uiinvite`): the
                                         #    TEAM plane named once (6), ★ all EIGHT arms x both handle
                                         #    values through the real perform path, with the NINE
                                         #    resulting words proved DISTINCT (101), the act's
                                         #    fail-closed target handling (15), ★ the `{dst, ctr}`
                                         #    correlation term by term incl. the terminal and
                                         #    zero-handle refusals (58), the confirmation's two identity
                                         #    rows (14), the forbidden-completion-word sweep over every
                                         #    state (78), the verdict carrier's offsetof-proved 8 bytes
                                         #    (5) and ★ PIN 12, the EQUIVALENCE case — one real-Node
                                         #    fixture driving the preflight AND the real
                                         #    `team_key_grant_send` at `authoritative`, one notch below
                                         #    and one above (16).
                                         #  +4 cases / +70 assertions — the §UI-16 N6 block appended to
                                         #    `test/test_firmware_ui_model.cpp` (target `model`): the act
                                         #    itself — short-then-double, ONE forward, the frozen hash,
                                         #    `GRANT QUEUED` and the terminal verdict (31); the push
                                         #    scope incl. the alarm's left-behind verdict (23); ★ pin 8,
                                         #    the EXPIRED window at the exact edge (10); and the
                                         #    unattached seam (6).
                                         #  +2 cases / +21 assertions — the §UI-16 N6 block appended to
                                         #    `test/test_firmware_ui_send.cpp` (target `uisend`): the
                                         #    router's two invite offers, and the measured OFFER ORDER
                                         #    (an armed UI slot keeps its own handle).
                                         #  +2 — `ui16-invreject`, extended in place: REJECT reaches the
                                         #    grant seam ZERO times and leaves no verdict.
                                         #  +1 — `ui16-reqpubkey-resources`: `sizeof(InviteGrantResult)`
                                         #    beside the moved `UiState`/`UiModel` figures (448 -> 456,
                                         #    872 -> 880; the window's offsets are UNMOVED).
                                         # ⚠ REPORTED, ⛔ NOT SILENTLY INHERITED: the figure this slice
                                         #   started from (2052 / 90330) is NOT the one the §CHROME-5
                                         #   entry below leaves (2043 / 90143). The 2043 -> 2052 /
                                         #   90143 -> 90330 step is §UI-16 N5's and reached this pin
                                         #   WITHOUT a derivation being written here. It was verified as
                                         #   the CLEAN baseline before this slice began (`pio test -e
                                         #   native` + the binary: 2052 / 90330 / 0 failed), so the
                                         #   arithmetic above is anchored on a MEASURED start, ⛔ not on
                                         #   the unexplained one.
                                         # ⛔ THE PREVIOUS ENTRY IS KEPT VISIBLE BELOW, unedited.
                                         # ---- (previous) 2026-08-23 by §CHROME-5 (the status
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
                                         #   `chrome-icons: the 24x24 MeshRoute mark decodes to the final logo_3…`
                                         #   24 the ASCII-art decode, one CHECK per pixel ROW (the picture IS the
                                         #      specification; the final asset re-pointed this same case)
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
                                         # ★ CROSS-CHECK RE-SYNCED 2026-08-24 by §UI-16 N5:
                                         #   **2043 / 90143 -> 2050 / 90283** (+7 cases / +140 assertions).
                                         #   DERIVED FROM THE SOURCE DIFF, not merely copied from the clean run:
                                         #   +7 cases are the three new invite-pure cases, the three N5 model
                                         #      flow cases and the one resource/offsetof case.
                                         #   +140 assertions = +89 direct doctest checks (+99 added -10 removed)
                                         #      plus +51 assertions executed by the existing cursor/menu/open
                                         #      helpers on the new and reshaped paths. The seven new cases close
                                         #      independently as 10 + 11 + 3 + 21 + 9 + 15 + 12 = 81; the
                                         #      remaining +59 are the N4 cases reshaped for the new preflight and
                                         #      REJECT/GRANT-ready boundary, including their helper executions.
                                         # ★ CROSS-CHECK RE-SYNCED 2026-08-24 by §UI-16 N5's QG BLOCKER FIX (the
                                         #   `WAITING FOR PUBKEY` claim is gated on the forward's answer):
                                         #   **2050 / 90283 -> 2052 / 90330** (+2 cases / +47 assertions).
                                         #   DERIVED, and MEASURED case by case with `program -tc=`;
                                         #   28 + 19 = 47 closes EXACTLY, with ⛔ no existing case edited:
                                         #   +1 case / +28 — `ui16-reqpubkey-started`
                                         #     (`test/test_firmware_ui_invite.cpp`, target `uiinvite`): the null
                                         #     seam and the zero target refused without spending a call (3), the
                                         #     ordinary acceptance with its carrier (6), the parse-failure shape
                                         #     INCLUDING the `code`-defaults-to-`queued` trap asserted rather
                                         #     than described (5), the six synchronous refusals x 2 (12) and the
                                         #     local-cache race counted as STARTED plus `accepted`-alone (2).
                                         #   +1 case / +19 — `ui16-reqpubkey-refused`
                                         #     (`test/test_firmware_ui_model.cpp`, target `model`): the
                                         #     unattached-seam arm driven through the real gestures (8) and the
                                         #     refusal/retry/parse-failure/race sequence with its completing
                                         #     push (11).
if os.environ.get("MR_MUT_BASE"):
    PIN_CASES, PIN_ASSERTS = (int(x) for x in os.environ["MR_MUT_BASE"].split(","))

# ★ THE DERIVED BASELINE, filled in by the baseline gate below from the clean run's own output and used by the exit
#   gate on the restored source. Declared here so the two gates read one pair of names, and so a reader of the exit
#   gate can see WHERE the figure it demands came from — it is this run's measurement, never a literal.
BASE_CASES = BASE_ASSERTS = None
# ⓘ ...and the cross-check itself is judged by the ORCHESTRATOR, once, against the baseline the worker trees agreed
#   on (see `orchestrate`'s `pin_stale`) — it is announced at BOTH ends of the merged report, exactly as before.

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
    # ⛔ `os._exit` SKIPS `atexit`, so an ORCHESTRATOR killed here would leave its workers running as orphans and its
    #   scratch trees on disk. The two roles need different teardown and this is the only handler either gets, so it
    #   does both — worker first (put the source back), then parent (kill the children, remove the trees).
    #   ⓘ `globals().get` because this handler is installed at IMPORT time, hundreds of lines before the orchestrator's
    #   helpers exist: a signal arriving in that window must not die of a NameError inside the handler.
    restore(f"signal {signum}")
    disarm()
    for _fn in ("_kill_workers", "_cleanup_scratch"):
        _f = globals().get(_fn)
        if _f:
            try:
                _f()
            except Exception:                      # a teardown that raises must not stop the other half
                pass
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
 # ⚠ M69 RE-ANCHORED AGAIN 2026-08-25 (§UI-16 K5), and recorded for the reason the 2026-08-20 retarget above is:
 #   K5's `saved_key_gesture` is the SAME two lines — `short` toggles, and BACK lands on the MENU — so the two-line
 #   anchor matched TWICE and the runner reported it VACUOUS. ★ The SEMANTIC is unchanged; the THIRD line
 #   (`run_create_team();`) is what makes it the CREATE confirmation's, and the saved-key twin has its own control
 #   (`--target=model` V29, `BACK` performs the install).
 ("M69 `short` in the confirmation ACTS instead of toggling (one press reaches CREATE)",
  "        if (g == Gesture::short_press) { prov_confirm_toggle(); return; }\n"
  "        if (_st.prov_confirm == ProvConfirm::back) { enter_provision(Provision::menu); return; }\n"
  "        run_create_team();",
  "        if (g == Gesture::short_press) { run_create_team(); return; }\n"
  "        if (_st.prov_confirm == ProvConfirm::back) { enter_provision(Provision::menu); return; }\n"
  "        run_create_team();"),
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
 # ⚠ M72 RE-ANCHORED 2026-08-25 (§UI-16 K5): the arm now forwards to `create_result_gesture()`, which decides WHERE
 #   the acknowledgement lands (the menu, or K5's saved-key offer when the joined team has a retained record). The
 #   old one-line anchor matched NOTHING and the runner reported it VACUOUS. ★ The SEMANTIC is unchanged and is the
 #   one that matters: acknowledging a result may ⛔ never re-run the act.
 ("M72 the result arm RE-RUNS the transaction on the press that leaves it",
  "            case Provision::create_result:  create_result_gesture();          return;",
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
  # ⚠ RE-ANCHORED 2026-08-25 (§UI-16 K6): the predicate gained a FIFTH child (`SAVED KEYS`), so the call it
  #   derives from now carries five parameters. Meaning unchanged — the parent row must be DERIVED from the child
  #   list and ⛔ never re-spelled from a fixed pair, which is the very drift this entry exists to catch.
  "    const ProvRowList l = provision_rows(create_team, join_static, join_team, invite, saved_keys);\n"
  "    for (uint8_t i = 0; i < l.n; ++i)\n"
  "        if (l.row[i] != ProvRow::back) return true;\n"
  "    return false;",
  "    return create_team || join_static;"),
 ("N03 ★★ the own-team id is not handed to the capture — the team we are already in is offered as a candidate",
  "        _st.nearby = nearby_capture(s.nearby, s.nearby_n, s.team_id);",
  "        _st.nearby = nearby_capture(s.nearby, s.nearby_n, 0);"),
 # ⚠ N04 RE-ANCHORED 2026-08-25 (§UI-17 keyrecv), and the re-anchor is RECORDED rather than the entry quietly
 #   rewritten (M69/M72's idiom): the bare `        list_follow_screen();` is no longer unique — the new
 #   `team_key_note_ack_landed` forwards to the SAME primitive at the same indent (deliberately: "leaving retires the
 #   view" is one invariant with two leave-paths, U1), so the one-line anchor matched TWICE and the runner reported it
 #   VACUOUS. ★ The SEMANTIC is unchanged and is still R-10's: the TICK must not re-read the scan. The following
 #   `sync_team_cursor(s);` is what makes the pair the TICK's.
 ("N04 ★★★ the scan is RE-READ EVERY TICK — owner ruling R-10's frozen-per-entry snapshot is gone and a team that "
  "walks into range inserts a row under the operator's cursor",
  "        list_follow_screen();\n        sync_team_cursor(s);",
  "        if (_st.provisioning == Provision::nearby) load_nearby(s);\n"
  "        list_follow_screen();\n        sync_team_cursor(s);"),
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
  "                load_invite(s);",
  "                ;"),
 # ★★★★ [[B249]] — THE FRESH-OPEN ANNOUNCEMENT'S THREE MODEL FAILURES. These are separate controls because a
 #      count-only case cannot prove the request followed the snapshot/window authority, and an order-only case
 #      cannot prove that redraw/tick/close refrain from repeating it.
 ("B249-1 ★★★ the fresh INVITE open never requests the existing triggered team announcement",
  "                if (_invite_dev) _invite_dev->request_team_announcement();",
  "                ;"),
 ("B249-2 ★★★ the fresh INVITE open requests the announcement TWICE",
  "                if (_invite_dev) _invite_dev->request_team_announcement();",
  "                if (_invite_dev) { _invite_dev->request_team_announcement(); _invite_dev->request_team_announcement(); }"),
 ("B249-3 ★★★ the announcement request precedes the member snapshot and established invitation arm",
  "                load_invite(s);\n"
  "                enter_provision(Provision::invite);\n"
  "                if (_invite_dev) _invite_dev->request_team_announcement();",
  "                if (_invite_dev) _invite_dev->request_team_announcement();\n"
  "                load_invite(s);\n"
  "                enter_provision(Provision::invite);"),
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
 ("V08 ★★★ the row is keyed by the DISPLAY NAME instead of the hash — N4's selection and N5's reqpubkey "
  "target both carry the name-derived value, and the name is MUTABLE (P-7d, node_hashlocate.cpp:346)",
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
 # ===== §UI-16 N5 — the EXPLICIT pubkey request and its enable/act boundary =====================================
 ("V10 ★★★ the request is AUTO-ISSUED on candidate entry — the operator never selected REQUEST PUBKEY or "
  "double-confirmed it (the owner-ratified ban reversed)",
  "        enter_provision(invite_grant_preflight(_invite_dev, _st.invite.sel_hash)\n"
  "                      ? Provision::invite_confirm : Provision::invite_need_pubkey);",
  "        if (_invite_dev) _invite_dev->issue(invite_reqpubkey_command(_st.invite.sel_hash));\n"
  "        enter_provision(invite_grant_preflight(_invite_dev, _st.invite.sel_hash)\n"
  "                      ? Provision::invite_confirm : Provision::invite_need_pubkey);"),
 ("V11 ★★ the NEED PUBKEY confirmation defaults to REQUEST PUBKEY instead of BACK — an immediate double airs it",
  "        _st.prov_confirm = ProvConfirm::back;",
  "        _st.prov_confirm = (p == Provision::invite_need_pubkey) ? ProvConfirm::confirm : ProvConfirm::back;"),
 ("V12 ★★★ the request timeout ENABLES GRANT KEY although no matching key arrived",
  "        if (window_active(s.now_ms)) return;\n"
  "        enter_provision(Provision::invite_closed);",
  "        if (_st.provisioning == Provision::invite_wait_pubkey &&\n"
  "            elapsed(s.now_ms, _last_input_ms) >= MESHROUTE_NS::protocol::hash_locate_giveup_ms) {\n"
  "            enter_provision(Provision::invite_confirm); return; }\n"
  "        if (window_active(s.now_ms)) return;\n"
  "        enter_provision(Provision::invite_closed);"),
 ("V13 ★★★ the PRESENCE OF A NAME enables GRANT KEY even when the key is only overheard (display metadata "
  "makes an airtime-and-secret decision)",
  "        enter_provision(invite_grant_preflight(_invite_dev, _st.invite.sel_hash)\n"
  "                      ? Provision::invite_confirm : Provision::invite_need_pubkey);",
  "        enter_provision((r.cand.name[0] || invite_grant_preflight(_invite_dev, _st.invite.sel_hash))\n"
  "                      ? Provision::invite_confirm : Provision::invite_need_pubkey);"),
 # ★★★★ V14 IS THE QG BLOCKER ITSELF, RESTORED (2026-08-24) — and it is a MODEL entry because the defect was the
 #      CALL SITE ignoring an answer, not the answer being wrong (the four ways to compute the answer wrongly are
 #      `--target=uiinvite` I16-I19). The shipped shape was `issue(...); enter_provision(wait)`, i.e. the screen
 #      claimed an outstanding request with NO seam attached and against a synchronous refusal — and pin 5 then
 #      leaves that claim on the panel for ever, because a timeout is ruled to change nothing.
 ("V14 ★★★ the WAITING FOR PUBKEY screen is entered whatever the seam answered — an unattached seam and a "
  "synchronous refusal both claim an outstanding request (the QG blocker, restored)",
  "        if (invite_issue_reqpubkey(_invite_dev, _st.invite.sel_hash))\n"
  "            enter_provision(Provision::invite_wait_pubkey);",
  "        (void)invite_issue_reqpubkey(_invite_dev, _st.invite.sel_hash);\n"
  "        enter_provision(Provision::invite_wait_pubkey);"),
 # ===== §UI-16 N6 — THE GRANT ACT's FLOW (the outcome mapping itself is `--target=uiinvite` I20-I30) ===========
 ("V15 ★★★ THE GRANT TARGET IS TAKEN FROM THE DISPLAY NAME (P-7d) — the private key is shipped to whatever the "
  "MUTABLE label hashes to, so two members sharing a name are one target and a rename re-aims the act",
  "        run_invite_grant(_st.invite.sel_hash);",
  "        { uint32_t k = 0;\n"
  "          for (const char* p = invite_name_of(s.member, s.team_shown, _st.invite.sel_hash); *p; ++p)\n"
  "              k = uint32_t(k * 31u + uint8_t(*p));\n"
  "          run_invite_grant(k); }"),
 # ⚠ V16/V17 RE-ANCHORED 2026-08-26 ([[B250]]): caller-aware landing gives `run_invite_reject` the snapshot and
 #   replaces its direct `enter_provision(invite)` with the one shared landing helper. The old patterns therefore
 #   matched zero sites in the first complete B250 battery (237 RED / 2 vacuous). The two properties are unchanged:
 #   REJECT must send nothing, and invitation-origin REJECT must add the selected hash before returning to its list.
 ("V16 ★★★ `REJECT` CALLS THE SEND — the SAFE default arm, the one selected on entry, ships the team's private "
  "key; one double-press on an unchanged confirmation grants instead of declining",
  "    void run_invite_reject(const UiSnapshot& s) {\n"
  "        (void)invite_handled_add(_st.invite, _st.invite.sel_hash);",
  "    void run_invite_reject(const UiSnapshot& s) {\n"
  "        run_invite_grant(_st.invite.sel_hash);\n"
  "        (void)invite_handled_add(_st.invite, _st.invite.sel_hash);"),
 ("V17 ★★ `REJECT` no longer adds the hash to the handled set (F-13) — the local refresh re-offers the candidate "
  "the operator has just declined, one tick later",
  "        (void)invite_handled_add(_st.invite, _st.invite.sel_hash);\n"
  "        leave_grant_chain(GrantExit::resume, s);",
  "        leave_grant_chain(GrantExit::resume, s);"),
 # ★★★★ V18 IS PIN 8, AND IT IS AN ORDERING DEFECT RATHER THAN A MISSING FEATURE: `on_gesture` runs BEFORE
 #      `on_tick`, so without this guard a `double` that lands after the ruled five minutes grants — and the tick
 #      that closes the window arrives afterwards, too late to have bounded anything.
 ("V18 ★★★ the window's own deadline is not consulted by the act — a grant fires out of an EXPIRED window "
  "because the closing tick has not run yet",
  "        if (!window_active(s.now_ms)) { enter_provision(Provision::invite_closed); return; }",
  "        (void)s;"),
 ("V19 ★★ the verdict is retired by the entry that OPENS it — the result screen renders the empty word for the "
  "act that just ran, and the correlated edge has nothing to promote",
  "        if (p != Provision::invite_result) _st.grant = InviteGrantResult{};",
  "        _st.grant = InviteGrantResult{};"),
 ("V20 ★★ a push may promote the verdict on ANY screen — an alarm (or a menu) left a queued handle in RAM, and a "
  "correlated edge upgrades a verdict nobody can see",
  "        if (_st.provisioning != Provision::invite_result) return false;",
  "        ;"),
 # ===== §UI-16 N6b (2026-08-24) — THE CORRELATION'S SECOND TERM ================================================
 # ★★★★ V21 IS THE QG BLOCKER ITSELF, RESTORED. The window freezes the row's team-local id; the core resolves the
 #      hash against the binding live AT SEND TIME. A member that re-ran team-DAD in between is granted on its NEW
 #      id — so a verdict carrying the FROZEN one can never be matched by the `send_aired` the core really emits,
 #      and the panel sits at `GRANT QUEUED` for ever. ⛔ The mutant is the TIDIER-LOOKING code (the id is right
 #      there in `_st.invite`), which is exactly why it needs a control rather than a comment.
 ("V21 ★★★ THE CORRELATION dst IS TAKEN FROM THE FROZEN SELECTION instead of the core's SEND-TIME resolution — a "
  "re-DAD inside the window leaves the verdict permanently unpromotable (§UI-16 N6b, QG blocker 2)",
  "        _st.grant = r;\n"
  "        enter_provision(Provision::invite_result);",
  "        r.dst = _st.invite.sel_id;\n"
  "        _st.grant = r;\n"
  "        enter_provision(Provision::invite_result);"),
 # ===== §UI-16 K4 — THE GRANT RECEIPT'S NOTE: THE MODEL HALF ===================================================
 # ★★★ The ROUTER's entries (`--target=uisend`, U10-U13) attack whether the push ever ARRIVES. These attack what
 #     the model DOES with it — the two ruled sentences, and the three things a push must never do.
 ("V22 ★★★ THE TWO RULED SENTENCES ARE SWAPPED — a save that FAILED renders `TEAM KEY RECEIVED`, which is the one "
  "word spec §4-K4 forbids on that path (the key is live in RAM and will not survive a reboot)",
  "        note.outcome = saved ? UiProvOutcome::team_key_received : UiProvOutcome::team_key_unsaved;",
  "        note.outcome = saved ? UiProvOutcome::team_key_unsaved : UiProvOutcome::team_key_received;"),
 ("V23 ★★★ THE NOTE NAVIGATES — a radio arrival opens the provisioning result arm under the operator's thumb, "
  "which is the one thing spec §4-K4 pin 3 rules out in as many words",
  "        _st.dirty = true;                      // a repaint is owed; ⛔ a wake is not (see above)",
  "        enter_provision(Provision::create_result);\n"
  "        _st.dirty = true;"),
 ("V24 ★★★ THE NOTE WAKES A DARK PANEL — §UI-17 R-7 scoped the wake to a DM addressed to us and a SEALED channel "
  "post; widening it here is a new owner ruling nobody made",
  "        _st.dirty = true;                      // a repaint is owed; ⛔ a wake is not (see above)",
  "        unblank(now_ms);\n        _st.dirty = true;"),
 ("V25 ★★ THE NOTE IS WRITTEN WITHOUT CLEARING THE SLOT — a previous verdict's `reason` / `team_id` / `node_id` "
  "survive under the new headline, so the panel renders another act's data beside this one's word",
  "        UiProvAnswer note{};",
  "        UiProvAnswer note = _st.prov_answer;"),
 ("V26 ★★ THE SUCCESS LEXEME IS RE-SPELLED — S-25 is §3.6.4's own word carried VERBATIM, and it is declared once "
  "so an owner re-ruling changes it in exactly one place",
  '        case UiProvOutcome::team_key_received: return "TEAM KEY RECEIVED";',
  '        case UiProvOutcome::team_key_received: return "KEY RECEIVED";'),
 ("V27 ★★ THE FAILURE'S SECOND ROW IS CLIPPED INTO THE FIRST — the ruled 26-column sentence is truncated to fit "
  "the 19-column body instead of rendering across two rows (§7.1 rule 5 — a durability claim may not be clipped)",
  '        case UiProvOutcome::team_key_unsaved: return "NOT SAVED";',
  '        case UiProvOutcome::team_key_unsaved: return "NOT SAVED - LOST ON";'),
 ("V28 ★ THE THIRD ROW BECOMES A GENERAL SLOT — `save_failed` grows one too, so the row stops meaning \"the ruled "
  "sentence continues here\" and the arm that owns it can no longer be told from the arms that do not",
  "        case UiProvOutcome::save_failed:\n        case UiProvOutcome::refused:",
  '        case UiProvOutcome::save_failed:       return "RETRY";\n        case UiProvOutcome::refused:'),
 # ===== §UI-16 K5 — THE SAVED-KEY OFFER: WHERE IT OPENS, WHAT IT DEFAULTS TO, WHAT EACH CHOICE COSTS ===========
 # ★★★★ V29 IS THE MODEL'S HALF OF THE P-2b PAIR: `BACK` — the arm the screen OPENS ON — performs the install. One
 #      press on the SAFE action reactivates a stored secret, which is the shape the delete modal's two separate
 #      branches exist to prevent, arriving in the one flow where the act is a key.
 ("V29 ★★★★ `BACK` PERFORMS THE INSTALL — the SAFE default arm activates the saved key, so declining the offer "
  "does the thing the offer was asking about (P-2b, the model's headline)",
  "        if (_st.prov_confirm == ProvConfirm::back) { enter_provision(Provision::menu); return; }\n"
  "        run_use_saved_key();",
  "        run_use_saved_key();"),
 ("V30 ★★★ THE OFFER OPENS WITHOUT THE KEYRING'S REPORT — every nearby join lands on `SAVED KEY FOUND`, so the "
  "operator is offered a key for a team no record exists for",
  "        if (a.outcome == UiProvOutcome::team_joined && a.saved_key && a.team_id != 0) {",
  "        if (a.outcome == UiProvOutcome::team_joined && a.team_id != 0) {"),
 ("V31 ★★ THE OFFER OPENS ON **ANY** ACKNOWLEDGEMENT — a CREATE's result screen leads into a saved-key offer for "
  "a team whose key was just minted",
  "        if (a.outcome == UiProvOutcome::team_joined && a.saved_key && a.team_id != 0) {",
  "        if (a.saved_key && a.team_id != 0) {"),
 ("V32 ★★★ THE ACT IS KEYED ON THE **FINGERPRINT** — the low 24 bits the offer screen printed, which 255 other "
  "teams share, instead of the id the transaction joined ([[B48]]'s class, over a stored secret)",
  "            in.team_id = _st.saved_key_team;     // ★ the JOINED team's identity, whole (U2)",
  "            in.team_id = _st.saved_key_team & 0x00FFFFFFu;"),
 ("V33 ★★★ THE OFFER OPENS ON THE **ACT** — `prov_confirm` is moved to CONFIRM after the entry, so the first "
  "`double` installs a stored secret (P-13 broken at the one screen that reaches a key)",
  "            enter_provision(Provision::saved_key);\n"
  "            _st.saved_key_team = a.team_id;           // ★ the joined team's identity, whole (U2)",
  "            enter_provision(Provision::saved_key);\n"
  "            _st.prov_confirm = ProvConfirm::confirm;\n"
  "            _st.saved_key_team = a.team_id;"),
 ("V34 ★★★ THE UNFINISHED OFFER SURVIVES THE BLANK — the operator wakes onto a screen they may not remember "
  "opening, one `double` from installing a key (OQ-3, and the arm is the one that reaches a secret)",
  "            if (_st.provisioning == Provision::saved_key) enter_provision(Provision::menu);",
  "            ;"),
 ("V35 ★★ THE OFFER'S TARGET IS NOT RETIRED BY THE ENTRY — a stale team id survives into later screens, so a "
  "later act can be aimed at a team the operator left",
  "        _st.saved_key_team = 0;",
  "        ;"),
 ("V36 ★★ THE SUCCESS AND FAILURE ENDINGS COLLAPSE INTO ONE WORD — a failed activation renders `TEAM KEY ACTIVE`, "
  "which is the 'success that isn't' over a node that holds no key",
  # ⚠ RE-ANCHORED 2026-08-25 (QG blocker 1): the failure head is now `kSavedKeyFailedText` (spec §8 S-39),
  #   because `NO TEAM KEY` became a sentence that can be FALSE once the refusals stopped clearing. The SEMANTIC is
  #   unchanged — a failed activation may ⛔ never render the success word.
  "        case UiProvOutcome::saved_key_failed:  return kSavedKeyFailedText;",
  "        case UiProvOutcome::saved_key_failed:  return \"TEAM KEY ACTIVE\";"),
 ("V37 ★★ THE SUCCESS SCREEN GROWS THE DURABILITY WARNING — `NOT SAVED` under a key that IS durable, i.e. S-27's "
  "ruled sentence said about the one path where it is false",
  "        case UiProvOutcome::saved_key_used:\n"
  "        // ⓘ `team_key_received`'s second row is deliberately EMPTY",
  "        case UiProvOutcome::saved_key_used:    return \"NOT SAVED\";\n"
  "        // ⓘ `team_key_received`'s second row is deliberately EMPTY"),
 ("V38 ★ the ruled offer lexeme is re-spelled — S-28 is owner-ruled and declared once, so a re-ruling must change "
  "it in exactly one place",
  'inline constexpr const char* kSavedKeyTitle = "SAVED KEY FOUND";',
  'inline constexpr const char* kSavedKeyTitle = "SAVED KEY";'),
 ("V39 ★ the ruled ACTION lexeme is re-spelled — S-29 is owner-ruled, and the word the operator presses is the "
  "one place the offer states what it will do",
  '        case ProvConfirm::confirm: return "USE SAVED KEY";',
  '        case ProvConfirm::confirm: return "INSTALL KEY";'),
 # ===== §UI-17 keyrecv — WHERE THE `TEAM KEY RECEIVED` ACKNOWLEDGEMENT LANDS (owner-ruled 2026-08-25, shape (a)) ===
 # ★★★ THREE ENTRIES, ONE PER WAY THIS RULING CAN BE UNDONE: the ask reverted, the ask over-applied, and the ⛔
 #     a-push-never-navigates control re-proven THROUGH the new landing (it is the K4 pin that a new destination is
 #     most likely to erode: once a press can navigate, wiring the arrival to the same helper is one line away).
 ("V40 ★★★★ THE LANDING IS REVERTED TO THE MENU — the owner's ask undone, and undone in the shape it would really "
  "arrive in: the helper stays, and simply goes back where every other terminal goes",
  "        if (_st.prov_answer.outcome != UiProvOutcome::team_key_received) return false;\n"
  "        _st.screen = Screen::status;\n"
  "        _st.cursor = 0;",
  "        if (_st.prov_answer.outcome != UiProvOutcome::team_key_received) return false;\n"
  "        enter_provision(Provision::menu);"),
 ("V41 ★★★ THE SCOPE OVERRUN — the FAILURE pair jumps to STATUS too, so a save that did NOT survive walks the "
  "operator away from the flow the remedies are in (the ⛔ consistency extension nobody ruled)",
  "        if (_st.prov_answer.outcome != UiProvOutcome::team_key_received) return false;",
  "        if (_st.prov_answer.outcome != UiProvOutcome::team_key_received\n"
  "                && _st.prov_answer.outcome != UiProvOutcome::team_key_unsaved) return false;"),
 ("V42 ★★★★ THE **ARRIVAL** NAVIGATES, THROUGH THE NEW LANDING — a radio receipt moves the panel to STATUS under "
  "the operator's thumb. ⛔ Spec §4-K4 pin 3 is unchanged by the ruling: only the PRESS may choose a destination",
  "        _st.dirty = true;                      // a repaint is owed; ⛔ a wake is not (see above)",
  "        team_key_note_ack_landed();\n"
  "        _st.dirty = true;                      // a repaint is owed; ⛔ a wake is not (see above)"),

 # ===== §UI-16 K6 — SAVED-KEY RETENTION MANAGEMENT, THE MODEL'S HALF (⛔ never "key rotation") ===================
 # ★★★★ THE FIVE WAYS THIS FLOW CAN BE UNDONE FROM THE MODEL, and each is a TEMPTING SIMPLIFICATION rather than a
 #      deletion: the confirmation bypassed, the PROTECTED row given a destructive screen, the act keyed on the
 #      cursor instead of the row's identity, the create RESUMED after the removal, and the "refreshed list" that
 #      is not refreshed. ⓘ The SERVICE's own rules are `--target=teamkeyring` T55-T66; these attack the FLOW.
 ("V43 ★★★★ THE CONFIRMATION IS BYPASSED — `double` on **BACK** falls through into the removal, so one press "
  "destroys a stored secret the operator was still looking at (P-13 broken at the one screen that is irreversible)",
  "        if (_st.prov_confirm == ProvConfirm::back) { enter_provision(Provision::saved_keys); return; }",
  "        if (_st.prov_confirm == ProvConfirm::back) { }"),
 ("V44 ★★★★ THE **ACTIVE** ROW IS GIVEN THE DESTRUCTIVE CONFIRMATION — the protected record lands on a screen "
  "that offers `FORGET KEY`, so the panel invites the one removal the service must refuse (and the operator learns "
  "the refusal only after pressing it)",
  "        enter_provision(r.key.active ? Provision::saved_keys_active : Provision::saved_keys_confirm);",
  "        enter_provision(Provision::saved_keys_confirm);"),
 ("V45 ★★★★ THE REMOVAL IS KEYED ON THE **CURSOR** — a row INDEX stands in for the record's identity, and the list "
  "skips a corrupt zero-id record, so the index names a different record than the panel drew (§B66, over a delete)",
  "        _st.forget_team = r.key.team_id;",
  "        _st.forget_team = _st.cursor;"),
 ("V46 ★★★★ THE CREATE IS **AUTOMATICALLY RESUMED** after the full-keyring refusal — the ruling's *two explicit "
  "transactions* collapsed into one, so a create that fails its second write can destroy an unrelated saved key on "
  "the way, and the operator never chose either half",
  "        if (a.outcome == UiProvOutcome::refused && a.keyring_full) {\n"
  "            load_saved_keys();\n"
  "            enter_provision(Provision::saved_keys);\n"
  "            return;\n"
  "        }",
  "        if (a.outcome == UiProvOutcome::refused && a.keyring_full) {\n"
  "            run_create_team();\n"
  "            return;\n"
  "        }"),
 ("V47 ★★★ THE \"REFRESHED LIST\" IS NOT REFRESHED — acknowledging `KEY FORGOTTEN` returns to the FROZEN copy, so "
  "the record just removed is still on the panel: a screen contradicting an act it reported as complete",
  "    void saved_keys_result_gesture() {\n"
  "        load_saved_keys();\n"
  "        enter_provision(Provision::saved_keys);\n"
  "    }",
  "    void saved_keys_result_gesture() {\n"
  "        enter_provision(Provision::saved_keys);\n"
  "    }"),
 ("V48 ★★★ A FAILED REMOVAL RENDERS THE SUCCESS WORD — `KEY FORGOTTEN` for a store that refused or a write that "
  "did not land, which is the 'success that isn't' over a key that is still there (spec §4-K6 pin 5)",
  "        case UiProvOutcome::key_forget_failed: return kKeyNotForgottenText;",
  "        case UiProvOutcome::key_forget_failed: return kKeyForgottenText;"),
 ("V49 ★★ THE FULL-STORE DOOR OPENS ON THE DISPLAY TOKEN — the navigation decision is taken by comparing "
  "`reason`'s TEXT instead of the typed flag, so a re-worded service token silently closes the one route out of "
  "the dead end ([[B48]]'s class at the navigation layer)",
  "        if (a.outcome == UiProvOutcome::refused && a.keyring_full) {",
  "        if (a.outcome == UiProvOutcome::refused && a.reason[0] == 'k' && a.reason[8] == 'f') {"),
 # ===== §UI-16 K6 (QG blocker, 2026-08-25) — THE RECEIVED GRANT'S ACK, AT THE MODEL ============================
 ("V51 ★★★★ THE RECEIVED-GRANT FULL LANDING IS DROPPED — the fifth received grant shows three correct rows and then "
  "acknowledges into the MENU, which is the QG blocker restored at the layer that owns the landing",
  "        if (!_st.prov_answer.keyring_full) return false;",
  "        return false;"),
 ("V52 ★★★ **EVERY** UNSAVED RECEIPT ACKNOWLEDGES INTO THE REMOVAL LIST — a corrupt store, an unreadable record or "
  "a failed activation all walk the operator into a screen that offers to delete a key, which is a FALSE remedy",
  "        if (!_st.prov_answer.keyring_full) return false;",
  "        ;"),
 ("V53 ★★★ THE FLAG IS ACCEPTED ON THE **SAVED** ARM — a `TEAM KEY RECEIVED` screen whose acknowledgement opens a "
  "removal list, i.e. the 'success that isn't' from the other side",
  "        note.keyring_full = !saved && keyring_full;",
  "        note.keyring_full = keyring_full;"),
 ("V54 ★★★ THE FULL LANDING IS KEYED ON THE **ARM** INSTEAD OF THE ANSWER — the static-join result screen renders "
  "the same note and would then give ONE receipt TWO endings",
  "        if (_st.prov_answer.outcome != UiProvOutcome::team_key_unsaved) return false;",
  "        if (_st.provisioning != Provision::create_result) return false;"),


 # ===== §UI-16 K7 ([[B245]]) — THE ROSTER GRANT'S ENTRY ===========================================================
 # ★★★ THE SLICE IS AN **ENTRY POINT**, so its controls attack exactly that: WHERE the act hangs, WHEN it is
 #     offered, WHAT identity it freezes, and that it REACHES the landed N5/N6 chain rather than a second copy of
 #     it. ⓘ The chain's own rulings keep their own battery (`--target=uiinvite`), which K7 leaves byte-identical —
 #     `src/firmware_ui_invite.h` is not touched by this slice at all, so those entries re-run unchanged.
 ("W01 ★★★★ THE ACT AUTO-FIRES ON ROW SELECTION — merely opening a member's act sub-view opens the grant chain, "
  "so the operator is put one press from shipping a private key by an act they did not ask for (P-12, the "
  "no-unsolicited shape, and the headline control of this slice)",
  "            _st.compose_grant_hash = team_member_hash_of(s.member, s.team_shown, _team_sel_id);\n"
  "            _st.compose_grant_row  = compose_grant_offered(/*dm=*/true, s.prov_invite, s.team_key_present,\n"
  "                                                           _st.compose_grant_hash, s.my_key_hash32);",
  "            _st.compose_grant_hash = team_member_hash_of(s.member, s.team_shown, _team_sel_id);\n"
  "            _st.compose_grant_row  = compose_grant_offered(/*dm=*/true, s.prov_invite, s.team_key_present,\n"
  "                                                           _st.compose_grant_hash, s.my_key_hash32);\n"
  "            if (_st.compose_grant_row) { run_roster_grant(s); return; }"),
 ("W02 ★★★★ THE CONFIRMATION IS SKIPPED — the act performs the grant itself instead of opening the chain's "
  "REJECT-default screen, so ONE double ships the key (P-13: the deliberate short-then-double is the whole guard)",
  "        enter_provision(invite_grant_preflight(_invite_dev, target)\n"
  "                      ? Provision::invite_confirm : Provision::invite_need_pubkey);",
  "        if (invite_grant_preflight(_invite_dev, target)) { run_invite_grant(target); return; }\n"
  "        enter_provision(Provision::invite_need_pubkey);"),
 ("W03 ★★★★ A **SECOND OUTCOME MAPPING IS FORKED** — the act stops going through the ONE reused call and maps the "
  "dispatch itself, so the panel's word and the core's word are free to disagree (the other headline; the anchor "
  "IS the reused call, which is what makes 'K7 adds no mapping' measurable rather than argued)",
  "        run_invite_grant(_st.invite.sel_hash);",
  "        { InviteGrantResult r{}; uint16_t c = 0; uint8_t d = 0;\n"
  "          const MESHROUTE_NS::Node::TeamKeyGrantTx tx =\n"
  "              _invite_dev ? _invite_dev->grant(_st.invite.sel_hash, kInviteGrantPlane, &c, &d)\n"
  "                          : MESHROUTE_NS::Node::TeamKeyGrantTx::queued;\n"
  "          r.hash = _st.invite.sel_hash; r.ctr = c; r.dst = d;\n"
  "          r.st = (tx == MESHROUTE_NS::Node::TeamKeyGrantTx::queued) ? InviteGrantState::sent\n"
  "                                                                    : InviteGrantState::failed;\n"
  "          _st.grant = r; enter_provision(Provision::invite_result); }"),
 ("W04 ★★★ THE TARGET IS RE-RESOLVED AT PRESS TIME FROM THE MUTABLE TEAM-LOCAL ID instead of the hash frozen when "
  "the operator pointed at the row — a member that re-ran team-DAD between the two is granted to whoever now "
  "wears its id, or to nobody (P-7d)",
  "        const uint32_t target = _st.compose_grant_hash;",
  "        const uint32_t target = team_member_hash_of(s.member, s.team_shown, _st.compose_peer);"),
 ("W05 ★★★ THE TARGET IS KEYED BY THE **DISPLAY NAME** — a mutable, self-asserted label decides whose private key "
  "is shipped, which is [[B48]]'s class at the worst possible site (P-7d)",
  "    for (uint8_t i = 0; i < n; ++i) if (mem[i].id == id) return mem[i].key_hash32;",
  "    for (uint8_t i = 0; i < n; ++i) if (mem[i].name[0] != '\\0') return mem[i].key_hash32;"),
 ("W06 ★★★ THE SELF REFUSAL IS DROPPED — the roster row that is US offers a GRANT KEY, and the operator is invited "
  "to run a pubkey ceremony against themselves",
  "    if (member_hash32 == own_hash32) return false;",
  "    // (the self veto dropped)"),
 ("W07 ★★★ THE KEYLESS OFFER APPEARS — a node with NO team content key offers to grant one, so the act's own "
  "precondition is discovered at the seam instead of never being offered (§K7 pin 6)",
  "    if (!dm || !can_grant || !team_key_present) return false;",
  "    if (!dm || !can_grant) return false;"),
 ("W08 ★★ THE AUTHORITATIVE FLOOR IS DROPPED — a ROUTE-ONLY member (no binding, no seal target) is offered as a "
  "grant target, which is the same fail-OPEN F-7 keeps off the invite list",
  "    if (member_hash32 == 0) return false;\n"
  "    if (member_hash32 == own_hash32) return false;",
  "    if (member_hash32 == own_hash32) return false;"),
 ("W09 ★★★ THE ROSTER ENTRY TAKES THE WINDOW'S SNAPSHOT — the door that deliberately bypasses the F-11 diff starts "
  "feeding it, so the two authorities and the volatile handled set acquire a second, unruled writer",
  "        _st.invite = InviteWindow{};\n"
  "        _st.invite.sel_hash = target;",
  "        _st.invite = invite_snapshot_take(s.member, s.team_shown);\n"
  "        _st.invite.sel_hash = target;"),
 # ⓘ RE-ANCHORED 2026-08-26 (§UI-10/11 P3), and the withdrawn pattern is KEPT VISIBLE:
 #   `"    if (grant && idx == sendable) return ComposeRow::grant;"`. P3 replaced the compile-time `sendable` with
 #   the PROJECTION's own length, so the old pattern matched NOTHING — and a control that matches nothing is
 #   VACUOUS. ⛔ MEASURED, ⛔ not anticipated: the first full pass after the slice reported exactly that. The
 #   PROPERTY is unchanged (the optional row's slot must be claimed only when the act is OFFERED); only the
 #   expression it is spelled against moved.
 ("W10 ★★ THE OPTIONAL ROW IS RESOLVED POSITIONALLY AGAIN (§B66) — the grant's slot is claimed whether or not the "
  "act is offered, so on every ordinary DM sub-view `back, don't send` becomes the grant row",
  "    if (grant && idx == l.n) return ComposeRow::grant;",
  "    if (idx == l.n) return ComposeRow::grant;"),
 ("W11 ★★ THE RULED FIVE-MINUTE BOUND IS NOT ARMED — the approval opened from the roster has no deadline at all, "
  "so N6 pin 8's 'the grant is unreachable with the window closed' guard never applies to this door",
  "        _invite_until_ms    = s.now_ms + kInviteWindowMs;",
  "        // (no deadline armed)"),

 # ===== [[B250]] — THE ROSTER GRANT'S EXPLICIT CALLER + IDENTITY-SAFE RETURN ===================================
 ("W12 ★★★★ THE ROSTER-ORIGIN BINDING IS DELETED while the rest of run_roster_grant remains — every shared "
  "exit sees `none` and falls to PROVISION instead of returning to the member who opened it",
  "        _grant_return = {GrantOrigin::team_roster, peer};     // bind the caller and navigation identity together",
  "        clear_grant_return();                                // caller binding deleted"),
 ("W13 ★★★ THE CALLER IS DERIVED FROM InviteWindow::taken instead of bound by the roster action — the empty "
  "roster carrier is treated as having no caller and terminal acknowledgement loses its parent",
  "        _grant_return = {GrantOrigin::team_roster, peer};     // bind the caller and navigation identity together",
  "        _grant_return = {_st.invite.taken ? GrantOrigin::invite_window : GrantOrigin::none, peer};"),
 ("W14 ★★★ BOTH ORIGINS COLLAPSE TO THE INVITATION LANDING — a roster REJECT/ack opens PROVISION's invitation "
  "parent instead of returning to TEAM",
  "            case GrantOrigin::team_roster:\n"
  "                return_to_team_roster(back.team_local_id, s);\n"
  "                return;",
  "            case GrantOrigin::team_roster:\n"
  "                enter_provision(how == GrantExit::resume ? Provision::invite : Provision::menu);\n"
  "                return;"),
 ("W15 ★★★ BOTH ORIGINS COLLAPSE TO THE TEAM LANDING — an invitation-window REJECT leaves its window and "
  "selects a roster row the invitation never named",
  "            case GrantOrigin::invite_window:\n"
  "                enter_provision(how == GrantExit::resume ? Provision::invite : Provision::menu);\n"
  "                return;",
  "            case GrantOrigin::invite_window:\n"
  "                return_to_team_roster(back.team_local_id, s);\n"
  "                return;"),
 ("W16 ★★★ GrantOrigin::none INFERS THE INVITATION PARENT instead of failing closed — the natural no-selection "
  "expiry re-opens an invitation list it has no selected caller for",
  "            case GrantOrigin::none:\n"
  "                enter_provision(Provision::menu);\n"
  "                return;",
  "            case GrantOrigin::none:\n"
  "                enter_provision(Provision::invite);\n"
  "                return;"),
 ("W17 ★★★ THE ROSTER IS RESTORED PASSIVE — the caller's entered list is lost and the next double only enters "
  "row zero instead of operating at the saved member",
  "        settings_follow_screen();              // closes SETTINGS/provisioning through the existing primitive (U1)\n"
  "        _st.list_view = ListView::interactive;",
  "        settings_follow_screen();              // closes SETTINGS/provisioning through the existing primitive (U1)\n"
  "        _st.list_view = ListView::passive;"),
 ("W18 ★★★ SETTINGS/PROVISIONING IS LEFT OPEN BEHIND THE TEAM BODY — the top-level screen says TEAM while the "
  "sub-state still owns SETTINGS",
  "        settings_follow_screen();              // closes SETTINGS/provisioning through the existing primitive (U1)",
  "        ;                                       // SETTINGS/provisioning left open"),
 ("W19 ★★★ THE RETURN RESTORES ROW ZERO instead of the saved TEAM-local identity — selecting member 1 and "
  "returning silently points at member 0",
  "        _team_sel_id = team_local_id;",
  "        _team_sel_id = s.team[0].id;"),
 ("W20 ★★★ THE B64 IDENTITY-FOLLOW STEP IS REMOVED — a member reordered while the terminal screen was up "
  "leaves the highlight at the old row",
  "        _st.dirty = true;\n"
  "        sync_team_cursor(s);",
  "        _st.dirty = true;"),
 ("W21 ★★★ A GONE RETURN SELECTS ITS NEIGHBOUR — the existing B64 refusal is replaced by row zero, so the "
  "next double opens a DM for somebody else",
  "        _team_sel_valid = false; _st.team_pick_gone = true; _st.dirty = true;",
  "        _team_sel_id = s.team[0].id; _team_sel_valid = true; _st.team_pick_gone = false; _st.dirty = true;"),
 ("W22 ★★★ A MATCHING ROSTER PUBKEY PUSH RETURNS TO THE INVITATION LIST — it loses the operation the operator "
  "started instead of opening the safe-default confirmation",
  "            case GrantOrigin::team_roster:\n"
  "                enter_provision(Provision::invite_confirm);\n"
  "                return;",
  "            case GrantOrigin::team_roster:\n"
  "                enter_provision(Provision::invite);\n"
  "                return;"),
 ("W23 ★★★ A MATCHING INVITATION PUBKEY PUSH OPENS THE ROSTER CONFIRMATION — invite-origin behaviour is no "
  "longer byte-identical and the refreshed candidate list is skipped",
  "            case GrantOrigin::invite_window:\n"
  "                enter_provision(Provision::invite);\n"
  "                return;",
  "            case GrantOrigin::invite_window:\n"
  "                enter_provision(Provision::invite_confirm);\n"
  "                return;"),
 ("W24 ★★★ A RETURN REPEATS THE PUBKEY REQUEST — cancellation/acknowledgement grows an unauthorised second "
  "device command instead of being navigation-only",
  "            case GrantOrigin::team_roster:\n"
  "                return_to_team_roster(back.team_local_id, s);",
  "            case GrantOrigin::team_roster:\n"
  "                (void)invite_issue_reqpubkey(_invite_dev, _st.invite.sel_hash);\n"
  "                return_to_team_roster(back.team_local_id, s);"),
 ("W25 ★★★ A TERMINAL ACKNOWLEDGEMENT REPEATS THE GRANT — the result's frozen hash is sent a second time "
  "before the roster landing",
  "            case GrantOrigin::team_roster:\n"
  "                return_to_team_roster(back.team_local_id, s);",
  "            case GrantOrigin::team_roster:\n"
  "                if (_st.grant.hash) run_invite_grant(_st.grant.hash);\n"
  "                return_to_team_roster(back.team_local_id, s);"),
 ("W26 ★★★ AN UNRELATED CLOSE CLEARS ONLY THE ROSTER PAYLOAD, NOT ITS ORIGIN — a later invitation window with no "
  "selected candidate inherits TEAM as its terminal parent instead of failing closed to PROVISION",
  "    void clear_grant_return() { _grant_return = GrantReturn{}; }",
  "    void clear_grant_return() { _grant_return.team_local_id = 0; }"),
 ("W27 ★★★ EMERGENCY RETIRES INVITATION CALLERS BUT LEAVES A ROSTER CALLER ARMED — after pre-emption, a fresh "
  "unbound expiry jumps to TEAM instead of the fail-closed PROVISION parent",
  "    void clear_grant_return() { _grant_return = GrantReturn{}; }",
  "    void clear_grant_return() {\n"
  "        if (_grant_return.origin == GrantOrigin::invite_window) _grant_return = GrantReturn{};\n"
  "    }"),

 ("V50 ★ the ruled RETENTION lexemes are re-spelled — S-40/S-42 are owner-ruled and declared once, so a re-ruling "
  "must change each in exactly one place (the V38/V39 treatment, one screen over)",
  'inline constexpr const char* kSavedKeysTitle    = "SAVED KEYS";      // S-40 — the PROVISION child row AND the title',
  'inline constexpr const char* kSavedKeysTitle    = "SAVED KEY LIST";'),

 # ===== §UI-10/11 P3 — THE CATALOG REACHES THE PANEL ==========================================================
 # ★★★ EVERY ONE IS A TEMPTING WRONG FIX RATHER THAN A DELETION, and each attacks a decision the design or the
 #     owner RULED. ⓘ The two decisions that live in `firmware_ui_send.h` (the stale-generation gate and the `-l`
 #     policy) are `--target=uisend`'s U14/U15; the ones in `src/firmware_ui.cpp` are the probe's C137-C141.
 ("Y01 \u2605\u2605\u2605\u2605 THE SLOT IS DERIVED FROM THE ROW INDEX \u2014 \u00a7B66's exact defect, and on a GAPPED catalog "
  "(dm1/dm4/dm8) row 1 then sends dm2's phrase to somebody the wearer picked dm4 for",
  "              compose_row_slot(_st.cursor, list), _st.compose_gen);",
  "              uint8_t(mrfw::kPresetDmFirst + _st.cursor), _st.compose_gen);"),
 ("Y02 \u2605\u2605\u2605 A DISABLED SLOT IS RENDERED \u2014 the tempting \"show them all, the wearer can see which are "
  "empty\". \u00a73.2.2 says the OLED lists only ENABLED slots, and a disabled row is a ZEROED row: a blank line that SENDS",
  "        if (!s.enabled) continue;                                   // \u2605 ENABLED ONLY",
  "        ;"),
 ("Y03 \u2605\u2605\u2605\u2605 THE L / - COLUMN IS DROPPED FROM A PRESET ROW \u2014 OQ-A's premise is that BOTH location "
  "states are shown, because the wearer CONFIRMS the marker as part of the double press. A blank column reads as `-`",
  "        case ComposeRow::text:  return l.row[idx].loc ? 'L' : '-';",
  "        case ComposeRow::text:  return '\\0';"),
 ("Y04 \u2605\u2605\u2605 THE FIXED TABLE IS RESURRECTED \u2014 the projection bypassed and the retired kDmTexts re-created "
  "inside the row resolver. The panel then shows the compiled defaults for ever while the verbs edit a record nothing reads",
  "        case ComposeRow::text:  return l.row[idx].text;",
  "        case ComposeRow::text:  { static const char* const t[] = { \"Are you OK?\", \"I'm OK\" };\n"
  "                                  return idx < 2 ? t[idx] : l.row[idx].text; }"),
 ("Y05 \u2605\u2605\u2605\u2605 K7's GRANT KEY ROW IS DISPLACED \u2014 pinned at the COMPILED list's length instead of the "
  "projection's, which is R-1's \"may not move K7's row\" broken the moment the wearer enables a third preset",
  "    if (grant && idx == l.n) return ComposeRow::grant;",
  "    if (grant && idx == 2) return ComposeRow::grant;"),
 ("Y06 \u2605\u2605\u2605\u2605 THE MODAL CLOSES ON A NO-OP \u2014 the equality dropped, so ANY ui preset verb (including an "
  "identical set that wrote nothing) shuts an open compose. Spec \u00a72's table rules the opposite in two of its rows",
  "        return _st.compose != Compose::none && !_st.compose_result && _st.compose_gen != s.preset_generation;",
  "        return _st.compose != Compose::none && !_st.compose_result;"),
 ("Y07 \u2605\u2605\u2605 THE EMPTY STATE IS BYPASSED \u2014 \u00a73.2.1's note never answered, so a wearer who cleared every "
  "preset gets a sub-view with one unexplained back row and no reason for it",
  "inline const char* compose_empty_note(const ComposeList& l) { return l.n == 0 ? kNoPresetsText : nullptr; }",
  "inline const char* compose_empty_note(const ComposeList& l) { (void)l; return nullptr; }"),
 ("Y08 \u2605\u2605\u2605\u2605 PRESET CHANGED FALLS THROUGH TO THE GENERIC FAILURE \u2014 spec \u00a72 gives the refusal a "
  "RULED VISIBLE WORD precisely to forbid this: the operator is told his message FAILED when NOTHING was submitted",
  "        if (k == SendKind::dm)                   _dm   = DmState::preset_changed;",
  "        if (k == SendKind::dm)                   _dm   = DmState::failed;"),
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
 #     produce a PICTURE rather than a compile error. These tables pin the owner-supplied final `logo_3.png` art;
 #     never drop them, or a later asset swap can ship mirrored or in the wrong bit order.
 ("I05 the final MARK is authored MIRRORED (left and right exchange places)",
  "inline constexpr uint8_t kMarkMeshRoute[72] = {\n"
  "    0x00, 0x00, 0x00,\n"
  "    0x00, 0x00, 0x00,\n"
  "    0x18, 0x00, 0x3F,\n"
  "    0x38, 0x80, 0x21,\n"
  "    0x68, 0xC0, 0x20,\n"
  "    0xC8, 0x60, 0x20,\n"
  "    0x88, 0x31, 0x20,\n"
  "    0x08, 0x1B, 0x20,\n"
  "    0x08, 0x0E, 0x20,\n"
  "    0x08, 0x04, 0x20,\n"
  "    0x08, 0x00, 0x20,\n"
  "    0x08, 0xF0, 0x3F,\n"
  "    0x08, 0x60, 0x00,\n"
  "    0x08, 0xC0, 0x00,\n"
  "    0x08, 0x80, 0x01,\n"
  "    0x08, 0x00, 0x03,\n"
  "    0x08, 0x00, 0x06,\n"
  "    0x08, 0x00, 0x0C,\n"
  "    0x1C, 0x00, 0x38,\n"
  "    0x22, 0x00, 0x44,\n"
  "    0x22, 0x00, 0x44,\n"
  "    0x22, 0x00, 0x44,\n"
  "    0x1C, 0x00, 0x38,\n"
  "    0x00, 0x00, 0x00,\n"
  "};",
  "inline constexpr uint8_t kMarkMeshRoute[72] = {\n"
  "    0x00, 0x00, 0x00,\n"
  "    0x00, 0x00, 0x00,\n"
  "    0xFC, 0x00, 0x18,\n"
  "    0x84, 0x01, 0x1C,\n"
  "    0x04, 0x03, 0x16,\n"
  "    0x04, 0x06, 0x13,\n"
  "    0x04, 0x8C, 0x11,\n"
  "    0x04, 0xD8, 0x10,\n"
  "    0x04, 0x70, 0x10,\n"
  "    0x04, 0x20, 0x10,\n"
  "    0x04, 0x00, 0x10,\n"
  "    0xFC, 0x0F, 0x10,\n"
  "    0x00, 0x06, 0x10,\n"
  "    0x00, 0x03, 0x10,\n"
  "    0x80, 0x01, 0x10,\n"
  "    0xC0, 0x00, 0x10,\n"
  "    0x60, 0x00, 0x10,\n"
  "    0x30, 0x00, 0x10,\n"
  "    0x1C, 0x00, 0x38,\n"
  "    0x22, 0x00, 0x44,\n"
  "    0x22, 0x00, 0x44,\n"
  "    0x22, 0x00, 0x44,\n"
  "    0x1C, 0x00, 0x38,\n"
  "    0x00, 0x00, 0x00,\n"
  "};"),
 ("I06 the MARK is authored MSB-first (every byte bit-reversed — the V4-port hazard §8.1 names, at stride 3)",
  "    0x18, 0x00, 0x3F,\n"
  "    0x38, 0x80, 0x21,\n"
  "    0x68, 0xC0, 0x20,\n"
  "    0xC8, 0x60, 0x20,\n"
  "    0x88, 0x31, 0x20,\n"
  "    0x08, 0x1B, 0x20,\n"
  "    0x08, 0x0E, 0x20,\n"
  "    0x08, 0x04, 0x20,\n"
  "    0x08, 0x00, 0x20,\n"
  "    0x08, 0xF0, 0x3F,\n"
  "    0x08, 0x60, 0x00,\n"
  "    0x08, 0xC0, 0x00,\n"
  "    0x08, 0x80, 0x01,\n"
  "    0x08, 0x00, 0x03,\n"
  "    0x08, 0x00, 0x06,\n"
  "    0x08, 0x00, 0x0C,\n"
  "    0x1C, 0x00, 0x38,\n"
  "    0x22, 0x00, 0x44,\n"
  "    0x22, 0x00, 0x44,\n"
  "    0x22, 0x00, 0x44,\n"
  "    0x1C, 0x00, 0x38,",
  "    0x18, 0x00, 0xFC,\n"
  "    0x1C, 0x01, 0x84,\n"
  "    0x16, 0x03, 0x04,\n"
  "    0x13, 0x06, 0x04,\n"
  "    0x11, 0x8C, 0x04,\n"
  "    0x10, 0xD8, 0x04,\n"
  "    0x10, 0x70, 0x04,\n"
  "    0x10, 0x20, 0x04,\n"
  "    0x10, 0x00, 0x04,\n"
  "    0x10, 0x0F, 0xFC,\n"
  "    0x10, 0x06, 0x00,\n"
  "    0x10, 0x03, 0x00,\n"
  "    0x10, 0x01, 0x80,\n"
  "    0x10, 0x00, 0xC0,\n"
  "    0x10, 0x00, 0x60,\n"
  "    0x10, 0x00, 0x30,\n"
  "    0x38, 0x00, 0x1C,\n"
  "    0x44, 0x00, 0x22,\n"
  "    0x44, 0x00, 0x22,\n"
  "    0x44, 0x00, 0x22,\n"
  "    0x38, 0x00, 0x1C,"),
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
 # --- §UI-10/11 P1 — the `/mrui` PRESET CATALOG RECORD -------------------------------------------------------------
 # ★★★ WHY THESE FIVE ARE HERE AND ⛔ NOT IN `--target=uipresets`: a battery is per-SOURCE-FILE, and the rules they
 #     attack are the RECORD's, which lives in this file. ★ N21 in particular is the ATTACKABLE half of spec pin 7
 #     (*"`/mrcfg` untouched"*): the SERVICE cannot reach a config record at all (it holds one seam), so the only
 #     place "the catalog is written through `/mrcfg`" can be EXPRESSED — and therefore reddened — is the slot table.
 ("N21 ★★★ THE CATALOG IS ADDRESSED AT `/mrcfg`: a phrase edit now writes the record whose version mismatch REPROVISIONS the node",
  "inline constexpr Slot kSlotUi    { \"/mrui\",    \"mr\",      \"ui\"    };",
  "inline constexpr Slot kSlotUi    { \"/mrcfg\",   \"mr\",      \"cfg\"   };"),
 ("N22 ★★ the catalog carries `/mrcfg`'s MAGIC — the two records become mutually readable, which is the same collapse one layer in",
  "constexpr uint32_t kUiPresetMagic   = 0x4D525531u;   // 'MRU1' — its OWN magic, ⛔ never kMagic ('MRC1'), never",
  "constexpr uint32_t kUiPresetMagic   = kMagic;        //"),
 ("N23 ★★ the /mrui seed stamps generation ZERO — the value reserved for 'no generation', which a SendReq can never seal",
  "    b.generation = 1;",
  "    b.generation = 0;"),
 ("N24 the /mrui version policy is RELAXED from equality to a range — an unknown layout is parsed as the wearer's phrases",
  "    return blob_valid_exact(b, n, kUiPresetMagic, kUiPresetVersion) ? UiPresetRead::ok : UiPresetRead::invalid;",
  "    return blob_valid_range(b, n, kUiPresetMagic, 1, 0xFFFFu) ? UiPresetRead::ok : UiPresetRead::invalid;"),
 ("N25 ★★ the /mrui read tests `absent` FIRST, so a store that would not open announces 'no catalog configured'",
  "    if (io.backend_failed) return UiPresetRead::io_failed;\n"
  "    if (io.oversize)       return UiPresetRead::invalid;\n"
  "    if (n == kSlotAbsent)  return UiPresetRead::absent;",
  "    if (n == kSlotAbsent)  return UiPresetRead::absent;\n"
  "    if (io.backend_failed) return UiPresetRead::io_failed;\n"
  "    if (io.oversize)       return UiPresetRead::invalid;"),
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
  # ⓘ RESTORED TO ITS LANDED ANCHOR 2026-08-25 (QG blocker 1): §UI-16 K5 briefly made `refuse` a template so the
  #   saved-key activation could share it. That was WRONG — the clearing is correct only where the LIVE KEY IS THE
  #   SUSPECT (this arm), while `use_saved` refuses SURGICALLY (its live key belongs to the current team and is
  #   innocent, see `--target=teamkeyring` T52). ⇒ the funnel is the boot restore's again, and so is this control.
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
 # ===== §UI-16 K3 — THE GRANT RECEIVE: PERSISTENCE **FIRST**, AND THE FOUR HANDLING-TIME RE-CHECKS ==============
 # ★★★ THE HEADLINE IS T27/T28: [[B240]]'s receive half was *adopt, push, persist nothing*, and the F-10 ruling is
 #     that the persistence runs FIRST and ⛔ only a `saved` verdict forwards the push. `src/fw_main.cpp` applies
 #     that as ONE branch on this service's answer — so ANY mutation that makes a non-persisting receipt answer
 #     `saved` restores the defect exactly, one layer below the branch, where no gate in `fw_main` could see it.
 ("T27 ★★★ THE ORDER IS INVERTED — the `/mrcfg` ACTIVATION is written BEFORE the durable key, so a reboot landing "
  "between the two finds a binding with no key behind it (QG blocker 2, arriving by push)",
  "        const KeyringResult kr = _keyring.put(g.push_team_id, g.live_pub, g.live_priv);",
  "        if (!binding_current(cur, g) && !_binding.commit_active(g.push_team_id, g.live_pub, g.live_priv)) {\n"
  "            r.outcome = GrantSave::binding_failed; return r;\n"
  "        }\n"
  "        const KeyringResult kr = _keyring.put(g.push_team_id, g.live_pub, g.live_priv);"),
 ("T28 ★★★ A FAILED `/mrteams` WRITE STILL ANSWERS `saved` — the push is forwarded and the panel says TEAM KEY "
  "RECEIVED for a key that exists only in RAM. This IS [[B240]] restored (F-10, the headline)",
  "        if (kr.verdict != KeyringVerdict::ok && kr.verdict != KeyringVerdict::unchanged) {\n"
  "            r.outcome = GrantSave::keyring_failed; r.err = kr.err; return r;   // ⛔ the activation is NOT written\n"
  "        }",
  "        (void)kr;"),
 ("T29 ★★ A FAILED `/mrcfg` ACTIVATION STILL ANSWERS `saved` — the key is durable but nothing will ever install it, "
  "and the panel claims durable adoption anyway",
  "            if (!_binding.commit_active(g.push_team_id, g.live_pub, g.live_priv)) {\n"
  "                r.outcome = GrantSave::binding_failed; return r;\n"
  "            }",
  "            (void)_binding.commit_active(g.push_team_id, g.live_pub, g.live_priv);"),
 # ★★ THE FOUR RE-CHECKS, EACH DROPPED ALONE. They are four entries and not one because each names a DIFFERENT
 #    authority — the push, the LIVE config, the live KEY, and the PERSISTED record — and dropping any one leaves
 #    the other three able to hide it.
 ("T30 ★ RE-CHECK (1) DROPPED — a grant naming team 0 reaches the store, which then has to refuse it there",
  "        if (g.push_team_id == 0)              { r.outcome = GrantSave::zero_team;   return r; }",
  ";"),
 ("T31 ★★★ RE-CHECK (2) DROPPED — the LIVE membership is not re-asked, so a `team 0` between RX and drain still "
  "activates the departed team's key",
  "        if (g.push_team_id != g.live_team_id) { r.outcome = GrantSave::not_our_team; return r; }",
  ";"),
 # ⛔⛔ T32's SHAPE IS CONSTRAINED BY THE PROPERTY ITSELF, AND THE CONSTRAINT IS RECORDED RATHER THAN WORKED AROUND.
 #    The obvious mutant — DELETE re-check (3) — hands a NULL pointer to `memcpy` inside `team_key_rec_put`, i.e.
 #    UNDEFINED BEHAVIOUR: MEASURED 2026-08-24, that mutant left the suite reporting 0 failed, so the runner scored
 #    it WORTHLESS ("nothing measures this") when in truth nothing could. ⇒ the entry attacks the same clause with a
 #    WELL-DEFINED tempting wrong fix — *"there is nothing to persist, so nothing failed"* — which is exactly the
 #    answer that forwards the push and puts `TEAM KEY RECEIVED` on the panel of a node that is KEYLESS. T37 below
 #    attacks the clause's PLACEMENT, which is the other half the deletion would have covered.
 ("T32 ★★★ RE-CHECK (3) ANSWERS `saved` — a node whose key was WIPED between RX and drain forwards the push and "
  "the panel says TEAM KEY RECEIVED for a key it does not even hold",
  "        if (!g.live_pub || !g.live_priv)      { r.outcome = GrantSave::no_live_key;  return r; }",
  "        if (!g.live_pub || !g.live_priv)      { r.outcome = GrantSave::saved;        return r; }"),
 ("T37 ★★ RE-CHECK (3) IS MOVED **BELOW** THE `/mrcfg` READ — the refusal still happens, but a node with no key "
  "now pays a flash read for a receipt it was always going to drop (the ⛔ ZERO-I/O half of the rule)",
  "        if (!g.live_pub || !g.live_priv)      { r.outcome = GrantSave::no_live_key;  return r; }\n"
  "\n"
  "        // (4) THE PERSISTED RECORD, WHICH IS A SECOND AUTHORITY. ⛔ Fails closed on an unreadable record.\n"
  "        TeamKeyBinding cur{};\n"
  "        if (!_binding.read(cur))                              { r.outcome = GrantSave::record_unreadable; return r; }",
  "        TeamKeyBinding cur{};\n"
  "        if (!_binding.read(cur))                              { r.outcome = GrantSave::record_unreadable; return r; }\n"
  "        if (!g.live_pub || !g.live_priv)      { r.outcome = GrantSave::no_live_key;  return r; }"),
 ("T33 ★★★ RE-CHECK (4) DROPPED — the PERSISTED record is never compared, so a key is marked ACTIVE against a "
  "`/mrcfg` that names another team (QG blocker 3, arriving by push)",
  "        if (cur.membership_team_id != g.push_team_id)         { r.outcome = GrantSave::record_mismatch;   return r; }",
  ";"),
 ("T34 ★★ THE UNREADABLE `/mrcfg` RECORD FAILS **OPEN** — an unestablished term is treated as satisfied and both "
  "records are written on facts nobody read (C2, inverted)",
  "        if (!_binding.read(cur))                              { r.outcome = GrantSave::record_unreadable; return r; }",
  "        (void)_binding.read(cur);"),
 ("T35 ★★ THE ACTIVATION'S ZERO-WRITE GUARD IS DROPPED — every re-grant of the SAME key rewrites `/mrcfg`, and a "
  "teammate re-sends on every join (K1's flash-wear discipline, on the second record)",
  "        if (!binding_current(cur, g)) {",
  "        if (true) {"),
 ("T36 ★ THE GUARD BECOMES 'A KEY IS PRESENT' RATHER THAN 'THIS KEY' — a stale witness for another team, or for a "
  "different public half, is accepted and the boot then rejects the pair it names",
  "        return cur.key_active && cur.binding_team_id == g.push_team_id\n"
  "               && cur.committed_present && cur.committed_pub\n"
  "               && memcmp(cur.committed_pub, g.live_pub, 32) == 0;",
  "        return cur.key_active && cur.committed_present && g.push_team_id != 0;"),
 # ===== the INVENTORY SENTINEL (2026-08-25) ======================================================================
 # ★★ THE FENCE ITSELF NEEDS NO ENTRY AND CANNOT HAVE ONE — that is the point of it: an outcome added without a word
 #    is a `-Werror=switch` BUILD FAILURE, and the totality case walks `0 .. count-1`, so the two halves are the
 #    COMPILER and the ENUM rather than anything a sed can weaken. (The N6b precedent, `mrui::InviteGrantState::count`,
 #    carries no entry either for the same reason.) What CAN drift is the sentinel's own status, and T38 is that:
 ("T38 ★ THE SENTINEL IS GIVEN A PLAUSIBLE WORD — `count` stops reading as 'not an outcome' and starts reading as "
  "one, which is how a sentinel quietly becomes a state a carrier may hold",
  '        case GrantSave::count:             return "?";',
  '        case GrantSave::count:             return "count";'),
 # ===== [[B243]] — THE UI ROUTING CLASSIFICATION (QG blocker, 2026-08-25) ========================================
 # ★★★ These three attack the decision that turns EIGHT outcomes into THREE doors. It is the decision the first cut
 #     of [[B243]] did not have — it answered the seam with a BOOLEAN — and the consequence was a panel announcing
 #     `TEAM KEY ACTIVE` for receipts where no key is active at all. The rule is structural (which side of
 #     re-check (3) an arm sits on), so each entry breaks it in a way that reads perfectly reasonable in isolation.
 ("T39 ★★★★ A SUPPRESSED ARM IS ROUTED TO THE FAILED-SAVE DOOR — the live pair was WIPED between RX and drain, and "
  "the panel is made to say `TEAM KEY ACTIVE` about a key this node does not hold. THIS IS THE QG BLOCKER of "
  "2026-08-25, restored verbatim (the honest-looking 'tell the operator something' reflex)",
  "        case GrantSave::no_live_key:       return GrantUiRoute::suppressed;",
  "        case GrantSave::no_live_key:       return GrantUiRoute::active_unsaved;"),
 ("T40 ★★★ AN `active_unsaved` ARM IS COLLAPSED INTO SILENCE — a keyring write that really failed over a key that "
  "really IS live says NOTHING, so the operator walks away believing a key he will lose on the next reboot is "
  "durable ([[B243]] restored from the other side)",
  "        case GrantSave::keyring_failed:    return GrantUiRoute::active_unsaved;",
  "        case GrantSave::keyring_failed:    return GrantUiRoute::suppressed;"),
 ("T41 ★★★★ THE VERDICT IS COLLAPSED BACK TO A BOOLEAN — every non-`saved` outcome takes the failed-save door, "
  "which is the WITHDRAWN shape exactly: the three before-re-check-(3) arms all claim an ACTIVE key",
  "        case GrantSave::zero_team:         return GrantUiRoute::suppressed;\n"
  "        case GrantSave::not_our_team:      return GrantUiRoute::suppressed;\n"
  "        case GrantSave::no_live_key:       return GrantUiRoute::suppressed;",
  "        case GrantSave::zero_team:         return GrantUiRoute::active_unsaved;\n"
  "        case GrantSave::not_our_team:      return GrantUiRoute::active_unsaved;\n"
  "        case GrantSave::no_live_key:       return GrantUiRoute::active_unsaved;"),
 # ===== §UI-16 K5 — THE PRESENCE QUESTION AND THE EXPLICIT ACTIVATION ==========================================
 # ★★★★ T41 IS P-2b's SIBLING HEADLINE AND IT IS ONE CHARACTER: the presence test stops asking about THIS team and
 #      starts asking whether the keyring holds ANYTHING — so a node that once belonged to some other team is
 #      offered "its" saved key for a team it has never met. ⓘ The other half of the P-2b pair (installing without
 #      the explicit double) lives in `--target=uiprov` and `--target=model`, because that is where the OFFER and
 #      the ACT are separated.
 ("T42 ★★ the presence question FAILS OPEN — an unreadable store answers 'yes', so the offer is made against a "
  "keyring nobody could read and the operator is walked into a refusal",
  "        if (_store.load(cur) != mrnv::TeamKeyRead::ok) return false;",
  "        if (_store.load(cur) == mrnv::TeamKeyRead::io_failed) return false;"),
 ("T43 ★★★ the presence question stops naming THE TEAM — any record at all answers yes, so `SAVED KEY FOUND` is "
  "offered for a team with NO retained record (P-2b's sibling)",
  "        return team_key_find(cur, team_id) >= 0;",
  "        return cur.count > 0;"),
 ("T44 ★★★★ THE ACTIVATION FORKS A SECOND INSTALL PATH — the binding is committed WITHOUT adopting, so the record "
  "is never verified and the panel reports a key the core does not hold",
  "    if (!live.adopt_key(cur.rec[idx].team_ch_pub, cur.rec[idx].team_ch_priv))\n"
  "        return SavedKeyUse::rejected;",
  "    ;"),
 ("T45 ★★★ A FAILED ACTIVATION IS REPORTED AS A SUCCESS — the `/mrcfg` write came back false and the verb still "
  "answers `installed`, so the panel claims a key the next boot will not restore",
  "    if (!binding.commit_active(team_id, cur.rec[idx].team_ch_pub, cur.rec[idx].team_ch_priv)) {\n"
  "        live.clear_key();\n"
  "        return SavedKeyUse::binding_failed;\n"
  "    }",
  "    (void)binding.commit_active(team_id, cur.rec[idx].team_ch_pub, cur.rec[idx].team_ch_priv);"),
 ("T46 ★★★ THE FAILED ACTIVATION IS LEFT **HALF-INSTALLED** — the live key stays up with no durable binding "
  "behind it, which is [[B240]]'s exact shape (works at the trailhead, dead at the summit)",
  "        live.clear_key();\n"
  "        return SavedKeyUse::binding_failed;",
  "        return SavedKeyUse::binding_failed;"),
 ("T47 ★★ THE ORDER IS INVERTED — the `/mrcfg` activation is committed BEFORE the record is verified, so a "
  "CORRUPT record leaves an active binding with nothing behind it",
  "    if (!live.adopt_key(cur.rec[idx].team_ch_pub, cur.rec[idx].team_ch_priv))\n"
  "        return SavedKeyUse::rejected;\n",
  "    if (!binding.commit_active(team_id, cur.rec[idx].team_ch_pub, cur.rec[idx].team_ch_priv))\n"
  "        return SavedKeyUse::binding_failed;\n"
  "    if (!live.adopt_key(cur.rec[idx].team_ch_pub, cur.rec[idx].team_ch_priv))\n"
  "        return SavedKeyUse::rejected;\n"),
 ("T48 ★★ the activation's ZERO FLOOR is dropped — `use_saved(0)` reaches the store instead of being refused "
  "before a load, and answers a different arm's word",
  "    if (team_id == 0) return SavedKeyUse::zero_team;",
  "    ;"),
 ("T49 ★ the two absent-ish answers are COLLAPSED — 'there is no record for this team' and 'the store could not "
  "be read at all' read alike, although they take different operator actions",
  "    if (st != mrnv::TeamKeyRead::ok)     return SavedKeyUse::store_failed;",
  "    if (st != mrnv::TeamKeyRead::ok)     return SavedKeyUse::no_record;"),

 # ===== §UI-16 K5 round 2 — THE MEMBERSHIP RE-CHECK, THE SURGICAL REFUSAL AND THE WRITER'S FENCE (QG blocker 1) ===
 # ★★★★ T50 IS THE BLOCKER ITSELF, RESTORED: the offer is built for team A and the operator's `double` lands some
 #      seconds later; a `team <id>` over serial moves MEMBERSHIP to B in between. Without the re-check, A's key goes
 #      LIVE under a `/mrcfg` that says B — the panel announces `TEAM KEY ACTIVE` for a binding the five-term boot
 #      restore will REJECT, i.e. a key that works until the next power cycle and then does not. ⓘ Nothing about the
 #      screen changes; only the term that was never asked.
 ("T50 ★★★★ THE MEMBERSHIP RE-CHECK IS DROPPED — a STALE offer installs another team's key under this team's "
  "record (the A -> B race, QG blocker 1 restored verbatim)",
  "    if (cur_bind.membership_team_id != team_id)        return SavedKeyUse::not_our_team;",
  "    ;"),
 ("T51 ★★★ THE RE-CHECK ASKS THE **BINDING** INSTEAD OF THE MEMBERSHIP — a stale binding then authorises itself, "
  "which is QG blocker 3's shape (the boot restore's term (ii)) arriving through the operator's button",
  "    if (cur_bind.membership_team_id != team_id)        return SavedKeyUse::not_our_team;",
  "    if (cur_bind.binding_team_id != team_id)           return SavedKeyUse::not_our_team;"),
 ("T52 ★★★★ THE REFUSAL GOES BACK THROUGH A CLEARING FUNNEL — the stale-target arm WIPES the live key, which at "
  "that moment belongs to the team we ARE in: an innocent key destroyed by a refusal about a different one",
  "    if (cur_bind.membership_team_id != team_id)        return SavedKeyUse::not_our_team;",
  "    if (cur_bind.membership_team_id != team_id)        { live.clear_key(); return SavedKeyUse::not_our_team; }"),
 ("T53 ★★★ THE WRITER'S SECOND AUTHORITY IS DROPPED — `commit_active` will write an ACTIVE BINDING into a `/mrcfg` "
  "record whose MEMBERSHIP names another team, i.e. a binding that lies and a boot that comes up keyless",
  "    return want_team_id != 0 && record_team_id == want_team_id;",
  "    return true;"),
 ("T54 ★★ THE FAIL-CLOSED READ IS FAIL-OPEN — an unreadable `/mrcfg` record is treated as a satisfied membership "
  "term, so the activation proceeds on facts nobody read (C2, inverted — T34's shape one verb over)",
  "    if (!binding.read(cur_bind))                       return SavedKeyUse::record_unreadable;",
  "    ;"),

 # ===== §UI-16 K6 — SAVED-KEY RETENTION MANAGEMENT (⛔ never "key rotation") ====================================
 # ★★★★ T55 IS THE SLICE'S HEADLINE AND IT IS THE IDIOM EVERY OTHER RING IN THIS TREE USES: evict the oldest and
 #      carry on. K1's P-15 exists because a team CONTENT key is UNRECOVERABLE — ⛔ no seed derives it — so the
 #      reflex a reviewer reaches for here destroys a secret only a teammate's re-grant can restore, silently, to
 #      make room for a key the operator was in the middle of creating. ⓘ It is a mutation of `put`, ⛔ not of
 #      `forget`: the point of K6 is that removal became EXPLICIT, ⛔ not that the store started making room.
 ("T55 ★★★★ SILENT OLDEST-RECORD EVICTION IN `put` — the fifth team quietly displaces the first, so a `team new` "
  "destroys an unrelated retained key and the operator is never told (P-15 inverted, the idiom's own trap)",
  "            if (cur.count >= mrnv::kTeamKeyRecs) { r.err = KeyringErr::keyring_full; return r; }",
  "            if (cur.count >= mrnv::kTeamKeyRecs) {\n"
  "                for (uint16_t e = 0; e + 1 < cur.count; ++e) cur.rec[e] = cur.rec[e + 1];\n"
  "                cur.count = static_cast<uint16_t>(cur.count - 1);\n"
  "            }"),
 ("T56 ★★★★ THE ACTIVE KEY'S PROTECTION IS DROPPED — the record behind the LIVE binding can be forgotten, so a "
  "working node reads the team channel today and, the boot restore finding no record, silently cannot tomorrow "
  "([[B240]]'s exact shape arriving through a management screen)",
  "    if (saved_key_is_active(bind, team_id))      return KeyringForget::active_key;           // ★ PROTECTED, 0 writes",
  "    ;"),
 ("T57 ★★★★ THE DELETE IS KEYED ON THE **DISPLAY FINGERPRINT** — the low 24 bits the panel prints — so two teams "
  "that share six hex digits are indistinguishable and the wrong key is destroyed while the right one is on screen "
  "([[B48]]'s class, arriving through a delete)",
  "    const int idx = team_key_find(cur, team_id);\n"
  "    if (idx < 0) return KeyringForget::no_record;                       // ⛔ 0 writes — there is nothing to remove",
  "    int idx = -1;\n"
  "    for (uint16_t i = 0; i < cur.count; ++i)\n"
  "        if ((cur.rec[i].team_id & 0xFFFFFFu) == (team_id & 0xFFFFFFu)) { idx = static_cast<int>(i); break; }\n"
  "    if (idx < 0) return KeyringForget::no_record;"),
 ("T58 ★★★★ THE COMPACTION LEAVES A **SECRET-BEARING TAIL** — the vacated slot is not wiped, so the blob keeps a "
  "byte-for-byte DUPLICATE of a still-live team's PRIVATE key in a slot nothing reads and nothing clears, "
  "recoverable from a flash dump long after the operator believed a key had been removed",
  "    crypto_wipe(&cur.rec[cur.count], sizeof cur.rec[cur.count]);",
  "    ;"),
 ("T59 ★★★ THE COMPACTION BECOMES A SWAP-WITH-THE-LAST — the survivors are re-ordered under the operator's cursor "
  "between one removal and the next, and the tail wipe then clears a slot the swap already overwrote",
  "    for (uint16_t i = static_cast<uint16_t>(idx); i + 1 < cur.count; ++i) cur.rec[i] = cur.rec[i + 1];",
  "    cur.rec[idx] = cur.rec[cur.count - 1];"),
 ("T60 ★★★ A FAILED SAVE IS RENDERED AS A SUCCESS — the one `/mrteams` write came back false and the verb still "
  "answers `forgotten`, so the panel says `KEY FORGOTTEN` about a removal that did not complete",
  "    if (!_store.save(cur)) return KeyringForget::nv_save_failed;        // ⛔ never reported as \"nothing changed\"",
  "    (void)_store.save(cur);"),
 ("T61 ★★★★ THE LIST RETURNS **KEY MATERIAL** — the metadata row is filled from the record's PRIVATE half instead "
  "of from its public id, so the enumeration becomes the reader K1 says this file does not have",
  "        out.rec[out.n].team_id = cur.rec[i].team_id;\n"
  "        out.rec[out.n].active  = saved_key_is_active(bind, cur.rec[i].team_id);   // ★ THE ONE PREDICATE (U1)",
  "        memcpy(&out.rec[out.n], cur.rec[i].team_ch_priv, sizeof out.rec[out.n]);\n"
  "        out.rec[out.n].active  = saved_key_is_active(bind, cur.rec[i].team_id);"),
 ("T62 ★★ THE MISSING STORE AND THE UNREADABLE STORE ARE COLLAPSED — \"the flash would not open\" reads as \"there "
  "is no such record\", so the operator is told a key is already gone while four intact ones sit behind a transient "
  "mount failure",
  "    if (st != mrnv::TeamKeyRead::ok)     return KeyringForget::store_failed;",
  "    if (st != mrnv::TeamKeyRead::ok)     return KeyringForget::no_record;"),
 ("T63 ★★★ THE BINDING READ IS FAIL-OPEN — an unreadable `/mrcfg` record is treated as \"nothing is active\", so a "
  "removal proceeds without ever establishing which record is the PROTECTED one (C2 inverted, T54's shape one verb "
  "over and with a destructive consequence)",
  "    if (!binding.read(bind))                     return KeyringForget::binding_unreadable;   // ⛔ 0 keyring reads",
  "    (void)binding.read(bind);"),
 ("T64 ★★★ THE ACTIVE PREDICATE ASKS **MEMBERSHIP** INSTEAD OF THE BINDING — a node that has left a team (`team 0` "
  "clears the binding and RETAINS the record, P-2b) can no longer free that record, and a stale binding's own "
  "record stops being protected: the marker and the protection both move to the wrong row",
  "    return team_id != 0 && b.key_active && b.binding_team_id == team_id;",
  "    return team_id != 0 && b.membership_team_id == team_id;"),
 ("T65 ★★ THE REMOVAL'S ZERO FLOOR IS DROPPED — `forget(0)` reaches the binding and the store instead of being "
  "refused before anything is read, and answers a different arm's word",
  "    if (team_id == 0) return KeyringForget::zero_team;                  // ⛔ 0 reads, 0 writes",
  "    ;"),
 # ===== §UI-16 K6 (QG blocker, 2026-08-25) — THE RECEIVED GRANT'S FULL-KEYRING LANDING =========================
 # ★★★★ T67 IS **THE BLOCKER ITSELF, RESTORED**: the `KEYRING FULL` refusal of a RECEIVED grant collapsed back into
 #      the generic `active_unsaved`, so the fifth received grant still shows three correct rows and then
 #      acknowledges wherever the generic note lands — a dead end reachable only over the air, while the `team new`
 #      refusal of the SAME store state reaches `SAVED KEYS`. ⓘ It is ONE word, which is why it is plausible.
 ("T67 ★★★★ THE RECEIVED GRANT'S FULL-KEYRING DISTINCTION IS REMOVED — the verdict stops carrying the one fact a "
  "ROUTE cannot, so the fifth RECEIVED grant loses its way out of the dead end (spec §K6 :987, either origin)",
  "    v.keyring_full = grant_ui_keyring_full(r);",
  "    v.keyring_full = false;"),
 ("T68 ★★★ THE FULL FACT IS CLAIMED FOR **EVERY** KEYRING REFUSAL — a corrupt or unopenable store also offers the "
  "removal list, i.e. a false remedy for a fault that has nothing to do with the four records",
  "    return r.outcome == GrantSave::keyring_failed && r.err == KeyringErr::keyring_full;",
  "    return r.outcome == GrantSave::keyring_failed;"),
 ("T69 ★★ THE VERDICT RE-DECIDES THE ROUTE instead of CALLING the landed classifier — the three ruled rows are put "
  "at the mercy of a second authority that can drift from `grant_ui_route_of`",
  "    v.route        = grant_ui_route_of(r.outcome);   // ★ the LANDED classifier, CALLED — ⛔ never re-spelled",
  "    v.route        = (r.outcome == GrantSave::saved) ? GrantUiRoute::received : GrantUiRoute::active_unsaved;"),

 ("T66 ★ THE `KeyringForget` SENTINEL IS GIVEN A PLAUSIBLE WORD — `count` stops reading as \"not an outcome\" and "
  "starts reading as a successful removal, which is the fence's whole point (the T38 shape, a fourth enum over)",
  "        case KeyringForget::count:              return \"?\";",
  "        case KeyringForget::count:              return \"forgotten\";"),
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
 # ===== §UI-16 N5 — the grant's own floor, the request carrier and exact push correlation =====================
 ("I12 ★★★ the preflight floor is lowered from AUTHORITATIVE to OVERHEARD — a spoofable key enables GRANT KEY",
  "           dev->peer_key_at_least(key_hash32, MESHROUTE_NS::Node::PeerKeyConf::authoritative);",
  "           dev->peer_key_at_least(key_hash32, MESHROUTE_NS::Node::PeerKeyConf::overheard);"),
 ("I13 ★★★ the reqpubkey carrier is changed from TEAM to GLOBAL — the identity request flies on the wrong plane",
  "    cmd.u.resolve.plane    = static_cast<uint8_t>(MESHROUTE_NS::Plane::TEAM);",
  "    cmd.u.resolve.plane    = static_cast<uint8_t>(MESHROUTE_NS::Plane::GLOBAL);"),
 ("I14 ★★★ ANY peer_key_cached enables GRANT KEY — the candidate's full hash is not compared",
  "    return pu.kind == MESHROUTE_NS::PushKind::peer_key_cached && pu.sender_hash == key_hash32;",
  "    return pu.kind == MESHROUTE_NS::PushKind::peer_key_cached;"),
 ("I15 ★★ a peer_key_cached for ANOTHER peer completes this candidate's name-refresh wait — one member can "
  "inherit another arrival's ceremony",
  "    return pu.kind == MESHROUTE_NS::PushKind::peer_key_cached && pu.sender_hash == key_hash32;",
  "    return pu.kind == MESHROUTE_NS::PushKind::peer_key_cached && pu.sender_hash != 0;"),
 # ===== §UI-16 N5, the QG BLOCKER's FOUR WAYS TO MISREAD THE FORWARD's ANSWER (2026-08-24) ====================
 # ★★★ THE PROPERTY IS ONE SENTENCE — *"`WAITING FOR PUBKEY` may be shown only for a request the executor
 #     ACCEPTED"* — and these are the four distinct ways to get it wrong. They are four entries and not one
 #     because they fail on four different inputs: no seam, a line that never parsed, a loud refusal, and
 #     (the opposite direction) a success mistaken for a failure.
 # ⚠ I17 IS THE SUBTLE ONE AND IT IS NOT HYPOTHETICAL: `CmdResult::code` DEFAULTS TO `queued`, so a
 #   default-constructed `ExecResult` — exactly what a failed parse or an unformattable command yields — carries
 #   the SUCCESS code. Dropping the `ok` term therefore reads a refusal as a successfully started workflow, and
 #   the mutant looks tidier than the original.
 # ★★ I19 IS THE OVER-STRICT FIX, i.e. the one a careful reader would reach for after seeing this blocker: also
 #    require `accepted`. It is WRONG, and the `reqpubkey` note in `lib/core/command.h` says why — the branch that
 #    answers from the LOCAL key cache reports `queued` with NO frame taken, so the strictest gate strands exactly
 #    the operator whose request already succeeded.
 ("I16 ★★★ the MISSING SEAM reads as a started request — an unattached model (a !MR_FEAT_OLED-shaped build, a "
  "partially-wired probe) claims WAITING FOR PUBKEY having issued nothing at all",
  "    if (!dev || key_hash32 == 0) return false;",
  "    if (!dev || key_hash32 == 0) return true;"),
 ("I17 ★★★ the `ok` term is dropped from the verdict — a line that never parsed carries `CmdResult`'s DEFAULT "
  "`queued` code, so a format/parse failure reads as a successfully started workflow",
  "    return r.ok && r.code == MESHROUTE_NS::CmdCode::queued;",
  "    return r.code == MESHROUTE_NS::CmdCode::queued;"),
 ("I18 ★★★ the CODE term is dropped — every synchronous refusal the executor can return (`err_no_identity`, a "
  "full TX queue) is reported to the operator as a request under way",
  "    return r.ok && r.code == MESHROUTE_NS::CmdCode::queued;",
  "    return r.ok;"),
 ("I19 ★★ the verdict ALSO demands `accepted` — the over-strict fix: the LOCAL-cache completion (`queued`, no "
  "frame taken) is misread as a refusal, stranding the operator whose request already succeeded",
  "    return r.ok && r.code == MESHROUTE_NS::CmdCode::queued;",
  "    return r.ok && r.code == MESHROUTE_NS::CmdCode::queued && r.accepted;"),
 # ===== §UI-16 N6 — THE GRANT'S OUTCOME MAPPING AND ITS CORRELATION ===========================================
 # ★★★★ I20 IS THE HEADLINE CONTROL OF THE WHOLE SLICE (✅ F-9), AND IT IS THE **WITHDRAWN RULE RESTORED**:
 #      *"`ctr != 0` = airborne ⇒ `KEY SENT`"*. It is the tidiest-looking line in the mapper and it is a claim
 #      about the AIR made from an answer about the QUEUE — the operator reads `KEY SENT` while the frame is
 #      still sitting in the TX queue (or is dropped by LBT and never leaves at all).
 ("I20 ★★★ `queued` IS MAPPED STRAIGHT TO `KEY SENT` — the F-9 defect restored: an admission to the TX queue is "
  "reported to the operator as a private key that has physically aired",
  "        case TX::queued:      return InviteGrantState::queued;",
  "        case TX::queued:      return InviteGrantState::sent;"),
 # ⛔⛔ I21 RE-ANCHORED 2026-08-24 (§UI-16 N6b) AND THE OLD FORM IS KEPT VISIBLE: it used to drop the `ctr` SPLIT
 #     inside the `queued` arm — but that split WAS the withdrawn inference, so there is no split left to drop.
 #     The property it defended survives in a truer shape: the explicitly-STORED park must keep its OWN word and
 #     ⛔ may not be laundered into the admission word.
 ("I21 ★★ the explicitly-STORED park is reported with the ADMISSION word — a send sitting behind an H resolve, "
  "with no flight and no handle, reads exactly like one really admitted to the TX queue",
  "        case TX::parked:      return InviteGrantState::parked;       // S-37, and ⛔ ONLY from this explicit outcome",
  "        case TX::parked:      return InviteGrantState::queued;"),
 ("I22 ★★★ the correlation is dropped to the `ctr` ALONE — a LOCAL handle, which `command.h` warns legitimately "
  "names another flight, so another origination's TxDone edge promotes this grant to KEY SENT",
  "    return r.st == InviteGrantState::queued && r.ctr != 0 && pu.ctr == r.ctr && pu.dst == r.dst;",
  "    return r.st == InviteGrantState::queued && r.ctr != 0 && pu.ctr == r.ctr;"),
 ("I23 ★★★ the correlation is dropped ENTIRELY — ANY send_aired on the node promotes the verdict, which is the "
  "false-confirmation shape the whole attribution layer exists to prevent",
  "    return r.st == InviteGrantState::queued && r.ctr != 0 && pu.ctr == r.ctr && pu.dst == r.dst;",
  "    (void)pu;\n    return r.st == InviteGrantState::queued;"),
 ("I24 ★★ the zero-handle term is dropped — a PARKED grant (ctr == 0) correlates against the ctr-0 push six "
  "unrelated operations emit, i.e. a wildcard match",
  "    return r.st == InviteGrantState::queued && r.ctr != 0 && pu.ctr == r.ctr && pu.dst == r.dst;",
  "    return (r.st == InviteGrantState::queued || r.st == InviteGrantState::parked) &&\n"
  "           pu.ctr == r.ctr && pu.dst == r.dst;"),
 ("I25 ★★★ THE SIX REFUSAL ARMS ARE COLLAPSED to one `failed` — `NO TEAM KEY` (ask a teammate), `NO IDENTITY` "
  "(this node cannot seal) and `NOT IN A TEAM` (join first) stop being three different remedies (S-24)",
  "        case TX::no_team:     return InviteGrantState::no_team;\n"
  "        case TX::no_key:      return InviteGrantState::no_key;\n"
  "        case TX::no_identity: return InviteGrantState::no_identity;\n"
  "        case TX::no_pubkey:   return InviteGrantState::no_pubkey;\n"
  "        case TX::self:        return InviteGrantState::self;\n"
  "        case TX::delegated:   return InviteGrantState::wrong_plane;\n"
  "        case TX::too_large:   return InviteGrantState::name_too_long;",
  "        case TX::no_team:     return InviteGrantState::failed;\n"
  "        case TX::no_key:      return InviteGrantState::failed;\n"
  "        case TX::no_identity: return InviteGrantState::failed;\n"
  "        case TX::no_pubkey:   return InviteGrantState::failed;\n"
  "        case TX::self:        return InviteGrantState::failed;\n"
  "        case TX::delegated:   return InviteGrantState::failed;\n"
  "        case TX::too_large:   return InviteGrantState::failed;"),
 # ⛔⛔ I26 IS THE UNREACHABLE ARM THAT LIES THE DAY IT BECOMES REACHABLE (C2). `delegated` cannot arrive while the
 #     UI sends `Plane::TEAM` — so "map it to something harmless" costs nothing today and reports a grant that was
 #     REFUSED as one admitted to the queue the moment I27 (or a future plane change) makes it reachable.
 ("I26 ★★ `delegated` returns a PLAUSIBLE word instead of failing loudly — the unreachable arm is mapped to the "
  "admission word, so a refused grant reads as a queued one",
  "        case TX::delegated:   return InviteGrantState::wrong_plane;",
  "        case TX::delegated:   return InviteGrantState::queued;"),
 ("I27 ★★★ the grant's PLANE is changed away from TEAM — the member enumerated on the team plane is addressed on "
  "another one, and `delegated` becomes reachable on the real seam",
  "inline constexpr MESHROUTE_NS::Plane kInviteGrantPlane = MESHROUTE_NS::Plane::TEAM;",
  "inline constexpr MESHROUTE_NS::Plane kInviteGrantPlane = MESHROUTE_NS::Plane::AUTO;"),
 ("I28 ★★★ an arm prints a COMPLETION word — `KEY SENT` becomes `JOIN COMPLETE` (S-32), which claims an "
  "end-to-end outcome no layer of this protocol acknowledges for a grant",
  'inline constexpr const char* kInviteKeySent     = "KEY SENT";        // S-22 — the design\'s own word (§3.6.4 :821)',
  'inline constexpr const char* kInviteKeySent     = "JOIN COMPLETE";'),
 ("I29 ★★★ THE CONFIRMATION DROPS THE FULL HASH WHEN A NAME IS PRESENT (P-7c) — a mutable, self-asserted label "
  "becomes the ONLY identity on the screen that ships a private key",
  "    ui_fmt_member_hash_full(r.hash, sizeof r.hash, key_hash32);",
  "    if (!invite_name_of(mem, n, key_hash32)[0]) ui_fmt_member_hash_full(r.hash, sizeof r.hash, key_hash32);"),
 ("I30 ★★ the HANDLED SET is keyed by the display NAME rather than the hash (P-7d) — the name is MUTABLE, so a "
  "rejection stops answering for the member that was rejected and starts answering for whoever wears the name",
  "            if (invite_handled_has(w, mem[i].key_hash32)) continue;",
  "            { uint32_t k = 0; for (const char* p = mem[i].name; *p; ++p) k = uint32_t(k * 31u + uint8_t(*p));\n"
  "              if (invite_handled_has(w, k)) continue; }"),
 # ===== §UI-16 N6b (2026-08-24) — THE TWO WORDS THAT USED TO BE INFERRED ======================================
 # ★★★★ I31 IS THE WITHDRAWN INFERENCE ITSELF, PUT BACK. It is the defect the corrective slice exists to remove:
 #      `GRANT PARKED` derived from a ZERO HANDLE rather than from an explicitly-stored park. Measured at the two
 #      sites, the handle answers a DIFFERENT question — a full TX queue drops the frame and still returns a
 #      non-zero counter, a full parked ring stores nothing and returns zero — so either word could be FALSE.
 #      ⛔ The mutant is the shipped N6 line, which is precisely why it needs a standing control.
 ("I31 ★★★ `GRANT PARKED` IS INFERRED FROM `ctr == 0` AGAIN — the withdrawn N6 rule restored, so a state the "
  "core never reported is put on the panel from a counter that does not answer that question (S-37)",
  "    out.st   = invite_grant_state_of(tx);    // ⛔ the outcome ALONE decides the word — ⛔ never the handle",
  "    out.st   = (tx == MESHROUTE_NS::Node::TeamKeyGrantTx::queued && ctr == 0)\n"
  "                 ? InviteGrantState::parked : invite_grant_state_of(tx);"),
 # ⛔⛔ I32 IS S-38's OWN PROHIBITION: the ADMISSION REFUSAL collapsed into the in-flight failure's word. They are
 #     different facts with different remedies — "the device is momentarily too busy to accept this" versus "a
 #     flight this node made came back failed" — and the second invites the operator to conclude the grant was
 #     attempted and lost.
 ("I32 ★★★ `GRANT QUEUE FULL` IS COLLAPSED INTO `GRANT FAILED` — the admission refusal borrows the correlated "
  "in-flight failure's word, which S-38 forbids in as many words",
  "        case TX::queue_full:  return InviteGrantState::queue_full;   // S-38, and ⛔ never collapsed into `failed`",
  "        case TX::queue_full:  return InviteGrantState::failed;"),
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
 # ⚠ V29 RE-ANCHORED 2026-08-25 (§UI-16 K5), and the re-anchor is RECORDED rather than the entry quietly rewritten:
 #   K5 added a SECOND zero floor to this file (`ui_prov_use_saved_key`'s, entry V38), so the one-line anchor matched
 #   TWICE and the runner reported it VACUOUS — a landed control silently retired by a new slice, which is exactly
 #   what this file's own N3 note warns about. ★ The SEMANTIC is unchanged: the JOIN's floor is dropped, and
 #   `TeamRequest{ mint = false, team_id = 0 }` is `team 0`, i.e. a LEAVE. The second line is what makes it the
 #   JOIN arm's — the two floors answer with different outcomes.
 ("V29 the zero-id floor is dropped — `team_id = 0` reaches the transaction, where it means LEAVE",
  "    if (team_id == 0) {\n        a.outcome = mrui::UiProvOutcome::join_refused;",
  "    if (false) {\n        a.outcome = mrui::UiProvOutcome::join_refused;"),
 ("V30 the join notification fires on every verdict ([[B194]] inverted, a third time)",
  "    const ProvResult res = dev.apply(rq, snap);\n    switch (res.verdict) {",
  "    const ProvResult res = dev.apply(rq, snap);\n    dev.on_applied(res);\n    switch (res.verdict) {"),
 ("V31 the joined answer carries no id, so the success screen can show neither identity",
  "            a.team_id = res.team_id;", "            a.team_id = 0;"),
 ("V32 the intent dispatch sends the NEARBY JOIN to the create act (a JOIN that mints, by routing)",
  "            case mrui::UiProvOp::join_team:   return ui_prov_join_team(_dev, intent.team_id);",
  "            case mrui::UiProvOp::join_team:   return ui_prov_create_team(_dev);"),
 # ================================ §UI-16 K5 — the SAVED-KEY OFFER's REPORT and the EXPLICIT ACTIVATION
 # ★★★★ V33 IS THE SLICE'S HEADLINE CONTROL AND IT IS THE P-2b RULING ITSELF: the join stops REPORTING that a key
 #      is retained and starts INSTALLING it. The panel looks identical — `TEAM JOINED`, same id, same fingerprint —
 #      while a stored secret has been reactivated by nothing but knowledge of a PUBLIC team id, which is the one
 #      thing the keyring ruling forbids. ⓘ It is a plausible "fix" precisely because it saves the operator two
 #      presses, which is how it would be argued for.
 # ⛔ V34 IS ITS SIBLING: the offer appears for a team with NO retained record, so the operator is walked into a
 #    refusal for a key that does not exist. The two together are the pair spec §4-K5 names.
 ("V33 ★★★★ THE JOIN **INSTALLS** THE RETAINED KEY INSTEAD OF REPORTING IT — P-2b broken by the one edit that "
  "looks like a convenience (the headline control)",
  "            a.saved_key = dev.has_saved_key(res.team_id);",
  "            a.saved_key = (dev.use_saved_key(res.team_id) == SavedKeyUse::installed);"),
 ("V34 ★★★ THE OFFER IS REPORTED UNCONDITIONALLY — `SAVED KEY FOUND` for a team whose key was never retained",
  "            a.saved_key = dev.has_saved_key(res.team_id);",
  "            a.saved_key = true;"),
 ("V35 ★★★ THE QUESTION IS KEYED ON THE **FINGERPRINT** — the low 24 bits the panel prints, which 255 other teams "
  "share ([[B48]]'s display-shaped-field class, one screen over)",
  "            a.saved_key = dev.has_saved_key(res.team_id);",
  "            a.saved_key = dev.has_saved_key(res.team_id & 0x00FFFFFFu);"),
 ("V36 ★★ THE QUESTION IS ASKED BEFORE THE VERDICT IS KNOWN — a REFUSED or FAILED join reports a retained key for "
  "a team it never joined, so the offer opens on a membership that does not exist",
  "    const ProvResult res = dev.apply(rq, snap);\n    switch (res.verdict) {",
  "    const ProvResult res = dev.apply(rq, snap);\n"
  "    a.saved_key = dev.has_saved_key(rq.team_id);\n    switch (res.verdict) {"),
 ("V37 ★★★ A FAILED ACTIVATION IS RENDERED AS A SUCCESS — everything but the zero floor reads as `installed`, so "
  "`TEAM KEY ACTIVE` appears over a node that holds no key at all",
  "    if (v == SavedKeyUse::installed) {",
  "    if (v != SavedKeyUse::zero_team) {"),
 ("V38 ★★ the activation's ZERO FLOOR is dropped — a `team_id` of 0 reaches the device seam instead of being "
  "refused before it",
  "    if (team_id == 0) {\n"
  "        a.outcome = mrui::UiProvOutcome::saved_key_failed;",
  "    if (false) {\n"
  "        a.outcome = mrui::UiProvOutcome::saved_key_failed;"),
 ("V39 ★ the failure loses the SERVICE's typed token, so the panel cannot say WHICH way the activation failed",
  "    a.reason  = saved_key_use_name(v);                  // ⛔ a FACT token, ⛔ never material",
  '    a.reason  = "";'),
 ("V40 ★★ the intent dispatch sends the SAVED-KEY activation to the nearby-JOIN act — a second membership "
  "transaction for an operator decision about a key",
  "            case mrui::UiProvOp::use_saved_key: return ui_prov_use_saved_key(_dev, intent.team_id);",
  "            case mrui::UiProvOp::use_saved_key: return ui_prov_join_team(_dev, intent.team_id);"),

 # ===== §UI-16 K6 — SAVED-KEY RETENTION MANAGEMENT, THE ADAPTER'S HALF (⛔ never "key rotation") ================
 # ★★★ FOUR ENTRIES, and they attack the MAPPING rather than the policy (which is `--target=teamkeyring`'s): the
 #     failing side must be the DEFAULT, the zero floor must sit in front of the seam, the failure must keep the
 #     service's own token, and the `KEYRING FULL` door must open on the TRANSACTION's error and nothing else.
 ("V41 ★★★★ A FAILED REMOVAL IS RENDERED AS A SUCCESS — everything but the protected-record refusal reads as "
  "`forgotten`, so `KEY FORGOTTEN` appears for a write that did not land and for a store nobody could read "
  "(spec §4-K6 pin 5, and the fail-closed direction inverted by one word)",
  "    if (v == KeyringForget::forgotten) {",
  "    if (v != KeyringForget::active_key) {"),
 ("V42 ★★ the removal's ZERO FLOOR is dropped — a `team_id` of 0 reaches the device seam instead of being refused "
  "before it, so a screen that can DESTROY a stored secret can reach one with a wildcard",
  "    if (team_id == 0) {\n"
  "        a.outcome = mrui::UiProvOutcome::key_forget_failed;",
  "    if (false) {\n"
  "        a.outcome = mrui::UiProvOutcome::key_forget_failed;"),
 ("V43 ★ the failure loses the SERVICE's typed token, so the panel cannot say WHICH way the removal refused — and "
  "`active_key` (a correct refusal) becomes indistinguishable from `nv_save_failed` (a write that did not land)",
  "    a.reason  = keyring_forget_name(v);                 // ⛔ a FACT token, ⛔ never material",
  '    a.reason  = "";'),
 ("V44 ★★★ THE `KEYRING FULL` DOOR OPENS FOR **EVERY** REFUSAL — a PHY divergence, a corrupt keyring or an "
  "unopenable store all send the acknowledgement into the retention list, so the operator is invited to delete a "
  "key over a fault that has nothing to do with the four records",
  "            a.keyring_full = (r.err == ProvErr::keyring_full);",
  "            a.keyring_full = true;"),
 ("V45 ★★ the intent dispatch sends the REMOVAL to the ACTIVATION — the fifth op installs a key where the "
  "operator asked to forget one, i.e. the two acts on one seam swapped",
  "            case mrui::UiProvOp::forget_key:  return ui_prov_forget_key(_dev, intent.team_id);",
  "            case mrui::UiProvOp::forget_key:  return ui_prov_use_saved_key(_dev, intent.team_id);"),
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
  '    snprintf(line, sizeof line, "L%u SF%u BW%lu.%02lu",\n'
  '             unsigned(p.layer), unsigned(p.routing_sf), khz, cen);',
  '    snprintf(line, sizeof line, "L%u SF%u BW%lu.%02lu",\n'
  '             unsigned(p.layer & 0x0F), unsigned(p.routing_sf), khz, cen);'),
 ("J23 the bandwidth is rendered in Hz (500000.00 — the units mix plan §3's mutation controls name)",
  "    const unsigned long khz = (unsigned long)(p.bw_hz / 1000u);",
  "    const unsigned long khz = (unsigned long)p.bw_hz;"),
 ("J24 the RESULT's node line drops the id (the one thing plan §2.3 rule 2 requires it to show)",
  '    snprintf(out, cap, "node %u", unsigned(node_id));', '    snprintf(out, cap, "node");'),
 ("J25 the confirmation's two actions are swapped, so `>BACK` performs the join",
  'inline const char* join_confirm_label(bool confirm) { return confirm ? "JOIN" : "BACK"; }',
  'inline const char* join_confirm_label(bool confirm) { return confirm ? "BACK" : "JOIN"; }'),
 ("J26 ★ B247's validity authority is bypassed — corrupt slot bytes are formatted as plausible PHY values",
  "    return mrfw::validate_join(mrfw::join_request_from_profile(p)) == mrfw::JoinErr::none;",
  "    (void)p; return true;"),
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
 # ===== §UI-16 N6 — THE ROUTER'S TWO INVITE OFFERS ============================================================
 # ★★★ THE §T3 SHAPE, ONE FEATURE OVER: an arm that is not spelled out here is an arm that silently answers
 #     `false`, and the ENTIRE feature then compiles, passes both pure suites and does nothing on the panel. The
 #     correlation RULE is `--target=uiinvite`'s; what these two attack is whether the push ever ARRIVES.
 ("U07 ★★★ the `send_aired` arm never offers the push to the INVITE verdict — `GRANT QUEUED` can never become "
  "`KEY SENT`, and every pure case in both other suites stays green",
  "            return m.on_invite_grant_push(pu);       // §UI-16 N6 — the grant's `KEY SENT` edge",
  "            return false;"),
 ("U08 ★★ the `send_failed` arm never offers the push to the INVITE verdict — a grant that gave up sits on "
  "`GRANT QUEUED` for as long as the operator looks at it",
  "            return m.on_invite_grant_push(pu);       // §UI-16 N6 — the grant's `GRANT FAILED` edge",
  "            return false;"),
 ("U09 ★★ the invite offer is made FIRST, before the two UI send slots — a UI DM holding the same handle loses "
  "its own TxDone edge to the grant verdict (the offer ORDER, inverted)",
  # ⓘ RE-AIMED 2026-08-30 ([[B268]] ruling (b)): the grant no longer rides the GENERIC `send_aired`, so "the invite
  #   offer is made first" is no longer expressible on that arm — the offer is gone from it entirely. The
  #   equivalent hazard now is a UI slot being offered the GRANT's own kind, where a `{dst, ctr}` alias could
  #   promote the wrong flight. That is what this entry attacks.
  "        case PK::team_key_grant_aired:\n"
  "            return m.on_invite_grant_push(pu);       // §UI-16 N6 — the grant's `KEY SENT` edge",
  "        case PK::team_key_grant_aired:\n"
  "            if (emg.match_aired(pu.dst, pu.ctr) || normal.match_aired(pu.dst, pu.ctr)) return true;\n"
  "            return m.on_invite_grant_push(pu);       // §UI-16 N6 — the grant's `KEY SENT` edge"),
 # ===== §UI-16 K4 — THE GRANT RECEIPT'S NOTE ==================================================================
 # ★★★ THE §T3 SHAPE FOR THE THIRD TIME IN THIS FILE: an arm that is not spelled out here is an arm that silently
 #     answers `false`, and the whole feature compiles, passes the keyring suite and shows NOTHING on the panel.
 ("U10 ★★★ the `team_key_received` arm is never spelled out — a receipt K3 persisted and FORWARDED reaches the "
  "router and renders nothing at all, while every K3 case stays green",
  "    if (pu.kind == PK::team_key_received) {\n"
  "        m.on_team_key_note(/*saved=*/true, /*keyring_full=*/false, now_ms);\n"
  "        return true;\n"
  "    }",
  ";"),
 ("U11 ★★★ the arm renders the FAILURE wording for a push K3 FORWARDED — the panel says `TEAM KEY ACTIVE` / "
  "`NOT SAVED` about a key that IS durable, which is the ruled pair told backwards",
  "        m.on_team_key_note(/*saved=*/true, /*keyring_full=*/false, now_ms);",
  "        m.on_team_key_note(/*saved=*/false, /*keyring_full=*/false, now_ms);"),
 ("U12 ★★★ the arm WAKES the panel — §UI-17 R-7 scoped the wake to a DM ADDRESSED TO US and a SEALED channel "
  "post, and widening it to a grant receipt is a new owner ruling nobody made",
  "        m.on_team_key_note(/*saved=*/true, /*keyring_full=*/false, now_ms);\n        return true;",
  "        m.on_team_key_note(/*saved=*/true, /*keyring_full=*/false, now_ms);\n        m.on_msg_wake(now_ms);\n        return true;"),
 ("U13 ★★ the receipt is COUNTED as an arriving DM — a phantom in the unread bar the operator can never open, "
  "because `lib/core` never inboxes a grant (it consumes the DM as control traffic)",
  "        m.on_team_key_note(/*saved=*/true, /*keyring_full=*/false, now_ms);\n        return true;",
  "        m.on_team_key_note(/*saved=*/true, /*keyring_full=*/false, now_ms);\n"
  "        c.last_dm_ms = now_ms; c.have_dm = true; ++c.arr_dm;\n        return true;"),

 # ===== §UI-10/11 P3 — THE EXECUTION-TIME FREEZE AND THE `-l` POLICY =========================================
 # ★★★ BOTH DECISIONS ARE THIS FILE'S AND NO OTHER INSTRUMENT'S: the compose LIST is `--target=model`'s (Y01-Y08)
 #     and the WIRING is the probe's (C137-C141), but what a `SendReq` is checked against at EXECUTION, and whether
 #     a configured location intent may be dropped to make a send succeed, are decided here.
 ("U14 \u2605\u2605\u2605\u2605 THE GENERATION CHECK IS DROPPED \u2014 the freeze reduced to \"is the slot still enabled?\", "
  "which is the half that CANNOT see a `reset <slot>`: the words are identical again, the generation moved, and the "
  "wearer's press is honoured against a catalog he never saw (design \u00a73.3's headline)",
  "    if (req.generation != cat.generation) return SendGate::preset_changed;   // \u2605 EQUALITY, never ordering (\u00a73.2.3)",
  "    ;"),
 ("U15 \u2605\u2605\u2605\u2605 `-l` IS STRIPPED ON A LOCATED CHANNEL POST WITH NO FIX \u2014 the exact \"make the preset "
  "send\" fix \u00a73.2.3 forbids in as many words (*never silently strip -l*). The post AIRS, without the coordinates "
  "the wearer configured, and nothing on the panel says so",
  '        n = loc ? snprintf(out, cap, "send_channel %u \\"%.*s\\" -t -l -e", unsigned(team_channel_id), tl, sl.text)',
  '        n = (loc && have_fix) ? snprintf(out, cap, "send_channel %u \\"%.*s\\" -t -l -e", unsigned(team_channel_id), tl, sl.text)'),
]

MUTS_TEAMGRANT = [
 # ★★★★ THE WHOLE TARGET IS ONE RULING: **the dispatch says what happened, and the caller never infers it.** All
 #      four entries are the SAME tempting wrong fix wearing four hats — collapse one of the outcomes back into
 #      `queued`, which is what the pre-correction function returned for every one of them. ⛔ Each is applied at
 #      exactly one site and each reddens a DIFFERENT native case, which is why they are four entries and not one.
 # ⚠ THIS TARGET COMPILES `lib/core`, so the whole native suite rebuilds behind each mutation — expect the run to
 #   be slower than a `src/` battery. That cost is the point: these are the arms the corpus cannot see either.
 ("G01 ★★★ THE ADMISSION REFUSAL IS LAUNDERED BACK INTO `queued` — a full TX queue (or a full parked ring) drops "
  "the frame at the door and the caller is told it was admitted, which is the QG blocker verbatim",
  "        case SendDispatch::Admit::refused: return TeamKeyGrantTx::queue_full;",
  "        case SendDispatch::Admit::refused: return TeamKeyGrantTx::queued;"),
 ("G02 ★★★ THE EXPLICITLY-STORED PARK IS REPORTED AS `queued` — the pre-correction answer, which left the panel "
  "to guess the parked state from a zero handle (the inference S-37 now forbids)",
  "        case SendDispatch::Admit::parked:  return TeamKeyGrantTx::parked;",
  "        case SendDispatch::Admit::parked:  return TeamKeyGrantTx::queued;"),
 ("G03 ★★★ A SEND THAT NEVER REACHED AN ADMISSION POINT IS REPORTED AS `queued` — the loud refusals (the joining "
  "gate, a seal failure, the type-19 structural refusals) are dressed as a successful enqueue",
  "        case SendDispatch::Admit::none:    return TeamKeyGrantTx::send_failed;",
  "        case SendDispatch::Admit::none:    return TeamKeyGrantTx::queued;"),
 # ⛔⛔ G04 IS THE CORRELATION'S SECOND TERM, KILLED AT THE SOURCE. The UI cannot compensate: a zero dst matches
 #     nothing the TxDone edge can carry, so the verdict is unpromotable no matter how correct the mapper is.
 ("G04 ★★★ THE SEND-TIME RESOLVED DESTINATION IS NEVER PUBLISHED — the caller gets a handle with no address, so "
  "no `send_aired` can ever correlate and the grant screen waits for ever",
  "            if (out_dst) *out_dst = dsp.dst;",
  "            if (out_dst) *out_dst = 0;"),
]

# ★★★★ THE TX-QUEUE ADMISSION **FACT**, at the one site that owns it (`--target=grantadmit`). ⛔ The mutant is the
#      shipped pre-N6b shape wearing a report: the store still happens or not exactly as before, and the caller is
#      told "admitted" either way — so every word derived from it is wrong again, and nothing one frame up can tell.
MUTS_GRANTADMIT = [
 ("A01 ★★★ the enqueue reports ADMITTED whether or not it stored the item — the `if`'s own answer discarded "
  "again, one layer below every word that depends on it",
  "        out_dispatch->admit = admitted ? SendDispatch::Admit::queued : SendDispatch::Admit::refused;",
  "        out_dispatch->admit = SendDispatch::Admit::queued;"),
 # ⛔⛔ A SECOND ENTRY WAS WRITTEN, MEASURED **GREEN**, AND IS PUBLISHED AS SUCH RATHER THAN QUIETLY KEPT (the
 #     `inner_len < 6` precedent, BASELINE.md §T3): *"a REFUSED frame is still given its minted handle"* —
 #     `out_dispatch->ctr = ctr;` instead of the `admitted ? ctr : 0` guard. It leaves the suite GREEN, and the
 #     reason is structural rather than a missing test: `team_key_grant_send` publishes `out_ctr`/`out_dst` ONLY on
 #     the `queued` arm, so a handle attached to a refused dispatch is unreachable from every public seam. ⇒ the
 #     zeroing is DEFENCE IN DEPTH, it is labelled as such at the site, and it is ⛔ NOT claimed as tested.
]

# ★★★★ THE PARKED-RING **FACT** (`--target=grantpark`). `park_send` had a silent early-out and its callers reported
#      it to the app as a parked send; the mutant restores exactly that by claiming a store the ring never made.
MUTS_GRANTPARK = [
 ("K01 ★★★ a FULL parked ring reports a STORED park — the send is dropped and the caller is told it is waiting "
  "behind an H resolve, which is `GRANT PARKED` shown for a state the node is not in (S-37)",
  "    if (_parked_sends_n >= protocol::cap_parked_sends) return false;   // full -> drop (the app can retry)",
  "    if (_parked_sends_n >= protocol::cap_parked_sends) return true;"),
]

# ===== B161 — canonical typed-answer producers / consumers / hybrid identity =====================================
# These entries attack decisions independently. The new native cases drive the real dedicated producers, deferred
# destination/relay consumers and a deliberately disagreeing carrier-vs-wire identity; a compile-only failure is
# unusable under the runner's standing rule.
MUTS_B161HASH = [
 ("H01 B161 TEAM origin is taken directly from the static node id instead of the one stamp_origin authority",
  "    stamp_origin(item, item.plane, dst);",
  "    item.origin = _node_id;"),
 ("H02 B161 the shared helper restores the legacy raw body and never writes the canonical origin envelope",
  "    const size_t n = pack_unicast_inner(std::span<uint8_t>(item.inner, sizeof item.inner), /*flags=*/0,\n"
  "                                        /*dst_key_hash32=*/0, /*layer_ids=*/nullptr, /*n_layers=*/0, /*cur=*/0,\n"
  "                                        item.origin, /*source_hash=*/0, body, body_len, /*lat_e7=*/0, /*lon_e7=*/0);",
  "    if (body_len > sizeof item.inner) return false;\n"
  "    for (uint8_t i = 0; i < body_len; ++i) item.inner[i] = body[i];\n"
  "    const size_t n = body_len;"),
 ("H03 B161 type 13 drops the H query's TEAM plane and falls back to AUTO",
  "    const size_t total = n + 32 + 1u + nlen;\n"
  "    if (!pack_typed_answer_inner(item, team_scoped ? Plane::TEAM : Plane::AUTO, to_origin,\n"
  "                                 body, static_cast<uint8_t>(total))) return;",
  "    const size_t total = n + 32 + 1u + nlen;\n"
  "    if (!pack_typed_answer_inner(item, Plane::AUTO, to_origin,\n"
  "                                 body, static_cast<uint8_t>(total))) return;"),
 ("H04 B161 type 13 omits both name_len and the name tail from the transmitted body",
  "    const size_t total = n + 32 + 1u + nlen;",
  "    const size_t total = n + 32;"),
 ("H05 B161 production type 5 is accidentally rebuilt by overwriting its established body prefix",
  "    body[n] = nlen; n += 1u + nlen;",
  "    body[n] = nlen; n += 1u + nlen; body[0] = _node_id;"),
 ("H06 B161 a type-13 hash/pubkey mismatch still installs mobile-home state and drains",
  "    if (kh != hb->key_hash32) return;                              // malformed answer earns no key/home/drain effect",
  "    ;"),
]

MUTS_B161RX = [
 ("R01 B161 types 1/2 restore a legacy raw fallback when canonical parsing/shape validation refuses",
  "            if (ui && canonical_typed_answer_body_valid(pa.type, ui->body))\n"
  "                on_hash_bind_response(ui->body.data(), static_cast<uint8_t>(ui->body.size()),\n"
  "                                      pa.type == DATA_TYPE_AUTHORITATIVE_H_ANSWER, pa.team_plane);",
  "            if (ui && canonical_typed_answer_body_valid(pa.type, ui->body))\n"
  "                on_hash_bind_response(ui->body.data(), static_cast<uint8_t>(ui->body.size()),\n"
  "                                      pa.type == DATA_TYPE_AUTHORITATIVE_H_ANSWER, pa.team_plane);\n"
  "            else on_hash_bind_response(pa.inner, pa.inner_len,\n"
  "                                       pa.type == DATA_TYPE_AUTHORITATIVE_H_ANSWER, pa.team_plane);"),
 ("R02 B161 type 8 restores a legacy raw fallback when canonical parsing/shape validation refuses",
  "            if (ui && canonical_typed_answer_body_valid(pa.type, ui->body))\n"
  "                on_mobile_hash_bind_response(ui->body.data(), static_cast<uint8_t>(ui->body.size()));",
  "            if (ui && canonical_typed_answer_body_valid(pa.type, ui->body))\n"
  "                on_mobile_hash_bind_response(ui->body.data(), static_cast<uint8_t>(ui->body.size()));\n"
  "            else on_mobile_hash_bind_response(pa.inner, pa.inner_len);"),
 ("R03 B161 destination types 1/2 pass the whole inner, reintroducing the one-byte body shift",
  "                on_hash_bind_response(ui->body.data(), static_cast<uint8_t>(ui->body.size()),",
  "                on_hash_bind_response(pa.inner, pa.inner_len,"),
 ("R04 B161 destination type 8 passes the whole inner, reintroducing the one-byte body shift",
  "                on_mobile_hash_bind_response(ui->body.data(), static_cast<uint8_t>(ui->body.size()));",
  "                on_mobile_hash_bind_response(pa.inner, pa.inner_len);"),
 ("R05 B161 destination type 13 passes the whole inner, reintroducing the one-byte body shift",
  "                on_mobile_hash_bind_pubkey_response(ui->body.data(), static_cast<uint8_t>(ui->body.size()));",
  "                on_mobile_hash_bind_pubkey_response(pa.inner, pa.inner_len);"),
 ("R06 B161 relay snoop passes the whole inner while forwarding still carries the canonical bytes",
  "                on_hash_bind_snoop(ui->body.data(), static_cast<uint8_t>(ui->body.size()),",
  "                on_hash_bind_snoop(pa.inner, pa.inner_len,"),
 ("R07 B161 implicit-forward credit collapses the identity back to the counter low nibble",
  "        if (rts_flight_identity_equal(r.id, mine)\n"
  "            && rts_wire_team_plane(r) == rts_wire_team_plane(mk.addr_len, mk.mobile_src))",
  "        if (r.ctr_lo == pt.ctr_lo\n"
  "            && rts_wire_team_plane(r) == rts_wire_team_plane(mk.addr_len, mk.mobile_src))"),
 ("R08 B161 terminal CTS collapses the identity back to the counter low byte",
  "            bound = rts_flight_identity_equal(c.id, mine) && (c.team_plane == my_team);",
  "            bound = (c.id.bytes[2] == mine.bytes[2]) && (c.team_plane == my_team);"),
]

MUTS_B161MAC = [
 ("M01 B161 flight_identity trusts PendingTx.origin instead of the origin exposed by the DATA inner",
  "                               ui ? ui->origin : 0, pt.ctr, pt.dst, pt.nonce_seed);",
  "                               pt.origin, pt.ctr, pt.dst, pt.nonce_seed);"),
]

# ===== B251 — hosted-mobile first-hop counter boundary / reverse-ACK correlation ================================
MUTS_B251RX = [
 ("X01 B251 translation is removed, restoring the downstream home/mobile counter alias",
  "    const bool translate_mobile_transit = hosted_mobile_direct && !for_me_dst(d.dst)\n"
  "        && !(_cfg.is_gateway && !_cfg.intra_layer_relay);",
  "    const bool translate_mobile_transit = false;"),
 ("X02 B251 first-hop identity is rebuilt from current row zero instead of the accepted hosted-mobile row",
  "             | (uint64_t(hosted_mobile_hash) << 24) | (uint64_t(d.dst) << 16) | d.ctr)",
  "             | (uint64_t(_active->_mobile_reg[0].key_hash32) << 24) | (uint64_t(d.dst) << 16) | d.ctr)"),
 ("X03 B251 the hosted-mobile hash discriminator is dropped from the seen-origin key",
  "        : hosted_mobile_direct\n          ? ((uint64_t(1) << 62)",
  "        : false\n          ? ((uint64_t(1) << 62)"),
 ("X04 B251 ordinary CRYPTED mobile transit is admitted to counter rewriting",
  "    if (!d.crypted && _active->_pending_rx->mobile_from && !_active->_pending_rx->wire_team_plane\n"
  "        && for_static_data && ui) {",
  "    if (_active->_pending_rx->mobile_from && !_active->_pending_rx->wire_team_plane\n"
  "        && for_static_data && ui) {"),
 ("X05 B251 mobile_from alone authorises translation, collapsing TEAM and static planes",
  "    if (!d.crypted && _active->_pending_rx->mobile_from && !_active->_pending_rx->wire_team_plane\n"
  "        && for_static_data && ui) {",
  "    if (!d.crypted && _active->_pending_rx->mobile_from && ui) {"),
 ("X06 B251 a mismatching SOURCE_HASH is allowed to borrow the hosted row's translation authority",
  "            if (ui->has_source_hash && ui->source_hash != row.key_hash32) {",
  "            if (false && ui->has_source_hash && ui->source_hash != row.key_hash32) {"),
 ("X07 B251 queue/correlation pressure is checked but ignored, so the mobile is hop-ACKed anyway",
  "    if (admission_refusal != 0) {",
  "    if (false && admission_refusal != 0) {"),
 ("X09 B251 completed-flight lookup drops the immediate sender, so two mobiles can alias before DATA",
  "        if (e.from != from || e.dst != dst || e.team_plane != team_plane) continue;",
  "        if (e.dst != dst || e.team_plane != team_plane) continue;"),
]

MUTS_B251HASH = [
 ("D01 B251 reverse lookup drops the returning destination discriminator",
  "        if (e.mobile_hash == mobile_hash && e.ctr_h == acked_ctr\n"
  "            && e.peer_kind == return_kind && e.peer == return_peer && e.layer == layer) {",
  "        if (e.mobile_hash == mobile_hash && e.ctr_h == acked_ctr\n"
  "            && e.peer_kind == return_kind && e.layer == layer) {"),
 ("D02 B251 reverse lookup drops the layer/plane discriminator",
  "        if (e.mobile_hash == mobile_hash && e.ctr_h == acked_ctr\n"
  "            && e.peer_kind == return_kind && e.peer == return_peer && e.layer == layer) {",
  "        if (e.mobile_hash == mobile_hash && e.ctr_h == acked_ctr\n"
  "            && e.peer_kind == return_kind && e.peer == return_peer) {"),
 ("D03 B251 reverse lookup drops the hosted-mobile hash discriminator",
  "        if (e.mobile_hash == mobile_hash && e.ctr_h == acked_ctr\n"
  "            && e.peer_kind == return_kind && e.peer == return_peer && e.layer == layer) {",
  "        if (e.ctr_h == acked_ctr\n"
  "            && e.peer_kind == return_kind && e.peer == return_peer && e.layer == layer) {"),
 ("D04 B251 a full live correlation ring evicts slot zero instead of applying backpressure",
  "    if (free_slot == kDelegAckNoSlot) {\n"
  "        ++_mobile_ctr_admission_refused_n;\n"
  "        MR_EMIT(\"deleg_ack_put_refused\", EF_I(\"mobile_hash\", static_cast<int64_t>(mobile_hash)),\n"
  "                EF_I(\"ctr_h\", ctr_h), EF_I(\"ctr_m\", ctr_m));\n"
  "        return false;                                             // every row is live: never evict one\n"
  "    }",
 "    if (free_slot == kDelegAckNoSlot) {\n"
  "        free_slot = 0;                                            // mutant: evict a live answer\n"
  "    }"),
 ("D05 B251 MOBILE_SEND treats a minted non-zero counter as admission and manufactures phantom evidence/state",
  "        const bool admitted = dispatch ? dispatch->admit == SendDispatch::Admit::queued : ctr_h != 0;",
  "        const bool admitted = ctr_h != 0;"),
]

# ===== §UI-10/11 P1 — src/firmware_ui_presets.h ====================================================================
# ★★★ WHAT THESE MEASURE: every OWNER-RULED clause of the preset catalog, attacked ON ITS OWN. The stakes are why the
#     battery exists — the emergency slot is what a long press sends when the wearer is in trouble, and every
#     canonical-byte rule below is what makes the flash-wear guard trustworthy. ⛔ Not one entry is a deletion for its
#     own sake: each is a TEMPTING WRONG FIX (a simpler predicate, a shorter order, a reused arm).
MUTS_UIPRESETS = [
 # --- the FOUR STORAGE STATES (spec §3-P1 / pin 7) ---------------------------------------------------------------
 ("U01 ★★★★ `io_failed` IS TREATED AS `invalid`: the REPAIR WRITE lands on a possibly-intact record after a transient mount fault",
  "inline bool preset_read_refuses_writes(mrnv::UiPresetRead st) { return st == mrnv::UiPresetRead::io_failed; }",
  "inline bool preset_read_refuses_writes(mrnv::UiPresetRead) { return false; }"),
 ("U02 ★★★ THE SEMANTIC GATE IS DROPPED — a header-valid record with corrupt SLOT fields rides in as the wearer's catalog",
  "        if (st == mrnv::UiPresetRead::ok && !presets_canonical(_cur)) st = mrnv::UiPresetRead::invalid;",
  "        ;"),
 ("U03 ★★ DEFAULTS-ON-CORRUPT, SILENTLY: the invalid load is no longer COUNTED, so nothing measures the warning",
  "        if (st == mrnv::UiPresetRead::invalid)   ++_invalid_loads;",
  "        ;"),
 ("U04 the two unreadable warnings are COLLAPSED — a corrupt record and a dead store take the same operator action",
  "        case mrnv::UiPresetRead::io_failed: return kPresetIoFailedLine;",
  "        case mrnv::UiPresetRead::io_failed: return kPresetInvalidLine;"),
 ("U05 ★★ AN ORDINARY FIRST BOOT WARNS — `absent` is worded as a fault, which is the collapse the four states exist to prevent",
  "        case mrnv::UiPresetRead::absent:    return nullptr;   // ★ a first boot is SILENT (owner-ruled)",
  "        case mrnv::UiPresetRead::absent:    return kPresetInvalidLine;"),
 # --- the CANONICAL RECORD BYTES, one entry per rule, from the WRITER and from the READER --------------------------
 ("U06 ★★★ THE GENERATION WRAP LANDS ON ZERO — the value reserved for 'no generation', disarming P3's stale-generation refusal",
  "    return n == 0u ? 1u : n;",
  "    return n;"),
 ("U07 ★★★ A CANONICAL-ZEROING RULE DROPPED: the slot is edited in place, so the TAIL of a longer phrase survives",
  "    s = mrnv::UiPresetSlot{};                       // ★ zeroes text, len, loc and enabled — see the block above",
  "    s.enabled = 0; s.loc = 0; s.len = 0;"),
 ("U08 the reader stops requiring the TAIL AFTER `len` to be zero — invisible garbage is accepted as canonical",
  "    for (uint8_t i = s.len; i < sizeof s.text; ++i) if (s.text[i] != 0) return false;",
  "    ;"),
 ("U09 ★★ the booleans become 'non-zero is true' instead of EXACTLY 0/1 — the coalescing compare is unreliable forever after",
  "    if (s.enabled > 1 || s.loc > 1) return false;                       // ★ EXACTLY 0 or 1",
  "    ;"),
 ("U10 ★★★ the reader no longer requires the EMERGENCY slot to be enabled — a bit-flip silently removes the alarm's phrase",
  "    if (mandatory && s.enabled != 1) return false;                      // ★ the emergency slot is ALWAYS enabled",
  "    ;"),
 ("U11 a DISABLED slot may keep its LOCATION flag — the 'disabled slots are all-zero' rule half-dropped",
  "        if (s.len != 0 || s.loc != 0) return false;",
  "        if (s.len != 0) return false;"),
 ("U12 the persisted generation may be ZERO — the record's one non-zero invariant dropped from the reader",
  "    if (b.generation == 0) return false;                                // ★ NON-ZERO by ruling (see the wrap note)",
  "    ;"),
 ("U13 ★★ the compiled EMERGENCY default is DISABLED AND EMPTY — §3.2.2's mandatory row broken at its source",
  "    { \"I'm in danger\", 1 },        //  0 emergency — ★ location ON, enabled, MANDATORY (§3.2.2 row 1)",
  "    { nullptr,         0 },        //  0 emergency"),
 ("U14 the compiled emergency default loses its LOCATION — §3.2.2 row 1 says location ON",
  "    { \"I'm in danger\", 1 },        //  0 emergency — ★ location ON, enabled, MANDATORY (§3.2.2 row 1)",
  "    { \"I'm in danger\", 0 },        //  0 emergency"),
 # --- the TRANSACTION: coalescing, its RULED LIMIT, and the candidate-then-live order ------------------------------
 ("U15 ★★★ COALESCING DROPPED — every `set` writes flash, including one that changes nothing",
  "        if (st == mrnv::UiPresetRead::ok &&\n"
  "            memcmp(&_cand, &_cur, sizeof _cand) == 0) { r.verdict = PresetVerdict::unchanged; return r; }",
  "        ;"),
 ("U16 ★★ the coalescing pin's ABSENT arm dropped — a no-op over a fresh device writes the record anyway",
  "        if (st == mrnv::UiPresetRead::absent &&\n"
  "            memcmp(&_cand, &_cur, sizeof _cand) == 0) { r.verdict = PresetVerdict::unchanged; return r; }",
  "        ;"),
 # ⛔ U17 WAS REWRITTEN 2026-08-25 (QG blocker 2), AND THE WITHDRAWN ENTRY IS KEPT VISIBLE BECAUSE IT WENT DEAD: it
 #   was `st == absent` -> `st != ok`, which reddened only while the absent baseline was `_live`. Now that `_cur`
 #   holds the DURABLE side on both readable arms (defaults for absent, the empty record for invalid), that edit
 #   alone no longer coalesces anything — a mutation that cannot redden is an entry that measures nothing ([[B217]]).
 #   ★ THE LIVE THREAT IS THE OTHER HALF OF THE SAME WRONG FIX — consulting RAM — so the entry now makes BOTH moves,
 #   and is RED from two independent directions: the invalid-repair case AND the late-absent durability case.
 ("U17 ★★★★ THE BASELINE BECOMES THE RUNNING CATALOG for every non-ok state: the REPAIR never happens AND a late-absent record never regains durability",
  "        if (st == mrnv::UiPresetRead::absent &&\n"
  "            memcmp(&_cand, &_cur, sizeof _cand) == 0) { r.verdict = PresetVerdict::unchanged; return r; }",
  "        if (st != mrnv::UiPresetRead::ok &&\n"
  "            memcmp(&_cand, &_live, sizeof _cand) == 0) { r.verdict = PresetVerdict::unchanged; return r; }"),
 ("U29 ★★★★ THE DEFECT VERBATIM (QG blocker 2): the absent arm coalesces against `_live` — a custom catalog whose record went ABSENT answers `unchanged` to its own re-entry and is LOST at the next boot",
  "        if (st == mrnv::UiPresetRead::absent &&\n"
  "            memcmp(&_cand, &_cur, sizeof _cand) == 0) { r.verdict = PresetVerdict::unchanged; return r; }",
  "        if (st == mrnv::UiPresetRead::absent &&\n"
  "            memcmp(&_cand, &_live, sizeof _cand) == 0) { r.verdict = PresetVerdict::unchanged; return r; }"),
 ("U30 ★★ an ABSENT record is materialised as an EMPTY one rather than the COMPILED DEFAULTS — so every no-op on a fresh device writes flash",
  "        if (st == mrnv::UiPresetRead::absent) preset_defaults(_cur);",
  "        if (st == mrnv::UiPresetRead::absent) mrnv::ui_preset_blob_init(_cur);"),
 ("U18 ★★★★ CANDIDATE/LIVE ORDER INVERTED: the catalog is PUBLISHED BEFORE the save, so a failed write leaves phrases that vanish at the next boot",
  "        if (!_store.save(_cand)) { r.verdict = PresetVerdict::nv_failed; r.err = PresetErr::store; return r; }",
  "        _live = _cand;\n"
  "        if (!_store.save(_cand)) { r.verdict = PresetVerdict::nv_failed; r.err = PresetErr::store; return r; }"),
 ("U19 ★★★ THE WITHDRAWN ORDER: the candidate is saved WITHOUT the next generation (the spec row corrected in QA round 2)",
  "        _cand.generation = preset_generation_next(_live.generation);",
  "        _cand.generation = _live.generation;"),
 ("U20 ★★ a failed save is reported as a SUCCESS — the 'success that isn't', over a catalog nothing stored",
  "        if (!_store.save(_cand)) { r.verdict = PresetVerdict::nv_failed; r.err = PresetErr::store; return r; }",
  "        _store.save(_cand);"),
 ("U21 ★ `reset all` resets the GENERATION to the compiled 1 — a SendReq sealed under an OLD catalog compares EQUAL again",
  "        _cand.generation = _live.generation;",
  "        ;"),
 # --- the EMERGENCY INVARIANTS and the `busy` table (spec §2) ------------------------------------------------------
 ("U22 ★★★★ `clear emergency` IS ACCEPTED — the one slot the design says can never be disabled, cleared or emptied",
  "        if (preset_slot_mandatory(static_cast<uint8_t>(slot))) { r.err = PresetErr::mandatory; return r; }",
  "        ;"),
 ("U23 ★★ an ACTIVE EMERGENCY no longer refuses `set` — an alarm's retry series can have its body changed halfway through",
  "        if (_gate.emergency_active()) { r.err = PresetErr::busy; return r; }        // ⛔ 0 loads, 0 writes\n"
  "        if (!preset_slot_valid(slot)) { r.err = PresetErr::bad_slot; return r; }    // ⛔ 0 loads, 0 writes\n"
  "        const PresetErr ve = validate_preset_text(text, len);",
  "        if (!preset_slot_valid(slot)) { r.err = PresetErr::bad_slot; return r; }\n"
  "        const PresetErr ve = validate_preset_text(text, len);"),
 ("U24 ★★ an ACTIVE EMERGENCY no longer refuses `reset all` — the whole catalog replaceable mid-alarm",
  "        if (_gate.emergency_active()) { r.err = PresetErr::busy; return r; }        // ⛔ 0 loads, 0 writes\n"
  "        const mrnv::UiPresetRead st = read_store();",
  "        const mrnv::UiPresetRead st = read_store();"),
 # --- VALIDATION: OQ-A's bound and §3.2.2's content rules ----------------------------------------------------------
 ("U25 ★★★ OQ-A'S WITHDRAWN DRAFT BOUND RESTORED (18) — the row always shows a location marker, so byte 18 is one the wearer cannot inspect",
  "    if (len == 0 || len > mrnv::kUiPresetTextMax) return PresetErr::bad_text;",
  "    if (len == 0 || len > 18) return PresetErr::bad_text;"),
 ("U26 the '≥ one non-space' rule dropped — a slot of spaces renders as a row the wearer believes is configured and cannot see",
  "    return non_space ? PresetErr::none : PresetErr::bad_text;",
  "    return PresetErr::none;"),
 ("U27 the `\"` / `\\` refusal dropped — the two printable bytes that break every quoted console/NDJSON form P2 emits",
  "        if (c == '\"' || c == '\\\\') return PresetErr::bad_text;",
  "        ;"),
 ("U28 the printable-ASCII domain is widened to 'not a control byte', so high bytes the panel cannot show are stored",
  "        if (c < 0x20 || c > 0x7e) return PresetErr::bad_text;         // ⇒ NUL, CR, LF, DEL and every high byte",
  "        if (c < 0x20) return PresetErr::bad_text;"),
 # --- QG blocker 1: THE LENGTH BOUND MUST NOT BE BYPASSABLE BY INTEGER NARROWING ------------------------------------
 # ★★★ THE TWO HALVES OF THE FIX GET ONE ENTRY EACH, because they fail in DIFFERENT PLACES and a reviewer's reflex
 #     ("the record's field is a uint8_t, so the parameter should be too") reaches for either. U31 narrows INSIDE the
 #     validator; U32 narrows AT THE PUBLIC BOUNDARY, before one line of the service runs. 273 & 0xFF = 17.
 ("U31 ★★★★ THE BOUND IS TESTED AFTER A NARROW: a 273-byte phrase becomes 17 and is ACCEPTED as valid",
  "    if (len == 0 || len > mrnv::kUiPresetTextMax) return PresetErr::bad_text;",
  "    len = static_cast<uint8_t>(len);\n"
  "    if (len == 0 || len > mrnv::kUiPresetTextMax) return PresetErr::bad_text;"),
 ("U32 ★★★★ THE PUBLIC BOUNDARY IS NARROWED BACK to the record's own width — the length is laundered AT THE CALL, before any check runs",
  "    PresetResult set(long slot, bool loc, const char* text, size_t len) {",
  "    PresetResult set(long slot, bool loc, const char* text, uint8_t len) {"),
]

# ===== §UI-10/11 P2 — src/firmware_ui_preset_verbs.h ===============================================================
# ★★★ WHAT THESE MEASURE: the PUBLISHED half of the slice. Every line below is a clause of a contract an iOS
#     companion parses (`ios-companion/INBOX_SYNC_CONTRACT.md`) or of the design's own §3.2.3 grammar — and every one
#     of them is a plausible simplification rather than a deletion. ⛔ The stakes are not cosmetic: a `list` that
#     hides the disabled slots makes `dm5` UNREACHABLE to an editor, a collapsed reason set makes `busy` and
#     `bad_text` indistinguishable to an app, and a stripped terminator breaks the STREAMING transport while the
#     direct one still looks right.
# ⛔ ONE MUTATION IS DELIBERATELY ABSENT AND IS RECORDED RATHER THAN FAKED: *"the BLE path forked from the USB path"*
#    cannot be expressed in this file, BY CONSTRUCTION — there is exactly one emitter, it is handed an
#    `IPresetLines` it cannot inspect, and nothing here can ask which transport is attached. A real fork would have
#    to be written in `src/fw_main.cpp`'s `ble_dispatch_line` (which intercepts `status`/`cfg`/`peers` before the
#    dispatch fallback), and NO battery reaches that TU. ⇒ V09 below is the closest reddenable shape — a
#    transport-shaped edit at the single emitter that leaves USB looking correct — and the structural half of the
#    proof is a grep: `ble_dispatch_line` contains no `ui` arm at all.
MUTS_UIPRESETVERBS = [
 # --- the three records' BYTES (design §3.2.3, verbatim) ----------------------------------------------------------
 ('V01 the reason word is DECORATED at the writer — every one of the six reaches the companion mis-spelled',
  '    j.lit("{\\"ev\\":\\"ui_preset_err\\",\\"reason\\":\\""); j.lit(preset_err_name(e)); j.ch(\'"\');',
  '    j.lit("{\\"ev\\":\\"ui_preset_err\\",\\"reason\\":\\"err_"); j.lit(preset_err_name(e)); j.ch(\'"\');'),
 ('V02 ★★ THE SIX REASONS ARE COLLAPSED TO ONE at the render site — `busy`, `mandatory` and `bad_text` become the same event',
  '            preset_emit_err(r.err, out);',
  '            preset_emit_err(PresetErr::store, out);'),
 ("V03 ★★★ `list` SKIPS THE DISABLED SLOTS — an editor can no longer address `dm5` to turn it on (§3.2.3's own words)",
  '    for (uint8_t i = 0; i < mrnv::kUiPresets; ++i) preset_emit_record(cat, i, out);',
  '    for (uint8_t i = 0; i < mrnv::kUiPresets; ++i) if (cat.slot(i).enabled) preset_emit_record(cat, i, out);'),
 ("V04 ★★ the end record's `capacity` becomes a HAND-WRITTEN literal — the number the companion sizes its editor from can now drift from the record",
  '    j.lit("{\\"ev\\":\\"ui_presets_end\\",\\"capacity\\":"); j.u32(mrnv::kUiPresets);',
  '    j.lit("{\\"ev\\":\\"ui_presets_end\\",\\"capacity\\":"); j.u32(16);'),
 ('V05 ★★ THE GENERATION IS DROPPED FROM THE END RECORD — a list no longer says WHICH catalog it just described',
  '    j.lit(",\\"generation\\":");     j.u32(generation);',
  '    ;'),
 ('V06 the two ACTIVE COUNTS are swapped in the end record — invisible on the compiled defaults, where both are 2',
  '                                             cat.enabled_count(PresetKind::dm),\n                                             cat.enabled_count(PresetKind::channel),',
  '                                             cat.enabled_count(PresetKind::channel),\n                                             cat.enabled_count(PresetKind::dm),'),
 # --- the RESULT -> OUTPUT rule (§3.2.3: "return the resulting record, or the full list for `reset all`") ----------
 ('V07 ★★★ A MUTATING VERB ANSWERS WITH A DUMP — the companion must diff seventeen records to find what it just changed',
  '            if (whole_list) preset_emit_list(cat, out);\n            else            preset_emit_record(cat, static_cast<uint8_t>(slot), out);',
  '            preset_emit_list(cat, out);'),
 ('V08 `reset all` answers with ONE record — for a verb that changed every slot and has no single one to name',
  '            preset_render(cat, 0, /*whole_list=*/true, r, out);         // §3.2.3: `reset all` answers with the LIST',
  '            preset_render(cat, 0, /*whole_list=*/false, r, out);        //'),
 # --- the ONE emitter, i.e. the whole shape of the transport agreement --------------------------------------------
 ('V09 ★★★ THE RECORD\'S TERMINATOR IS STRIPPED at the one emitter ("the sink adds one") — USB still reads correctly, the BLE line sink NEVER SHIPS',
  'inline void preset_emit(IPresetLines& out, const char* buf, size_t n) { out.line(buf, n); }',
  "inline void preset_emit(IPresetLines& out, const char* buf, size_t n) { if (n && buf[n - 1] == '\\n') --n; out.line(buf, n); }"),
 # --- the `busy` table (spec §2) as THIS layer can break it -----------------------------------------------------
 ("V10 ★★★★ `busy` BYPASSED FOR A NO-OP: the verb answers `unchanged` from the LIVE catalog without ever asking the service — the owner's ruled row, broken by an optimisation",
  '        const PresetResult r = cat.set(slot, loc, xt, xn);',
  '        PresetResult r{};\n        const mrnv::UiPresetSlot& cs = cat.slot(static_cast<uint8_t>(slot));\n        if (cs.enabled && cs.loc == (loc ? 1 : 0) && cs.len == xn && memcmp(cs.text, xt, xn) == 0)\n            r.verdict = PresetVerdict::unchanged;\n        else r = cat.set(slot, loc, xt, xn);'),
 ('V11 ★★ `mandatory` IS PRE-EMPTED IN THE PARSER ("check it early") — so `clear emergency` answers `mandatory` DURING AN ALARM, where the ruled answer is `busy`',
  '        const PresetResult r = cat.clear(slot);',
  '        if (preset_slot_mandatory(static_cast<uint8_t>(slot))) { preset_emit_err(PresetErr::mandatory, out); return true; }\n        const PresetResult r = cat.clear(slot);'),
 # --- the boot diagnosis -----------------------------------------------------------------------------------------
 ('V12 ★★★ THE BOOT LINE IS PRINTED FOR `ok` AND `absent` too ("say something reassuring") — an ordinary first boot reads like a fault',
  '    if (!ln) return st;                          // ★ `ok` / `absent` print NOTHING — an ordinary first boot is silent',
  '    if (!ln) ln = "  ui presets = loaded";'),
 ('V13 ★★ THE `invalid` WARNING IS NEVER CLEARED — `cfg` goes on reporting a corrupt record long after a successful mutation repaired it',
  '    void on_result(const PresetResult& r) { if (r.verdict == PresetVerdict::ok) boot = mrnv::UiPresetRead::ok; }',
  '    void on_result(const PresetResult&) { }'),
 # --- the grammar ------------------------------------------------------------------------------------------------
 ("V14 ★★★ `loc=` GOES LENIENT — anything that is not `off` means ON, so `loc=maybe` quietly AIRS THE WEARER'S COORDINATES",
  '    if (t && n == 7 && !memcmp(t, "loc=off", 7)) { out = false; return true; }',
  '    if (t && n >= 4 && !memcmp(t, "loc=", 4)) { out = !(n == 7 && !memcmp(t, "loc=off", 7)); return true; }'),
 ('V15 ★★ THE ONE-DIGIT ORDINAL RULE IS DROPPED — `dm10` parses as `dm1` and the operator edits a slot he did not name',
  '    if (n - pre != 1) return -1;                       // ⛔ exactly one digit — see the block above',
  '    ;'),
 ('V16 the slot TOKEN is emitted 0-based (`dm0`..`dm7`) — the wire names stop matching the grammar that parses them',
  '                           : snprintf(out, cap, "%s%u", preset_kind_name(k), static_cast<unsigned>(o));',
  '                           : snprintf(out, cap, "%s%u", preset_kind_name(k), static_cast<unsigned>(o - 1));'),
 ('V17 an ABSENT text term becomes `bad_text` instead of the grammar — an incomplete line is answered with a reason code instead of the shape',
  '        if (a.exhausted()) return false;\n        const long slot = preset_slot_of_token(st, sn);',
  '        const long slot = preset_slot_of_token(st, sn);'),
 ('V18 a TRAILING TOKEN on `list` is IGNORED rather than refused (C2) — `ui preset list all` silently runs a plain list',
  '        if (!a.exhausted()) return false;                              // C2 — a trailing token is a MISTYPE',
  '        ;'),
 ('V19 ★★ THE MUTATING VERBS STOP DISTINGUISHING `refused` FROM A SUCCESS: a refusal renders the UNCHANGED record, so a companion reads a rejected edit as applied',
  '        case PresetVerdict::refused:\n        case PresetVerdict::nv_failed:',
  '        case PresetVerdict::refused:\n            preset_emit_record(cat, static_cast<uint8_t>(slot), out);\n            return;\n        case PresetVerdict::nv_failed:'),
]

# ===== §B20/B21 — lib/core/node_mac.cpp: the seal's CAP and the DST_HASH guard's TWO conditions ==================
# ★★★ EVERY ENTRY IS A TEMPTING WRONG FIX, NOT A DELETION. B01 is the pre-2026-08-28 tree verbatim ("241 is the
#     carrier's size, what else would you pass?"); B02/B03 are the two ways a reviewer "simplifies" the two-bound
#     reconciliation; B04 is B21's original diagnosis restored ("no DST_HASH means no pubkey — it says so right
#     there"); B05/B06 are the two halves of the silence the row named, each removed on its own so neither can hide
#     behind the other.
MUTS_B20MAC = [
 ("B01 ★★★ [[B20]] VERBATIM: the seal is sized by the BUFFER again (241, a 4-B-MAC constant) instead of the frame — "
  "the 215-216 band is admitted, queued, its RTS airs, and the DATA is dropped at TX time with nothing pushed",
  "        const size_t frame_cap = data_inner_cap(item.flags, type);\n"
  "        const size_t seal_cap  = frame_cap < sizeof item.inner ? frame_cap : sizeof item.inner;",
  "        const size_t seal_cap  = sizeof item.inner;"),
 ("B02 ★★ the carrier boundary is OFF BY ONE — exactly one body length (the cap+1) still disappears, which is the "
  "hardest shape to notice and the reason both sides of every cap are asserted",
  "        const size_t seal_cap  = frame_cap < sizeof item.inner ? frame_cap : sizeof item.inner;",
  "        const size_t seal_cap  = frame_cap + 1 < sizeof item.inner ? frame_cap + 1 : sizeof item.inner;"),
 ("B03 ★★★ THE TWO BOUNDS ARE RECONCILED THE WRONG WAY — the LAXER one wins, so the buffer's 241 re-admits what the "
  "carrier will drop. The brief's named failure: a laxer check re-admitting past the carrier cap",
  "        const size_t seal_cap  = frame_cap < sizeof item.inner ? frame_cap : sizeof item.inner;",
  "        const size_t seal_cap  = frame_cap > sizeof item.inner ? frame_cap : sizeof item.inner;"),
 ("B04 ★★★ [[B21]]'s WRONG CONDITION RESTORED: the guard stops distinguishing 'no key' from 'the key did not fit', "
  "so an oversized sealed DM sends the operator after a key he already holds",
  "            const bool key_known = (dh != 0);",
  "            const bool key_known = false;"),
 ("B05 ★★ [[B21]]'s SILENCE, HALF ONE: the size arm emits but no longer PUSHES — the app is told nothing, which is "
  "exactly the 'no send_failed' the register row names",
  # ⓘ RE-ANCHORED 2026-08-30 (§CUSTODY-B wrapped both B21 pushes in the §6.2(5) lifecycle gate). The MUTATION
  #   IS UNCHANGED — it still deletes the push and leaves the emit — only the anchor text moved.
  "                if (generic_lifecycle) push_send_failed(SendFailReason::too_large, dst, ctr);   // §CUSTODY-B §6.2(5)\n"
  "            } else {",
  "            } else {"),
 ("B06 ★★ [[B21]]'s SILENCE, HALF TWO: the genuinely-keyless arm emits but no longer PUSHES — the sibling defect, "
  "removed separately so neither half can pass on the other's assertion",
  # ⓘ RE-ANCHORED 2026-08-30 (§CUSTODY-B), same reason as B05 above; the mutation itself is unchanged.
  "                if (generic_lifecycle) push_send_failed(SendFailReason::no_pubkey, dst, ctr);   // §CUSTODY-B §6.2(5)\n"
  "            }\n"
  "            return ctr;",
  "            }\n"
  "            return ctr;"),
 # ⚠ B07/B08 attack the RECOVERY, not the refusal, and they are the entries B01-B06 could NOT reach: B01/C03 do go
 #   RED, but on EARLIER airtime/pack assertions, so their RED never proved that a failed pack RELEASES the node for
 #   its next transmission. The measuring assertion is the follow-up DM in the relayed-backstop case; without it both
 #   of these would be inert, which is exactly why that case exists.
 ("B07 ★★★ THE FAILED FLIGHT IS NEVER RELEASED: `_pending_tx` still holds the frame pack_data refused, so nothing "
  "re-fires it and the node's NEXT DM never reaches the air — the pre-2026-08-28 wedge, half one",
  "        _active->_pending_tx.reset(); become_free();\n"
  "        return;",
  "        become_free();\n"
  "        return;"),
 ("B08 ★★★ THE QUEUE IS NEVER PUMPED: the flight IS released, but `become_free()` is the DRAIN (node_mac.cpp:891), "
  "so a DM already waiting behind the doomed forward is never started — the same wedge, other half",
  "        _active->_pending_tx.reset(); become_free();\n"
  "        return;",
  "        _active->_pending_tx.reset();\n"
  "        return;"),
]

# ===== §B20 — lib/core/frame_codec.h: THE ONE LENGTH AUTHORITY ====================================================
# ★★ These attack the FORMULA rather than its consumer. C01 is B20's root cause reproduced AT the authority (the
#    4-B MAC assumed for every frame); C02/C05 drop a term each; C03 is the "cap = the frame budget, the header is
#    someone else's problem" simplification, which is precisely the shape that burns a queue slot and real RTS
#    airtime on a frame that can never air.
MUTS_B20CODEC = [
 ("C01 ★★★ [[B20]]'s ROOT CAUSE, AT THE AUTHORITY: the trailer is assumed to be the 4-B MAC for every frame, so a "
  "CRYPTED inner cap reads 243 instead of 239 and the two silent bytes come back",
  "    const size_t overhead = DATA_HDR_LEN + (type != 0 ? size_t{1} : size_t{0}) + data_mac_len(flags);",
  "    const size_t overhead = DATA_HDR_LEN + (type != 0 ? size_t{1} : size_t{0}) + DATA_MAC_LEN;"),
 ("C02 ★★ the TYPE byte is forgotten in the inner cap, so a TYPED sealed DM (MOBILE_SEND, the team key grant) is "
  "given one byte more than its frame can carry — the same defect one byte narrower",
  "    const size_t overhead = DATA_HDR_LEN + (type != 0 ? size_t{1} : size_t{0}) + data_mac_len(flags);",
  "    const size_t overhead = DATA_HDR_LEN + data_mac_len(flags);"),
 ("C03 ★★★ the cap becomes the WHOLE FRAME BUDGET (the overhead is computed and ignored) — every length is admitted, "
  "a tx-queue slot is burned and the RTS airs for a DATA that pack_data then refuses",
  "    return frame_cap > overhead ? frame_cap - overhead : size_t{0};",
  "    return frame_cap > overhead ? frame_cap : size_t{0};"),
 ("C04 ★★ data_frame_len forgets the TYPE byte, so the packer's refusal and the sender's preflight DRIFT — the exact "
  "two-copies-of-the-arithmetic failure this helper exists to make impossible",
  "    return DATA_HDR_LEN + (type != 0 ? size_t{1} : size_t{0}) + inner_len + data_mac_len(flags);",
  "    return DATA_HDR_LEN + inner_len + data_mac_len(flags);"),
 ("C05 ★★ data_frame_len's trailer collapses to the 4-B MAC — the frame length it reports for a CRYPTED DATA is 4 B "
  "short, so it stops describing the frame pack_data actually writes",
  "    return DATA_HDR_LEN + (type != 0 ? size_t{1} : size_t{0}) + inner_len + data_mac_len(flags);",
  "    return DATA_HDR_LEN + (type != 0 ? size_t{1} : size_t{0}) + inner_len + DATA_MAC_LEN;"),
]


# ★★★ [[B159]] — DE-DUPLICATION VS THE RETRY HORIZON (2026-08-28, correction round).
# The row: "DATA de-duplication can expire inside the retry horizon and deliver a retry twice". The fix has TWO
# halves and each must be independently reachable by a mutation: the SENDER's give-up is now a real deadline (so the
# horizon is bounded at all), and the RECEIVER's retention is that bound plus one measured MAC exchange.
MUTS_B159CONST = [
 ("B01 the retention reverts to the retired 30 s — the [[B159]] defect verbatim",
  "inline constexpr uint32_t seen_origin_ttl_ms = gateway_send_giveup_ms + mac_exchange_margin_ms;",
  "inline constexpr uint32_t seen_origin_ttl_ms = 30000;"),
 ("B02 the start->arrival margin is dropped (the REJECTED first cut: retention == the give-up value)",
  "inline constexpr uint32_t seen_origin_ttl_ms = gateway_send_giveup_ms + mac_exchange_margin_ms;",
  "inline constexpr uint32_t seen_origin_ttl_ms = gateway_send_giveup_ms;"),
 ("B03 the margin drops below ONE EXCHANGE AT THE WORST SUPPORTED PHY (SF12/BW7800/CR8/255B = 279 765 ms)",
  "inline constexpr uint32_t mac_exchange_margin_ms = 300000;",
  "inline constexpr uint32_t mac_exchange_margin_ms = 279764;"),
 ("B03b the margin reverts to the REJECTED corpus-observation value (30 s covers 5 062 ms, not the envelope)",
  "inline constexpr uint32_t mac_exchange_margin_ms = 300000;",
  "inline constexpr uint32_t mac_exchange_margin_ms = 30000;"),
]

MUTS_B159DL = [
 ("B04 the deadline predicate never fires — the give-up returns to a pure timeout-ENTRY test",
  "    if (age < protocol::gateway_send_giveup_ms) return false;",
  "    return false;\n    if (age < protocol::gateway_send_giveup_ms) return false;"),
 ("B05 the deadline is off by one — an air start exactly AT the bound is admitted",
  "    if (age < protocol::gateway_send_giveup_ms) return false;",
  "    if (age <= protocol::gateway_send_giveup_ms) return false;"),
 ("B08 the handoff guard never cancels — a late RTS is judged and then aired anyway",
  "    giveup_flight(SendFailReason::gateway_unreachable, pt.dst, pt.ctr);\n    return true;\n}",
  "    giveup_flight(SendFailReason::gateway_unreachable, pt.dst, pt.ctr);\n    return false;\n}"),
 # ⓘ B09 IS EXPECTED-UNUSABLE, for the reason recorded at the guard: a non-gateway flight is capped at
 # cascade_requeue_total_max_ms(60 s) + one requeue_backoff_ms(<=20 s) = 80 s, so it can never reach the 150 s
 # bound and the scope guard has no reachable behavioural difference. It still encodes which patience governs
 # which flight and must NOT be deleted. Kept as a standing record rather than closed by weakening the code.
 ("B09 the handoff guard stops scoping to a scheduled gateway — it would police every flight",
  "    if (find_gw_schedule(pt.next) == nullptr) return false;",
  "    if (false) return false;"),
]

MUTS_B159MAC = [
 ("B06 the RTS HAL handoff on the DUTY-deferred path stops testing the deadline (the blocker-1 defect verbatim)",
  "    if (rts_handoff_deadline_cancel(d.flight_gen)) { d.pending = false; return; }\n",
  ""),
 ("B07 the RTS HAL handoff on the immediate/LBT-deferred path stops testing the deadline",
  "        if (rts_handoff_deadline_cancel(completion_gen))\n"
  "            return true;                                              // §TX1: a deliberate CANCEL — given up loudly, nothing rejected\n",
  ""),
]

MUTS_B159RX = [
 ("B09 the dedup ignores expiry — a genuinely-new flight reusing the identity is SUPPRESSED (over-correction)",
  "    const bool live_dup = (so != _active->_seen_origins.end() && so->second > nowm);",
  "    const bool live_dup = (so != _active->_seen_origins.end());"),
 ("B10 the expiry PRUNE is removed — the clearing term the B239 audit rests on",
  "    for (auto it = _active->_seen_origins.begin(); it != _active->_seen_origins.end(); )\n"
  "        { if (it->second <= now_ms) { _active->_seen_origin_from.erase(it->first); "
  "it = _active->_seen_origins.erase(it); } else ++it; }\n",
  ""),
 ("B11 capacity pressure REFUSES the new key instead of rolling the oldest",
  "        _active->_seen_origin_from.erase(oldest->first);\n"
  "        _active->_seen_origins.erase(oldest);",
  "        return;"),
]


# ★★★ [[B159]] blocker-1 round 4 — the DEVICE-BOUNDARY decision logic. `lib/hal` is NOT compiled by the simulator
# but IS compiled and linked by the native suite (`.pio/build/native/lib*/hal/device_hal.o` — verified, not assumed),
# so this logic is genuinely host-testable and needs no pure-logic-header extraction.
MUTS_B159HAL = [
 ("B12 the PHYSICAL-START deadline check is dropped — a queued frame airs past its deadline (the blocker verbatim)",
  "    if (e.deadline_ms != 0 && _clock.now_ms() >= e.deadline_ms) {",
  "    if (false) {"),
 ("B13 the NO-DEADLINE sentinel is ignored — ordinary traffic expires too (C2 over-correction)",
  "    if (e.deadline_ms != 0 && _clock.now_ms() >= e.deadline_ms) {",
  "    if (_clock.now_ms() >= e.deadline_ms) {"),
 ("B14 the expiry still calls start_transmit — the frame airs anyway and the outcome lies",
  "        _txq_head = static_cast<uint8_t>((_txq_head + 1) % kTxQCap);   // drop it: the frame never flies\n"
  "        _txq_count--;\n"
  "        push_tx_outcome(expired);\n"
  "        return;",
  "        push_tx_outcome(expired);"),
 ("B15 the expiry outcome drops the sending site's flight identity — correlation becomes impossible",
  "        const TxOutcome expired{ TxOutcomeKind::expired, BusyReason::none, TxResult::ok,\n"
  "                                 e.tag, e.seq, e.sf, 0 };",
  "        const TxOutcome expired{ TxOutcomeKind::expired, BusyReason::none, TxResult::ok,\n"
  "                                 e.tag, 0, e.sf, 0 };"),
]

MUTS_B159MAP = [
 ("B16 the expiry is mapped to a NON-TERMINAL outcome — the flight re-enters retry instead of ending",
  "            giveup_flight(SendFailReason::gateway_unreachable, dst, ctr);\n            return;",
  "            return;"),
 ("B17 the wrong-flight guard is dropped — a SUPERSEDED flight's expiry fails the LIVE flight",
  "            if (info.seq == 0 || !_active->_pending_tx\n"
  "                || _active->_pending_tx->flight_gen != info.seq) return;   // superseded/unowned: report only",
  "            if (!_active->_pending_tx) return;"),
]

# ★★ ADDED 2026-08-28 BY [[B134]] (the durable ESP32/Heltec inbox). Targets are per-SOURCE-FILE because a
#    battery is: the SEAM's own decisions (the path, the mount policy, the append verdict, the segment read, the
#    meta length — now `src/device_inbox_seam.h`, see the re-aim note in TARGET_SRC), the NVS classifier that
#    stayed ESP32-only (`src/device_inbox_fs_esp32.h`), the two behaviours the REUSED ring logic had to gain to
#    be a device store (`lib/core/segmented_inbox_store.h` — `wipe()` and the read-cursor coalescing), and the
#    platform-neutral DELETE CONTRACT that must hold UNCHANGED across the new backend (`lib/core/inbox.cpp`).
# ⚠ The third target re-attacks already-shipped [[B133]]/[[B135]] rulings ON PURPOSE: "the tombstone contract is
#   backend-neutral" is a CLAIM of this slice, and a claim with no controlled mutation is the [[B217]] shape.
#   Kept separate from any other target on the same file so it can be re-proved independently.
# ★★ SPLIT OUT OF `MUTS_B134SEAM` 2026-08-29 BY [[B260]] — the entries are VERBATIM, only their FILE changed.
#    D7 is the one [[B134]] decision that did NOT move to `device_inbox_seam.h`, because an `esp_err_t` is not a
#    fact any other platform has. Its four arms stay aimed at `src/device_inbox_fs_esp32.h`, where they still are.
MUTS_B134NVS = [
 ("F21 ★★★ THE ROUND-4 BLOCKER VERBATIM: an NVS LOOKUP ERROR becomes `absent` — corrupt or unreadable metadata "
  "enters the fresh path exactly as it did through `Preferences::isKey()`, which is `getType()` collapsing "
  "NOT_FOUND and every other NVS error into one PT_INVALID",
  "    if (err != kNvsOk)       return meshroute::MetaLoad::error;",
  "    if (err != kNvsOk)       return meshroute::MetaLoad::absent;"),
 ("F22 ★★★ the ONE absence widens to 'anything that is not OK' — every medium fault is then a first boot, which "
  "is the isKey() conflation restored one layer up",
  "    if (err == kNvsNotFound) return meshroute::MetaLoad::absent;\n"
  "    if (err != kNvsOk)       return meshroute::MetaLoad::error;",
  "    if (err != kNvsOk)       return meshroute::MetaLoad::absent;"),
 ("F23 ★★ the OTHER direction: NOT_FOUND is classified as an error, so a genuinely first-boot node (nvs.h:31 — a "
  "READONLY open of a never-written namespace answers NOT_FOUND) refuses to mount its inbox for ever",
  "    if (err == kNvsNotFound) return meshroute::MetaLoad::absent;",
  "    if (err == kNvsNotFound) return meshroute::MetaLoad::error;"),
 ("F24 ★★ a PRESENT key of the wrong length is called `loaded` — a half-populated Meta then reaches the ring "
  "arithmetic whose seg_count divides and whose head_seg bounds the walk",
  "    return meta_len_ok(got, want) ? meshroute::MetaLoad::loaded : meshroute::MetaLoad::error;",
  "    return meshroute::MetaLoad::loaded;"),
 ("F25 ★★ ...or `absent`, which is the same wrong-length blob taken as a reason to start over",
  "    return meta_len_ok(got, want) ? meshroute::MetaLoad::loaded : meshroute::MetaLoad::error;",
  "    return meta_len_ok(got, want) ? meshroute::MetaLoad::loaded : meshroute::MetaLoad::absent;"),
]

MUTS_B134SEAM = [
 ("F01 ★★★ the torn-write guard drops its COMMIT term — 'w == n is surely enough'. It is not: `File::write` is "
  "fwrite into a 4 KiB stdio buffer and `File::close` returns void, so w==n is a verdict about RAM",
  "    return w == static_cast<size_t>(n) && synced && after == before + n && closed;",
  "    return w == static_cast<size_t>(n) && synced && closed;"),
 ("F02 ★★ the guard accepts ANY progress (`w > 0`) — the classic short-write-is-fine slip; a 3-of-273-byte "
  "record then reports success and the next append is consumed as its body",
  "    return w == static_cast<size_t>(n) && synced && after == before + n && closed;",
  "    return w > 0 && synced && closed;"),
 ("F03 ★★★ the commit is dropped — the growth check then measures the BUFFER, not the medium, so the guard is "
  "still there and still proves nothing (the shape of an instrument that cannot fail)",
  "    const bool     synced = fs.sync();                 // fflush + fsync, BOTH results checked\n"
  "    const uint32_t after  = fs.size();                 // re-stat: the COMMITTED length, not the buffered one",
  "    const bool     synced = true;\n"
  "    const uint32_t after  = fs.size();"),
 # ★★★ THE QG BLOCKER-2/3 ENTRIES (2026-08-28).
 ("F11 ★★★ THE BLOCKER-2 VERBATIM: the SYNC RESULT is dropped from the verdict — a COMPLETE write whose fsync "
  "failed then reports success, which is precisely what `fs::File` does (flush() discards fflush+fsync, close() "
  "returns void) and precisely why this adapter went to POSIX",
  "    return w == static_cast<size_t>(n) && synced && after == before + n && closed;",
  "    return w == static_cast<size_t>(n) && after == before + n && closed;"),
 # ⛔ THERE IS NO ENTRY FOR `LfsIo::sync()` ITSELF, AND THE ABSENCE IS DELIBERATE + STATED. Its nine one-line POSIX
 #   forwards sit inside `#if defined(ARDUINO) && ESP32`, so NO host gate compiles them — the same reality split
 #   `device_nv.h`'s platform arms have always had. A mutation there comes back "the suite still passes", which
 #   would be an instrument reporting on code it never ran. ⇒ what IS attacked here is D3's USE of the result
 #   (F11), which is host-reachable and reddens; that the ESP32 `sync()` really drives `fsync` is M2 bench residue.
 ("F13 ★★ the CLOSE result is dropped — `fclose` still flushes and can still fail on the part the sync did not "
  "cover, and a leaked failure there is the same tear one call later",
  "    return w == static_cast<size_t>(n) && synced && after == before + n && closed;",
  "    return w == static_cast<size_t>(n) && synced && after == before + n;"),
 ("F14 ★★★ the seam's erase always claims success — `wipe()` above it then reports a cleared inbox for a "
  "destructive verb while the records are still on the partition",
  "        IoT io; return io.remove(p);",
  "        IoT io; io.remove(p); return true;"),
 # ⓘ AND FOR THE SAME REASON THERE IS NO ENTRY INSIDE `LfsIo::any_under` ITSELF (its ENOENT-vs-error mapping).
 #   Two were written, both came back "the suite still passes", and both were REMOVED rather than kept as decoration:
 #   that body is ESP32-only. What IS host-reachable — and what F16 below attacks — is the seam's FORWARD of the
 #   inspection verdict, which is where a swallowed `*ok` would actually reach `begin()`.
 ("F18 ★★★ QG ROUND 3: an ITERATION ERROR is read as a clean END OF DIRECTORY — a walk that died partway then "
  "reports 'this store holds no records' over a live history",
  "        if (!w.next(&have, &size)) { err = true; break; }           // ⛔ an ERROR is never a clean end",
  "        if (!w.next(&have, &size)) break;"),
 ("F19 ★★ the iteration error is DETECTED but not reported — `ok` stays true, so the caller still reads the "
  "unanswerable walk as an authoritative 'empty'",
  "    if (err) { if (ok) *ok = false; return false; }\n"
  "    return found;",
  "    return found;"),
 ("F20 ★★★ an `opendir` failure keeps `ok` true regardless of cause — the ENOENT-only rule collapses and any FS "
  "error over a live store becomes 'no records'",
  "    if (!w.open(&absent)) { if (ok) *ok = absent; return false; }   // ⛔ only a real absence keeps `ok` true",
  "    if (!w.open(&absent)) return false;"),
 ("F16 ★★★ QG ROUND 2: the seam SWALLOWS the inspection verdict — the backend says 'I could not answer' and the "
  "store is told 'no records', which hands begin() straight to the silent re-initialise path over a live history",
  "    bool any_segments(bool* ok) const override { IoT io; return io.any_under(_dir, ok); }",
  "    bool any_segments(bool* ok) const override {\n"
  "        if (ok) *ok = true;\n"
  "        IoT io; bool ignored = true; return io.any_under(_dir, &ignored);\n"
  "    }"),
 ("F15 ★★ a failed record-folder create no longer fails the mount — every append afterwards fails at `fopen` for "
  "a reason nothing at boot names",
  "        IoT io;\n"
  "        return io.ensure_dir(_dir);",
  "        IoT io;\n"
  "        io.ensure_dir(_dir);\n"
  "        return true;"),
 ("F04 ★★ the mount formats on the FIRST attempt — 'formatOnFail is the recovery, just pass true' silently "
  "reformats a recoverable filesystem and can never report that it did",
  "    if (fs.mount(/*format_on_fail=*/false)) { r.mounted = true; return r; }   // clean mount -> nothing was erased\n"
  "    r.mounted   = fs.mount(/*format_on_fail=*/true);                          // corrupt/blank -> format + remount\n"
  "    r.formatted = r.mounted;                                                  // ONLY a successful reformat wiped records",
  "    r.mounted   = fs.mount(/*format_on_fail=*/true);\n"
  "    r.formatted = false;"),
 ("F05 ★★★ EPOCH PERSISTENCE DROPPED at the source: a real format is never REPORTED, so §10.1 never bumps and "
  "the companion keeps its cursors into a history that no longer exists",
  "    r.formatted = r.mounted;                                                  // ONLY a successful reformat wiped records",
  "    r.formatted = false;"),
 ("F06 ★★ the over-correction: a FAILED format is reported as a wipe — an unmountable store bumps the epoch and "
  "makes every companion re-pull for nothing",
  "    r.formatted = r.mounted;                                                  // ONLY a successful reformat wiped records",
  "    r.formatted = true;"),
 ("F07 ★★ only the FIRST store learns the mount had to format — the second store's epoch never bumps, so half "
  "the wiped history is silently re-read against stale cursors",
  "        if (formatted) *formatted = _once->formatted;",
  "        if (formatted) *formatted = false;"),
 ("F08 ★★ the meta length verdict accepts a SHORT read — a half-populated Meta reaches the ring arithmetic whose "
  "seg_count divides and whose head_seg bounds the walk",
  "inline bool meta_len_ok(int got, uint16_t want) { return got == static_cast<int>(want); }",
  "inline bool meta_len_ok(int got, uint16_t want) { (void)want; return got > 0; }"),
 ("F09 ★ the whole-segment read stops clamping to cap — a segment larger than the 4 KiB scratch overruns it",
  "    if (sz > cap) sz = cap;",
  "    (void)cap;"),
 ("F10 ★ the path formatter's zero-capacity guard is removed ('cap is never 0') — it then writes a terminator "
  "into a buffer it was told it may not touch",
  "    if (!out || cap == 0) return;\n"
  "    size_t p = 0;",
  "    if (!out) return;\n"
  "    size_t p = 0;"),
]

# ★★ ADDED 2026-08-29 BY [[B260]] (retiring the hand-maintained nRF52 inbox twin). ⓘ THE SIZE OF THIS LIST IS
#    ITSELF THE SLICE'S MEASUREMENT: the retired `src/device_inbox_store.h` held a whole second ring — begin,
#    append, read_since, the eviction, the §10.1 detect — every line of which would have needed its own entries
#    here. After the retirement the nRF52 path decides exactly TWO things of its own, and everything else it now
#    does is attacked by `b134seam` / `b134store` / `b134inbox` on the SHARED code it runs.
# ⛔ THERE ARE NO ENTRIES INSIDE `QspiIo`, `QspiDirWalk` OR `InternalFsWriteIo`, AND THE ABSENCE IS DELIBERATE +
#    STATED (the `LfsIo` precedent). Those bodies sit inside `#if defined(ARDUINO) && nRF52 && QSPIFLASH`, so no
#    host gate compiles them; a mutation there comes back "the suite still passes", which is an instrument
#    reporting on code it never ran. What IS attacked is the DECISIONS they feed — N1's classification and N2's
#    verdict — both host-reachable and both reddening. That the adapters really call `lfs_file_sync` /
#    `lfs_dir_read` / `lfs_remove` on real QSPI is M2 bench residue, not a battery claim.
MUTS_B260 = [
 # ---- N1: the InternalFS meta load's three-valued verdict ----
 ("N01 \u2605\u2605\u2605 THE ROW'S \u2462 VERBATIM, ONE LAYER DOWN: a BACKEND REFUSAL becomes `absent` \u2014 a mount that failed, "
  "a metadata error or an open that failed on a file that EXISTS all enter begin()'s fresh path, which "
  "re-initialises over a live QSPI history: head/tail to 0/0 hides every other segment while it is physically "
  "present, and next_seq to 1 reuses sequences the companion has already filed",
  "    if (io.backend_failed) return meshroute::MetaLoad::error;    // \u26d4 FIRST: a refusal is never an absence",
  "    if (io.backend_failed) return meshroute::MetaLoad::absent;"),
 ("N02 \u2605\u2605\u2605 the refusal check is DROPPED ENTIRELY \u2014 `fs_read_slot` answers kSlotAbsent for a failed mount AND "
  "for a first boot alike, so removing this line is exactly the [[B218]] conflation restored",
  "    if (io.backend_failed) return meshroute::MetaLoad::error;    // \u26d4 FIRST: a refusal is never an absence\n"
  "    if (n == mrnv::kSlotAbsent) return meshroute::MetaLoad::absent;",
  "    if (n == mrnv::kSlotAbsent) return meshroute::MetaLoad::absent;"),
 ("N03 \u2605\u2605 the ORDER is inverted \u2014 the absent sentinel is tested BEFORE the refusal, so every backend failure "
  "is still read as a fresh device even though the check is right there",
  "    if (io.backend_failed) return meshroute::MetaLoad::error;    // \u26d4 FIRST: a refusal is never an absence\n"
  "    if (n == mrnv::kSlotAbsent) return meshroute::MetaLoad::absent;",
  "    if (n == mrnv::kSlotAbsent) return meshroute::MetaLoad::absent;\n"
  "    if (io.backend_failed) return meshroute::MetaLoad::error;"),
 ("N04 \u2605\u2605 an OVER-LENGTH meta file is accepted \u2014 the read takes a valid PREFIX of a longer blob and "
  "presents it as the whole record, which the length alone can never show",
  "    if (io.oversize) return meshroute::MetaLoad::error;          // a valid PREFIX is not a valid record",
  "    (void)io;"),
 ("N05 \u2605\u2605\u2605 THE MIGRATION HOLE: a WRONG-LENGTH read is called `loaded` \u2014 the retired twin's 24-byte v6 blob "
  "is then interpreted as the shared store's 28-byte v4 Meta, so `records_state` and four bytes of `epoch` are "
  "whatever the stack left behind, and the store mounts on them",
  "    return mrinboxfs::meta_len_ok(n, want) ? meshroute::MetaLoad::loaded : meshroute::MetaLoad::error;",
  "    (void)want; return meshroute::MetaLoad::loaded;"),
 ("N06 \u2605\u2605\u2605 ...or `absent`, which is the same stale blob taken as a REASON TO START OVER \u2014 the silent "
  "re-initialise the whole three-valued load exists to prevent, arriving through the migration door",
  "    return mrinboxfs::meta_len_ok(n, want) ? meshroute::MetaLoad::loaded : meshroute::MetaLoad::error;",
  "    return mrinboxfs::meta_len_ok(n, want) ? meshroute::MetaLoad::loaded : meshroute::MetaLoad::absent;"),
 ("N07 \u2605\u2605 a genuine first boot is called an ERROR \u2014 the other direction, and it is not harmless: a "
  "freshly-flashed node would refuse to mount its inbox for ever",
  "    if (n == mrnv::kSlotAbsent) return meshroute::MetaLoad::absent;",
  "    if (n == mrnv::kSlotAbsent) return meshroute::MetaLoad::error;"),
 # ---- N2: the checked meta commit ----
 ("N08 \u2605\u2605\u2605 THE ROW'S \u2460 AT ITS SOURCE: the SYNC RESULT is dropped from the meta save's verdict \u2014 a complete "
  "write whose commit failed then reports success, so every `save_meta()` the shared store CHECKS becomes a "
  "checked lie, and a rotation acknowledges a record the next boot cannot reach",
  "    return w == static_cast<size_t>(n) && synced && after == n && closed;",
  "    return w == static_cast<size_t>(n) && after == n && closed;"),
 ("N09 \u2605\u2605\u2605 the meta save always succeeds \u2014 the twin's behaviour restored wholesale; the store's checks all "
  "pass and nothing is ever actually persisted-and-verified",
  "    return w == static_cast<size_t>(n) && synced && after == n && closed;",
  "    (void)w; (void)synced; (void)after; (void)closed; return true;"),
 ("N10 \u2605\u2605 the guard accepts ANY progress (`w > 0`) \u2014 a partially written 28-byte Meta is a half-populated "
  "struct whose seg_count divides and whose head_seg bounds a ring walk on the next boot",
  "    return w == static_cast<size_t>(n) && synced && after == n && closed;",
  "    return w > 0 && synced && after == n && closed;"),
 ("N11 \u2605\u2605 the MEDIUM-SIDE length term is dropped \u2014 a write that RETURNS n while committing less then "
  "reports a durable save, which is the fault a return value alone can never see",
  "    return w == static_cast<size_t>(n) && synced && after == n && closed;",
  "    return w == static_cast<size_t>(n) && synced && closed;"),
 ("N12 \u2605\u2605 the length term is loosened to `>=` \u2014 an overwrite that APPENDED instead of truncating passes, and "
  "the next boot reads a valid PREFIX of a stale blob as the live metadata (N1's `oversize` arm, one boot later)",
  "    return w == static_cast<size_t>(n) && synced && after == n && closed;",
  "    return w == static_cast<size_t>(n) && synced && after >= n && closed;"),
 ("N13 \u2605 the CLOSE result is dropped \u2014 the close still flushes and can still fail on what the sync did not "
  "cover, and a leaked failure there is the same lost write one call later",
  "    return w == static_cast<size_t>(n) && synced && after == n && closed;",
  "    return w == static_cast<size_t>(n) && synced && after == n;"),
 ("N14 \u2605\u2605 the commit is skipped and the length re-read anyway \u2014 the growth term then measures the same "
  "unsynced state it measured before, so the guard is still there and still proves nothing",
  "    const bool     synced = fs.sync();\n"
  "    const uint32_t after  = fs.size();     // re-stat: the COMMITTED length, not the in-RAM one",
  "    const bool     synced = true;\n"
  "    const uint32_t after  = fs.size();"),
 # ---- MetaStoreOver: the composition itself ----
 ("N15 \u2605\u2605\u2605 the meta store SWALLOWS the backend's verdict \u2014 `SlotIo` is collected and then classified as if "
  "it were clean, so the four facts `fs_read_slot` separates are conflated again at the one seam that consumes "
  "them (the [[B221]] shape: a shim signature that erases the distinction)",
  "        const int n = mrnv::fs_read_slot(fs, _path, blob, len, &io);\n"
  "        return classify_meta_read(n, io, len);",
  "        const int n = mrnv::fs_read_slot(fs, _path, blob, len, &io);\n"
  "        return classify_meta_read(n, mrnv::SlotIo{}, len);"),
 ("N16 \u2605\u2605 the meta store stops asking for the backend facts at all (`io = nullptr`), which is the pre-[[B218]] "
  "call shape \u2014 no lookup, no size question, and a metadata error indistinguishable from a fresh device",
  "        const int n = mrnv::fs_read_slot(fs, _path, blob, len, &io);",
  "        const int n = mrnv::fs_read_slot(fs, _path, blob, len, nullptr);"),
]

MUTS_B134STORE = [
 ("G01 ★★★ wipe() reverts to the base NO-OP — `factory_reset confirm` and `prep-restart` leave the ENTIRE "
  "durable history on the medium, which is the exact state [[B134]] exists to end",
  "        for (uint16_t i = 0; i < ring_segs(); ++i) if (!_records->seg_erase(i)) ok = false;\n"
  "        _meta.head_seg = _meta.tail_seg = 0;",
  "        _meta.head_seg = _meta.tail_seg = 0;"),
 ("G02 ★★ wipe() erases the records but leaves the ring bookkeeping stale (its nRF52 twin's shape) — the store "
  "resumes at a head that describes bytes that no longer exist",
  "        _meta.head_seg = _meta.tail_seg = 0;\n"
  "        _meta.seg_count = ring_segs();\n"
  "        _total = 0; _count = 0; _head_sealed = false;",
  "        _head_sealed = false;"),
 # ⓘ G03 (the original "wear-coalescing removed" entry) WAS RETIRED IN QG ROUND 7, not lost: its anchor was the
 #   one-line `set_read_cursor` that round 7 replaced, and its content is now **G40** against the current form.
 #   Recorded rather than silently renumbered, so the battery's history stays auditable.
 ("G04 ★★ wipe() also resets next_seq — 'a wipe means start over' makes a wiped store REUSE sequences the "
  "companion has already filed, which no epoch bump can repair",
  "        _total = 0; _count = 0; _head_sealed = false;",
  "        _total = 0; _count = 0; _head_sealed = false; _meta.next_seq = 1;"),
 # ★★★ THE QG BLOCKER-1/3 ENTRIES (2026-08-28). Each one restores, at match count 1, exactly the line the QG round
 #     found — a metadata or erase result being DISCARDED. Every one of them is a "reported success without durable
 #     success", which is why they are written as the ORIGINAL bare call rather than as a deletion.
 ("G05 ★★★ THE BLOCKER VERBATIM: the ROTATION's save_meta() result is discarded again — the head moves, the record "
  "is written and `append` returns TRUE while the persisted meta still points at the OLD head, so a reboot loses an "
  "ACKNOWLEDGED record and a tombstone written this way lets the deleted message COME BACK",
  "        if (!save_meta()) { _meta_dirty = true; return false; }",
  "        save_meta();"),
 ("G06 ★★★ the RETRY-BEFORE-WRITE latch is dropped — after one failed save the store keeps accepting records under "
  "a topology the medium does not have, and never re-persists it either",
  "    if (_meta_dirty) { if (!save_meta()) return false; _meta_dirty = false; }",
  "    _meta_dirty = false;"),
 ("G07 ★★ the retry is attempted but its RESULT is ignored — 'we tried, carry on': the append proceeds under the "
  "same unpersisted topology it was supposed to refuse",
  "    if (_meta_dirty) { if (!save_meta()) return false; _meta_dirty = false; }",
  "    if (_meta_dirty) { save_meta(); _meta_dirty = false; }"),
 ("G08 ★★★ begin()'s FRESH-baseline save goes back to persisting nothing — the store runs with no topology, epoch "
  "or high-water on the medium, and a reboot inside the first seq batch REUSES sequences over a live log",
  "        if (!save_meta()) { _fault = SegMountFault::meta_unwritable; return false; }\n"
  "    } else if (!version_ok) {",
  "    } else if (!version_ok) {"),
 ("G09 ★★ begin()'s §10.1 epoch-bump save is unchecked — the records were wiped, the bump never reaches the "
  "medium, and the companion keeps its cursors against a history that no longer exists",
  "        //    its cursors against a history that no longer exists and silently never re-pulls. Fail the mount.\n"
  "        if (!save_meta()) { _fault = SegMountFault::meta_unwritable; return false; }\n"
  "    }",
  "        save_meta();\n"
  "    }"),
 ("G10 ★★ the ROLL's target-segment erase is unchecked — stale lapped bytes survive into the new head and "
  "read_since parses them as frames (the §B135 mis-parse arriving from the other end)",
  "        if (!_records->seg_erase(next_head)) { _meta_dirty = true; return false; }",
  "        _records->seg_erase(next_head);"),
 ("G11 ★★★ wipe() ignores every erase result — `prep-restart` and `factory_reset` report a cleared inbox over "
  "records that are still RECOVERABLE on flash (a data-retention lie, the worst direction)",
  "        for (uint16_t i = 0; i < ring_segs(); ++i) if (!_records->seg_erase(i)) ok = false;",
  "        for (uint16_t i = 0; i < ring_segs(); ++i) _records->seg_erase(i);"),
 ("G12 ★★ wipe() ignores the metadata half — the segments went but the topology saying so did not, and the verb "
  "still claims success",
  "        if (!save_meta()) { _meta_dirty = true; ok = false; }\n"
  "        return ok;",
  "        save_meta();\n"
  "        return ok;"),
 ("G13 ★★ wipe() STOPS at the first failing segment — same `false`, but strictly MORE recoverable history left "
  "behind, which is the opposite of what a destructive verb owes",
  "        for (uint16_t i = 0; i < ring_segs(); ++i) if (!_records->seg_erase(i)) ok = false;",
  "        for (uint16_t i = 0; i < ring_segs(); ++i) if (!_records->seg_erase(i)) return false;"),
 # ★★★ THE QG ROUND-2 ENTRIES — the boot-recovery discriminator. Every one is "invalid metadata over a LIVE log
 #     silently becomes a fresh store", which hides records AND reuses sequences in one step.
 ("G15 ★★★ THE ROUND-2 BLOCKER VERBATIM: the fresh-vs-corrupted DISCRIMINATOR is dropped — missing/invalid "
  "metadata over EXISTING records re-initialises again, so head/tail reset to 0/0 hides every segment past the "
  "first and next_seq resets to 1 over a log the companion has already filed",
  "        if (have_records && !formatted) { _fault = SegMountFault::meta_lost_over_records; return false; }",
  "        (void)have_records;"),
 ("G16 ★★★ the INSPECTION FAILURE is mapped onto 'no records' — the other door into the same silent re-init: a "
  "records store that could not ANSWER is treated as one that answered 'empty'",
  "    if (!insp_ok) { _fault = SegMountFault::records_uninspectable; return false; }",
  "    (void)insp_ok;"),
 ("G17 ★★ the discriminator is inverted by trusting `formatted` alone — a store that mounted CLEANLY over live "
  "records is re-initialised because nothing had to be formatted",
  "        if (have_records && !formatted) { _fault = SegMountFault::meta_lost_over_records; return false; }",
  "        if (have_records && formatted) { _fault = SegMountFault::meta_lost_over_records; return false; }"),
 ("G18 ★★ the discriminator REFUSES EVERY torn meta, records or not — first boot and every post-format boot then "
  "come up with a dead inbox, which is the 19.7 reflash-wipes-once expectation broken from the safe side",
  "        if (have_records && !formatted) { _fault = SegMountFault::meta_lost_over_records; return false; }",
  "        { _fault = SegMountFault::meta_lost_over_records; return false; }"),
 # ★★★ THE QG ROUND-3 ENTRIES — CORRUPT IS NOT ABSENT, and the discriminator is attacked in BOTH directions.
 ("G19 ★★★ THE ROUND-3 BLOCKER VERBATIM: a CORRUPT meta is read back as ABSENT — so corrupt metadata over an "
  "EMPTY record store is 'first boot' again, which is exactly the post-prep-restart state where the meta is the "
  "only thing still holding the sequence high-water and the epoch",
  "    const MetaLoad ml = load_meta();\n"
  "    if (ml == MetaLoad::error) { _fault = SegMountFault::meta_corrupt; return false; }\n"
  "    const bool had_meta   = (ml == MetaLoad::loaded);",
  "    const MetaLoad ml = load_meta();\n"
  "    const bool had_meta   = (ml == MetaLoad::loaded);"),
 ("G20 ★★ the OTHER direction: a genuinely ABSENT meta is treated as corrupt, so FIRST BOOT refuses to mount and "
  "every freshly flashed node comes up with a dead inbox (19.7 broken from the safe side)",
  "    if (ml == MetaLoad::error) { _fault = SegMountFault::meta_corrupt; return false; }",
  "    if (ml != MetaLoad::loaded) { _fault = SegMountFault::meta_corrupt; return false; }"),
 ("G21 ★★★ `load_meta` stops classifying a STRUCTURALLY IMPOSSIBLE blob as an error and calls it absent again — "
  "the same conflation one layer down, where a torn seg_count/head/tail becomes 'we must be fresh'",
  "        if (_meta.magic != kMagic || _meta.seg_count != ring_segs()\n"
  "            || _meta.head_seg >= _meta.seg_count || _meta.tail_seg >= _meta.seg_count) return MetaLoad::error;",
  "        if (_meta.magic != kMagic || _meta.seg_count != ring_segs()\n"
  "            || _meta.head_seg >= _meta.seg_count || _meta.tail_seg >= _meta.seg_count) return MetaLoad::absent;"),
 # ★★★ THE QG ROUND-5 ENTRIES — the acknowledged-empty marker. Each attacks one of the four transitions.
 ("G23 ★★★ THE ROUND-5 BLOCKER VERBATIM: the §10.1 detect goes back to guarding on `next_seq > 1`, which says "
  "'this store once had traffic' and stays true for ever — so every reboot of an empty store bumps the epoch "
  "again (2, 3, 4...) and the companion re-pulls an unchanged empty inbox on each one",
  "    if (version_ok && records_empty && _meta.records_state == kRecordsNonEmpty) {",
  "    if (version_ok && records_empty && _meta.next_seq > 1) {"),
 ("G24 ★★★ the detect stops RECORDING the acknowledgement — it bumps correctly once but never marks the store "
  "empty, so the very next boot detects the same loss again: the ratchet restored one line lower",
  "        _meta.records_state = kRecordsEmpty;               // ★ acknowledged — the next boot must NOT bump again",
  "        ;"),
 ("G25 ★★★ the EMPTY -> NON_EMPTY transition's save is unchecked — a record is acknowledged under a marker that "
  "never reached the medium, so the next boot reads it as an external loss and bumps for nothing",
  "        if (!save_meta()) { _meta.records_state = kRecordsEmpty; _meta_dirty = true; return false; }",
  "        save_meta();"),
 ("G26 ★★ ...or the transition is not persisted AT ALL — the marker lives only in RAM, so every boot after a "
  "wipe re-detects the first append's records as a loss",
  "        _meta.records_state = kRecordsAppendPending;\n"
  "        if (!save_meta()) { _meta.records_state = kRecordsEmpty; _meta_dirty = true; return false; }",
  "        _meta.records_state = kRecordsAppendPending;"),
 ("G27 ★★★ the WIPE transition's bump+mark is dropped — a deliberate wipe stops telling the companion anything, "
  "and the §10.1 arm then has to re-derive it on the next boot (which is the ratchet again)",
  "        if (had_history) _meta.epoch += 1;",
  "        (void)had_history;"),
 ("G28 ★★ the wipe bumps UNCONDITIONALLY — wiping an already-empty store destroyed no history but still tells "
  "every companion to drop its cursors and re-pull",
  "        if (had_history) _meta.epoch += 1;",
  "        _meta.epoch += 1;"),
 ("G29 ★★★ THE EXTERNAL-LOSS ARM IS DROPPED ENTIRELY — §10.1 goes dead, so a records wipe outside this store is "
  "never announced and the companion reads its cursors against a history that no longer exists",
  "    if (version_ok && records_empty && _meta.records_state == kRecordsNonEmpty) {",
  "    if (false) {"),
 # ★★★ THE QG ROUND-7 ENTRIES — the durable read-cursor's "success that isn't".
 ("G37 ★★★ THE ROUND-7 BLOCKER VERBATIM: the save result is discarded, so `mark_read` reports success over a "
  "cursor that never reached the medium",
  "        if (save_meta()) { _meta_dirty = false; return true; }\n"
  "        _meta.read_cursor = prev;                                    // RAM must never out-run the medium\n"
  "        _meta_dirty = true;\n"
  "        return false;",
  "        save_meta();\n"
  "        return true;"),
 ("G38 ★★★ the failed value is RETAINED in RAM — the cursor then LOOKS persisted, and the retry with the same "
  "value is swallowed by the coalescing, so the medium can never be repaired",
  "        _meta.read_cursor = prev;                                    // RAM must never out-run the medium",
  "        ;"),
 ("G39 ★★★ the coalescing stops consulting the dirty latch — a repeat of the same cursor short-circuits to "
  "success even while the store knows its metadata did not persist",
  "        if (seq == _meta.read_cursor && !_meta_dirty) return true;   // genuinely nothing to write",
  "        if (seq == _meta.read_cursor) return true;"),
 ("G40 ★★ the wear guard is removed instead of being made dirty-aware — every mark_read in a pull session "
  "rewrites the meta blob again, which is the churn the coalescing exists to stop",
  "        if (seq == _meta.read_cursor && !_meta_dirty) return true;   // genuinely nothing to write",
  "        ;"),
 ("G41 ★★ `set_next_seq` keeps the un-persisted high-water in RAM — `persisted_next_seq()` then reports a value "
  "that is not on the medium, which is the same over-claim one field along",
  "        _meta.next_seq = prev;\n"
  "        _meta_dirty = true;\n"
  "        return false;",
  "        return false;"),
 # ★★★ THE QG ROUND-6 ENTRIES — `append_pending` and the marker's range check.
 ("G30 ★★★ THE ROUND-6 GAP VERBATIM: the pending state is dropped and the marker goes straight to `non_empty` "
  "before any bytes are written — a first append whose record write then lands NOTHING leaves that claim over an "
  "empty medium, and the next boot bumps the epoch for a message that never existed",
  "        _meta.records_state = kRecordsAppendPending;\n"
  "        if (!save_meta()) { _meta.records_state = kRecordsEmpty; _meta_dirty = true; return false; }",
  "        _meta.records_state = kRecordsNonEmpty;\n"
  "        if (!save_meta()) { _meta.records_state = kRecordsEmpty; _meta_dirty = true; return false; }"),
 ("G31 ★★★ pending resolves to `non_empty` REGARDLESS of the medium — 'an append was attempted, so there must be "
  "something there': the resolve stops looking and the false-wipe bump comes straight back",
  "        _meta.records_state = records_empty ? kRecordsEmpty : kRecordsNonEmpty;",
  "        _meta.records_state = kRecordsNonEmpty;"),
 ("G32 ★★ ...or to `empty` regardless — the opposite guess, which DISARMS §10.1 for a store whose records really "
  "did land and then really were lost",
  "        _meta.records_state = records_empty ? kRecordsEmpty : kRecordsNonEmpty;",
  "        _meta.records_state = kRecordsEmpty;"),
 ("G33 ★★★ the pending RESOLUTION bumps the epoch — resolving a bookkeeping question is reported to the companion "
  "as a wipe, so every interrupted append costs a full re-pull",
  "    if (version_ok && _meta.records_state == kRecordsAppendPending) {\n"
  "        _meta.records_state = records_empty ? kRecordsEmpty : kRecordsNonEmpty;",
  "    if (version_ok && _meta.records_state == kRecordsAppendPending) {\n"
  "        _meta.epoch += 1;\n"
  "        _meta.records_state = records_empty ? kRecordsEmpty : kRecordsNonEmpty;"),
 ("G34 ★★★ the records_state RANGE CHECK is removed — a corrupt marker value falls through every arm that tests "
  "it, so the store mounts with fault=0 over a partition it cannot classify and §10.1 is bypassed entirely",
  "        if (_meta.records_state > kRecordsStateMax) return MetaLoad::error;",
  "        ;"),
 ("G35 ★★ the range check REFUSES A VALID VALUE (`pending` becomes out of range) — every store caught mid-append "
  "then refuses to mount for ever, which is the guard broken from the safe side",
  "        if (_meta.records_state > kRecordsStateMax) return MetaLoad::error;",
  "        if (_meta.records_state >= kRecordsStateMax) return MetaLoad::error;"),
 ("G36 ★★ the wipe's pending arm stops consulting the medium and treats pending as history — an append that "
  "never landed then makes a wipe announce a loss that did not happen",
  "                              || (_meta.records_state == kRecordsAppendPending && _total_before_erase > 0);",
  "                              || (_meta.records_state == kRecordsAppendPending);"),
 ("G14 ★ the EVICTION path charges back bytes it never erased — `_total` under-counts the medium, so the byte cap "
  "stops capping and the partition fills past its ring",
  "        if (!_records->seg_erase(_meta.tail_seg)) { _meta_dirty = true; break; }\n"
  "        _total -= (tsz <= _total ? tsz : _total);",
  "        _records->seg_erase(_meta.tail_seg);\n"
  "        _total -= (tsz <= _total ? tsz : _total);"),
]

MUTS_B134ACK = [
 ("A01 ★★★ the ack's result mapping becomes UNCONDITIONAL SUCCESS — the exact shape the verb had before QG round "
  "7: a cursor the store refused to persist is reported to the companion as marked",
  'inline const char* inbox_mark_result(bool persisted) { return persisted ? "marked" : "io_error"; }',
  'inline const char* inbox_mark_result(bool persisted) { (void)persisted; return "marked"; }'),
 ("A02 ★★ the mapping is INVERTED — a persisted cursor reports io_error, so a working node looks broken and the "
  "companion retries for ever",
  'inline const char* inbox_mark_result(bool persisted) { return persisted ? "marked" : "io_error"; }',
  'inline const char* inbox_mark_result(bool persisted) { return persisted ? "io_error" : "marked"; }'),
]

MUTS_B134RAM = [
 ("R01 ★★★ wipe() drops back to the base's successful NO-OP — `prep-restart` HALTS but does NOT reboot, so the "
  "console reports a cleared inbox while `pull_inbox` still streams every record until the operator cuts power",
  "    bool wipe() override { _head = 0; _count = 0; _read_cursor = 0; return true; }",
  "    // (inherits the base no-op)"),
 ("R02 ★★ wipe() empties the ring but leaves the READ CURSOR pointing at records that no longer exist — the "
  "unread badge then counts against a history that was just destroyed",
  "    bool wipe() override { _head = 0; _count = 0; _read_cursor = 0; return true; }",
  "    bool wipe() override { _head = 0; _count = 0; return true; }"),
 ("R03 ★★ wipe() also re-rolls the storage epoch — it announces a wipe to the companion that the imminent reboot "
  "makes moot, forcing a full re-pull for nothing",
  "    bool wipe() override { _head = 0; _count = 0; _read_cursor = 0; return true; }",
  "    bool wipe() override { _head = 0; _count = 0; _read_cursor = 0; _epoch += 1; return true; }"),
]

MUTS_B134INBOX = [
 ("H01 ★★★ TOMBSTONE CAP IGNORED — erase() writes a marker past the bound pull()'s fixed pre-pass array can "
  "hold, so a deleted message becomes visible again: the one outcome §3.5 forbids",
  "    if (s.tombs >= protocol::inbox_max_tombstones) return InboxEraseResult::io_error;",
  "    (void)s.tombs;"),
 ("H02 ★★ a REPEAT delete reports `erased` instead of not_found — the modal shows success and appends a second "
  "marker against the cap for a record that was already gone",
  "    if (!s.live || s.tombstoned) return InboxEraseResult::not_found;",
  "    if (!s.live) return InboxEraseResult::not_found;"),
 ("H03 ★★★ the verdict stops asking whether the append LANDED — a torn tombstone reports `erased` while the "
  "message is still in every future pull ('a success that isn't', across the NEW backend this time)",
  "    return appended ? InboxEraseResult::erased : InboxEraseResult::io_error;",
  "    return InboxEraseResult::erased;"),
 ("H04 ★★★ BOTH mounts stop running: the `||` short-circuit is restored, so a DM failure means the CHANNEL store "
  "never attempts its mount and two corrupted keys report 5/0 — a diagnostic that UNDER-STATES the damage",
  "    const bool dm_ok = _dm->begin();\n"
  "    const bool ch_ok = _chan->begin();\n"
  "    if (!dm_ok || !ch_ok) {",
  "    if (!_dm->begin() || !_chan->begin()) {"),
 ("H05 ★★★ `mark_read` DISCARDS the store's verdict again — the verb above it then acks success for a cursor the "
  "store refused to persist, and an unwired inbox reports success too",
  "    if (!enabled()) return false;                                 // an unwired inbox persisted nothing\n"
  "    InboxStore* s = (kind == InboxKind::dm) ? _dm : _chan;\n"
  "    return s->set_read_cursor(seq);                               // ⛔ the verdict is RELAYED, never discarded",
  "    if (!enabled()) return true;\n"
  "    InboxStore* s = (kind == InboxKind::dm) ? _dm : _chan;\n"
  "    s->set_read_cursor(seq);\n"
  "    return true;"),
]

####################################################################################################################
# §A0 — the custody spec's characterization + audit slice (2026-08-29).
#
# ★★★ WHAT THESE THREE BATTERIES EXIST TO PROVE, and it is NOT the usual thing: A0 lands ZERO production change,
#     so there is no new behaviour to defend. What must be defended is the CLAIM that
#     `test/test_data_type_audit_a0.cpp` measures the DATA surface it says it measures. A characterization pin that
#     cannot fail is worse than none — it launders an assumption into an "assertion". Spec §18.0.4 requires a
#     negative control per claim; these are those controls.
# ⓘ ALL THREE COMPILE `lib/core`, so the whole native suite rebuilds behind each mutation — expect slow runs. That
#   cost is the point: these are exactly the arms the corpus cannot see (s18 originates no typed answer at all).
####################################################################################################################

MUTS_A0RX = [
 # ⛔⛔ THE SUBJECT: the addressed-consumer dispatch in `do_post_ack` (node_mac_rx.cpp:1551-1934) is a flat IF-CHAIN
 #     with NO `else`/`default` arm, so an unknown type reaches the generic deliver tail and becomes user inbox
 #     content + a `msg_recv` push. A04 installs the fail-closed guard Slice B is specified to add (§6.2(2)).
 # ★ IT MUST REDDEN §A0-4 AND LEAVE §A0-4b GREEN: the consumed types return earlier and are untouched. INTRO and
 #   SEALED_RELAY are exempted from the guard because they fall through DELIBERATELY (:1862 strips a prefix, :1901
 #   sets crypted_ok) — mutating those would redden half the suite for an unrelated reason and prove nothing about
 #   the unknown-type claim.
 ("A04 ★★★ THE MISSING DEFAULT ARM IS INSTALLED (Slice B's fail-closed rule, applied early) — an addressed "
  "unknown type is dropped instead of inboxed, so the pre-transition fall-through stops being pinned",
  "            (void)team_key_grant_receive(dec_body, dec_body_len, dec_source_hash, pa.origin);   // emits/pushes its own outcome\n"
  "            become_free(); return;\n"
  "        }",
  "            (void)team_key_grant_receive(dec_body, dec_body_len, dec_source_hash, pa.origin);   // emits/pushes its own outcome\n"
  "            become_free(); return;\n"
  "        }\n"
  "        if (pa.type != 0 && pa.type != DATA_TYPE_INTRO && pa.type != DATA_TYPE_SEALED_RELAY) { become_free(); return; }"),
 # ⓘ A05 ATTACKS THE CONTROL RATHER THAN THE CLAIM, which is what makes §A0-4b a control at all: if the E2E-ack arm
 #   stopped returning, an ack WOULD reach the deliver tail — and §A0-4b is the only case that would notice.
 # ⛔⛔ A06 IS THE CONTROL FOR [[A0-F3]] — THE AMPLIFICATION CLAIM — AND IT EXISTS BECAUSE THE QG A0 REVIEW FOUND
 #     §A0-4c PROVING ITS OWN CONVERSE (blocker 1): the case carried the name "an unknown type carrying
 #     E2E_ACK_REQ still earns an ACK" while its fixture sent WITHOUT the flag and asserted only that no ack
 #     appeared. The case now has a real POSITIVE arm (the flag forged into the DATA in flight), and this entry is
 #     what proves that arm can fail. ★ It gates the reply on a KNOWN type — the tempting "fix" that would make
 #     the amplification quietly disappear and take the finding with it.
 ("A08 ★★★ THE E2E-ACK REPLY IS GATED ON A KNOWN TYPE — an unknown addressed type stops earning an ACK, so the "
  "amplification half of the unknown-type fall-through ([[A0-F3]]) stops being pinned",
  "        if (pa.flags & DATA_FLAG_E2E_ACK_REQ) {",
  "        if ((pa.flags & DATA_FLAG_E2E_ACK_REQ) && pa.type == 0) {"),
 ("A05 ★★★ THE E2E-ACK ARM STOPS CONSUMING — the receipt falls through to the generic deliver, so an ack becomes "
  "an ordinary inbox message and the \"consumed types are not delivered\" control stops being pinned",
  "            MR_EMIT(\"e2e_ack_rx\", EF_I(\"from\", pa.origin), EF_I(\"ctr\", acked));  // KEEP for the sim analyzer (free on metal)\n"
  "            become_free();\n"
  "            return;",
  "            MR_EMIT(\"e2e_ack_rx\", EF_I(\"from\", pa.origin), EF_I(\"ctr\", acked));  // KEEP for the sim analyzer (free on metal)\n"
  "            (void)0;"),
]

MUTS_A0CODEC = [
 # ⛔⛔ THE SUBJECT: `parse_data` copies frame[8] verbatim — no range check, no enum-membership check, no reserved-
 #     value rejection (frame_codec.cpp:959). That total permissiveness is WHY an unknown type reaches the receive
 #     path at all, so §A0-3 pins it directly.
 ("A06 ★★★ parse_data GAINS AN ENUM-RANGE REJECT — every unknown/reserved TYPE byte stops parsing, so the "
  "codec's measured permissiveness (the precondition for the whole fall-through finding) stops being pinned",
  "        o.type      = frame[DATA_HDR_LEN];                        // byte 8",
  "        o.type      = frame[DATA_HDR_LEN]; if (o.type > 19) return std::nullopt;   // byte 8"),
 # ⓘ A07 attacks the OTHER half of §A0-3: that the APP bit is DERIVED and therefore always agrees with the type.
 #   Expect it to redden broadly — every typed frame in the suite loses its TYPE byte. Breadth is not a defect in a
 #   control; being unable to fail would be.
 ("A07 ★★★ THE APP BIT STOPS BEING DERIVED FROM THE TYPE — pack_data emits no TYPE byte, so the flag and the type "
  "can disagree and the \"APP <=> type != 0\" invariant stops being pinned",
  "    const uint8_t flags = static_cast<uint8_t>(in.type != 0 ? (in.flags | DATA_FLAG_APP)",
  "    const uint8_t flags = static_cast<uint8_t>(false ? (in.flags | DATA_FLAG_APP)"),
]

####################################################################################################################
# §CUSTODY-A — the DATA-namespace transition (2026-08-29).
#
# ★★★ WHAT THESE FOUR BATTERIES DEFEND. The slice lands three things a green suite could otherwise be green
#     WITHOUT: the EXACT bounded range predicate, the ONE trait authority (which has no production consumer yet,
#     so the tests are its only caller), and a SEMANTIC store-version bump whose whole job is to make a stale
#     record unreachable. Every one of those is a claim about something NOT happening, which is precisely the
#     shape that rots into decoration if nothing proves it can fail.
####################################################################################################################

MUTS_SLICEACODEC = [
 # ⛔⛔ THE SUBJECT: the design NAMES `t & 0x80` as the wrong form (§5.1) because it admits the reserved
 #     0xC0..0xFD block, the inbox-only 0xFE tombstone and 0xFF as "protocol-internal DATA" — 64 values that no
 #     origination may use and that Slice B's fail-closed internal arm must never adopt. This is that exact
 #     tempting one-liner. It must redden the exhaustive 256-value sweep AND the trait table (the reserved rows
 #     take their traits from this predicate).
 ("S01 ★★★ THE RANGE PREDICATE DEGRADES TO THE HIGH-BIT TEST — 0xC0..0xFF start classifying as protocol-internal, "
  "which is the exact form the design forbids by name",
  "    return t >= data_type_internal_lo && t <= data_type_internal_hi;",
  "    return (t & 0x80) != 0;"),
 # ⓘ S02 attacks the rule the whole namespace hangs on: RESERVING A NUMBER IS NOT KNOWING THE TYPE. Making the
 #   reservation `known` is the single most tempting "tidy-up" a later reader could apply, and it would silently
 #   give an unimplemented app code the behaviour of an implemented one.
 ("S02 ★★★ THE APP_MESSAGE RESERVATION IS TREATED AS KNOWN — an unimplemented application code stops taking the "
  "unknown-application behaviour it is specified to take",
  "        case DATA_TYPE_CHANNEL_POST:\n            return DataTypeTraits{ true,  false, true,  true,  false };",
  "        case DATA_TYPE_CHANNEL_POST:\n        case DATA_TYPE_APP_MESSAGE:\n"
  "            return DataTypeTraits{ true,  false, true,  true,  false };"),
 # ⛔ S03 widens the ONE membership the design pins exactly (§7.1): {E2E_ACK} at Slice A. A second persistent
 #   type would start writing internal control traffic into the user's durable inbox.
 ("S03 ★★★ persistent_outcome WIDENS BEYOND {E2E_ACK} — a team-key grant would start being written to durable "
  "inbox storage, which §7.1 pins as exactly one member at this slice",
  "        case DATA_TYPE_TEAM_KEY_GRANT:\n            return DataTypeTraits{ true,  true,  false, false, false };",
  "        case DATA_TYPE_TEAM_KEY_GRANT:\n            return DataTypeTraits{ true,  true,  false, false, true  };"),
 # ⓘ S04 removes the internal-range FALLBACK, so an unallocated internal value (0x81, 0x87, 0xBF …) would report
 #   itself application-bearing with a full generic send lifecycle — the opposite of the fail-closed direction
 #   Slice B builds on. It is the arm that decides what an UNKNOWN internal type is, and nothing else reads it.
 ("S04 ★★★ THE UNKNOWN-INTERNAL FALLBACK IS DROPPED — an unallocated internal value falls to the reserved row and "
  "stops being classified internal at all, so Slice B would build its fail-closed arm on a lie",
  "    if (data_type_is_internal(t))    return DataTypeTraits{ false, true,  false, false, false };",
  "    if (false)                       return DataTypeTraits{ false, true,  false, false, false };"),
]

MUTS_SLICEAINBOX = [
 # ⛔⛔ THE RUNTIME HALF OF "one renumbered value reverted to its old number". The enum member itself cannot be
 #     reverted (it does not compile — see TARGET_SRC's note), but a CONSUMER can be, and this is the consumer
 #     that decides what byte a durable E2E-ack receipt carries for ever. Reverting it writes the OLD 3 into new
 #     stores, where 3 now means DATA_TYPE_SEALED_RELAY — the precise confusion the v4->v5 wipe exists to make
 #     unreachable, reintroduced from the writing side instead of the reading side.
 ("S05 ★★★ record_ack STAMPS THE OLD ORDINAL 3 — a durable receipt is written with the value that now means "
  "SEALED_RELAY, so every stored ack becomes an application record",
  "/*type*/ DATA_TYPE_E2E_ACK,",
  "/*type*/ 3,"),
]

MUTS_SLICEASTORE = [
 # ⛔⛔ S06 IS THE MIGRATION ITSELF. With the version left at 4, a v4 store's `version_ok` is TRUE, the upgrade
 #     branch never runs, and the stored type-3 receipt survives into the new namespace as a SEALED_RELAY. That
 #     is the §18.1.8 arm, and the CONTROL case beside it proves the hazard is real rather than theoretical.
 ("S06 ★★★ THE SEMANTIC VERSION IS NOT BUMPED — a v4 store mounts as current, so its type-3 E2E receipts survive "
  "and reappear as sealed-relay application records",
  "    static constexpr uint16_t kVersion     = 5;",
  "    static constexpr uint16_t kVersion     = 4;"),
 # ⓘ S07 keeps the version bump but drops its WIPE. The epoch still advances and the companion still re-syncs —
 #   so every count-shaped assertion still passes — while the unreadable old records stay on the medium. It is
 #   the "looks migrated" failure, and only an assertion about the RECORDS can see it.
 ("S07 ★★★ THE UPGRADE STOPS ERASING — the version and epoch move but the v4 records stay on the medium, so the "
  "migration reports success over history it did not remove",
  "        for (uint16_t i = 0; i < ring_segs(); ++i) if (!_records->seg_erase(i)) { _fault = SegMountFault::records_unmountable; return false; }\n"
  "        _meta.version   = kVersion;",
  "        _meta.version   = kVersion;"),
]

MUTS_SLICEAJSON = [
 # ⛔⛔ [[B265]] REINTRODUCED, verbatim in its original form. The companion would render an E2E receipt as
 #   `"type":128` and a sealed-relay record as `"type":"e2e_ack"` — a swap, not a gap. ★ It must ALSO be
 #   rejected by `tools/check_data_type_literals.py`, whose `--selftest` reintroduces this same form: the native
 #   battery proves the BEHAVIOUR can fail, the structural search proves the SHAPE cannot come back unnoticed.
 ("S08 ★★★ THE [[B265]] NUMERIC LITERAL COMES BACK — the companion encoder compares the DM type against 3 again, "
  "which after the transition is SEALED_RELAY and not the receipt",
  "    if (type == MESHROUTE_NS::DATA_TYPE_E2E_ACK) j.lit(\",\\\"type\\\":\\\"e2e_ack\\\"\");",
  "    if (type == 3) j.lit(\",\\\"type\\\":\\\"e2e_ack\\\"\");"),
]


# =========================================================================================================
# §CUSTODY-B — common internal behaviour (2026-08-30). Nine entries, one decision each.
# =========================================================================================================
MUTS_SLICEBMAC = [
 # ★★ B01/B02 ARE THE BRIEF'S "SEPARATE ARMS FOR BOTH HISTORICAL LISTS" (§18.2.7), and they must be separate
 #    because the two halves fail DIFFERENTLY: reverting the CHECK half alone is invisible to a same-type pair
 #    (the STAMP half still exempts, so the floor never arms) and is caught only by §A0-2c's mixed pair;
 #    reverting the STAMP half alone is caught by §A0-2/§A0-2b. One combined entry would let either half hide.
 ("B01 ★★★ THE CHECK HALF REVERTS TO THE HISTORICAL THREE-TYPE LIST — every hash/mobile answer is paced behind "
  "the 3 s USER-DM floor again, which is the §6.2(4) widening undone at the half §A0-2 cannot see",
  "        const bool exempt_type = data_type_traits(pt.type).internal;",
  "        const bool exempt_type = (pt.type == DATA_TYPE_E2E_ACK) || (pt.type == DATA_TYPE_REMOTE_CMD)\n"
  "                              || (pt.type == DATA_TYPE_REMOTE_RESP);"),
 ("B02 ★★★ THE STAMP HALF REVERTS TO THE HISTORICAL THREE-TYPE LIST — an internal origination arms the user-DM "
  "floor again, so the NEXT user DM waits behind a frame the user never sent",
  "        const bool exempt_type = data_type_traits(item.type).internal;",
  "        const bool exempt_type = (item.type == DATA_TYPE_E2E_ACK) || (item.type == DATA_TYPE_REMOTE_CMD)\n"
  "                              || (item.type == DATA_TYPE_REMOTE_RESP);"),
 # ⓘ B03 IS THE TEMPTING WRONG PREDICATE, and design §6.1 names it explicitly: `app_dm` is an ENCODING input,
 #   not a lifecycle question. TEAM_KEY_GRANT is internal AND travels with `app_dm = true`, so substituting one
 #   for the other keeps the generic push on exactly the type §6.2(5) exists to take it off.
 ("B03 ★★★ THE ORIGINATION GATE ASKS `app_dm` INSTEAD OF THE TRAIT — the sealed team-key grant keeps a generic "
  "user-send failure it does not own (design §6.1: app_dm is an encoding input, not a lifecycle authority)",
  "    const bool generic_lifecycle = data_type_traits(type).generic_send_lifecycle;\n"
  "    // R6.1 §6.4 join-participation gate",
  "    const bool generic_lifecycle = app_dm;\n"
  "    // R6.1 §6.4 join-participation gate"),

 ("M04 ★★ THE HELPER CALL IN `do_data_tx`'s PACK-FAILED PATH IS DELETED — the SEVENTH site this sweep found "
  "reverts to destroying an admitted carrier in total silence, grant included ([[B268]] blocker-1)",
  "        terminal_carrier_outcome(pt.type, !pt.has_previous_hop, /*generic_owed=*/false,\n"
  "                                 SendFailReason::none, pt.dst, pt.ctr);\n",
  ""),
]

MUTS_SLICEBRX = [
 ("B04 ★★★ THE FAIL-CLOSED GUARD IS DELETED — an addressed unknown-internal type falls through to the deliver "
  "tail exactly as it did before this slice, i.e. the stray 0x94's 32 raw key bytes become inbox TEXT again "
  "(A0-F10b's \"harmless\" stray, restored verbatim)",
  "        if (data_type_traits(pa.type).internal) {\n"
  "            MR_EMIT(\"unsupported_internal\", EF_I(\"type\", pa.type), EF_I(\"origin\", pa.origin),\n"
  "                    EF_I(\"dst\", pa.dst), EF_I(\"ctr\", pa.ctr));\n"
  "            become_free();\n"
  "            return;\n"
  "        }\n",
  ""),
 # ★★★ B05 IS THE QG-REQUIRED CONTROL. `known` proves ALLOCATION, not that a handler ran.
 ("B05 ★★★ THE GUARD IS WEAKENED TO `internal && !known` — a KNOWN-but-unwired internal type (MOBILE_KEY_FORWARD "
  "on a static/gateway build) walks straight past it into the inbox as text, which is the exact [[B264]] class "
  "the strong predicate exists to close",
  "        if (data_type_traits(pa.type).internal) {",
  "        if (data_type_traits(pa.type).internal && !data_type_traits(pa.type).known) {"),
 # ★★★ B06/B07 ATTACK PLACEMENT, NOT EXISTENCE. Both insert a CORRECTLY-WRITTEN guard at a WRONG branch.
 ("B06 ★★★ A SECOND, CORRECTLY-WRITTEN GUARD IS PLACED **BEFORE** THE HOSTED-MOBILE LAST-MILE FORK — the home is "
  "the outer wire destination but only a PROXY, so it now EATS its own mobile's unknown-internal traffic and the "
  "mobile never gets to judge it. Every \"unknown internal is dropped\" assertion still passes.",
  "        // §mobile 3a: HOST last-mile forward",
  "        if (data_type_traits(pa.type).internal) { become_free(); return; }\n"
  "        // §mobile 3a: HOST last-mile forward"),
 ("B07 ★★★ THE RELAY ARM GAINS THE GUARD — a content-blind forwarder now passes a SEMANTIC verdict on traffic "
  "that is not addressed to it, so an unknown-internal frame dies at the first hop instead of reaching its "
  "destination (design §6.2(3): a relay forwards it normally)",
  "        // C.2 cache-on-pass: a relayed hash-bind answer is cleartext -> snoop the binding before forwarding.",
  "        if (data_type_traits(pa.type).internal) { become_free(); return; }\n"
  "        // C.2 cache-on-pass: a relayed hash-bind answer is cleartext -> snoop the binding before forwarding."),
 # ⓘ B08 IS THE TELEMETRY-BOUND CONTROL under the owner's S0 ruling: the bound is fixed-size and NON-AMPLIFYING,
 #   scalar-only. A body field re-opens the very sink the guard exists to close, on the console and in the NDJSON.
 ("B08 ★★★ THE GUARD'S TELEMETRY GAINS A BODY-DERIVED FIELD — the ruled SCALAR-ONLY bound is broken and the "
  "stray key material reaches the event stream instead of the inbox, which is the same leak one pipe along",
  "            MR_EMIT(\"unsupported_internal\", EF_I(\"type\", pa.type), EF_I(\"origin\", pa.origin),\n"
  "                    EF_I(\"dst\", pa.dst), EF_I(\"ctr\", pa.ctr));",
  "            MR_EMIT(\"unsupported_internal\", EF_I(\"type\", pa.type), EF_I(\"origin\", pa.origin),\n"
  "                    EF_I(\"dst\", pa.dst), EF_I(\"ctr\", pa.ctr), EF_I(\"len\", pa.inner_len));"),
 # ⓘ B09 IS THE OVER-CORRECTION CONTROL (§6.2(6)): suppressing a PROTOCOL-SPECIFIC result would leave an internal
 #   exchange with no outcome at all — the opposite failure from leaving the generic one in place. It applies the
 #   SAME trait gate one line too far, to `send_e2e_acked`, which is exactly how this over-correction would be
 #   written by someone who read §6.2(5) without §6.2(6).
 # ⛔⛔ AN EARLIER B09 GATED `send_acked` ITSELF AND WAS **UNUSABLE** — nothing in the suite pinned that push at
 #    all. Recorded rather than silently replaced: the finding is a REAL coverage gap in the generic family's
 #    POSITIVE arm, and it is closed by §CUSTODY-B/3f (which now asserts an application flight still earns its
 #    `send_acked`) — so the gate this slice added has a control in BOTH directions, not just the suppressing one.
 ("B09 ★★★ THE TRAIT GATE IS APPLIED ONE LINE TOO FAR, TO `send_e2e_acked` — the PROTOCOL-SPECIFIC result of the "
  "one internal type that has one disappears with the generic family (§6.2(6) inverted): a -a send is delivered "
  "and acked end-to-end and the app is never told",
  "            Push pu{}; pu.kind = PushKind::send_e2e_acked; pu.dst = pa.origin; pu.ctr = acked; pu.sender_hash = acker_hash; enqueue_push(pu);",
  "            if (data_type_traits(pa.type).generic_send_lifecycle) { Push pu{}; pu.kind = PushKind::send_e2e_acked; pu.dst = pa.origin; pu.ctr = acked; pu.sender_hash = acker_hash; enqueue_push(pu); }"),

 ("R04 ★★★ THE HELPER CALL IN `handle_nack`'s FULL-QUEUE GIVE-UP IS DELETED — a grant NACKed with no requeue room "
  "dies unreported. ⛔ A DIFFERENT SITE FROM `giveup_flight`, which is why it needs its own arm ([[B268]] blocker-1)",
  "                terminal_carrier_outcome(pt.type, !pt.has_previous_hop, /*generic_owed=*/true,\n"
  "                                         giveup_fail_reason(\"rts_giveup\"), pt.dst, pt.ctr);   // §3-A.5: no_cts\n",
  ""),
]

MUTS_SLICEBCASCADE = [
 # ⓘ B10 IS THE OVER-CORRECTION IN THE OTHER DIRECTION: an APPLICATION envelope losing its user outcome. It is
 #   also the [[B263]] fence's guard — the application arm must stay reproducibly as it is until Slice E.
 # ⛔⛔ B10 / B11 / C03 RETIRED HERE 2026-08-30 BY [[B268]] BLOCKER-1, RECORD KEPT RATHER THAN ENTRIES DELETED.
 #     All three attacked `giveup_flight`'s INLINE gate + grant arm. Those lines no longer exist: QG required ONE
 #     shared post-admission terminal helper, so the trait decision and the grant's replacement moved into
 #     `Node::terminal_carrier_outcome` (node.cpp). ⇒ THE THREE DECISIONS DID NOT LOSE THEIR CONTROLS; THEY MOVED:
 #         B10 (gate narrowed to the untyped DM — app envelopes lose their outcome) -> `sliceBnode` N06
 #         B11 (gate deleted — internal types get the generic push again)           -> `sliceBnode` N07
 #         C03 (the grant's terminal push deleted)                                  -> `sliceBnode` N08
 #     and each CALL SITE gained its own arm (C04..C08 here, N04/N05, H01/H02, R04, M04) — which is the point of
 #     the blocker: one shared arm would have let any single site lose its report with the suite still green.
 ("C04 ★★★ THE HELPER CALL IN `giveup_flight` IS DELETED — the cascade-terminal grant reports nothing and the "
  "§UI-16 panel is stranded on `GRANT QUEUED`; the generic failure disappears for every carrier too",
  "    terminal_carrier_outcome(type, own, /*generic_owed=*/true, reason, dst, ctr);\n",
  ""),
 ("C05 ★★★ THE HELPER CALL IN `defer_send`'s REDRAIN GIVE-UP IS DELETED — a grant that re-drains past its cap "
  "dies unreported",
  "        terminal_carrier_outcome(item.type, !item.is_forward, /*generic_owed=*/true,\n"
  "                                 SendFailReason::no_route, item.dst, item.ctr);\n",
  ""),
 ("C06 ★★★ THE HELPER CALL IN `defer_send`'s CAP REFUSAL IS DELETED — a grant refused by a FULL defer ring dies "
  "unreported",
  "        terminal_carrier_outcome(item.type, !item.is_forward, /*generic_owed=*/true,   // [[B268]] blocker-1\n"
  "                                 SendFailReason::queue_full, item.dst, item.ctr);   // was reason=none -> a reason-LESS send_failed (the emit above is device-stripped, so this Push is the app's only signal)\n",
  ""),
 ("C07 ★★★ THE HELPER CALL IN `try_drain_deferred`'s TTL GIVE-UP IS DELETED — a deferred grant that ages out "
  "dies unreported",
  "            terminal_carrier_outcome(d.item.type, !d.item.is_forward, /*generic_owed=*/true,   // [[B268]] blocker-1\n"
  "                                     SendFailReason::no_route, d.item.dst, d.item.ctr);   // §3-A.5: match the sibling defer_send giveup in defer_send() — was reason=none\n",
  ""),
 ("C08 ★★ THE HELPER CALL IN `gateway_doorstep_hold`'s QUEUE-FULL DROP IS DELETED — the FIFTH site this sweep "
  "found goes back to reporting nothing to anybody, grant included",
  "    else terminal_carrier_outcome(it.type, !it.is_forward, /*generic_owed=*/false,\n"
  "                                  SendFailReason::queue_full, it.dst, it.ctr);\n",
  ""),
]


# =========================================================================================================
# [[B268]] — the team-key grant's OWN outcomes (owner ruling (b), 2026-08-30). The four controls the ruling names.
# =========================================================================================================

MUTS_SLICEBCHANNEL = [
 # ★★★ [[B268]] BLOCKER-1: the two REPROVISION-PURGE carrier deaths, one arm each.
 ("H01 ★★★ THE HELPER CALL IN THE QUEUED-CARRIER PURGE IS DELETED — a reprovision with a grant sitting in the TX "
  "queue strands the panel, and every queued application carrier loses its `send_failed`",
  "                if (all) terminal_carrier_outcome(it.type, !it.is_forward,\n"
  "                                                  carrier_owes_send_failed(it.is_channel_m, it.is_forward),\n"
  "                                                  SendFailReason::reprovisioned, it.dst, it.ctr);\n",
  ""),
 ("H02 ★★★ THE HELPER CALL IN THE IN-FLIGHT PURGE IS DELETED — a reprovision while the grant is ON THE AIR "
  "strands the panel; the generic twin goes with it",
  "            if (all) terminal_carrier_outcome(L._pending_tx->type, !L._pending_tx->has_previous_hop,   // [[B268]] blocker-1\n"
  "                                              carrier_owes_send_failed(L._pending_tx->m_broadcast,\n"
  "                                                                       L._pending_tx->has_previous_hop),\n"
  "                                              SendFailReason::reprovisioned, L._pending_tx->dst, L._pending_tx->ctr);\n",
  ""),
]

MUTS_SLICEBNODE = [
 ("N01 ★★★ THE GRANT'S AIRED PUSH IS DELETED — §UI-16's panel can never leave `GRANT QUEUED`, because the generic "
  "`send_aired` it used to wait for is suppressed for a protocol-internal type and nothing replaces it. This is "
  "[[B268]] VERBATIM, and it is the defect the whole ruling exists to close",
  "        if (pt.type == DATA_TYPE_TEAM_KEY_GRANT && !pt.m_broadcast && !pt.has_previous_hop) {\n"
  "            Push g{}; g.kind = PushKind::team_key_grant_aired; g.dst = pt.dst; g.ctr = pt.ctr; enqueue_push(g);\n"
  "        }\n",
  ""),
 ("N02 ★★★ THE CORRELATION IS DROPPED — the aired push carries dst 0 instead of the flight's own, so the verdict "
  "either never promotes or promotes on a WRONG-FLIGHT match. `{dst, ctr}` is the whole reason a re-DAD inside the "
  "grant window cannot strand or misattribute the screen",
  "            Push g{}; g.kind = PushKind::team_key_grant_aired; g.dst = pt.dst; g.ctr = pt.ctr; enqueue_push(g);",
  "            Push g{}; g.kind = PushKind::team_key_grant_aired; g.dst = 0; g.ctr = pt.ctr; enqueue_push(g);"),
 # ⓘ N03 IS THE OVER-CORRECTION IN THE OPPOSITE DIRECTION: the grant falls back into the GENERIC family, which is
 #   precisely what §6.2(5) forbids and what the owner rejected option (a) for.
 ("N03 ★★★ THE SUPPRESSION IS DISABLED FOR EVERY TYPE — the grant takes the GENERIC `send_aired` path again, so a "
  "protocol-internal frame re-acquires a user-send outcome (§6.2(5) undone) AND its own kind is never minted",
  "    if (!data_type_traits(pt.type).generic_send_lifecycle) {",
  "    if (false) {"),

 ("N04 ★★★ THE HELPER CALL IN THE REPROVISION DEFERRED-PURGE IS DELETED — a `join`/`leave` with a deferred grant "
  "strands the panel, and every deferred APPLICATION carrier loses its `send_failed` too ([[B268]] blocker-1)",
  "            terminal_carrier_outcome(it.type, !it.is_forward,\n"
  "                                     carrier_owes_send_failed(it.is_channel_m, it.is_forward),\n"
  "                                     SendFailReason::reprovisioned, it.dst, it.ctr);\n",
  ""),
 ("N05 ★★ THE HELPER CALL IN THE LBT/STASH DATA GIVE-UP IS DELETED — the SIXTH site this sweep found reverts to "
  "releasing a stranded flight in total silence, grant included ([[B268]] blocker-1)",
  "            terminal_carrier_outcome(_active->_pending_tx->type, !_active->_pending_tx->has_previous_hop,\n"
  "                                     /*generic_owed=*/false, SendFailReason::none,\n"
  "                                     _active->_pending_tx->dst, _active->_pending_tx->ctr);\n",
  ""),

 # ★★★ THE THREE HELPER-INTERNAL ARMS, inherited from the retired sliceBcascade B10/B11/C03 (see the note there).
 ("N06 ★★★ THE HELPER'S GENERIC ARM NARROWS TO THE UNTYPED DM — every APPLICATION ENVELOPE (INTRO / MOBILE_SEND / "
  "SEALED_RELAY) loses its user-send failure at EVERY one of the eleven terminal sites at once, which is §6.4 inverted",
  "    if (generic_owed && data_type_traits(type).generic_send_lifecycle) push_send_failed(reason, dst, ctr);",
  "    if (generic_owed && type == 0) push_send_failed(reason, dst, ctr);"),
 ("N07 ★★★ THE HELPER'S TRAIT TERM IS DROPPED — a cascade-exhausted E2E ack / hash answer pushes `send_failed` "
  "under ITS dst and ITS ctr again, into the ring the companion correlates by ctr ([[B59]]'s exact shape)",
  "    if (generic_owed && data_type_traits(type).generic_send_lifecycle) push_send_failed(reason, dst, ctr);",
  "    if (generic_owed) push_send_failed(reason, dst, ctr);"),
 ("N08 ★★★ THE HELPER'S GRANT ARM IS DELETED — every one of the eleven post-admission carrier deaths stops "
  "reporting the grant at once: the §UI-16 panel is stranded on `GRANT QUEUED` whatever kills the flight",
  "    if (own_origination && type == DATA_TYPE_TEAM_KEY_GRANT) {\n"
  "        Push g{}; g.kind = PushKind::team_key_grant_failed; g.reason = reason; g.dst = dst; g.ctr = ctr;\n"
  "        enqueue_push(g);\n"
  "    }\n",
  ""),
]

MUTS_BY_TARGET = {"a0rx": MUTS_A0RX, "a0codec": MUTS_A0CODEC,
                  "sliceAcodec": MUTS_SLICEACODEC, "sliceAinbox": MUTS_SLICEAINBOX,
                  "sliceAstore": MUTS_SLICEASTORE, "sliceAjson": MUTS_SLICEAJSON,
                  "sliceBmac": MUTS_SLICEBMAC, "sliceBrx": MUTS_SLICEBRX,
                  "sliceBcascade": MUTS_SLICEBCASCADE, "sliceBnode": MUTS_SLICEBNODE,
                  "sliceBchannel": MUTS_SLICEBCHANNEL,
                  "b134seam": MUTS_B134SEAM, "b134nvs": MUTS_B134NVS, "b260": MUTS_B260,
                  "b134store": MUTS_B134STORE, "b134inbox": MUTS_B134INBOX,
                  "b134ram": MUTS_B134RAM, "b134ack": MUTS_B134ACK,
                  "b20mac": MUTS_B20MAC, "b20codec": MUTS_B20CODEC,
                  "teamgrant": MUTS_TEAMGRANT, "grantadmit": MUTS_GRANTADMIT, "grantpark": MUTS_GRANTPARK,
                  "b161hash": MUTS_B161HASH, "b161rx": MUTS_B161RX, "b161mac": MUTS_B161MAC,
                  "b251rx": MUTS_B251RX, "b251hash": MUTS_B251HASH,
                  "b159const": MUTS_B159CONST, "b159dl": MUTS_B159DL,
                  "b159mac": MUTS_B159MAC, "b159rx": MUTS_B159RX,
                  "b159hal": MUTS_B159HAL, "b159map": MUTS_B159MAP,
                  "uipresets": MUTS_UIPRESETS, "uipresetverbs": MUTS_UIPRESETVERBS,
                  "model": MUTS_MODEL, "config": MUTS_CONFIG, "chrome": MUTS_CHROME, "icons": MUTS_ICONS,
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
# ★ THE SELECTION IS CARRIED AS **INDICES INTO `MUTS`**, not as labels, because the parallel layer has to hand a shard
#   to another process and then prove that the verdicts that came back are about the entries it asked for. An index is
#   the only handle both sides can check against the same table (the parent re-checks each returned label against
#   `MUTS[idx][0]` — see the merge), and it is what makes a MISSING entry nameable rather than merely uncounted.
_SEL_IDX = [_i for _i, (_l, _p, _r) in enumerate(MUTS) if not only or _l.startswith(only)]
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
    print(f"workers    {_WORKERS} (default {_WORKERS_DEFAULT} = min(usable cores {_usable_cores()} - 2, 6))")
    sys.exit(0)


# ===== ★★★ THE ORCHESTRATOR — IT NEVER TOUCHES THE REPOSITORY ======================================================
# ⛔⛔ EVERYTHING IN THIS BLOCK IS READ-ONLY WITH RESPECT TO `ROOT`. It takes md5s, it rsyncs OUT of the tree, and it
#    reads worker output. It takes NO source lock and NO build lock (there is nothing of the real tree left to
#    serialise against — two orchestrations can now run side by side, which is exactly what the old shared
#    `.pio/build/native` made impossible), it arms NO backup, and it starts NO build. The only thing it asserts about
#    the real tree is that it is UNCHANGED, twice: once between launch and the shard copies, and once at the end.
_SCRATCH_ROOT = None
_WORKER_PROCS = []
_WORKER_SIDECARS = []       # the /tmp keyed dirs+locks the workers will create; see `_worker_sidecar_paths`


def _worker_sidecar_paths(tree):
    """The two keyed paths a worker in `tree` will create OUTSIDE its tree, computed by the SAME rules the worker
    uses (`_BK_DIR` / `_BUILD_LOCK` above).

    ⛔ THEY MUST BE CLEANED UP OR EVERY RUN LITTERS: the sidecar backup and the build lock are keyed by a hash of a
    RESOLVED PATH, and a scratch tree's path is unique per run — so N workers leave N dead `mr_*-<key>` entries in
    the temp dir every time, keyed to trees that no longer exist. (Measured before this was added: 54 of them.) ⓘ
    They are derived, not discovered: this removes only the exact paths OUR trees map to, never a glob that could
    reach another session's live backup.
    """
    src_key = hashlib.sha1(str(Path(os.path.join(tree, TARGET_SRC[_TARGET])).resolve()).encode()).hexdigest()[:16]
    bld_key = hashlib.sha1(str(Path(os.path.join(tree, ".pio/build/native")).resolve()).encode()).hexdigest()[:16]
    return (os.path.join(tempfile.gettempdir(), f"mr_ui7d_mutation_backup-{src_key}"),
            os.path.join(tempfile.gettempdir(), f"mr_mutation_buildlock-{bld_key}"))


def _cleanup_scratch():
    """Remove the scratch trees and the keyed sidecars their workers made.

    ⓘ `MR_MUT_KEEP_SCRATCH=1` keeps the trees — a crashed worker's tree still holds its installed mutant and its
    build directory, which is the only post-mortem material a deleted tree would destroy. The sidecars are kept with
    them, because a tree without its INFLIGHT marker is half a post-mortem.
    """
    global _SCRATCH_ROOT
    d, _SCRATCH_ROOT = _SCRATCH_ROOT, None
    if not d:
        return
    if os.environ.get("MR_MUT_KEEP_SCRATCH"):
        print(f"  scratch KEPT (MR_MUT_KEEP_SCRATCH=1): {d}", flush=True)
        return
    shutil.rmtree(d, ignore_errors=True)
    for bk_dir, bld_lock in _WORKER_SIDECARS:
        shutil.rmtree(bk_dir, ignore_errors=True)
        try:
            os.remove(bld_lock)
        except OSError:
            pass


def _kill_workers():
    for p in _WORKER_PROCS:
        if p.poll() is None:
            try:
                p.kill()
            except OSError:
                pass


def _real_fingerprint():
    """md5 of EVERY target file this runner knows how to mutate — not merely the one selected.

    ⚠ Deliberately wider than the run: the promise being kept is "this tool did not write your tree", and a promise
    scoped to the one file the run happened to select is a promise about the incident rather than the invariant.
    """
    return {rel: _md5(os.path.join(ROOT, rel))
            for rel in sorted(set(TARGET_SRC.values())) if os.path.exists(os.path.join(ROOT, rel))}


def orchestrate():
    global _SCRATCH_ROOT, _WORKERS
    t0 = time.time()
    me = os.path.abspath(__file__)
    rel_me = os.path.relpath(me, ROOT)
    if shutil.which("rsync") is None:
        print("  ABORT `rsync` is not on PATH — the scratch trees cannot be made, so nothing was measured.")
        print("  ⛔ There is no fallback on purpose: a half-copied tree would build and report verdicts about a tree")
        print("     that is not yours. Install rsync (it is the only external tool this runner needs).")
        sys.exit(9)

    # ⓘ Never more workers than entries: an empty shard is a scratch tree rsynced, a `.pio` allocated and a cold
    #   native build paid for nothing.
    _WORKERS = min(_WORKERS, len(_SEL_IDX))
    print(f"-- target {_TARGET}: {Path(H).resolve()} ({len(MUTS)} entries)", flush=True)
    print(f"-- selection: {_SCOPE_NOW}", flush=True)
    print(f"-- workers: {_WORKERS}"
          f"{'' if _WORKERS != _WORKERS_DEFAULT else f' (default = min(usable cores {_usable_cores()} - 2, 6))'}"
          f"{'   [SERIAL REFERENCE PATH]' if _WORKERS == 1 else ''}", flush=True)

    # ★★ THE INFLIGHT MARKER'S JOB, ADAPTED. In the serial tool the marker answered "is the file in the tree still
    #    mine?" across a build. Nothing of ours is in the tree any more, so the question it now answers is the only
    #    one left that can invalidate a run: DID THE REAL TREE MOVE BETWEEN THE LAUNCH AND THE COPIES? A shard cut
    #    from a source the operator was mid-edit on measures a file nobody has.
    fp_launch = _real_fingerprint()

    _SCRATCH_ROOT = tempfile.mkdtemp(prefix="mr_mutation_run-",
                                     dir=os.environ.get("MR_MUT_SCRATCH") or None)
    atexit.register(_cleanup_scratch)
    print(f"-- scratch root: {_SCRATCH_ROOT}   (removed on exit; MR_MUT_KEEP_SCRATCH=1 to keep)", flush=True)

    trees = []
    t_copy = time.time()
    for w in range(_WORKERS):
        tree = os.path.join(_SCRATCH_ROOT, f"w{w}")
        # ⛔ rsync OF THE WORKING TREE, never `git archive`: this repository is DIRTY BY DESIGN — an uncommitted
        #   slice is the normal state here, and a battery run against `HEAD` would measure code nobody wrote yet.
        #   `.git` and `.pio` are excluded: the first is 100x the payload and no build reads it (the native env has
        #   no `git_rev.py`), the second is a 140 MB build directory each worker must own a FRESH copy of anyway.
        r = subprocess.run(["rsync", "-a", "--delete", "--exclude=.git", "--exclude=.pio", ROOT + "/", tree + "/"],
                           capture_output=True, text=True)
        if r.returncode != 0:
            print(f"  ABORT rsync of the working tree into {tree} failed (rc {r.returncode}):")
            print("        " + (r.stderr.strip().splitlines() or ["(no stderr)"])[-1][:200])
            sys.exit(9)
        # ⛔ AND THE COPY OF THIS FILE MUST BE BYTE-IDENTICAL, checked rather than assumed: the worker judges entries
        #   by ITS OWN `MUTS` table, so a scratch tree carrying a different revision of this script would return
        #   verdicts about entries the parent cannot name. (It cannot happen today — the copy is seconds old — which
        #   is exactly when a guard is cheap to install and impossible to argue about later.)
        if _md5(os.path.join(tree, rel_me)) != _md5(me):
            print(f"  ABORT the copy of {rel_me} in {tree} is not byte-identical to the running script.")
            sys.exit(9)
        _WORKER_SIDECARS.append(_worker_sidecar_paths(tree))
        trees.append(tree)
    copy_s = time.time() - t_copy

    fp_now = _real_fingerprint()
    if fp_now != fp_launch:
        # ⛔ REFUSE TO START — before a single build. This is the [[B217]] rule at the other end of the run: a battery
        #   whose baseline was cut from a moving tree is not a weaker measurement, it is a measurement OF NOTHING.
        print("  ABORT the real tree changed between launch and the shard copies — refused before any build.")
        for k in sorted(set(fp_launch) | set(fp_now)):
            if fp_launch.get(k) != fp_now.get(k):
                print(f"        {k}: {fp_launch.get(k, '(absent)')} -> {fp_now.get(k, '(absent)')}")
        print("  The shards would have been cut from different revisions of the tree. Nothing was measured.")
        sys.exit(9)

    # ★ ROUND-ROBIN, not contiguous blocks: a VACUOUS entry costs no build at all while a compiling one costs a
    #   full native build, and contiguous blocks would hand one worker a run of cheap entries and another a run of
    #   expensive ones. Round-robin needs no cost model to stay balanced.
    shards = [[] for _ in range(_WORKERS)]
    for n, idx in enumerate(_SEL_IDX):
        shards[n % _WORKERS].append(idx)

    results, streams = [None] * _WORKERS, [[] for _ in range(_WORKERS)]

    def _pump(w, proc):
        for line in proc.stdout:
            line = line.rstrip("\n")
            streams[w].append(line)
            print(f"  [w{w}] {line}", flush=True)

    threads = []
    t_run = time.time()
    for w, tree in enumerate(trees):
        if not shards[w]:
            continue                       # ⓘ fewer selected entries than workers — an empty shard is not launched
        res_path = os.path.join(_SCRATCH_ROOT, f"result-w{w}.json")
        cmd = [sys.executable, os.path.join(tree, rel_me), f"--target={_TARGET}",
               f"--shard-id={w}", f"--shard-entries={','.join(str(i) for i in shards[w])}",
               f"--shard-result={res_path}"]
        # ★ BATTERY-PRIVATE COMPILER CACHE (owner-ruled 2026-08-24 — see the header's ⛔⛔ COMPILER CACHE block):
        #   a persistent cache of the batteries' OWN (⛔ never ~/.ccache: NOHASHDIR-relativized objects must not
        #   leak into the owner's real builds' debug info), with CCACHE_BASEDIR = this worker's tree so paths
        #   relativize and CCACHE_NOHASHDIR so `-g` stops hashing the per-tree build dir. Measured: 524/524
        #   cross-tree hits, a warm 10-entry repeat 75.8 s -> 11.5 s, verdicts byte-identical.
        wenv = dict(os.environ,
                    CCACHE_DIR=os.path.expanduser("~/.cache/meshroute-battery-ccache"),
                    CCACHE_BASEDIR=tree, CCACHE_NOHASHDIR="1")
        p = subprocess.Popen(cmd, cwd=tree, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             text=True, bufsize=1, env=wenv)
        _WORKER_PROCS.append(p)
        results[w] = {"proc": p, "path": res_path, "tree": tree, "assigned": shards[w]}
        t = threading.Thread(target=_pump, args=(w, p), daemon=True)
        t.start()
        threads.append(t)

    for w, r in enumerate(results):
        if r:
            r["rc"] = r["proc"].wait()
    for t in threads:
        t.join()
    run_s = time.time() - t_run

    # ===== THE MERGE, AND ITS ONE RULE: EVERY INDEX HANDED OUT COMES BACK NAMED ====================================
    print("", flush=True)
    print("=" * 118, flush=True)
    print(f"-- MERGED REPORT — target {_TARGET}: {Path(H).resolve()}", flush=True)

    integrity = []                          # ⛔ anything in here means the run cannot be read as a verdict at all
    verdicts, bases = {}, []
    for w, r in enumerate(results):
        if not r:
            continue
        try:
            with open(r["path"]) as f:
                r["json"] = json.load(f)
        except (OSError, ValueError) as e:
            r["json"] = None
            integrity.append(f"worker {w} left no readable result file ({e}); it exited {r['rc']}")
            continue
        j = r["json"]
        if j.get("base"):
            bases.append((w, tuple(j["base"])))
        for ent in j.get("entries", []):
            i = ent["idx"]
            if not (0 <= i < len(MUTS)) or MUTS[i][0] != ent["label"]:
                here = repr(MUTS[i][0]) if 0 <= i < len(MUTS) else "(index out of range)"
                integrity.append(f"worker {w} reported index {i} as {ent['label']!r}, this table has {here}")
                continue
            if i in verdicts:
                integrity.append(f"index {i} ({MUTS[i][0]}) was reported twice")
            verdicts[i] = ent
        # ⛔ 0 = every entry RED, 1 = at least one worthless entry. ANY other code is an abort (2..8) or a death, and
        #   both mean this shard's entries were not all judged — see the missing-index sweep below, which is what
        #   actually names them. This line exists so the reader is told WHY they are missing.
        if r["rc"] not in (0, 1):
            integrity.append(f"worker {w} exited {r['rc']} — not a verdict code (0 = all RED, 1 = something "
                             f"worthless); its shard was not completed")
        if not j.get("restored"):
            integrity.append(f"worker {w} did not report its source restored")

    missing = [i for i in _SEL_IDX if i not in verdicts]

    if bases:
        agreed = {b for _, b in bases}
        if len(agreed) != 1:
            integrity.append("the workers derived DIFFERENT clean baselines from copies of one tree: "
                             + "; ".join(f"w{w} {b[0]} / {b[1]} / {b[2]}" for w, b in bases))
        b0 = bases[0][1]
        print(f"  ok   clean baseline {b0[0]} / {b0[1]} / {b0[2]}   (DERIVED per worker tree; "
              f"{len(bases)} tree(s) agree)", flush=True)
    else:
        b0 = None
        integrity.append("no worker derived a clean baseline — nothing measured a clean tree")

    # ★★ THE CROSS-CHECK PIN, ONCE, AGAINST THE MERGED (agreed) BASELINE — warn-only, per [[B217]]: a moved figure is
    #    bookkeeping and must never zero a run, so it does not touch the exit code.
    pin_stale = None
    if b0 and (PIN_CASES, PIN_ASSERTS) != (b0[0], b0[1]):
        pin_stale = ((PIN_CASES, PIN_ASSERTS), (b0[0], b0[1]))
        print("  " + "!" * 112, flush=True)
        print("  !!  STALE CROSS-CHECK PIN — the hand-pinned figure is NOT what these clean trees run.", flush=True)
        print(f"  !!      pinned   {PIN_CASES} / {PIN_ASSERTS}"
              f"{'   (from MR_MUT_BASE)' if os.environ.get('MR_MUT_BASE') else '   (PIN_CASES, this file)'}",
              flush=True)
        print(f"  !!      derived  {b0[0]} / {b0[1]}   <-- THE BATTERY USED THIS", flush=True)
        print(f"  !!  The battery RAN anyway, {_SCOPE_NOW}: per [[B217]] a stale figure must NEVER zero a run.",
              flush=True)
        print("  !!  Then re-pin PIN_CASES / PIN_ASSERTS with the derivation (or unset MR_MUT_BASE).", flush=True)
        print("  " + "!" * 112, flush=True)

    ok = bad = 0
    for i in _SEL_IDX:
        ent = verdicts.get(i)
        if ent is None:
            # ⛔⛔ THE [[B227]]/[[B237]] LINE. A shard that died mid-entry loses THIS entry and no other, and the one
            #    thing that must never happen is for it to be absent from the report and therefore read as fine.
            print(f"  MISSING {MUTS[i][0]} — NO VERDICT (its worker did not report it)", flush=True)
            bad += 1
            continue
        print(ent["line"], flush=True)
        for x in ent.get("extra", []):
            print(x, flush=True)
        if ent["verdict"] == "RED":
            ok += 1
        else:
            bad += 1

    for w, r in enumerate(results):
        if not r:
            continue
        j = r["json"] or {}
        print(f"  worker {w}: {len(j.get('entries', []))}/{len(r['assigned'])} entries, "
              f"{sum(1 for e in j.get('entries', []) if e['verdict'] == 'RED')} RED / "
              f"{sum(1 for e in j.get('entries', []) if e['verdict'] != 'RED')} worthless, "
              f"wall {j.get('elapsed', 0.0):.1f}s, rc {r['rc']}, "
              f"source restored: md5 {j.get('md5_after', '(none)')} "
              f"({'MATCHES' if j.get('restored') else 'DIFFERS — FAIL'})   {r['tree']}", flush=True)

    # ★★ WHAT REPLACES THE EXIT-GATE REBUILD (defect 3). The old gate rebuilt the REAL tree to prove the restoration
    #    was effective at the BUILD level, because the real tree's binary was the last mutant's. This runner never
    #    built the real tree, so there is no binary to refresh and no restoration to prove — what is worth proving is
    #    the stronger statement the new shape allows: the tree is BYTE-IDENTICAL to how we found it.
    fp_end = _real_fingerprint()
    if fp_end != fp_launch:
        print("  ⛔ FAIL the real tree is NOT byte-identical to launch:", flush=True)
        for k in sorted(set(fp_launch) | set(fp_end)):
            if fp_launch.get(k) != fp_end.get(k):
                print(f"        {k}: {fp_launch.get(k, '(absent)')} -> {fp_end.get(k, '(absent)')}", flush=True)
        integrity.append("the real tree's target files moved during the run")
    else:
        print(f"real tree untouched: all {len(fp_launch)} target files byte-identical to launch (md5); "
              f"no build ran in {ROOT}", flush=True)

    print(f"timing: copy {copy_s:.1f}s + battery {run_s:.1f}s = {time.time() - t0:.1f}s total, "
          f"{len(_SEL_IDX)} entries over {sum(1 for r in results if r)} worker(s)", flush=True)
    # ⚠ THE SHAPE IS THE SERIAL RUNNER'S, AND THE PARENTHETICAL IS NOT DECORATION: a MISSING entry counts toward
    #   `unusable` (it is certainly not RED), but "worthless mutation" and "never judged" are different facts about
    #   the tree and a reader who cannot tell them apart from this line has been laundered to ([[B237]]).
    print(f"mutations: {ok} RED / {bad} unusable"
          f"{f' (of which {len(missing)} MISSING — never judged)' if missing else ''}", flush=True)

    if integrity:
        print("  " + "#" * 112, flush=True)
        print("  ##  ⛔ RUN INTEGRITY FAILURE — this run's verdicts may NOT be read as a battery result.", flush=True)
        for m in integrity:
            print(f"  ##    {m}", flush=True)
        if missing:
            print(f"  ##    {len(missing)} selected entr{'y' if len(missing) == 1 else 'ies'} came back with NO "
                  f"verdict: {', '.join(MUTS[i][0].split()[0] for i in missing)}", flush=True)
        print("  ##  A shard that did not report is NOT a shard that passed ([[B227]]/[[B237]]).", flush=True)
        print("  " + "#" * 112, flush=True)
    if pin_stale:
        print("  " + "!" * 112)
        print(f"  !!  REMINDER — the cross-check pin is STALE: pinned {pin_stale[0][0]} / {pin_stale[0][1]}, these "
              f"trees run {pin_stale[1][0]} / {pin_stale[1][1]}.")
        print(f"  !!  The battery above {_SCOPE_PAST} on the derived figure ([[B217]]); re-pin PIN_CASES/PIN_ASSERTS.")
        print("  " + "!" * 112)

    # ⛔ 9 OUTRANKS 1: "some entry is worthless" and "this run cannot be trusted" are different answers, and folding
    #   the second into the first is the laundering [[B237]] closed on the sibling harness.
    # ★ BUT AN ABORT EVERY WORKER AGREED ON KEEPS ITS OWN CODE. A RED clean tree (2), a held lock (3), a foreign edit
    #   (5) — these are the serial runner's documented answers, and they describe the TREE, which every worker copied
    #   from the same place. Reporting them as a generic 9 would tell a reader "the harness broke" about a condition
    #   the harness diagnosed correctly. ⓘ Only when they ALL agree: a mixed set really is a harness problem.
    codes = {r["rc"] for r in results if r}
    if len(codes) == 1 and codes <= {2, 3, 4, 5}:
        sys.exit(codes.pop())
    if integrity:
        sys.exit(9)
    sys.exit(0 if bad == 0 else 1)


if not _IS_WORKER:
    try:
        orchestrate()
    except KeyboardInterrupt:
        print("\n  INTERRUPTED — killing the workers and removing the scratch trees. The real tree was never "
              "written to by this run; its targets are unchanged.", flush=True)
        _kill_workers()
        _cleanup_scratch()
        sys.exit(130)

# ===== ★ FROM HERE DOWN: THE WORKER — the unchanged serial runner, in a scratch tree it owns outright ==============
# ⓘ `ROOT` is this scratch tree (it derives from `__file__`), so `_BK_DIR`, `_BK_LOCK` and `_BUILD_LOCK` — all keyed by
#   a resolved path — are private to this worker by construction. THAT is why N workers need no new locking: the
#   guards were already keyed to the resource, and the resource is now per-worker.
_SEL_IDX = _SHARD_ENTRIES
for _i in _SEL_IDX:
    if not 0 <= _i < len(MUTS):
        _refuse_argv(f"--shard-entries names index {_i}, outside the {len(MUTS)}-entry '{_TARGET}' table",
                     "A shard index this table cannot resolve is a verdict about nothing.")
_shard = {"shard": _SHARD_ID, "target": _TARGET, "root": ROOT, "base": None,
          "entries": [], "restored": False, "md5_after": None, "elapsed": 0.0}
_shard_t0 = time.time()


def _shard_flush():
    """Write the result file after EVERY entry, atomically.

    ⛔ THE POINT IS THE CRASH, not tidiness: a worker killed mid-entry must still have told the parent about the
    entries it DID judge, so the parent's missing-index sweep names exactly one entry rather than the whole shard.
    `os.replace` so the parent can never read a half-written file.
    """
    _shard["elapsed"] = time.time() - _shard_t0
    tmp = _SHARD_RESULT + ".tmp"
    with open(tmp, "w") as f:
        json.dump(_shard, f)
    os.replace(tmp, _SHARD_RESULT)


_shard_flush()

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
    print(f"-- target {_TARGET}: {Path(H).resolve()} ({len(_SEL_IDX)} of {len(MUTS)} entries in this shard)",
          flush=True)
    print("-- baseline gate: THIS WORKER'S OWN clean tree must run 0 FAILED; its own counts become this shard's "
          "baseline", flush=True)
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
    print(f"  ok   clean baseline {base_cases} / {base_asserts} / {base_failed}   (DERIVED from this tree)",
          flush=True)
    # ⓘ THE CROSS-CHECK PIN IS **NOT** JUDGED HERE ANY MORE — the parent judges it ONCE, against the baseline the
    #   worker trees agreed on, and prints the [[B217]] banner at both ends of the merged report. N workers printing
    #   N identical banners is not louder, it is noise; and a per-shard banner could not say `_SCOPE_NOW` truthfully
    #   (this process ran a shard, not the selection). The derived figure travels in the result file instead.
    _shard["base"] = [base_cases, base_asserts, base_failed]
    _shard_flush()

    for _i in _SEL_IDX:
        label, pat, rep = MUTS[_i]
        # ★ Each entry's verdict is recorded as the EXACT LINE the serial runner printed, so the parent's merged
        #   report is the same report — reassembled in table order — rather than a paraphrase of it.
        def _verdict(kind, line, extra=()):
            print(line, flush=True)
            for _x in extra:
                print(_x, flush=True)
            _shard["entries"].append({"idx": _i, "label": label, "verdict": kind, "line": line,
                                      "extra": list(extra)})
            _shard_flush()

        hits = orig.count(pat)
        if hits != 1:
            _verdict("VACUOUS", f"  VACUOUS {label} — match count {hits} (must be exactly 1)"); bad += 1; continue
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
            _err = [f"        {line[:160]}" for line in out.splitlines() if "error:" in line][:1]
            _verdict("UNUSABLE", f"  UNUSABLE {label} — the mutant does not compile / did not run", _err)
            bad += 1; continue
        failed, cases, asserts = res
        if failed > 0:
            _verdict("RED", f"  ok   {label} -> RED ({failed} assertion(s) failed, match count 1)"); ok += 1
        else:
            _verdict("FAIL", f"  FAIL {label} — the suite still PASSES; nothing measures this"); bad += 1
except KeyboardInterrupt:
    # A bare traceback here would bury the one fact a reader needs — WAS THE SOURCE PUT BACK? — so say it plainly.
    # ⚠ CORRECTED: this comment used to claim *"the `finally` below has already run by the time this prints"*. IT HAS
    #   NOT — Python runs this `except` body FIRST and the `finally` only afterwards, which is exactly why the explicit
    #   `restore()` below is here and not redundant: it is what makes the md5 line printed at the end of this block TRUE
    #   at the moment it is printed. `restore()` is idempotent (it no-ops when the md5 already matches), so the `finally`
    #   running second changes nothing. ⓘ Same family as a comment asserting a `-Wswitch` guard the code did not have.
    print("\n  INTERRUPTED — restoring this shard's source now, before this exits", flush=True)
    restore("SIGINT")
    disarm()
    _shard["md5_after"] = md5(H)
    _shard["restored"] = (md5(H) == base_md5)
    _shard_flush()          # ⛔ the entries judged so far still get reported; the parent names the rest as MISSING
    print(f"source restored: md5 {md5(H)} ({'MATCHES' if md5(H) == base_md5 else 'DIFFERS — FAIL'})")
    sys.exit(130)
finally:
    # ⛔ THE ONE THING THIS BLOCK EXISTS FOR: the real source goes back whatever happened above.
    restore("finally")
    disarm()

after = md5(H)
_shard["md5_after"] = after
_shard["restored"] = (after == base_md5)
_shard_flush()
print(f"source restored: md5 {after} ({'MATCHES' if after == base_md5 else 'DIFFERS — FAIL'})")

# ★★★ THE EXIT-GATE REBUILD IS **GONE, NOT MOVED**, AND THAT IS THE POINT OF THE SCRATCH-TREE SHAPE (2026-08-24).
#     ⚠ WHAT IT USED TO SAY, kept because the reasoning is the record: *"restoring the source is not the whole job —
#       the binary left in `.pio/build/native/` is the LAST MUTANT'S, so anyone running `./.pio/build/native/program`
#       immediately after this tool saw a spurious failure on a correct tree (observed; it fooled its own author
#       once)"*. ⇒ it rebuilt once on the restored source and re-required the derived baseline.
#     ⛔ THAT DEFECT NO LONGER EXISTS TO GUARD AGAINST. It was never about the source; it was about a MUTANT ARTEFACT
#       LEFT WHERE SOMEBODY WOULD RUN IT. The only tree this runner builds is a scratch copy that is deleted moments
#       from now and whose binary no human or gate will ever execute — while `ROOT`'s own `.pio/build/native` is
#       never written by this runner at all, so it can no longer be a mutant's. A rebuild here would prove a
#       restoration nothing depends on, at the cost of one full native build PER WORKER.
#     ★ WHAT PROVES THE RESTORATION NOW, and it is strictly stronger than a rebuild ever was: (1) the md5 line above
#       — the file is byte-identical to the one this shard started from, and a build is a pure function of its
#       sources, so a re-derivation of the same counts could not tell us anything the hash has not; and (2) in the
#       PARENT, the real tree's every target file re-hashed against its launch fingerprint. The old gate proved a
#       copy was clean; the new one proves the ORIGINAL was never touched.
print(f"shard {_SHARD_ID}: {ok} RED / {bad} unusable   (of {len(_SEL_IDX)} entries)")
sys.exit(0 if bad == 0 and after == base_md5 else 1)
