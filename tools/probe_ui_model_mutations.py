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
BASE_CASES, BASE_ASSERTS = 1680, 83432   # ★★ RE-PINNED 2026-08-16 by §CHROME-4: 1678 / 83346 -> **1680 / 83432**
                                         # (+2 cases / +86 assertions: `chrome4-audit:` — design §7.3's audit of
                                         # every PURE panel string against the rail's 19-column body, including
                                         # §7.1 rule 6's preset-collision check — and `chrome-nav:`'s pin on the
                                         # rail enumerator order the renderer indexes by. The remaining assertions
                                         # are the RE-DERIVED inbox-detail geometry: 21 -> 19 columns, 42 -> 38
                                         # characters a page, 6 -> 7 pages.)
                                         # ---- §CHROME-3's derivation, kept as history ----
                                         # ★★ RE-PINNED 2026-08-16 by §CHROME-3: 1673 / 83284 -> **1678 / 83346**
                                         # (+5 cases / +62 assertions, ALL in `test/test_firmware_ui_chrome.cpp`'s
                                         # new `chrome-invalidate:` group — §8.3's rule, including the case that
                                         # pins §8.3.1's WITHDRAWN instruction as NOT implemented).
                                         # ---- §CHROME-1's derivation, kept as history ----
                                         # ★★ RE-PINNED 2026-08-16 by §CHROME-1: 1651 / 82867 -> 1671 / 83252
                                         # (round 1: +20 cases / +385 assertions, ALL in the new
                                         # `test/test_firmware_ui_chrome.cpp`) -> **1673 / 83284** (round 2's QG
                                         # corrections: the compose/inbox-detail PRECEDENCE case split out with its
                                         # own control, one REAL model-driven outcome transition, the geometric
                                         # battery bound, and the two vacuous 17-iteration outcome loops REPLACED by
                                         # a cross product that can actually come out differently).
                                         # ⛔⛔ AND THE PIN WAS ALREADY STALE WHEN THIS SLICE FOUND IT, which is
                                         # worth recording rather than quietly overwriting: it still read
                                         # 1615 / 82362 while HEAD `b8929e5` measured 1651 / 82867 — i.e. §T3 and
                                         # the §B200 arc added 36 cases / 505 assertions without re-pinning, so
                                         # ANY run of this tool in between ABORTED at the baseline gate and
                                         # measured NOTHING. ⓘ That is the gate working (it refused rather than
                                         # reporting successes), but a gate nobody can pass is a battery nobody
                                         # runs. The 1615 -> 1651 step is NOT attributed here; only 1651 -> 1671 is.
                                         # ---- the pre-§CHROME-1 derivation, kept as history ----
                                         # 2026-08-13 §notify-every-save ([[B194]]): +2 cases / +23 assertions — the
                                         # `leave` SHAPE (all four covered fields reset to 0 under an open draft ->
                                         # conflict + SAVE refused) and its NEGATIVE half (a `join`-shaped write moves
                                         # no covered field and must raise NOTHING, which is what makes "notify on
                                         # EVERY user-initiated save" self-limiting rather than merely loud).
                                         # 2026-08-13 §UI-14 follow-up: 1610 / 82310 -> 1613 / 82339 for the IMMEDIATE
                                         # external-write notification (spec §3.6.1) — incl. the change->REVERT->SAVE
                                         # case the save-time byte comparison structurally cannot catch.
                                         # 2026-08-13 §UI-14: 1581 / 81943 -> +29 cases / +367 assertions, all in
                                         # test/test_firmware_ui_model.cpp (the SETTINGS screen, its marker and the
                                         # save/discard/reboot states) — 2 of those assertions are the CYCLE update
                                         # existing cases needed, the rest are new. ⓘ Before that: 1552 / 81563 ->
                                         # 1581 / 81943 was §UI-13's. The pin is the WHOLE suite's, so BOTH targets
                                         # move together when either adds cases — that is why it is one constant and
                                         # not a per-target one.
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
 ("M50 a value row can be edited while the service is not open",
  "if (_cfg && _cfg->is_open()) { _st.settings = Settings::editing; _st.dirty = true; }",
  "{ _st.settings = Settings::editing; _st.dirty = true; }"),
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

MUTS_BY_TARGET = {"model": MUTS_MODEL, "config": MUTS_CONFIG, "chrome": MUTS_CHROME, "icons": MUTS_ICONS}
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
