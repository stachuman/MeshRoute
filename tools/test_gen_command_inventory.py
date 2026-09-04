#!/usr/bin/env python3
# MeshRoute — tools/test_gen_command_inventory.py
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# ★★ THE SABOTAGE BATTERY FOR tools/gen_command_inventory.py (remote-admin v2 slice 0e-A).
#
# The brief's own words: "A regex that merely counts `strncmp` tokens is not enough: fixture controls must exercise
# each supported source shape, and the real-tree test must prove representative rows from all three surfaces were
# seen." So this file has TWO halves:
#
#   1. FIXTURE SABOTAGE — a synthetic mini-tree exercising every source shape S1..S5, mutated one way at a time. Each
#      mutation must fail FOR ITS OWN NAMED REASON, never merely "differently". Eight required classes:
#         add / remove a top-level verb · add / remove a sub-verb · add / remove a caller-only arm ·
#         change a feature gate without changing a name · duplicate a normalized row · zero rows ·
#         a row without provenance · a populated authority cell.
#   2. REAL-TREE CONTROLS — the tracked table equals fresh generation byte-for-byte, all three surfaces are
#      represented, every authority cell is empty, and the wiring/coverage refusals fire on the real source.
#
# ⛔ Every fixture is an ISOLATED COPY under a temp dir. Nothing here writes to the repository.
"""Selftests and sabotage controls for tools/gen_command_inventory.py."""

from __future__ import annotations

import os
import re
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import gen_command_inventory as G   # noqa: E402

REPO_ROOT = G.REPO_ROOT


# ---------------------------------------------------------------------------------------------------------------
# A synthetic tree that exercises every supported source shape.
# ---------------------------------------------------------------------------------------------------------------

FIX_COMMANDS = '''\
namespace mrfw {

static void handle_thing(const char* args, Print& out) {
    if (!strncmp(args, "alpha", 5)) { do_alpha(); return; }
    if (!strcmp(args, "beta")) { do_beta(); return; }
}

bool dispatch(const char* line, size_t len, Print& out) {
    if ((len == 4 && !strncmp(line, "help", 4)) || (len == 1 && line[0] == '?')) { dump_help(out); return true; }
    if (len == 6 && !strncmp(line, "status", 6)) { dump_status(out); return true; }
    if (len > 6 && !strncmp(line, "thing ", 6)) { handle_thing(line + 6, out); return true; }
#if MR_FEAT_WIDGET
    if (len == 6 && !strncmp(line, "widget", 6)) { do_widget(out); return true; }
#endif
    return false;
}

}  // namespace mrfw
'''

FIX_MAIN = '''\
namespace mrfw {

static size_t ble_dispatch_line(const char* line, size_t len, char* out, size_t cap) {
    if (len == 6 && !strncmp(line, "whoami", 6)) { return emit_ready(out, cap); }
    if (len == 5 && !strncmp(line, "extra", 5)) { return emit_extra(out, cap); }
    if (dispatch(line, len, ls)) { return 0; }
    if (parse_command(line, len, cmd) == Ok) { return 0; }
    return 0;
}

static void service_console() {
    if (!dispatch(line, pos, mrcon)) {
        if (parse_command(line, pos, cmd) == Ok) { run(cmd); }
    }
}

}  // namespace mrfw
'''

FIX_PARSE = '''\
namespace meshroute { namespace console {

static bool tok_eq(Tok t, const char* s) { return !strncmp(t.p, s, t.n) && s[t.n] == '\\0'; }

ParseErr parse_command(const char* line, size_t len, Command& out) {
    if (tok_eq(verb, "send")) { return ParseErr::ok; }
    return ParseErr::unknown_verb;
}

} }
'''

FIX_PRESET = '''\
namespace mrfw {

static bool preset_word_is(const char* t, size_t n, const char* w) { return n == strlen(w) && !strncmp(t, w, n); }

inline bool preset_verb(const char* args, size_t len, IPresetLines& out) {
    if (!a.word(t, n) || !preset_word_is(t, n, "preset")) return false;
    if (preset_word_is(t, n, "list")) { emit_list(out); return true; }
    return false;
}

}  // namespace mrfw
'''

FIX_SURFACES = (
    G.Surface("src/firmware_commands.cpp", "dispatch", "top", "serial,ble",
              reached_from=(("src/fw_main.cpp", "service_console", "dispatch"),
                            ("src/fw_main.cpp", "ble_dispatch_line", "dispatch"))),
    G.Surface("src/firmware_commands.cpp", "handle_thing", "sub", "serial,ble", parent="thing",
              reached_from=(("src/firmware_commands.cpp", "dispatch", "handle_thing"),)),
    G.Surface("src/firmware_ui_preset_verbs.h", "preset_verb", "sub", "serial,ble", parent="ui"),
    G.Surface("src/fw_main.cpp", "ble_dispatch_line", "caller", "ble"),
    G.Surface("lib/console/console_parse.cpp", "parse_command", "caller", "serial,ble",
              reached_from=(("src/fw_main.cpp", "service_console", "parse_command"),
                            ("src/fw_main.cpp", "ble_dispatch_line", "parse_command"))),
)

FIX_NON_COMMAND = {
    ("lib/console/console_parse.cpp", "tok_eq"): "the S3 helper's own definition",
    ("src/firmware_ui_preset_verbs.h", "preset_word_is"): "the S4 helper's own definition",
}

FIX_FILES = {
    "src/firmware_commands.cpp": FIX_COMMANDS,
    "src/fw_main.cpp": FIX_MAIN,
    "lib/console/console_parse.cpp": FIX_PARSE,
    "src/firmware_ui_preset_verbs.h": FIX_PRESET,
}


class FixtureTree:
    """An isolated copy of the synthetic tree, with the generator's pins swapped to match it."""

    def __init__(self, edits=None, surfaces=None, non_command=None):
        self.edits = edits or {}
        self.surfaces = FIX_SURFACES if surfaces is None else surfaces
        self.non_command = dict(FIX_NON_COMMAND) if non_command is None else dict(non_command)
        self.root = None
        self._saved = None

    def __enter__(self):
        self.root = tempfile.mkdtemp(prefix="radmin0e-inv-")
        for rel, body in FIX_FILES.items():
            body = self.edits.get(rel, lambda s: s)(body)
            path = os.path.join(self.root, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(body)
        self._saved = (G.SURFACES, G.NON_COMMAND, G.SCAN_FILES)
        G.SURFACES = self.surfaces
        G.NON_COMMAND = self.non_command
        G.SCAN_FILES = tuple(sorted(FIX_FILES))
        return self

    def __exit__(self, *exc):
        G.SURFACES, G.NON_COMMAND, G.SCAN_FILES = self._saved
        shutil.rmtree(self.root, ignore_errors=True)
        return False

    def rows(self):
        return G.build_rows(self.root)[0]

    def cells(self):
        return {(r.verb, r.subverb, r.func) for r in self.rows()}


def sub(pattern, repl):
    return lambda s: re.sub(pattern, repl, s, count=1)


class TestFixtureShapes(unittest.TestCase):
    """The generator sees every supported source shape in the synthetic tree."""

    def test_baseline_sees_all_five_shapes(self):
        with FixtureTree() as t:
            shapes = set()
            for rel in G.SCAN_FILES:
                for site in G.scan_file(t.root, rel)[0]:
                    shapes.update(site.shapes)
            self.assertEqual({"S1", "S2", "S3", "S4", "S5"}, shapes,
                             "the fixture must exercise every supported comparison shape")

    def test_baseline_rows(self):
        with FixtureTree() as t:
            cells = t.cells()
        self.assertIn(("help (alias: ?)", "—", "dispatch"), cells, "S5 alias folded into one row")
        self.assertIn(("status", "—", "dispatch"), cells)
        self.assertIn(("widget", "—", "dispatch"), cells, "a gated arm stays present")
        self.assertIn(("thing", "alpha", "handle_thing"), cells)
        self.assertIn(("thing", "beta", "handle_thing"), cells)
        self.assertIn(("whoami", "—", "ble_dispatch_line"), cells)
        self.assertIn(("send", "—", "parse_command"), cells)
        self.assertIn(("ui", "preset list", "preset_verb"), cells,
                      "the family guard must prefix its arms — `ui list` is not a grammar")

    def test_gate_macro_is_exact(self):
        with FixtureTree() as t:
            gates = {r.verb: r.gate for r in t.rows()}
        self.assertEqual("MR_FEAT_WIDGET", gates["widget"])
        self.assertEqual("—", gates["status"])


class TestSabotage(unittest.TestCase):
    """Each mutation must be caught, and for its own named reason."""

    # ---- 1/2: add and remove one TOP-LEVEL verb --------------------------------------------------------------
    def test_added_top_level_verb_changes_the_table(self):
        with FixtureTree() as base:
            before = base.cells()
        with FixtureTree({"src/firmware_commands.cpp": sub(
                r'(    if \(len == 6 && !strncmp\(line, "status", 6\)\).*\n)',
                r'\1    if (len == 3 && !strncmp(line, "zzz", 3)) { do_zzz(out); return true; }\n')}) as t:
            after = t.cells()
        self.assertEqual({("zzz", "—", "dispatch")}, after - before)

    def test_removed_top_level_verb_changes_the_table(self):
        with FixtureTree() as base:
            before = base.cells()
        with FixtureTree({"src/firmware_commands.cpp": sub(
                r'    if \(len == 6 && !strncmp\(line, "status", 6\)\).*\n', "")}) as t:
            after = t.cells()
        self.assertEqual({("status", "—", "dispatch")}, before - after)

    # ---- 3/4: add and remove one SUB-VERB --------------------------------------------------------------------
    def test_added_sub_verb_changes_the_table(self):
        with FixtureTree() as base:
            before = base.cells()
        with FixtureTree({"src/firmware_commands.cpp": sub(
                r'(    if \(!strcmp\(args, "beta"\)\).*\n)',
                r'\1    if (!strcmp(args, "gamma")) { do_gamma(); return; }\n')}) as t:
            after = t.cells()
        self.assertEqual({("thing", "gamma", "handle_thing")}, after - before)

    def test_removed_sub_verb_changes_the_table(self):
        with FixtureTree() as base:
            before = base.cells()
        with FixtureTree({"src/firmware_commands.cpp": sub(
                r'    if \(!strcmp\(args, "beta"\)\).*\n', "")}) as t:
            after = t.cells()
        self.assertEqual({("thing", "beta", "handle_thing")}, before - after)

    # ---- 5/6: add and remove one CALLER-ONLY arm -------------------------------------------------------------
    def test_added_caller_only_arm_changes_the_table(self):
        with FixtureTree() as base:
            before = base.cells()
        with FixtureTree({"src/fw_main.cpp": sub(
                r'(    if \(len == 5 && !strncmp\(line, "extra", 5\)\).*\n)',
                r'\1    if (len == 4 && !strncmp(line, "peek", 4)) { return emit_peek(out, cap); }\n')}) as t:
            after = t.cells()
        self.assertEqual({("peek", "—", "ble_dispatch_line")}, after - before)

    def test_removed_caller_only_arm_changes_the_table(self):
        with FixtureTree() as base:
            before = base.cells()
        with FixtureTree({"src/fw_main.cpp": sub(
                r'    if \(len == 5 && !strncmp\(line, "extra", 5\)\).*\n', "")}) as t:
            after = t.cells()
        self.assertEqual({("extra", "—", "ble_dispatch_line")}, before - after)

    # ---- 7: a feature gate MOVES without any name changing --------------------------------------------------
    def test_changed_feature_gate_without_a_name_change(self):
        with FixtureTree() as base:
            before = {r.verb: r.gate for r in base.rows()}
        with FixtureTree({"src/firmware_commands.cpp":
                          sub(r"#if MR_FEAT_WIDGET", "#if MR_FEAT_GADGET")}) as t:
            after = {r.verb: r.gate for r in t.rows()}
        self.assertEqual(set(before), set(after), "no name changed")
        self.assertEqual("MR_FEAT_WIDGET", before["widget"])
        self.assertEqual("MR_FEAT_GADGET", after["widget"], "the gate move must be visible in the table")

    # ---- 8: a duplicated normalized row ---------------------------------------------------------------------
    def test_duplicate_normalized_row_is_refused(self):
        with FixtureTree() as t:
            rows = t.rows()
            rows.append(rows[0])
            with self.assertRaises(G.GeneratorError) as cm:
                G.verify_rows(rows)
        self.assertIn("duplicate normalized row", str(cm.exception))

    # ---- 9: generation returns zero rows ---------------------------------------------------------------------
    def test_emptied_surface_is_refused_not_passed(self):
        """One dispatcher loses every arm while its wiring stays intact: refusal (c)."""
        with FixtureTree({"src/fw_main.cpp": lambda s: re.sub(
                r'^    if \(len == \d+ && !strncmp\(line, "(?:whoami|extra)".*\n', "", s, flags=re.M)}) as t:
            with self.assertRaises(G.GeneratorError) as cm:
                t.rows()
        self.assertIn("produced NO rows", str(cm.exception))
        self.assertIn("ble_dispatch_line", str(cm.exception))

    def test_global_zero_rows_is_refused_not_passed(self):
        """Nothing recognised anywhere and nothing pinned: refusal (d) must still not print PASS."""
        strip_all = lambda s: re.sub(r"^.*(?:strn?cmp|tok_eq|preset_word_is)\(.*\n", "", s, flags=re.M)
        with FixtureTree({rel: strip_all for rel in FIX_FILES}, surfaces=(), non_command={}) as t:
            with self.assertRaises(G.GeneratorError) as cm:
                t.rows()
        self.assertIn("zero rows", str(cm.exception))

    # ---- 10: a row without owner/function/transport/source provenance -----------------------------------------
    def test_row_without_provenance_is_refused(self):
        wanted = {"func": "owning function", "transports": "transport set",
                  "source": "file:line", "gate": "feature gate", "verb": "verb", "subverb": "sub-verb"}
        for field, name in wanted.items():
            with FixtureTree() as t:
                rows = t.rows()
                setattr(rows[0], field, "")
                with self.assertRaises(G.GeneratorError) as cm:
                    G.verify_rows(rows)
            msg = str(cm.exception)
            self.assertIn("has no %s" % name, msg, "blanking %r must be named exactly" % field)
            self.assertIn("full provenance", msg)

    def test_row_with_a_non_file_line_source_is_refused(self):
        with FixtureTree() as t:
            rows = t.rows()
            rows[0].source = "somewhere"
            with self.assertRaises(G.GeneratorError) as cm:
                G.verify_rows(rows)
        self.assertIn("file:line", str(cm.exception))

    # ---- 11: a populated authority cell ------------------------------------------------------------------------
    def test_populated_authority_cell_is_refused(self):
        with FixtureTree() as t:
            rows = t.rows()
            rows[0].authority = "owner"
            with self.assertRaises(G.GeneratorError) as cm:
                G.verify_rows(rows)
        self.assertIn("authority classification", str(cm.exception))

    # ---- 12: a NEW dispatcher must not be able to land silently -----------------------------------------------
    def test_unclassified_dispatcher_is_a_hard_error(self):
        with FixtureTree({"src/firmware_commands.cpp": sub(
                r"(bool dispatch\()",
                'static void handle_newfamily(const char* a, Print& out) {\n'
                '    if (!strcmp(a, "sneaky")) { do_sneaky(); return; }\n'
                '}\n\n\\1')}) as t:
            with self.assertRaises(G.GeneratorError) as cm:
                t.rows()
        self.assertIn("unclassified function", str(cm.exception))
        self.assertIn("handle_newfamily", str(cm.exception))

    # ---- 13: a renamed surface must not silently vanish -------------------------------------------------------
    def test_renamed_surface_function_is_refused(self):
        with FixtureTree({"src/firmware_commands.cpp":
                          sub(r"handle_thing\(const char\* args", "handle_thingy(const char* args")}) as t:
            with self.assertRaises(G.GeneratorError) as cm:
                t.rows()
        self.assertIn("no longer resolves", str(cm.exception))

    # ---- 14: the transport claim is measured, not asserted ----------------------------------------------------
    def test_broken_wiring_makes_the_transport_claim_refuse(self):
        with FixtureTree({"src/fw_main.cpp": sub(
                r"    if \(dispatch\(line, len, ls\)\) \{ return 0; \}\n", "")}) as t:
            with self.assertRaises(G.GeneratorError) as cm:
                t.rows()
        self.assertIn("no call to `dispatch` exists there", str(cm.exception))

    # ---- 15: the generator must read the real source, not a cache ---------------------------------------------
    def test_generator_reads_the_files_it_is_pointed_at(self):
        with FixtureTree() as t:
            first = t.cells()
            path = os.path.join(t.root, "src/firmware_commands.cpp")
            with open(path, "r", encoding="utf-8") as fh:
                body = fh.read()
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(body.replace('"status", 6', '"stateus", 7'))
            second = t.cells()
        self.assertNotEqual(first, second, "a control that passes without re-reading source is not a control")


class TestRealTree(unittest.TestCase):
    """Controls against the real repository."""

    @classmethod
    def setUpClass(cls):
        cls.rows = G.build_rows(REPO_ROOT)[0]

    def test_tracked_table_equals_fresh_generation(self):
        text, _ = G.generate(REPO_ROOT)
        path = os.path.join(REPO_ROOT, G.TRACKED_OUTPUT)
        self.assertTrue(os.path.exists(path), "%s must be tracked" % G.TRACKED_OUTPUT)
        with open(path, "r", encoding="utf-8") as fh:
            self.assertEqual(fh.read(), text,
                             "regenerate with `python3 tools/gen_command_inventory.py --write`")

    def test_every_authority_cell_is_empty(self):
        self.assertTrue(self.rows)
        for r in self.rows:
            self.assertEqual("", r.authority, "0e must not classify %s" % r.source)
        path = os.path.join(REPO_ROOT, G.TRACKED_OUTPUT)
        with open(path, "r", encoding="utf-8") as fh:
            body = fh.read()
        for line in body.split("\n"):
            if line.startswith("| `") and line.count("|") == 8:
                self.assertRegex(line, r"\|\s*\|\s*$", "the authority cell must be blank: %s" % line)

    def test_all_three_surfaces_are_represented(self):
        kinds = {}
        for s in G.SURFACES:
            kinds.setdefault(s.kind, set()).add("%s::%s" % (s.file, s.func))
        seen = {r.surface for r in self.rows}
        for kind in ("top", "sub", "caller", "remote"):
            self.assertTrue(kinds[kind] & seen, "no rows from any %s surface" % kind)

    def test_representative_real_rows(self):
        cells = {(r.verb, r.subverb, r.func, r.transports) for r in self.rows}
        # surface 1 — the top-level verb map, including the shared help/? arm and a gated family
        self.assertIn(("help (alias: ?)", "—", "dispatch", "serial,ble"), cells)
        self.assertIn(("ui", "—", "dispatch", "serial,ble"), cells)
        # surface 2 — sub-verb dispatchers named by the brief
        self.assertIn(("cfg set", "sf_list", "handle_cfg_set", "serial,ble"), cells)
        self.assertIn(("team", "exportkey", "handle_team", "serial,ble"), cells)
        self.assertIn(("mobile", "register scan", "handle_mobile", "serial,ble"), cells)
        self.assertIn(("ui", "preset list", "preset_verb", "serial,ble"), cells)
        # surface 3 — caller-only arms
        self.assertIn(("send", "—", "parse_command", "serial,ble"), cells)
        self.assertIn(("send_channel", "—", "parse_command", "serial,ble"), cells)
        self.assertIn(("send_layer", "—", "parse_command", "serial,ble"), cells)
        self.assertIn(("whoami", "—", "ble_dispatch_line", "ble"), cells)
        self.assertIn(("status", "—", "ble_dispatch_line", "ble"), cells)
        # the legacy over-the-air remote-admin set that v2 replaces
        self.assertIn(("password rotate", "—", "remote_exec", "radio(REMOTE_CMD)"), cells)
        self.assertIn(("reboot (alias: prep-restart)", "—", "remote_exec", "radio(REMOTE_CMD)"), cells)

    def test_feature_gates_are_recorded_with_their_exact_macro(self):
        gates = {}
        for r in self.rows:
            gates.setdefault(r.gate, []).append(r)
        for macro in ("MR_FEAT_OLED", "MR_FEAT_REMOTE_MGMT", "MR_N_LAYERS < 2",
                      "!(MR_N_LAYERS < 2)", "MR_N_LAYERS < 2 && MR_FEAT_MOBILE"):
            self.assertIn(macro, gates, "no row carries the gate %r" % macro)

    def test_every_row_has_full_provenance(self):
        for r in self.rows:
            self.assertRegex(r.source, r"^[a-z].*\.(cpp|h):[0-9]+$")
            self.assertTrue(r.func and r.transports and r.gate and r.verb and r.subverb)

    def test_no_duplicate_rows(self):
        keys = [r.key() for r in self.rows]
        self.assertEqual(len(keys), len(set(keys)))

    def test_row_count_is_reported_and_nonzero(self):
        text, rows = G.generate(REPO_ROOT)
        self.assertGreater(len(rows), 100)
        self.assertIn("Total rows: **%d**." % len(rows), text)


if __name__ == "__main__":
    unittest.main()
