#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""B188 durable VERIFIER for the rolling-duty-window fixtures. ⛔ EXITS NONZERO ON FAILURE.

⛔ NON-CORPUS. Nothing here touches `simulation/`, the 36-row runner or any anchor table.

WHY THIS FILE HAS TEETH, stated because the alternative has already happened once in this arc: a fixture
whose scenario `expect` block is empty and whose reader never exits nonzero reports success no matter what
the behaviour does. Every check below prints PASS/FAIL, the run exits 1 if any failed, and `--mutate <id>`
inverts one expectation to prove the verifier can fail.

★★ THE METHOD THAT MAKES THE `busy_until` / `wait_ms` CHECKS REAL: the duty ledger is RECONSTRUCTED from the
stream's own `tx` records and the refusal's arithmetic is recomputed independently, rather than asserting
that "a number was present". ⚠ The reconstruction is a **LOWER BOUND, not an exact replica**: a `tx` record's
`time_ms` is the DECISION instant while the simulator's ledger stamps `max(now, earliest_tx) + airtime`, and the
RX->TX turnaround inside that `max` is never published in the stream. Otherwise it follows the simulator's rule
(`FirmwareNode::airtimeUsedInWindow` / `oldestTxEndMs`, verified in source):
  · one ledger entry per transmitted frame: `end_ms = tx.time_ms + tx.airtime_ms`, weight = `airtime_ms`;
  · entries with `end_ms <= now - window` are dropped (`cutoff`, floored at 0);
  · `used(now)` = sum of the survivors; `oldest(now)` = the earliest surviving `end_ms`;
  · the enforced budget is `duty_cycle_fraction * window`, and a frame is refused iff
    `used + its_airtime > budget`;
  · a refusal's `busy_until_ms` is `oldest(now) + window` — the instant the FRONT entry ages out, i.e. the
    incremental step the corpus never reaches.

Usage: verify.py <dir-with-the-three-ndjson-streams> [--mutate <check-id>]
Exit:  0 = every check passed · 1 = at least one check failed
"""
import sys, os, json, collections, math

WINDOW   = 30000          # the compressed window A and B are authored with (gen.py WINDOW_MS)
FRACTION = 0.01           # simulation.radio.duty_cycle = 1 PERCENT
BUDGET   = int(FRACTION * WINDOW)          # 300 ms
ONESHOT_WINDOW = 3600000                   # the inherited default C deliberately keeps
ONESHOT_BUDGET = int(FRACTION * ONESHOT_WINDOW)

SCEN_A = 'b188_a_rolling_mobile'
SCEN_B = 'b188_b_rolling_data'
SCEN_C = 'b188_c_control_oneshot'


class Checker:
    def __init__(self, mutate=None):
        self.fails, self.n, self.mutate = [], 0, mutate

    def eq(self, cid, label, got, want):
        if self.mutate == cid:                       # mutation proof: invert the expectation
            want = (want + 1) if isinstance(want, (int, float)) else f'MUTATED::{want}'
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

    def lt(self, cid, label, got, want):
        if self.mutate == cid:
            want = got
        self.n += 1
        ok = got < want
        print(f'   [{"PASS" if ok else "FAIL"}] {cid:<7} {label}: got {got!r} want < {want!r}')
        if not ok:
            self.fails.append(f'{cid} {label}: got {got!r} want < {want!r}')


class Stream:
    """The three record kinds this fixture needs, plus the reconstructed per-node duty ledger."""

    def __init__(self, path):
        # ★★ ONE READER, ONE PASS (and only ONE parser — see the note above). Every record keeps its STREAM
        # ORDINAL `k`, the line's 0-based position, which is the causal clock: a timestamp is NOT an ordering
        # (many records share a millisecond), so the ordinal is what breaks the tie.
        # ⛔⛔ AND EVERY FIELD THIS FILE READS IS EITHER VALIDATED OR REFUSED. Three rounds of review found the
        #    same shape each time — an invented price, an unbound node, an overwritten observation, a defaulted
        #    node authority, an unchecked SF — so there is no `.get(key, default)` left on a load-bearing field
        #    and no assignment that can overwrite an earlier observation. A record missing a field it needs is
        #    appended to `self.malformed` and every scenario asserts that list is EMPTY.
        self.tx = []            # (k, t, node, airtime, label, frame_len, sf, bw_hz, cr)
        self.deferred = []      # (k, t, node, reason, label, busy_until, len, sf)
        self.emits = []         # (k, t, node_idx, kind, data)
        self.node_names = {}    # node index -> name (script_emit carries the INDEX, tx carries the NAME)
        # ★★★ THE PRICING KEY IS THE AIRTIME FUNCTION'S WHOLE DOMAIN, NOT `length`.
        # ⛔⛔ IT USED TO BE `airtime_of_len[len] = airtime`, a bare assignment keyed on LENGTH ALONE — so a
        #    second observation of the same length SILENTLY OVERWROTE the first (a conflicting pair therefore
        #    vanished instead of failing) AND the (sf, bw, cr) captured on the very next line was discarded,
        #    which is also why a wrong refusal SF was invisible. ⇒ observations are now a SET per
        #    (len, sf, bw_hz, cr) shape: two different airtimes for one shape is a CONFLICT, and a conflict
        #    invalidates pricing rather than being resolved.
        self.obs = collections.defaultdict(set)          # (len, sf, bw, cr) -> {airtime_ms, ...}
        self.sim_end_ms = None                           # the run horizon, from the `sim_end` record
        self.malformed = []                              # (ordinal, type, what was missing) — must stay EMPTY
        for k, d in enumerate(self._iter_records(path)):
            if 'type' not in d:
                # ⓘ AUDIT (round 3): an untyped record is REFUSED rather than skipped. Skipping was already
                #   fail-safe (a dropped `tx` removes ledger entries, which makes the arithmetic checks FAIL,
                #   not pass) — but "validate or refuse" is the rule, not "fail in a safe direction".
                self.malformed.append((k, '?', 'no type field'))
                continue
            ty = d['type']
            if ty == 'tx':
                need = ('time_ms', 'node', 'airtime_ms', 'hex', 'sf', 'bw_hz', 'cr', 'label')
                if any(f not in d for f in need) or not d['hex'] or not d['sf'] or not d['bw_hz'] or not d['cr']:
                    self.malformed.append((k, ty, 'missing/empty ' + ','.join(f for f in need if f not in d)))
                    continue
                flen = len(d['hex']) // 2
                shape = (flen, d['sf'], d['bw_hz'], d['cr'])
                self.tx.append((k, d['time_ms'], d['node'], d['airtime_ms'], d['label'], flen,
                                d['sf'], d['bw_hz'], d['cr']))
                self.obs[shape].add(d['airtime_ms'])     # a SET: a second, different value is kept, not lost
            elif ty == 'tx_deferred':
                need = ('time_ms', 'node', 'reason', 'label', 'busy_until_ms', 'len', 'sf')
                if any(f not in d for f in need):
                    self.malformed.append((k, ty, 'missing ' + ','.join(f for f in need if f not in d)))
                    continue
                self.deferred.append((k, d['time_ms'], d['node'], d['reason'], d['label'],
                                      d['busy_until_ms'], d['len'], d['sf']))
            elif ty == 'script_emit':
                if 'time_ms' not in d or 'node' not in d or 'emit_type' not in d or 'data' not in d:
                    self.malformed.append((k, ty, 'missing time_ms/node/emit_type/data'))
                    continue
                self.emits.append((k, d['time_ms'], d['node'], d['emit_type'], d['data']))
            elif ty == 'node_started':
                # `script_emit` carries the node INDEX while `tx` carries the NAME, so the two must be bound.
                # `node_started` is emitted in the scenario's node order, which is the index order.
                if 'node' not in d:
                    self.malformed.append((k, ty, 'missing node'))
                    continue
                # ⓘ AUDIT (round 3): appending by count cannot OVERWRITE a mapping, but a REPEATED name would
                #   silently invent an extra index and slide the whole emit-index -> name binding. The
                #   simulator emits one `node_started` per node, so a repeat is refused.
                if d['node'] in self.node_names.values():
                    self.malformed.append((k, ty, f"duplicate node_started for {d['node']}"))
                    continue
                self.node_names[len(self.node_names)] = d['node']
            elif ty == 'sim_end':
                # ⛔⛔ THE HORIZON IS THE RUN'S END, NOT THE LAST FRAME. Using the timestamp of the final
                #    transmitted frame is SELF-FULFILLING: a MISSING late transmission moves the apparent
                #    horizon BACKWARDS, so the very thing being measured moves the yardstick that decides
                #    whether it is missing.
                if 'time_ms' not in d:
                    self.malformed.append((k, ty, 'missing time_ms'))
                    continue
                self.sim_end_ms = d['time_ms']
        # node name -> [(ordinal, end_ms, airtime)] in stream order
        self.ledger = collections.defaultdict(list)
        for (k, t, n, air, lab, flen, sf, bw, cr) in self.tx:
            self.ledger[n].append((k, t + air, air))

    @staticmethod
    def _iter_records(path):
        """THE single reader. ⛔ There is no second parser over this stream: [[B162]] was two readers
        disagreeing, and the raw-substring length scan this file used to carry was the same shape (it also made
        the result depend on JSON whitespace, which `json.loads` does not)."""
        with open(path) as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                yield json.loads(line)

    # ---- node authority ----------------------------------------------------------------
    # ⛔⛔ `S.node_names.get(idx, 'Home')` USED TO SIT IN `check_b`. Removing every `node_started` record left
    #    the verifier reporting 31/31 PASS on a stream with NO node authority at all — an unagreed silent
    #    fallback (C2), in the very binding path the previous round repaired. There is no default now.
    def resolve_node(self, idx):
        """The node NAME for an emit's index, or None. ⛔ Never a guess. ⓘ This is the ONE surviving
        `dict.get()` on a load-bearing field and it has NO default value: `None` propagates into an explicit
        refusal at every caller (`pair_refusal` returns unbound; `check_b` fails `B1c`)."""
        return self.node_names.get(idx)

    def duty_blocked_emits(self):
        """`duty_cycle_blocked` emits, split into the two shapes the firmware actually raises:
        the labelled pre-check (`tx_with_retry` / `lbt_complete`, carrying `label` + `wait_ms`) and the
        `tx_flood` one (which carries `airtime_ms` and NO label). ⓘ AUDIT (round 3): the RTS filter reads
        `d.get('label')`, which is a DISCRIMINATOR rather than a default — but an emit that lost its label
        would silently leave the denominator, so the split is asserted instead of assumed."""
        labelled, flood, malformed = [], [], []
        for e in self.emits_of('duty_cycle_blocked'):
            d = e[4]
            if 'label' in d and 'wait_ms' in d:
                labelled.append(e)
            elif d.get('source') == 'tx_flood':
                flood.append(e)
            else:
                malformed.append(e)
        return labelled, flood, malformed

    # ---- ★★★ PRICING: SHAPE-AWARE, CROSS-CHECKED, AND REFUSING RATHER THAN SUBSTITUTING ----
    # ⛔ The first version substituted "the smallest airtime observed on this PHY" for a frame length never
    #    aired, and claimed that was a safe lower bound. It was the opposite: 4 bytes price at 156 ms on this
    #    PHY and the substitute was 177 ms, so `used + airtime > budget` could be OVERSTATED.
    @staticmethod
    def est_airtime(len_bytes, sf, bw_hz, cr, preamble=16):
        """`SimRadio::getEstAirtimeFor`, transcribed. The `_cr in [5..8]` multiplier convention, the SF5/6
        6.25-symbol sync + `+36` numerator, and the integer truncation are all as in the C++."""
        t_sym = float(1 << sf) / (bw_hz / 1000.0)
        low_sf = sf in (5, 6)
        t_pre = (preamble + (6.25 if low_sf else 4.25)) * t_sym
        de = 1 if t_sym >= 16.0 else 0
        num = 8.0 * len_bytes - 4.0 * sf + (36 if low_sf else 44)
        den = 4.0 * (sf - 2 * de)
        pay_sym = 8 + int(max(math.ceil(num / den) * cr, 0.0))
        return int(t_pre + pay_sym * t_sym)

    def conflicting_observations(self):
        """Shapes for which the stream published MORE THAN ONE airtime. ⛔ A conflict is a refusal: the
        airtime function is deterministic in (len, sf, bw, cr), so two answers mean the stream cannot be
        trusted to price anything — it is NOT resolved by preferring either value."""
        return {shape: sorted(v) for shape, v in self.obs.items() if len(v) > 1}

    def pricing_crosscheck(self):
        """(agreements, disagreements) of the transcribed formula against EVERY observation, each priced with
        ITS OWN shape. ⛔ One disagreement — or one conflicting shape — invalidates pricing for this stream."""
        if self.conflicting_observations():
            return 0, len(self.obs)
        ok = bad = 0
        for (flen, sf, bw, cr), airs in sorted(self.obs.items()):
            if self.est_airtime(flen, sf, bw, cr) == next(iter(airs)):
                ok += 1
            else:
                bad += 1
        return ok, bad

    def shape_for(self, sf):
        """The single (bw_hz, cr) this stream used AT THIS SF, or None if it is not unique. A refused frame's
        record carries `len` and `sf` but no bandwidth or coding rate, so those must be ESTABLISHED from the
        stream's own transmissions at that SF — never assumed from a global."""
        pairs = {(bw, cr) for (flen, s, bw, cr) in self.obs if s == sf}
        return next(iter(pairs)) if len(pairs) == 1 else None

    def airtime_for(self, frame_len, sf):
        """The refused frame's airtime at ITS OWN SF, or None = UNPRICED.
        ★★ THE GENERAL FORM, RECORDED BECAUSE IT IS THE SHAPE OF EVERY DEFECT THIS FILE HAS BEEN REPAIRED FOR:
           **VALIDATE THE INPUT BEFORE SERVING IT, NEVER AFTER.** A conflicting observation must not be
           returnable by a path that never asks about the conflict.
        ⛔ THE ORDERING THIS FIXES (review, non-blocking — the complete gate still failed via `A1d`/`A1f`, so it
           could not go false-green): the observed value was served FIRST, so a stream in which some OTHER shape
           had conflicting observations could still hand back an exact price for a clean shape — i.e. pricing
           was invalidated stream-wide but one path never looked.
        ⇒ the order is now: (1) is the stream's pricing valid at all (no conflicting shape, formula agrees with
           every observation)? (2) can this SF's shape be established? (3) THEN serve — the observation if this
           exact shape has one unambiguous value, else the formula for that shape.
        """
        # (1) STREAM-WIDE VALIDITY FIRST — a conflict anywhere means nothing here may be priced.
        if self.conflicting_observations():
            return None
        ok, bad = self.pricing_crosscheck()
        if bad or ok == 0:
            return None
        # (2) the refused frame's record carries `len` and `sf` but no bandwidth or coding rate, so the rest of
        #     the shape must be ESTABLISHED from this stream's own transmissions at that SF — never assumed.
        shape = self.shape_for(sf)
        if shape is None:
            return None
        # (3) serve: the observation for this exact shape, else the cross-checked formula for it.
        key = (frame_len, sf) + shape
        if key in self.obs:
            airs = self.obs[key]
            if len(airs) != 1:
                return None          # unreachable while (1) holds; kept so the invariant is local, not implied
            return next(iter(airs))
        return self.est_airtime(frame_len, sf, *shape)

    # ---- ★★★ THE CAUSAL LEDGER. `before` is the STREAM ORDINAL of the event being checked, and only TX
    #      records with a STRICTLY SMALLER ordinal may enter — i.e. only airtime the node had ALREADY spent
    #      when that event was produced.
    # ⛔⛔ THE FIRST VERSION HAD NO `before` PARAMETER AND WAS THEREFORE ACAUSAL: it summed the WHOLE run's TX
    #     records and then filtered by timestamp, so a frame transmitted AFTER a refusal — or in the SAME
    #     millisecond but later in the stream — was counted as already spent. Every ledger-derived figure it
    #     published was re-derived; the one that moved is recorded in the README beside its pre-repair value.
    # ⓘ Same-millisecond correctness is exactly what the ordinal buys: `end_ms`/`now` cannot separate two
    #   records inside one millisecond, and the simulator's own ledger is a FIFO whose order is the order the
    #   frames were staged — which is the order they appear in the stream.
    def _alive(self, node, now, before, window):
        cutoff = now - window if now > window else 0
        return [(k, e, a) for (k, e, a) in self.ledger[node] if k < before and e > cutoff]

    def used(self, node, now, before, window=WINDOW):
        return sum(a for (k, e, a) in self._alive(node, now, before, window))

    def oldest(self, node, now, before, window=WINDOW):
        alive = self._alive(node, now, before, window)
        return min(e for (k, e, a) in alive) if alive else 0

    def duty_refusals(self):
        return [r for r in self.deferred if r[3] == 'duty_cycle_exceeded']

    def emits_of(self, kind):
        return [e for e in self.emits if e[3] == kind]

    def tx_at(self, node, t, label=None):
        return [x for x in self.tx if x[2] == node and x[1] == t and (label is None or x[4] == label)]

    def tx_after_ordinal(self, node, k):
        """Frames this node aired STRICTLY AFTER the event at ordinal `k`. Ordinal, not timestamp: a frame in
        the same millisecond but earlier in the stream is not a resumption of anything."""
        return [x for x in self.tx if x[2] == node and x[0] > k]


def pair_refusal(S, emit):
    """★★★ BIND A `mobile_tx_refused` EMIT TO ITS `tx_deferred` RECORD, OR REFUSE.
    ⛔⛔ THE FIRST VERSION MATCHED ONLY (t, busy_until, reason) AND NEVER COMPARED THE NODE — although the node
       was unpacked in the very same tuple. QG changed one emit's node from `MobileM` to `Peer` and the
       verifier still reported 26/26 PASS, so the check that validates B186a's entire point (the right
       operation reported on the RIGHT NODE) could not see the wrong node.
    ALL FOUR conditions now hold, and an AMBIGUOUS match is a REFUSAL, never a pick:
      1. the emit's node INDEX resolves, through `node_started` order, to the deferred record's node NAME;
      2. the deferred record's ordinal PRECEDES the emit's ordinal (the refusal happens, THEN it is reported);
      3. the millisecond, `busy_until_ms` and reason agree;
      4. the SF agrees — ⛔ QG changed one `mobile_tx_refused.sf` from 8 to 12 and the previous version still
         reported 31/31 PASS, because the candidate filter never looked at SF. It does now, and the SAME SF is
         what prices the refused frame (`airtime_for(len, sf)`), so a wrong SF cannot pass unnoticed anywhere;
      5. EXACTLY ONE deferred record satisfies 1-4 — two candidates mean the stream cannot tell them apart.
    Returns the matching record, or None (unbound: no candidate, or more than one)."""
    (ek, et, en, _kind, d) = emit
    name = S.resolve_node(en)          # ⛔ no fallback: an unresolved index is UNBOUND, never assumed
    if name is None:
        return None
    cands = [r for r in S.duty_refusals()
             if r[2] == name and r[0] < ek and r[1] == et
             and r[5] == d['busy_until_ms'] and r[3] == d['reason_name'] and r[7] == d['sf']]
    return cands[0] if len(cands) == 1 else None


def classify_defer(S, sender, t, wait, horizon, blocked_times):
    """What became of a duty-deferred RTS at its due instant. Split out so `selftest.py` can drive it on a
    synthetic stream — in particular the case that MUST be `lost`."""
    due = t + wait
    if horizon is not None and due > horizon:
        return 'past_end'
    if S.tx_at(sender, due, 'RTS'):
        return 'flew'
    if due in blocked_times:
        return 'redeferred'
    return 'lost'


def check_a(C, S):
    print(f'A {SCEN_A} — the SIMULATOR\'s asynchronous duty hard-block (the only refusal carrying busy_until)')
    C.eq('A0', 'records this reader could not validate (every field is checked or refused)',
         len(S.malformed), 0)
    C.ge('A0b', 'node authority resolved from `node_started` (no default is permitted anywhere)',
         len(S.node_names), 1)
    ref = S.duty_refusals()
    # A1 — REFUSAL AT EXHAUSTION. Not "a refusal happened": every refusal is re-derived from the CAUSAL
    # ledger (only frames aired at a SMALLER stream ordinal), so a spurious refusal — or a budget the fixture
    # never actually reached before that instant — fails.
    C.ge('A1', 'duty refusals present', len(ref), 1)
    # ★ PRICING FIRST, AND IT IS VALIDATED BEFORE IT IS USED: the transcribed `getEstAirtimeFor` must
    #   reproduce EVERY (len -> airtime) pair this stream published. A single disagreement ⇒ nothing is
    #   priced by formula and the affected refusals are reported UNPRICED.
    ok, bad = S.pricing_crosscheck()
    C.ge('A1c', 'observations (len, SF, BW, CR) the formula reproduces EXACTLY', ok, 1)
    C.eq('A1d', 'observations the formula gets WRONG (pricing is invalid if any)', bad, 0)
    C.eq('A1f', 'shapes with CONFLICTING observed airtimes (a conflict is refused, not resolved)',
         len(S.conflicting_observations()), 0)
    priced = [(k, t, n, ln, sf, S.airtime_for(ln, sf)) for (k, t, n, r, lab, bu, ln, sf) in ref]
    unpriced = [x for x in priced if x[5] is None]
    C.eq('A1e', 'refusals that could NOT be priced (reported, never substituted)', len(unpriced), 0)
    correct = sum(1 for (k, t, n, ln, sf, air) in priced
                  if air is not None and S.used(n, t, k) + air > BUDGET)
    C.eq('A1b', f'every PRICED refusal is genuine exhaustion: causal used + airtime > {BUDGET} ms budget',
         correct, len(priced) - len(unpriced))
    by_src = collections.Counter(
        'observed' if (S.shape_for(sf) and (ln, sf) + S.shape_for(sf) in S.obs) else 'formula'
        for (k, t, n, r, lab, bu, ln, sf) in ref)
    print(f'   ⓘ pricing: {ok} observations reproduced, {bad} wrong, {len(S.conflicting_observations())} '
          f'conflicting · refusals priced {dict(by_src)} · unpriced {len(unpriced)}')
    # A2 — CORRECT busy_until, for EVERY refusal, recomputed: oldest entry ALIVE AT THAT ORDINAL + the window.
    exact = sum(1 for (k, t, n, r, lab, bu, ln, sf) in ref if bu == S.oldest(n, t, k) + WINDOW)
    C.eq('A2', 'busy_until == causal oldest_in_window_tx_end + window (all refusals)', exact, len(ref))
    # A3 — INCREMENTAL ROLLING EXPIRY. Three independent facts, because "the window rolls" is a claim:
    #   (a) the FRONT of the window advances across refusals (many distinct oldest entries, not one);
    #   (b) at least one wait is a FRACTION of a window — i.e. only the oldest entry has to age out,
    #       which is what "incremental" means and what a never-rolling budget can never show;
    #   (c) the CAUSALLY reconstructed used-airtime DECREASES between two successive refusals of one node:
    #       budget is genuinely RECLAIMED, the behaviour the whole corpus is dark on.
    per_node = collections.defaultdict(list)
    for (k, t, n, r, lab, bu, ln, sf) in ref:
        per_node[n].append((k, t, bu))
    busiest = max(per_node, key=lambda x: len(per_node[x]))
    fronts = {S.oldest(n, t, k) for (k, t, n, r, lab, bu, ln, sf) in ref}
    C.ge('A3a', 'distinct window-front entries feeding the refusals', len(fronts), 3)
    partial = min(bu - t for (k, t, bu) in per_node[busiest])
    C.lt('A3b', f'smallest wait is a FRACTION of the {WINDOW} ms window', partial, WINDOW)
    series = [S.used(busiest, t, k) for (k, t, bu) in per_node[busiest]]
    drops = sum(1 for i in range(1, len(series)) if series[i] < series[i - 1])
    C.ge('A3c', f'causal used-airtime DROPS between refusals at {busiest} (budget reclaimed)', drops, 1)
    # A4 — RESUMED TRANSMISSION once budget becomes available: after the FIRST refusal of the busiest node a
    # frame really is aired, at a LATER ORDINAL and at/after the promised instant. ⛔ A refusal count alone
    # would pass on a node that fell silent forever, which is exactly the outcome this check excludes.
    k0, t0, bu0 = per_node[busiest][0]
    resumed = [x for x in S.tx_after_ordinal(busiest, k0) if x[1] >= bu0]
    C.ge('A4', f'{busiest} airs a frame at/after its first busy_until ({bu0}), later in the stream', len(resumed), 1)
    C.ge('A4b', 'that resumption is inside the following window', 1,
         1 if any(x[1] < bu0 + WINDOW for x in resumed) else 2)
    # A5 — §B186a cross-check: a mobile J refusal is now ATTRIBUTED to its operation, at the same
    # millisecond and with the same busy_until. Without B186a these refusals name only "a beacon".
    mref = S.emits_of('mobile_tx_refused')
    C.ge('A5', 'mobile_tx_refused reports present', len(mref), 1)
    ops = sorted({d['op'] for (k, t, n, kind, d) in mref})
    C.ge('A5b', 'distinct mobile operations named', len(ops), 2)
    # ⛔ ONLY duty-refused mobile emits enter the denominator: the other reasons (`self_tx_in_flight`,
    #    `channel_busy`) are reported through a `tx_deferred` too, but this pairing is written for the duty
    #    shape and must not silently claim to have bound the others.
    duty_emits = [e for e in mref if e[4]['reason_name'] == 'duty_cycle_exceeded']
    bound = [e for e in duty_emits if pair_refusal(S, e) is not None]
    C.eq('A5c', 'every duty-refused mobile op BOUND to exactly one preceding same-node tx_deferred',
         len(bound), len(duty_emits))
    # ★ And the binding really does carry the node: every bound pair names the same node twice, by two
    #   different identifiers (the emit's INDEX and the record's NAME).
    same_node = sum(1 for e in duty_emits
                    if (r := pair_refusal(S, e)) is not None and S.resolve_node(e[2]) == r[2])
    C.eq('A5d', 'every bound pair agrees on the NODE (emit index -> name == deferred node)',
         same_node, len(duty_emits))
    # ★ AND ON THE SF — the field a wrong value used to slip through, and the same field that prices the frame.
    same_sf = sum(1 for e in duty_emits
                  if (r := pair_refusal(S, e)) is not None and r[7] == e[4]['sf'])
    C.eq('A5e', 'every bound pair agrees on the SF (the value that also prices the refused frame)',
         same_sf, len(duty_emits))
    print(f'   ⓘ ops named: {ops}')


def check_b(C, S):
    print(f'B {SCEN_B} — the FIRMWARE duty PRE-CHECK, its promised wait, and the frame later FLYING')
    C.eq('B0', 'records this reader could not validate', len(S.malformed), 0)
    labelled, flood, bad_shape = S.duty_blocked_emits()
    C.eq('B0b', 'duty_cycle_blocked emits of an unrecognised shape (none may silently leave the denominator)',
         len(bad_shape), 0)
    blocked = [(k, t, n, d) for (k, t, n, kind, d) in labelled if d['label'] == 'RTS']
    C.ge('B1', 'firmware pre-check refusals (duty_cycle_blocked{RTS}) present', len(blocked), 1)
    # Bind the emit's node INDEX to a name: exactly one node originates DMs here, so exactly one index may
    # appear. Asserted rather than assumed — if a second node ever blocked, the arithmetic below would be
    # reading the wrong ledger and the check must fail loudly instead of quietly averaging two nodes.
    idxs = sorted({n for (k, t, n, d) in blocked})
    C.eq('B1b', 'exactly one node index raises the RTS pre-check refusals', len(idxs), 1)
    # ⛔⛔ NO FALLBACK. This line read `S.node_names.get(idxs[0], 'Home')`; removing every `node_started`
    #    record from the stream then left the verifier reporting 31/31 PASS on a run with NO node authority —
    #    an unagreed silent default (C2) inside the binding path. The resolution must SUCCEED or fail loudly.
    sender = S.resolve_node(idxs[0]) if len(idxs) == 1 else None
    C.eq('B1c', 'the blocking node index RESOLVES to a name (no default, no guess)', sender is not None, True)
    if sender is None:
        return
    # B2 — THE PROMISED WAIT IS RECOMPUTED FROM THE CAUSAL LEDGER, not merely present.
    # ⚠ THE TOLERANCE IS MEASURED AND ITS DIRECTION IS PINNED, which is what keeps it from being a fudge:
    #   the stream's `tx.time_ms` is the DECISION instant (`now`), while the ledger stamps
    #   `max(now, earliest_tx) + airtime` — `SimController` pushes a synthesised TX past the radio's RX->TX
    #   turnaround (`f.start_ms = (now > earliest_tx) ? now : earliest_tx`), a latency the stream does not
    #   publish. So the reconstruction is a LOWER BOUND by construction: the reported wait may exceed it by
    #   the accumulated turnaround and may NEVER be below it. A wrong formula is wrong by the window
    #   (~30 000 ms), so the band cannot launder one.
    devs = [d['wait_ms'] - (S.oldest(sender, t, k) + WINDOW - t) for (k, t, n, d) in blocked]
    C.eq('B2', 'wait_ms >= causal oldest_in_window_tx_end + window - now (all refusals; never below)',
         sum(1 for v in devs if v >= 0), len(blocked))
    C.lt('B2b', 'the whole deviation is the unpublished RX->TX turnaround, not the window',
         max(devs), 10)
    # B3 — RESUMED TRANSMISSION, AT THE PROMISED INSTANT. `rts_duty_defer_fire` re-checks the budget, so
    # the honest expectation is: at exactly `t + wait_ms` the frame EITHER flies OR is deferred again —
    # and it must fly at least once, else "resumed" would be a word rather than a measurement.
    # ⛔ A due instant PAST THE END OF THE RUN is excluded, and the count of those is asserted separately so
    #    the exclusion cannot hide a loss: the run simply ended before the timer could fire.
    # ★★ THE HORIZON IS `sim_end`, READ FROM THE STREAM — not the last transmitted frame. Asserted present,
    #    because falling back to "the last TX" is the self-fulfilling yardstick this check was repaired for.
    C.eq('B3z', 'the run horizon comes from the `sim_end` record', S.sim_end_ms is not None, True)
    horizon = S.sim_end_ms
    blocked_times = {bt for (bk, bt, bn, bd) in blocked}
    counts = collections.Counter(classify_defer(S, sender, t, d['wait_ms'], horizon, blocked_times)
                                 for (k, t, n, d) in blocked)
    flew, redeferred, lost, past_end = (counts['flew'], counts['redeferred'],
                                        counts['lost'], counts['past_end'])
    C.ge('B3', 'deferred RTS flies at EXACTLY t + wait_ms', flew, 1)
    C.eq('B3b', 'no deferred RTS vanishes at a due instant INSIDE the run', lost, 0)
    C.lt('B3c', 'the excluded past-the-end defers are a small minority', past_end, max(2, len(blocked) // 2))
    # B4 — the waits are not one constant: the window front moves, so the promised wait moves with it.
    waits = sorted({d['wait_ms'] for (k, t, n, d) in blocked})
    C.ge('B4', 'distinct wait_ms values', len(waits), 3)
    C.lt('B4b', 'shortest wait is a FRACTION of the window', min(waits), WINDOW)
    print(f'   ⓘ pre-check refusals {len(blocked)} · flew-at-due {flew} · re-deferred {redeferred} '
          f'· past-the-end {past_end} · deviation band {min(devs)}..{max(devs)} ms')


def check_c(C, S, Sb):
    print(f'C {SCEN_C} CONTROL — the ONE-SHOT regime all 36 corpus scenarios are in')
    # C1 — ZERO refusals of BOTH kinds. This is the attribution: the same load, the same topology, the
    # same commands, and the ONLY difference is the window.
    C.eq('C0', 'records this reader could not validate', len(S.malformed), 0)
    C.eq('C1', 'simulator duty refusals', len(S.duty_refusals()), 0)
    lab, fld, badsh = S.duty_blocked_emits()
    C.eq('C1b', 'firmware duty pre-check refusals (both shapes)', len(lab) + len(fld) + len(badsh), 0)
    # C2 — …AND THE LOAD REALLY RAN. A zero from a scenario that transmitted nothing measures nothing;
    # the control must carry MORE traffic than the throttled subject, not less.
    C.ge('C2', 'frames aired in the control', len(S.tx), 50)
    C.ge('C2b', 'control airs MORE than the throttled subject B', len(S.tx), len(Sb.tx) + 1)
    # C3 — the direct measurement of WHY the corpus is dark: against the inherited 1 h window the peak
    # in-window airtime never approaches the 36 000 ms allowance, so the post-exhaustion path is never
    # entered and the rolling boundary is never crossed. ★ CAUSAL: measured at each TX's own ordinal, i.e.
    # what that node had spent when it transmitted — never with the rest of the run folded in.
    peak = max((S.used(n, t, k, ONESHOT_WINDOW) + a for (k, t, n, a, l, fl, sf, bw, cr) in S.tx), default=0)
    C.ge('C3', 'peak in-window airtime measured (the instrument is alive)', peak, 1)
    C.lt('C3b', f'peak in-window airtime stays under the {ONESHOT_BUDGET} ms one-shot budget', peak, ONESHOT_BUDGET)
    print(f'   ⓘ peak 1 h-window airtime {peak} ms vs budget {ONESHOT_BUDGET} ms '
          f'({100.0 * peak / ONESHOT_BUDGET:.2f} %)')


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    d = sys.argv[1]
    mutate = None
    if '--mutate' in sys.argv:
        mutate = sys.argv[sys.argv.index('--mutate') + 1]
    C = Checker(mutate)
    streams = {}
    for name in (SCEN_A, SCEN_B, SCEN_C):
        p = os.path.join(d, name + '.ndjson')
        if not os.path.exists(p):
            print(f'MISSING STREAM {p} — run the three fixtures first (see README)')
            return 2
        streams[name] = Stream(p)
    check_a(C, streams[SCEN_A])
    check_b(C, streams[SCEN_B])
    check_c(C, streams[SCEN_C], streams[SCEN_B])
    print(f'\n{C.n} checks, {len(C.fails)} failed')
    for f in C.fails:
        print('  FAILED ' + f)
    return 1 if C.fails else 0


if __name__ == '__main__':
    sys.exit(main())
