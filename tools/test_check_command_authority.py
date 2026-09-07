"""Independent artifact binding controls; part of the tools unittest sweep."""
from pathlib import Path
import unittest

import check_command_authority as C
import gen_command_inventory as G


class TestCommandAuthority(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(G.REPO_ROOT)
        cls.texts = tuple((root / p).read_text(encoding="utf-8") for p in (G.AUTHORITY_TABLE, C.HEADER, G.TRACKED_OUTPUT))

    def test_three_real_artifacts_agree(self):
        self.assertEqual([], C.check(*self.texts))

    def test_six_controls_reject_their_own_named_defect(self):
        self.assertEqual([], C.selftest(self.texts))

    def test_empty_table_refuses(self):
        self.assertTrue(C.check("## Semantic policy\n", *self.texts[1:]))

    def test_empty_production_initializer_refuses(self):
        table, header, inventory = self.texts
        start = header.index("inline constexpr CommandPolicy kCommandPolicy[] = {")
        end = header.index("\n};", start)
        header = header[:start] + "inline constexpr CommandPolicy kCommandPolicy[] = {" + header[end:]
        self.assertTrue(C.check(table, header, inventory))

    def test_bare_mentions_do_not_replace_the_positional_table(self):
        table, header, inventory = self.texts
        self.assertTrue(C.check(table.replace("## Semantic policy", "## Historical mention", 1), header, inventory))

    def test_header_duplicate_refuses(self):
        table, header, inventory = self.texts
        row = '    {"acl", "—", CommandClass::owner, false},'
        self.assertIn(row, header)
        self.assertTrue(any("duplicate" in s for s in C.check(table, header.replace(row, row + "\n" + row, 1), inventory)))

    def test_legacy_surface_is_not_a_second_semantic_class(self):
        rows = C.parse_inventory(self.texts[2])
        status = [r for r in rows if r.verb == "status"]
        self.assertEqual({"open", "open · surface:transport", "open · surface:legacy"}, {r.authority for r in status})

    def test_disruptive_flag_disagreement_refuses(self):
        table, header, inventory = self.texts
        old = '    {"team", "new", CommandClass::owner, true},'
        self.assertIn(old, header)
        self.assertTrue(any("table/header disagreement" in s for s in C.check(table, header.replace(old, old.replace("true", "false"), 1), inventory)))


if __name__ == "__main__":
    unittest.main()
