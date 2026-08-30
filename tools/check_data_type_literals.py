#!/usr/bin/env python3
# MeshRoute — check_data_type_literals.py
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""§18.1.4 STRUCTURAL CONTROL — no surviving NUMERIC DataType literal in active source.

THE RULE THIS ENFORCES (custody design §5.3): after the namespace transition, code must use SYMBOLIC names or
the new values. It must not "retain literal comparisons such as `type == 3` for E2E ACK", translate old on-air
values, or infer a type from its former ordinal. [[B265]] was one such literal
(`console_json.cpp` comparing `type == 3`) and it is closed by this slice; this check is what keeps it closed.

⛔⛔ SWITCH-CASE LABELS ARE IN SCOPE, AND THEY ARE THE HALF THAT ALMOST ESCAPED. The QG brief review found five
    live numeric `case 1:`..`case 5:` DataType labels in `lib/core/frame_trace.h` that A0's census had missed —
    beyond the three `type == 3` sites it did find, which is why A0's "console_json.cpp is the sole production
    numeric coupling" statement is corrected in the evidence file. A stale case label is the worst shape of this
    defect class:
      · it still COMPILES after a renumbering — it simply never fires for its type, and fires for whatever now
        owns that number (post-transition `case 3:` would have printed "E2E_ACK" for a SEALED_RELAY);
      · `-Wswitch` cannot see it (the switch is over a `uint8_t`, and the sibling dispatch is an if-chain —
        finding A0-F2); and
      · `frame_trace.h` is `#if defined(ARDUINO)`, so NEITHER the native suite NOR the simulator ever compiles
        it. ⇒ this script is the ONLY gate that can see a regression there, which is why it exists.

WHAT IS SEARCHED, and what is deliberately not:
  · active `lib/`, `src/` and `test/` C/C++ sources (vendored trees excluded);
  · comparisons of a type-ish identifier against a small integer literal (`type == 3`, `pa.type != 12`, …);
  · `case <small int>:` labels inside a `switch` whose subject is a type-ish identifier; and
  · a `/*type=*/<small int>` argument comment — the shape the test literal had.
  ⛔ NOT searched: comments and strings (they are stripped first — a HISTORICAL note naming ordinal 3 is
    allowed and, in this tree, is the correction idiom); `type != 0` and `type == 0` (0 is the UNTYPED DM, not
    an allocated value, and the codec's own `type != 0` is the APP-bit derivation the design keeps).

EXIT 0 = clean. EXIT 1 = a numeric DataType coupling survived, with its file:line named.

USAGE:  python3 tools/check_data_type_literals.py
        python3 tools/check_data_type_literals.py --selftest   # prove the search can FAIL (§18.0.3)

⚠ §18.0.3 — A ZERO SEARCH RESULT IS EVIDENCE ONLY UNDER A REINTRODUCED KNOWN INSTANCE. `--selftest` puts each
  known historical form back into a COPY of its real file and requires the search to reject it. A search that
  cannot fail is not a measurement.
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ROOTS = ("lib", "src", "test")
EXTS = (".cpp", ".h", ".hpp", ".cc", ".c")
# vendored / third-party trees this project does not own (MeshCore radio headers, doctest, …)
EXCLUDE_PARTS = ("third_party", "vendor", "libdeps", "doctest")

# An identifier that names a DATA type. Kept narrow on purpose: a broad "anything called type" match would
# sweep up EventField types, PushKind, record kinds and NV blob versions, and a check that cries wolf is a
# check people switch off.
TYPE_IDENT = r"(?:[A-Za-z_][A-Za-z0-9_]*\.)?(?:[A-Za-z_][A-Za-z0-9_]*->)?(?:data_)?(?:pa\.|it\.|d->|o\.|in\.|e\.)?[A-Za-z_]*type"
NUM = r"(0[xX][0-9a-fA-F]+|\d+)"

# `<type-ish> == 3` / `!= 12` / `== 0x80` … against a literal (decimal or hex).
CMP = re.compile(r"\b(" + TYPE_IDENT + r")\s*(==|!=|<=|>=|<|>)\s*" + NUM + r"\b")
# `/*type=*/3`
ARG = re.compile(r"/\*\s*type\s*=\s*\*/\s*" + NUM + r"\b")
# `case <n>:` — only meaningful inside a switch over a type-ish subject; the subject is tracked below.
CASE = re.compile(r"^\s*case\s+" + NUM + r"\s*:")
SWITCH = re.compile(r"\bswitch\s*\(\s*([^)]*?)\s*\)")

# ★★ THE SEMANTIC SET — the ONLY values a literal has to name to be a defect, and the reason the check does not
#    cry wolf. A literal is flagged iff it is either a CURRENT allocated DataType (parsed from the enum, so the
#    check follows the source rather than a copy of it) or one of the PRE-transition ordinals 1..19 (an
#    ordinal-inference leftover — design §5.3's "infer a type from its former ordinal").
#    ⛔ Everything else is NOT a DataType coupling and must not be reported: `type != 0` (the untyped DM and the
#      codec's APP-bit derivation), a deliberately UNALLOCATED probe value in a test (200, 0x81, 0xC0, 0xFE …
#      which is exactly what the unknown-type characterization needs), and an opaque carrier-copy value.
OLD_ORDINALS = set(range(1, 20))


def current_allocated():
    text = (ROOT / "lib/core/frame_codec.h").read_text()
    m = re.search(r"enum\s+DataType\s*:\s*uint8_t\s*\{(.*?)\n\};", text, re.S)
    if not m:
        raise SystemExit("FAIL: could not parse `enum DataType` — the check cannot run, so it must not pass.")
    vals = {int(v, 0) for v in re.findall(r"DATA_TYPE_[A-Z0-9_]+\s*=\s*(0[xX][0-9a-fA-F]+|\d+)\s*,",
                                          m.group(1))}
    if not vals:
        raise SystemExit("FAIL: `enum DataType` parsed to zero members — refusing to pass vacuously.")
    return vals


SEMANTIC = None      # filled by main()/selftest(); {current allocated} | {old ordinals}


def semantic(v):
    return v in SEMANTIC


def strip_comments_and_strings(text):
    """Blank out //, /* */, "..." and '...' so a HISTORICAL mention is never a hit.

    Line structure is preserved (blanks, not deletions) so reported line numbers stay true.
    """
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c in "\"'":
            q = c
            j = i + 1
            while j < n and text[j] != q:
                if text[j] == "\\":
                    j += 1
                j += 1
            j = min(j + 1, n)
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def scan_text(text, path):
    """Return [(line_no, kind, snippet)] for every surviving numeric DataType coupling."""
    src = strip_comments_and_strings(text)
    hits = []
    # a `/*type=*/N` argument comment survives the stripper only if we look at the ORIGINAL, so scan raw for it
    for i, raw in enumerate(text.split("\n"), 1):
        m = ARG.search(raw)
        if m and semantic(int(m.group(1), 0)):
            hits.append((i, "argument comment", raw.strip()[:120]))

    lines = src.split("\n")
    # track the innermost `switch (...)` subject by brace depth, so a `case 3:` is only a hit when the switch
    # is over something type-ish (a `case 3:` in a state machine is nobody's business here).
    switch_stack = []   # (depth_at_open, subject)
    depth = 0
    for i, ln in enumerate(lines, 1):
        sm = SWITCH.search(ln)
        if sm:
            switch_stack.append((depth, sm.group(1)))
        cm = CASE.match(ln)
        if cm and switch_stack:
            subject = switch_stack[-1][1]
            if re.search(r"type", subject) and semantic(int(cm.group(1), 0)):
                hits.append((i, "switch-case label (subject `%s`)" % subject, ln.strip()[:120]))
        for m in CMP.finditer(ln):
            ident, op, val = m.group(1), m.group(2), m.group(3)
            if not semantic(int(val, 0)):
                continue                       # `type != 0` (the untyped DM) and unallocated probe values
            hits.append((i, "comparison", ("%s %s %s" % (ident, op, val))[:120]))
        depth += ln.count("{") - ln.count("}")
        while switch_stack and depth <= switch_stack[-1][0]:
            switch_stack.pop()
    return [(path, i, k, s) for (i, k, s) in hits]


def sources():
    for r in ROOTS:
        for p in sorted((ROOT / r).rglob("*")):
            if p.suffix not in EXTS or not p.is_file():
                continue
            if any(part in EXCLUDE_PARTS for part in p.parts):
                continue
            yield p


def scan_tree(override=None):
    """override = {Path: replacement_text}, used by the selftest to reintroduce a known instance."""
    override = override or {}
    hits = []
    for p in sources():
        text = override.get(p, p.read_text(errors="replace"))
        hits += scan_text(text, p.relative_to(ROOT))
    return hits


def selftest():
    global SEMANTIC
    SEMANTIC = current_allocated() | OLD_ORDINALS
    base = scan_tree()
    if base:
        print("SELFTEST ABORTED — the live tree is not clean, so a negative control proves nothing:")
        for h in base[:10]:
            print("   · %s:%d  %s  %s" % h)
        return 1

    # Each control REINTRODUCES a form that really existed in this tree before §CUSTODY-A closed it.
    controls = [
        ("[[B265]]'s comparison — `type == 3` in the companion JSON encoder",
         ROOT / "lib/console/console_json.cpp",
         "    if (type == MESHROUTE_NS::DATA_TYPE_E2E_ACK) j.lit",
         "    if (type == 3) j.lit"),
        ("a frame_trace.h SWITCH-CASE LABEL reverted to its numeric form (the half A0's census missed, and the "
         "one NO compiler and NO test can see — the header is #if defined(ARDUINO))",
         ROOT / "lib/core/frame_trace.h",
         "                                case DATA_TYPE_E2E_ACK: Serial.print(F(\"E2E_ACK\"));break;",
         "                                case 3: Serial.print(F(\"E2E_ACK\"));break;"),
        ("the TEST literal — `/*type=*/3` passed to write_inbox_dm",
         ROOT / "test/test_console_json.cpp",
         "                       /*type=*/meshroute::DATA_TYPE_E2E_ACK);",
         "                       /*type=*/3);"),
        ("an ordinal-inference comparison anywhere in the MAC receive path",
         ROOT / "lib/core/node_mac_rx.cpp",
         "        if (pa.type == DATA_TYPE_MOBILE_LAYER_QUERY",
         "        if (pa.type == 10"),
    ]

    ok = True
    for label, path, old, new in controls:
        text = path.read_text()
        if text.count(old) != 1:
            print("  ⛔ SELFTEST BROKEN — anchor for %r matched %d times in %s" %
                  (label, text.count(old), path.relative_to(ROOT)))
            ok = False
            continue
        hits = scan_tree({path: text.replace(old, new, 1)})
        if hits:
            print("  RED   (control fired)  %s" % label)
            print("           first: %s:%d  %s  %s" % hits[0])
        else:
            print("  ⛔ GREEN (control DEAD) %s  <-- the search cannot see this form" % label)
            ok = False
    print()
    if ok:
        print("SELFTEST PASS — %d/%d controls RED. The zero result above is a measurement." %
              (len(controls), len(controls)))
        return 0
    print("SELFTEST FAIL — at least one known literal form passes the search unnoticed.")
    return 1


# =================================================================================================
# --comments : an ADVISORY inventory. ⛔ NOT A GATE, and the reason is worth stating rather than hiding.
# =================================================================================================
# The gate above strips comments on purpose: a comment naming a PRE-transition ordinal may be perfectly
# correct — this tree's correction idiom REQUIRES recording what a claim used to say. So "an ordinal appears
# in a comment" is not a defect; "an ordinal appears in a comment that still asserts it as CURRENT" is, and
# that distinction is a judgement about meaning, not a lexical property.
#
# ⛔⛔ WHICH IS WHY THERE IS NO COMMENT GATE HERE, and the alternative was considered and REJECTED. Any
#     lexical rule lands in one of two places, both worse than nothing:
#       · flag every mention -> it fires on every legitimate historical note, so it gets switched off; or
#       · accept an escape word ("historical", "was", "ordinal") -> green becomes a property of having typed
#         the word. That is EXACTLY the vacuity class the A0 review already caught one level up, where a
#         structural check passed on a MENTION of a section rather than the section's evidence.
# ⇒ what lands instead is an INVENTORY: it classifies every DATA-context ordinal in a comment, prints the
#   grep patterns and the beacon/TLV exclusions it used, and ALWAYS EXITS 0. Its value is that the §CUSTODY-A
#   sweep becomes RE-RUNNABLE by one command instead of a reconstructed regex — a reviewer can diff this
#   listing against the report's site table, and a later slice can re-run it after touching DATA prose.

COMMENT_PAT = re.compile(
    r"\b(DATA[ _]?)?TYPE[ -]?(1[0-9]|[1-9])\b|\btype[ -](1[0-9]|[1-9])\b|"
    r"\btypes? (1[0-9]|[1-9])(/(1[0-9]|[1-9]))*\b", re.I)
# The OTHER namespace this must never damage: the BCN ext-TLV types 1..5 are a different numbering entirely.
BEACON_PAT = re.compile(
    r"tlv|beacon|bcn|ext_type|ext-tlv|suspect|liveness|gateway[_-]layer|bridged|team_id_tlv|"
    r"channel[_-]digest|dv:|\bids\b|pairs", re.I)
# Wording that marks a mention as deliberately about the PAST (the correction idiom).
HISTORY_PAT = re.compile(
    r"v4 store|numeric type 3|heir|pre-transition|historical|ordinal|used to|"
    r"before the .{0,24}transition|stored type-3|RETIRED|§CUSTODY-A", re.I)


def comment_inventory():
    """Classify every DATA-context ordinal appearing in a comment. Advisory only; always returns 0."""
    rows = []
    for p in sources():
        raw = p.read_text(errors="replace").split("\n")
        stripped = strip_comments_and_strings("\n".join(raw)).split("\n")
        for i, (ln, code) in enumerate(zip(raw, stripped), 1):
            # a hit that survives stripping is CODE and belongs to the gate above, not to this inventory
            if not COMMENT_PAT.search(ln) or COMMENT_PAT.search(code):
                continue
            cls = ("beacon/TLV (other namespace — PRESERVE)" if BEACON_PAT.search(ln)
                   else "historical (correction idiom — allowed)" if HISTORY_PAT.search(ln)
                   else "REVIEW: reads as a CURRENT DATA-type claim")
            rows.append((cls, str(p.relative_to(ROOT)), i, ln.strip()[:104]))
    from collections import Counter
    counts = Counter(r[0] for r in rows)
    print("ADVISORY comment inventory — NOT A GATE (see the note at `--comments`; this always exits 0).")
    print("  patterns : %s" % COMMENT_PAT.pattern)
    print("  preserved: %s" % BEACON_PAT.pattern)
    print("  historical: %s" % HISTORY_PAT.pattern)
    print()
    for k in sorted(counts):
        print("  %-46s %4d" % (k, counts[k]))
    print()
    for cls, f, i, txt in rows:
        if cls.startswith("REVIEW"):
            print("  REVIEW  %s:%d  %s" % (f, i, txt))
    print()
    print("⛔ A `REVIEW` row is a PROMPT, not a verdict — read the line's context. A zero REVIEW count is not")
    print("   a proof of anything; this inventory cannot tell an active claim from a well-worded historical one.")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true",
                    help="reintroduce each known historical literal and require the search to reject it")
    ap.add_argument("--comments", action="store_true",
                    help="ADVISORY inventory of DATA-context ordinals in COMMENTS (never a gate; always exits 0)")
    args = ap.parse_args()
    global SEMANTIC
    SEMANTIC = current_allocated() | OLD_ORDINALS
    if args.comments:
        return comment_inventory()
    if args.selftest:
        return selftest()
    hits = scan_tree()
    if hits:
        print("FAIL — %d surviving numeric DataType coupling(s) (design §5.3: symbolic names or new values):"
              % len(hits))
        for h in hits:
            print("   · %s:%d  [%s]  %s" % h)
        return 1
    n = sum(1 for _ in sources())
    print("PASS — %d active source file(s) scanned; no numeric DataType comparison, argument literal or "
          "switch-case label survives." % n)
    print("       (roots: %s · comments and strings stripped, so historical notes are allowed)"
          % ", ".join(ROOTS))
    print("       semantic set = %d current allocated value(s) | the 19 pre-transition ordinals"
          % len(current_allocated()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
