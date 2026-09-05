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
"""Extract the real BLE help-refusal guard from src/fw_main.cpp and EXECUTE it against the real help router."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

REFUSAL_CALL = 'write_err(out, cap, "help", "console_only")'


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


def extract_guard(fw_main_text: str) -> str:
    """The exact condition text of the `if` that answers `console_only`. Fail loud on anything unexpected."""
    blank = _blank_comments(fw_main_text)
    hits = [m.start() for m in re.finditer(re.escape(REFUSAL_CALL), blank)]
    if len(hits) != 1:
        raise GuardError(f"expected exactly ONE executable {REFUSAL_CALL!r} in fw_main.cpp, found {len(hits)}")
    at = hits[0]
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
    if "help" not in cond:
        raise GuardError(f"the extracted condition does not test `help`: {cond!r}")
    return cond


# ---------------------------------------------------------------------------------------------------------------
# The corpus. ⛔ SYSTEMATIC, not a handful of favourites: every owner topic in four shapes, plus the near-misses a
#   prefix bug would leak, plus lines that must stay OUT of the help family entirely.
# ---------------------------------------------------------------------------------------------------------------
TOPICS = ("messaging", "identity", "mobile", "inbox", "diagnostics", "remote", "test", "provisioning", "cfg")

def corpus() -> list:
    """-> [(line, must_be_refused)] — the SECOND element is the owner's rule, not the guard's opinion."""
    rows = [("help", True), ("?", True), ("help ", True), ("help  ", True)]
    for t in TOPICS:
        rows.append((f"help {t}", True))          # the real form
        rows.append((f"help  {t}", True))         # leading spaces — the `peers ` idiom still selects it
        rows.append((f"help {t} x", True))        # malformed tail — still a help line, still refused
        rows.append((f"help {t}x", True))         # a prefix near-miss — still begins with the token `help `
    rows += [("help zzz", True), ("help MESSAGING", True), ("help -h", True)]
    # ...and the lines that are NOT help at all: the guard must let them through, and the router must not own them.
    rows += [("helpful", False), ("helper x", False), ("hel", False), ("h", False), ("HELP", False),
             ("?x", False), ("? messaging", False), ("status", False), ("peers all", False), ("", False)]
    return rows


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


def build_and_run(fw_main_path: str, cxx: str, flags: list, out_dir: str, tag: str = "real") -> tuple:
    """-> (returncode, stdout). Raises GuardError if the guard cannot be extracted."""
    with open(fw_main_path, "r", encoding="utf-8") as fh:
        cond = extract_guard(fh.read())
    rows = "\n".join('        { %s, %d },' % (_c_lit(line), 1 if must else 0) for line, must in corpus())
    src = TU.replace("__GUARD__", cond).replace("__ROWS__", rows)
    src = src.replace("#include <string>", "#include <string>\n#include <algorithm>")
    tu = os.path.join(out_dir, f"ble_guard_{tag}.cpp")
    binary = os.path.join(out_dir, f"ble_guard_{tag}.bin")
    with open(tu, "w", encoding="utf-8") as fh:
        fh.write(src)
    b = subprocess.run([cxx, *flags, tu, "-o", binary], capture_output=True, text=True)
    if b.returncode != 0:
        return 2, "BLE-GUARD BUILD FAILED\n" + b.stderr
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
    try:
        rc, text = build_and_run(a.fw_main, a.cxx, flags, out)
    except GuardError as exc:
        print(f"BLE-GUARD REFUSED: {exc}")
        return 1
    print("   extracted guard: " + extract_guard(open(a.fw_main, encoding="utf-8").read()))
    sys.stdout.write("".join("   " + ln + "\n" for ln in text.rstrip("\n").split("\n")))
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
