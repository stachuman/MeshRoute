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
    if (help_command(line, len, out)) return true;
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

# ★ §0a: the extracted help family. The synthetic `dispatch` above keeps its LEGACY one-line aliased help arm on
#   purpose — the shape 0a replaced — so the fixture proves the generator still reads BOTH shapes, the old in-place
#   arm and the new header router. The four traps below are the "both-direction" half the 0a brief requires: a
#   commented-out arm, a comparison written inside a STRING, a helper definition, and a LENGTH GUARD that is not an
#   alias spelling. None of them may produce a command row.
FIX_HELP = '''\
namespace mrfw {

// A helper definition, not a dispatcher: it is pinned in NON_COMMAND and must contribute no row.
inline bool help_is_dash(const char* s) { return !strncmp(s, "-", 1); }

inline void topic_messaging(Print& out) {
    out.println(F("  send <id> \"<text>\"   e.g. an arm written in TEXT: !strncmp(a, \"phantom\", 7)"));
}

inline bool help_command(const char* line, size_t len, Print& out) {
    if (!(len == 1 && line[0] == '?') && (len < 4 || strncmp(line, "help", 4))) return false;
    if (len == 1 || len == 4) { render_index(out); return true; }
    if (line[4] != ' ') return false;
    const char* a = line + 5; size_t an = len - 5;
    if      (an ==  9 && !strncmp(a, "messaging",   9)) topic_messaging(out);
    else if (an ==  8 && !strncmp(a, "identity",    8)) topic_identity(out);
    // else if (an ==  5 && !strncmp(a, "ghost",     5)) topic_ghost(out);
#if MR_HELP_HAS_MOBILE
    else if (an ==  6 && !strncmp(a, "mobile",      6)) topic_mobile(out);
#endif
    else                                                render_usage(out);
    return true;
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
    G.Surface("src/firmware_help.h", "help_command", "top", "serial",
              reached_from=(("src/firmware_commands.cpp", "dispatch", "help_command"),)),
    G.Surface("src/fw_main.cpp", "ble_dispatch_line", "caller", "ble"),
    G.Surface("lib/console/console_parse.cpp", "parse_command", "caller", "serial,ble",
              reached_from=(("src/fw_main.cpp", "service_console", "parse_command"),
                            ("src/fw_main.cpp", "ble_dispatch_line", "parse_command"))),
)

FIX_NON_COMMAND = {
    ("src/firmware_help.h", "help_is_dash"): "a helper definition, not a command arm",
    ("lib/console/console_parse.cpp", "tok_eq"): "the S3 helper's own definition",
    ("src/firmware_ui_preset_verbs.h", "preset_word_is"): "the S4 helper's own definition",
}

FIX_FILES = {
    "src/firmware_commands.cpp": FIX_COMMANDS,
    "src/firmware_help.h": FIX_HELP,
    "src/fw_main.cpp": FIX_MAIN,
    "lib/console/console_parse.cpp": FIX_PARSE,
    "src/firmware_ui_preset_verbs.h": FIX_PRESET,
}

# Fixture-only classifications, explicitly provided independently of scanning. Extra rows cover the
# deliberate source additions below; the real three-artifact checker separately refuses orphans.
FIX_POLICY = {
    "extra": "—", "help": "identity|messaging|mobile|audio", "help (alias: ?)": "—",
    "send": "—", "status": "—", "thing": "—|alpha|beta|gamma", "ui": "preset|preset list",
    "whoami": "—", "widget": "—", "zzz": "—", "peek": "—", "cfg set": "—", "quit": "—",
    "erase (alias: wipe)": "—", "gizmo": "—", "reboot": "—", "zzznew": "—",
    "joinprofile": "—", "peers": "—|all", "stateus": "—",
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
        policy_path = os.path.join(self.root, G.AUTHORITY_TABLE)
        os.makedirs(os.path.dirname(policy_path), exist_ok=True)
        with open(policy_path, "w", encoding="utf-8") as fh:
            fh.write("## Semantic policy\n\n| verb | sub-verb | class | disruptive | authority |\n")
            for verb, subs in FIX_POLICY.items():
                for sv in subs.split("|"):
                    fh.write("| `%s` | `%s` | operator | no | synthetic fixture |\n" % (verb, sv))
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

    # Slice 0e's test_populated_authority_cell_is_refused is INVERTED by the owner's Slice 6 ruling.
    def test_unclassified_row_is_refused(self):
        with FixtureTree() as t:
            rows = t.rows()
            rows[0].authority = ""
            with self.assertRaises(G.GeneratorError) as cm:
                G.verify_rows(rows)
        self.assertIn("unclassified authority", str(cm.exception))

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


class TestHelpSurface(unittest.TestCase):
    """§0a/[[B208]]: BOTH DIRECTIONS on the extracted `help` family.

    The 0a brief requires exactly this pair: "a real help arm is found, while a comment, string example, helper
    definition or similarly shaped non-dispatch comparison is not". A parser that satisfies only the first half
    would happily invent `help ghost` and `help phantom`; one that satisfies only the second would have dropped the
    `help (alias: ?)` row when the text moved into the header.
    """

    def test_the_real_help_arms_are_found(self):
        with FixtureTree() as t:
            cells = t.cells()
        self.assertIn(("help (alias: ?)", "—", "help_command"), cells,
                      "the family guard is the `help` row, with `?` folded in as its S5 alias")
        self.assertIn(("help", "messaging", "help_command"), cells)
        self.assertIn(("help", "identity", "help_command"), cells)
        self.assertIn(("help", "mobile", "help_command"), cells)

    def test_a_commented_out_arm_is_not_found(self):
        with FixtureTree() as t:
            self.assertNotIn(("help", "ghost", "help_command"), t.cells())

    def test_an_arm_written_inside_a_string_is_not_found(self):
        with FixtureTree() as t:
            subs = {r.subverb for r in t.rows()}
        self.assertNotIn("phantom", subs, "help TEXT that quotes a strncmp must never become a command row")

    def test_a_helper_definition_contributes_no_row(self):
        with FixtureTree() as t:
            funcs = {r.func for r in t.rows()}
        self.assertNotIn("help_is_dash", funcs)

    def test_the_length_guard_is_not_read_as_an_alias(self):
        """`line[4] != ' '` and `line[4] == ' '` are LENGTH GUARDS. Only `line[0] == '?'` is a spelling."""
        with FixtureTree() as t:
            help_rows = [r for r in t.rows() if r.func == "help_command"]
        aliased = [r.verb for r in help_rows if "alias" in r.verb]
        self.assertEqual(["help (alias: ?)"], aliased,
                         "exactly one alias, and it is `?` — never a space from a length guard")

    def test_a_removed_help_topic_changes_the_table(self):
        with FixtureTree() as base:
            before = base.cells()
        with FixtureTree({"src/firmware_help.h": sub(
                r'    else if \(an ==  8 && !strncmp\(a, "identity",    8\)\) topic_identity\(out\);\n', "")}) as t:
            after = t.cells()
        self.assertEqual({("help", "identity", "help_command")}, before - after)
        self.assertEqual(set(), after - before)

    def test_an_added_help_topic_changes_the_table(self):
        with FixtureTree() as base:
            before = base.cells()
        with FixtureTree({"src/firmware_help.h": sub(
                r'(    else if \(an ==  8 && !strncmp\(a, "identity",    8\)\) topic_identity\(out\);\n)',
                r'\1    else if (an ==  5 && !strncmp(a, "audio",       5)) topic_audio(out);\n')}) as t:
            after = t.cells()
        self.assertEqual({("help", "audio", "help_command")}, after - before)

    def test_an_emptied_help_router_is_refused_not_passed(self):
        """Strip EVERY comparison from the router — the guard included — and refusal (c) must fire.

        ⚠ Stripping only the nine topic arms is NOT enough and must not be: the family guard is itself the `help`
          row, so the surface is still populated and the generator is right to stay green. This test removes the
          guard too, which is the only shape that genuinely empties the dispatcher.
        """
        with FixtureTree({"src/firmware_help.h": lambda s: "\n".join(
                ln for ln in s.split("\n") if "strncmp(a," not in ln and 'strncmp(line, "help"' not in ln)}) as t:
            with self.assertRaises(G.GeneratorError) as cm:
                t.rows()
        self.assertIn("help_command", str(cm.exception))

    def test_the_dispatch_call_site_is_what_proves_the_wiring(self):
        """Delete `help_command(...)` from dispatch and the transport claim must REFUSE, not quietly stand."""
        with FixtureTree({"src/firmware_commands.cpp":
                          sub(r"    if \(help_command\(line, len, out\)\) return true;\n", "")}) as t:
            with self.assertRaises(G.GeneratorError) as cm:
                t.rows()
        self.assertIn("help_command", str(cm.exception))


class TestActionAdmissionSurfaces(unittest.TestCase):
    """P1: shared predicates keep their real provenance and verified caller chains."""

    def modified_rows(self, rel, old, new):
        with tempfile.TemporaryDirectory() as root:
            for path in (*G.SCAN_FILES, G.AUTHORITY_TABLE):
                dest = os.path.join(root, path)
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                shutil.copy2(os.path.join(REPO_ROOT, path), dest)
            path = os.path.join(root, rel)
            with open(path, encoding="utf-8") as f:
                text = f.read()
            self.assertEqual(text.count(old), 1, "control must match exactly once")
            with open(path, "w", encoding="utf-8") as f:
                f.write(text.replace(old, new))
            return G.build_rows(root)[0]

    def test_real_predicate_rows_have_original_policy_and_actual_owner(self):
        rows = G.build_rows(REPO_ROOT)[0]
        actual = {(r.verb, r.subverb, r.func, r.transports, r.authority)
                  for r in rows if r.func in {"action_sleep_admit", "action_crash_admit", "parse_confirm_token"}}
        self.assertEqual(actual, {
            ("sleep", "off", "action_sleep_admit", "serial,ble", "operator D"),
            ("factory_reset", "confirm", "parse_confirm_token", "serial,ble", "owner D"),
            *(("crashtest", mode, "action_crash_admit", "serial,ble", "owner D")
              for mode in ("hang", "fault", "reboot")),
        })
        self.assertEqual(len(rows), 204)

    def test_each_new_caller_hop_must_exist(self):
        for rel, old, new in (
            ("src/firmware_commands.cpp", "action_sleep_admit(arg, n,", "wrong_sleep(arg, n,"),
            ("src/firmware_commands.cpp", "action_factory_reset_admit(arg, n,", "wrong_factory(arg, n,"),
            ("src/firmware_action_effects.h", "parse_confirm_token(arg, n)", "wrong_confirm(arg, n)"),
            ("src/fw_main.cpp", "mrfw::action_crash_admit(args,", "mrfw::wrong_crash(args,"),
        ):
            with self.subTest(rel=rel, call=old), self.assertRaisesRegex(G.GeneratorError, "no call"):
                self.modified_rows(rel, old, new)

    def test_predicate_enclosing_feature_gate_is_preserved(self):
        path = "src/firmware_config_parse.h"
        lo, hi = G._function_spans(REPO_ROOT, path)["parse_confirm_token"]
        with open(os.path.join(REPO_ROOT, path), encoding="utf-8") as f:
            body = "\n".join(f.read().split("\n")[lo - 1:hi])
        rows = self.modified_rows(path, body, "#if MR_FEAT_TEST\n" + body + "\n#endif")
        predicate = [r for r in rows if r.func == "parse_confirm_token"]
        self.assertEqual(len(predicate), 1)
        self.assertEqual(predicate[0].gate, "MR_FEAT_TEST")

    def test_empty_confirmation_predicate_is_refused(self):
        with self.assertRaisesRegex(G.GeneratorError, "produced NO rows"):
            self.modified_rows("src/firmware_config_parse.h", 'strncmp(s, "confirm", 7)', '0')

    def test_missing_confirmation_predicate_is_refused(self):
        with self.assertRaisesRegex(G.GeneratorError, "no longer resolves"):
            self.modified_rows("src/firmware_config_parse.h", "bool parse_confirm_token(", "bool missing_confirm_token(")

    def test_duplicate_confirmation_predicate_is_refused(self):
        with self.assertRaisesRegex(G.GeneratorError, "exactly one definition"):
            self.modified_rows("src/firmware_config_parse.h", "bool parse_confirm_token(",
                               'bool parse_confirm_token() { return false; }\ninline bool parse_confirm_token(')

    def test_new_action_header_dispatcher_cannot_be_silently_omitted(self):
        with self.assertRaisesRegex(G.GeneratorError, "unclassified function"):
            self.modified_rows("src/firmware_action_effects.h", "} // namespace mrfw",
                               'inline bool unclassified(const char* x) { return !strcmp(x, "newverb"); }\n} // namespace mrfw')

    def test_comment_and_string_shadows_do_not_create_predicates(self):
        rows = self.modified_rows("src/firmware_config_parse.h", "bool parse_confirm_token(",
            '// bool parse_confirm_token() { return false; }\n'
            'const char* example = "bool parse_confirm_token() { }";\ninline bool parse_confirm_token(')
        self.assertEqual(len(rows), 204)


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

    def test_cfg_key_buffer_fits_every_inventory_key(self):
        with open(os.path.join(REPO_ROOT, "src", "firmware_config.cpp"), encoding="utf-8") as fh:
            source = fh.read()
        code = G._blank_comments_and_literals(source)
        bound = [m.group(1) for m in re.finditer(r'constexpr size_t kCfgKeyMaxBytes = sizeof\("([^"]+)"\);', source)
                 if code[m.start():m.start() + 9] == "constexpr"]
        self.assertEqual(len(bound), 1)
        self.assertIn("char key[kCfgKeyMaxBytes]", code)
        capacity = len(bound[0].encode("ascii")) + 1
        keys = {r.subverb.split()[0] for r in self.rows if r.verb == "cfg set" and r.subverb != "—"}
        self.assertTrue(keys)
        self.assertIn("remote_action_activation_ms", keys)
        self.assertIn("gw_announce_interval", keys)
        self.assertGreater(capacity, max(map(len, keys)))

    # Slice 0e's test_every_authority_cell_is_empty is now the opposite obligation.
    def test_every_row_carries_exactly_one_authority(self):
        self.assertTrue(self.rows)
        table = G.read_authority_table(REPO_ROOT)
        surfaces = {"%s::%s" % (s.file, s.func): s for s in G.SURFACES}
        for r in self.rows:
            self.assertEqual(G.authority_cell(table[G.semantic_key(r)], G.surface_eligibility(surfaces[r.surface])), r.authority)
        path = os.path.join(REPO_ROOT, G.TRACKED_OUTPUT)
        with open(path, "r", encoding="utf-8") as fh:
            body = fh.read()
        for line in body.split("\n"):
            if line.startswith("| `") and line.count("|") == 8:
                self.assertTrue(G.markdown_cells(line)[-1], "an authority cell must not be blank: %s" % line)

    def test_the_help_family_is_present_in_the_tracked_table(self):
        """§0g: the help surface yields EXACTLY the primary `help (alias: ?)` row — the nine topics are RETIRED.

        This row used to pin the two topic-gating macros (`MR_HELP_HAS_MOBILE`/`MR_HELP_HAS_REMOTE`). The owner's
        2026-09-05 ruling removed the sections those macros gated, so the macros went with them: keeping them alive
        merely to keep this assertion green would be preserving a dead gate to satisfy its own test.
        """
        rows, _n, _v, _r = G.build_rows(REPO_ROOT)
        help_rows = [r for r in rows if r.func == "help_command"]
        self.assertEqual(1, len(help_rows),
                         "the retired `help <topic>` sub-verbs must not come back as inventory rows")
        primary = help_rows[0]
        self.assertEqual("help (alias: ?)", primary.verb)
        self.assertEqual("—", primary.subverb)
        self.assertEqual("—", primary.gate, "bare `help`/`?` is compiled into every product profile")
        self.assertEqual("src/firmware_help.h", primary.source.split(":")[0])
        with open(os.path.join(REPO_ROOT, "src", "firmware_help.h"), encoding="utf-8") as fh:
            header = fh.read()
        for retired in ("MR_HELP_HAS_MOBILE", "MR_HELP_HAS_REMOTE"):
            self.assertNotIn(retired, header, f"{retired} was retired with the topic sections it gated")

    def test_no_help_text_line_became_a_command_row(self):
        """Only the router may yield rows from the help header — never a rendered name line."""
        rows, _n, _v, _r = G.build_rows(REPO_ROOT)
        from_header = [r for r in rows if r.source.startswith("src/firmware_help.h")]
        self.assertTrue(from_header)
        self.assertTrue(all(r.func == "help_command" for r in from_header),
                        "only the router may yield rows from the help header")

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
        # §0a: `help`/`?` recognition moved from `dispatch` into `src/firmware_help.h::help_command`, and BLE still
        # refuses it before the text fallback — so the row is `serial` there and `ble` (the refusal) in fw_main.
        self.assertIn(("help (alias: ?)", "—", "help_command", "serial"), cells)
        self.assertIn(("help (alias: ?)", "—", "ble_dispatch_line", "ble"), cells)
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


# ---------------------------------------------------------------------------------------------------------------
# §0g — the PRIMARY-VERB PROJECTION
# ---------------------------------------------------------------------------------------------------------------
# ★ A SEPARATE SYNTHETIC TREE, so the projection's rules are proven on shapes the real tree may not currently hold.
#   It adds, to the baseline fixture: a MULTIWORD top-level arm (`cfg set `), a DUPLICATE arm, a WORD alias pair, two
#   COMPLEMENTARY gated arms for one spelling, and a RADIO-ONLY surface. The baseline already supplies serial+BLE
#   arms, a BLE-only caller, a sub-verb dispatcher, a gated arm and the `help`/`?` punctuation alias.
PROJ_ARMS = (
    '    if (len > 8 && !strncmp(line, "cfg set ", 8)) { handle_cfg_set(line + 8, out); return true; }\n'
    # The old fixture had same-gate duplicate arms. Slice 6 rejects those; keep the projection's
    # union test with complementary gates, and test same-gate duplicate refusal separately below.
    '#if MR_FEAT_WIDGET\n'
    '    if (len == 4 && !strncmp(line, "quit", 4)) { do_quit(out); return true; }\n'
    '#else\n'
    '    if (len == 4 && !strncmp(line, "quit", 4)) { do_quit(out); return true; }\n'
    '#endif\n'
    '    if ((len == 5 && !strncmp(line, "erase", 5)) || (len == 4 && !strncmp(line, "wipe", 4)))'
    ' { do_erase(out); return true; }\n'
    '#if MR_FEAT_WIDGET\n'
    '    if (len == 5 && !strncmp(line, "gizmo", 5)) { do_gizmo_a(out); return true; }\n'
    '#else\n'
    '    if (len == 5 && !strncmp(line, "gizmo", 5)) { do_gizmo_b(out); return true; }\n'
    '#endif\n'
    '    return false;\n'
    '}\n'
    '\n'
    'static size_t remote_encode(const char* verb, size_t n, uint8_t* enc) {\n'
    '    if (n == 6 && !strncmp(verb, "reboot", 6)) { return enc_reboot(enc); }\n'
    '    return 0;\n'
    '}\n'
)

PROJ_SURFACES = FIX_SURFACES + (
    G.Surface("src/firmware_commands.cpp", "remote_encode", "remote", "radio(REMOTE_CMD)"),
)
# MR_HELP_HAS_MOBILE is the FIXTURE help router's own gate; it is a profile axis HERE so the projection can be
# exercised with that arm both compiled and compiled out.
PROJ_ON = {"MR_FEAT_WIDGET": 1, "MR_HELP_HAS_MOBILE": 1}
PROJ_OFF = {"MR_FEAT_WIDGET": 0, "MR_HELP_HAS_MOBILE": 0}


def _proj_tree(extra_arms=""):
    return FixtureTree({"src/firmware_commands.cpp":
                        lambda body: body.replace("    return false;\n}\n", extra_arms + PROJ_ARMS, 1)},
                       surfaces=PROJ_SURFACES)


class TestPrimaryProjection(unittest.TestCase):
    """§0g: the reduction from classified rows to the console's bare primary command names."""

    def test_the_eight_rules_on_one_tree(self):
        with _proj_tree() as t:
            names = G.primary_names(t.rows(), PROJ_ON)
        self.assertEqual(["cfg", "erase", "gizmo", "help", "quit", "send", "status", "thing", "widget", "wipe"],
                         names)
        # (4) the multiword arm collapsed to its first token, and `set` never became a command
        self.assertNotIn("set", names)
        # (5) the punctuation alias is help's spelling, not a second command
        self.assertNotIn("?", names)
        # (2) sub-verbs, BLE-only caller arms and the radio-only surface are all excluded
        for excluded in ("alpha", "beta", "whoami", "extra", "reboot"):
            self.assertNotIn(excluded, names)
        # (7) the duplicate `quit` arm appears exactly once
        self.assertEqual(1, names.count("quit"))
        # (8) bytewise ascending
        self.assertEqual(sorted(set(names), key=lambda n: n.encode()), names)

    def test_a_word_alias_is_an_independent_primary_name(self):
        with _proj_tree() as t:
            rows = t.rows()
            names = G.primary_names(rows, PROJ_ON)
        self.assertIn("erase", names)
        self.assertIn("wipe", names, "a word alias is a spelling a user can actually type")
        self.assertIn("erase (alias: wipe)", {r.verb for r in rows}, "...recorded as ONE inventory row")

    def test_a_disabled_feature_row_disappears_and_its_complementary_twin_does_not(self):
        with _proj_tree() as t:
            rows = t.rows()
            on, off = G.primary_names(rows, PROJ_ON), G.primary_names(rows, PROJ_OFF)
        self.assertIn("widget", on)
        self.assertNotIn("widget", off, "a gated arm this build does not compile must not be advertised")
        self.assertIn("gizmo", on)
        self.assertIn("gizmo", off, "complementary #if/#else arms mean the verb is ALWAYS compiled")
        self.assertEqual({"widget"}, set(on) - set(off))

    def test_a_new_source_command_changes_the_list_without_touching_the_projection(self):
        """The oracle must be self-maintaining: adding a dispatch arm is the ONLY edit required."""
        with _proj_tree() as t:
            before = G.primary_names(t.rows(), PROJ_ON)
        with _proj_tree('    if (len == 6 && !strncmp(line, "zzznew", 6)) { do_new(out); return true; }\n') as t:
            after = G.primary_names(t.rows(), PROJ_ON)
        self.assertEqual(["zzznew"], sorted(set(after) - set(before)))

    def test_an_empty_projection_REFUSES(self):
        with self.assertRaises(G.GeneratorError):
            G.primary_projection([], PROJ_ON)

    def test_a_gate_naming_a_macro_outside_the_profile_REFUSES(self):
        with _proj_tree() as t:
            rows = t.rows()
        with self.assertRaises(G.GeneratorError):
            G.primary_names(rows, {"MR_HELP_HAS_MOBILE": 1})   # MR_FEAT_WIDGET missing: refuse, never assume 0

    def test_only_the_top_and_parse_command_surfaces_are_primary(self):
        primary = {(s.file, s.func) for s in G.SURFACES if G.is_primary_surface(s)}
        self.assertIn(("src/firmware_commands.cpp", "dispatch"), primary)
        self.assertIn(("src/firmware_help.h", "help_command"), primary)
        self.assertIn(("lib/console/console_parse.cpp", "parse_command"), primary)
        self.assertNotIn(("src/fw_main.cpp", "service_console"), primary)
        self.assertNotIn(("src/fw_main.cpp", "ble_dispatch_line"), primary)
        self.assertNotIn(("src/firmware_config.cpp", "handle_cfg_set"), primary)

    def test_the_real_tree_projects_the_manual_s_primary_names(self):
        """The manual's primary-name count is REPRODUCED from source, never copied into it.

        ⚠ 49 -> 51 (2026-09-06, §RADMIN slice 3): the ACCEPT-only `acl` and `admin-id` families. The count is a
          FULL-BUILD figure, so it moves on the four ACCEPT profiles and ⛔ NOT on the two mobile ones — which
          `TestRadminAcceptAxis` below pins per profile rather than leaving to this one number.
        """
        rows, _n, _v, _r = G.build_rows(REPO_ROOT)
        self.assertEqual(51, len(G.primary_names(rows, G.PROFILES["full_oled"])))
        # ...and the CLIENT arm is the control: a single full-build number could not tell a product GATE from a
        # global addition. `mobile_oled` projects 46 = its 39 router-owned forms + the 7 parser-owned ones, and
        # ⛔ neither target-store family is among them (pinned by name in TestRadminAcceptAxis below).
        mob = G.primary_names(rows, G.PROFILES["mobile_oled"])
        # ⚠ 46 -> 48 (2026-09-06, §RADMIN slice 4): the CLIENT-only `admin-key` and `admin-target` families. The
        #   full-build figure above is UNCHANGED at 51 because the CLIENT axis is 0 on every ACCEPT profile — the
        #   mirror image of the slice-3 movement, and exactly the asymmetry that makes these two numbers a GATE
        #   test rather than a global-addition test.
        self.assertEqual(48, len(mob))
        self.assertNotIn("acl", mob)
        self.assertNotIn("admin-id", mob)
        self.assertIn("admin-key", mob)
        self.assertIn("admin-target", mob)


# ================================================================================================================
# [[B319]] — the FIFTH profile axis, `MR_FEAT_RADMIN_ACCEPT` (§RADMIN slice 3).
#
# ★★★ WHY THIS CLASS EXISTS. `eval_gate` REFUSES a macro the profile table does not name, deliberately: an
#     undefined macro is 0 in a real `#if`, but here that would silently DROP a command from the expected help
#     list. The first ACCEPT-gated dispatch arm therefore could not be projected at all until the table learned the
#     axis — and the axis had to be added as SIX LITERAL RULED VALUES, never computed from a neighbouring macro.
# ================================================================================================================
class TestRadminAcceptAxis(unittest.TestCase):

    RULED = {"full_oled": 1, "full_headless": 1, "gateway": 1, "gateway_oled": 1, "mobile": 0, "mobile_oled": 0}

    def test_every_profile_declares_the_axis_with_its_ruled_literal_value(self):
        self.assertEqual(sorted(self.RULED), sorted(G.PROFILES))
        for name, want in self.RULED.items():
            self.assertIn("MR_FEAT_RADMIN_ACCEPT", G.PROFILES[name],
                          f"{name}: the axis must be DECLARED, or eval_gate refuses the first gated arm")
            self.assertEqual(want, G.PROFILES[name]["MR_FEAT_RADMIN_ACCEPT"], f"{name}: wrong ruled value")

    def test_the_axis_is_evaluated_and_separates_the_profiles(self):
        for name, want in self.RULED.items():
            self.assertEqual(bool(want), G.eval_gate("MR_FEAT_RADMIN_ACCEPT", G.PROFILES[name]))

    def test_a_profile_missing_the_axis_still_REFUSES(self):
        """The refusal `eval_gate` has always made is PRESERVED — adding a column must not weaken it."""
        stripped = {k: v for k, v in G.PROFILES["gateway"].items() if k != "MR_FEAT_RADMIN_ACCEPT"}
        with self.assertRaises(G.GeneratorError):
            G.eval_gate("MR_FEAT_RADMIN_ACCEPT", stripped)

    def test_an_unrelated_unknown_macro_still_REFUSES(self):
        with self.assertRaises(G.GeneratorError):
            G.eval_gate("MR_FEAT_SOMETHING_NOBODY_DECLARED", G.PROFILES["gateway"])

    def test_the_axis_is_INDEPENDENT_of_the_legacy_switch(self):
        """A SYNTHETIC evaluator fixture: the legacy value varies while ACCEPT is held fixed, and vice versa.

        ⛔ NEITHER combination below is a newly legal BOARD profile — `lib/core/mr_features.h` carries an `#error`
           that makes the two agree until Slice 10 deletes the legacy switch. The point is that the GENERATOR reads
           two independent columns, so the day that `#error` goes the table keeps measuring instead of aliasing.
        """
        base = dict(G.PROFILES["gateway"])
        for legacy in (0, 1):
            m = dict(base, MR_FEAT_REMOTE_MGMT=legacy, MR_FEAT_RADMIN_ACCEPT=1)
            self.assertTrue(G.eval_gate("MR_FEAT_RADMIN_ACCEPT", m))
            self.assertEqual(bool(legacy), G.eval_gate("MR_FEAT_REMOTE_MGMT", m))
        for legacy in (0, 1):
            m = dict(base, MR_FEAT_REMOTE_MGMT=legacy, MR_FEAT_RADMIN_ACCEPT=0)
            self.assertFalse(G.eval_gate("MR_FEAT_RADMIN_ACCEPT", m))

    def test_the_axis_is_NOT_derived_from_MR_FEAT_MOBILE(self):
        """The two FULL static profiles set MR_FEAT_MOBILE=1 AND ACCEPT=1 — so that inference is simply false."""
        for name in ("full_oled", "full_headless"):
            self.assertEqual(1, G.PROFILES[name]["MR_FEAT_MOBILE"])
            self.assertEqual(1, G.PROFILES[name]["MR_FEAT_RADMIN_ACCEPT"])

    def test_the_generator_source_carries_no_derivation_of_the_axis(self):
        """A literal typed column, ⛔ never computed inside the tool (that is [[B319]]'s whole point)."""
        with open(os.path.join(REPO_ROOT, "tools", "gen_command_inventory.py"), encoding="utf-8") as fh:
            text = fh.read()
        table = text[text.index("PROFILES = {"):text.index("PROFILE_ENVS")]
        for name, want in self.RULED.items():
            self.assertIn("MR_FEAT_RADMIN_ACCEPT=%d" % want, table)
        for forbidden in ("MR_FEAT_RADMIN_ACCEPT=MR_FEAT_REMOTE_MGMT", "MR_FEAT_RADMIN_ACCEPT = MR_FEAT",
                          "not MR_FEAT_MOBILE"):
            self.assertNotIn(forbidden, table)

    def test_the_real_ACCEPT_gated_rows_project_onto_exactly_the_four_accept_profiles(self):
        """The rows are the REAL recorded ones, and the two families appear iff the profile is an ACCEPT build."""
        rows, _n, _v, _r = G.build_rows(REPO_ROOT)
        gated = [r for r in rows if r.gate == "MR_FEAT_RADMIN_ACCEPT"]
        self.assertEqual({"acl", "admin-id"}, {r.verb for r in gated},
                         "the ACCEPT-gated top-level rows are exactly the two target-store families")
        self.assertTrue(all(r.transports == "serial" for r in gated),
                        "R-RA-29: the family is recorded SERIAL-only — BLE refuses it before the seam")
        for name, want in self.RULED.items():
            names = set(G.primary_names(rows, G.PROFILES[name]))
            for verb in ("acl", "admin-id"):
                if want:
                    self.assertIn(verb, names, f"{name}: an ACCEPT build must advertise `{verb}`")
                else:
                    self.assertNotIn(verb, names, f"{name}: a CLIENT build must NOT advertise `{verb}`")


# ================================================================================================================
# [[B319]]'s TWIN — the SIXTH profile axis, `MR_FEAT_RADMIN_CLIENT` (§RADMIN slice 4), and R-RA-30's per-row
# transport split.
#
# ★★★ WHY IT IS A SECOND CLASS AND NOT A PARAMETER OF THE FIRST. The two axes are COMPLEMENTARY on every row of
#     the product table and INDEPENDENT as columns, and the difference is exactly what must be measured: they are
#     complementary because `lib/core/mr_features.h`'s R-RA-17 `#error` makes a BOARD exactly one endpoint — while
#     the HOST is deliberately BOTH. A test that derived one from the other would already be describing the host
#     wrongly, and would stop measuring the day a third product role exists.
# ================================================================================================================
class TestRadminClientAxis(unittest.TestCase):

    RULED = {"full_oled": 0, "full_headless": 0, "gateway": 0, "gateway_oled": 0, "mobile": 1, "mobile_oled": 1}

    def test_every_profile_declares_the_axis_with_its_ruled_literal_value(self):
        self.assertEqual(sorted(self.RULED), sorted(G.PROFILES))
        for name, want in self.RULED.items():
            self.assertIn("MR_FEAT_RADMIN_CLIENT", G.PROFILES[name],
                          f"{name}: the axis must be DECLARED, or eval_gate refuses the first gated arm")
            self.assertEqual(want, G.PROFILES[name]["MR_FEAT_RADMIN_CLIENT"], f"{name}: wrong ruled value")

    def test_the_axis_is_evaluated_and_separates_the_profiles(self):
        for name, want in self.RULED.items():
            self.assertEqual(bool(want), G.eval_gate("MR_FEAT_RADMIN_CLIENT", G.PROFILES[name]))

    def test_a_profile_missing_the_axis_still_REFUSES(self):
        """The refusal `eval_gate` has always made is PRESERVED — adding a SIXTH column must not weaken it."""
        stripped = {k: v for k, v in G.PROFILES["mobile"].items() if k != "MR_FEAT_RADMIN_CLIENT"}
        with self.assertRaises(G.GeneratorError):
            G.eval_gate("MR_FEAT_RADMIN_CLIENT", stripped)

    def test_the_two_radmin_axes_are_read_INDEPENDENTLY(self):
        """A SYNTHETIC evaluator fixture: all FOUR combinations are evaluated, including the two the product table
        never carries. ⛔ NEITHER {1,1} nor {0,0} is a legal BOARD (R-RA-17's `#error`) — {1,1} IS the host, and
        {0,0} is nothing. The point is that the GENERATOR reads two columns, so it cannot alias them."""
        base = dict(G.PROFILES["gateway"])
        for accept in (0, 1):
            for client in (0, 1):
                m = dict(base, MR_FEAT_RADMIN_ACCEPT=accept, MR_FEAT_RADMIN_CLIENT=client)
                self.assertEqual(bool(accept), G.eval_gate("MR_FEAT_RADMIN_ACCEPT", m))
                self.assertEqual(bool(client), G.eval_gate("MR_FEAT_RADMIN_CLIENT", m))

    def test_the_axis_is_NOT_the_inverse_of_ACCEPT_inside_the_tool(self):
        """A literal typed column, ⛔ never computed — [[B319]]'s point, restated for its twin."""
        with open(os.path.join(REPO_ROOT, "tools", "gen_command_inventory.py"), encoding="utf-8") as fh:
            text = fh.read()
        table = text[text.index("PROFILES = {"):text.index("PROFILE_ENVS")]
        for name, want in self.RULED.items():
            self.assertIn("MR_FEAT_RADMIN_CLIENT=%d" % want, table)
        for forbidden in ("MR_FEAT_RADMIN_CLIENT=MR_FEAT", "MR_FEAT_RADMIN_CLIENT = MR_FEAT",
                          "not MR_FEAT_RADMIN_ACCEPT", "1 - MR_FEAT_RADMIN_ACCEPT"):
            self.assertNotIn(forbidden, table)

    def test_the_axis_is_NOT_derived_from_MR_FEAT_MOBILE(self):
        """The two FULL static profiles set MR_FEAT_MOBILE=1 AND CLIENT=0 — so that inference is simply false."""
        for name in ("full_oled", "full_headless"):
            self.assertEqual(1, G.PROFILES[name]["MR_FEAT_MOBILE"])
            self.assertEqual(0, G.PROFILES[name]["MR_FEAT_RADMIN_CLIENT"])

    def test_the_real_CLIENT_gated_rows_project_onto_exactly_the_two_mobile_profiles(self):
        rows, _n, _v, _r = G.build_rows(REPO_ROOT)
        gated = [r for r in rows if r.gate == "MR_FEAT_RADMIN_CLIENT"]
        self.assertEqual({"admin-key", "admin-target"}, {r.verb for r in gated},
                         "the CLIENT-gated top-level rows are exactly the two controller-store families")
        self.assertTrue(all(r.transports == "serial" for r in gated),
                        "R-RA-30: a BARE family name is console-only — only list/show cross secured BLE")
        for name, want in self.RULED.items():
            names = set(G.primary_names(rows, G.PROFILES[name]))
            for verb in ("admin-key", "admin-target"):
                if want:
                    self.assertIn(verb, names, f"{name}: a CLIENT build must advertise `{verb}`")
                else:
                    self.assertNotIn(verb, names, f"{name}: an ACCEPT build must NOT advertise `{verb}`")

    def test_R_RA_30_is_recorded_PER_SUB_VERB_and_only_for_list_and_show(self):
        """The ruling's split is in the AUTHORITY TABLE, per row — not inferred from a name shape."""
        rows, _n, _v, _r = G.build_rows(REPO_ROOT)
        fam = [r for r in rows if r.verb in ("admin-key", "admin-target") and r.subverb != "—"]
        self.assertTrue(fam, "the controller sub-verb rows must exist at all")
        for r in fam:
            first = r.subverb.split(" ")[0]
            want = "serial,ble" if first in ("list", "show") else "serial"
            self.assertEqual(want, r.transports,
                             f"R-RA-30: `{r.verb} {r.subverb}` must be {want}, not {r.transports}")
        public = {(r.verb, r.subverb) for r in fam if r.transports == "serial,ble"}
        self.assertEqual({("admin-key", "list"), ("admin-key", "show"), ("admin-key", "show self"),
                          ("admin-target", "list"), ("admin-target", "show")}, public)
        # ⛔ AND THE SECRET HALF IS NAMED, so a widened override cannot pass unnoticed.
        for secret in (("admin-key", "generate"), ("admin-key", "import"), ("admin-key", "export"),
                       ("admin-key", "remove"), ("admin-key", "reset"), ("admin-target", "add"),
                       ("admin-target", "set"), ("admin-target", "remove"), ("admin-target", "reset")):
            self.assertNotIn(secret, public, f"{secret} must be USB-only (design §6.2)")

    def test_the_override_never_widens_a_surface_it_was_not_spelled_on(self):
        """A LITERAL table, ⛔ not a rule about the word `list`: the target family's `acl list` stays serial."""
        rows, _n, _v, _r = G.build_rows(REPO_ROOT)
        acl_list = [r for r in rows if r.verb == "acl" and r.subverb == "list"]
        self.assertEqual(1, len(acl_list))
        self.assertEqual("serial", acl_list[0].transports,
                         "R-RA-29 refuses the WHOLE target family over BLE — `acl list` included")


class TestSlice6Normalization(unittest.TestCase):
    ARMS = '''\
#if MR_N_LAYERS < 2
    if (!strncmp(line, "joinprofile", 11)) { handle_joinprofile(line, out); return true; }
#else
    if (!strncmp(line, "joinprofile", 11)) {
        out.println(F("> err gateway_build (joinprofile is normal-node only)"));
        return true;
    }
#endif
    if (len == 5 && !strncmp(line, "peers", 5)) { dump_peers(out); return true; }
    if (len > 5 && !strncmp(line, "peers ", 6)) {
        if (!strncmp(args, "all", 3)) { dump_all(out); return true; }
        return true;
    }
'''

    def fixture(self):
        return FixtureTree({"src/firmware_commands.cpp": lambda s: s.replace("    return false;", self.ARMS + "    return false;", 1)})

    def test_joinprofile_keeps_both_gates_anchors_and_refusal(self):
        with self.fixture() as t:
            rows = [r for r in t.rows() if r.verb == "joinprofile"]
        self.assertEqual(2, len(rows))
        self.assertEqual({"MR_N_LAYERS < 2", "!(MR_N_LAYERS < 2)"}, {r.gate for r in rows})
        self.assertEqual(2, len({r.source for r in rows}))
        self.assertEqual({"—", "— refused gateway_build"}, {r.subverb for r in rows})
        self.assertEqual({("joinprofile", "—")}, {G.semantic_key(r) for r in rows})

    def test_equalized_gates_and_dropped_discriminator_refuse(self):
        with self.fixture() as t:
            rows = [r for r in t.rows() if r.verb == "joinprofile"]
        rows[1].gate, rows[1].subverb = rows[0].gate, rows[0].subverb
        with self.assertRaisesRegex(G.GeneratorError, "duplicate normalized row"):
            G.verify_rows(rows)

    def test_peers_level_guard_is_not_an_extra_bare_row(self):
        with self.fixture() as t:
            rows = [r for r in t.rows() if r.verb == "peers"]
        self.assertEqual(["all", "—"], sorted(r.subverb for r in rows))

    def test_real_normalization_and_discriminator_bindings(self):
        rows = G.build_rows(REPO_ROOT)[0]
        self.assertEqual(204, len(rows))  # Slice 7a: one cfg key added to Slice 6's 203 normalized rows.
        refusals = [r for r in rows if "— refused" in r.subverb]
        self.assertEqual({("peers", "<args> — refused console_only"), ("joinprofile", "— refused gateway_build")},
                         {(r.verb, r.subverb) for r in refusals})
        self.assertTrue(all(G.semantic_key(r)[1] == "—" for r in refusals))

    def test_missing_ruled_classification_refuses_generation(self):
        with FixtureTree() as t:
            path = os.path.join(t.root, G.AUTHORITY_TABLE)
            with open(path, encoding="utf-8") as fh:
                table = fh.read()
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(table.replace("| `status` | `—` | operator | no | synthetic fixture |\n", "", 1))
            with self.assertRaisesRegex(G.GeneratorError, "unclassified inventory row"):
                t.rows()


if __name__ == "__main__":
    unittest.main()
