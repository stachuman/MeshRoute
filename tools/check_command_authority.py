#!/usr/bin/env python3
"""Slice 6: bind the positional ruled table, production policy and generated inventory.

Like check_a0_matrix, a mention outside the actual table/initializer is not a row. Selftests
mutate disposable artifact copies and require a named diagnostic, never just a nonzero exit.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys
import tempfile

import gen_command_inventory as G

ROOT = Path(__file__).resolve().parents[1]
HEADER = "src/firmware_command_authority.h"


def parse_header(text):
    text = re.sub(r"//[^\n]*|/\*[\s\S]*?\*/", "", text)
    enum = re.search(r"enum class CommandClass\s*:\s*uint8_t\s*\{([^}]+)\}", text)
    array = re.search(r"kCommandPolicy\[\]\s*=\s*\{([\s\S]*?)\n\};", text)
    if not enum or not array:
        raise G.GeneratorError("missing production policy enum/initializer")
    classes = {v.strip().removesuffix("_") for v in enum.group(1).split(",") if v.strip()}
    if classes != G.AUTHORITY_CLASSES:
        raise G.GeneratorError("unclassified production class set")
    rows = {}
    for line in array.group(1).splitlines():
        if not line.strip():
            continue
        m = re.fullmatch(r'\s*\{"([^"]+)", "([^"]+)", CommandClass::(\w+), (true|false)\},\s*', line)
        if not m:
            raise G.GeneratorError("malformed production policy row: " + line)
        verb, subverb, cls, disruptive = m.groups()
        key = verb, subverb
        cls = cls.removesuffix("_")
        if cls not in classes:
            raise G.GeneratorError("unclassified production row: " + repr(key))
        if key in rows:
            raise G.GeneratorError("duplicate production semantic row: " + repr(key))
        rows[key] = cls, disruptive == "true"
    if not rows:
        raise G.GeneratorError("empty production policy")
    return rows


def parse_inventory(text):
    section = text.split("## Recognised-but-excluded comparison sites", 1)[0]
    rows = []
    for line in section.splitlines():
        if not line.startswith("| `"):
            continue
        cells = G.markdown_cells(line)
        if len(cells) != 7:
            raise G.GeneratorError("malformed inventory row: " + line)
        verb, subverb, func, transports, gate, source, authority = cells
        rows.append(G.Row(verb, subverb, func, transports, gate, source, authority,
                          source.rsplit(":", 1)[0] + "::" + func))
    if not rows:
        raise G.GeneratorError("empty inventory")
    G.verify_rows(rows)
    return rows


def check(table_text, header_text, inventory_text):
    try:
        table = G.parse_authority_table(table_text)
        header = parse_header(header_text)
        inventory = parse_inventory(inventory_text)
    except G.GeneratorError as exc:
        return [str(exc)]
    failures = []
    surfaces = {s.file + "::" + s.func: s for s in G.SURFACES}
    used = set()
    for row in inventory:
        key = G.semantic_key(row)
        used.add(key)
        if key not in table:
            failures.append("missing ruled row: " + repr(key))
            continue
        surface = surfaces.get(row.surface)
        if surface is None:
            failures.append("unknown inventory surface: " + row.surface)
            continue
        eligibility = G.surface_eligibility(surface)
        expected = G.authority_cell(table[key], eligibility)
        if row.authority != expected:
            failures.append("authority/surface mark disagreement at " + row.source)
    for key in table:
        if key not in used:
            failures.append("orphan ruled row: " + repr(key))
        if key not in header or table[key] != header[key]:
            failures.append("table/header disagreement: " + repr(key))
    for key in header.keys() - table.keys():
        failures.append("orphan production row: " + repr(key))
    return failures


def selftest(texts):
    table, header, inventory = texts
    first = next(line for line in table.splitlines(True) if line.startswith("| `"))
    controls = (
        ("missing row", "missing ruled row", (table.replace(first, "", 1), header, inventory)),
        ("duplicate row", "duplicate semantic", (table.replace(first, first + first, 1), header, inventory)),
        ("unclassified row", "unclassified", (table.replace(first, first.replace("| owner |", "| unclassified |"), 1), header, inventory)),
        ("table/header disagreement", "table/header disagreement",
         (table, header.replace('"acl", "—", CommandClass::owner', '"acl", "—", CommandClass::operator_', 1), inventory)),
        ("orphan row", "orphan", (table + "| `orphan-probe` | — | owner | no | control |\n", header, inventory)),
        ("surface mark dropped", "surface mark", (table, header, inventory.replace(" · surface:local", "", 1))),
    )
    failures = []
    with tempfile.TemporaryDirectory(prefix="mr-command-authority-") as tmp:
        for name, diagnostic, mutant in controls:
            if mutant == texts:
                failures.append(name + ": control not applied")
                continue
            paths = [Path(tmp) / filename for filename in ("table.md", "header.h", "inventory.md")]
            for path, text in zip(paths, mutant):
                path.write_text(text, encoding="utf-8")
            errors = check(*(path.read_text(encoding="utf-8") for path in paths))
            if not any(diagnostic in error for error in errors):
                failures.append(name + ": missing intended refusal: " + repr(errors))
            else:
                print("RED: " + name + " -> " + next(e for e in errors if diagnostic in e))
    return failures


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args(argv)
    try:
        texts = tuple((args.root / p).read_text(encoding="utf-8") for p in (G.AUTHORITY_TABLE, HEADER, G.TRACKED_OUTPUT))
        errors = check(*texts)
        if not errors and args.selftest:
            errors = selftest(texts)
    except (OSError, G.GeneratorError) as exc:
        errors = [str(exc)]
    if errors:
        for error in errors:
            print("FAIL: " + error, file=sys.stderr)
        return 1
    print("PASS: command authority — ruled table / production header / generated inventory agree"
          + ("; 6/6 selftests RED" if args.selftest else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
