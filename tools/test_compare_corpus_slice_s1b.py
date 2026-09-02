#!/usr/bin/env python3
# MeshRoute — tools/test_compare_corpus_slice_s1b.py
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""Control suite for `tools/compare_corpus_slice_s1b.py`.

WHY IT EXISTS. The comparator is the ONLY thing standing between "S1b moved two corpus rows for three reviewed
reasons" and "S1b moved two corpus rows". A green comparator run is evidence only if the comparator can go RED,
and only if each of its eight checks can go red *for its own reason* — a suite that proves "some check fires"
would let seven of them rot.

⛔ EVERY EXPECTATION HERE IS DERIVED, never hand-pinned: the fixtures are built from named constants read out
   of the comparator itself (`APPENDED`, `DOMAINS`, `read_ttl_ms`), so a schema change breaks the test at the
   place the schema changed rather than silently making it vacuous.

★ THE VACUITY ARM (`test_zz_blinding_the_checks_breaks_this_suite`) is the standing requirement this repo puts
  on every instrument: blind the comparator's own checks and this suite must COLLAPSE. Without it a suite can
  pass because nothing it asserts is load-bearing.

RUN
    python3 tools/test_compare_corpus_slice_s1b.py            # directly
    python3 -m unittest discover -s tools -p "test_*.py"      # the standing discovery
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import compare_corpus_slice_s1b as C  # noqa: E402


TTL = C.read_ttl_ms()


def line(node, t, ev, data):
    return json.dumps({"type": "script_emit", "node": node, "time_ms": t, "emit_type": ev, "data": data},
                      separators=(",", ":"), sort_keys=True)


def noise(node, t, k=0):
    return line(node, t, "rt_update", {"dest": k, "hops": 1})


class Fixture:
    """A minimal BEFORE/AFTER stream pair carrying all three permitted transformations, plus ordinary traffic.

    The shape mirrors what the real corpus produced: a reservation, its activation at the same instant, a
    reverse ACK that translates it, then a second flight whose reservation drives a prune scan that expires the
    first — the expiry line landing IMMEDIATELY BEFORE the reservation that drove it.
    """

    MH, CTRM, LAYER, T1, T2 = 0xABCDEF01, 1, 0, 30, 31

    def __init__(self):
        self.before = [
            noise(1, 10),
            line(2, 100, C.RESERVED_EVENT, {"mobile_hash": self.MH, "ctr_m": self.CTRM,
                                            "target": self.T1, "layer": self.LAYER}),
            line(2, 100, C.PUT_EVENT, {"mobile_hash": self.MH, "ctr_h": 7, "ctr_m": self.CTRM,
                                       "peer": self.T1, "layer": self.LAYER}),
            noise(1, 120),
            line(2, 100 + TTL, C.RESERVED_EVENT, {"mobile_hash": self.MH, "ctr_m": self.CTRM,
                                                  "target": self.T2, "layer": self.LAYER}),
            line(2, 100 + TTL, C.PUT_EVENT, {"mobile_hash": self.MH, "ctr_h": 8, "ctr_m": self.CTRM,
                                             "peer": self.T2, "layer": self.LAYER}),
            line(2, 200 + TTL, C.REVACK_EVENT, {"local": 254, "ctr": self.CTRM}),
            noise(1, 999),
        ]
        self.after = [
            noise(1, 10),
            line(2, 100, C.RESERVED_EVENT, {"mobile_hash": self.MH, "ctr_m": self.CTRM,
                                            "target": self.T1, "layer": self.LAYER, "target_kind": 0}),
            line(2, 100, C.PUT_EVENT, {"mobile_hash": self.MH, "ctr_h": 7, "ctr_m": self.CTRM,
                                       "peer": self.T1, "layer": self.LAYER}),
            noise(1, 120),
            # ★ the expiry precedes the reservation whose scan produced it
            line(2, 100 + TTL, C.EXPIRED_EVENT, {"mobile_hash": self.MH, "ctr_m": self.CTRM, "ctr_h": 7,
                                                 "target": self.T1, "layer": self.LAYER, "custody_state": 2}),
            line(2, 100 + TTL, C.RESERVED_EVENT, {"mobile_hash": self.MH, "ctr_m": self.CTRM,
                                                  "target": self.T2, "layer": self.LAYER, "target_kind": 0}),
            line(2, 100 + TTL, C.PUT_EVENT, {"mobile_hash": self.MH, "ctr_h": 8, "ctr_m": self.CTRM,
                                             "peer": self.T2, "layer": self.LAYER}),
            line(2, 200 + TTL, C.REVACK_EVENT, {"local": 254, "ctr": self.CTRM,
                                                "mobile_hash": self.MH, "ctr_h": 8}),
            noise(1, 999),
        ]


def run(before, after, b_md5="aaaaaaaa", a_md5="bbbbbbbb"):
    """The comparator's full verdict over one stream pair -> (findings, messages)."""
    out: list = []
    bad, _, _, _, _ = C.compare_streams("fx", list(before), list(after), b_md5, a_md5, TTL, out)
    bad += C.check_lifecycle("fx", C.read_after_events(list(after)), TTL, out)
    if C.ledger(list(before)) != C.ledger(list(after)):
        out.append("  C8  FAIL fx: the traffic ledger moved")
        bad += 1
    return bad, out


class TestPermittedShapes(unittest.TestCase):
    def test_the_undoctored_pair_is_green(self):
        f = Fixture()
        bad, out = run(f.before, f.after)
        self.assertEqual(bad, 0, "\n".join(out))

    def test_every_appended_key_is_derived_from_the_comparator_not_typed_here(self):
        # If the reviewed schema ever changes, this fixture must change with it — so read it, don't retype it.
        self.assertEqual(C.APPENDED[C.RESERVED_EVENT], frozenset({"target_kind"}))
        self.assertEqual(C.APPENDED[C.REVACK_EVENT], frozenset({"mobile_hash", "ctr_h"}))
        f = Fixture()
        for ev, keys in C.APPENDED.items():
            b = [ln for ln in f.before if f'"emit_type":"{ev}"' in ln]
            a = [ln for ln in f.after if f'"emit_type":"{ev}"' in ln]
            self.assertTrue(b and a, ev)
            bd = json.loads(b[0])["data"]
            ad = json.loads(a[0])["data"]
            self.assertEqual(set(ad) - set(bd), set(keys))

    def test_ttl_is_read_from_the_header_and_is_the_named_e2e_deadline(self):
        text = (C.ROOT / "lib" / "core" / "protocol_constants.h").read_text(encoding="utf-8")
        self.assertIn("delegated_custody_ttl_ms", text)
        self.assertGreater(TTL, 0)
        self.assertEqual(TTL, 300_000)   # DERIVED above; asserted here so a silent retune is visible

    def test_a_refusal_is_raised_when_the_constant_is_gone(self):
        with tempfile.TemporaryDirectory() as d:
            fake = Path(d)
            (fake / "lib" / "core").mkdir(parents=True)
            (fake / "lib" / "core" / "protocol_constants.h").write_text("// nothing here\n", encoding="utf-8")
            with self.assertRaises(C.Refusal):
                C.read_ttl_ms(fake)


class TestResidueControls(unittest.TestCase):
    """C1 — the ordered residue. Each control doctors ONE thing about an ORDINARY line."""

    def _red(self, mutate_after=None, mutate_before=None):
        f = Fixture()
        b, a = list(f.before), list(f.after)
        if mutate_before:
            mutate_before(b)
        if mutate_after:
            mutate_after(a)
        bad, out = run(b, a)
        self.assertGreater(bad, 0, "control did not fire:\n" + "\n".join(out))
        return out

    def test_a_field_of_an_ordinary_event_changes(self):
        self._red(lambda a: a.__setitem__(3, noise(1, 121)))

    def test_two_ordinary_events_are_reordered(self):
        def swap(a):
            a[0], a[3] = a[3], a[0]
        self._red(swap)

    def test_an_ordinary_event_disappears(self):
        self._red(lambda a: a.pop(3))

    def test_an_ordinary_event_appears(self):
        self._red(lambda a: a.insert(3, noise(1, 121)))

    def test_a_delivered_event_appears_and_the_ledger_moves(self):
        out = self._red(lambda a: a.insert(3, line(1, 121, "delivered", {"dst": 2})))
        self.assertTrue(any("C8" in m or "C1" in m for m in out), out)


class TestPairedAppendControls(unittest.TestCase):
    """C2 — the paired lines may GROW and may do nothing else."""

    def _doctor_paired(self, ev, fn):
        f = Fixture()
        a = list(f.after)
        for i, ln in enumerate(a):
            if f'"emit_type":"{ev}"' in ln:
                o = json.loads(ln)
                fn(o)
                a[i] = json.dumps(o, separators=(",", ":"), sort_keys=True)
                break
        bad, out = run(f.before, a)
        self.assertGreater(bad, 0, "control did not fire:\n" + "\n".join(out))
        return out

    def test_the_appended_field_is_missing(self):
        for ev, keys in C.APPENDED.items():
            with self.subTest(ev=ev):
                self._doctor_paired(ev, lambda o, k=sorted(keys)[0]: o["data"].pop(k, None))

    def test_an_existing_field_changed_value(self):
        self._doctor_paired(C.RESERVED_EVENT, lambda o: o["data"].__setitem__("ctr_m", 99))

    def test_an_unreviewed_extra_field_rides_along(self):
        self._doctor_paired(C.RESERVED_EVENT, lambda o: o["data"].__setitem__("outward_type", 0))

    def test_the_envelope_changed(self):
        self._doctor_paired(C.RESERVED_EVENT, lambda o: o.__setitem__("node", 77))

    def test_a_paired_line_moved(self):
        f = Fixture()
        a = list(f.after)
        i = next(k for k, ln in enumerate(a) if f'"emit_type":"{C.RESERVED_EVENT}"' in ln)
        a[i], a[0] = a[0], a[i]
        bad, out = run(f.before, a)
        self.assertGreater(bad, 0, "\n".join(out))

    def test_an_appended_field_that_is_not_an_integer(self):
        for bad_value in (False, 0.0, "0"):
            with self.subTest(v=bad_value):
                self._doctor_paired(C.RESERVED_EVENT,
                                    lambda o, v=bad_value: o["data"].__setitem__("target_kind", v))


class TestLifecycleControls(unittest.TestCase):
    """C4/C5/C6/C7 — the new events must name a real row, at the real edge, before the clear."""

    def _red(self, fn):
        f = Fixture()
        a = list(f.after)
        i = next(k for k, ln in enumerate(a) if f'"emit_type":"{C.EXPIRED_EVENT}"' in ln)
        fn(a, i)
        bad, out = run(f.before, a)
        self.assertGreater(bad, 0, "control did not fire:\n" + "\n".join(out))
        return out

    def test_the_expiry_is_omitted(self):
        # A row that never closes is not a residue error — it is a MISSING lifecycle event, and the model must
        # notice it downstream (the reverse ACK can then match two rows).
        self._red(lambda a, i: a.pop(i))

    def test_the_expiry_names_no_live_row(self):
        out = self._red(lambda a, i: a.insert(i, line(9, 5, C.EXPIRED_EVENT,
                                                      {"mobile_hash": 1, "ctr_m": 1, "ctr_h": 1,
                                                       "target": 1, "layer": 0, "custody_state": 2})))
        self.assertTrue(any("C4" in m for m in out), out)

    def test_the_expiry_names_the_wrong_identity(self):
        def doctor(a, i):
            o = json.loads(a[i]); o["data"]["mobile_hash"] = 0xDEAD
            a[i] = json.dumps(o, separators=(",", ":"), sort_keys=True)
        self.assertTrue(any("C4" in m for m in self._red(doctor)))

    def test_the_expiry_names_a_target_the_row_never_retained(self):
        def doctor(a, i):
            o = json.loads(a[i]); o["data"]["target"] = 251
            a[i] = json.dumps(o, separators=(",", ":"), sort_keys=True)
        self.assertTrue(any("C4" in m for m in self._red(doctor)))

    def test_the_expiry_reports_a_ctr_h_the_row_never_activated_on(self):
        def doctor(a, i):
            o = json.loads(a[i]); o["data"]["ctr_h"] = 4242
            a[i] = json.dumps(o, separators=(",", ":"), sort_keys=True)
        self.assertTrue(any("C4" in m for m in self._red(doctor)))

    def test_the_expiry_trails_its_scan_driver(self):
        # ⓘ TWO CHECKS OWN THIS DEFECT AND EITHER IS A CORRECT VERDICT: C5 says the expiry no longer precedes
        #   the operation at its instant, C6-completeness says the driver scanned while a past-edge row was
        #   still live. The control asserts the DEFECT is caught, not which sentence catches it — pinning one
        #   would make the test brittle about wording rather than about behaviour.
        def doctor(a, i):
            ln = a.pop(i)
            a.insert(i + 1, ln)
        out = self._red(doctor)
        self.assertTrue(any("C5" in m or "C6" in m for m in out), out)

    def test_the_expiry_fires_before_the_named_edge(self):
        def doctor(a, i):
            o = json.loads(a[i]); o["time_ms"] = int(o["time_ms"]) - 1
            a[i] = json.dumps(o, separators=(",", ":"), sort_keys=True)
            # the driver moves with it, or C5 would fire instead of C6
            o2 = json.loads(a[i + 1]); o2["time_ms"] = int(o2["time_ms"]) - 1
            a[i + 1] = json.dumps(o2, separators=(",", ":"), sort_keys=True)
        self.assertTrue(any("C6" in m for m in self._red(doctor)))

    # ⚠ CORRECTED IN PLACE 2026-09-02 BY §B278 S3. THIS TEST WAS `test_forwarded_is_unreachable` and asserted
    #   that a `custody_state` of 3 goes RED. That was TRUE THROUGH S1b/S2 — nothing wrote the state — and is
    #   now FALSE: S3's `deleg_custody_mark_forwarded` writes it, so the comparator must ACCEPT it. ⛔ The
    #   coverage did not go with it, it SPLIT: the reachable state is a POSITIVE control here, and the closed
    #   domain above it keeps its own RED control. Renaming rather than deleting keeps the history readable.
    def test_forwarded_is_now_a_reachable_state_and_the_domain_still_closes(self):
        def doctor_ok(a, i):
            o = json.loads(a[i]); o["data"]["custody_state"] = C.CUSTODY_STATE_FORWARDED
            a[i] = json.dumps(o, separators=(",", ":"), sort_keys=True)
        f = Fixture()
        a = list(f.after)
        i = next(k for k, ln in enumerate(a) if f'"emit_type":"{C.EXPIRED_EVENT}"' in ln)
        doctor_ok(a, i)
        bad, out = run(f.before, a)
        self.assertEqual(bad, 0, "a `forwarded` row must be ACCEPTED since S3:\n" + "\n".join(out))
        # …and the domain is still CLOSED one above it.
        def doctor_bad(a2, i2):
            o = json.loads(a2[i2]); o["data"]["custody_state"] = C.CUSTODY_STATE_FORWARDED + 1
            a2[i2] = json.dumps(o, separators=(",", ":"), sort_keys=True)
        out = self._red(doctor_bad)
        self.assertTrue(any("C7" in m for m in out), out)

    def test_the_custody_state_domain_is_exactly_the_four_lifecycle_values(self):
        # DERIVED from the comparator's own table, so a future widening breaks here rather than silently.
        self.assertEqual(C.DOMAINS["custody_state"], (0, C.CUSTODY_STATE_FORWARDED))
        self.assertEqual(C.CUSTODY_STATE_FORWARDED, 3)

    def test_every_domain_rejects_out_of_range_and_non_integer(self):
        for key, (lo, hi) in C.DOMAINS.items():
            for value in (hi + 1, lo - 1, float(lo), False, "0"):
                with self.subTest(key=key, value=value):
                    def doctor(a, i, k=key, v=value):
                        o = json.loads(a[i]); o["data"][k] = v
                        a[i] = json.dumps(o, separators=(",", ":"), sort_keys=True)
                    self._red(doctor)

    def test_a_release_with_a_cause_outside_the_seven_value_authority(self):
        def doctor(a, i):
            o = json.loads(a[i])
            a.insert(i, line(o["node"], o["time_ms"], C.RELEASED_EVENT,
                             dict(o["data"], cause=99, target_kind=0)))
        self.assertTrue(any("C7" in m for m in self._red(doctor)))

    def test_a_well_formed_release_is_accepted(self):
        """The positive control for the release shape — without it the cause checks could pass vacuously."""
        f = Fixture()
        a = list(f.after)
        # a RESERVED row that is released instead of expiring: strike the expiry, add the release in its place
        i = next(k for k, ln in enumerate(a) if f'"emit_type":"{C.EXPIRED_EVENT}"' in ln)
        o = json.loads(a[i])
        a[i] = line(o["node"], o["time_ms"], C.RELEASED_EVENT,
                    {"mobile_hash": o["data"]["mobile_hash"], "ctr_m": o["data"]["ctr_m"],
                     "target": o["data"]["target"], "target_kind": 0, "layer": o["data"]["layer"],
                     "cause": C.DOMAINS["cause"][1]})
        bad, out = run(f.before, a)
        self.assertEqual(bad, 0, "\n".join(out))


class TestMovementLicence(unittest.TestCase):
    """C3 — a stream's bytes move IFF it carries a permitted transformation."""

    def test_an_unlicensed_mover_is_refused(self):
        f = Fixture()
        plain = [noise(1, 10), noise(1, 20)]
        bad, out = run(plain, plain, "aaaaaaaa", "cccccccc")
        self.assertGreater(bad, 0, "\n".join(out))
        self.assertTrue(any("C3" in m for m in out), out)
        del f

    def test_a_licensed_stream_that_did_not_move_is_refused(self):
        f = Fixture()
        bad, out = run(f.before, f.after, "aaaaaaaa", "aaaaaaaa")
        self.assertGreater(bad, 0, "\n".join(out))
        self.assertTrue(any("C3" in m for m in out), out)

    def test_an_untouched_stream_stays_green(self):
        plain = [noise(1, 10), noise(1, 20)]
        bad, out = run(plain, plain, "aaaaaaaa", "aaaaaaaa")
        self.assertEqual(bad, 0, "\n".join(out))


class TestVacuity(unittest.TestCase):
    """★ THE STANDING VACUITY ARM: blind the comparator's checks and this suite must COLLAPSE."""

    def test_zz_blinding_the_checks_breaks_this_suite(self):
        saved_compare, saved_life, saved_ledger = C.compare_streams, C.check_lifecycle, C.ledger
        try:
            C.compare_streams = lambda *a, **k: (0, {}, [], [], [])
            C.check_lifecycle = lambda *a, **k: 0
            C.ledger = lambda *a, **k: {}
            loader = unittest.TestLoader()
            suite = unittest.TestSuite([
                loader.loadTestsFromTestCase(TestResidueControls),
                loader.loadTestsFromTestCase(TestPairedAppendControls),
                loader.loadTestsFromTestCase(TestLifecycleControls),
                loader.loadTestsFromTestCase(TestMovementLicence),
            ])
            result = unittest.TextTestRunner(stream=open("/dev/null", "w"), verbosity=0).run(suite)
            broken = len(result.failures) + len(result.errors)
            self.assertGreater(broken, 10,
                               f"blinding the comparator broke only {broken} control(s) — the suite is "
                               f"largely vacuous")
        finally:
            C.compare_streams, C.check_lifecycle, C.ledger = saved_compare, saved_life, saved_ledger


if __name__ == "__main__":
    unittest.main(verbosity=2)
