#!/usr/bin/env python3
# MeshRoute — tools/probe_console_sink/help_manifest.py
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# ★★ THE HELP CONTENT-MULTISET AUTHORITY for remote-admin v2 slice 0a (B208's bounded help-topic split).
#
# WHY IT EXISTS. 0a splits one 89-line `dump_help()` dump into a compact index plus nine `help <topic>` sections. The
# thing that can silently go wrong is not the router — it is a help LINE quietly disappearing, appearing twice, or
# surviving under a different `#if` than the command it documents. `git diff` cannot answer that: the lines MOVE, so
# every one of them shows as -/+ and a genuinely deleted line looks exactly like a moved one.
# ⇒ the no-loss proof is a MULTISET COMPARISON, per real product profile, between
#     (a) the PRE-SLICE `dump_help()` body, rendered for that profile's macros  — `legacy` mode below, and
#     (b) what the compiled `src/firmware_help.h` ACTUALLY EMITS for that profile — printed by the probe binary
#         (`probe_main.cpp`, help section) after it has been through the real `GuardedConsole`.
#   Duplicate text keeps its multiplicity: a `Counter`, never a `set` (two identical continuation lines legitimately
#   exist, and a set comparison would hide one of them being dropped).
#
# ⛔ THE BASELINE IS FROZEN, AND ITS PROVENANCE IS A COMMAND, NOT A CLAIM. `help_baseline.json` is generated ONCE from
#    the pre-slice source and committed. It is reproducible at any later date without a checkout, because `legacy`
#    mode reads a FILE:
#        git show 807ebde:src/firmware_commands.cpp > /tmp/base.cpp
#        python3 tools/probe_console_sink/help_manifest.py freeze /tmp/base.cpp tools/probe_console_sink/help_baseline.json
#    `verify` re-runs exactly that comparison, so a hand-edited baseline is caught by re-deriving it from the source.
#
# ⚠ WHAT THIS FILE IS NOT: it is not a second help renderer. It never contains help TEXT of its own — every string it
#   compares is read either from the pre-slice C++ source or from the probe binary's own output.
"""Freeze / verify / compare the MeshRoute console-help content multiset, per product profile."""

from __future__ import annotations

import argparse
import collections
import json
import os
import re
import sys

# ---------------------------------------------------------------------------------------------------------------
# The REAL product profile matrix, derived from `pio project config --json-output` + lib/core/mr_features.h.
# ⛔ Not invented: every row below is the resolved macro set of at least one REAL board env, and the env list is the
#    evidence. `native` is deliberately ABSENT — platformio.ini's `test_build_src = no` means no native target
#    compiles `src/`, so it is not a help profile at all.
# ⓘ MR_FEAT_MOBILE / MR_FEAT_REMOTE_MGMT are DERIVED by mr_features.h from MR_PROFILE_*, not set directly:
#      MR_PROFILE_GATEWAY -> MR_FEAT_TEAM 0, MR_FEAT_MOBILE 0     MR_PROFILE_MOBILE -> MR_FEAT_REMOTE_MGMT 0
# ⓘ MR_RF_*_RANGE_TEXT differ ONLY on the two `*_v4` envs (863..928 / 22). They change a SUBSTRING of one cfg line,
#   never which lines exist, so the profiles below carry the default envelope text and the v4 variance is recorded
#   in the slice evidence instead of doubling the matrix.
PROFILES = {
    #  name                MR_N_LAYERS  MR_FEAT_MOBILE  MR_FEAT_REMOTE_MGMT  MR_FEAT_OLED   real envs
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

# The macro TEXT expansions the help body concatenates into a string literal. Defaults from lib/hal/rf_capabilities.h.
TEXT_MACROS = {"MR_RF_FREQ_RANGE_TEXT": "100..1000", "MR_RF_OUTPUT_RANGE_TEXT": "-9..22"}

LEGACY_SIGNATURE = "static void dump_help(Print& out) {"


class ManifestError(RuntimeError):
    """A refusal. A manifest that cannot be derived must stop the gate, never soften into a warning."""


# ---------------------------------------------------------------------------------------------------------------
# Preprocessor-condition evaluation (the tiny subset the help body actually uses)
# ---------------------------------------------------------------------------------------------------------------

_TOKEN_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*|\d+|&&|\|\||[!()<>=]=?|.")


def eval_cond(expr: str, macros: dict) -> bool:
    """Evaluate one `#if` expression under `macros`. Fail loud on anything outside the supported subset."""
    if not expr.strip():
        raise ManifestError("empty #if expression")
    py, i = [], 0
    for tok in _TOKEN_RE.findall(expr):
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
                raise ManifestError("`defined()` is outside this evaluator's subset: " + expr)
            # An undefined macro is 0 in a #if — but here that would silently drop a whole block, so REFUSE
            # instead: every macro the help body tests must be a named axis of the profile matrix.
            if tok not in macros:
                raise ManifestError(f"#if names macro {tok!r} which is not a profile axis: {expr!r}")
            py.append(str(int(macros[tok])))
        else:
            raise ManifestError(f"unsupported token {tok!r} in #if {expr!r}")
        i += 1
    try:
        return bool(eval("".join(py), {"__builtins__": {}}, {}))     # noqa: S307 — the token filter above is the guard
    except Exception as exc:                                          # pragma: no cover - refusal path
        raise ManifestError(f"cannot evaluate #if {expr!r}: {exc}") from exc


# ---------------------------------------------------------------------------------------------------------------
# Legacy (pre-slice) `dump_help()` extraction
# ---------------------------------------------------------------------------------------------------------------

def _decode_c(raw: str) -> str:
    out, i = [], 0
    while i < len(raw):
        c = raw[i]
        if c == "\\" and i + 1 < len(raw):
            nxt = raw[i + 1]
            if nxt == "x":                       # \xNN — the help body carries UTF-8 as hex escapes (the ⚠ glyph)
                out.append(chr(int(raw[i + 2:i + 4], 16)))
                i += 4
                continue
            out.append({"n": "\n", "r": "\r", "t": "\t", "0": "\0", "\\": "\\", '"': '"', "'": "'"}.get(nxt, nxt))
            i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


def _literal_of(call_args: str) -> str:
    """Decode `F("a" MACRO "b")` — adjacent literals concatenate, TEXT_MACROS expand. Refuses anything else."""
    inner = call_args.strip()
    if not inner.startswith("F(") or not inner.endswith(")"):
        raise ManifestError(f"help emission is not the F(...) idiom: {call_args[:70]!r}")
    inner = inner[2:-1].strip()
    parts, i = [], 0
    while i < len(inner):
        c = inner[i]
        if c.isspace():
            i += 1
        elif c == '"':
            j = i + 1
            while j < len(inner):
                if inner[j] == "\\":
                    j += 2
                    continue
                if inner[j] == '"':
                    break
                j += 1
            parts.append(_decode_c(inner[i + 1:j]))
            i = j + 1
        else:
            m = re.match(r"[A-Za-z_][A-Za-z0-9_]*", inner[i:])
            if not m or m.group(0) not in TEXT_MACROS:
                raise ManifestError(f"unexpanded token in help literal: {inner[i:i + 40]!r}")
            parts.append(TEXT_MACROS[m.group(0)])
            i += m.end()
    return "".join(parts)


def _call_args(line: str, open_idx: int) -> str:
    """The argument text of the call whose '(' is at `open_idx`, honouring nesting AND string literals.

    ⚠ NOT `line.rindex(')')`: several help emissions carry a trailing `//` comment that itself contains a ')'
      (`… gateway_only")));   // §team-id-cfg-removal: … (see below)`), and the naive form swallowed it.
    """
    depth, i, n = 0, open_idx, len(line)
    while i < n:
        c = line[i]
        if c == '"':
            i += 1
            while i < n and line[i] != '"':
                i += 2 if line[i] == "\\" else 1
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return line[open_idx + 1:i]
        i += 1
    raise ManifestError(f"unbalanced call in help body: {line[:80]!r}")


def _function_body(text: str, signature: str) -> str:
    try:
        i = text.index(signature)
    except ValueError as exc:
        raise ManifestError(f"signature not found: {signature!r}") from exc
    i = text.index("{", i)
    depth, j = 1, i + 1
    while j < len(text) and depth:
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
        j += 1
    return text[i + 1:j - 1]


def extract_legacy(source_text: str, macros: dict) -> list:
    """Render the pre-slice `dump_help()` for `macros` -> [ {text, kind, cond, terminated}, … ] in source order."""
    body = _function_body(source_text, LEGACY_SIGNATURE)
    rows, gates = [], []      # gates: [ [current_expr, [prior_exprs…]], … ]
    for raw in body.split("\n"):
        pp = re.match(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$", raw)
        if pp:
            directive, rest = pp.group(1), re.sub(r"//.*$", "", pp.group(2)).strip()
            if directive == "if":
                gates.append([rest, []])
            elif directive in ("ifdef", "ifndef"):
                raise ManifestError("#ifdef/#ifndef in dump_help is outside this evaluator's subset")
            elif directive == "elif":
                gates[-1][1].append(gates[-1][0])
                gates[-1][0] = rest
            elif directive == "else":
                prior = gates[-1][1] + [gates[-1][0]]
                gates[-1] = ["__ELSE__", prior]
            elif directive == "endif":
                gates.pop()
            continue
        m = re.search(r"\bout\.(println|print)\s*\(", raw)
        if not m:
            continue
        args = _call_args(raw, raw.index("(", m.start()))
        active, cond_parts = True, []
        for cur, prior in gates:
            if cur == "__ELSE__":
                taken = not any(eval_cond(p, macros) for p in prior)
                cond_parts.append("!(" + " || ".join(prior) + ")")
            else:
                taken = eval_cond(cur, macros)
                cond_parts.append(cur)
            active = active and taken
        if not active:
            continue
        text = _literal_of(args)
        rows.append(dict(text=text, kind=classify(text), cond=" && ".join(cond_parts) or "—",
                         terminated=(m.group(1) == "println")))
    if not rows:
        raise ManifestError("dump_help() yielded zero emissions — the extractor matched nothing")
    return rows


def classify(text: str) -> str:
    """content | heading | separator, derived from the text's own shape (never from a hand-kept list)."""
    if text == "":
        return "separator"
    return "heading" if text[0] != " " else "content"


def rendered_bytes(text: str, terminated: bool = True) -> int:
    """The bytes this line puts on the wire: the payload + the CRLF `Print::println` appends.

    ⚠ EVERY string in this module is LATIN-1 (one char == one byte), because the help body writes UTF-8 as raw
      `\\xNN` byte escapes (`"\\xe2\\x9a\\xa0"` = ⚠). Decoding those as code points and re-encoding UTF-8 doubled
      the glyph's width, i.e. reported a byte count no board ever emits. Read bytes, count bytes.
    """
    return len(text) + (2 if terminated else 0)


# ---------------------------------------------------------------------------------------------------------------
# Baseline freeze / verify / compare
# ---------------------------------------------------------------------------------------------------------------

def build_baseline(source_text: str) -> dict:
    out = {"_note": "FROZEN pre-slice dump_help() content, per profile. Regenerate only from a pre-0a source; see "
                    "the module docstring for the exact command.",
           "profiles": {}}
    for name, macros in PROFILES.items():
        rows = extract_legacy(source_text, macros)
        out["profiles"][name] = {
            "macros": macros,
            "envs": list(PROFILE_ENVS[name]),
            "content": [r["text"] for r in rows if r["kind"] == "content"],
            "headings": [r["text"] for r in rows if r["kind"] == "heading"],
            "separators": sum(1 for r in rows if r["kind"] == "separator"),
            "total_lines": len(rows),
            "total_bytes": sum(rendered_bytes(r["text"], r["terminated"]) for r in rows),
        }
    return out


def content_block(stdout: str, profile: str) -> list:
    """The CONTENT lines the probe binary printed for `profile`, between its own BEGIN/END markers.

    ⛔ REFUSES when the markers are missing or the declared count disagrees. Returning `[]` from a probe that never
      ran would otherwise flow straight into `compare_content` and look like a clean comparison against nothing.
    ⚠ `stdout` arrives UTF-8-decoded from `subprocess(text=True)`; it is re-encoded into the module's LATIN-1
      (one char == one byte) space so it compares against the frozen baseline on the same terms.
    """
    begin, end = f"HELP-CONTENT-BEGIN {profile} ", "HELP-CONTENT-END"
    lines = stdout.split("\n")
    starts = [i for i, ln in enumerate(lines) if ln.startswith(begin)]
    if len(starts) != 1:
        raise ManifestError(f"expected exactly one {begin!r} marker in the probe output, found {len(starts)}")
    i = starts[0]
    declared = int(lines[i][len(begin):].strip())
    try:
        j = next(k for k in range(i + 1, len(lines)) if lines[k] == end)
    except StopIteration as exc:
        raise ManifestError("probe output has no HELP-CONTENT-END marker") from exc
    body = [ln.encode("utf-8").decode("latin-1") for ln in lines[i + 1:j]]
    if len(body) != declared:
        raise ManifestError(f"probe declared {declared} content lines but printed {len(body)}")
    return body


def compare_content(expected: list, actual: list) -> list:
    """Multiset diff (multiplicity preserved). -> list of human-readable problem strings; empty == identical."""
    exp, act = collections.Counter(expected), collections.Counter(actual)
    problems = []
    for text, n in sorted((exp - act).items()):
        problems.append(f"MISSING x{n}: {_show(text)[:96]!r}")
    for text, n in sorted((act - exp).items()):
        problems.append(f"UNEXPECTED x{n}: {_show(text)[:96]!r}")
    return problems


def _read(path: str) -> str:
    """Read as LATIN-1 so one character is exactly one wire byte (see `rendered_bytes`)."""
    with open(path, "r", encoding="latin-1", newline="") as fh:
        return fh.read()


def _show(text: str) -> str:
    """Best-effort display of a latin-1 (= byte) string: render it as the UTF-8 it really is when it decodes."""
    try:
        return text.encode("latin-1").decode("utf-8")
    except UnicodeDecodeError:
        return text


def load_baseline(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("legacy", help="print the pre-slice manifest for one profile")
    p.add_argument("source")
    p.add_argument("--profile", default="full_oled", choices=sorted(PROFILES))
    p = sub.add_parser("freeze", help="write the frozen per-profile baseline JSON")
    p.add_argument("source")
    p.add_argument("out")
    p = sub.add_parser("verify", help="re-derive the baseline from a pre-slice source and require it to match")
    p.add_argument("source")
    p.add_argument("baseline")
    p = sub.add_parser("compare", help="compare a probe's rendered content lines against the frozen baseline")
    p.add_argument("baseline")
    p.add_argument("rendered", help="file of rendered content lines, one per line (probe output)")
    p.add_argument("--profile", required=True, choices=sorted(PROFILES))
    a = ap.parse_args(argv[1:])

    if a.cmd == "legacy":
        rows = extract_legacy(_read(a.source), PROFILES[a.profile])
        for n, r in enumerate(rows, 1):
            print(f"{n:3d} {r['kind'][:4]:4s} {rendered_bytes(r['text'], r['terminated']):4d}B "
                  f"[{r['cond']}] {_show(r['text'])}")
        print(f"--- {len(rows)} lines, "
              f"{sum(rendered_bytes(r['text'], r['terminated']) for r in rows)} rendered bytes, profile {a.profile}")
        return 0
    if a.cmd == "freeze":
        base = build_baseline(_read(a.source))
        with open(a.out, "w", encoding="utf-8") as fh:
            json.dump(base, fh, indent=1, ensure_ascii=False)
            fh.write("\n")
        print(f"wrote {a.out}: " + ", ".join(f"{k}={v['total_lines']}L/{v['total_bytes']}B"
                                            for k, v in base["profiles"].items()))
        return 0
    if a.cmd == "verify":
        fresh = build_baseline(_read(a.source))
        stored = load_baseline(a.baseline)
        if fresh["profiles"] != stored["profiles"]:
            print("FAIL: the frozen baseline does not re-derive from the given pre-slice source")
            for name in sorted(PROFILES):
                if fresh["profiles"].get(name) != stored["profiles"].get(name):
                    print(f"   profile {name} differs")
                    for problem in compare_content(stored["profiles"].get(name, {}).get("content", []),
                                                   fresh["profiles"][name]["content"]):
                        print("     " + problem)
            return 1
        print("ok: frozen baseline re-derives exactly from the given pre-slice source")
        return 0
    # compare
    stored = load_baseline(a.baseline)["profiles"][a.profile]
    actual = [ln for ln in _read(a.rendered).split("\n") if ln != ""]
    problems = compare_content(stored["content"], actual)
    for problem in problems:
        print("   " + problem)
    print(f"   content multiset {a.profile}: expected {len(stored['content'])} / actual {len(actual)} / "
          f"{len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
