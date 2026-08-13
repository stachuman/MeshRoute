#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""B183: post-loss mobile attachment TRANSACTION GRAPH from a lus NDJSON stream.

  DISCOVER PHY tx -> per-host PHY rx (or a named loss) -> host OFFER staging ->
  OFFER PHY tx (or the simulator's refusal) -> mobile PHY rx -> selected OFFER ->
  CLAIM PHY tx -> chosen-host PHY rx -> mobile_registered -> roster ->
  mobile_attach_confirmed.

★★ AMBIGUITY IS REFUSED, NOT WARNED ABOUT. When a byte-identical re-emission of the
   same `pkt` falls inside an emission's arrival window, this tool prints ⛔ REFUSED
   and does NOT consume the receives (an earlier revision printed a warning and
   consumed them anyway — caught by independent review). Any refusal makes the tool
   EXIT 1 unless `--allow-ambiguous` is passed.

Usage: txgraph.py <scenario.json> <stream.ndjson> [mobile-name-substring] [--allow-ambiguous]
Exit:  0 = no correlation was refused · 1 = at least one refusal
"""
import sys, collections, graph

LIFE = {'mobile_adopted', 'mobile_reset', 'mobile_attach_confirmed',
        'presence_home_lost', 'mobile_reclaim_tx'}


def run(cfg, nd, only=None, out=sys.stdout):
    S = graph.Stream(cfg, nd)
    P = lambda *a: print(*a, file=out)
    refusals = []
    defer = collections.defaultdict(list)
    for e in S.deferred:
        defer[(e['node'], e['time_ms'])].append(e)

    for m in S.mobiles:
        name, key = S.names[m], S.keyhash[m]
        if only and only not in name:
            continue
        P('#' * 104)
        P(f'# {name}   key_hash32=0x{key:08x}   (configured node_id={S.nodeid[m]}, NOT used for correlation)')
        P('#' * 104)
        life = S.emits_of(node=m, kinds=LIFE)
        last_reset = S.last_reset_ms(m)
        P(f'  adopted (PROVISIONAL)  = {[(t,d["home"],d["local_id"],d["epoch"]) for (t,n,k,d) in life if k=="mobile_adopted"]}')
        P(f'  attach_confirmed       = {[(t,d["home"],d["local_id"],d["epoch"]) for (t,n,k,d) in life if k=="mobile_attach_confirmed"]}')
        P(f'  reset                  = {[(t,d["reason"]) for (t,n,k,d) in life if k=="mobile_reset"]}')
        P(f'  presence_home_lost     = {[(t,d["home"],d["miss"]) for (t,n,k,d) in life if k=="presence_home_lost"]}')
        P(f'  ⇒ POST-LOSS INTERVAL = ({last_reset} .. end], all figures below are restricted to it')

        intents = [t for (t, n, k, d) in S.emits_of(node=m, kinds={'mobile_discover_tx'}) if t > last_reset]
        discs = [e for e in S.tx if e['node'] == name and e['j']['kind'] == 'DISCOVER'
                 and e['time_ms'] > last_reset]
        P(f'  EDGE A  mobile_discover_tx (INTENT) = {len(intents)}   DISCOVER PHY tx = {len(discs)}')
        airedA = {d['time_ms'] for d in discs}
        for t in intents:
            if t in airedA:
                continue
            r = S.refusal_for(name, t)
            if not r:
                refusals.append(r.why)
                P(f'      ⛔ intent t={t} never aired; REFUSED to attribute: {r.why}')
            else:
                P(f'      ⛔ intent t={t} NEVER REACHED THE AIR: {r["reason"]} '
                  f'(at {r["at_ms"]}, via {r["via"]})')

        tot = collections.Counter()
        for d in discs:
            t0, j = d['time_ms'], d['j']
            P('')
            P(f'  ── DISCOVER  pkt={d["pkt"]} t={t0} sf={d["sf"]} bw={d["bw_hz"]} air={d["airtime_ms"]}ms '
              f'len={j["len"]}B  last_home_id={j.get("last_home_id")} last_epoch={j.get("last_reg_epoch")}')
            arr = S.arrivals(d)
            if not arr:
                refusals.append(arr.why)
                P(f'     ⛔ EDGE B REFUSED — receives NOT consumed: {arr.why}')
                continue
            rxs, losses = arr
            tot['discover_air'] += 1
            recv = S.static_receivers(d)
            hosts = sorted(recv, key=lambda h: -recv[h][0])
            n_elig = sum(1 for h in hosts if recv[h][1])
            lc = collections.Counter(r['type'] for r in losses)
            P(f'     EDGE B  DISCOVER -> static PHY rx: received by {len(hosts)} statics '
              f'({n_elig} of them ELIGIBLE hosts); explicit losses {dict(lc)}')
            tot['static_rx'] += len(hosts); tot['host_rx_eligible'] += n_elig
            t1 = t0 + 6000
            for h in hosts:
                hi = S.idx[h]
                snr, is_elig = recv[h]
                sched = [x for x in S.emits_of(node=hi, t0=t0, t1=t1,
                         kinds={'mobile_offer_scheduled', 'mobile_offer_coalesced',
                                'mobile_offer_admission_rejected', 'mobile_offer_dropped'})
                         if x[3].get('to_key') == key]
                oemit = [x for x in S.emits_of(node=hi, kinds={'mobile_offer_tx'}, t0=t0, t1=t1)
                         if x[3].get('to_key') == key]
                otx = [e for e in S.tx if e['node'] == h and e['j']['kind'] == 'OFFER'
                       and e['j'].get('target_key_hash32') == key and t0 <= e['time_ms'] <= t1]
                tot['staged'] += len(sched); tot['offer_tx_emit'] += len(oemit); tot['offer_air'] += len(otx)
                P(f'       · {"HOST " if is_elig else "opted-out " }{h:<22} snr={snr:>6.1f} dB   '
                  f'staged={len(sched)}  mobile_offer_tx(emit)={len(oemit)}  OFFER PHY tx={len(otx)}')
                airedC = {e['time_ms'] for e in otx}
                for (tt, nn, kk, dd) in oemit:
                    if tt in airedC:
                        continue
                    r = S.refusal_for(h, tt)
                    if not r:
                        refusals.append(r.why)
                        P(f'            ⛔ EDGE C: mobile_offer_tx t={tt} never aired; REFUSED: {r.why}')
                        tot['offer_lost_unattributed'] += 1
                    else:
                        P(f'            ⛔ EDGE C: mobile_offer_tx t={tt} local_id={dd.get("local_id")} '
                          f'but NO OFFER PHY tx: {r["reason"]} (at {r["at_ms"]}, via {r["via"]})')
                        tot['offer_lost_at_hal'] += 1
                for e in otx:
                    a2 = S.arrivals(e)
                    if not a2:
                        refusals.append(a2.why); P(f'            ⛔ OFFER rx REFUSED: {a2.why}'); continue
                    rx2, lo2 = a2
                    at = [r for r in rx2 if r['to'] == name]
                    lo3 = [r for r in lo2 if r.get('to') == name]
                    P(f'            OFFER pkt={e["pkt"]} t={e["time_ms"]} local_id={e["j"]["proposed_mobile_id"]} '
                      f'-> mobile PHY rx={len(at)}' + ('' if at else '  LOSS=' + str(
                          [(x['type'], x.get('snr'), x.get('threshold'), x.get('interferer')) for x in lo3])))
                    tot['offer_rx_at_mobile'] += len(at)
            for e in [x for x in S.tx if x['node'] == name and x['j']['kind'] == 'CLAIM'
                      and x['j']['key_hash32'] == key and t0 <= x['time_ms'] <= t0 + 8000]:
                tot['claim_air'] += 1
                a3 = S.arrivals(e)
                got = '(REFUSED)' if not a3 else [r['to'] for r in a3[0]]
                if not a3:
                    refusals.append(a3.why)
                else:
                    tot['claim_rx_at_host'] += len(a3[0])
                P(f'     EDGE D  CLAIM pkt={e["pkt"]} t={e["time_ms"]} chosen_host_id={e["j"]["chosen_host_id"]} '
                  f'local_id={e["j"]["proposed_node_id"]} epoch={e["j"]["claim_epoch"]} -> host PHY rx {got}')
            nh = S.emits_of(node=m, kinds={'mobile_no_host'}, t0=t0, t1=t0 + 12000)
            if nh:
                tot['no_host'] += 1
                P(f'     ⇒ mobile_no_host t={nh[0][0]} backoff_ms={nh[0][3]["backoff_ms"]}')
        P('')
        P(f'  POST-LOSS TOTALS {name}: {dict(tot)}')
        P('')
    return refusals


if __name__ == '__main__':
    argv = [a for a in sys.argv if a != '--allow-ambiguous']
    allow = '--allow-ambiguous' in sys.argv
    ref = run(argv[1], argv[2], argv[3] if len(argv) > 3 else None)
    if ref:
        print(f'\n{"OK (accepted via --allow-ambiguous)" if allow else "FAIL"}: '
              f'{len(ref)} correlation(s) REFUSED as ambiguous.')
        sys.exit(0 if allow else 1)
    print('\nOK: 0 correlations refused — every receive/refusal bound exactly.')
    sys.exit(0)
