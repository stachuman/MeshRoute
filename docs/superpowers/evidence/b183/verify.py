#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""B183 durable VERIFIER for the five preserved fixtures. ⛔ EXITS NONZERO ON FAILURE.

Why this file exists: the scenarios' own `expect` blocks cannot express the edges
this diagnosis is about (exact `(home, local_id, epoch)` tuples, "an emit fired but
no PHY frame exists"), and four of the five fixtures had NO `expect` entries at all,
so every replay reported success no matter what the behaviour did. A fixture that
cannot fail preserves a story, not evidence. This verifier is the fixture's teeth.

Each check names the EXACT graph edge it pins. Identity is ALWAYS the stable
`key_hash32` plus the leased `(home, local_id, epoch)` triple — ⛔ never the
configured mobile `node_id`.

Usage: verify.py <dir-with-the-five-ndjson-streams> [--mutate <check-id>]
       `--mutate` inverts one expectation to prove this verifier can fail
       (it must then report that check as FAILED and exit 1).
Exit:  0 = every check passed · 1 = at least one check failed
"""
import sys, os, collections, graph

MOB_KEY = 0xe27bc270
HOME_A, HOME_B = 19, 20                     # configured node ids, used ONLY to read the leased triple
LOCAL, EP1, EP2 = 254, 1, 2

FIX = ('b183_c1_healthy', 'b183_c2_postloss', 'b183_c3_ineligible',
       'b183_c4_hostduty', 'b183_c5_mobileduty')


class Checker:
    def __init__(self, mutate=None):
        self.fails, self.n, self.mutate = [], 0, mutate

    def eq(self, cid, label, got, want):
        if self.mutate == cid:                       # mutation proof: invert the expectation
            want = (want + 1) if isinstance(want, int) else f'MUTATED::{want}'
        self.n += 1
        ok = got == want
        print(f'   [{"PASS" if ok else "FAIL"}] {cid:<7} {label}: got {got!r} want {want!r}')
        if not ok:
            self.fails.append(f'{cid} {label}: got {got!r} want {want!r}')

    def ge(self, cid, label, got, want):
        if self.mutate == cid:
            want = got + 1
        self.n += 1
        ok = got >= want
        print(f'   [{"PASS" if ok else "FAIL"}] {cid:<7} {label}: got {got!r} want >= {want!r}')
        if not ok:
            self.fails.append(f'{cid} {label}: got {got!r} want >= {want!r}')


# ---------------------------------------------------------------- extractors
def confirmed(S, m):
    """The ONLY proof of final success: matching-roster confirmations, as (home, local_id, epoch)."""
    return [(d['home'], d['local_id'], d['epoch'])
            for (t, n, k, d) in S.emits_of(node=m, kinds={'mobile_attach_confirmed'})]


def adopted(S, m):
    """PROVISIONAL adoption only — deliberately reported separately from confirmed."""
    return [(d['home'], d['local_id'], d['epoch'])
            for (t, n, k, d) in S.emits_of(node=m, kinds={'mobile_adopted'})]


def claim_received(S, m, host_id, epoch):
    """A CLAIM naming `host_id`/`epoch` that the chosen host PHY-received. Refusal-safe."""
    got = 0
    for e in S.tx:
        j = e['j']
        if j['kind'] != 'CLAIM' or j['key_hash32'] != S.keyhash[m]:
            continue
        if j['chosen_host_id'] != host_id or j['claim_epoch'] != epoch:
            continue
        arr = S.arrivals(e)
        if not arr:
            continue                                  # ambiguous -> not counted as evidence
        want = next((nm for nm, i in S.idx.items() if S.nodeid[i] == host_id), None)
        got += sum(1 for r in arr[0] if r['to'] == want)
    return got


def registered_at(S, host_id, local_id, epoch, key):
    hi = next((i for i in range(len(S.names)) if S.nodeid[i] == host_id), None)
    return sum(1 for (t, n, k, d) in S.emits_of(node=hi, kinds={'mobile_registered'})
               if d['key'] == key and d['local_id'] == local_id and d['epoch'] == epoch)


def rx_at(S, m, hostname):
    """Post-loss DISCOVER PHY receptions at a NAMED static, regardless of its eligibility.
    ⛔ Must not be filtered by `S.eligible`: C3 makes the replacement ineligible ON PURPOSE
    and its whole point is that RECEPTION is unchanged while only eligibility moves."""
    t0, name = S.last_reset_ms(m), S.names[m]
    n = 0
    for e in S.tx:
        if e['node'] != name or e['j']['kind'] != 'DISCOVER' or e['time_ms'] <= t0:
            continue
        recv = S.static_receivers(e)
        if recv and hostname in recv:
            n += 1
    return n


def postloss(S, m):
    """(t_last_reset, discover intents, DISCOVER PHY tx, per-host staged/offer_emit/offer_air)"""
    t0, name, key = S.last_reset_ms(m), S.names[m], S.keyhash[m]
    intents = [t for (t, n, k, d) in S.emits_of(node=m, kinds={'mobile_discover_tx'}) if t > t0]
    phy = [e for e in S.tx if e['node'] == name and e['j']['kind'] == 'DISCOVER' and e['time_ms'] > t0]
    staged = [x for x in S.emits_of(kinds={'mobile_offer_scheduled'}, t0=t0)
              if x[3].get('to_key') == key]
    oemit = [x for x in S.emits_of(kinds={'mobile_offer_tx'}, t0=t0) if x[3].get('to_key') == key]
    oair = [e for e in S.tx if e['j']['kind'] == 'OFFER'
            and e['j'].get('target_key_hash32') == key and e['time_ms'] > t0]
    hostrx = 0
    for d in phy:
        arr = S.arrivals(d)
        if arr:
            hostrx += len({r['to'] for r in arr[0] if S.idx.get(r['to']) in S.eligible})
    return t0, intents, phy, hostrx, staged, oemit, oair


def refusal_reasons(S, nodename, times):
    out = collections.Counter()
    for t in times:
        r = S.refusal_for(nodename, t)
        out[r['reason'] if r else 'UNATTRIBUTED'] += 1
    return out


def main(argv):
    d = argv[1]
    mutate = argv[argv.index('--mutate') + 1] if '--mutate' in argv else None
    C = Checker(mutate)
    if mutate:
        print(f'*** MUTATION MODE: expectation {mutate} is inverted; this run MUST fail ***')

    S = {f: graph.Stream(os.path.join(os.path.dirname(os.path.abspath(__file__)), f + '.json'),
                         os.path.join(d, f + '.ndjson')) for f in FIX}

    # ---------------- C1 healthy positive control: the instrument must be reachable
    s = S['b183_c1_healthy']; m = s.mobiles[0]
    print('C1 healthy positive control (no home loss) — proves every edge can be non-zero')
    C.eq('C1-1', 'confirmed (home,local_id,epoch)', confirmed(s, m), [(HOME_A, LOCAL, EP1)])
    C.eq('C1-2', 'mobile_reset count', len(s.emits_of(node=m, kinds={'mobile_reset'})), 0)
    _, it, phy, hrx, st, oe, oa = postloss(s, m)
    C.eq('C1-3', 'EDGE A intents == DISCOVER PHY tx', (len(it), len(phy)), (1, 1))
    C.ge('C1-4', 'EDGE B eligible-host PHY rx', hrx, 1)
    C.ge('C1-5', 'EDGE C OFFER PHY tx', len(oa), 1)
    C.ge('C1-6', 'EDGE C OFFER PHY rx at the mobile',
         sum(len([r for r in (s.arrivals(e)[0] if s.arrivals(e) else []) if r['to'] == s.names[m]])
             for e in oa), 1)
    C.eq('C1-7', 'EDGE D CLAIM(host 19, epoch 1) PHY-received at HomeA',
         claim_received(s, m, HOME_A, EP1), 1)
    C.eq('C1-8', 'mobile_registered at HomeA (key, 254, epoch 1)',
         registered_at(s, HOME_A, LOCAL, EP1, MOB_KEY), 1)

    # ---------------- C2 post-loss positive control: a full reattachment must be observable
    s = S['b183_c2_postloss']; m = s.mobiles[0]
    print('C2 post-loss positive control (HomeA dies; HomeB eligible + duty-idle)')
    C.eq('C2-1', 'presence_home_lost naming HomeA',
         [(x[3]['home'], x[3]['miss']) for x in s.emits_of(node=m, kinds={'presence_home_lost'})],
         [(HOME_A, 3)])
    C.eq('C2-2', 'confirmed (home,local_id,epoch) x2',
         confirmed(s, m), [(HOME_A, LOCAL, EP1), (HOME_B, LOCAL, EP2)])
    t0, it, phy, hrx, st, oe, oa = postloss(s, m)
    C.eq('C2-3', 'the epoch-2 confirmation lies AFTER the loss/reset',
         [(dd['home'], dd['local_id'], dd['epoch']) for (t, n, k, dd)
          in s.emits_of(node=m, kinds={'mobile_attach_confirmed'}, t0=t0)],
         [(HOME_B, LOCAL, EP2)])
    C.eq('C2-4', 'EDGE A post-loss intents == DISCOVER PHY tx', (len(it), len(phy)), (1, 1))
    C.ge('C2-5', 'EDGE B post-loss eligible-host PHY rx', hrx, 1)
    C.eq('C2-6', 'EDGE C post-loss staged / offer_tx emit / OFFER PHY tx',
         (len(st), len(oe), len(oa)), (1, 1, 1))
    C.eq('C2-7', 'EDGE D CLAIM(host 20, epoch 2) PHY-received at HomeB',
         claim_received(s, m, HOME_B, EP2), 1)
    C.eq('C2-8', 'mobile_registered at HomeB (key, 254, epoch 2)',
         registered_at(s, HOME_B, LOCAL, EP2, MOB_KEY), 1)

    # ---------------- C3 eligibility mutation: DISCOVER received, ZERO staging
    s = S['b183_c3_ineligible']; m = s.mobiles[0]
    print('C3 eligibility mutation (HomeB host_mobiles=false) — edge C FIRST hop must vanish')
    C.eq('C3-1', 'confirmed = the initial attachment ONLY', confirmed(s, m), [(HOME_A, LOCAL, EP1)])
    t0, it, phy, hrx, st, oe, oa = postloss(s, m)
    C.ge('C3-2', 'EDGE A post-loss DISCOVER PHY tx', len(phy), 1)
    hb = next(nm for nm, i in s.idx.items() if s.nodeid[i] == HOME_B)
    C.eq('C3-3a', 'HomeB is correctly NOT an eligible host', s.idx[hb] in s.eligible, False)
    C.ge('C3-3b', 'EDGE B DISCOVER PHY-received AT HomeB (reception UNCHANGED)', rx_at(s, m, hb), 1)
    C.eq('C3-4', 'EDGE C staged / offer_tx emit / OFFER PHY tx ALL ZERO',
         (len(st), len(oe), len(oa)), (0, 0, 0))
    C.ge('C3-5', 'mobile_no_host raised', len(s.emits_of(node=m, kinds={'mobile_no_host'}, t0=t0)), 1)

    # ---------------- C4 host-side transmitter mutation: offer_tx present, ZERO PHY frame
    s = S['b183_c4_hostduty']; m = s.mobiles[0]
    print('C4 host TX duty exhausted — edge C LAST hop must vanish, the EMIT must remain')
    C.eq('C4-1', 'confirmed = the initial attachment ONLY', confirmed(s, m), [(HOME_A, LOCAL, EP1)])
    t0, it, phy, hrx, st, oe, oa = postloss(s, m)
    C.ge('C4-2', 'EDGE A post-loss DISCOVER PHY tx', len(phy), 1)
    hb = next(nm for nm, i in s.idx.items() if s.nodeid[i] == HOME_B)
    C.eq('C4-3a', 'HomeB IS still an eligible host', s.idx[hb] in s.eligible, True)
    C.ge('C4-3b', 'EDGE B DISCOVER PHY-received AT HomeB (reception UNCHANGED)', rx_at(s, m, hb), 1)
    C.ge('C4-4', 'EDGE C mobile_offer_tx emits PRESENT', len(oe), 1)
    C.ge('C4-5', 'EDGE C staged PRESENT', len(st), 1)
    C.eq('C4-6', 'EDGE C OFFER PHY tx ZERO', len(oa), 0)
    C.eq('C4-7', 'every lost OFFER bound EXACTLY to duty_cycle_exceeded at HomeB',
         dict(refusal_reasons(s, hb, [x[0] for x in oe])), {'duty_cycle_exceeded': len(oe)})

    # ---------------- C5 mobile-side transmitter mutation: intent present, ZERO PHY frame
    s = S['b183_c5_mobileduty']; m = s.mobiles[0]
    print('C5 mobile TX duty exhausted — edge A must vanish, the INTENT must remain')
    C.eq('C5-1', 'confirmed = the initial attachment ONLY', confirmed(s, m), [(HOME_A, LOCAL, EP1)])
    C.eq('C5-2', 'but mobile_adopted DOES carry the unconfirmed epoch-2 provisional',
         (HOME_B, LOCAL, EP2) in adopted(s, m), True)
    C.eq('C5-3', 'and it was reset as claim_unconfirmed',
         [dd['reason'] for (t, n, k, dd) in s.emits_of(node=m, kinds={'mobile_reset'})][-1],
         'claim_unconfirmed')
    t0, it, phy, hrx, st, oe, oa = postloss(s, m)
    C.ge('C5-4', 'EDGE A mobile_discover_tx INTENTS PRESENT', len(it), 1)
    C.eq('C5-5', 'EDGE A DISCOVER PHY tx ZERO', len(phy), 0)
    C.eq('C5-6', 'every lost DISCOVER bound EXACTLY to duty_cycle_exceeded at the mobile',
         dict(refusal_reasons(s, s.names[m], it)), {'duty_cycle_exceeded': len(it)})

    print(f'\n{C.n} checks, {len(C.fails)} failed')
    for f in C.fails:
        print(f'  FAILED: {f}')
    return 1 if C.fails else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
