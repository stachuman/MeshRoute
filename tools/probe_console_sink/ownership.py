#!/usr/bin/env python3
# MeshRoute — tools/probe_console_sink/ownership.py
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# ★★★ §RADMIN-0c — THE ROUTER/PARSER OWNERSHIP GATE. It answers the ONE question 0c's unified ordering rests on:
#
#     ⇒ IS THERE ANY PRIMARY COMMAND FORM THAT **BOTH** `mrfw::dispatch()` (with its extracted `help_command`
#       half) AND `meshroute::console::parse_command()` WOULD ACCEPT, ON ANY REAL PRODUCT PROFILE?
#
# WHY IT IS LOAD-BEARING. Before 0c the two transports made that fork in OPPOSITE orders — USB asked the router
# first, BLE asked the parser first. Unifying them is behaviour-preserving **only where the two sets are disjoint**;
# on a line both would accept, the order IS the behaviour. 0c therefore does not choose a winner by code order: it
# measures the intersection, requires it EMPTY, and pins that emptiness so a future collision turns this gate RED
# and reaches the owner as a ruling rather than as a silent reordering.
#
# ⛔ IT IS A PROJECTION OF `tools/gen_command_inventory.py`'s CLASSIFIED ROWS, NEVER A SECOND SCAN (R-RA-1). A
#    hand-written list of "the router's verbs" is exactly the artefact the owner's ruling forbids, and a second
#    parser would fork the classification the generator exists to keep single.
#
# WHAT COUNTS AS WHOSE, and why:
#   · ROUTER-owned — every row on a surface of kind "top": `src/firmware_commands.cpp::dispatch` and the help family
#     extracted to `src/firmware_help.h::help_command`. These are the forms `dispatch(line, len, out)` returns true
#     for.
#   · PARSER-owned — every row on `lib/console/console_parse.cpp::parse_command`. These are the forms
#     `parse_command` turns into a `meshroute::Command`; `dispatch()` returns false for all of them, which is
#     precisely why the callers had to open-code a second arm at all.
#   · Everything else is excluded and named: `service_console` / `ble_dispatch_line` only RE-TEST lines another
#     surface owns (so counting them would double-count `peerkey`), sub-verb dispatchers are inside an arm their
#     parent already owns, and the legacy over-the-air `remote_*` surfaces are not console forms.
#
# USAGE
#   tools/probe_console_sink/ownership.py                 # the gate (all six real profiles), pins enforced
#   tools/probe_console_sink/ownership.py --show          # the derived table, for the evidence file
#   tools/probe_console_sink/ownership.py --selftest      # the two positive controls (collision / missing form)
"""Derive the router-owned and parser-owned primary console forms per profile and refuse a non-empty intersection."""

from __future__ import annotations

import argparse
import copy
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import gen_command_inventory as GEN   # noqa: E402 — the ONE classification authority


class OwnershipError(RuntimeError):
    """A refusal. A collision or a lost form must stop the gate, never colour it."""


# ★★ THE PINS. Derived by running `--show` on this tree and written down WITH the derivation, the `PIN_CHECKS`
#    idiom (`tools/probe_ui_model_mutations.py`, `tools/probe_inbox_verbs/run.sh`): WITHOUT A PIN, A DISAPPEARING
#    COMMAND IS INVISIBLE — an empty intersection over an empty set is also "empty".
#    · parser = 7 on every profile: `peerkey`, `peername`, `reqpubkey`, `resolve`, `send`, `send_channel`,
#      `send_layer`. `console_parse.cpp` carries no `#if`, so no profile can change that number.
#    · router = 41 on the reference `full_headless` build, and every other profile's number is that 41 plus or
#      minus NAMED gated `dispatch()` arms — derived by running `--show` on this tree, never quoted from the brief:
#        full_headless 41  (the reference)
#        full_oled     42  = 41 + `ui`                              (`MR_FEAT_OLED`)
#        gateway       39  = 41 − `mobile` − `team`                 (`MR_N_LAYERS >= 2`)
#        gateway_oled  40  = 39 + `ui`
#        mobile        38  = 41 − `password` − `unlock` − `lock`    (`MR_FEAT_REMOTE_MGMT 0`)
#        mobile_oled   39  = 38 + `ui`
#      ⓘ `join`/`create`/`joinprofile` do NOT drop out on a gateway build: the `#else` arm keeps the same three
#        spellings and answers `err gateway_build`, so the FORM is still router-owned there. That is the reason the
#        gateway delta is −2 and not −5, and it is exactly the kind of thing a typed count would have got wrong.
PINS = {
    #  profile          router  parser
    "full_headless": (41, 7),
    "full_oled": (42, 7),
    "gateway": (39, 7),
    "gateway_oled": (40, 7),
    "mobile": (38, 7),
    "mobile_oled": (39, 7),
}


def _first_token_names(rows, surfaces, want, macros):
    """The set of primary FORMS a chosen surface family accepts on one profile."""
    names = set()
    for r in rows:
        key = tuple(r.surface.split("::", 1))
        s = surfaces.get(key)
        if s is None or key not in want:
            continue
        if "serial" not in [t.strip() for t in r.transports.split(",")]:
            continue
        if r.gate != "—" and not GEN.eval_gate(r.gate, macros):
            continue
        for spelling in GEN._spellings(r.verb):
            name = spelling.split()[0]
            if GEN._WORD_NAME_RE.match(name):
                names.add(name)
    return names


def ownership(rows, profile: str):
    """-> (router_names, parser_names) for one real product profile."""
    surfaces = {(s.file, s.func): s for s in GEN.SURFACES}
    macros = GEN.PROFILES[profile]
    router_keys = {(s.file, s.func) for s in GEN.SURFACES if s.kind == "top"}
    parser_keys = {(s.file, s.func) for s in GEN.SURFACES
                   if s.kind == "caller" and s.func == "parse_command"}
    if not router_keys or not parser_keys:
        raise OwnershipError("the surface table no longer names a router or a parser surface")
    return (_first_token_names(rows, surfaces, router_keys, macros),
            _first_token_names(rows, surfaces, parser_keys, macros))


def check(rows, pins=None):
    """-> (report lines, failures). Every profile is measured; a failure NAMES the profile and the offending form."""
    pins = PINS if pins is None else pins
    out, bad = [], 0
    for profile in sorted(GEN.PROFILES):
        router, parser = ownership(rows, profile)
        both = sorted(router & parser)
        want_r, want_p = pins.get(profile, (None, None))
        union_ok = (router | parser) == set(GEN.primary_names(rows, GEN.PROFILES[profile]))
        ok = (not both) and bool(router) and bool(parser) and union_ok \
            and len(router) == want_r and len(parser) == want_p
        detail = "router=%d parser=%d intersection=%s union==projection=%s" % (
            len(router), len(parser), both or "EMPTY", union_ok)
        if want_r is not None and (len(router) != want_r or len(parser) != want_p):
            detail += "  !! PIN expected router=%s parser=%s" % (want_r, want_p)
        if both:
            detail += ("  !! COLLISION: %s is accepted by BOTH the console router and the command parser on this "
                       "profile. 0c does NOT choose a winner by code order — this is an OWNER RULING." % both)
        if not union_ok:
            detail += "  !! the router|parser union is not the generator's own primary projection"
        out.append(("O-%s" % profile, "router/parser ownership is disjoint and complete", ok, detail))
        if not ok:
            bad += 1
    return out, bad


# ---------------------------------------------------------------------------------------------------------------
# The positive controls. ⛔ IN-PROCESS ROW MUTATIONS, so no source file is copied, written or restored: the tree
#    cannot move, and the control still proves the CHECKER (not the source) is what refuses.
# ---------------------------------------------------------------------------------------------------------------

def selftest(rows):
    lines, bad = [], 0

    # C-COLLIDE — a synthetic form owned by BOTH surfaces. The gate must REFUSE it, on every profile it appears on.
    # ⓘ The clone is a real `Row` handed the ROUTER's surface, i.e. the exact shape a future `dispatch()` arm named
    #   `send` would produce — not a string the checker was taught to dislike.
    victim = next(r for r in rows if r.surface.endswith("::parse_command"))
    clone = copy.copy(victim)
    clone.surface = "src/firmware_commands.cpp::dispatch"
    clone.func = "dispatch"
    rep, nbad = check(rows + [clone])
    if nbad == 0 or not any("COLLISION" in d for _i, _d, _o, d in rep):
        lines.append("  FAIL C-COLLIDE a synthetic router/parser collision was ACCEPTED — the gate measures nothing")
        bad += 1
    else:
        hit = [i for i, _d, o, _x in rep if not o]
        lines.append("  ok   C-COLLIDE a synthetic collision (`%s` given to both surfaces) is REFUSED on %d/%d "
                     "profiles" % (victim.verb, len(hit), len(GEN.PROFILES)))

    # C-MISSING — one router FORM deleted, i.e. every row that spells it. The count pin must refuse even though the
    # intersection is still empty — "disjoint" says nothing about "complete", which is why both terms are gated.
    # ⛔ DELETING ONE ROW IS NOT ENOUGH AND THAT IS THE MEASUREMENT, not a workaround: several verbs own more than
    #   one arm (`peers`, `peers all`, `peers <bad>`; `joinprofile` on both sides of the gateway `#if`), so removing
    #   a single row leaves the NAME behind. The control therefore deletes the whole form, which is what "a current
    #   router primary form disappears" actually means.
    victim_name = None
    for r in rows:
        if not r.surface.endswith("::dispatch"):
            continue
        name = GEN._spellings(r.verb)[0].split()[0]
        if sum(1 for q in rows if q.surface.endswith("::dispatch")
               and GEN._spellings(q.verb)[0].split()[0] == name) == 1:
            victim_name = name
            break
    if victim_name is None:
        lines.append("  FAIL C-MISSING no single-row router form exists to delete — the control cannot be built")
        bad += 1
    else:
        kept = [r for r in rows if not (r.surface.endswith("::dispatch")
                                        and GEN._spellings(r.verb)[0].split()[0] == victim_name)]
        rep, nbad = check(kept)
        if nbad == 0:
            lines.append("  FAIL C-MISSING a deleted router form left the gate GREEN — the pins measure nothing")
            bad += 1
        else:
            lines.append("  ok   C-MISSING deleting the router form `%s` REFUSES on %d/%d profile(s) (count pin)"
                         % (victim_name, nbad, len(GEN.PROFILES)))

    # C-EMPTY — the parser surface emptied. "no intersection" over an empty set must not read as a pass.
    rep, nbad = check([r for r in rows if not r.surface.endswith("::parse_command")])
    if nbad == 0:
        lines.append("  FAIL C-EMPTY an EMPTY parser surface still passed — a vacuous emptiness is not a result")
        bad += 1
    else:
        lines.append("  ok   C-EMPTY an emptied parser surface REFUSES on %d profile(s)" % nbad)
    return lines, bad


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--show", action="store_true", help="print the derived per-profile sets (evidence)")
    ap.add_argument("--selftest", action="store_true", help="run the positive controls only")
    a = ap.parse_args(argv)

    rows, _notes, _values, _retests = GEN.build_rows(ROOT)

    if a.show:
        for profile in sorted(GEN.PROFILES):
            router, parser = ownership(rows, profile)
            print("%-14s router(%2d)=%s" % (profile, len(router), " ".join(sorted(router))))
            print("%-14s parser(%2d)=%s" % ("", len(parser), " ".join(sorted(parser))))
            print("%-14s intersection=%s" % ("", sorted(router & parser) or "EMPTY"))
        return 0

    if a.selftest:
        lines, bad = selftest(rows)
        for ln in lines:
            print(ln)
        print("ownership selftest: %d control(s) failed" % bad)
        return 1 if bad else 0

    rep, bad = check(rows)
    for cid, desc, ok, detail in rep:
        print("   %s %s %s   [%s]" % ("ok  " if ok else "FAIL", cid, desc, detail))
    print("   ownership: %d passed / %d failed / %d total" % (len(rep) - bad, bad, len(rep)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
