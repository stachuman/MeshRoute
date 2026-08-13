#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""B183: attribute every J-family frame the firmware reported as sent that never
reached the air.

Edges:
  A  mobile_discover_tx (INTENT — emitted BEFORE tx_initiating) -> DISCOVER PHY tx
  C  mobile_offer_tx    (emitted at the accepted HAL handoff)   -> OFFER    PHY tx

★★ AMBIGUITY IS REFUSED, NOT GUESSED. A frame is bound to a simulator refusal only
   by `graph.Stream.refusal_for`'s two EXACT bindings. Anything else is counted as
   UNATTRIBUTED and printed in full, and the tool EXITS 1 unless
   `--allow-unattributed` is passed.
⛔ An earlier revision attributed a missing frame to "the next BCN refusal within
   4000 ms". That is a guess and it inflated the attribution rate to 100 %; it was
   caught by independent review and is deliberately recorded here as retired.

Usage: lostj.py <scenario.json> <stream.ndjson> [--allow-unattributed]
Exit:  0 = every lost J frame bound exactly · 1 = at least one UNATTRIBUTED
"""
import sys, collections, graph

def main(argv):
    cfg, nd = argv[1], argv[2]
    allow = '--allow-unattributed' in argv
    S = graph.Stream(cfg, nd)
    print(f'stream={nd}')
    unattributed_total = 0
    for emit, phy in (('mobile_discover_tx', 'DISCOVER'), ('mobile_offer_tx', 'OFFER')):
        air = collections.defaultdict(set)
        for e in S.tx:
            if e['j']['kind'] == phy:
                air[e['node']].add(e['time_ms'])
        aired = sum(len(v) for v in air.values())
        emits = S.emits_of(kinds={emit})
        reasons, refused = collections.Counter(), []
        for (t, n, k, d) in emits:
            nm = S.names[n]
            if t in air[nm]:
                continue
            r = S.refusal_for(nm, t)
            if not r:
                refused.append(r.why)
            else:
                reasons[(r['reason'], r['via'])] += 1
        lost = len(emits) - aired
        print(f'  {emit}: {len(emits)} emits -> {aired} {phy} PHY tx; {lost} never aired')
        for (rsn, via), c in sorted(reasons.items(), key=lambda x: -x[1]):
            print(f'      {c:>3}  ATTRIBUTED  reason={rsn}  via={via}')
        print(f'      {len(refused):>3}  ⛔ UNATTRIBUTED (ambiguity REFUSED, never guessed)')
        for w in refused:
            print(f'             · {w}')
        assert sum(reasons.values()) + len(refused) == lost, 'accounting mismatch'
        unattributed_total += len(refused)

    print('  attributable-loss emits the FIRMWARE raised (0 ⇒ the silent path was the one taken):')
    for k in ('tx_deferred_lost', 'mobile_offer_dropped', 'mobile_offer_admission_rejected',
              'mobile_tx_rejected', 'tx_hal_rejected', 'mobile_tx_cancelled_stale',
              'mobile_offer_ring_full'):
        print(f'      {k:<34} {len(S.emits_of(kinds={k}))}')

    if unattributed_total and not allow:
        print(f'\nFAIL: {unattributed_total} lost J frame(s) could not be bound EXACTLY. '
              f'This is the correct outcome for an ambiguous stream — pass '
              f'--allow-unattributed to report them without failing.')
        return 1
    print(f'\nOK: {unattributed_total} unattributed'
          f'{" (accepted via --allow-unattributed)" if unattributed_total else ""}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
