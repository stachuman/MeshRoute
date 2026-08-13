#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""B183 diagnosis: shared lus-NDJSON reader + J-frame decoder + the ONE correlator.

⛔ Diagnosis instrument only. No firmware/telemetry change. Correlation is by
   decoded J-frame contents (key_hash32 / target_key_hash32 / chosen_host_id)
   and simulator `pkt`+arrival-window identity — NEVER the configured node_id.

★★ AMBIGUITY IS REFUSED, NEVER GUESSED ([[B182]]'s rule). The simulator `pkt` is
   a CONTENT hash reused by byte-identical re-emissions (up to 104x in `s07`), and
   a node can have several BCN frames refused in one millisecond. Every correlation
   below therefore returns an EXPLICIT refusal instead of picking a candidate, and
   the callers must surface it (see txgraph.py / lostj.py, which exit nonzero).
   ⛔ Do not "improve" any of these into a nearest-match heuristic: an earlier
   revision of this very file did exactly that and independent review caught it.
"""
import json, collections, math

ARRIVAL_SLACK_MS = 500          # rx/loss records land at tx.t + airtime (+ a few ms of receiver skew)
LOSS_TYPES = ('collision', 'drop_weak', 'drop_sf_mismatch', 'drop_preamble_miss', 'drop_rx_blind')


class Refused:
    """An explicit refusal to attribute. Truthy-false so `if not result:` is safe."""
    __slots__ = ('why',)

    def __init__(self, why):
        self.why = why

    def __bool__(self):
        return False

    def __repr__(self):
        return f'REFUSED({self.why})'


# ---------------------------------------------------------------- J frame decode
def dec_j(hexstr):
    """Decode a J-family frame per frame_codec.cpp pack_j_* / parse_j. None = not J."""
    try:
        b = bytes.fromhex(hexstr)
    except Exception:
        return None
    if len(b) < 2 or (b[0] >> 4) != 0x9:
        return None
    o = {'leaf_id': b[0] & 0x0F, 'gw': bool(b[1] & 0x80), 'is_mobile': bool(b[1] & 0x40),
         'opcode': (b[1] >> 4) & 0x03, 'wire_version': b[1] & 0x0F, 'len': len(b)}
    op = o['opcode']
    u32 = lambda i: int.from_bytes(b[i:i + 4], 'little')
    if op == 0:                                              # DISCOVER 6 / 9 / 13 B
        if len(b) not in (6, 9, 13):
            return None
        o['kind'] = 'DISCOVER'; o['key_hash32'] = u32(2)
        if len(b) >= 9:
            o['last_home_id'] = b[6]; o['last_home_layer'] = b[7]; o['last_reg_epoch'] = b[8]
        if len(b) == 13:
            o['last_home_key_hash32'] = u32(9)
    elif op == 3:                                            # OFFER 8 B static / 13 B mobile
        if len(b) != (13 if o['is_mobile'] else 8):
            return None
        o['kind'] = 'OFFER'; o['responder_node_id'] = b[2]
        o['responder_key_hash32'] = u32(3); o['data_sf_bitmap'] = b[7]
        if o['is_mobile']:
            o['proposed_mobile_id'] = b[8]; o['target_key_hash32'] = u32(9)
    elif op == 1:                                            # CLAIM 11 B
        if len(b) != 11:
            return None
        o['kind'] = 'CLAIM'; o['key_hash32'] = u32(2); o['proposed_node_id'] = b[6]
        o['lease_age_seconds'] = int.from_bytes(b[7:9], 'little')
        o['claim_epoch'] = b[9]; o['chosen_host_id'] = b[10]
    elif op == 2:                                            # DENY 15 / 19 B
        if len(b) not in (15, 19):
            return None
        o['kind'] = 'DENY'; o['denied_node_id'] = b[2]
        o['owner_key_hash32'] = u32(3); o['claimant_key_hash32'] = u32(7); o['reason'] = b[13]
    return o


def haversine(a, b):
    R = 6371000.0
    la1, lo1, la2, lo2 = map(math.radians, (a[0], a[1], b[0], b[1]))
    h = math.sin((la2 - la1) / 2) ** 2 + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2) ** 2
    return 2 * R * math.asin(math.sqrt(h))


class Stream:
    def __init__(self, cfgpath, ndjson):
        cfg = json.load(open(cfgpath))
        self.cfg = cfg
        self.names = [n['name'] for n in cfg['nodes']]
        self.idx = {n: i for i, n in enumerate(self.names)}
        self.keyhash, self.nodeid = {}, {}
        for i, n in enumerate(cfg['nodes']):
            if 'key_hash32' in n:
                self.keyhash[i] = int(str(n['key_hash32']), 16)
            self.nodeid[i] = n.get('node_id')
        self.mobiles = [i for i, n in enumerate(cfg['nodes'])
                        if n.get('config', {}).get('is_mobile')]
        self.eligible = {i for i in range(len(self.names)) if i not in self.mobiles
                         and not cfg['nodes'][i].get('config', {}).get('is_gateway')
                         and cfg['nodes'][i].get('config', {}).get('host_mobiles', True)}

        self.tx, self.alltx, self.deferred, self.emits = [], [], [], []
        self.tx_by_pkt = {}
        self.rx = collections.defaultdict(list)
        self.loss = collections.defaultdict(list)
        self.pos = collections.defaultdict(list)
        for line in open(ndjson):
            try:
                e = json.loads(line)
            except Exception:
                continue
            t = e.get('type')
            if t == 'tx':
                self.alltx.append(e)
                j = dec_j(e.get('hex', ''))
                if j:
                    e['j'] = j
                    self.tx.append(e)
            elif t == 'rx':
                self.rx[e['pkt']].append(e)
            elif t in LOSS_TYPES:
                self.loss[e['pkt']].append(e)
            elif t == 'script_emit':
                self.emits.append((e['time_ms'], e['node'], e['emit_type'], e['data']))
                if e['emit_type'] == 'mobile_visibility':
                    d = e['data']
                    self.pos[e['node']].append((e['time_ms'], d['lat'], d['lon']))
            elif t == 'tx_deferred':
                self.deferred.append(e)

        # ---- emission index for pkt correlation
        self._emis = collections.defaultdict(list)
        for e in self.alltx:
            self._emis[e['pkt']].append(e['time_ms'])
        for k in self._emis:
            self._emis[k].sort()
        # ---- simulator refusal index (label+reason), per node, per ms
        self._ref = collections.defaultdict(list)
        for e in self.deferred:
            self._ref[e['node']].append((e['time_ms'], e['label'], e['reason']))
        for k in self._ref:
            self._ref[k].sort()
        # ---- firmware LBT-defer requests, per node
        self._lbt = collections.defaultdict(list)
        for (t, n, k, d) in self.emits:
            if k == 'tx_lbt_defer' and d.get('kind') == 'initiating':
                self._lbt[self.names[n]].append((t, d['defer_ms']))

    # ---------------------------------------------------------------- emits
    def emits_of(self, node=None, kinds=None, t0=None, t1=None):
        out = []
        for (t, n, k, d) in self.emits:
            if node is not None and n != node:
                continue
            if kinds is not None and k not in kinds:
                continue
            if t0 is not None and t < t0:
                continue
            if t1 is not None and t > t1:
                continue
            out.append((t, n, k, d))
        return out

    def last_reset_ms(self, mobile_idx):
        r = [t for (t, n, k, d) in self.emits_of(node=mobile_idx, kinds={'mobile_reset'})]
        return r[-1] if r else 0

    # ---------------------------------------------------------------- pkt -> receives
    def arrivals(self, tx):
        """Receives+losses belonging to THIS emission, or Refused() when a byte-identical
        re-emission of the same pkt falls inside the arrival window (then the records
        cannot be split and MUST NOT be consumed)."""
        ts = self._emis[tx['pkt']]
        lo, hi = tx['time_ms'], tx['time_ms'] + tx['airtime_ms'] + ARRIVAL_SLACK_MS
        clash = [t for t in ts if t != tx['time_ms'] and lo <= t <= hi]
        if clash:
            return Refused(f'pkt {tx["pkt"]} re-emitted at {clash} inside the arrival window '
                           f'[{lo},{hi}] — receives cannot be attributed')
        rx = [r for r in self.rx.get(tx['pkt'], []) if lo <= r['time_ms'] <= hi]
        ls = [r for r in self.loss.get(tx['pkt'], []) if lo <= r['time_ms'] <= hi]
        return (rx, ls)

    def static_receivers(self, tx):
        """Every NON-MOBILE node that PHY-received this emission, each tagged with whether
        it is an ELIGIBLE host. ⛔ Deliberately separate from `eligible`: C3 makes the
        replacement static ineligible ON PURPOSE, and the whole point of that control is
        that RECEPTION is unchanged while only the eligibility term moves. Counting only
        eligible receivers would silently erase the evidence."""
        arr = self.arrivals(tx)
        if not arr:
            return arr
        out = {}
        for r in arr[0]:
            i = self.idx.get(r['to'])
            if i is None or i in self.mobiles:
                continue
            out[r['to']] = (max(out.get(r['to'], (-999, False))[0], r['snr']), i in self.eligible)
        return out

    # ---------------------------------------------------------------- lost J frame -> refusal
    def refusal_for(self, nodename, t):
        """Why did a J frame the firmware reported as sent at `t` never reach the air?

        Two admissible bindings, both EXACT; anything else is refused:
          (1) the frame crossed `_hal.tx()` in this millisecond -> exactly ONE `tx_deferred`
              with label BCN at the SAME millisecond;
          (2) the frame was LBT-deferred -> exactly ONE `tx_lbt_defer{kind:"initiating"}` at
              `t` (so the deferred frame is determined) AND exactly ONE BCN refusal at
              `t + defer_ms`.
        ⛔ No nearest-match, no time horizon, no "first candidate wins"."""
        same = [(rt, lab, rsn) for (rt, lab, rsn) in self._ref.get(nodename, [])
                if rt == t and lab == 'BCN']
        if len(same) == 1:
            return {'reason': same[0][2], 'at_ms': t, 'via': 'same_ms'}
        if len(same) > 1:
            return Refused(f'{nodename} t={t}: {len(same)} BCN refusals in the SAME millisecond '
                           f'({[x[2] for x in same]}) — cannot bind this frame to one of them')
        defers = [dm for (dt, dm) in self._lbt.get(nodename, []) if dt == t]
        if len(defers) != 1:
            return Refused(f'{nodename} t={t}: no same-ms BCN refusal and '
                           f'{len(defers)} concurrent LBT defer requests — no exact binding')
        fire = t + defers[0]
        cand = [(rt, lab, rsn) for (rt, lab, rsn) in self._ref.get(nodename, [])
                if rt == fire and lab == 'BCN']
        if len(cand) != 1:
            return Refused(f'{nodename} t={t}: LBT defer fires at {fire} but {len(cand)} '
                           f'BCN refusals there — no exact binding')
        return {'reason': cand[0][2], 'at_ms': fire, 'via': f'lbt_defer(+{defers[0]}ms)'}
