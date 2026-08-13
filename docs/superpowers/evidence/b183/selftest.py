#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""B183 harness SELF-TEST — proves the instruments can FAIL. ⛔ EXITS NONZERO ON FAILURE.

Every case is driven off a hand-built synthetic stream so the expectation is exact and
independent of any simulator run. It pins the two guarantees the tools ADVERTISE, both of
which an earlier revision advertised and did not implement:

  1. `Stream.arrivals`  — a byte-identical `pkt` re-emitted inside the arrival window must
     be REFUSED, and the receives must NOT be consumed (it used to warn and consume).
  2. `Stream.refusal_for` — a lost J frame is bound ONLY by an exact binding; several BCN
     refusals in one millisecond, or several concurrent LBT defers, must be REFUSED
     (it used to take "the next BCN refusal within 4000 ms", i.e. a guess).

Plus the negative half of each: the unambiguous shape must still be attributed, so the
refusal cannot be vacuously "always refuse".

Usage: selftest.py
Exit:  0 = every self-test passed · 1 = at least one failed
"""
import json, os, sys, tempfile, graph

FAILS = []


def check(label, got, want):
    ok = got == want
    print(f'   [{"PASS" if ok else "FAIL"}] {label}: got {got!r} want {want!r}')
    if not ok:
        FAILS.append(label)


def build(tmp, nodes, records):
    cfg = {'_name': 'selftest', 'simulation': {'duration_ms': 1000, 'step_ms': 1,
           'radio': {'sf': 8, 'bw': 62.5, 'cr': 5}}, 'commands': [], 'expect': [],
           'nodes': nodes, 'topology': {'links': []}}
    cp = os.path.join(tmp, 'cfg.json'); np_ = os.path.join(tmp, 's.ndjson')
    json.dump(cfg, open(cp, 'w'))
    with open(np_, 'w') as f:
        for r in records:
            f.write(json.dumps(r) + '\n')
    return graph.Stream(cp, np_)


def node(name, nid, key, mobile=False, host=True):
    c = {'routing_sf': 8}
    if mobile:
        c['is_mobile'] = True
    if not host:
        c['host_mobiles'] = False
    return {'name': name, 'lat': 47.6, 'lon': -122.3, 'node_id': nid,
            'key_hash32': key, 'config': c}


# A minimal 9-byte mobile DISCOVER: b0=0x90 (cmd J, leaf 0), b1=0x40 (is_mobile, opcode 0,
# wire_version 0), key_hash32 LE = 0xe27bc270, then last_home 0/0/0 (fresh).
DISC = '904070c27be2000000'
NODES = [node('HostX', 19, '0x33333333'), node('MobileM', 50, '0xe27bc270', mobile=True)]


def main():
    tmp = tempfile.mkdtemp(prefix='b183_selftest_')

    # ---- 1a  arrivals(): ambiguous reused pkt inside the window MUST be refused
    print('1a arrivals(): byte-identical pkt re-emitted inside the arrival window')
    S = build(tmp, NODES, [
        {'type': 'tx', 'time_ms': 100, 'node': 'MobileM', 'pkt': 'aaaa', 'hex': DISC,
         'airtime_ms': 177, 'sf': 8, 'bw_hz': 62500, 'cr': 5, 'label': 'BCN'},
        {'type': 'tx', 'time_ms': 300, 'node': 'MobileM', 'pkt': 'aaaa', 'hex': DISC,
         'airtime_ms': 177, 'sf': 8, 'bw_hz': 62500, 'cr': 5, 'label': 'BCN'},
        {'type': 'rx', 'time_ms': 277, 'from': 'MobileM', 'to': 'HostX', 'snr': 20.0,
         'rssi': -90.0, 'pkt': 'aaaa', 'airtime_ms': 177, 'sf': 8, 'bw_hz': 62500, 'cr': 5},
    ])
    r = S.arrivals(S.tx[0])
    check('1a refused', bool(r), False)
    check('1a receives NOT consumed (result is not a tuple)', isinstance(r, tuple), False)
    check('1a static_receivers also refuses', bool(S.static_receivers(S.tx[0])), False)

    # ---- 1b  arrivals(): the SAME shape spaced beyond the window must still be attributed
    print('1b arrivals(): same pkt re-emitted OUTSIDE the window -> must be attributed')
    S = build(tmp, NODES, [
        {'type': 'tx', 'time_ms': 100, 'node': 'MobileM', 'pkt': 'aaaa', 'hex': DISC,
         'airtime_ms': 177, 'sf': 8, 'bw_hz': 62500, 'cr': 5, 'label': 'BCN'},
        {'type': 'tx', 'time_ms': 5000, 'node': 'MobileM', 'pkt': 'aaaa', 'hex': DISC,
         'airtime_ms': 177, 'sf': 8, 'bw_hz': 62500, 'cr': 5, 'label': 'BCN'},
        {'type': 'rx', 'time_ms': 277, 'from': 'MobileM', 'to': 'HostX', 'snr': 20.0,
         'rssi': -90.0, 'pkt': 'aaaa', 'airtime_ms': 177, 'sf': 8, 'bw_hz': 62500, 'cr': 5},
    ])
    r = S.arrivals(S.tx[0])
    check('1b attributed', bool(r), True)
    check('1b exactly 1 receive, at HostX', [x['to'] for x in r[0]], ['HostX'])
    check('1b static_receivers names HostX as ELIGIBLE',
          {k: v[1] for k, v in S.static_receivers(S.tx[0]).items()}, {'HostX': True})

    # ---- 2a  refusal_for(): two BCN refusals in ONE millisecond MUST be refused
    print('2a refusal_for(): 2 BCN refusals in the same millisecond')
    S = build(tmp, NODES, [
        {'type': 'script_emit', 'node': 1, 'time_ms': 500, 'emit_type': 'mobile_discover_tx',
         'data': {'key': 0xe27bc270}},
        {'type': 'tx_deferred', 'time_ms': 500, 'node': 'MobileM', 'len': 9,
         'reason': 'duty_cycle_exceeded', 'sf': 8, 'label': 'BCN', 'busy_until_ms': 9},
        {'type': 'tx_deferred', 'time_ms': 500, 'node': 'MobileM', 'len': 9,
         'reason': 'duty_cycle_exceeded', 'sf': 8, 'label': 'BCN', 'busy_until_ms': 9},
    ])
    check('2a refused', bool(S.refusal_for('MobileM', 500)), False)

    # ---- 2b  refusal_for(): exactly ONE same-ms BCN refusal must be attributed
    print('2b refusal_for(): exactly 1 BCN refusal in the same millisecond')
    S = build(tmp, NODES, [
        {'type': 'tx_deferred', 'time_ms': 500, 'node': 'MobileM', 'len': 9,
         'reason': 'duty_cycle_exceeded', 'sf': 8, 'label': 'BCN', 'busy_until_ms': 9},
    ])
    r = S.refusal_for('MobileM', 500)
    check('2b attributed via same_ms', (bool(r), r and r['reason'], r and r['via']),
          (True, 'duty_cycle_exceeded', 'same_ms'))

    # ---- 2c  refusal_for(): TWO concurrent LBT defers must be refused (the s07 t=1375860 shape)
    print('2c refusal_for(): 2 concurrent tx_lbt_defer requests, refusal only at one fire time')
    S = build(tmp, NODES, [
        {'type': 'script_emit', 'node': 1, 'time_ms': 500, 'emit_type': 'tx_lbt_defer',
         'data': {'kind': 'initiating', 'defer_ms': 415, 'busy_until_ms': 900}},
        {'type': 'script_emit', 'node': 1, 'time_ms': 500, 'emit_type': 'tx_lbt_defer',
         'data': {'kind': 'initiating', 'defer_ms': 534, 'busy_until_ms': 900}},
        {'type': 'tx_deferred', 'time_ms': 1034, 'node': 'MobileM', 'len': 9,
         'reason': 'self_tx_in_flight', 'sf': 8, 'label': 'BCN', 'busy_until_ms': 1092},
    ])
    check('2c refused', bool(S.refusal_for('MobileM', 500)), False)

    # ---- 2d  refusal_for(): ONE LBT defer + ONE refusal at its fire time -> attributed
    print('2d refusal_for(): 1 tx_lbt_defer + 1 BCN refusal at exactly t+defer_ms')
    S = build(tmp, NODES, [
        {'type': 'script_emit', 'node': 1, 'time_ms': 500, 'emit_type': 'tx_lbt_defer',
         'data': {'kind': 'initiating', 'defer_ms': 534, 'busy_until_ms': 900}},
        {'type': 'tx_deferred', 'time_ms': 1034, 'node': 'MobileM', 'len': 9,
         'reason': 'self_tx_in_flight', 'sf': 8, 'label': 'BCN', 'busy_until_ms': 1092},
    ])
    r = S.refusal_for('MobileM', 500)
    check('2d attributed via lbt_defer', (bool(r), r and r['reason'], r and r['at_ms']),
          (True, 'self_tx_in_flight', 1034))

    # ---- 2e  refusal_for(): a LATE refusal with no defer to explain it MUST be refused
    #          (this is precisely the retired "next BCN refusal within 4000 ms" guess)
    print('2e refusal_for(): a BCN refusal 998 ms later with NO defer request -> must refuse')
    S = build(tmp, NODES, [
        {'type': 'tx_deferred', 'time_ms': 1498, 'node': 'MobileM', 'len': 9,
         'reason': 'channel_busy', 'sf': 8, 'label': 'BCN', 'busy_until_ms': 1600},
    ])
    check('2e refused (the retired 4000 ms horizon would have ATTRIBUTED this)',
          bool(S.refusal_for('MobileM', 500)), False)

    # ---- 3  a non-BCN refusal must never be borrowed for a J frame
    print('3 refusal_for(): a same-ms refusal of a DATA frame must not explain a J frame')
    S = build(tmp, NODES, [
        {'type': 'tx_deferred', 'time_ms': 500, 'node': 'MobileM', 'len': 40,
         'reason': 'duty_cycle_exceeded', 'sf': 8, 'label': 'DATA', 'busy_until_ms': 9},
    ])
    check('3 refused', bool(S.refusal_for('MobileM', 500)), False)

    print(f'\n{len(FAILS)} self-test failure(s)')
    for f in FAILS:
        print(f'  FAILED: {f}')
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
