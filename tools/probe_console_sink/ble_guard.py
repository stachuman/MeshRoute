#!/usr/bin/env python3
# MeshRoute — tools/probe_console_sink/ble_guard.py
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# ★★ THE EXECUTED BLE HELP-REFUSAL CHECK (§0a/[[B208]], owner ruling 2026-09-04: "help should not be transferred by
#    BLE"). It answers ONE question, and answers it by RUNNING code rather than by reading it:
#
#        ⇒ IS EVERY LINE THE HELP ROUTER OWNS REFUSED BY THE BLE GUARD BEFORE IT CAN REACH `dispatch()`?
#
# WHY IT IS NOT A PLAIN GREP. Slice 0a's whole defect was a COMPOSITION: `mrfw::help_command` began owning
# `help <topic>`, while `ble_dispatch_line`'s guard still only matched `len == 4`. Neither half was wrong on its own;
# the product of the two was. A structural pin on the guard's TEXT cannot see that — only evaluating the guard
# against the same lines the router is evaluated against can.
#
# WHY IT IS NOT A HAND-COPIED PREDICATE EITHER. `src/fw_main.cpp` cannot be host-compiled (g_node, mrnv, RadioLib,
# the whole board glue), so the guard cannot be linked. ⇒ this file EXTRACTS THE GUARD'S CONDITION TEXT FROM THE REAL
# `src/fw_main.cpp`, compiles that exact expression into a generated TU beside the REAL `src/firmware_help.h`, and
# runs the pair. A copy in a test would drift from the source the day the source changed; an extraction cannot —
# if the guard is edited, this check compiles the edited guard or fails loud (the anchor is unique or it refuses).
#
# ⛔ The extraction is the measurement's weak point, so it is bounded and fail-loud: the condition is taken from the
#    UNIQUE `if` whose body is `return write_err(out, cap, "help", "console_only");` inside `ble_dispatch_line`, and
#    anything else — zero matches, two matches, a body that is not that call — is a REFUSAL, never a skipped check.
"""Extract the real BLE console_only guards from src/fw_main.cpp and EXECUTE each against its real router.

TWO families since §RADMIN slice 3: `help` (anchor `write_err(out, cap, "help", "console_only")`, measured
against `mrfw::help_command`) and `admin` (anchor `…"admin"…`, measured against `mrfw::admin_verb_owns`, and
additionally required to sit inside `ble_dispatch_line`, BEFORE the seam, gated on EXACTLY
`MR_FEAT_RADMIN_ACCEPT`). Each family prints its own `rows=/checks=/failed=` line.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

REFUSAL_CALL = 'write_err(out, cap, "help", "console_only")'

# ★★★ §RADMIN SLICE 3 — THE SECOND FAMILY, AND THE EXTRACTOR IS GENERALIZED RATHER THAN COPIED. R-RA-29 refuses
#     the WHOLE target-side family over BLE with its own named envelope, so this file now answers the same
#     question twice, once per family:
#
#         ⇒ IS EVERY LINE THE <family> ROUTER OWNS REFUSED BY THE BLE GUARD BEFORE IT CAN REACH `dispatch()`?
#
#     ⛔ The `admin` anchor is UNIQUE for the same reason the `help` one is: two matches, zero matches, or a body
#        that is not that call is a REFUSAL, never a skipped check.
ADMIN_REFUSAL_CALL = 'write_err(out, cap, "admin", "console_only")'
# ★★ AND ITS PRODUCT GATE IS PART OF THE MEASUREMENT (R-RA-8): the target family exists on ACCEPT builds only, so a
#    guard that is ungated (a CLIENT board would carry a dead guard for a family it does not have), widened with the
#    legacy switch, or gated on the WRONG capability is a refusal — ⛔ not a passed row.
ADMIN_GATE = "MR_FEAT_RADMIN_ACCEPT"
# The transport seam the guard must PRECEDE. A guard moved below this call would refuse nothing: the seam has
# already run the router and streamed the answer.
SEAM_CALL = "exec_console_line("
BLE_FUNC = "ble_dispatch_line"


class GuardError(RuntimeError):
    """A refusal. An un-extractable guard must stop the gate, never silently skip the rows."""


def _blank_comments(text: str) -> str:
    """Same-length copy with // and /* */ comment bodies blanked, so a commented-out guard cannot be extracted."""
    out, i, n = list(text), 0, len(text)
    while i < n:
        if text[i] == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
        elif text[i] == "/" and i + 1 < n and text[i + 1] == "*":
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = " "
                if i + 1 < n:
                    out[i + 1] = " "
            i += 2
        else:
            i += 1
    return "".join(out)


def _enclosing_gate(blank: str, at: int) -> str:
    """The innermost `#if <expr>` still open at offset `at` — '' when the site is ungated.

    ⛔ Comment-blanked input only: a `#if` inside a comment must not open a scope.
    """
    stack = []
    for m in re.finditer(r"(?m)^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b([^\n]*)$", blank[:at]):
        d, rest = m.group(1), m.group(2).strip()
        if d in ("if", "ifdef", "ifndef"):
            stack.append(rest)
        elif d == "elif":
            if stack:
                stack[-1] = rest
        elif d == "else":
            if stack:
                stack[-1] = "!(" + stack[-1] + ")"
        elif d == "endif":
            if stack:
                stack.pop()
    return stack[-1] if stack else ""


def _func_span(blank: str, name: str):
    """(lo, hi) byte offsets of `name`'s brace-balanced body. Fail loud when it cannot be found."""
    m = re.search(r"\b" + re.escape(name) + r"\s*\(", blank)
    if not m:
        raise GuardError(f"cannot locate `{name}` in fw_main.cpp")
    i = blank.index("{", m.end())
    depth, j = 1, i + 1
    while j < len(blank) and depth:
        if blank[j] == "{":
            depth += 1
        elif blank[j] == "}":
            depth -= 1
        j += 1
    return i, j


def extract_guard(fw_main_text: str, refusal_call: str = REFUSAL_CALL, want_gate: str = None) -> str:
    """The exact condition text of the `if` that answers `console_only`. Fail loud on anything unexpected.

    `want_gate` — when given, the site must sit inside EXACTLY that `#if` expression. `refusal_call` selects the
    family. Both defaults reproduce the pre-§RADMIN-3 behaviour byte for byte.
    """
    blank = _blank_comments(fw_main_text)
    hits = [m.start() for m in re.finditer(re.escape(refusal_call), blank)]
    if len(hits) != 1:
        raise GuardError(f"expected exactly ONE executable {refusal_call!r} in fw_main.cpp, found {len(hits)}")
    at = hits[0]
    # ★ THE SITE MUST BE INSIDE `ble_dispatch_line` AND BEFORE THE TRANSPORT SEAM. A refusal that drifted below
    #   `exec_console_line` would refuse nothing: the router has already answered and the sink has already streamed.
    lo, hi = _func_span(blank, BLE_FUNC)
    if not (lo < at < hi):
        raise GuardError(f"the {refusal_call!r} refusal is not inside `{BLE_FUNC}`")
    seam = blank.find(SEAM_CALL, lo, hi)
    if seam < 0:
        raise GuardError(f"`{BLE_FUNC}` no longer calls `{SEAM_CALL}` — the ordering claim is unmeasurable")
    if at > seam:
        raise GuardError(f"the {refusal_call!r} refusal sits AFTER the transport seam — it refuses nothing")
    got_gate = _enclosing_gate(blank, at)
    if want_gate is not None and got_gate != want_gate:
        raise GuardError(f"the {refusal_call!r} refusal is gated on {got_gate!r}, expected exactly {want_gate!r}")
    open_if = blank.rfind("if (", 0, at)
    if open_if < 0:
        raise GuardError("no `if (` precedes the console_only refusal")
    # brace-free paren match forwards from that `if (`
    i = blank.index("(", open_if)
    depth, j = 0, i
    while j < len(blank):
        if blank[j] == "(":
            depth += 1
        elif blank[j] == ")":
            depth -= 1
            if depth == 0:
                break
        j += 1
    if depth != 0:
        raise GuardError("the guard's condition is unbalanced")
    cond = fw_main_text[i + 1:j].strip()
    between = blank[j + 1:at].strip()
    if between not in ("", "return", "{ return", "{return"):
        raise GuardError(f"the extracted `if` does not directly guard the refusal (found {between!r})")
    if "\n" in cond:
        cond = " ".join(part.strip() for part in cond.split("\n"))
    if refusal_call is REFUSAL_CALL or refusal_call == REFUSAL_CALL:
        if "help" not in cond:
            raise GuardError(f"the extracted condition does not test `help`: {cond!r}")
    else:
        # ⛔ THE ADMIN GUARD MUST EVALUATE THE FAMILY PREDICATE, ⛔ never a hand-written prefix test. A broad
        #    `strncmp(line, "admin", 5)` would swallow `admin-key` — the CONTROLLER's verb (Slice 4) — and a
        #    partial family (only `acl`) would leak the other half onto the link.
        if "admin_verb_owns" not in cond:
            raise GuardError(f"the extracted admin condition does not call `admin_verb_owns`: {cond!r}")
    return cond


# ---------------------------------------------------------------------------------------------------------------
# The corpus. ⛔ SYSTEMATIC, not a handful of favourites: every retired topic word in four shapes, plus the
#   near-misses a prefix bug would leak, plus lines that must stay OUT of the help family entirely.
# ⓘ §0g RETIRED the nine `help <topic>` sections, so these words no longer select anything — the router answers each
#   of them with its bounded refusal. They stay in the corpus DELIBERATELY: they are the exact shapes a reader will
#   still type, they are what the BLE guard must keep covering, and dropping them would shrink the one row that
#   caught the slice-0a defect. The rule under test is unchanged: router owns the line <=> BLE refuses it.
# ---------------------------------------------------------------------------------------------------------------
RETIRED_TOPIC_WORDS = ("messaging", "identity", "mobile", "inbox", "diagnostics", "remote", "test",
                       "provisioning", "cfg")

def corpus() -> list:
    """-> [(line, must_be_refused)] — the SECOND element is the owner's rule, not the guard's opinion."""
    rows = [("help", True), ("?", True), ("help ", True), ("help  ", True)]
    for t in RETIRED_TOPIC_WORDS:
        rows.append((f"help {t}", True))          # the real form
        rows.append((f"help  {t}", True))         # leading spaces — the `peers ` idiom still selects it
        rows.append((f"help {t} x", True))        # malformed tail — still a help line, still refused
        rows.append((f"help {t}x", True))         # a prefix near-miss — still begins with the token `help `
    rows += [("help zzz", True), ("help MESSAGING", True), ("help -h", True)]
    # ...and the lines that are NOT help at all: the guard must let them through, and the router must not own them.
    rows += [("helpful", False), ("helper x", False), ("hel", False), ("h", False), ("HELP", False),
             ("?x", False), ("? messaging", False), ("status", False), ("peers all", False), ("", False)]
    return rows



# ---------------------------------------------------------------------------------------------------------------
# §RADMIN slice 3 — THE ADMIN FAMILY'S CORPUS. SYSTEMATIC, and the `must_refuse` column is the OWNER'S RULE
#   (R-RA-29: *"the whole target-side family ... is refused over BLE"*), NOT this instrument's opinion and NOT read
#   off the predicate under test. Every OWNED line refuses, malformed subforms included; every near miss and every
#   foreign token does not.
# ---------------------------------------------------------------------------------------------------------------
ADMIN_SUBFORMS = (
    "", " ", "  ", "\t", " list", " show", " generate", " rotate confirm", " reset confirm",
    " add owner " + "ab" * 32, " add operator " + "cd" * 32, " set 0 owner", " remove 0 confirm",
    " bogus", " add", " set", " remove", " list x", " show x", " LIST", "\tlist", "  list  ",
)


def admin_corpus() -> list:
    """-> [(line, must_be_refused)] — R-RA-29's rule, spelled out row by row."""
    rows = []
    for fam in ("acl", "admin-id"):
        for tail in ADMIN_SUBFORMS:
            rows.append((fam + tail, True))          # EVERY owned form, malformed subforms INCLUDED
    # ...and the lines that are NOT this family. `admin-key` is the CONTROLLER's verb (Slice 4) and a broad `admin`
    # prefix test would swallow it — which is exactly what a wrong guard would do.
    rows += [("acls", False), ("aclx", False), ("acl_list", False), ("ac", False), ("a", False),
             ("ACL", False), ("ACL list", False), ("xacl", False), (" acl", False),
             ("admin", False), ("admin ", False), ("admin list", False), ("admin-i", False),
             ("admin-idx", False), ("admin-identity", False), ("admin-identity show", False),
             ("admin-key", False), ("admin-key show self", False), ("ADMIN-ID", False),
             ("help", False), ("status", False), ("peers all", False), ("ui preset list", False)]
    return rows


ADMIN_TU = r"""// GENERATED by tools/probe_console_sink/ble_guard.py — not a committed file.
// It holds the BLE guard's condition EXTRACTED VERBATIM from src/fw_main.cpp, evaluated against the REAL
// src/firmware_admin_verbs.h family predicate. Nothing here is a hand-written copy of production logic.
#include <cstdio>
#include <cstring>
#include "firmware_admin_verbs.h"

// >>> EXTRACTED FROM src/fw_main.cpp — the condition of the `if` that answers the `admin` console_only envelope <<<
static bool ble_refuses(const char* line, size_t len) {
    (void)line; (void)len;
    return (__GUARD__);
}

int main() {
    static const struct { const char* line; int must_refuse; } kRows[] = {
__ROWS__
    };
    int checks = 0, fails = 0;
    for (const auto& r : kRows) {
        const size_t len = strlen(r.line);
        const bool refused = ble_refuses(r.line, len);
        const bool owned   = mrfw::admin_verb_owns(r.line, len);

        ++checks;
        if (refused != (r.must_refuse != 0)) {
            ++fails;
            printf("  FAIL A1 `%s` BLE refusal is %s, must be %s\n", r.line,
                   refused ? "yes" : "no", r.must_refuse ? "yes" : "no");
        }
        // THE COMPOSITION INVARIANT: anything the router OWNS must be refused before it can reach dispatch().
        ++checks;
        if (owned && !refused) {
            ++fails;
            printf("  FAIL A2 `%s` is OWNED by the target-store router but NOT refused by BLE\n", r.line);
        }
        // ...and the converse: over-refusing would swallow another verb (`admin-key` is Slice 4's).
        ++checks;
        if (refused && !owned) {
            ++fails;
            printf("  FAIL A3 `%s` is refused as admin but the router does not own it — the guard is too wide\n",
                   r.line);
        }
        // The guard is a PURE PREDICATE: this TU links no store, no service and no sink, so a guard that tried to
        // touch one would not compile. That absence is the check; this row pins the empty-line floor.
        ++checks;
        if (refused && len == 0) {
            ++fails;
            printf("  FAIL A4 the empty line is refused — the guard has no length floor\n");
        }
    }
    printf("ADMIN-GUARD rows=%d checks=%d failed=%d\n", (int)(sizeof kRows / sizeof kRows[0]), checks, fails);
    return fails == 0 ? 0 : 1;
}
"""

TU = r'''// GENERATED by tools/probe_console_sink/ble_guard.py — not a committed file.
// It contains the BLE guard's condition EXTRACTED VERBATIM from src/fw_main.cpp, evaluated against the REAL
// src/firmware_help.h router. Nothing here is a hand-written copy of production logic.
#include <cstdio>
#include <cstring>
#include <string>
#include <Arduino.h>
#include "firmware_help.h"

class Cap : public Print {
public:
    size_t write(uint8_t b) override { s.push_back((char)b); return 1; }
    size_t write(const uint8_t* p, size_t n) override { s.append((const char*)p, n); return n; }
    std::string s;
};

// >>> EXTRACTED FROM src/fw_main.cpp — the condition of the `if` that answers `console_only` <<<
static bool ble_refuses(const char* line, size_t len) {
    (void)line; (void)len;
    return (__GUARD__);
}

int main() {
    static const struct { const char* line; int must_refuse; } kRows[] = {
__ROWS__
    };
    int checks = 0, fails = 0;
    for (const auto& r : kRows) {
        const size_t len = strlen(r.line);
        const bool refused = ble_refuses(r.line, len);
        Cap c;
        const bool owned = mrfw::help_command(r.line, len, c);
        const size_t streamed = refused ? 0u : c.s.size();   // refused => fw_main returns BEFORE dispatch()

        ++checks;
        if (refused != (r.must_refuse != 0)) {
            ++fails;
            printf("  FAIL B1 `%s` BLE refusal is %s, must be %s\n", r.line,
                   refused ? "yes" : "no", r.must_refuse ? "yes" : "no");
        }
        // ★ THE COMPOSITION INVARIANT, and the one this whole file exists for:
        //   anything the help router OWNS must be refused before it can reach dispatch().
        ++checks;
        if (owned && !refused) {
            ++fails;
            printf("  FAIL B2 `%s` is OWNED by the help router but NOT refused by BLE — %zu B / %zu lines "
                   "would stream over NUS\n", r.line, c.s.size(),
                   (size_t)std::count(c.s.begin(), c.s.end(), '\n'));
        }
        ++checks;
        if (streamed != 0) {
            ++fails;
            printf("  FAIL B3 `%s` would put %zu B of help on the BLE link\n", r.line, streamed);
        }
        // A line the guard refuses must be a help line at all: over-refusing would swallow another verb.
        ++checks;
        if (refused && !owned) {
            ++fails;
            printf("  FAIL B4 `%s` is refused as help but the help router does not own it — the guard is too wide\n",
                   r.line);
        }
    }
    printf("BLE-GUARD rows=%d checks=%d failed=%d\n", (int)(sizeof kRows / sizeof kRows[0]), checks, fails);
    return fails == 0 ? 0 : 1;
}
'''


def build_and_run(fw_main_path: str, cxx: str, flags: list, out_dir: str, tag: str = "real",
                  family: str = "help") -> tuple:
    """-> (returncode, stdout). Raises GuardError if the guard cannot be extracted."""
    with open(fw_main_path, "r", encoding="utf-8") as fh:
        text = fh.read()
    if family == "admin":
        cond = extract_guard(text, ADMIN_REFUSAL_CALL, ADMIN_GATE)
        rows = "\n".join('        { %s, %d },' % (_c_lit(line), 1 if must else 0)
                          for line, must in admin_corpus())
        src = ADMIN_TU.replace("__GUARD__", cond).replace("__ROWS__", rows)
    else:
        cond = extract_guard(text)
        rows = "\n".join('        { %s, %d },' % (_c_lit(line), 1 if must else 0) for line, must in corpus())
        src = TU.replace("__GUARD__", cond).replace("__ROWS__", rows)
        src = src.replace("#include <string>", "#include <string>\n#include <algorithm>")
    tu = os.path.join(out_dir, f"ble_guard_{family}_{tag}.cpp")
    binary = os.path.join(out_dir, f"ble_guard_{family}_{tag}.bin")
    with open(tu, "w", encoding="utf-8") as fh:
        fh.write(src)
    b = subprocess.run([cxx, *flags, tu, "-o", binary], capture_output=True, text=True)
    if b.returncode != 0:
        return 2, ("BLE-GUARD BUILD FAILED (%s)\n" % family) + b.stderr
    r = subprocess.run([binary], capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def _c_lit(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main(argv):
    # ⚠ Split on `--` BEFORE argparse: the compiler flags contain `-I…`/`--`-shaped tokens that argparse would try
    #   to own, and the first version of this file handed `--out` straight to g++.
    if "--" not in argv:
        sys.exit("usage: ble_guard.py <fw_main.cpp> <cxx> [--out DIR] -- <compiler flags…>")
    cut = argv.index("--")
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("fw_main")
    ap.add_argument("cxx")
    ap.add_argument("--out", default=None)
    a = ap.parse_args(argv[1:cut])
    flags = argv[cut + 1:]
    out = a.out or tempfile.mkdtemp(prefix="mr-bleguard-")
    rc_total = 0
    src_text = open(a.fw_main, encoding="utf-8").read()
    # BOTH FAMILIES, and a refusal in EITHER stops the gate. A family whose guard cannot be extracted is never a
    # skipped section: it is an instrument refusal with a non-zero exit.
    for family, anchor, gate in (("help", REFUSAL_CALL, None), ("admin", ADMIN_REFUSAL_CALL, ADMIN_GATE)):
        try:
            cond = extract_guard(src_text, anchor, gate)
            rc, text = build_and_run(a.fw_main, a.cxx, flags, out, family=family)
        except GuardError as exc:
            print(f"BLE-GUARD REFUSED ({family}): {exc}")
            rc_total = 1
            continue
        print(f"   extracted {family} guard: " + cond)
        sys.stdout.write("".join("   " + ln + "\n" for ln in text.rstrip("\n").split("\n")))
        rc_total = rc_total or rc
    return rc_total


if __name__ == "__main__":
    sys.exit(main(sys.argv))
