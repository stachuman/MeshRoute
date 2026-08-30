#!/usr/bin/env python3
# MeshRoute — check_a0_matrix.py
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""§A0 STRUCTURAL CONTROL for the DATA-universe matrix (custody spec §18.0.1).

WHAT THIS ENFORCES, and why it is a script rather than a native test: the matrix is a MARKDOWN artefact
(`docs/superpowers/evidence/2026-08-29-custody-a0-matrix.md`) and the enum is C++
(`lib/core/frame_codec.h`). Nothing in the native suite can notice that a new `DataType` member was added
without a matrix row — a `-Wswitch` diagnostic cannot see an if-chain (that is finding A0-F2), and doctest
cannot read a document. So the binding between the two is checked HERE.

THE SPEC'S REQUIREMENT (§18.0.1, corrected 2026-08-29 by the QG A0-design review) is that the control
"parses the enum AND requires the named special rows — it fails if a new type is added without a row or any
named special row is missing." The enum alone omits live rows, so BOTH halves are checked:

  (1) EVERY member of `enum DataType` has a matrix row keyed by its symbol.
  (2) EVERY member's row states the numeric value the enum actually assigns (so a renumbering that does not
      reach the matrix is caught, not just an addition).
  (3) The FIVE NAMED SPECIAL-ROW CLASSES each have their OWN SECTION carrying real evidence:
        · UNTYPED_DM              — type 0, no TYPE byte (the ordinary DM); not an enum member
        · ENCLOSED_ONLY           — values intended for enclosed origination; outer receipt is not
                                    currently prevented
        · ALLOCATED_NOT_EMITTED   — reserved/retired rows, with that status stated
        · UNKNOWN_REPRESENTATIVE  — representative unknown values and their fall-through
        · TOMBSTONE_NON_WIRE      — 0xFE, an inbox store marker, explicitly NOT a DataType

EXIT 0 = the matrix is structurally complete for the current tree. EXIT 1 = it is not, with the reason named.

USAGE:  python3 tools/check_a0_matrix.py            # check
        python3 tools/check_a0_matrix.py --selftest # prove the control can FAIL (spec §18.0.3)

⛔⛔ CHECK (3) WAS REWRITTEN 2026-08-29 (QG A0 review, blocker 2), AND THE DEFECT IS WORTH KEEPING ON RECORD
    BECAUSE IT IS THE VACUITY CLASS ONE LEVEL UP. The first version asked only whether each class NAME occurred
    ANYWHERE in the document. QG deleted the entire UNKNOWN_REPRESENTATIVE evidence section, left the name in
    this file's own introductory list, and the check still passed — it was measuring a mention, not evidence.
    ⇒ each class is now bound to a POSITIONAL section: a heading matching `SPECIAL-ROW: <KEY>`, which must
    carry substantive content (a table or a list) before the next heading of the same or higher level.
    ★ AND THE OLD SELFTEST COULD NOT HAVE CAUGHT THIS: it replaced EVERY occurrence of the token, so it
    destroyed the heading and the summary mention together and went RED for the wrong reason — a selftest blind
    to its own instrument's weakness. It now deletes ONLY the section body and heading, deliberately LEAVING the
    summary references intact, which is exactly the state QG constructed by hand.

⚠ §18.0.3 — A ZERO SEARCH RESULT IS ACCEPTED ONLY WHEN A REINTRODUCED KNOWN INSTANCE MAKES THE SEARCH FAIL.
  `--selftest` is that control: it re-runs every check against a mutated copy of the enum and of the matrix,
  and REQUIRES each to be rejected. A green check whose selftest does not fire proves nothing.
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENUM_SRC = ROOT / "lib/core/frame_codec.h"
MATRIX = ROOT / "docs/superpowers/evidence/2026-08-29-custody-a0-matrix.md"

# The five named special-row classes the spec requires. Keys are matched literally in the matrix.
SPECIAL_ROWS = (
    "UNTYPED_DM",
    "ENCLOSED_ONLY",
    "ALLOCATED_NOT_EMITTED",
    "UNKNOWN_REPRESENTATIVE",
    "TOMBSTONE_NON_WIRE",
    # ★★ ADDED 2026-08-29 BY §CUSTODY-A. The matrix's §1.2 table is the PRE-transition record and its `#` column
    #    keeps the historical ordinals — rewriting it would destroy the evidence A0 exists to preserve. So the
    #    CURRENT numeric binding moved to its own POSITIONAL section, and check (2) below reads THAT one only.
    "CURRENT NAMESPACE",
)
# The section check (2) binds to for current values. ⛔ Deliberately ONE named section, not "anywhere in the
# document": a value stated in §1.2's historical table must NOT satisfy the current-value check, and before this
# split it did.
CURRENT_NS_KEY = "CURRENT NAMESPACE"
# ⛔ A row this table may never lose. `APP_MESSAGE` is a pure RESERVATION with no behaviour, which makes it the
#    single most droppable row in the document and the one whose absence would be least noticed — so it is
#    named explicitly rather than left to the generic enum-membership check.
REQUIRED_ROWS = ("DATA_TYPE_APP_MESSAGE",)


def parse_enum(text):
    """Return {symbol: value} for `enum DataType : uint8_t { ... }`, source-derived."""
    m = re.search(r"enum\s+DataType\s*:\s*uint8_t\s*\{(.*?)\n\};", text, re.S)
    if not m:
        raise SystemExit("FAIL: could not locate `enum DataType : uint8_t { ... }` in "
                         f"{ENUM_SRC.relative_to(ROOT)} — the control cannot run, so it must not pass.")
    out = {}
    for line in m.group(1).split("\n"):
        line = line.split("//")[0]                      # strip the trailing prose
        em = re.match(r"\s*(DATA_TYPE_[A-Z0-9_]+)\s*=\s*(0[xX][0-9a-fA-F]+|\d+)\s*,", line)
        if em:
            out[em.group(1)] = int(em.group(2), 0)   # §CUSTODY-A: values are hex now; base 0 accepts both
    if not out:
        raise SystemExit("FAIL: `enum DataType` parsed to zero members — refusing to pass vacuously.")
    return out


def extract_special_section(matrix_text, key):
    """Return the full text of the `SPECIAL-ROW: <key>` section (heading + body), or None.

    POSITIONAL, not textual: the section runs from its heading to the next heading of the SAME OR HIGHER
    level. A bare mention of `key` in a summary list or a prose paragraph is deliberately invisible here —
    that is the whole point of the blocker-2 rewrite.
    """
    m = re.search(rf"^(#{{2,6}})\s+[^\n]*?SPECIAL-ROW:\s*{re.escape(key)}\b[^\n]*$",
                  matrix_text, re.M)
    if not m:
        return None
    level = len(m.group(1))
    rest = matrix_text[m.end():]
    nxt = re.search(rf"^#{{1,{level}}}\s+", rest, re.M)
    return m.group(0) + (rest[:nxt.start()] if nxt else rest)


def delete_special_section(matrix_text, key):
    """Remove ONLY the section (heading + body), leaving every other mention of `key` untouched.

    ⚠ This is the exact hand-constructed state the QG A0 review used to defeat the first checker: the
    evidence is gone, the name still appears in the document's introductory list. A control that instead
    replaced every occurrence would destroy the heading AND the mention, and would go RED for the wrong
    reason without ever exercising the positional binding.
    """
    sec = extract_special_section(matrix_text, key)
    if sec is None:
        return matrix_text
    return matrix_text.replace(sec, "", 1)


def value_forms(val):
    """Every spelling the CURRENT NAMESPACE table may legitimately use for one value."""
    return (f"0x{val:02X}", f"0x{val:02x}", str(val))


def check(enum_text, matrix_text):
    """Return a list of failure strings ([] == pass)."""
    fails = []
    members = parse_enum(enum_text)
    current = extract_special_section(matrix_text, CURRENT_NS_KEY)
    if current is None:
        fails.append(f"missing NAMED SPECIAL-ROW SECTION for `{CURRENT_NS_KEY}` — there is nowhere for the "
                     f"post-transition numeric assignments to live, so no current value can be checked")

    for sym, val in sorted(members.items(), key=lambda kv: kv[1]):
        # (1) a row keyed by the symbol, anywhere in the document (the historical matrix satisfies this for the
        #     pre-transition members; the CURRENT NAMESPACE table satisfies it for anything added since)
        if sym not in matrix_text:
            fails.append(f"missing matrix row for enum member {sym} (= {val:#04x})")
            continue
        # (2) ★ THE CURRENT NUMERIC BINDING, READ FROM THE `CURRENT NAMESPACE` SECTION ONLY. `| <sym> | <val> |`
        #     in that section's first two cells. A stale value there is RED; a correct value in §1.2's
        #     HISTORICAL table does not help, which is the whole point of the positional split.
        if current is None:
            continue
        if not any(re.search(rf"\|\s*`?{sym}`?\s*\|\s*`?{form}`?\s*\|", current)
                   for form in value_forms(val)):
            fails.append(f"the CURRENT NAMESPACE table does not state {sym}'s current value {val:#04x} "
                         f"(a renumbering that did not reach the table, or a stale value left behind)")

    # (2b) rows the table may never lose, named explicitly
    if current is not None:
        for sym in REQUIRED_ROWS:
            if not re.search(rf"\|\s*`?{sym}`?\s*\|", current):
                fails.append(f"the CURRENT NAMESPACE table has no row for {sym} — a reservation with no "
                             f"behaviour is the easiest row to drop and the hardest to miss the absence of")

    # (3) the five named special-row classes, bound POSITIONALLY to a section carrying real evidence
    for key in SPECIAL_ROWS:
        sec = extract_special_section(matrix_text, key)
        if sec is None:
            fails.append(f"missing NAMED SPECIAL-ROW SECTION for `{key}` — no heading matches "
                         f"`SPECIAL-ROW: {key}` (a mention elsewhere in the document is NOT evidence)")
            continue
        body = [ln for ln in sec.split("\n")[1:] if ln.strip()]          # drop the heading itself
        substantive = [ln for ln in body if ln.lstrip().startswith(("|", "-", "*"))]
        if len(body) < 3 or not substantive:
            fails.append(f"NAMED SPECIAL-ROW SECTION `{key}` is present but EMPTY of evidence "
                         f"({len(body)} content line(s), {len(substantive)} table/list line(s)) — "
                         f"a heading with nothing under it is not a row")

    return fails


def selftest():
    """Prove each check can FAIL. Spec §18.0.3: a zero result is accepted only under a reintroduced instance."""
    enum_text = ENUM_SRC.read_text()
    matrix_text = MATRIX.read_text()

    baseline = check(enum_text, matrix_text)
    if baseline:
        print("SELFTEST ABORTED — the live tree does not pass, so a negative control proves nothing:")
        for f in baseline:
            print(f"   · {f}")
        return 1

    controls = []

    # C1 — a NEW enum member with no matrix row must be rejected.
    anchor = "    DATA_TYPE_TEAM_KEY_GRANT                = 0xA2,"
    if anchor not in enum_text:
        print("SELFTEST ABORTED — the C1 enum anchor moved; fix the anchor, never the check.")
        return 1
    mutated = enum_text.replace(
        anchor, anchor + "\n    DATA_TYPE_A0_SELFTEST_NEW               = 0xA3,", 1)
    controls.append(("a NEW enum member with no matrix row", check(mutated, matrix_text)))

    # C2 — a RENUMBERED member whose matrix row still states the old value must be rejected.
    mutated = enum_text.replace("DATA_TYPE_E2E_ACK                       = 0x80,",
                                "DATA_TYPE_E2E_ACK                       = 0xB0,", 1)
    controls.append(("a RENUMBERED member the CURRENT NAMESPACE table did not follow", check(mutated, matrix_text)))

    # ★★ C8 (§CUSTODY-A) — a STALE value left in the CURRENT NAMESPACE table. ⛔ This is the control the
    #    positional split exists for: the value below is edited back to its PRE-transition ordinal, which is
    #    still stated (correctly, as history) in §1.2's own table — so a check that matched "anywhere in the
    #    document" would sail straight past it.
    stale = matrix_text.replace("| `DATA_TYPE_E2E_ACK` | `0x80` |", "| `DATA_TYPE_E2E_ACK` | `3` |", 1)
    if stale == matrix_text:
        controls.append(("a STALE current value in the CURRENT NAMESPACE table",
                         ["selftest could not locate the row to make stale"]))
    else:
        controls.append(("a STALE current value in the CURRENT NAMESPACE table (the old ordinal, which §1.2 "
                         "still states as history)", check(enum_text, stale)))

    # ★★ C9 (§CUSTODY-A) — the APP_MESSAGE row DELETED. A reservation with no behaviour is the row most likely
    #    to be tidied away, and its loss would silently un-reserve 0x05 for the app-code design.
    dropped = re.sub(r"^\| `DATA_TYPE_APP_MESSAGE` \|[^\n]*\n", "", matrix_text, count=1, flags=re.M)
    if dropped == matrix_text:
        controls.append(("the APP_MESSAGE row deleted", ["selftest could not locate the APP_MESSAGE row"]))
    else:
        controls.append(("the APP_MESSAGE row deleted from the CURRENT NAMESPACE table",
                         check(enum_text, dropped)))

    # C3..C7 — each named special-row SECTION deleted one at a time, with every OTHER mention of the key left
    # in place (the introductory list, the prose). This reproduces the exact state that defeated the first
    # checker; a control that blanked all occurrences would prove nothing about the positional binding.
    for key in SPECIAL_ROWS:
        mutated = delete_special_section(matrix_text, key)
        if mutated == matrix_text:
            controls.append((f"the `{key}` SECTION deleted (summary mention left intact)",
                             ["selftest could not locate the section to delete"]))
            continue
        still_mentioned = key in mutated          # the defect QG constructed: name present, evidence gone
        controls.append((f"the `{key}` SECTION deleted (summary mention left intact: "
                         f"{'yes' if still_mentioned else 'NO — control is weaker than intended'})",
                         check(enum_text, mutated)))

    ok = True
    for label, fails in controls:
        if fails:
            print(f"  RED   (control fired)  {label}")
        else:
            print(f"  ⛔ GREEN (control DEAD) {label}  <-- this check cannot fail; it is decoration")
            ok = False
    print()
    if ok:
        print(f"SELFTEST PASS — {len(controls)}/{len(controls)} controls RED. "
              "The check is an instrument, not decoration.")
        return 0
    print("SELFTEST FAIL — at least one check cannot fail for the property it claims.")
    return 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true",
                    help="prove every check can FAIL (spec §18.0.3); does not modify any file")
    args = ap.parse_args()

    if not MATRIX.exists():
        print(f"FAIL: the A0 matrix is missing: {MATRIX.relative_to(ROOT)}")
        return 1

    if args.selftest:
        return selftest()

    fails = check(ENUM_SRC.read_text(), MATRIX.read_text())
    members = parse_enum(ENUM_SRC.read_text())
    if fails:
        print(f"FAIL — the A0 matrix is not structurally complete ({len(fails)} problem(s)):")
        for f in fails:
            print(f"   · {f}")
        return 1
    print(f"PASS — {len(members)} enum members each have a matrix row, the CURRENT NAMESPACE table states "
          f"every current value, and all {len(SPECIAL_ROWS)} named special rows are present.")
    print(f"       enum:   {ENUM_SRC.relative_to(ROOT)}")
    print(f"       matrix: {MATRIX.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
