#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""§remote-admin v2 SLICE 1b — THE FIRST-CONSUMER OWNERSHIP CONTRACT for `MR_FEAT_RADMIN_CLIENT/ACCEPT`.

★★★ WHY THIS FILE EXISTS, AND WHAT IT REPLACES. Slice 1 landed the capability pair with NO consumer, and pinned
    that fact in two places — `run.sh`'s S3 (*"exactly ONE production location names the pair"*) and
    `tools/test_probe_features.py::test_the_pair_has_no_consumer`. Slice 1b IS the first consumer, so those two
    pins are now false. ⛔ THE ANSWER IS NOT TO DELETE THE CENSUS. A pin that says "nobody uses this" and a pin
    that says "exactly these sites use it, each under exactly its own capability" are the same instrument aimed one
    slice further on; deleting it would retire the only thing that can see an unapproved consumer appearing.

WHAT IS CHECKED, said plainly (each check reads the REAL production sources — there is no policy model here that
could drift from them; the only thing written down is the APPROVED CENSUS, which is the reviewed contract itself):

  · O1  which FILES name the pair in CODE, across all of `lib/`, `src/` and `test/`;
  · O2  which file DEFINES it (exactly one) and how many times;
  · O3  `node.h` names it only in declaration guards, never in an expression;
  · O4  the EXACT site census of the consuming TU, as normalized text — an extra site inside an ALLOWED file is
        rejected just as loudly as a new file;
  · O5  NO LEGACY WIDENING: no directive anywhere combines a RADMIN capability with `MR_FEAT_REMOTE_MGMT` by
        `||`/`&&`. The single `!=` agreement pin in `mr_features.h` is the one permitted co-occurrence (R-RA-26),
        and it is permitted BY NAME rather than by a loose pattern;
  · O6  each owned symbol is compiled under its OWN capability and never under the other one (accept ⇒ command,
        client ⇒ response), and the shared staging helper under exactly `ACCEPT || CLIENT`;
  · O7  the real router CALLS the pure decision, with the two macros in the declared argument order, and SELECTS
        on the decision's result rather than re-testing the type — a correctly written helper that the router
        bypasses must fail;
  · O8  each owned arm actually CONSUMES (`become_free();` + `return;`);
  · O9  there is NO `none` arm: an unowned type must reach the existing fail-closed guard, not a new early return;
  · O10 tests never become production owners;
  · O11 the pure decision itself is capability-AGNOSTIC — declared `static` and compiled on every build;
  · O12 no `#ifndef` override surface for the pair anywhere in `lib/`, `src/`, `test/`.

⛔ WHAT THIS IS NOT. It is a SOURCE contract. It does not execute a receive path and it does not prove a board
   compiled anything out: the native drive (`test/test_node_r3.cpp` §radmin-1b/1..5) and the per-board
   preprocessing evidence are separate and remain mandatory. A source check alone is not an RX drive.

⛔ IT NEVER WRITES THE REPOSITORY. `--controls` snapshots the scanned sources into a temporary directory and edits
   THAT; the shared checkout is hashed before and after every run and a difference is a hard failure.

USAGE:  ownership.py --root <repo>              # the checks (default)
        ownership.py --root <repo> --controls   # the negative controls + the controls-of-the-controls
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import sys
import tempfile
from pathlib import Path

SCAN_DIRS = ("lib", "src", "test")
SCAN_EXT = (".h", ".hpp", ".c", ".cc", ".cpp", ".inc")

HDR = "lib/core/mr_features.h"
NODE_H = "lib/core/node.h"
RX = "lib/core/node_mac_rx.cpp"
# ★★ §RADMIN SLICE 3 (2026-09-06) — THE FIRST `src/` CONSUMERS. Slice 1/1b's owners were all in `lib/core`; the
#    target-store slice adds the local USB surface, which necessarily lives in the firmware glue: the console
#    bindings + the router arm, the boot-wrapper declaration, the BLE refusal + the boot call, and the two help
#    names. ⇒ the approved census grows from THREE files to SEVEN, and each new site is spelled out below.
# ⛔ THE PURE SERVICE HEADERS ARE **NOT** HERE, AND THEIR ABSENCE IS THE POINT ([[B255]] idiom):
#    `src/firmware_admin_identity.h`, `firmware_admin_acl.h` and `firmware_admin_verbs.h` carry ⛔ NO capability
#    macro at all, so the native suite drives every service arm without defining a product role. If one of them
#    ever names `MR_FEAT_RADMIN_*`, check O1 REJECTS it as an unapproved consumer — which is exactly the guard
#    that keeps the gating at the instantiation instead of leaking into the policy.
# ★★ §RADMIN SLICE 5 (2026-09-07) — THE CENSUS GROWS FROM SEVEN FILES TO NINE. The target's session state is a
#    `Node` member and its expiry is a `Node` timer case, so `lib/core/node.cpp` becomes an owner; the legacy
#    staging slot became CLIENT-only, so `lib/core/node_mac.cpp`'s `take_remote_inbound` guard does too.
# ⛔ AND TWO NEW PRODUCTION FILES ARE **DELIBERATELY ABSENT**, which is the [[B255]] idiom holding rather than a
#    gap: `lib/core/remote_session.{h,cpp}` (the whole session/admission/bootstrap implementation) and
#    `src/firmware_admin_runtime.h` (the prepare/commit/discard seam) carry ⛔ NO `MR_FEAT_RADMIN_*` reference at
#    all, so the native suite drives every arm of both without declaring a product role. Check O1 REJECTS either
#    of them the moment one names the pair — controls W-S5-GATE-PURE-SESSION / -RUNTIME prove exactly that.
NODE_CPP = "lib/core/node.cpp"
MAC_CPP = "lib/core/node_mac.cpp"
CMDS_CPP = "src/firmware_commands.cpp"
CMDS_H = "src/firmware_commands.h"
FW_MAIN = "src/fw_main.cpp"
HELP_H = "src/firmware_help.h"

# ★★★ THE APPROVED CENSUS — the reviewed contract, spelled out. Normalization: comments removed, string literals
#     masked, runs of whitespace collapsed. Compared as a MULTISET per file, so a duplicated site is caught too.
APPROVED_SITES = {
    HDR: [
        "# define MR_FEAT_RADMIN_CLIENT 1",          # MR_PROFILE_MOBILE  -> {1, 0}
        "# define MR_FEAT_RADMIN_ACCEPT 0",
        "# define MR_FEAT_RADMIN_CLIENT 0",          # defined(ARDUINO)   -> {0, 1}
        "# define MR_FEAT_RADMIN_ACCEPT 1",
        "# define MR_FEAT_RADMIN_CLIENT 1",          # HOST (native, lus) -> {1, 1}
        "# define MR_FEAT_RADMIN_ACCEPT 1",
        "# if MR_FEAT_RADMIN_CLIENT && MR_FEAT_RADMIN_ACCEPT",       # R-RA-17 board exclusivity, both
        "# if !MR_FEAT_RADMIN_CLIENT && !MR_FEAT_RADMIN_ACCEPT",     # R-RA-17 board exclusivity, neither
        "# if MR_FEAT_RADMIN_ACCEPT != MR_FEAT_REMOTE_MGMT",         # the legacy AGREEMENT pin (not a widening)
    ],
    NODE_H: [
        "#if MR_FEAT_RADMIN_ACCEPT",   # §radmin-5: the firmware-installed session API (commit/draw/readiness)
        "#if MR_FEAT_RADMIN_ACCEPT",   # rx_remote_cmd_accept + the four private radmin_* halves of the arm
        "#if MR_FEAT_RADMIN_CLIENT",   # rx_remote_resp_client declaration
        # ⛔⛔ CORRECTED 2026-09-07 BY SLICE 5, the old entry kept visible: this row read
        #    "#if MR_FEAT_RADMIN_ACCEPT || MR_FEAT_RADMIN_CLIENT"  # the shared staging helper declaration.
        #    The ACCEPT owner no longer stages anything legacy (R-RA-22's partitioned admission replaced it), so
        #    the helper is CLIENT-ONLY. ⇒ the multiset changes without the COUNT changing, which is exactly the
        #    class of drift this per-file sorted comparison exists to catch.
        "#if MR_FEAT_RADMIN_CLIENT",   # the staging helper declaration — CLIENT-only since slice 5
        "#if MR_FEAT_RADMIN_CLIENT",   # §radmin-5: the legacy `_remote_inbound` SLOT — CLIENT-only storage
        "#if MR_FEAT_RADMIN_ACCEPT",   # §radmin-5: the ONE `_radmin_session` state block (2 064 B, accept-only)
    ],
    NODE_CPP: [
        "#if MR_FEAT_RADMIN_ACCEPT",   # §radmin-5: the ONE shared expiry timer case in `on_timer`
        "#if MR_FEAT_RADMIN_ACCEPT",   # §radmin-5: the checked epoch draw + commit/entropy-failed install seam
    ],
    MAC_CPP: [
        "#if MR_FEAT_RADMIN_CLIENT",   # §radmin-5: `take_remote_inbound`'s real body (⛔ `#else` = a false stub)
    ],
    RX: [
        "#if MR_FEAT_RADMIN_CLIENT",   # the staging helper DEFINITION — CLIENT-only since slice 5 (was ACCEPT||CLIENT)
        "#if MR_FEAT_RADMIN_ACCEPT",   # the accept-owned v2 admission (rx_remote_cmd_accept + its three halves)
        "#if MR_FEAT_RADMIN_CLIENT",   # rx_remote_resp_client definition
        "const RadminRxOwner radmin_owner = radmin_rx_owner(pa.type, "
        "MR_FEAT_RADMIN_CLIENT, MR_FEAT_RADMIN_ACCEPT);",     # ★ THE ONE production call, argument order pinned
        "#if MR_FEAT_RADMIN_ACCEPT",                          # the accept-owned dispatch arm
        "#if MR_FEAT_RADMIN_CLIENT",                          # the client-owned dispatch arm
    ],
    # ---- §RADMIN slice 3: the target-store console surface, ACCEPT-only (R-RA-8) ------------------------------
    # ---- §RADMIN slice 3: the target-store console surface, ACCEPT-only (R-RA-8) ------------------------------
    # ---- §RADMIN slice 4: the controller-store console surface, CLIENT-only (R-RA-8's other half) -------------
    # ★★ THE TWO ROLES ARE INTERLEAVED PER FILE AND THE MULTISET IS EXACT: swapping a CLIENT gate for an ACCEPT
    #    one (or the reverse) changes NO count but changes the multiset, and the check below compares the SORTED
    #    LIST rather than a length. That is the whole reason this census exists rather than a `grep -c`.
    CMDS_CPP: [
        "#if MR_FEAT_RADMIN_ACCEPT",   # the store/draw/sink bindings, the three entry points and the router arm
        "#if MR_FEAT_RADMIN_ACCEPT",   # 7b-2: scalar status snapshot and five ordered counters, no CLIENT surface
        "#if MR_FEAT_RADMIN_ACCEPT",   # the ONE dispatch forwarding arm
        "#if MR_FEAT_RADMIN_CLIENT",   # the store/draw/use/sink bindings, the resident book, the entry points
        "#if MR_FEAT_RADMIN_CLIENT",   # do_regen()'s ADMISSION predicate, before any draw/write/identity change
        "#if MR_FEAT_RADMIN_CLIENT",   # do_regen()'s WARNING, after the complete success line
        "#if MR_FEAT_RADMIN_CLIENT",   # the ONE dispatch forwarding arm
    ],
    CMDS_H: [
        "#if MR_FEAT_RADMIN_ACCEPT",   # the boot wrapper's declaration. ⛔ NO `#else` stub: the call site is gated
        "#if MR_FEAT_RADMIN_CLIENT",   # the controller boot wrapper's declaration. ⛔ NO `#else` stub either
    ],
    FW_MAIN: [
        "#if MR_FEAT_RADMIN_ACCEPT",   # R-RA-29's BLE refusal, BEFORE the transport-neutral seam
        "#if MR_FEAT_RADMIN_ACCEPT",   # setup()'s boot report + §radmin-5's LIVE INSTALL, beside the legacy admin_load
        "#if MR_FEAT_RADMIN_ACCEPT",   # §radmin-7a: the post-init `> remote-activation` boot line (B363)
        "#if MR_FEAT_RADMIN_ACCEPT",   # §radmin-7b-1: main-loop executor service (no CLIENT instance)
        "#if MR_FEAT_RADMIN_CLIENT",   # R-RA-30's SUB-VERB-AWARE BLE refusal, also before the seam
        "#if MR_FEAT_RADMIN_CLIENT",   # setup()'s READ-ONLY controller boot report call
        "#if MR_FEAT_RADMIN_CLIENT",   # §radmin-5: the WHOLE legacy rcmd drain block, statics included
    ],
    HELP_H: [
        "#if MR_FEAT_RADMIN_ACCEPT",   # the two sorted primary names in the bare index
        "#if MR_FEAT_RADMIN_CLIENT",   # the two sorted CONTROLLER primary names, contiguous with them
    ],
}
APPROVED_FILES = sorted(APPROVED_SITES)
# One stable letter per approved file, in `APPROVED_FILES` order — the O4x check ids.
CHECK_SUFFIX = "abcdefghijklmnopqrstuvwxyz"
assert len(APPROVED_FILES) <= len(CHECK_SUFFIX), "ownership.py: more approved files than check-id letters"
# The ONE co-occurrence of a RADMIN capability with the legacy switch that is NOT a widening (R-RA-26).
LEGACY_AGREEMENT_PIN = "# if MR_FEAT_RADMIN_ACCEPT != MR_FEAT_REMOTE_MGMT"
DECISION_CALL = ("const RadminRxOwner radmin_owner = radmin_rx_owner(pa.type, "
                 "MR_FEAT_RADMIN_CLIENT, MR_FEAT_RADMIN_ACCEPT);")
DECISION_DECL = "static RadminRxOwner radmin_rx_owner(uint8_t type, bool client_on, bool accept_on);"
DECISION_DEF = "Node::RadminRxOwner Node::radmin_rx_owner(uint8_t type, bool client_on, bool accept_on)"
OWNED = (("rx_remote_cmd_accept", "MR_FEAT_RADMIN_ACCEPT", "MR_FEAT_RADMIN_CLIENT"),
         ("rx_remote_resp_client", "MR_FEAT_RADMIN_CLIENT", "MR_FEAT_RADMIN_ACCEPT"))
# ⛔⛔ CORRECTED 2026-09-07 BY SLICE 5, the old value kept visible: this was
#    `"MR_FEAT_RADMIN_ACCEPT || MR_FEAT_RADMIN_CLIENT"` and the check below called it "the SHARED staging helper".
#    It is not shared any more — the ACCEPT owner runs the v2 admission and stages nothing legacy — so the guard
#    is the CLIENT capability alone. ⛔ Do NOT restore the disjunction: it would re-admit an accept-side legacy
#    consumer that R-RA-27 handed to this slice to remove.
STAGING_HELPER_GUARD = "MR_FEAT_RADMIN_CLIENT"


class GateError(RuntimeError):
    """A malformed/unreadable source or a broken instrument. ⛔ NEVER a passed check and never a passed control."""


# ---------------------------------------------------------------------------------------------------------------
# source normalization
# ---------------------------------------------------------------------------------------------------------------
def strip_comments_and_strings(text: str) -> str:
    """Remove // and /* */ comments and MASK string/char literal contents, PRESERVING every newline.

    Line structure is preserved because the preprocessor-condition tracker below counts on line numbers, and
    because a census that shifted lines could not report where it found something.
    """
    out: list[str] = []
    i, n, state = 0, len(text), 0          # 0 = code, 1 = "..", 2 = '..'
    while i < n:
        c = text[i]
        if state == 0:
            if c == "/" and i + 1 < n and text[i + 1] == "/":
                while i < n and text[i] != "\n":
                    out.append(" ")
                    i += 1
                continue
            if c == "/" and i + 1 < n and text[i + 1] == "*":
                out.append("  ")
                i += 2
                while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                    out.append("\n" if text[i] == "\n" else " ")
                    i += 1
                if i >= n:
                    raise GateError("unterminated /* */ comment")
                out.append("  ")
                i += 2
                continue
            if c in "\"'":
                state = 1 if c == '"' else 2
            out.append(c)
            i += 1
        else:
            close = '"' if state == 1 else "'"
            if c == "\\" and i + 1 < n:
                out.append(" " if text[i + 1] != "\n" else "\n")
                out.append(" ")
                i += 2
                continue
            if c == close:
                out.append(c)
                state = 0
            else:
                out.append("\n" if c == "\n" else " ")   # ⛔ the literal's CONTENT is masked, not read
            i += 1
    return "".join(out)


def norm(line: str) -> str:
    return " ".join(line.split())


def read_code(root: Path, rel: str) -> list[str]:
    p = root / rel
    try:
        raw = p.read_text(encoding="utf-8")
    except OSError as e:
        raise GateError(f"cannot read {rel}: {e}") from e
    except UnicodeDecodeError as e:
        raise GateError(f"{rel} is not UTF-8: {e}") from e
    return strip_comments_and_strings(raw).splitlines()


def scan_files(root: Path) -> list[str]:
    out: list[str] = []
    for d in SCAN_DIRS:
        base = root / d
        if not base.is_dir():
            raise GateError(f"scan directory {d}/ is missing under {root}")
        for p in sorted(base.rglob("*")):
            if p.is_file() and p.suffix in SCAN_EXT:
                out.append(str(p.relative_to(root)))
    if not out:
        raise GateError("no sources found to scan")
    return out


def cond_stack_per_line(lines: list[str]) -> list[list[str]]:
    """The RAW `#if`/`#elif` conditions active on each line (a taken-branch approximation, `#else` marked)."""
    stacks: list[list[str]] = []
    stack: list[str] = []
    for ln in lines:
        t = ln.strip()
        if t.startswith("#"):
            body = t[1:].strip()
            kw = body.split(None, 1)[0] if body else ""
            arg = body[len(kw):].strip()
            if kw in ("if", "ifdef", "ifndef"):
                stack.append(arg if kw == "if" else f"{kw} {arg}")
                stacks.append(list(stack))
                continue
            if kw == "elif":
                if not stack:
                    raise GateError("#elif without #if")
                stack[-1] = arg
                stacks.append(list(stack))
                continue
            if kw == "else":
                if not stack:
                    raise GateError("#else without #if")
                stack[-1] = "!(" + stack[-1] + ")"
                stacks.append(list(stack))
                continue
            if kw == "endif":
                if not stack:
                    raise GateError("#endif without #if")
                stacks.append(list(stack))
                stack.pop()
                continue
        stacks.append(list(stack))
    if stack:
        raise GateError(f"unbalanced preprocessor conditionals ({len(stack)} unclosed)")
    return stacks


# ---------------------------------------------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------------------------------------------
def run_checks(root: Path) -> list[tuple[str, bool, str]]:
    res: list[tuple[str, bool, str]] = []

    def ok(cid, msg):
        res.append((cid, True, msg))

    def bad(cid, msg):
        res.append((cid, False, msg))

    files = scan_files(root)
    code = {rel: read_code(root, rel) for rel in files}
    named = sorted(rel for rel, lines in code.items() if any("MR_FEAT_RADMIN" in l for l in lines))

    # O1 — the FILE census.
    if named == APPROVED_FILES:
        ok("O1", f"the pair is named in CODE by exactly the {len(APPROVED_FILES)} approved files: " + ", ".join(named))
    else:
        bad("O1", f"file census moved: {named} != approved {APPROVED_FILES}")

    # O2 — exactly one DEFINER, with the derived arm count.
    definers = {rel: [norm(l) for l in lines if norm(l).startswith("# define MR_FEAT_RADMIN")]
                for rel, lines in code.items()}
    definers = {k: v for k, v in definers.items() if v}
    if list(definers) == [HDR] and len(definers[HDR]) == 6:
        ok("O2", f"{HDR} is the sole definer, with 6 `#define`s (3 derivation arms x 2 capabilities)")
    else:
        bad("O2", f"definer census moved: { {k: len(v) for k, v in definers.items()} } != {{{HDR}: 6}}")

    # O3 — node.h names the pair only in declaration guards.
    nh = [norm(l) for l in code[NODE_H] if "MR_FEAT_RADMIN" in l]
    if nh and all(l.startswith("#if") for l in nh):
        ok("O3", f"{NODE_H} names the pair only on {len(nh)} `#if` declaration guards, never in an expression")
    else:
        bad("O3", f"{NODE_H} names the pair outside a declaration guard: {[l for l in nh if not l.startswith('#if')]}")

    # O4 — the EXACT site census per approved file (multiset).
    for rel in APPROVED_FILES:
        got = sorted(norm(l) for l in code[rel] if "MR_FEAT_RADMIN" in norm(l))
        want = sorted(APPROVED_SITES[rel])
        # ★ THE CHECK ID IS DERIVED FROM THE FILE'S POSITION IN THE APPROVED LIST, ⛔ no longer a hand-kept map of
        #   three names: §RADMIN slice 3 took the census from three files to seven, and a literal map is exactly
        #   the shape that raises a `KeyError` — i.e. an INSTRUMENT CRASH — the day a reviewed file is added.
        #   `APPROVED_FILES` is `sorted(APPROVED_SITES)`, so the suffix is stable for a given census.
        cid = "O4" + CHECK_SUFFIX[APPROVED_FILES.index(rel)]
        if got == want:
            ok(cid, f"{rel}: all {len(got)} naming sites match the approved census exactly")
        else:
            extra = [x for x in got if got.count(x) > want.count(x) or x not in want]
            miss = [x for x in want if x not in got or want.count(x) > got.count(x)]
            bad(cid, f"{rel}: site census moved — unapproved/extra {sorted(set(extra))}, missing {sorted(set(miss))}")

    # O5 — NO LEGACY WIDENING.
    widened = []
    for rel, lines in code.items():
        for i, l in enumerate(lines, 1):
            t = norm(l)
            if not t.startswith("#"):
                continue
            if "MR_FEAT_RADMIN" in t and "MR_FEAT_REMOTE_MGMT" in t and t != LEGACY_AGREEMENT_PIN:
                widened.append(f"{rel}:{i} {t}")
    if not widened:
        ok("O5", "no directive widens a RADMIN capability with MR_FEAT_REMOTE_MGMT "
                 "(the one `!=` agreement pin in mr_features.h is permitted BY NAME)")
    else:
        bad("O5", "legacy widening found: " + " | ".join(widened))

    # O6 — each owned symbol under its OWN capability, and never under the other one.
    for sym, own, other in OWNED:
        offenders = []
        seen = 0
        for rel in (NODE_H, RX):
            lines, stacks = code[rel], cond_stack_per_line(code[rel])
            for i, l in enumerate(lines):
                if sym not in l:
                    continue
                seen += 1
                conds = [norm(c) for c in stacks[i]]
                if own not in conds:
                    offenders.append(f"{rel}:{i+1} not under `{own}` (active: {conds})")
                if other in conds:
                    offenders.append(f"{rel}:{i+1} compiled under the WRONG capability `{other}`")
        cid = "O6a" if own == "MR_FEAT_RADMIN_ACCEPT" else "O6b"
        if seen and not offenders:
            ok(cid, f"all {seen} occurrences of `{sym}` compile under `{own}` and never under `{other}`")
        elif not seen:
            bad(cid, f"`{sym}` does not occur at all — the owned entry point is gone")
        else:
            bad(cid, f"`{sym}`: " + " | ".join(offenders))
    off = []
    seen = 0
    for rel in (NODE_H, RX):
        lines, stacks = code[rel], cond_stack_per_line(code[rel])
        for i, l in enumerate(lines):
            if "remote_inbound_stage" not in l:
                continue
            conds = [norm(c) for c in stacks[i]]
            if STAGING_HELPER_GUARD in conds:      # the declaration/definition sites
                seen += 1
            elif not ({"MR_FEAT_RADMIN_ACCEPT", "MR_FEAT_RADMIN_CLIENT"} & set(conds)):
                off.append(f"{rel}:{i+1} ungated (active: {conds})")
    # ⛔⛔ THE EXPECTED COUNT WENT 2 -> 3 IN SLICE 5, AND THE CHECK GOT STRONGER RATHER THAN LOOSER. Under the
    #    old `ACCEPT || CLIENT` guard only the DECLARATION and the DEFINITION matched it; the two CALL sites sat
    #    one level deeper, under their own owners, and were merely "not ungated". Now that the helper's guard IS
    #    `MR_FEAT_RADMIN_CLIENT`, its one surviving CALL site matches too — so the count 3 asserts exactly what
    #    Slice 5 removed: declaration + definition + THE SINGLE CLIENT CALLER, and ⛔ no accept-side caller.
    #    A restored `remote_inbound_stage(pa, ui, false)` inside the accept arm would make it 4 and fail here.
    if seen == 3 and not off:
        ok("O6c", f"the legacy staging helper is declared, defined and called under exactly "
                  f"`{STAGING_HELPER_GUARD}` — 3 sites, ⛔ CLIENT-only since slice 5, and ⛔ never the old "
                  f"ACCEPT||CLIENT disjunction that let the accept arm stage a legacy body")
    else:
        bad("O6c", f"legacy staging helper guard moved: {seen} guarded site(s) (want 3); {off}")

    # O7 — the router CALLS the decision, argument order pinned, and SELECTS on its result.
    rx = code[RX]
    calls = [norm(l) for l in rx if "radmin_rx_owner(" in norm(l) and "Node::radmin_rx_owner" not in norm(l)]
    if calls == [DECISION_CALL]:
        ok("O7a", "the production router makes exactly ONE call to the pure decision, with "
                  "(pa.type, MR_FEAT_RADMIN_CLIENT, MR_FEAT_RADMIN_ACCEPT) in the declared order")
    else:
        bad("O7a", f"the production decision call moved/was bypassed: {calls} != [{DECISION_CALL!r}]")
    arms = [norm(l) for l in rx if "RadminRxOwner::command_accept)" in norm(l)
            or "RadminRxOwner::response_client)" in norm(l)]
    want_arms = ["if (radmin_owner == RadminRxOwner::command_accept) {",
                 "if (radmin_owner == RadminRxOwner::response_client) {"]
    if arms == want_arms:
        ok("O7b", "both dispatch arms select on the DECISION'S RESULT, not on the raw type byte")
    else:
        bad("O7b", f"dispatch arms bypass the decision: {arms} != {want_arms}")

    # O8 — each owned arm consumes.
    body = "\n".join(rx)
    consumed = 0
    for sym in ("rx_remote_cmd_accept(pa,", "rx_remote_resp_client(pa,"):
        j = body.find(sym + " ui ? &*ui : nullptr);")
        if j < 0:
            continue
        tail = norm(body[j:j + 400].replace("\n", " "))
        if "become_free(); return; }" in tail:
            consumed += 1
    if consumed == 2:
        ok("O8", "both owned arms CONSUME the frame (`become_free(); return;` immediately after the owner call)")
    else:
        bad("O8", f"only {consumed}/2 owned arms consume the frame")

    # O9 — no `none` arm in the dispatch: the unowned type must reach the EXISTING fail-closed guard.
    # ⛔ A `none` ARM IS A COMPARISON, not a mention: the decision's OWN `return ... : RadminRxOwner::none` arms
    #   are what produce the value. Only a router that TESTS for it can consume it, so that is what is searched for.
    none_sites = [f"{i+1}: {norm(l)}" for i, l in enumerate(rx)
                  if "== RadminRxOwner::none" in norm(l) or "!= RadminRxOwner::none" in norm(l)]
    if not none_sites:
        ok("O9", "there is no `none` arm — an unowned remote type takes no branch here and reaches the guard")
    else:
        bad("O9", "a `none` arm was added (it must consume nothing): " + " | ".join(none_sites))

    # O10 — tests never become production owners.
    test_owners = [rel for rel in named if rel.startswith("test" + os.sep) or rel.startswith("test/")]
    if not test_owners:
        ok("O10", "no file under test/ names the capability pair in code — tests are not production owners")
    else:
        bad("O10", f"test files name the pair: {test_owners}")

    # O11 — the pure decision is capability-AGNOSTIC (compiled on every build).
    prob = []
    nh_lines, nh_st = code[NODE_H], cond_stack_per_line(code[NODE_H])
    decl = [i for i, l in enumerate(nh_lines) if norm(l) == DECISION_DECL]
    if len(decl) != 1:
        prob.append(f"{NODE_H}: {len(decl)} `static` declarations of the decision (want exactly 1, verbatim)")
    else:
        conds = [norm(c) for c in nh_st[decl[0]]]
        if any("MR_FEAT_RADMIN" in c for c in conds):
            prob.append(f"{NODE_H}: the decision's declaration is capability-GATED ({conds})")
    rx_st = cond_stack_per_line(rx)
    dfn = [i for i, l in enumerate(rx) if norm(l) == DECISION_DEF]
    if len(dfn) != 1:
        prob.append(f"{RX}: {len(dfn)} definitions of the decision (want exactly 1)")
    else:
        conds = [norm(c) for c in rx_st[dfn[0]]]
        if any("MR_FEAT_RADMIN" in c for c in conds):
            prob.append(f"{RX}: the decision's definition is capability-GATED ({conds})")
    if not prob:
        ok("O11", "the pure decision is declared `static` and compiled UNGATED on every build "
                  "(only its consumers are capability-gated)")
    else:
        bad("O11", " | ".join(prob))

    # O12 — no `#ifndef` override surface anywhere in the scanned tree.
    over = [f"{rel}:{i+1}" for rel, lines in code.items() for i, l in enumerate(lines)
            if norm(l).startswith("#ifndef MR_FEAT_RADMIN") or norm(l).startswith("# ifndef MR_FEAT_RADMIN")]
    if not over:
        ok("O12", "the pair has no `#ifndef` override surface in lib/, src/ or test/ (R-RA-26)")
    else:
        bad("O12", f"an override surface exists: {over}")

    return res


# ---------------------------------------------------------------------------------------------------------------
# the controls
# ---------------------------------------------------------------------------------------------------------------
# Each control: (id, description, relative file, exact find text, replacement, the check ids that MUST reject it).
CONTROLS = [
    ("W-UNKNOWN", "an UNAPPROVED production file starts naming the pair (a consumer nobody reviewed)",
     "src/firmware_remote.cpp", "// MeshRoute — src/firmware_remote.cpp",
     "// MeshRoute — src/firmware_remote.cpp\n#if MR_FEAT_RADMIN_ACCEPT\n#endif", ("O1",)),
    ("W-EXTRA-SITE", "an EXTRA capability guard appears inside an ALLOWED file (a second, unreviewed consumer)",
     RX, "void Node::do_post_ack() {", "#if MR_FEAT_RADMIN_ACCEPT\n#endif\nvoid Node::do_post_ack() {", ("O4e",)),
    ("W-NOCALL", "the production call to the pure decision is DELETED and replaced by a constant",
     RX, DECISION_CALL, "const RadminRxOwner radmin_owner = RadminRxOwner::command_accept;", ("O4e", "O7a")),
    ("W-BYPASS", "the decision is still called but the router BYPASSES it and re-tests the raw type byte "
                 "(a correctly tested helper the real router does not use)",
     RX, "if (radmin_owner == RadminRxOwner::command_accept) {",
     "if (pa.type == DATA_TYPE_REMOTE_CMD) {", ("O7b",)),
    ("W-SWAP", "★ the two MACRO ARGUMENTS are swapped at the call site — INVISIBLE to the {1,1} native binary, "
               "which is exactly why this control lives here and not in a mutation battery",
     RX, DECISION_CALL,
     "const RadminRxOwner radmin_owner = radmin_rx_owner(pa.type, MR_FEAT_RADMIN_ACCEPT, MR_FEAT_RADMIN_CLIENT);",
     ("O4e", "O7a")),
    ("W-OWNER-CMD", "the COMMAND entry point is compiled under the CLIENT capability (the R-RA-8 inversion)",
     RX, "#if MR_FEAT_RADMIN_ACCEPT\n// ACCEPT-OWNED.",
     "#if MR_FEAT_RADMIN_CLIENT\n// ACCEPT-OWNED.", ("O4e", "O6a")),
    ("W-OWNER-RESP", "the RESPONSE entry point is compiled under the ACCEPT capability (the mirror inversion)",
     RX, "#if MR_FEAT_RADMIN_CLIENT\n// CLIENT-OWNED.",
     "#if MR_FEAT_RADMIN_ACCEPT\n// CLIENT-OWNED.", ("O4e", "O6b")),
    ("W-WIDEN-ACCEPT", "the ACCEPT owner is legacy-widened with `|| MR_FEAT_REMOTE_MGMT` (forbidden by R-RA-27)",
     RX, "#if MR_FEAT_RADMIN_ACCEPT\n// ACCEPT-OWNED.",
     "#if MR_FEAT_RADMIN_ACCEPT || MR_FEAT_REMOTE_MGMT\n// ACCEPT-OWNED.", ("O4e", "O5")),
    ("W-WIDEN-CLIENT", "the CLIENT owner is legacy-widened with `|| MR_FEAT_REMOTE_MGMT`",
     RX, "#if MR_FEAT_RADMIN_CLIENT\n// CLIENT-OWNED.",
     "#if MR_FEAT_RADMIN_CLIENT || MR_FEAT_REMOTE_MGMT\n// CLIENT-OWNED.", ("O4e", "O5")),
    ("W-NONE-ARM", "an early consume/return is added for the `none` decision — an unowned type would stop "
                   "reaching the fail-closed guard, and NO capability name appears in the edit at all",
     RX, "        (void)radmin_owner;",
     "        if (radmin_owner == RadminRxOwner::none) { become_free(); return; }\n        (void)radmin_owner;",
     ("O9",)),
    ("W-TEST-OWNER", "a TEST file starts naming the capability pair (tests must not become production owners)",
     "test/test_node_r3.cpp", "#include <cstring>", "#include <cstring>\n#if MR_FEAT_RADMIN_ACCEPT\n#endif",
     ("O1", "O10")),
    ("W-OVERRIDE", "an `#ifndef` override surface is re-opened on the pair (an invalid pair becomes dialable)",
     HDR, "#if defined(MR_PROFILE_MOBILE)\n#  define MR_FEAT_RADMIN_CLIENT 1",
     "#ifndef MR_FEAT_RADMIN_CLIENT\n#endif\n#if defined(MR_PROFILE_MOBILE)\n#  define MR_FEAT_RADMIN_CLIENT 1",
     ("O4a", "O12")),
    ("W-DECISION-GATED", "the pure decision itself becomes capability-gated, so a role-disabled build could not "
                         "even compute an owner and the four-combination native test would stop being possible",
     NODE_H, "    " + DECISION_DECL,
     "#if MR_FEAT_RADMIN_ACCEPT\n    " + DECISION_DECL + "\n#endif", ("O4c", "O11")),
    # ---- §RADMIN slice 3: one control PER NEW OWNER BOUNDARY, in the shape of the twelve above ----------------
    # ★ EACH IS THE TEMPTING WRONG EDIT, not a bare deletion: a boundary DELETED, a boundary WIDENED with the
    #   legacy switch, a boundary INVERTED onto the wrong capability, a DUPLICATE guard, and the pure service
    #   headers ACQUIRING a capability macro (the [[B255]] idiom's own violation).
    ("W-S3-DROP-DISPATCH", "§RADMIN slice 3: the ACCEPT gate around the ROUTER FORWARDING arm is deleted, so a "
                           "CLIENT board would route `acl`/`admin-id` into a target store it must not have",
     CMDS_CPP, "#if MR_FEAT_RADMIN_ACCEPT\n    if (admin_router_arm(line, len, out)) return true;\n#endif",
     "    if (admin_router_arm(line, len, out)) return true;", ("O4f",)),
    ("W-S3-DROP-BLE", "§RADMIN slice 3: the ACCEPT gate around the R-RA-29 BLE REFUSAL is deleted, so a CLIENT "
                      "board acquires an unused target-family guard it was ruled not to carry",
     FW_MAIN, "#if MR_FEAT_RADMIN_ACCEPT\n    if (mrfw::admin_verb_owns(line, len))",
     "    if (mrfw::admin_verb_owns(line, len))", ("O4i",)),
    ("W-S3-WIDEN-BOOT", "§RADMIN slice 3: the boot-call gate is legacy-widened with `|| MR_FEAT_REMOTE_MGMT` "
                        "(forbidden by R-RA-27 — the capability must not be aliased to the legacy switch)",
     FW_MAIN, "#if MR_FEAT_RADMIN_ACCEPT\n    mrfw::admin_stores_boot_report_console();",
     "#if MR_FEAT_RADMIN_ACCEPT || MR_FEAT_REMOTE_MGMT\n    mrfw::admin_stores_boot_report_console();",
     ("O4i", "O5")),
    ("W-S3-INVERT-HELP", "§RADMIN slice 3: the help index's two names are compiled under the CLIENT capability — "
                         "the R-RA-8 inversion, which would advertise a target surface on a MOBILE build",
     # ⓘ REJECTED BY O4f ALONE, and that is the CORRECT answer rather than a weaker one: the file census (O1) is
     #   unmoved because `firmware_help.h` is still an approved namer — the site's TEXT is what changed, which is
     #   precisely the class O4's per-file MULTISET exists to catch. Naming O1 here would have been a control that
     #   passed for the wrong reason.
     HELP_H, "#if MR_FEAT_RADMIN_ACCEPT\n    out.println(F(\"acl\"));",
     "#if MR_FEAT_RADMIN_CLIENT\n    out.println(F(\"acl\"));", ("O4h",)),
    ("W-S3-DUP-DECL", "§RADMIN slice 3: a DUPLICATE capability guard appears in the boot wrapper's header — a "
                      "second, unreviewed gating site inside an allowed file",
     CMDS_H, "void admin_stores_boot_report_console();",
     "void admin_stores_boot_report_console();\n#endif\n#if MR_FEAT_RADMIN_ACCEPT", ("O4g",)),
    ("W-S3-GATE-PURE", "§RADMIN slice 3: a PURE SERVICE HEADER acquires a capability macro — the [[B255]] idiom's "
                       "own violation, which would stop the native suite exercising the service arms at all",
     "src/firmware_admin_acl.h", "namespace mrfw {",
     "#if MR_FEAT_RADMIN_ACCEPT\n#endif\nnamespace mrfw {", ("O1",)),
    # ---- §RADMIN slice 4: one control PER NEW CONTROLLER OWNER BOUNDARY, in the same five shapes ---------------
    ("W-S4-DROP-DISPATCH", "§RADMIN slice 4: the CLIENT gate around the controller ROUTER FORWARDING arm is "
                           "deleted, so an ACCEPT board would route `admin-key`/`admin-target` into controller "
                           "stores it was ruled not to have",
     CMDS_CPP, "#if MR_FEAT_RADMIN_CLIENT\n    if (admin_client_router_arm(line, len, out)) return true;\n#endif",
     "    if (admin_client_router_arm(line, len, out)) return true;", ("O4f",)),
    ("W-S4-DROP-BLE", "§RADMIN slice 4: the CLIENT gate around the R-RA-30 BLE SPLIT is deleted, so an ACCEPT "
                      "board acquires a controller-family guard it was ruled not to carry",
     FW_MAIN, "#if MR_FEAT_RADMIN_CLIENT\n    if (mrfw::admin_client_ble_refuses(line, len))",
     "    if (mrfw::admin_client_ble_refuses(line, len))", ("O4i",)),
    ("W-S4-WIDEN-BOOT", "§RADMIN slice 4: the controller boot-call gate is legacy-widened with "
                        "`|| MR_FEAT_REMOTE_MGMT` (forbidden by R-RA-27)",
     FW_MAIN, "#if MR_FEAT_RADMIN_CLIENT\n    mrfw::admin_client_stores_boot_report_console();",
     "#if MR_FEAT_RADMIN_CLIENT || MR_FEAT_REMOTE_MGMT\n    mrfw::admin_client_stores_boot_report_console();",
     ("O4i", "O5")),
    ("W-S4-INVERT-HELP", "§RADMIN slice 4: the help index's two CONTROLLER names are compiled under the ACCEPT "
                         "capability — the R-RA-8 inversion, which would advertise a controller surface on a "
                         "GATEWAY build",
     HELP_H, "#if MR_FEAT_RADMIN_CLIENT\n    out.println(F(\"admin-key\"));",
     "#if MR_FEAT_RADMIN_ACCEPT\n    out.println(F(\"admin-key\"));", ("O4h",)),
    ("W-S4-INVERT-REGEN", "★★ §RADMIN slice 4: `do_regen`'s CLIENT ADMISSION is compiled under the ACCEPT "
                          "capability — the controller check would run on the managed half and never on the "
                          "controller, which is the exact inversion R-RA-8 exists to forbid",
     CMDS_CPP, "#if MR_FEAT_RADMIN_CLIENT\n    // ★★★ §RADMIN slice 4 — THE CONTROLLER ADMISSION",
     "#if MR_FEAT_RADMIN_ACCEPT\n    // ★★★ §RADMIN slice 4 — THE CONTROLLER ADMISSION", ("O4f",)),
    ("W-S4-DUP-DECL", "§RADMIN slice 4: a DUPLICATE capability guard appears in the controller boot wrapper's "
                      "header — a second, unreviewed gating site inside an allowed file",
     CMDS_H, "void admin_client_stores_boot_report_console();",
     "void admin_client_stores_boot_report_console();\n#endif\n#if MR_FEAT_RADMIN_CLIENT", ("O4g",)),
    # ---- §RADMIN slice 5: one control PER NEW OWNER BOUNDARY, in the same five shapes ------------------------
    # ★ THE FIRST TWO ARE THE ONES THIS SLICE MOST NEEDED: the state block and the legacy slot are now on
    #   OPPOSITE capabilities, and getting either wrong is invisible to the `{1,1}` native binary — it would
    #   simply move ~2 kB of RAM onto the wrong product, which no native case and no corpus stream can see.
    ("W-S5-DROP-SESSION", "★ §RADMIN slice 5: the ACCEPT gate around the 2 064-byte session STATE BLOCK is "
                          "deleted, so a CLIENT board would carry the whole target-side session state it was "
                          "ruled not to have — invisible to a {1,1} host binary",
     # ⓘ THE GATE IS NEUTRALISED, NOT DELETED, and that is the honest edit rather than the convenient one:
     #   removing the `#if` line alone leaves its `#endif` orphaned, which is an UNBALANCED SOURCE and therefore
     #   an INSTRUMENT ERROR (`ctl-BAD`), never a control. `#if 1` compiles the block on EVERY profile — exactly
     #   the defect this control is about — while keeping the file well-formed.
     NODE_H, "#if MR_FEAT_RADMIN_ACCEPT\n    // ★★★ §remote-admin v2 SLICE 5 — **THE ONE ACCEPT-ONLY REMOTE-ADMIN STATE BLOCK.**",
     "#if 1\n    // ★★★ §remote-admin v2 SLICE 5 — **THE ONE ACCEPT-ONLY REMOTE-ADMIN STATE BLOCK.**", ("O4c",)),
    ("W-S5-INVERT-SLOT", "★ §RADMIN slice 5: the LEGACY STAGING SLOT is compiled under ACCEPT instead of CLIENT "
                         "— the R-RA-8 inversion for storage: the target keeps the slot Slice 5 removed and the "
                         "controller loses the one it still needs",
     NODE_H, "#if MR_FEAT_RADMIN_CLIENT\n    // §remote-mgmt: single inbound `rcmd`/resp slot",
     "#if MR_FEAT_RADMIN_ACCEPT\n    // §remote-mgmt: single inbound `rcmd`/resp slot", ("O4c",)),
    ("W-S5-WIDEN-TIMER", "§RADMIN slice 5: the ACCEPT gate on the shared EXPIRY TIMER case is legacy-widened "
                         "with `|| MR_FEAT_REMOTE_MGMT` (forbidden by R-RA-27)",
     NODE_CPP, "#if MR_FEAT_RADMIN_ACCEPT\n    // §remote-admin v2 SLICE 5 (R-RA-22 / design §15): the ONE shared",
     "#if MR_FEAT_RADMIN_ACCEPT || MR_FEAT_REMOTE_MGMT\n    // §remote-admin v2 SLICE 5 (R-RA-22 / design §15): the ONE shared",
     ("O4b", "O5")),
    ("W-S5-INVERT-TAKE", "§RADMIN slice 5: `take_remote_inbound`'s real body is compiled under ACCEPT instead of "
                         "CLIENT, so the drain returns to the role that no longer has a slot to drain",
     MAC_CPP, "#if MR_FEAT_RADMIN_CLIENT\nbool Node::take_remote_inbound(RemoteInbound& out) {",
     "#if MR_FEAT_RADMIN_ACCEPT\nbool Node::take_remote_inbound(RemoteInbound& out) {", ("O4d",)),
    ("W-S5-DROP-DRAIN", "§RADMIN slice 5: the CLIENT gate around fw_main's whole LEGACY DRAIN block is deleted, "
                        "so an ACCEPT board keeps the legacy `rcmd` execution path this slice removed AND pays "
                        "its two statics again",
     FW_MAIN, "#if MR_FEAT_RADMIN_CLIENT\n    { static meshroute::Node::RemoteInbound ri;",
     "    { static meshroute::Node::RemoteInbound ri;", ("O4i",)),
    ("W-S5-GATE-PURE-SESSION", "★ §RADMIN slice 5: the PURE SESSION IMPLEMENTATION acquires a capability macro — "
                               "the [[B255]] idiom's own violation, which would stop the native suite driving "
                               "the classifier, the crypto and the expiry at all",
     "lib/core/remote_session.h", "namespace MESHROUTE_NS {",
     "#if MR_FEAT_RADMIN_ACCEPT\n#endif\nnamespace MESHROUTE_NS {", ("O1",)),
    ("W-S5-GATE-PURE-RUNTIME", "§RADMIN slice 5: the PURE prepare/commit/discard SEAM acquires a capability "
                               "macro — the same violation on the file that owns the durable/live ORDERING",
     "src/firmware_admin_runtime.h", "namespace mrfw {",
     "#if MR_FEAT_RADMIN_ACCEPT\n#endif\nnamespace mrfw {", ("O1",)),
    ("W-S4-GATE-PURE", "§RADMIN slice 4: a PURE CONTROLLER SERVICE HEADER acquires a capability macro — the "
                       "[[B255]] idiom's own violation, which would stop the native suite exercising the "
                       "keyring's arms at all",
     "src/firmware_admin_keyring.h", "namespace mrfw {",
     "#if MR_FEAT_RADMIN_CLIENT\n#endif\nnamespace mrfw {", ("O1",)),
    ("W-S4-GATE-PURE-VERBS", "§RADMIN slice 4: the CONTROLLER VERB header acquires a capability macro — the same "
                             "violation on the file that owns R-RA-30's split predicate, which the BLE guard's "
                             "extractor compiles UNGATED",
     "src/firmware_admin_client_verbs.h", "namespace mrfw {",
     "#if MR_FEAT_RADMIN_CLIENT\n#endif\nnamespace mrfw {", ("O1",)),
]

# The controls of the controls: an edit that is NOT a violation must leave the checker GREEN; a find that does not
# match exactly once is an INSTRUMENT ERROR, not a control; an unreadable source is a GATE ERROR, not a pass.
BENIGN = (RX, "// THE PURE DECISION.", "// THE PURE DECISION (a benign comment edit — control Y1).")


def snapshot(root: Path, dst: Path) -> None:
    for rel in scan_files(root):
        d = dst / rel
        d.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(root / rel, d)


def sha_tree(root: Path) -> str:
    h = hashlib.sha256()
    for rel in scan_files(root):
        h.update(rel.encode())
        h.update(hashlib.sha256((root / rel).read_bytes()).digest())
    return h.hexdigest()


def apply_one(work: Path, pristine: Path, rel: str, find: str, repl: str) -> None:
    """Restore `rel` from the pristine snapshot, then apply EXACTLY ONE match. Anything else is a GateError."""
    shutil.copyfile(pristine / rel, work / rel)
    txt = (work / rel).read_text(encoding="utf-8")
    hits = txt.count(find)
    if hits != 1:
        raise GateError(f"the control's find text matched {hits} time(s) in {rel}, must be exactly 1")
    new = txt.replace(find, repl)
    if new == txt:
        raise GateError(f"the control's edit changed nothing in {rel} (vacuous)")
    (work / rel).write_text(new, encoding="utf-8")


def run_controls(root: Path) -> int:
    n_ok = n_bad = 0
    before = sha_tree(root)
    with tempfile.TemporaryDirectory(prefix="mr_ownership_ctl-") as tmp:
        pristine = Path(tmp) / "pristine"
        work = Path(tmp) / "work"
        snapshot(root, pristine)
        snapshot(root, work)
        if sha_tree(work) != before:
            print("  ctl-BAD  the isolated snapshot is not identical to the checkout — nothing measured")
            return 1

        # the positive baseline: the untouched snapshot must be GREEN, or every rejection below is meaningless
        base = run_checks(work)
        if any(not okk for _, okk, _ in base):
            print("  ctl-BAD  the untouched snapshot is already RED — no control below can mean anything")
            for cid, okk, msg in base:
                if not okk:
                    print(f"           {cid}: {msg}")
            return 1
        print(f"  ctl-ok   Y0 the untouched snapshot passes all {len(base)} checks "
              f"(the baseline every rejection below is measured against)")
        n_ok += 1

        for cid, desc, rel, find, repl, want in CONTROLS:
            try:
                apply_one(work, pristine, rel, find, repl)
                got = sorted(c for c, okk, _ in run_checks(work) if not okk)
            except GateError as e:
                print(f"  ctl-BAD  {cid} — INSTRUMENT ERROR, not a control: {e}")
                n_bad += 1
                continue
            finally:
                shutil.copyfile(pristine / rel, work / rel)
            missing = [w for w in want if w not in got]
            if not got:
                print(f"  ctl-BAD  {cid} — the checker ACCEPTED it ({desc}); nothing measures this")
                n_bad += 1
            elif missing:
                print(f"  ctl-BAD  {cid} — rejected, but not by the named check(s) {missing}; rejected by {got}")
                n_bad += 1
            else:
                print(f"  ctl-ok   {cid} -> REJECTED by {', '.join(got)} — {desc}")
                n_ok += 1

        # Y1 — a benign edit must NOT be rejected (the controls above must not be rejecting everything).
        rel, find, repl = BENIGN
        try:
            apply_one(work, pristine, rel, find, repl)
            got = sorted(c for c, okk, _ in run_checks(work) if not okk)
        except GateError as e:
            print(f"  ctl-BAD  Y1 — INSTRUMENT ERROR: {e}")
            n_bad += 1
            got = ["(error)"]
        finally:
            shutil.copyfile(pristine / rel, work / rel)
        if not got:
            print("  ctl-ok   Y1 a benign comment edit is ACCEPTED — the checks reject violations, not changes")
            n_ok += 1
        elif got != ["(error)"]:
            print(f"  ctl-BAD  Y1 a benign comment edit was rejected by {got} — the checks are over-broad")
            n_bad += 1

        # Y2/Y3 — a multi-match and a zero-match find are INSTRUMENT ERRORS, never controls.
        for yid, find, why in (("Y2", "#include", "a multi-match find"), ("Y3", "__no_such_text__", "a vacuous find")):
            try:
                apply_one(work, pristine, RX, find, find + " ")
                print(f"  ctl-BAD  {yid} {why} was ACCEPTED as a control — the single-match assertion is dead")
                n_bad += 1
            except GateError:
                print(f"  ctl-ok   {yid} {why} is refused as an INSTRUMENT ERROR, never scored as a control")
                n_ok += 1
            finally:
                shutil.copyfile(pristine / RX, work / RX)

        # Y4 — an unreadable required source is a GATE ERROR, not a pass.
        try:
            (work / RX).write_bytes(b"\xff\xfe\x00 not utf-8 \xff")
            run_checks(work)
            print("  ctl-BAD  Y4 an unreadable source did NOT raise a gate error")
            n_bad += 1
        except GateError:
            print("  ctl-ok   Y4 an unreadable/malformed source raises a GATE ERROR, never a silent pass")
            n_ok += 1
        finally:
            shutil.copyfile(pristine / RX, work / RX)

    after = sha_tree(root)
    if before != after:
        print(f"  ctl-BAD  the shared checkout MOVED during the controls ({before} -> {after})")
        n_bad += 1
    else:
        print(f"  ctl-ok   Y5 the shared checkout is byte-identical before and after ({before[:16]}…)")
        n_ok += 1
    print(f"ownership controls: {n_ok} verified / {n_bad} unusable")
    return 1 if n_bad else 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=str(Path(__file__).resolve().parents[2]))
    ap.add_argument("--controls", action="store_true")
    a = ap.parse_args()
    root = Path(a.root).resolve()
    try:
        if a.controls:
            return run_controls(root)
        before = sha_tree(root)
        res = run_checks(root)
        for cid, okk, msg in res:
            print(f"  {'ok  ' if okk else 'FAIL'} {cid} {msg}")
        after = sha_tree(root)
        if before != after:
            print(f"  FAIL O13 the scanned sources moved during the run ({before} -> {after})")
            return 1
        print(f"  ok   O13 source integrity: {len(scan_files(root))} scanned files, "
              f"sha256(tree) {before[:16]}… before and after")
        return 1 if any(not okk for _, okk, _ in res) else 0
    except GateError as e:
        print(f"  GATE ERROR — the ownership contract could not be evaluated: {e}")
        return 2


if __name__ == "__main__":
    sys.exit(main())
