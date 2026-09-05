#!/usr/bin/env python3
# MeshRoute — tools/gen_command_inventory.py
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# ★★ REMOTE-ADMIN v2 SLICE 0e-A — THE GENERATED COMMAND/SUB-COMMAND INVENTORY (R-RA-1).
#
# The owner's ruling R-RA-1 is that the authority table is DERIVED FROM SOURCE BY AN INSTRUMENT and never typed:
#     "a generator tool that derives the verb and sub-verb list from source and pins it, like the DataType checker
#      does. The open/operator/owner classification of each row is then your ruling, taken once against a complete
#      list rather than guessed per command."
# ⇒ THIS FILE PRODUCES THE COMPLETE LIST AND NOTHING ELSE. The `authority` column of every emitted row is EMPTY and
#   the generator REFUSES to run if any row carries one (`verify_rows`). 0e does not classify; the owner does, once,
#   over the generated table.
#
# ---- THE TWO COMMANDS, BOTH REAL ([[B295]], slice 0g) -----------------------------------------------------------
#     python3 tools/gen_command_inventory.py --write     regenerate the tracked inventory
#     python3 tools/gen_command_inventory.py --check     verify it byte-for-byte (identical to a bare invocation)
#   ⛔ `--check` was advertised here and in the generated footer for a whole slice while argparse never defined it,
#     so the documented verify command refused as an unknown argument. Output modes are now one mutually exclusive
#     argparse group: a conflicting pair is REFUSED, never silently resolved by precedence.
#
# ---- SLICE 0g ALSO PUBLISHES THE PRIMARY-VERB PROJECTION ---------------------------------------------------------
#   `primary_names(rows, macros)` reduces these classified rows to the console's bare primary command names for one
#   product profile. It is the completeness/feature-gate ORACLE for `src/firmware_help.h`, whose flash-resident list
#   is only a presentation copy. See the projection section below.
#
# ⛔ WHAT THIS IS NOT: it is not a `grep -c strncmp`. A token counter cannot tell a verb from a flag, cannot say which
#    function owns an arm, cannot see a `#if MR_FEAT_*` gate, and cannot notice a NEW sub-verb dispatcher appearing.
#    The model below is structural: every recognised comparison site is attributed to its enclosing FUNCTION, its
#    enclosing PREPROCESSOR GATE, and its enclosing COMMAND BLOCK CHAIN, and every scanned function must be
#    classified. An unclassified comparison site is a HARD ERROR, which is what makes a newly-added dispatcher
#    impossible to land silently.
#
# ---- THE SOURCE-SHAPE MODEL (all five shapes are exercised by tools/test_gen_command_inventory.py fixtures) -------
#   S1  !strncmp(<buf>, "lit", N)              the dispatch()/sub-verb prefix compare
#   S2  !strcmp(<buf>, "lit")                  the exact compare (handle_cfg_set keys, handle_mobile, remote_encode)
#   S3  tok_eq(<tok>, "lit")                   lib/console/console_parse.cpp's token compare (send/send_channel/…)
#   S4  preset_word_is(<t>, <n>, "lit")        src/firmware_ui_preset_verbs.h's word compare (ui preset …)
#   S5  <buf>[0] == '<c>'                      an ALIAS SPELLING of a shape-S1..S4 literal on the SAME line
#                                              (`help` / `?`; `off` / `0`). ⛔ INDEX 0 ONLY and never ' ' or '\0' —
#                                              `args[4] == ' '` / `args[N] == '\0'` are LENGTH GUARDS, not spellings,
#                                              and misreading one as an alias is exactly the defect this rule avoids.
#
# ---- LEVELS: HOW A SUB-VERB IS TOLD FROM AN ARGUMENT ------------------------------------------------------------
# A brace block adds a command LEVEL only when the condition that opened it CONTAINS a comparison. A bare scoping
# block (`{` on its own, `console_parse.cpp:263`) and a plain `if (is_channel) {` are TRANSPARENT. Consequences,
# all verified against the tree:
#   · `dispatch`'s `if (… !strncmp(line, "peers ", 6)) { … !strncmp(a, "all", 3) …}`  ⇒ verb `peers`, sub-verb `all`
#   · `handle_mobile`'s `register` block containing `scan`                            ⇒ sub-verb `register scan`
#   · `parse_command`'s bare block                                                    ⇒ `send`/`send_channel`/
#                                                                                       `send_layer` stay level 0
#   · `handle_create`'s key loop is transparent                                       ⇒ its `key=` names are level 0,
#                                                                                       exactly like handle_cfg_set's
#
# ---- BOTH COVERAGE DIRECTIONS ARE REFUSALS (the ABI probe's PINNED-table idiom, probe_board_abi.py:467-479) ------
#   (a) every SURFACES / NON_COMMAND entry MUST resolve to a real function in the real file — a rename reddens;
#   (b) every recognised comparison site MUST fall inside a classified function — a NEW dispatcher reddens;
#   (c) every surface MUST yield at least one row — a silently-emptied surface reddens;
#   (d) zero rows overall is a REFUSAL, never a PASS.
#
# ⛔ 0e LANDS NO PRODUCTION CODE. This tool only reads.
"""Generate the complete MeshRoute command/sub-command inventory from source (remote-admin v2 slice 0e-A)."""

from __future__ import annotations

import argparse
import difflib
import os
import re
import sys
from dataclasses import dataclass, field

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TRACKED_OUTPUT = "docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md"

# The comparison helpers whose calls are command tests. `\b` matters: `strncpy(` must not read as `strncmp(`.
CMP_FUNCS = ("strncmp", "strcmp", "tok_eq", "preset_word_is")
CMP_CALL_RE = re.compile(r"\b(" + "|".join(CMP_FUNCS) + r")\s*\(")
# S5: an alias spelling. Index 0 only; ' ' and '\0' are length guards and are excluded by construction.
ALIAS_CHAR_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\[\s*0\s*\]\s*==\s*'((?:[^'\\]|\\.)+)'")

FUNC_DEF_RE = re.compile(r"^(?!#)[A-Za-z_][A-Za-z0-9_:<>,&*\s\[\]]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
NOT_A_FUNC = {"if", "for", "while", "switch", "return", "else", "do", "case", "sizeof", "catch"}


@dataclass(frozen=True)
class Surface:
    """One classified command surface: a function whose comparison sites are command arms."""

    file: str
    func: str
    kind: str            # "top" | "sub" | "caller" | "remote"
    transports: str      # the transport set the arms of this surface are reachable on
    parent: str = ""     # for kind == "sub": the top-level verb this surface belongs to
    reached_from: tuple = ()   # ((file, func, symbol), …) — call sites proving the wiring claim


# ★ THE PINNED SURFACE TABLE. Its two coverage refusals (a)/(b) above are what keep it honest: an entry that no
#   longer resolves reddens, and a comparison site in no entry reddens. The `transports` claim of every entry is
#   additionally PROVEN by `reached_from`, which is checked against the real call sites — not asserted.
SURFACES = (
    # ---- surface 1: the top-level verb map -----------------------------------------------------------------
    # ⓘ THE WIRING CHAIN GAINED A HOP IN SLICE 0c AND THE CLAIM FOLLOWED THE SOURCE. Until 0c both transports called
    #   `dispatch` directly, in OPPOSITE orders relative to `parse_command`; 0c put one transport-neutral seam,
    #   `mrfw::exec_console_line`, between them, so the honest two-hop chain is: each caller calls the seam, and the
    #   seam calls the router. ⛔ The CLASSIFICATION is untouched — same surface, same kind, same transport set, same
    #   rows; only the anchors that PROVE the transport claim moved, and each hop below is still verified against the
    #   real source (a wrong hop is a hard refusal, which is how this edit was forced in the first place).
    Surface("src/firmware_commands.cpp", "dispatch", "top", "serial,ble",
            reached_from=(("src/firmware_commands.cpp", "exec_console_line", "dispatch"),
                          ("src/fw_main.cpp", "service_console", "exec_console_line"),
                          ("src/fw_main.cpp", "ble_dispatch_line", "exec_console_line"))),
    # ---- surface 1b: the console help family, extracted to a header by remote-admin v2 slice 0a -------------
    # ⚠ WHY A SECOND TOP-LEVEL SURFACE EXISTS AT ALL. Until slice 0a, `help` / `?` was one arm INSIDE `dispatch()`
    #   and this generator saw it there. 0a moved the text and the whole recognition into `src/firmware_help.h` so a
    #   probe could compile and RUN it (`test_build_src = no` keeps `firmware_commands.cpp` out of every host build),
    #   leaving `dispatch()` with a single `help_command(line, len, out)` call and no literal to scan. Scanning only
    #   `dispatch()` therefore DROPPED the `help (alias: ?)` row — a command vanishing from the authority table as an
    #   incidental result of moving text, which is exactly what 0e's refusals exist to prevent. ⇒ the generator
    #   follows the source shape to where the decision now lives.
    # ⓘ `kind="top"` is not a courtesy: `help` really is a top-level verb of the same router, and the nine
    #   `help <topic>` arms are recorded as its sub-verbs because the file's family guard (`if (!(… '?') && (len < 4
    #   || strncmp(line, "help", 4))) return false;`) prefixes them exactly the way `preset_verb`'s guard prefixes
    #   the `ui preset …` arms.
    Surface("src/firmware_help.h", "help_command", "top", "serial",
            reached_from=(("src/firmware_commands.cpp", "dispatch", "help_command"),)),
    # ---- surface 2: the sub-verb dispatchers ---------------------------------------------------------------
    Surface("src/firmware_commands.cpp", "handle_route_cmd", "sub", "serial,ble", parent="route",
            reached_from=(("src/firmware_commands.cpp", "dispatch", "handle_route_cmd"),)),
    Surface("src/firmware_commands.cpp", "handle_sleep", "sub", "serial,ble", parent="sleep",
            reached_from=(("src/firmware_commands.cpp", "dispatch", "handle_sleep"),)),
    Surface("src/firmware_commands.cpp", "handle_debug", "sub", "serial,ble", parent="debug",
            reached_from=(("src/firmware_commands.cpp", "dispatch", "handle_debug"),)),
    Surface("src/firmware_commands.cpp", "handle_factory_reset", "sub", "serial,ble", parent="factory_reset",
            reached_from=(("src/firmware_commands.cpp", "dispatch", "handle_factory_reset"),)),
    Surface("src/firmware_commands.cpp", "handle_testsched", "sub", "serial,ble", parent="testsend|testch",
            reached_from=(("src/firmware_commands.cpp", "dispatch", "handle_testsched"),)),
    Surface("src/fw_main.cpp", "handle_crashtest", "sub", "serial,ble", parent="crashtest",
            reached_from=(("src/firmware_commands.cpp", "dispatch", "fw_crashtest"),
                          ("src/fw_main.cpp", "fw_crashtest", "handle_crashtest"))),
    Surface("src/firmware_config.cpp", "handle_cfg_set", "sub", "serial,ble", parent="cfg set",
            reached_from=(("src/firmware_commands.cpp", "dispatch", "handle_cfg_set"),
                          ("src/fw_main.cpp", "ble_dispatch_line", "handle_cfg_set"))),
    Surface("src/firmware_config.cpp", "handle_team", "sub", "serial,ble", parent="team",
            reached_from=(("src/firmware_commands.cpp", "dispatch", "handle_team"),)),
    Surface("src/firmware_config.cpp", "team_forget_key", "sub", "serial,ble", parent="team forgetkey",
            reached_from=(("src/firmware_config.cpp", "handle_team", "team_forget_key"),)),
    Surface("src/firmware_config.cpp", "handle_mobile", "sub", "serial,ble", parent="mobile",
            reached_from=(("src/firmware_commands.cpp", "dispatch", "handle_mobile"),)),
    Surface("src/firmware_config.cpp", "handle_create", "sub", "serial,ble", parent="create",
            reached_from=(("src/firmware_commands.cpp", "dispatch", "handle_create"),)),
    Surface("src/firmware_config.cpp", "handle_joinprofile", "sub", "serial,ble", parent="joinprofile",
            reached_from=(("src/firmware_commands.cpp", "dispatch", "handle_joinprofile"),)),
    Surface("src/firmware_ui_preset_verbs.h", "preset_verb", "sub", "serial,ble", parent="ui",
            reached_from=(("src/firmware_commands.cpp", "handle_ui", "preset_verb"),)),
    # ---- surface 3: the caller-only arms around the execution seam -----------------------------------------
    # ⓘ Slice 0c renamed what these arms surround: they used to sit around `dispatch()` directly, and now they sit
    #   around `mrfw::exec_console_line`. What they ARE is unchanged — the per-transport envelope arms (a malformed
    #   `peerkey`/`peername`, the BLE help refusal, the companion's direct JSON verbs) that each caller still owns.
    Surface("src/fw_main.cpp", "ble_dispatch_line", "caller", "ble"),
    Surface("src/fw_main.cpp", "service_console", "caller", "serial"),
    Surface("lib/console/console_parse.cpp", "parse_command", "caller", "serial,ble",
            reached_from=(("src/firmware_commands.cpp", "exec_console_line", "parse_command"),
                          ("src/fw_main.cpp", "service_console", "exec_console_line"),
                          ("src/fw_main.cpp", "ble_dispatch_line", "exec_console_line"))),
    # ---- the legacy over-the-air remote-admin verb set (what remote-admin v2 replaces) ---------------------
    Surface("src/firmware_remote.cpp", "remote_encode", "remote", "radio(REMOTE_CMD)",
            reached_from=(("src/firmware_remote.cpp", "remote_exec", "remote_encode"),)),
    Surface("src/firmware_remote.cpp", "remote_exec", "remote", "radio(REMOTE_CMD)"),
)

# ★ THE OTHER HALF OF THE PIN. A comparison site in a function listed here is DELIBERATELY not a command row, and the
#   reason is recorded. A site in a function in NEITHER table is a hard error — that is refusal (b).
NON_COMMAND = {
    ("lib/console/console_parse.cpp", "tok_eq"):
        "the S3 comparison helper's own definition — it compares nothing but its argument",
    ("src/firmware_ui_preset_verbs.h", "preset_word_is"):
        "the S4 comparison helper's own definition",
    ("src/firmware_remote.cpp", "remote_verb_open"):
        "a POLICY predicate over verbs remote_encode already emits (spec §4: only status/routes are open); "
        "emitting it again would duplicate one semantic arm",
    ("src/firmware_remote.cpp", "admin_verb_gated"):
        "the controller-side twin of remote_verb_open — the same policy question, the same two verbs, no new arm",
}

SCAN_FILES = (
    "src/firmware_commands.cpp",
    "src/firmware_help.h",
    "src/firmware_config.cpp",
    "src/firmware_remote.cpp",
    "src/fw_main.cpp",
    "src/firmware_ui_preset_verbs.h",
    "lib/console/console_parse.cpp",
)

# ★ THE REAL PRODUCT PROFILE MATRIX — the resolved macro set of at least one REAL board env. Moved here from the
#   RETIRED `tools/probe_console_sink/help_manifest.py` by slice 0g so that ONE table serves the generator, the
#   console-sink gate and its wrapper; `tools/probe_console_sink/run.sh` builds exactly these six and its wrapper
#   asserts the two lists name the same builds. ⛔ `native` is deliberately absent: `platformio.ini`'s
#   `test_build_src = no` means no native target compiles `src/`.
PROFILES = {
    #  name                MR_N_LAYERS  MR_FEAT_MOBILE  MR_FEAT_REMOTE_MGMT  MR_FEAT_OLED
    "full_oled":      dict(MR_N_LAYERS=1, MR_FEAT_MOBILE=1, MR_FEAT_REMOTE_MGMT=1, MR_FEAT_OLED=1),
    "full_headless":  dict(MR_N_LAYERS=1, MR_FEAT_MOBILE=1, MR_FEAT_REMOTE_MGMT=1, MR_FEAT_OLED=0),
    "gateway":        dict(MR_N_LAYERS=2, MR_FEAT_MOBILE=0, MR_FEAT_REMOTE_MGMT=1, MR_FEAT_OLED=0),
    "gateway_oled":   dict(MR_N_LAYERS=2, MR_FEAT_MOBILE=0, MR_FEAT_REMOTE_MGMT=1, MR_FEAT_OLED=1),
    "mobile":         dict(MR_N_LAYERS=1, MR_FEAT_MOBILE=1, MR_FEAT_REMOTE_MGMT=0, MR_FEAT_OLED=0),
    "mobile_oled":    dict(MR_N_LAYERS=1, MR_FEAT_MOBILE=1, MR_FEAT_REMOTE_MGMT=0, MR_FEAT_OLED=1),
}
PROFILE_ENVS = {
    "full_oled":     ("heltec_v3", "heltec_v4"),
    "full_headless": ("xiao_sx1262", "xiao_esp32s3", "production"),
    "gateway":       ("gateway", "gateway_esp32s3"),
    "gateway_oled":  ("gateway_heltec", "gateway_heltec_v4"),
    "mobile":        ("xiao_mobile", "xiao_esp32s3_mobile"),
    "mobile_oled":   ("heltec_mobile", "heltec_v4_mobile"),
}


class GeneratorError(RuntimeError):
    """A refusal. Never a warning: a coverage hole must stop the gate, not colour it."""


# ---------------------------------------------------------------------------------------------------------------
# Lexing helpers
# ---------------------------------------------------------------------------------------------------------------

def _blank_comments_and_literals(text: str) -> str:
    """Return `text` with // and /* */ comments and string/char literal BODIES blanked, lengths preserved.

    Length preservation matters: every offset computed on the blanked text indexes the original text unchanged.
    """
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            out[i] = out[i + 1] = " "
            i += 2
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = " "
                if i + 1 < n:
                    out[i + 1] = " "
                i += 2
        elif c in "\"'":
            quote = c
            i += 1
            while i < n and text[i] != quote:
                if text[i] == "\\":
                    out[i] = " "
                    i += 1
                    if i < n:
                        out[i] = " "
                        i += 1
                    continue
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                i += 1
        else:
            i += 1
    return "".join(out)


def _c_string_literals(text: str) -> list:
    """Every C string literal in `text`, as (start_offset, decoded_value)."""
    res = []
    for m in re.finditer(r'"((?:[^"\\\n]|\\.)*)"', text):
        res.append((m.start(), _decode_c(m.group(1))))
    return res


def _decode_c(raw: str) -> str:
    return (raw.replace("\\\\", "\x00ESC\x00").replace('\\"', '"').replace("\\n", "\n")
            .replace("\\t", "\t").replace("\\r", "\r").replace("\\0", "\0")
            .replace("\x00ESC\x00", "\\"))


def _match_paren(blank: str, open_idx: int) -> int:
    """Index just past the ')' matching the '(' at `open_idx`, or -1."""
    depth = 0
    for i in range(open_idx, len(blank)):
        if blank[i] == "(":
            depth += 1
        elif blank[i] == ")":
            depth -= 1
            if depth == 0:
                return i + 1
    return -1


@dataclass
class Site:
    file: str
    line: int
    func: str
    gate: str
    chain: tuple           # the enclosing command tokens, outermost first
    literals: tuple        # canonical spelling first, then alias spellings
    shapes: tuple


@dataclass
class Row:
    verb: str
    subverb: str
    func: str
    transports: str
    gate: str
    source: str
    authority: str = ""
    surface: str = ""
    shapes: tuple = field(default_factory=tuple)

    def key(self):
        return (self.surface, self.verb, self.subverb, self.func, self.source)


# ---------------------------------------------------------------------------------------------------------------
# The scanner
# ---------------------------------------------------------------------------------------------------------------

def _gate_expr(directive: str, rest: str) -> str:
    rest = rest.strip()
    rest = re.sub(r"\s*//.*$", "", rest)
    rest = re.sub(r"\s*/\*.*$", "", rest).strip()
    if directive == "ifdef":
        return "defined(%s)" % rest
    if directive == "ifndef":
        return "!defined(%s)" % rest
    return rest


def _negate(expr: str) -> str:
    if expr.startswith("!(") and expr.endswith(")"):
        return expr[2:-1]
    return "!(%s)" % expr


def scan_file(root: str, rel: str) -> list:
    """Every recognised comparison site in `rel`, with function, gate and command chain."""
    path = os.path.join(root, rel)
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()
    lines = text.split("\n")
    blank_lines = _blank_comments_and_literals(text).split("\n")

    sites = []
    value_sites = []
    retest_sites = []
    depth = 0
    func = None
    func_depth = None
    gate_stack = []          # (expr, taken_else) per open #if
    chain = []               # (depth_at_open, token)

    i = 0
    while i < len(lines):
        raw = lines[i]
        blank = blank_lines[i]
        stripped = raw.lstrip()

        # ---- preprocessor gates (file-global, so a gated arm is ALWAYS present in the table, never compiled away)
        pp = re.match(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$", raw)
        if pp:
            directive, rest = pp.group(1), pp.group(2)
            if directive in ("if", "ifdef", "ifndef"):
                gate_stack.append([_gate_expr(directive, rest), []])
            elif directive == "elif":
                if gate_stack:
                    gate_stack[-1][1].append(gate_stack[-1][0])
                    gate_stack[-1][0] = _gate_expr("if", rest)
            elif directive == "else":
                if gate_stack:
                    prior = gate_stack[-1][1] + [gate_stack[-1][0]]
                    gate_stack[-1][0] = " && ".join(_negate(p) for p in prior)
            elif directive == "endif":
                if gate_stack:
                    gate_stack.pop()
            i += 1
            continue

        # ---- join a comparison call that spans lines (paren-balanced), so no arm is lost to wrapping
        span_last = i
        work_raw, work_blank = raw, blank
        if CMP_CALL_RE.search(blank) and work_blank.count("(") > work_blank.count(")"):
            j = i
            while j + 1 < len(lines) and work_blank.count("(") > work_blank.count(")"):
                j += 1
                work_raw += "\n" + lines[j]
                work_blank += "\n" + blank_lines[j]
            span_last = j

        literals, shapes, values = _arms_on_line(work_raw, work_blank)

        # ---- function bodies. Detected BEFORE the site is recorded, so a SINGLE-LINE body
        #      (`static bool remote_verb_open(const char* v) { return !strcmp(v, "status") || …; }`) attributes its
        #      own arms to itself instead of falling outside every function.
        code = "\n".join(blank_lines[i:span_last + 1])
        if func is None:
            m = FUNC_DEF_RE.match(raw)
            if m and m.group(1) not in NOT_A_FUNC and "{" in code and not code.rstrip().endswith(";"):
                func = m.group(1)
                func_depth = depth

        low_depth, new_depth = _brace_profile(code, depth)
        # ---- a block closed on this line ends its token's scope BEFORE this line's own arm is attributed
        while chain and chain[-1][0] >= low_depth:
            chain.pop()

        gate_now = " && ".join(g[0] for g in gate_stack) if gate_stack else "—"
        if literals and func is not None:
            # ★ A RE-TEST IS NOT A SECOND ARM. `remote_exec`'s reboot/prep-restart arm re-asks `!strcmp(v, "reboot")`
            #   twice inside its own block to pick the reply text and the action code. Those literals are a SUBSET of
            #   the arm that opened the block, so they are the same command, not `reboot reboot`.
            if chain and set(literals) <= set(chain[-1][2]):
                retest_sites.append((rel, i + 1, func, " ".join(literals), chain[-1][1]))
            else:
                sites.append(Site(
                    file=rel, line=i + 1, func=func, gate=gate_now,
                    chain=tuple(c[1] for c in chain),
                    literals=tuple(literals), shapes=tuple(sorted(set(shapes)))))
                for buf, val in values:
                    value_sites.append((rel, i + 1, func, buf, val))
        elif literals and func is None:
            raise GeneratorError(
                "%s:%d: comparison site outside any function body — the scanner's function model is wrong, "
                "which would silently drop arms" % (rel, i + 1))

        # ---- command levels: a block adds one only when a comparison opened it ------------------------------
        if literals and new_depth > low_depth and func is not None:
            chain.append((low_depth, literals[0], tuple(literals)))
        # ★ THE GUARD SHAPE. `preset_verb` does not open a block for the family word; it REJECTS everything else:
        #       if (!a.word(t, n) || !preset_word_is(t, n, "preset")) return false;
        #   Every arm below it is therefore reachable only under `preset`, and reading them as siblings would have
        #   published `ui list` / `ui set` — grammars that do not exist. A NEGATED comparison in an `if (…) return …;`
        #   with NO brace on the line prefixes the rest of the function body.
        #   ⛔ The "no brace" test is load-bearing: `dispatch`'s arms are `if (…) { …; return true; }` on one line,
        #      which is an ARM, not a guard, and must not prefix everything after it.
        elif (literals and func is not None and "{" not in code
                and re.search(r"\breturn\b", code) and re.match(r"^\s*if\s*\(", code)
                and _has_nomatch_polarity(code)):
            chain.append((func_depth, literals[0], tuple(literals)))

        depth = new_depth
        if func is not None and depth <= func_depth:
            func = None
            func_depth = None
            chain = []
        i = span_last + 1

    if gate_stack:
        raise GeneratorError("%s: unbalanced preprocessor conditionals at EOF" % rel)
    return sites, value_sites, retest_sites


SHAPE_OF = {"strncmp": "S1", "strcmp": "S2", "tok_eq": "S3", "preset_word_is": "S4"}
# ★ POLARITY, and why it cannot be read off a leading `!`. `strcmp`/`strncmp` return ZERO on a match, so `!strncmp(…)`
#   is the idiom for "MATCHES"; `tok_eq`/`preset_word_is` return a bool, so `!tok_eq(…)` is "DOES NOT MATCH". Treating
#   every `!` as a negation read `ble_dispatch_line`'s `if (… !strncmp(line, "peers ", 6)) return …;` as a family GUARD
#   and prefixed every later arm of that function with `peers`.
ZERO_IS_MATCH = ("strncmp", "strcmp")


def _brace_profile(code: str, start: int):
    """(minimum, final) brace depth across `code`, starting at `start`.

    ⛔ A net delta is NOT enough. `handle_route_cmd`'s `} else if (!strncmp(args, "del", 3) …) {` has delta 0, so a
       net-delta model never closed the `add` block and published the sub-verb `route add del`. The MINIMUM is what
       says a block ended mid-line; the FINAL says a new one opened.
    """
    depth = start
    low = start
    for ch in code:
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            low = min(low, depth)
    return low, depth


def _has_nomatch_polarity(code: str) -> bool:
    """True when some comparison on this line is tested in its DOES-NOT-MATCH sense (a family guard)."""
    for m in CMP_CALL_RE.finditer(code):
        bang = bool(re.search(r"!\s*$", code[:m.start()]))
        if (m.group(1) not in ZERO_IS_MATCH) == bang:
            return True
    return False


def _arms_on_line(raw: str, blank: str):
    """Split one (possibly joined) line's comparisons into ONE arm plus its argument-value comparisons.

    ★ THE BUFFER IS WHAT SEPARATES AN ALIAS FROM A VALUE. `handle_cfg_set`'s
      `else if (!strcmp(key, "e2e_dm")) { b.e2e_dm = (… || !strcmp(val, "on") || !strcmp(val, "true")) …; }`
      tests the KEY once and the VALUE twice. Grouping every literal on the line would have published a command
      `cfg set e2e_dm (alias: on, true)` — two spellings that are not spellings of the command at all. So the arm is
      the literals compared against the FIRST buffer seen; literals compared against any other buffer are
      argument-value comparisons, returned separately and listed in the generated file rather than dropped silently.
    Returns (arm_literals, shapes, value_tokens) with the canonical spelling first.
    """
    per_buffer, shapes_by_buf, order = {}, {}, []
    lits = _c_string_literals(raw)
    for m in CMP_CALL_RE.finditer(blank):
        fn = m.group(1)
        open_idx = blank.index("(", m.start())
        close = _match_paren(blank, open_idx)
        if close < 0:
            continue
        args_blank = blank[open_idx + 1:close - 1]
        base = open_idx + 1
        first = args_blank.split(",")[0].strip()
        mb = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)$", first)
        buf = mb.group(1) if mb else "<expr>"
        for off, val in lits:
            if base <= off < close - 1:
                if buf not in per_buffer:
                    per_buffer[buf], shapes_by_buf[buf] = [], []
                    order.append(buf)
                if val not in per_buffer[buf]:
                    per_buffer[buf].append(val)
                    shapes_by_buf[buf].append(SHAPE_OF[fn])
    if not order:
        return [], [], []
    arm_buf = order[0]
    literals, shapes = list(per_buffer[arm_buf]), list(shapes_by_buf[arm_buf])
    values = [(b, v) for b in order[1:] for v in per_buffer[b]]

    for am in ALIAS_CHAR_RE.finditer(blank):
        # ⛔ The STRUCTURE is matched on the blanked text (so a commented-out arm cannot register) but the CHARACTER
        #    itself is read back from `raw` at the same offsets — `_blank_comments_and_literals` keeps the quotes and
        #    blanks the body, so `am.group(2)` is whitespace and would silently alias every arm to " ".
        buf = am.group(1)
        ch = _decode_c(raw[am.start(2):am.end(2)])
        if buf == arm_buf and ch not in (" ", "\0") and ch not in literals:
            literals.append(ch)
            shapes.append("S5")
    return literals, shapes, values


# ---------------------------------------------------------------------------------------------------------------
# Classification and row synthesis
# ---------------------------------------------------------------------------------------------------------------

def _function_spans(root: str, rel: str) -> dict:
    """name -> (first_line, last_line) for every top-level function body in `rel`."""
    path = os.path.join(root, rel)
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()
    lines = text.split("\n")
    blank_lines = _blank_comments_and_literals(text).split("\n")
    spans, depth, func, fdepth, start = {}, 0, None, None, None
    for i, raw in enumerate(lines, 1):
        code = blank_lines[i - 1]
        if func is None:
            m = FUNC_DEF_RE.match(raw)
            if m and m.group(1) not in NOT_A_FUNC and "{" in code and not code.rstrip().endswith(";"):
                func, fdepth, start = m.group(1), depth, i
        depth += code.count("{") - code.count("}")
        if func is not None and depth <= fdepth:
            spans.setdefault(func, (start, i))
            func, fdepth, start = None, None, None
    return spans


def _normalize(tok: str) -> str:
    """A verb's inventory spelling: the source literal with its trailing separator space removed."""
    return tok.rstrip(" ") if tok.strip(" ") else tok


def _alias_note(literals) -> str:
    """Alias spellings of ONE semantic arm, recorded in the cell — never as a second row (0e-A, verbatim)."""
    extra = [_normalize(x) for x in literals[1:]]
    return " (alias: %s)" % ", ".join(extra) if extra else ""


def build_rows(root: str) -> tuple:
    surfaces = {(s.file, s.func): s for s in SURFACES}
    all_spans = {rel: _function_spans(root, rel) for rel in SCAN_FILES}

    # ---- refusal (a): every pinned entry must resolve to a real function --------------------------------------
    for (rel, func) in list(surfaces) + list(NON_COMMAND):
        if rel not in all_spans:
            raise GeneratorError("pinned entry %s::%s names a file outside SCAN_FILES" % (rel, func))
        if func not in all_spans[rel]:
            raise GeneratorError(
                "pinned entry %s::%s no longer resolves to a function in the real source — a rename or removal "
                "must not leave this gate green" % (rel, func))

    # ---- the wiring claim: every reached_from call site must really exist -------------------------------------
    for s in SURFACES:
        for (crel, cfunc, symbol) in s.reached_from:
            if crel not in all_spans or cfunc not in all_spans[crel]:
                raise GeneratorError("%s::%s claims a caller %s::%s that does not exist"
                                     % (s.file, s.func, crel, cfunc))
            lo, hi = all_spans[crel][cfunc]
            with open(os.path.join(root, crel), "r", encoding="utf-8") as fh:
                body = _blank_comments_and_literals(fh.read()).split("\n")[lo - 1:hi]
            if not re.search(r"\b%s\s*\(" % re.escape(symbol), "\n".join(body)):
                raise GeneratorError(
                    "%s::%s claims transport set %r via %s::%s, but no call to `%s` exists there — the transport "
                    "column would be an assertion instead of a measurement"
                    % (s.file, s.func, s.transports, crel, cfunc, symbol))

    rows, notes, values, retests, per_surface = [], [], [], [], {}
    for rel in SCAN_FILES:
        sites, value_sites, retest_sites = scan_file(root, rel)
        for (vrel, vline, vfunc, vbuf, vval) in value_sites:
            if (vrel, vfunc) in surfaces:
                values.append((vrel, vline, vfunc, vbuf, vval))
        for (rrel, rline, rfunc, rlits, rarm) in retest_sites:
            if (rrel, rfunc) in surfaces:
                retests.append((rrel, rline, rfunc, rlits, rarm))
        for site in sites:
            key = (site.file, site.func)
            if key in NON_COMMAND:
                notes.append((site.file, site.line, site.func, NON_COMMAND[key]))
                continue
            if key not in surfaces:
                # ---- refusal (b): a NEW dispatcher can never land silently -----------------------------------
                raise GeneratorError(
                    "%s:%d: comparison site in unclassified function `%s`. Every command surface must be pinned in "
                    "SURFACES (with its transport wiring) or excluded in NON_COMMAND (with a reason). Refusing to "
                    "emit an inventory that would silently omit it."
                    % (site.file, site.line, site.func))
            s = surfaces[key]
            chain = [_normalize(t) for t in site.chain]
            token = _normalize(site.literals[0]) + _alias_note(site.literals)
            if s.kind == "sub":
                verb, sub = s.parent, " ".join(chain + [token])
            else:
                parts = chain + [token]
                verb, sub = parts[0], " ".join(parts[1:]) or "—"
            row = Row(verb=verb, subverb=sub, func=site.func, transports=s.transports,
                      gate=site.gate, source="%s:%d" % (site.file, site.line),
                      surface="%s::%s" % (s.file, s.func), shapes=site.shapes)
            rows.append(row)
            per_surface.setdefault(key, []).append(row)

    # ---- refusal (c): no surface may go empty -------------------------------------------------------------
    for s in SURFACES:
        if not per_surface.get((s.file, s.func)):
            raise GeneratorError(
                "surface %s::%s produced NO rows. A dispatcher emptied of its arms must redden, not pass."
                % (s.file, s.func))
    # ---- refusal (d): zero rows is never a PASS -----------------------------------------------------------
    if not rows:
        raise GeneratorError("generation produced zero rows — refusing to report success")

    verify_rows(rows)
    rows.sort(key=lambda r: r.key())
    return rows, notes, values, retests


def verify_rows(rows) -> None:
    """Provenance and the empty-authority invariant. Both are STOP conditions of slice 0e."""
    seen = {}
    for r in rows:
        for name, val in (("verb", r.verb), ("owning function", r.func),
                          ("transport set", r.transports), ("feature gate", r.gate),
                          ("file:line", r.source), ("sub-verb", r.subverb)):
            if not val or not str(val).strip():
                raise GeneratorError("row %r has no %s — every row must carry full provenance" % (r.source, name))
        if r.authority != "":
            raise GeneratorError(
                "row %s carries an authority classification (%r). 0e generates the COMPLETE LIST; the open/"
                "operator/owner ruling is the owner's, taken once over it (R-RA-1)." % (r.source, r.authority))
        if not re.match(r"^[^:]+:[0-9]+$", r.source):
            raise GeneratorError("row %r has no file:line provenance" % r.source)
        k = r.key()
        if k in seen:
            raise GeneratorError(
                "duplicate normalized row %r — one semantic arm must appear exactly once (aliases are recorded "
                "in the verb cell, never as a second row)" % (k,))
        seen[k] = True


# ---------------------------------------------------------------------------------------------------------------
# The primary-verb projection (remote-admin v2 slice 0g)
# ---------------------------------------------------------------------------------------------------------------
# ★★ THE OWNER RULED (2026-09-05) that console help becomes a BARE PRIMARY-VERB INDEX and that THIS GENERATOR is its
#   completeness and feature-gate oracle: "every primary verb the build compiles appears EXACTLY once, under the same
#   feature gate as its dispatch arm, and nothing else appears".
# ⛔ IT IS A PROJECTION OF THE ROWS ABOVE, NEVER A SECOND SCAN. Re-parsing C++ here would fork the classification the
#   whole file exists to keep single, and an allow-list of "real" commands would be the hand-maintained table R-RA-1
#   forbids. Every rule below is a reduction of `Row` fields that the scanner already derived from source.
#
# WHICH SURFACES INTRODUCE A PRIMARY CONSOLE VERB:
#   · kind "top"    — the two halves of the top-level verb map (`dispatch` + the extracted `help_command`).
#   · `parse_command` — Surface 3's CONSOLE TOKEN PARSER. Its seven names (`send`, `send_channel`, `send_layer`,
#     `peerkey`, `peername`, `reqpubkey`, `resolve`) are consumed there and NEVER reach `dispatch()`, so a projection
#     over Surface 1 alone silently loses them (42 instead of 49 on a full build).
#   · every other caller is excluded: `service_console`/`ble_dispatch_line` only re-test lines another surface owns,
#     so counting them would double-count `peerkey` and import BLE-only arms into a serial index.
_ALIAS_NOTE_RE = re.compile(r"^(?P<base>.*?)\s*\(alias:\s*(?P<aliases>[^)]*)\)\s*$")
# A primary NAME is a word: `?` is punctuation and is the alias spelling of `help`, never a second command.
_WORD_NAME_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_.\-]*\Z")
_COND_TOKEN_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*|\d+|&&|\|\||[!()<>=]=?|.")


def eval_gate(expr: str, macros: dict) -> bool:
    """Evaluate one recorded `#if` expression under `macros`. Fail loud outside the supported subset."""
    if not expr.strip():
        raise GeneratorError("empty gate expression")
    py = []
    for tok in _COND_TOKEN_RE.findall(expr):
        if tok.isspace():
            continue
        if tok == "&&":
            py.append(" and ")
        elif tok == "||":
            py.append(" or ")
        elif tok == "!":
            py.append(" not ")
        elif tok in ("(", ")", "<", ">", "<=", ">=", "==", "!="):
            py.append(tok)
        elif tok.isdigit():
            py.append(tok)
        elif re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", tok):
            if tok == "defined":
                raise GeneratorError("`defined()` is outside this evaluator's subset: " + expr)
            # An undefined macro is 0 in a real #if — but here that would silently DROP a command from the expected
            # help list, so REFUSE: every macro a dispatch arm is gated on must be a named axis of the matrix.
            if tok not in macros:
                raise GeneratorError("gate %r names macro %r which is not a profile axis" % (expr, tok))
            py.append(str(int(macros[tok])))
        else:
            raise GeneratorError("gate %r contains unsupported token %r" % (expr, tok))
    return bool(eval("".join(py)))   # noqa: S307 — the token filter above is the whitelist


def _spellings(verb: str):
    """Every independently accepted spelling recorded in one verb cell: `a (alias: b)` -> [a, b]; `a|b` -> [a, b]."""
    m = _ALIAS_NOTE_RE.match(verb)
    base, extra = (m.group("base"), m.group("aliases").split(",")) if m else (verb, [])
    out = []
    for spelling in [base] + list(extra):
        for piece in spelling.split("|"):
            piece = piece.strip()
            if piece:
                out.append(piece)
    return out


def is_primary_surface(surface) -> bool:
    """Does this Surface introduce top-level console verbs? (see the block comment above)"""
    if surface.kind == "top":
        return True
    return surface.kind == "caller" and surface.func == "parse_command"


def primary_projection(rows, macros: dict) -> dict:
    """-> {primary name: [(file:line, gate), ...]} for ONE product profile.

    The eight reduction rules, in order: serial-only, primary surfaces, gate evaluation, multiword -> first token,
    alias expansion, generator-annotation canonicalization, de-duplication AFTER gating, and (in the caller) a
    bytewise sort.
    """
    primary = {(s.file, s.func) for s in SURFACES if is_primary_surface(s)}
    names = {}
    for r in rows:
        if "serial" not in [t.strip() for t in r.transports.split(",")]:
            continue                                          # (1) the serial dispatcher only
        if tuple(r.surface.split("::", 1)) not in primary:
            continue                                          # (2) top-level + the console token parser
        if r.gate != "—" and not eval_gate(r.gate, macros):
            continue                                          # (3) this build does not compile the arm
        for spelling in _spellings(r.verb):                   # (5)(6) aliases and `a|b`, canonicalized
            name = spelling.split()[0]                        # (4) `cfg set` -> `cfg`
            if not _WORD_NAME_RE.match(name):
                continue                                      # (5) `?` is help's spelling, not a command
            names.setdefault(name, []).append((r.source, r.gate))   # (7) de-duplicated by dict key
    if not names:
        raise GeneratorError(
            "the primary-verb projection is EMPTY for macros %r — a build with no console commands is not a "
            "result, it is a broken projection" % (macros,))
    return names


def primary_names(rows, macros: dict) -> list:
    """The bytewise-sorted unique primary command names of one profile — (8), and the help index's exact order."""
    return sorted(primary_projection(rows, macros), key=lambda n: n.encode("utf-8"))


# ---------------------------------------------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------------------------------------------

def _md_escape(s: str) -> str:
    return s.replace("|", "\\|")


def render(rows, notes, values, retests) -> str:
    kinds = {s.kind: None for s in SURFACES}
    titles = {"top": "Surface 1 — top-level `dispatch()` verbs",
              "sub": "Surface 2 — sub-verb dispatchers",
              "caller": "Surface 3 — caller-only arms around `dispatch()`",
              "remote": "Legacy over-the-air remote-admin verbs (what v2 replaces)"}
    out = []
    out.append("<!-- GENERATED BY tools/gen_command_inventory.py — DO NOT EDIT BY HAND. -->")
    out.append("<!-- Regenerate: python3 tools/gen_command_inventory.py --write ; verify: --check -->")
    out.append("# MeshRoute command/sub-command inventory — remote-admin v2 slice 0e-A (R-RA-1)")
    out.append("")
    out.append("Derived from source by `tools/gen_command_inventory.py`. **The `authority` column is deliberately "
               "empty on every row**: 0e produces the complete list; the open/operator/owner classification is the "
               "owner's single ruling taken over it (R-RA-1).")
    out.append("")
    out.append("Alias spellings of one semantic arm are recorded in the verb/sub-verb cell as `(alias: …)`; they are "
               "never a second row. A `#if`-gated arm is present with its exact macro rather than disappearing under "
               "the host's current flags.")
    out.append("")
    out.append("Total rows: **%d**." % len(rows))
    out.append("")
    for kind in kinds:
        subset = [r for r in rows if any(s.kind == kind and "%s::%s" % (s.file, s.func) == r.surface
                                         for s in SURFACES)]
        if not subset:
            continue
        out.append("## %s" % titles[kind])
        out.append("")
        out.append("| verb | sub-verb | owning function | transports | feature gate | file:line | authority |")
        out.append("| --- | --- | --- | --- | --- | --- | --- |")
        for r in subset:
            out.append("| `%s` | %s | `%s` | %s | %s | `%s` | %s |" % (
                _md_escape(r.verb),
                ("`%s`" % _md_escape(r.subverb)) if r.subverb != "—" else "—",
                r.func, _md_escape(r.transports),
                ("`%s`" % _md_escape(r.gate)) if r.gate != "—" else "—",
                r.source, ""))
        out.append("")
    out.append("## Recognised-but-excluded comparison sites")
    out.append("")
    out.append("Sites in classified non-command functions. Listing them is the other half of the coverage pin: a "
               "site in a function in neither table is a hard error.")
    out.append("")
    out.append("| file:line | function | reason |")
    out.append("| --- | --- | --- |")
    for rel, line, func, reason in sorted(notes):
        out.append("| `%s:%d` | `%s` | %s |" % (rel, line, func, reason))
    out.append("")
    out.append("## Argument-value comparisons inside command surfaces")
    out.append("")
    out.append("Literals a command arm tests against a **different buffer** than the one that selects the arm "
               "(`cfg set e2e_dm` testing its `val` for `on`/`true`). They are values, not commands, so they are "
               "not rows — and they are listed here so nothing recognised is silently dropped.")
    out.append("")
    out.append("| file:line | function | buffer | value |")
    out.append("| --- | --- | --- | --- |")
    for rel, line, func, buf, val in sorted(values):
        out.append("| `%s:%d` | `%s` | `%s` | `%s` |" % (rel, line, func, buf, _md_escape(val)))
    out.append("")
    out.append("## Re-tests of an already-open arm")
    out.append("")
    out.append("A comparison whose literals are a subset of the arm that opened its enclosing block "
               "(`remote_exec` re-asking `!strcmp(v, \"reboot\")` inside the reboot/prep-restart arm). One command, "
               "not two — recorded here rather than emitted as a row.")
    out.append("")
    out.append("| file:line | function | re-tested | enclosing arm |")
    out.append("| --- | --- | --- | --- |")
    for rel, line, func, lits, arm in sorted(retests):
        out.append("| `%s:%d` | `%s` | `%s` | `%s` |" % (rel, line, func, _md_escape(lits), _md_escape(arm)))
    out.append("")
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------------------------------------------

def generate(root: str):
    """(rendered inventory, command rows). The row list is the count every caller should quote."""
    rows, notes, values, retests = build_rows(root)
    return render(rows, notes, values, retests), rows


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", default=REPO_ROOT, help="repository root to scan")
    ap.add_argument("--out", default=None, help="tracked output path (default: %s)" % TRACKED_OUTPUT)
    # ★ [[B295]]: ONE mutually exclusive group over the output modes. `--check` is an explicit spelling of the
    #   DEFAULT verification — the same code path, the same verdict, the same exit status — and `--check --write`,
    #   `--check --stdout` and `--write --stdout` are refused by argparse rather than one silently taking precedence.
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true",
                      help="verify the tracked inventory byte-for-byte (the default when no mode is given)")
    mode.add_argument("--write", action="store_true", help="write the generated inventory to the tracked path")
    mode.add_argument("--stdout", action="store_true", help="print the generated inventory instead of checking")
    mode.add_argument("--primary", metavar="PROFILE",
                      help="print one product profile's bare primary command names (%s)" % ", ".join(PROFILES))
    args = ap.parse_args(argv)
    out_path = args.out or os.path.join(args.root, TRACKED_OUTPUT)

    try:
        text, rows = generate(args.root)
    except GeneratorError as exc:
        print("gen_command_inventory: REFUSED: %s" % exc, file=sys.stderr)
        return 2

    if args.primary is not None:
        if args.primary not in PROFILES:
            print("gen_command_inventory: REFUSED: unknown profile %r (known: %s)"
                  % (args.primary, ", ".join(PROFILES)), file=sys.stderr)
            return 2
        try:
            names = primary_names(rows, PROFILES[args.primary])
        except GeneratorError as exc:
            print("gen_command_inventory: REFUSED: %s" % exc, file=sys.stderr)
            return 2
        for name in names:
            print(name)
        return 0
    if args.stdout:
        sys.stdout.write(text)
        return 0
    if args.write:
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        with open(out_path, "w", encoding="utf-8") as fh:
            fh.write(text)
        print("gen_command_inventory: wrote %s (%d command rows)" % (out_path, len(rows)))
        return 0

    # ---- the verification, reached by a bare invocation AND by `--check`: ONE path, so the two cannot diverge ----
    if not os.path.exists(out_path):
        print("gen_command_inventory: FAIL: tracked inventory %s is missing" % out_path, file=sys.stderr)
        return 1
    with open(out_path, "r", encoding="utf-8") as fh:
        have = fh.read()
    if have != text:
        diff = difflib.unified_diff(have.split("\n"), text.split("\n"),
                                    "tracked", "generated", lineterm="")
        print("gen_command_inventory: FAIL: tracked inventory is stale", file=sys.stderr)
        for ln in list(diff)[:80]:
            print(ln, file=sys.stderr)
        return 1
    print("gen_command_inventory: PASS: %s matches fresh generation byte-for-byte (%d command rows)"
          % (out_path, len(rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
