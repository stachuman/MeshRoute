#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""The NATIVE mutation battery for the PURE `src/` units — §UI-7D slice B's `firmware_ui_model.h` and §UI-13's
`firmware_config_service.h`.

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
}
_flags = [a for a in sys.argv[1:] if a.startswith("--")]
_TARGET = "model"
for _f in _flags:
    if _f.startswith("--target="):
        _TARGET = _f.split("=", 1)[1]
if _TARGET not in TARGET_SRC:
    print(f"  ABORT unknown --target={_TARGET}; known targets: {', '.join(sorted(TARGET_SRC))}")
    sys.exit(6)
H = os.path.join(ROOT, TARGET_SRC[_TARGET])

# ★★ THE PINNED CLEAN BASELINE, AND IT IS A GATE RATHER THAN A COMMENT. Every entry below is judged by "the suite went
#    RED"; on a tree that is ALREADY red that verdict is meaningless and this runner would report 32 successes having
#    measured nothing — the instrument-that-cannot-fail shape, in the tool built to prevent it. ⇒ the clean tree is run
#    FIRST and must produce EXACTLY these figures, or the run ABORTS before a single mutation is applied.
#    ⓘ Override deliberately (a slice that legitimately adds cases): MR_MUT_BASE="cases,asserts".
BASE_CASES, BASE_ASSERTS = 1843, 87045   # ★★ RE-PINNED 2026-08-20 by [[B232]] (the SETTINGS single entry):
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
    BASE_CASES, BASE_ASSERTS = (int(x) for x in os.environ["MR_MUT_BASE"].split(","))

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
 ("M06 a row with seq 0 is selected anyway",
  "&& s.inbox[_st.cursor].seq != 0) {", ") {"),
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
 ("M19 the modal has no inactivity timeout",
  "if (_st.detail != InboxModal::closed && elapsed(s.now_ms, _last_input_ms) >= kBlankMs) close_detail();",
  ";"),
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
 ("M34 the SETTINGS screen is not list-aware (one press leaves it)",
  "if (_st.screen == Screen::settings) return settings_row_list(s).n;",
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
  "        provision_reset_on_leave(_st.provisioning, _st.prov_confirm);",
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
  "        _st.prov_answer = UiProvAnswer{};\n"
  "        _st.dirty = true;\n"
  "    }",
  "        _st.dirty = true;\n"
  "    }"),
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
 ("M98 [[B232]] the closed view reports the MENU's length, so `short` walks rows that are not on the panel",
  "if (_st.screen == Screen::settings && _st.settings == Settings::closed) return 1;",
  ";"),
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
]

MUTS_BY_TARGET = {"model": MUTS_MODEL, "config": MUTS_CONFIG, "chrome": MUTS_CHROME, "icons": MUTS_ICONS,
                  "joinprofiles": MUTS_JOINPROFILES, "devicenv": MUTS_DEVICENV, "cfgparse": MUTS_CFGPARSE,
                  "uiprov": MUTS_UIPROV, "uijoin": MUTS_UIJOIN, "provservice": MUTS_PROVSERVICE}
MUTS = MUTS_BY_TARGET[_TARGET]

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
    # (failed assertions, total cases, total assertions) — the last two are what the baseline gate checks.
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
# ⚠ The label filter is POSITIONAL and the flags are not: `--target=` must not be mistaken for an entry prefix (it
#   would match nothing and the run would silently measure ZERO mutations while reporting success).
_positional = [a for a in sys.argv[1:] if not a.startswith("--")]
only = _positional[0] if _positional else None

try:
    # ★★ THE BASELINE GATE. ⛔ Nothing below may run on a tree whose clean suite is not exactly green: a RED verdict is
    #    only evidence if GREEN was the alternative.
    print(f"-- target {_TARGET}: {Path(H).resolve()} ({len(MUTS)} entries)", flush=True)
    print(f"-- baseline gate: the CLEAN tree must be exactly {BASE_CASES} / {BASE_ASSERTS} / 0", flush=True)
    base, base_out = run_suite()
    if base is None:
        print("  ABORT the clean tree does not build / did not run — no mutation was applied")
        for line in base_out.splitlines():
            if "error:" in line:
                print("        " + line[:160]); break
        sys.exit(2)
    base_failed, base_cases, base_asserts = base
    if (base_failed, base_cases, base_asserts) != (0, BASE_CASES, BASE_ASSERTS):
        print(f"  ABORT the clean baseline is {base_cases} / {base_asserts} / {base_failed}, expected "
              f"{BASE_CASES} / {BASE_ASSERTS} / 0 — every mutation would read RED for the wrong reason. "
              f"No mutation was applied.")
        sys.exit(2)
    print(f"  ok   clean baseline {base_cases} / {base_asserts} / {base_failed}", flush=True)

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
#    restored source and require the pinned baseline again. That refreshes the artefact AND proves the restoration is
#    effective at the BUILD level, not merely at the file level.
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
sys.exit(0 if bad == 0 and after == base_md5 and exit_ok else 1)
