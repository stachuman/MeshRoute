#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""B487/B488: the transcript comparator's regressions (B10) — the real functions and the stock CLI.

1. refusals that need no build; 2. the ONE validity contract (review TPR-1), no build; 3. both profiles end to end
with their pinned records; 4. reproducibility; and the classified controls B-C0..B-C6 (review TPR-2), each checked
for ITS OWN required outcome — never "any nonzero exit"."""

import concurrent.futures
import contextlib
import importlib.util
import io
import itertools
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / "tools" / "probe_inbox_verbs"
TOOL = HERE / "transcript.py"
DRIVER = HERE / "transcript_main.cpp"
FW_MAIN = ROOT / "src" / "fw_main.cpp"
REVIEW = ROOT / "docs" / "superpowers" / "evidence" / "2026-09-30-b478-b487-b488-brief-review"
ROWS = {"full_headless": 202, "mobile": 206}
TOO_LONG = "> err bad_line too_long\\n"
_COPIES = itertools.count()

# The records each profile must show (measured; the pre-check's labelled diagnostic agrees byte for byte).
# `hash=0x2972535171` is the fake Print's DECIMAL rendering of 0xB12D4983 (B460) — recorded, not endorsed.
_FP = "fp=ctr+0 txq=0 txdrop=0 rt={rt} hw_dm+0 hw_ch+0 wipes+0 nvw+{nvw} kh={kh} pend=0 recs=3/2 routed=-"
_REGEN = '> regen ok  key_hash32= 0xB12D4983  name="transcript"\\r\\n'
_NOTE = "> regen note old self ACL grants do not follow the new key; dedicated keys and targets preserved\\n"
_WHOAMI = '[whoami] id=0 hash=0x2972535171 name="transcript" leaf=0 gw=0 gwonly=0 mobile=0\\r\\n'
_PARSE = "> parse error\\r\\n"
_UNKNOWN = '{"err":"parse","msg":"unknown_cmd"}\\n'


def _rec(usb, ble_ret, buf, nus, rt=0, nvw=0, kh="b12d4983"):
    fp = _FP.format(rt=rt, nvw=nvw, kh=kh)
    return ("  SER usb=[%s] ble=[] %s" % (usb, fp), "  BLE ret=%d buf=[%s] nus=[%s] usb=[] %s" % (ble_ret, buf, nus, fp))


PINNED = {
    "full_headless": {
        "L003": ("acl list", _rec("> acl end count=0 owners=0 operators=0\\n", 37,
                                  '{"err":"admin","msg":"console_only"}\\n', "", kh="00000000")),
        "L133": ("regen", _rec(_REGEN, 0, "", _REGEN, nvw=1)),
        "L166": ("whoami", _rec(_WHOAMI, 0, "", _WHOAMI)),
        "L201": ("z" * 1023, _rec(_PARSE, 36, '{"err":"bad_line","msg":"too_long"}\\n', "", rt=1)),
        "L202": ("12345678", _rec(_PARSE, 36, _UNKNOWN, "", rt=1)),
    },
    "mobile": {
        "L003": ("acl list", _rec(_PARSE, 36, _UNKNOWN, "", kh="00000000")),
        "L133": ("regen", _rec(_REGEN + _NOTE, 0, "", _REGEN + _NOTE, nvw=1)),
        "L170": ("whoami", _rec(_WHOAMI, 0, "", _WHOAMI)),
        "L205": ("z" * 1023, _rec(_PARSE, 36, '{"err":"bad_line","msg":"too_long"}\\n', "", rt=1)),
        "L206": ("12345678", _rec(_PARSE, 36, _UNKNOWN, "", rt=1)),
    },
}

# The exact one-site edits of the controls.
SITE_POINTER = "static void mr0c_serial_tail(const char (&line)[%(extent)s], size_t pos) {"
SITE_LIVE = 'install_live_name_pos(seed_id("transcript", 10, 0x20));'
SITE_PROFILE = ('printf("MR0C-PROFILE MR_N_LAYERS=%d MR_FEAT_MOBILE=%d MR_FEAT_OLED=%d MR_FEAT_RADMIN_ACCEPT=%d "\n'
                '           "MR_FEAT_RADMIN_CLIENT=%d\\n", (int)MR_N_LAYERS, (int)MR_FEAT_MOBILE, (int)MR_FEAT_OLED,\n'
                '           (int)MR_FEAT_RADMIN_ACCEPT, (int)MR_FEAT_RADMIN_CLIENT);')
THREE_MACROS = ('printf("MR0C-PROFILE MR_N_LAYERS=%d MR_FEAT_MOBILE=%d MR_FEAT_OLED=%d\\n",\n'
                '           (int)MR_N_LAYERS, (int)MR_FEAT_MOBILE, (int)MR_FEAT_OLED);')
SITE_BLE_INCLUDE = '#include "device_ble.h"         // the real BLE line store the extracted BLE tail limits a line by\n'


def edit(text, old, new):
    """An exact one-site edit; a site that matches zero or several places is a VACUOUS control (an error)."""
    if text.count(old) != 1:
        raise LookupError("control site matches %d places, not exactly one: %.80r" % (text.count(old), old))
    return text.replace(old, new)


def load(source=None):
    """A FRESH copy of transcript.py as a module — the real text, or an exact edit of it — with the real
    `__file__`, so its HERE/ROOT resolve to this tree. Copies share nothing but the inventory module."""
    spec = importlib.util.spec_from_file_location("mr0c_transcript_copy_%d" % next(_COPIES), TOOL)
    module = importlib.util.module_from_spec(spec)
    if source is None:
        spec.loader.exec_module(module)
    else:
        exec(compile(source, str(TOOL), "exec"), module.__dict__)
    return module


def cli(*args):
    return subprocess.run([sys.executable, "-B", str(TOOL), *map(str, args)], cwd=ROOT, capture_output=True,
                          encoding="utf-8", timeout=1200)


def run_main(t, *args):
    """The stock CLI's entry point, in-process: -> (exit code, stdout, stderr)."""
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = t.main([str(a) for a in args])
    return code, out.getvalue(), err.getvalue()


def esc(data):
    """The driver's own escape (`esc()` in transcript_main.cpp), for synthetic ledgers."""
    return "".join({13: "\\r", 10: "\\n", 92: "\\\\"}.get(b, chr(b) if 0x20 <= b <= 0x7E else "\\x%02x" % b)
                   for b in data)


FP0 = "ctr+0 txq=0 txdrop=0 rt=0 hw_dm+0 hw_ch+0 wipes+0 nvw+0 kh=00000000 pend=0 recs=0/0 routed=-"


def synthetic(t, profile, rows=None, declared=None):
    """A grammatically complete ledger over `rows` (default: the expected matrix) — VALID only when the rows and the
    declared count are the expected matrix."""
    rows = t.expected_matrix(profile) if rows is None else rows
    macros = " ".join("%s=%d" % kv for kv in t.GEN.PROFILES[profile].items())
    lines = ["# transcript label=synthetic shape=post0c profile=%s fw_main=synthetic sha256=synthetic" % profile,
             "MR0C-TRANSCRIPT v1 adapters=post0c/000000000000 lines=%d" % (len(rows) if declared is None else declared),
             "MR0C-PROFILE " + macros]
    for cid, command in rows:
        lines += ["LINE %s len=%d [%s]" % (cid, len(command), esc(command)), "  SER usb=[] ble=[] fp=" + FP0,
                  "  BLE ret=0 buf=[] nus=[] usb=[] fp=" + FP0]
    lines.append("MR0C-TRANSCRIPT END chk=0 fail=0")
    return "\n".join(lines) + "\n"


def rows_of(t, ledger):
    return {row.id: row for row in t.parse_ledger(ledger, "test").rows}


class TranscriptRefusalTests(unittest.TestCase):
    """B10.1 — refusals that need no build."""

    @classmethod
    def setUpClass(cls):
        cls.t = load()
        cls.fw = FW_MAIN.read_text(encoding="utf-8")

    def test_serial_and_ble_anchors_missing_or_duplicated_refuse(self):
        t, fw = self.t, self.fw
        self.assertEqual(set(t.extract_regions(fw)), {"serial", "ble"})
        serial = "line[pos] = '\\0';"
        ble = re.search(r'!strncmp\(line, "del_msg",\s+7\)', fw).group(0)
        for text in (edit(fw, serial, "line[pos] = 0;"), edit(fw, serial, serial + "\n    " + serial),
                     fw.replace(ble, '!strncmp(line, "del_msX", 7)'), edit(fw, ble, ble + "\n        /* " + ble + " */")):
            with self.assertRaises(t.ExtractError):
                t.extract_regions(text)

    def test_usb_array_missing_duplicated_or_reshaped_refuses(self):
        t, fw = self.t, self.fw
        decl = "static char   line[meshroute::console::local_command_max_bytes + 1];"
        self.assertEqual(t.usb_line_extent(fw), "meshroute::console::local_command_max_bytes + 1")
        for why, text in (("missing", edit(fw, decl, "static size_t line_unused = 0;")),
                          ("duplicated", edit(fw, decl, decl + "\n    static char line[8];")),
                          ("reshaped type", edit(fw, decl, decl.replace("char", "unsigned char"))),
                          ("reshaped initialiser", edit(fw, decl, decl[:-1] + " = {};")),
                          ("reshaped rank", edit(fw, decl, decl.replace("];", "][2];")))):
            with self.subTest(why=why), self.assertRaises(t.ExtractError):
                t.usb_line_extent(text)

    def test_builder_boundary_missing_or_duplicated_refuses(self):
        t = self.t
        runner = (HERE / "run.sh").read_text(encoding="utf-8")
        self.assertTrue(t.builder_prefix(runner).endswith("\n"))
        boundary = "rc=0\nif ! build_support;"
        for text in ("#!/bin/bash\nbuild_support() { :; }\n", "x\n" + boundary + "\n" + boundary + "\n",
                     runner.replace(boundary, "rc=0\nif ! build_support ;")):
            with self.assertRaises(t.ExtractError):
                t.builder_prefix(text)
        self.assertFalse(hasattr(t, "DEFAULT_DEFS") or hasattr(t, "default_flags"), "the private recipe is gone")

    def test_unsupported_profile_refuses_before_building(self):
        with tempfile.TemporaryDirectory(prefix="mr0c-refuse-") as tmp:
            for args in (("--profile", "full_oled", "--out", Path(tmp) / "never"),
                         ("--print-matrix", "--profile", "gateway")):
                r = cli(*args)
                self.assertEqual(r.returncode, 2, r.stderr)
                self.assertIn("the supported profiles are full_headless and mobile", r.stderr)
                self.assertEqual(r.stdout, "")
            self.assertFalse((Path(tmp) / "never").exists(), "a refused profile must not reach the build")

    def test_print_matrix_honours_the_profile(self):
        for profile, n in ROWS.items():
            r = cli("--print-matrix", "--profile", profile)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(len([ln for ln in r.stdout.splitlines() if not ln.startswith("#")]), n)
            self.assertIn("# %d rows" % n, r.stdout)

    def test_three_macro_profile_line_is_refused_by_verify_profile(self):
        t = self.t
        with self.assertRaisesRegex(t.ExtractError, "are not profile 'full_headless'"):
            t.verify_profile("MR0C-PROFILE MR_N_LAYERS=1 MR_FEAT_MOBILE=1 MR_FEAT_OLED=0\n", "full_headless")
        t.verify_profile("MR0C-PROFILE MR_N_LAYERS=1 MR_FEAT_MOBILE=1 MR_FEAT_OLED=0 MR_FEAT_RADMIN_ACCEPT=1 "
                         "MR_FEAT_RADMIN_CLIENT=0", "full_headless")

    def test_label_that_would_break_the_head_is_refused(self):
        r = cli("--label", "two words")
        self.assertEqual(r.returncode, 2)
        self.assertIn("--label must be one printable-ASCII token", r.stderr)


class TranscriptValidityTests(unittest.TestCase):
    """B10.2 — ONE validity contract (review TPR-1): `--compare` refuses each invalid pair with exit 2, even when
    both sides are identical. No build."""

    @classmethod
    def setUpClass(cls):
        cls.t = load()
        cls.tmp = Path(tempfile.mkdtemp(prefix="mr0c-validity-"))
        cls.full = synthetic(cls.t, "full_headless")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def compare(self, t, a, b=None):
        pa, pb = self.tmp / "a.txt", self.tmp / "b.txt"
        pa.write_text(a, encoding="ascii")
        pb.write_text(a if b is None else b, encoding="ascii")
        return run_main(t, "--compare", pa, pb)

    def invalid_ledgers(self, t):
        full, lines = self.full, self.full[:-1].split("\n")
        expected = t.expected_matrix("full_headless")
        swapped = list(expected)
        cid, command = swapped[9]
        swapped[9] = (cid, bytes(b"Q"[0] if i == 0 else c for i, c in enumerate(command)))
        ser = next(i for i, ln in enumerate(lines) if ln.startswith("  SER "))
        ble = ser + 1
        rejoin = lambda ls: "\n".join(ls) + "\n"
        return {
            "the review's empty ledger": (REVIEW / "b8-empty-ledger.txt").read_text(encoding="ascii"),
            "the review's one-row ledger": (REVIEW / "b8-one-row-ledger.txt").read_text(encoding="ascii"),
            "a 50-row prefix with its count adjusted": synthetic(t, "full_headless", expected[:50]),
            "an unknown extra row with its count adjusted": synthetic(t, "full_headless",
                                                                     expected + [("L203", b"bogus")]),
            "a missing row with its count adjusted": synthetic(t, "full_headless", expected[:4] + expected[5:]),
            "wrong command bytes at an unchanged ID and length": synthetic(t, "full_headless", swapped),
            "a SER record missing its ble field": rejoin(lines[:ser] + [lines[ser].replace(" ble=[]", "")]
                                                         + lines[ser + 1:]),
            "a BLE record missing its nus field": rejoin(lines[:ble] + [lines[ble].replace(" nus=[]", "")]
                                                         + lines[ble + 1:]),
            "an fp missing its pend field": rejoin(lines[:ser] + [lines[ser].replace(" pend=0", "")]
                                                   + lines[ser + 1:]),
            "a LINE without its SER and BLE": rejoin(lines[:ser] + lines[ser + 2:]),
            "no identity line": rejoin(lines[:1] + lines[2:]),
            "a duplicated identity line": rejoin(lines[:2] + lines[1:]),
            "no profile line": rejoin(lines[:2] + lines[3:]),
            "a duplicated profile line": rejoin(lines[:3] + lines[2:]),
            "a three-macro profile line": rejoin(lines[:2] + [" ".join(lines[2].split()[:4])] + lines[3:]),
            "no END": rejoin(lines[:-1]),
            "a duplicated END": rejoin(lines + lines[-1:]),
            "a line after END": rejoin(lines + ["trailing"]),
            "a head without profile=": full.replace(" profile=full_headless", "", 1),
            "an unsupported declared profile": full.replace("profile=full_headless", "profile=full_oled", 1),
        }

    def validity_failures(self, t):
        """-> [(case, exit, output)] for every invalid pair `--compare` did NOT refuse with exit 2."""
        failures = []
        for why, ledger in self.invalid_ledgers(t).items():
            code, out, err = self.compare(t, ledger)
            if code != 2 or "COMPARE REFUSED" not in err:
                failures.append((why, code, out + err))
        return failures

    def test_invalid_ledgers_are_refused_even_when_identical(self):
        self.assertEqual(self.validity_failures(self.t), [])

    def test_valid_ledgers_still_compare_equal_and_different(self):
        code, out, _ = self.compare(self.t, self.full)
        self.assertEqual(code, 0, out)
        self.assertIn("COMPARE: identical — 202 rows, 606 records", out)
        other = self.full.replace("  SER usb=[] ble=[]", "  SER usb=[x] ble=[]", 1)
        code, out, _ = self.compare(self.t, self.full, other)
        self.assertEqual(code, 1, out)
        self.assertRegex(out, r"\nDIFF L001 SER \[.*\] — usb: A=\[\] B=\[x\]\n")

    def test_mixed_profiles_are_refused(self):
        code, _, err = self.compare(self.t, self.full, synthetic(self.t, "mobile"))
        self.assertEqual(code, 2)
        self.assertIn("A declares profile full_headless and B declares mobile", err)

    def malformed(self, t):
        """QA TQ-1 (B491): ledgers whose NUMBERS or PROFILE FIELDS cannot be read — each must be a named refusal."""
        lines = self.full[:-1].split("\n")
        rejoin = lambda ls: "\n".join(ls) + "\n"
        line1 = next(i for i, ln in enumerate(lines) if ln.startswith("LINE L001 "))
        return {
            "a profile field without `=`": rejoin(lines[:2] + [lines[2] + " malformed"] + lines[3:]),
            "a 5,000-digit identity count": rejoin(lines[:1] + [re.sub(r"lines=\d+", "lines=" + "9" * 5000, lines[1])]
                                                   + lines[2:]),
            "a 5,000-digit LINE length": rejoin(lines[:line1] + [re.sub(r"len=\d+", "len=" + "9" * 5000, lines[line1])]
                                                + lines[line1 + 1:]),
        }

    def test_malformed_numbers_and_profile_fields_are_named_refusals_in_either_position(self):
        """Through the stock CLI, in BOTH ledger positions: exit 2, a named refusal, no traceback."""
        good = self.tmp / "good.txt"
        good.write_text(self.full, encoding="ascii")
        for why, ledger in self.malformed(self.t).items():
            bad = self.tmp / "bad.txt"
            bad.write_text(ledger, encoding="ascii")
            for order in ((bad, good), (good, bad)):
                with self.subTest(why=why, bad_side="A" if order[0] == bad else "B"):
                    r = cli("--compare", *order)
                    self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
                    self.assertIn("COMPARE REFUSED: %s (" % ("A" if order[0] == bad else "B"), r.stderr)
                    self.assertNotIn("Traceback", r.stderr)
        other = self.tmp / "other.txt"                       # the valid pairs keep their 0 / 1
        other.write_text(self.full.replace("  SER usb=[] ble=[]", "  SER usb=[x] ble=[]", 1), encoding="ascii")
        self.assertEqual(cli("--compare", good, good).returncode, 0)
        self.assertEqual(cli("--compare", good, other).returncode, 1)

    def test_malformed_ledgers_are_named_refusals_of_the_shared_validator(self):
        """The fresh-ledger run path and `--compare` share `validate_ledger`: it raises ONLY its named refusal."""
        for why, ledger in self.malformed(self.t).items():
            with self.subTest(why=why):
                with self.assertRaises(self.t.LedgerError) as caught:
                    self.t.validate_ledger(ledger, "the fresh ledger")
                self.assertRegex(str(caught.exception), {
                    "a profile field without `=`": r"^the fresh ledger, line 3: .* not `name=value`: 'malformed'$",
                    "a 5,000-digit identity count": r"^the fresh ledger, line 2: the identity's lines= has 5000 digits",
                    "a 5,000-digit LINE length": r"^the fresh ledger, line 4: LINE L001's len has 5000 digits",
                }[why])

    def test_control_b_c0_trusting_the_ledger_count_lets_the_counterexamples_compare_equal(self):
        b0 = load()
        b0.match_expected = lambda *args, **kwargs: None      # B-C0: the expected-matrix check bypassed
        for name in ("b8-empty-ledger.txt", "b8-one-row-ledger.txt"):
            with self.subTest(name=name):
                code, out, err = run_main(b0, "--compare", REVIEW / name, REVIEW / name)
                self.assertEqual(code, 0, out + err)          # the review's counterexample, reproduced
        failed = {why: code for why, code, _out in self.validity_failures(b0)}    # the regression now FAILS
        for why in ("the review's empty ledger", "the review's one-row ledger", "a 50-row prefix with its count adjusted"):
            self.assertEqual(failed.get(why), 0, failed)


class TranscriptEndToEndTests(unittest.TestCase):
    """B10.3/B10.4 and the build controls. Every build runs once, concurrently, in setUpClass."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="mr0c-e2e-"))
        cls.t = load()
        tool = TOOL.read_text(encoding="utf-8")
        driver = DRIVER.read_text(encoding="utf-8")

        def driver_copy(name, text):
            path = cls.tmp / name / "transcript_main.cpp"
            path.parent.mkdir(parents=True)
            path.write_text(text, encoding="utf-8")
            return str(path)

        b5 = load()
        real_matrix = b5.derive_matrix

        def with_unrepresentable_row(profile=b5.PROBE_PROFILE):          # B-C5: one 1,024-byte row
            matrix, n_projected = real_matrix(profile)
            return matrix + [("L%03d" % (len(matrix) + 1), "z" * 1024, "control B-C5")], n_projected
        b5.derive_matrix = with_unrepresentable_row
        jobs = {
            "full_headless-1": lambda: cls.stock("full_headless", "fh1"),
            "full_headless-2": lambda: cls.stock("full_headless", "fh2", relative=True),
            "mobile-1": lambda: cls.stock("mobile", "mo1"),
            "mobile-2": lambda: cls.stock("mobile", "mo2"),
            "B-C1": lambda: cls.inprocess(load(edit(tool, SITE_POINTER, "static void mr0c_serial_tail(const char* "
                                                                        "line, size_t pos) {")), "c1"),
            "B-C2": lambda: cls.inprocess(cls.t, "c2", driver_copy("c2-driver", edit(driver, SITE_LIVE,
                                          'seed_id("transcript", 10, 0x20);'))),
            "B-C3": lambda: cls.inprocess(cls.t, "c3", driver_copy("c3-driver", edit(driver, SITE_PROFILE,
                                                                                   THREE_MACROS))),
            "B-C5": lambda: cls.inprocess(b5, "c5"),
            "B-C6": lambda: cls.inprocess(cls.t, "c6", driver_copy("c6-driver", edit(driver, SITE_BLE_INCLUDE, ""))),
            "TQ-1": lambda: cls.inprocess(cls.t, "tq1", driver_copy("tq1-driver", edit(
                driver, '"MR_FEAT_RADMIN_CLIENT=%d\\n"', '"MR_FEAT_RADMIN_CLIENT=%d malformed\\n"'))),
        }
        with concurrent.futures.ThreadPoolExecutor(max_workers=len(jobs)) as pool:
            futures = {name: pool.submit(job) for name, job in jobs.items()}
            cls.runs = {name: f.result() for name, f in futures.items()}

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    @classmethod
    def stock(cls, profile, name, relative=False):
        out = cls.tmp / name
        # ⓘ one run passes RELATIVE paths: the runner's prefix `cd`s into its own directory, so every path the tool
        #   hands the builder must already be absolute (a relative `--out` once lost the generated header).
        arg = Path(os.path.relpath(out, ROOT)) if relative else out
        r = cli("--profile", profile, "--out", arg, "--emit", arg / "emitted.txt")
        return {"code": r.returncode, "ledger": r.stdout, "stderr": r.stderr, "out": out}

    @classmethod
    def inprocess(cls, t, name, driver=None):
        out = cls.tmp / name
        out.mkdir()
        res = t.run_profile(str(FW_MAIN), str(out), "full_headless", "control",
                            **({"driver": driver} if driver else {}))
        return {"code": res.code, "ledger": res.ledger, "stderr": "\n".join(res.messages), "out": out,
                "child_rc": res.child_rc, "built": res.built}

    def check_records(self, profile, ledger, only=None):
        rows = rows_of(self.t, ledger)
        for cid, (command, (ser, ble)) in PINNED[profile].items():
            if only is None or cid in only:
                row = rows[cid]
                self.assertEqual(row.command, command.encode(), cid)
                self.assertEqual(("  SER usb=[%s] ble=[%s] fp=%s" % row.ser,
                                  "  BLE ret=%s buf=[%s] nus=[%s] usb=[%s] fp=%s" % row.ble), (ser, ble), cid)

    def test_both_profiles_exit_0_with_five_macros_and_their_row_counts(self):
        for name in ("full_headless-1", "full_headless-2", "mobile-1", "mobile-2"):
            with self.subTest(run=name):
                run, profile = self.runs[name], name.rsplit("-", 1)[0]
                self.assertEqual(run["code"], 0, run["stderr"])
                ledger = self.t.validate_ledger(run["ledger"], name)
                self.assertEqual((ledger.profile, len(ledger.rows)), (profile, ROWS[profile]))
                profile_line = run["ledger"].split("\n")[2]
                self.assertEqual(len(profile_line.split()) - 1, 5, profile_line)
                self.assertEqual((run["out"] / "emitted.txt").read_text(encoding="ascii"), run["ledger"])
                self.assertEqual((run["out"] / "transcript.stderr").read_bytes(), b"", "the driver wrote stderr")

    def test_pinned_records_with_sink_isolation_and_every_fingerprint_field(self):
        for profile in ROWS:
            with self.subTest(profile=profile):
                self.check_records(profile, self.runs[profile + "-1"]["ledger"])

    def test_two_fresh_builds_per_profile_compare_equal(self):
        for profile in ROWS:
            with self.subTest(profile=profile):
                a, b = self.runs[profile + "-1"], self.runs[profile + "-2"]
                self.assertNotEqual(a["out"], b["out"])
                r = cli("--compare", a["out"] / "emitted.txt", b["out"] / "emitted.txt")
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                self.assertIn("COMPARE: identical — %d rows" % ROWS[profile], r.stdout)

    def test_the_build_is_the_runner_prefix_plus_the_appended_lines(self):
        run = self.runs["full_headless-1"]
        shadow = [p for p in run["out"].iterdir() if p.name.startswith("mr0c-shadow-")]
        self.assertEqual(len(shadow), 1)
        self.assertEqual([p.name for p in shadow[0].iterdir()], ["mr0c_adapters.h"], "the shadow dir holds one header")
        prefix = self.t.builder_prefix((HERE / "run.sh").read_text(encoding="utf-8"))
        appended = self.t.builder_appended(str(shadow[0]), str(DRIVER), str(run["out"] / "transcript.bin"))
        self.assertEqual((run["out"] / "build.sh").read_text(encoding="utf-8"),
                         prefix + "\n" + "\n".join(appended) + "\n")
        header = (shadow[0] / "mr0c_adapters.h").read_text(encoding="utf-8")
        for region in self.t.extract_regions(FW_MAIN.read_text(encoding="utf-8")).values():
            self.assertIn(region, header, "an extracted region's bytes changed")
        extent = "meshroute::console::local_command_max_bytes + 1"
        self.assertIn("typedef char Mr0cUsbLine[%s];" % extent, header)
        self.assertIn("static void mr0c_serial_tail(const char (&line)[%s], size_t pos) {" % extent, header)

    def test_control_b_c1_pointer_parameter_refuses_every_row_longer_than_seven(self):
        run = self.runs["B-C1"]
        self.assertEqual(run["code"], 0, run["stderr"])                  # behavioural: builds, runs, VALID ledger
        rows = list(rows_of(self.t, run["ledger"]).values())
        refused = [r.id for r in rows if TOO_LONG in r.ser[0]]
        self.assertEqual(refused, [r.id for r in rows if r.length > 7])
        self.assertEqual(len(refused), 164)
        for cid in ("L003", "L201", "L202"):                               # acl list, 1,023 B, 12345678
            with self.subTest(record=cid), self.assertRaises(AssertionError):
                self.check_records("full_headless", run["ledger"], only={cid})
        self.assertEqual(self.compare_with_final(run)[0], 1)

    def compare_with_final(self, run):
        control = run["out"] / "control.txt"
        control.write_text(run["ledger"], encoding="ascii")
        return run_main(self.t, "--compare", self.runs["full_headless-1"]["out"] / "emitted.txt", control)

    def test_control_b_c2_live_fixture_removed_differs_on_exactly_regen_and_whoami(self):
        run = self.runs["B-C2"]
        self.assertEqual(run["code"], 0, run["stderr"])
        code, out, err = self.compare_with_final(run)
        self.assertEqual(code, 1, out + err)
        ids = sorted({m.group(1) for m in re.finditer(r"^DIFF (L\d+) ", out, re.M)})
        commands = dict(self.t.expected_matrix("full_headless"))
        self.assertEqual(sorted(commands[i].decode() for i in ids), ["regen", "whoami"], out)

    def test_control_b_c3_three_macro_profile_line_is_refused(self):
        run = self.runs["B-C3"]
        self.assertEqual((run["code"], run["ledger"], run["child_rc"]), (2, "", 0), run["stderr"])
        self.assertRegex(run["stderr"], r"INVALID LEDGER — the fresh ledger, line 3: compiled macros .* are not "
                                        r"profile 'full_headless'")

    def test_control_b_c4_value_changes_compare_1_and_row_changes_are_refused(self):
        final = (self.runs["full_headless-1"]["out"] / "emitted.txt").read_text(encoding="ascii")
        lines = final[:-1].split("\n")
        at = {ln.split()[1]: i for i, ln in enumerate(lines) if ln.startswith("LINE ")}
        def changed(i, old, new):
            out = list(lines)
            out[i] = edit(out[i], old, new)
            return "\n".join(out) + "\n"
        ser133 = lines[at["L133"] + 1]
        usb = re.search(r"usb=\[(.*)\] ble=\[\]", ser133).group(1)
        cases = {
            "one output byte": (changed(at["L003"] + 1, "count=0", "count=1"), "L003 SER"),
            "one sink field moved": (changed(at["L133"] + 1, "usb=[%s] ble=[]" % usb, "usb=[] ble=[%s]" % usb),
                                     "L133 SER"),
            "one fingerprint field": (changed(at["L166"] + 2, "kh=b12d4983", "kh=b12d4984"), "L166 BLE"),
        }
        tmp = self.tmp / "b-c4"
        tmp.mkdir(exist_ok=True)
        (tmp / "final.txt").write_text(final, encoding="ascii")
        for why, (ledger, named) in cases.items():
            with self.subTest(why=why):
                (tmp / "x.txt").write_text(ledger, encoding="ascii")
                code, out, err = run_main(self.t, "--compare", tmp / "final.txt", tmp / "x.txt")
                self.assertEqual(code, 1, out + err)
                self.assertIn("COMPARE: 1 record(s) differ: %s" % named, out)
        row = lines[at["L050"]:at["L050"] + 3]
        for why, ledger, says in (
                ("one row removed", "\n".join(lines[:at["L050"]] + lines[at["L050"] + 3:]) + "\n",
                 "declares lines=202 but carries 201 row(s)"),
                ("one row duplicated", "\n".join(lines[:at["L050"]] + row + lines[at["L050"]:]) + "\n",
                 "declares lines=202 but carries 203 row(s)")):
            with self.subTest(why=why):
                (tmp / "x.txt").write_text(ledger, encoding="ascii")
                code, out, err = run_main(self.t, "--compare", tmp / "final.txt", tmp / "x.txt")
                self.assertEqual(code, 2, out + err)
                self.assertIn(says, err)

    def test_control_b_c5_unrepresentable_row_is_the_drivers_own_refusal(self):
        run = self.runs["B-C5"]
        self.assertIsNotNone(run["built"], "B-C5 must build — a compiler failure is not its outcome")
        self.assertEqual((run["code"], run["ledger"], run["child_rc"]), (2, "", 4), run["stderr"])
        self.assertIn("the transcript driver ended with exit 4 — no ledger", run["stderr"])
        self.assertIn("MR0C-REFUSED row L203 len=1024 cannot be represented in the USB intake array (1024 bytes, "
                      "1023 admitted)", run["stderr"])

    def test_a_malformed_fresh_profile_line_is_a_named_refusal_of_the_run_path(self):
        run = self.runs["TQ-1"]                                  # QA TQ-1 (B491), through `run_profile`
        self.assertEqual((run["code"], run["ledger"], run["child_rc"]), (2, "", 0), run["stderr"])
        self.assertIn("INVALID LEDGER — the fresh ledger, line 3: the MR0C-PROFILE line carries a field that is not "
                      "`name=value`: 'malformed'", run["stderr"])

    def test_control_b_c6_device_ble_include_removed_is_a_build_refusal(self):
        run = self.runs["B-C6"]
        self.assertEqual((run["code"], run["ledger"], run["built"], run["child_rc"]), (2, "", None, None),
                         run["stderr"])
        self.assertIn("TRANSCRIPT REFUSED: the inbox runner's builder refused (exit 2", run["stderr"])
        self.assertIn("'mrble' has not been declared", run["stderr"])


if __name__ == "__main__":
    unittest.main()
