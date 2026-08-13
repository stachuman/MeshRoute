#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""B188 SELF-TEST for the READER and the CAUSAL LEDGER underneath `verify.py`. ⛔ EXITS NONZERO ON FAILURE.

★★ WHY THIS FILE EXISTS, and it is the sharper half of the [[B188]] repair: `verify.py`'s 26 checks and all 26
of their mutations passed **over an ACAUSAL ledger** — a wrong model, validated consistently. Checks over the
fixture streams cannot catch that, because the model is what they are expressed in. So the model itself needs
controls, on streams whose right answer is known by construction.

THE ORDINAL RULE THE LEDGER NOW OBEYS:
  the ledger for an event at stream ordinal `K` contains ONLY `tx` records with ordinal **< K**.
  · a timestamp is NOT an ordering — many records share a millisecond — so the ORDINAL breaks the tie;
  · a frame transmitted after the event, or in the same millisecond but later in the stream, was NOT yet
    airtime the node had spent, and counting it is the acausality this file exists to forbid.

THE CONTROL GROUPS — SIX of them (each also proves the PRE-REPAIR behaviour would have FAILED it; a control the
old code would also have passed is not a control):
  1. **FUTURE TX** — a `tx` after the checked event must be excluded. ★ This is the control that would have
     caught the original defect.
  2. **SAME-TIMESTAMP ORDERING** — two records inside one millisecond must be consumed in ordinal order.
  3. **COMPACT vs NORMALLY-SPACED NDJSON** — the reader must not depend on whitespace or formatting.
     ★ The retired raw-substring length scan did exactly that, which is why it is gone (one reader, [[B162]]).
  4. **NODE BINDING** — a report must bind to a refusal on its OWN node, at a preceding ordinal, with the same
     SF, and unambiguously; and a MISSING node authority must refuse rather than default to a name.
  5. **RUN HORIZON** — `sim_end` decides what is inside the run, never the last transmitted frame.
  6. **PRICING** — shape-keyed `(len, SF, BW, CR)` observations, conflicts refused, formula cross-checked, and
     an unpriceable frame reported UNPRICED instead of substituted.

Usage: selftest.py [--mutate <check-id>]     `--mutate` inverts one expectation to prove this file can fail.
Exit:  0 = every control passed · 1 = at least one failed
"""
import json, os, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from verify import Checker, Stream, pair_refusal, classify_defer   # ⛔ the SAME reader/binder/classifier
                                                                  #    verify.py uses — never a second copy

BIG = 10 ** 9        # `before = BIG` reproduces the retired ACAUSAL reconstruction (the whole run's records)


def write_stream(records, compact=True, pad=False):
    """Serialize records as NDJSON. `compact` mirrors what `lus` emits; the spaced form is the whitespace
    variant control 3 requires."""
    fd, path = tempfile.mkstemp(suffix='.ndjson')
    with os.fdopen(fd, 'w') as f:
        for r in records:
            line = json.dumps(r, separators=(',', ':')) if compact else json.dumps(r)
            if pad:
                line = '  ' + line + '   '
            f.write(line + '\n')
    return path


def tx(t, node, air, hexs='6013ff40', label='BCN'):
    return {'type': 'tx', 'time_ms': t, 'node': node, 'pkt': 'x', 'hex': hexs,
            'airtime_ms': air, 'sf': 8, 'bw_hz': 62500, 'cr': 5, 'label': label}


def deferred(t, node, busy_until, reason='duty_cycle_exceeded', ln=4):
    return {'type': 'tx_deferred', 'time_ms': t, 'node': node, 'len': ln, 'reason': reason,
            'sf': 8, 'label': 'BCN', 'tx_info': '', 'busy_until_ms': busy_until}


def started(name):
    return {'type': 'node_started', 'time_ms': 1000, 'node': name}


def sim_end(t):
    return {'type': 'sim_end', 'time_ms': t}


def refused_emit(t, node_idx, op, busy_until, reason='duty_cycle_exceeded', sf=8):
    return {'type': 'script_emit', 'time_ms': t, 'node': node_idx, 'emit_type': 'mobile_tx_refused',
            'data': {'op': op, 'reason': 3, 'reason_name': reason, 'sf': sf, 'busy_until_ms': busy_until}}


def blocked_rts(t, node_idx, wait):
    return {'type': 'script_emit', 'time_ms': t, 'node': node_idx, 'emit_type': 'duty_cycle_blocked',
            'data': {'label': 'RTS', 'source': 'lbt_complete', 'wait_ms': wait}}


# ---------------------------------------------------------------- control 1
def control_future_tx(C):
    print('1 FUTURE TX — a `tx` AFTER the checked event must be excluded from its ledger')
    # ord0 TX 100 ms (already spent) · ord1 the refusal · ord2 TX 200 ms (the FUTURE frame)
    recs = [started('N'), tx(1000, 'N', 100), deferred(1200, 'N', 31100), tx(1300, 'N', 200)]
    p = write_stream(recs)
    S = Stream(p); os.unlink(p)
    k_ref = S.duty_refusals()[0][0]
    C.eq('S1a', 'causal used at the refusal counts ONLY the earlier frame', S.used('N', 1200, k_ref), 100)
    C.eq('S1b', 'the retired acausal reconstruction counted the future frame too',
         S.used('N', 1200, BIG), 300)
    # ★ AND THE SAME DEFECT ON `oldest`, which is what `busy_until` is computed from. Window 500 ms: the only
    #   causally-live entry has expired, so the honest answer is "no ledger entry"; the acausal read invents a
    #   front from a frame that had not been transmitted yet.
    recs2 = [started('N'), tx(100, 'N', 50), deferred(1000, 'N', 0), tx(900, 'N', 50)]
    p2 = write_stream(recs2)
    S2 = Stream(p2); os.unlink(p2)
    k2 = S2.duty_refusals()[0][0]
    C.eq('S1c', 'causal oldest at the refusal (window 500, everything expired)',
         S2.oldest('N', 1000, k2, 500), 0)
    C.eq('S1d', 'the retired acausal reconstruction returned a FUTURE frame as the window front',
         S2.oldest('N', 1000, BIG, 500), 950)


# ---------------------------------------------------------------- control 2
def control_same_timestamp(C):
    print('2 SAME-TIMESTAMP ORDERING — a millisecond tie is broken by the stream ordinal, not by the clock')
    # All three records share t=1000: TX(ord1) · refusal(ord2) · TX(ord3). Only the FIRST is already spent.
    recs = [started('N'), tx(1000, 'N', 100), deferred(1000, 'N', 31100), tx(1000, 'N', 200)]
    p = write_stream(recs)
    S = Stream(p); os.unlink(p)
    k_ref = S.duty_refusals()[0][0]
    C.eq('S2a', 'the reader assigns STRICTLY INCREASING ordinals in file order',
         [x[0] for x in S.tx] == sorted(x[0] for x in S.tx) and S.tx[0][0] < k_ref < S.tx[1][0], True)
    C.eq('S2b', 'causal used inside one millisecond counts the EARLIER record only',
         S.used('N', 1000, k_ref), 100)
    C.eq('S2c', 'a timestamp filter alone cannot separate them (the retired behaviour)',
         S.used('N', 1000, BIG), 300)
    C.eq('S2d', 'the later same-ms frame IS visible to an ordinal-ordered resumption check',
         len(S.tx_after_ordinal('N', k_ref)), 1)


# ---------------------------------------------------------------- control 3
def control_whitespace(C):
    print('3 COMPACT vs NORMALLY-SPACED NDJSON — the reader must not depend on formatting')
    recs = [started('N'), tx(1000, 'N', 100, hexs='001301013333333300000000ef36'), deferred(1200, 'N', 31100)]
    p_compact = write_stream(recs, compact=True)
    p_spaced = write_stream(recs, compact=False, pad=True)
    Sc = Stream(p_compact)
    Ss = Stream(p_spaced)
    C.eq('S3a', 'both encodings yield the SAME ledger', Sc.ledger['N'], Ss.ledger['N'])
    C.eq('S3b', 'both encodings yield the SAME shape-keyed observations (collected in the ONE pass)',
         dict(Sc.obs), dict(Ss.obs))
    C.eq('S3c', 'both encodings yield the SAME causal used', Sc.used('N', 1200, BIG), Ss.used('N', 1200, BIG))
    # ★ THE CONTROL'S OWN CONTROL: the retired raw-substring scan is whitespace-dependent, so it MISSES the
    #   spaced encoding entirely. Reproduced here (not imported — it no longer exists) to prove S3a-c
    #   discriminate rather than merely agreeing with themselves.
    def retired_substring_scan(path):
        n = 0
        with open(path) as f:
            for line in f:
                if '"type":"tx"' in line:
                    n += 1
        return n
    C.eq('S3d', 'retired substring scan on the COMPACT encoding', retired_substring_scan(p_compact), 1)
    C.eq('S3e', 'retired substring scan on the SPACED encoding MISSES it (why one JSON reader is mandatory)',
         retired_substring_scan(p_spaced), 0)
    os.unlink(p_compact)
    os.unlink(p_spaced)


# ---------------------------------------------------------------- control 4
def control_wrong_node(C):
    print('4 NODE BINDING — a report attributed to the WRONG node must NOT bind (the by-hand QG mutation)')
    # Two nodes: index 0 = MobileM, index 1 = Peer. The refusal really happened at MobileM.
    base = [started('MobileM'), started('Peer'), tx(1000, 'MobileM', 100),
            deferred(1200, 'MobileM', 31100)]
    # (a) the RIGHT node binds.
    p = write_stream(base + [refused_emit(1200, 0, 'discover', 31100)])
    S = Stream(p); os.unlink(p)
    C.eq('S4a', 'the correctly-attributed report BINDS to its tx_deferred',
         pair_refusal(S, S.emits_of('mobile_tx_refused')[0]) is not None, True)
    # (b) ★★ THE WRONG NODE MUST NOT BIND. This is precisely the mutation QG performed by hand and the
    #     pre-repair A5c reported 26/26 PASS on, because it never compared the node.
    p = write_stream(base + [refused_emit(1200, 1, 'discover', 31100)])
    S = Stream(p); os.unlink(p)
    C.eq('S4b', 'the same report attributed to Peer does NOT bind (wrong node)',
         pair_refusal(S, S.emits_of('mobile_tx_refused')[0]) is None, True)
    # (c) an AMBIGUOUS match is a REFUSAL, not a pick: two identical deferred records cannot be told apart.
    p = write_stream([started('MobileM'), tx(1000, 'MobileM', 100), deferred(1200, 'MobileM', 31100),
                      deferred(1200, 'MobileM', 31100), refused_emit(1200, 0, 'discover', 31100)])
    S = Stream(p); os.unlink(p)
    C.eq('S4c', 'two indistinguishable candidates ⇒ UNBOUND (refusal, never a pick)',
         pair_refusal(S, S.emits_of('mobile_tx_refused')[0]) is None, True)
    # (d) causality in the BINDING too: a refusal recorded AFTER its report cannot be its cause.
    p = write_stream([started('MobileM'), tx(1000, 'MobileM', 100),
                      refused_emit(1200, 0, 'discover', 31100), deferred(1200, 'MobileM', 31100)])
    S = Stream(p); os.unlink(p)
    C.eq('S4d', 'a tx_deferred at a LATER ordinal than the report does NOT bind',
         pair_refusal(S, S.emits_of('mobile_tx_refused')[0]) is None, True)
    # (e) ★★ WRONG SF MUST NOT BIND. QG changed one report's `sf` from 8 to 12 and the previous version still
    #     reported 31/31 PASS, because the candidate filter never compared SF — the same omission that let
    #     pricing ignore the shape.
    p = write_stream(base + [refused_emit(1200, 0, 'discover', 31100, sf=12)])
    S = Stream(p); os.unlink(p)
    C.eq('S4e', 'a report whose SF disagrees with the tx_deferred does NOT bind',
         pair_refusal(S, S.emits_of('mobile_tx_refused')[0]) is None, True)
    # (f) ⛔⛔ MISSING NODE AUTHORITY MUST REFUSE, NOT DEFAULT. Removing every `node_started` record used to
    #     leave `check_b`'s `sender` defaulting to the literal 'Home' and the whole run passing (C2).
    p = write_stream([tx(1000, 'MobileM', 100), deferred(1200, 'MobileM', 31100),
                      refused_emit(1200, 0, 'discover', 31100)])
    S = Stream(p); os.unlink(p)
    C.eq('S4f', 'with NO node_started records the index resolves to nothing', S.resolve_node(0), None)
    C.eq('S4g', '…and the report is therefore UNBOUND (never defaulted to a name)',
         pair_refusal(S, S.emits_of('mobile_tx_refused')[0]) is None, True)


# ---------------------------------------------------------------- control 5
def control_horizon(C):
    print('5 RUN HORIZON — `sim_end`, not the last transmitted frame (a missing TX must not move the yardstick)')
    # The last TX is at 100 000; the run ends at 600 000. A retry due at 200 000 with NOTHING after it is
    # ★ LOST — under the retired "last transmitted frame" horizon it would have been silently reclassified as
    #   outside the run, i.e. the missing transmission would have excused itself.
    recs = [started('Home'), tx(100000, 'Home', 100, label='RTS'),
            blocked_rts(170000, 0, 30000), sim_end(600000)]
    p = write_stream(recs)
    S = Stream(p); os.unlink(p)
    C.eq('S5a', 'the horizon is read from `sim_end`', S.sim_end_ms, 600000)
    C.eq('S5b', 'a retry due BEFORE sim_end with no later TX counts as LOST',
         classify_defer(S, 'Home', 170000, 30000, S.sim_end_ms, {170000}), 'lost')
    C.eq('S5c', '…and the retired last-TX horizon would have called the same case "past_end"',
         classify_defer(S, 'Home', 170000, 30000, max(t for (k, t, n, a, l, fl, sf, bw, cr) in S.tx),
                        {170000}), 'past_end')
    # …while a frame that really flies at the due instant is `flew`, and one due after the run is `past_end`.
    recs2 = [started('Home'), tx(100000, 'Home', 100, label='RTS'), blocked_rts(170000, 0, 30000),
             tx(200000, 'Home', 100, label='RTS'), blocked_rts(590000, 0, 30000), sim_end(600000)]
    p2 = write_stream(recs2)
    S2 = Stream(p2); os.unlink(p2)
    C.eq('S5d', 'a retry that flies at exactly t + wait_ms is `flew`',
         classify_defer(S2, 'Home', 170000, 30000, S2.sim_end_ms, {170000, 590000}), 'flew')
    C.eq('S5e', 'a retry due after sim_end is `past_end`',
         classify_defer(S2, 'Home', 590000, 30000, S2.sim_end_ms, {170000, 590000}), 'past_end')


# ---------------------------------------------------------------- control 6
def control_pricing(C):
    print('6 PRICING — the formula is cross-checked, and an unpriceable frame is UNPRICED, never substituted')
    # One aired 14-byte frame at SF8/BW62.5/CR5. The formula must reproduce it, and must then price the
    # 4-byte frame this stream never aired — at 156 ms, NOT at the 177 ms the retired "smallest observed"
    # substitution would have invented (which was LARGER than the truth, inverting its own safety claim).
    recs = [started('N'), tx(1000, 'N', 197, hexs='00' * 14), deferred(1200, 'N', 31197, ln=4)]
    p = write_stream(recs)
    S = Stream(p); os.unlink(p)
    ok, bad = S.pricing_crosscheck()
    C.eq('S6a', 'the transcribed getEstAirtimeFor reproduces the observation', (ok, bad), (1, 0))
    C.eq('S6b', 'the never-aired 4-byte frame prices at 156 ms (formula), not 197 (smallest observed)',
         S.airtime_for(4, 8), 156)
    C.eq('S6c', 'an OBSERVED shape is still taken from the observation', S.airtime_for(14, 8), 197)
    # (★) ⛔⛔ A CONFLICTING DUPLICATE OBSERVATION MUST BE DETECTED, NOT OVERWRITTEN. QG supplied an INCORRECT
    #     14-byte observation followed by a CORRECT one; the pricing key was `length` alone and a bare
    #     `dict[k] = v`, so the second silently replaced the first and `pricing_crosscheck()` returned (1, 0) —
    #     the conflict vanished. Observations are now a SET per (len, SF, BW, CR) shape.
    conflict = [started('N'),
                {'type': 'tx', 'time_ms': 1000, 'node': 'N', 'pkt': 'a', 'hex': '00' * 14,
                 'airtime_ms': 999, 'sf': 8, 'bw_hz': 62500, 'cr': 5, 'label': 'BCN'},   # WRONG, first
                {'type': 'tx', 'time_ms': 2000, 'node': 'N', 'pkt': 'b', 'hex': '00' * 14,
                 'airtime_ms': 197, 'sf': 8, 'bw_hz': 62500, 'cr': 5, 'label': 'BCN'},   # correct, second
                deferred(2200, 'N', 31197, ln=4)]
    pc = write_stream(conflict)
    Sc = Stream(pc); os.unlink(pc)
    C.eq('S6f', 'two different airtimes for ONE shape are RETAINED as a conflict, not overwritten',
         Sc.conflicting_observations(), {(14, 8, 62500, 5): [197, 999]})
    C.eq('S6g', 'a conflict invalidates the cross-check outright', Sc.pricing_crosscheck(), (0, 1))
    C.eq('S6h', '…and nothing is priced from a conflicted stream (UNPRICED)', Sc.airtime_for(4, 8), None)
    # (★) ★★ THE ORDERING CONTROL. A conflict on ONE shape invalidates the stream's pricing, so a DIFFERENT,
    #     perfectly clean shape must ALSO come back UNPRICED. Before the reorder, `airtime_for` served an
    #     observed value before asking about conflicts, so this call returned 218 — an exact price from a
    #     stream whose pricing was already void. General form: validate the input BEFORE serving it.
    ordering = conflict + [{'type': 'tx', 'time_ms': 3000, 'node': 'N', 'pkt': 'c', 'hex': '00' * 18,
                            'airtime_ms': 218, 'sf': 8, 'bw_hz': 62500, 'cr': 5, 'label': 'BCN'}]
    po = write_stream(ordering)
    So = Stream(po); os.unlink(po)
    C.eq('S6m', 'the OTHER shape really is clean and observed (the premise)',
         sorted(So.obs[(18, 8, 62500, 5)]), [218])
    C.eq('S6n', '…yet it is UNPRICED, because a conflict ANYWHERE voids the stream\'s pricing',
         So.airtime_for(18, 8), None)
    # (★) THE SHAPE IS PART OF THE KEY: the same length at a different SF is a DIFFERENT observation, and
    #     pricing a refusal uses the refusal's OWN SF.
    two_sf = [started('N'),
              {'type': 'tx', 'time_ms': 1000, 'node': 'N', 'pkt': 'a', 'hex': '00' * 14,
               'airtime_ms': 197, 'sf': 8, 'bw_hz': 62500, 'cr': 5, 'label': 'BCN'},
              # ⓘ 790 is `est_airtime(14, SF10, 62.5 kHz, CR4/5)`, NOT a number chosen by hand. ★ THE ORDERING
              #   FIX CAUGHT THIS: an earlier draft of this control used an invented 730, and once the
              #   stream-wide cross-check ran BEFORE serving, the whole stream was correctly declared
              #   UNPRICED — the instrument refusing its own test's invented value.
              {'type': 'tx', 'time_ms': 2000, 'node': 'N', 'pkt': 'b', 'hex': '00' * 14,
               'airtime_ms': 790, 'sf': 10, 'bw_hz': 62500, 'cr': 5, 'label': 'BCN'},
              deferred(2200, 'N', 31197, ln=14)]
    p2sf = write_stream(two_sf)
    S2sf = Stream(p2sf); os.unlink(p2sf)
    C.eq('S6i', 'one length at two SFs is two observations, not a collision',
         len([k for k in S2sf.obs if k[0] == 14]), 2)
    C.eq('S6j', 'a refusal at SF8 prices at the SF8 observation', S2sf.airtime_for(14, 8), 197)
    C.eq('S6k', 'the same refusal length at SF10 prices at the SF10 observation', S2sf.airtime_for(14, 10), 790)
    # ⛔ AND WHEN IT CANNOT PRICE, IT SAYS SO: a stream with two (bw, cr) pairs AT ONE SF cannot establish the
    #    refused frame's shape, so `airtime_for` returns None and the caller must count it as UNPRICED.
    mixed = [started('N'), tx(1000, 'N', 197, hexs='00' * 14),
             {'type': 'tx', 'time_ms': 2000, 'node': 'N', 'pkt': 'y', 'hex': '00' * 14,
              'airtime_ms': 400, 'sf': 8, 'bw_hz': 125000, 'cr': 5, 'label': 'BCN'},
             deferred(2200, 'N', 31197, ln=4)]
    p2 = write_stream(mixed)
    S2 = Stream(p2); os.unlink(p2)
    C.eq('S6d', 'two bandwidths at ONE SF ⇒ the shape is UNESTABLISHED ⇒ UNPRICED (None)',
         S2.airtime_for(4, 8), None)
    # …and a formula that disagrees with an observation invalidates pricing outright.
    bad_obs = [started('N'),
               {'type': 'tx', 'time_ms': 1000, 'node': 'N', 'pkt': 'z', 'hex': '00' * 14,
                'airtime_ms': 999, 'sf': 8, 'bw_hz': 62500, 'cr': 5, 'label': 'BCN'},
               deferred(1200, 'N', 31197, ln=4)]
    p3 = write_stream(bad_obs)
    S3 = Stream(p3); os.unlink(p3)
    C.eq('S6e', 'a formula/observation disagreement invalidates pricing (UNPRICED, not "close enough")',
         S3.airtime_for(4, 8), None)
    # (★) MALFORMED INPUT IS REFUSED, NOT ABSORBED: a `tx` without `hex`/`sf` would otherwise become a
    #     length-0 or SF-0 observation and pollute the table.
    p4 = write_stream([started('N'), {'type': 'tx', 'time_ms': 1, 'node': 'N', 'airtime_ms': 100,
                                      'sf': 8, 'bw_hz': 62500, 'cr': 5, 'label': 'BCN'}])
    S4 = Stream(p4); os.unlink(p4)
    C.eq('S6l', 'a `tx` record missing `hex` is REFUSED into `malformed`, not read as length 0',
         (len(S4.tx), len(S4.malformed)), (0, 1))


def main():
    mutate = None
    if '--mutate' in sys.argv:
        mutate = sys.argv[sys.argv.index('--mutate') + 1]
    C = Checker(mutate)
    control_future_tx(C)
    control_same_timestamp(C)
    control_whitespace(C)
    control_wrong_node(C)
    control_horizon(C)
    control_pricing(C)
    print(f'\n{C.n} controls, {len(C.fails)} failed')
    for f in C.fails:
        print('  FAILED ' + f)
    return 1 if C.fails else 0


if __name__ == '__main__':
    sys.exit(main())
