#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""Estimate LoRa fading parameters from real firmware console logs.

Turns the node trace (`t=… ms «rx <TYPE> … sf=… snr=… rssi=…`) into the two
numbers the simulator's fading model actually consumes:

    topology.links[].snr_std_dev     sigma  (dB)  — per DIRECTED link
    simulation.radio.snr_coherence_ms  tau  (ms)  — the OU correlation time

The model (lora-universal-simulator core/link/LinkFadingState.cpp) is a
stationary Ornstein-Uhlenbeck process:

    offset <- alpha*offset + sqrt(1-alpha^2)*sigma*N(0,1),  alpha = exp(-dt/tau)

so sigma is the standard deviation of the SNR fluctuation about a link's mean,
and tau is recovered from the lag autocorrelation:  tau = -dt / ln(rho(dt)).
If rho is ~0 at every lag the traffic samples, then `snr_coherence_ms = 0`
(the i.i.d. branch) is the CORRECT answer, not a failure — each packet simply
sees an independent draw at that cadence.

★ WHAT THIS TOOL IS FOR, AND WHAT IT IS NOT
It measures RF/channel statistics only. It draws no protocol conclusions: the
logs it reads may come from older firmware whose protocol behaviour has since
changed, so retry loops, unregistered mobiles, delivery gaps etc. are NOT
interpreted here. Anomalies are reported only where they affect measurement
validity (missing samples bias sigma downward).

USAGE
    python3 tools/fading_from_logs.py dumps/                # human report
    python3 tools/fading_from_logs.py dumps/43.log dumps/10.log
    python3 tools/fading_from_logs.py dumps/ --json
    python3 tools/fading_from_logs.py dumps/ --links        # sim config patch
    python3 tools/fading_from_logs.py dumps/ --min-samples 50 --clip-db 10

ONE FILE PER NODE, NAMED BY NODE ID (`43.log`, `node43.log`, `n43_run2.log`).
The receiver is implicit in the trace — it is whichever node produced the log —
so the id comes from the filename. `to=` on addressed frames is used as a
cross-check and disagreements are reported.

★ WHY FILES MUST NOT BE CONCATENATED: `t=` is each node's own millis(), so
different files sit on different epochs. Series are therefore built strictly
WITHIN a file; samples are never correlated across files.

MEASUREMENT LIMITS baked in as guards (each is reported, never silent):
  * SATURATION — LoRa PktSnr reporting is only trustworthy to roughly +10 dB
    (the simulator models a +12 dB report ceiling). Samples at/above --clip-db
    are excluded from sigma and counted; a series dominated by them cannot
    measure fading at all and is flagged UNUSABLE.
  * QUANTIZATION — snr rides a 0.25 dB grid (int8 quarter-dB register), printed
    to 1 dp. sigma below ~0.15 dB is not resolvable and is flagged.
  * CENSORING — deep fades LOSE packets, so those samples are absent and sigma
    comes out biased LOW. For periodic series the tool detects the cadence and
    counts missing slots, which bounds the bias.
  * SILENT LOG DROPS — the firmware console sink drops a whole line when the USB
    FIFO is full (src/console_sink.h) and claims success, so a missing sample can
    be a dropped LOG LINE rather than a lost frame. The two are indistinguishable
    here; gap counts cover both and must be read as an upper bound on frame loss.
  * MIXED SF — control and data frames run different spreading factors. Series
    are keyed by SF and never pooled across it.
  * STATIC vs MOVING — fading needs something to move. A static bench link
    legitimately shows sigma of a few tenths of a dB; meaningful sigma comes from
    motion. Pass --moving to label which receivers were in motion so the report
    separates the two regimes.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import sys
from collections import defaultdict

# ---------------------------------------------------------------------------
# Line parsing
# ---------------------------------------------------------------------------
# Deliberately field-NAME based, not positional: these dumps can come from older
# firmware whose trace layout differed. Anything unrecognised is counted and
# reported rather than skipped silently.
#
# The rx/tx discriminator is the presence of `snr=`: frame_trace.h prints snr and
# rssi only when is_rx (lib/core/frame_trace.h — "»tx … no snr/rssi"). The
# negative lookbehind keeps `snr_q4=` telemetry from matching.
RE_T = re.compile(r"\bt=(\d+)\s*ms")
RE_SNR = re.compile(r"(?<![_A-Za-z])snr=(-?\d+(?:\.\d+)?)")
RE_RSSI = re.compile(r"(?<![_A-Za-z])rssi=(-?\d+(?:\.\d+)?)")
RE_SF = re.compile(r"(?<![_A-Za-z])sf=(\d+)")
RE_FROM = re.compile(r"(?<![_A-Za-z])from=(\d+)")
RE_HASH = re.compile(r"(?<![_A-Za-z])hash=([0-9A-Fa-f]{4,8})")
RE_TO = re.compile(r"(?<![_A-Za-z])to=(\d+)")
# Frame type: the token right after the rx/tx marker. Marker glyphs have varied
# across firmware revisions, so match the type name directly.
RE_TYPE = re.compile(r"(?:«|<<|\brx\b)\s*(?:rx\s+)?([A-Z][A-Z0-9_]*)")
RE_TX_TYPE = re.compile(r"(?:»|>>|\btx\b)\s*(?:tx\s+)?([A-Z][A-Z0-9_]*)")
RE_NODE_ID = re.compile(r"(\d+)")

# Frame types that carry an explicit sender in the trace (frame_trace.h:56/65/69
# print from= for BCN/RTS/CTS). DATA and ACK do NOT — they need handshake state.
SELF_IDENTIFYING = {"BCN", "RTS", "CTS"}
# How long a handshake attribution stays valid. An exchange is RTS->CTS->DATA->ACK
# within a few hundred ms; well beyond that the pairing is not trustworthy, so the
# sample is marked unattributed instead of guessed (never mis-attribute silently).
HANDSHAKE_TTL_MS = 5000


class Sample:
    __slots__ = ("t_ms", "snr", "rssi", "sf", "sender", "ftype", "attributed_by")

    def __init__(self, t_ms, snr, rssi, sf, sender, ftype, attributed_by):
        self.t_ms = t_ms
        self.snr = snr
        self.rssi = rssi
        self.sf = sf
        self.sender = sender
        self.ftype = ftype
        self.attributed_by = attributed_by


RE_WHOAMI = re.compile(r"\[whoami\]\s*id=(\d+)")
RE_RESPONDER = re.compile(r"(?<![_A-Za-z])responder=(\d+)")


def receiver_from_filename(path):
    """Node id from the filename ('43.log', 'node43.log', 'n43_run2.log').

    Last resort only: real captures are named by role ('M1-1.txt', 'N1-2.txt'),
    where the digits are a label index, NOT a node id — and worse, several files
    can share one ('M1-1' and 'N1-1' both yield 1). identify_receiver() below
    prefers on-air evidence and only falls back here.
    """
    stem = os.path.splitext(os.path.basename(path))[0]
    m = RE_NODE_ID.search(stem)
    return int(m.group(1)) if m else None


def identify_receiver(path):
    """Infer whose log this is from the node's OWN transmissions.

    Returns (node_id, how, notes). Priority, most trustworthy first:

      1. `»tx BCN/RTS/CTS from=<id>` — the src field of a frame THIS node sent, i.e.
         the identity its peers actually address. Modal value wins.
      2. `»tx J … responder=<id>` — the OFFER's responder is the offering node.
      3. `[whoami] id=<id>`       — authoritative for the node's CONFIGURED id, but
         a mobile transmits under a home-leased local id, so it can legitimately
         differ from (1). Recorded as a note when it does; (1) still wins because
         link attribution must use the on-air identity.
      4. the filename.

    ★ `origin=` is NEVER used: H frames preserve the ORIGINATOR's id across
    forwards, so a relay's own `»tx H origin=58` says nothing about the relay.
    Using it mislabels the file (N1-1.txt shows origin=58 54x but is node 39).
    """
    from_votes, resp_votes, whoami = defaultdict(int), defaultdict(int), None
    with open(path, "r", errors="replace") as fh:
        for line in fh:
            m_w = RE_WHOAMI.search(line)
            if m_w:
                whoami = int(m_w.group(1))
            if RE_SNR.search(line):
                continue                     # an rx line: not self-identifying
            m_tx = RE_TX_TYPE.search(line)
            if not m_tx:
                continue
            if m_tx.group(1) in ("RTS", "CTS", "BCN"):
                m_f = RE_FROM.search(line)
                if m_f:
                    from_votes[int(m_f.group(1))] += 1
            m_r = RE_RESPONDER.search(line)
            if m_r:
                resp_votes[int(m_r.group(1))] += 1

    notes = []
    node = how = None
    if from_votes:
        node, n = max(from_votes.items(), key=lambda kv: kv[1])
        how = f"tx-from (x{n})"
        if len(from_votes) > 1:
            notes.append("multiple tx from= ids: " + ", ".join(
                f"{k}x{v}" for k, v in sorted(from_votes.items(), key=lambda kv: -kv[1])))
    elif resp_votes:
        node, n = max(resp_votes.items(), key=lambda kv: kv[1])
        how = f"tx-responder (x{n})"
    elif whoami is not None:
        node, how = whoami, "whoami"
    else:
        node, how = receiver_from_filename(path), "filename(WEAK)"

    if whoami is not None and node is not None and whoami != node:
        notes.append(f"[whoami] id={whoami} differs from the on-air id {node} "
                     f"(normal for a mobile: it transmits under a home-leased local id)")
    if resp_votes and node is not None:
        top_r = max(resp_votes.items(), key=lambda kv: kv[1])[0]
        if top_r != node:
            notes.append(f"responder={top_r} disagrees with the on-air id {node}")
    return node, how, notes


def parse_file(path):
    """Stream one node's log -> (samples, stats). Never loads the file whole."""
    st = {
        "lines": 0, "rx_lines": 0, "samples": 0,
        "no_time": 0, "no_sf": 0, "unattributed": 0,
        "to_self": 0, "to_seen": 0,
    }
    samples = []
    # Handshake state for attributing DATA/ACK, which carry no from=.
    pend_data_from = None   # we sent CTS to X => the next DATA is from X
    pend_data_at = 0
    pend_ack_from = None    # we sent DATA to X => the next ACK is from X
    pend_ack_at = 0

    rcv_id, rcv_how, rcv_notes = identify_receiver(path)
    st["rcv_how"], st["rcv_notes"] = rcv_how, rcv_notes

    with open(path, "r", errors="replace") as fh:
        for line in fh:
            st["lines"] += 1

            m_snr = RE_SNR.search(line)
            if not m_snr:
                # A tx line: mine it only for handshake state.
                m_tx = RE_TX_TYPE.search(line)
                if m_tx:
                    ttype = m_tx.group(1)
                    m_to = RE_TO.search(line)
                    m_t = RE_T.search(line)
                    tt = int(m_t.group(1)) if m_t else 0
                    if m_to:
                        if ttype == "CTS":
                            pend_data_from, pend_data_at = int(m_to.group(1)), tt
                        elif ttype == "DATA":
                            pend_ack_from, pend_ack_at = int(m_to.group(1)), tt
                continue

            st["rx_lines"] += 1
            m_t = RE_T.search(line)
            if not m_t:
                st["no_time"] += 1
                continue
            t_ms = int(m_t.group(1))

            m_sf = RE_SF.search(line)
            if not m_sf:
                st["no_sf"] += 1
                continue
            sf = int(m_sf.group(1))

            snr = float(m_snr.group(1))
            m_rssi = RE_RSSI.search(line)
            rssi = float(m_rssi.group(1)) if m_rssi else None

            m_type = RE_TYPE.search(line)
            ftype = m_type.group(1) if m_type else "?"

            # Cross-check the receiver id against `to=` on addressed frames.
            m_to = RE_TO.search(line)
            if m_to and rcv_id is not None and ftype not in ("BCN",):
                st["to_seen"] += 1
                if int(m_to.group(1)) == rcv_id:
                    st["to_self"] += 1

            # --- sender attribution, in order of trustworthiness ---
            sender, how = None, None
            m_from = RE_FROM.search(line)
            m_hash = RE_HASH.search(line)
            if m_from:
                sender, how = int(m_from.group(1)), "from"
            elif m_hash:
                # A mobile's key hash is a MORE stable key than its short id,
                # which is reassigned on (re-)registration. Kept as its own
                # namespace: we cannot prove hash X and short id Y are one node.
                sender, how = "h:" + m_hash.group(1).upper(), "hash"
            elif ftype == "DATA" and pend_data_from is not None \
                    and t_ms - pend_data_at <= HANDSHAKE_TTL_MS:
                sender, how = pend_data_from, "handshake"
            elif ftype == "ACK" and pend_ack_from is not None \
                    and t_ms - pend_ack_at <= HANDSHAKE_TTL_MS:
                sender, how = pend_ack_from, "handshake"

            if sender is None:
                st["unattributed"] += 1
                continue

            samples.append(Sample(t_ms, snr, rssi, sf, sender, ftype, how))
            st["samples"] += 1

    return samples, st, rcv_id


# ---------------------------------------------------------------------------
# Statistics
# ---------------------------------------------------------------------------
# Log-spaced lag buckets (seconds). Chosen to straddle the cadences real traffic
# actually provides: ~0.2 s CTS/ACK inside one exchange, seconds between
# exchanges, ~120 s mobile DISCOVER retries, ~300 s beacon periods.
LAG_EDGES_S = [0.05, 0.15, 0.5, 1.5, 5, 15, 60, 180, 600]
QUANT_DB = 0.25            # snr register grid
SIGMA_RESOLUTION_DB = 0.15  # below this, sigma is quantization noise


def mean_std(xs):
    n = len(xs)
    if n < 2:
        return (xs[0] if xs else None), None
    mu = sum(xs) / n
    var = sum((x - mu) ** 2 for x in xs) / (n - 1)
    return mu, math.sqrt(var)


def autocorr_by_lag(samples, mu, sigma, max_lag_s):
    """Lag-binned autocorrelation over a forward window.

    Only pairs within max_lag are considered, so cost is ~O(n * pairs-in-window)
    rather than O(n^2) — a multi-hour capture stays cheap.
    """
    if sigma is None or sigma <= 0:
        return {}
    # Per bucket: [sum of products, pair count, sum of ACTUAL lags]. The actual-lag
    # sum matters for honesty: a bucket labelled by its edge (e.g. "600s") can hold
    # pairs at 244/366/488 s, so the edge is not the lag that was measured. Report
    # the mean real lag, and feed THAT to the tau fit.
    acc = defaultdict(lambda: [0.0, 0, 0.0])
    max_lag_ms = max_lag_s * 1000.0
    n = len(samples)
    for i in range(n):
        ti, xi = samples[i].t_ms, samples[i].snr - mu
        for j in range(i + 1, n):
            dt = samples[j].t_ms - ti
            if dt > max_lag_ms:
                break
            dt_s = dt / 1000.0
            for e in LAG_EDGES_S:
                if dt_s <= e:
                    slot = acc[e]
                    slot[0] += xi * (samples[j].snr - mu)
                    slot[1] += 1
                    slot[2] += dt_s
                    break
    out = {}
    for e, (s, c, dtsum) in acc.items():
        # 3 pairs is already thin; the count is always reported so the reader can
        # judge. (An earlier cut required 5 and thereby DISCARDED the most
        # informative bucket on a short series while keeping a coarser one.)
        if c >= 3:
            out[e] = (s / c / (sigma * sigma), c, dtsum / c)
    return out


def fit_tau_ms(rho_by_lag):
    """Least-squares fit of ln(rho) vs lag -> tau, or None when the data cannot
    support one.

    ★ DELIBERATELY CONSERVATIVE. A lag-binned estimator on a short, unevenly
    sampled series produces plenty of spurious structure: rho values that exceed
    1 (impossible for a true autocorrelation — an artefact of normalising a
    high-local-variance subset by the global sigma), and noise-level rho at long
    lags that a naive fit turns into a confident multi-hundred-second tau. Both
    were observed on the first real captures. So a tau is reported ONLY when
    there is genuine, well-sampled short-lag correlation to decay FROM:

      * every bucket used must have |rho| <= 1 and at least MIN_PAIRS pairs;
      * the shortest usable bucket must show rho >= RHO_REAL (real correlation,
        not noise);
      * at least two such buckets must exist, and the fitted slope must be
        negative (decaying).

    Otherwise: None, which the report prints as "~0 (iid)" — and at a cadence
    where no correlation survives, coherence 0 is the CORRECT model setting.
    """
    # >=3 points, so a single strong short-lag bucket cannot be fitted against one
    # noise-level long-lag bucket (that produced a spurious "tau=279 s" from
    # rho=0.99@0.12s vs rho=0.14@538s on the first real capture), and the points
    # must actually DECAY rather than merely differ.
    MIN_PAIRS, RHO_REAL, MIN_PTS = 10, 0.30, 3
    pts = [(mean_dt, r) for _e, (r, c, mean_dt) in sorted(rho_by_lag.items())
           if c >= MIN_PAIRS and -1.0 <= r <= 1.0 and r > 0.05]
    if len(pts) < MIN_PTS or pts[0][1] < RHO_REAL:
        return None
    if pts[-1][1] >= pts[0][1]:            # no net decay across the span
        return None
    sx = sy = sxx = sxy = 0.0
    for dt_s, r in pts:
        x, y = dt_s * 1000.0, math.log(r)
        sx += x; sy += y; sxx += x * x; sxy += x * y
    n = len(pts)
    denom = n * sxx - sx * sx
    if abs(denom) < 1e-9:
        return None
    slope = (n * sxy - sx * sy) / denom
    if slope >= 0:
        return None
    return -1.0 / slope


def cadence_gaps(samples):
    """Modal inter-sample spacing + missing-slot count for periodic series.

    A regular series (mobile DISCOVER retries, beacons) makes CENSORING visible:
    a missing slot is either a lost frame (a deep fade — exactly the tail that
    biases sigma low) or a dropped log line. Reported as an upper bound.
    """
    if len(samples) < 4:
        return None, 0, 0
    deltas = [samples[i + 1].t_ms - samples[i].t_ms for i in range(len(samples) - 1)]
    # Mode on a 1 s grid: robust against ms jitter without needing a histogram fit.
    buckets = defaultdict(int)
    for d in deltas:
        buckets[round(d / 1000.0)] += 1
    modal_s, modal_n = max(buckets.items(), key=lambda kv: kv[1])
    # Require the modal spacing to dominate (>=60% of gaps), else this is bursty
    # traffic with an incidental mode rather than a periodic probe.
    if modal_s <= 0 or modal_n < 0.60 * len(deltas):
        return None, 0, 0
    period_ms = modal_s * 1000.0
    # ★ Count only SHORT runs of missed slots (k<=MAX_RUN). A gap of thousands of
    # periods is a capture discontinuity (console drop burst, node reset, operator
    # stopping the log), NOT a censored deep fade — extrapolating across it once
    # produced "16062 missing slots" on a 32-minute series.
    MAX_RUN = 5
    missing = discont = 0
    for d in deltas:
        k = round(d / period_ms)
        if 2 <= k <= MAX_RUN:
            missing += k - 1
        elif k > MAX_RUN:
            discont += 1
    if discont:
        missing = -missing - 1 if False else missing   # keep missing as fades only
    return period_ms, missing, modal_n


def analyse(samples, clip_db, max_lag_s):
    """Group into (sender, sf) series and compute per-series statistics."""
    series = defaultdict(list)
    for s in samples:
        series[(s.sender, s.sf)].append(s)

    results = []
    for (sender, sf), ss in series.items():
        ss.sort(key=lambda x: x.t_ms)
        clipped = [x for x in ss if x.snr >= clip_db]
        usable = [x for x in ss if x.snr < clip_db]
        snrs = [x.snr for x in usable]
        mu, sigma = mean_std(snrs) if snrs else (None, None)
        rho = autocorr_by_lag(usable, mu, sigma, max_lag_s) if sigma else {}
        period_ms, missing, modal_n = cadence_gaps(ss)
        rssis = [x.rssi for x in usable if x.rssi is not None]
        rssi_mu, _ = mean_std(rssis) if rssis else (None, None)
        # Implied noise floor: N = RSSI - SNR. A cross-check only — on SX126x the
        # two come from different estimators, so treat drift here as a caution
        # flag rather than a measurement.
        noise = [x.rssi - x.snr for x in usable if x.rssi is not None]
        noise_mu, noise_sd = mean_std(noise) if len(noise) > 1 else (None, None)

        flags = []
        if not usable:
            flags.append("UNUSABLE:all-clipped")
        elif len(clipped) > len(ss) * 0.25:
            flags.append(f"SATURATED:{len(clipped)}/{len(ss)}-clipped")
        if sigma is not None and sigma < SIGMA_RESOLUTION_DB:
            flags.append("sigma<quantization")
        if missing:
            flags.append(f"censored:{missing}-missing-slots")
        if noise_sd is not None and noise_sd > 3.0:
            flags.append(f"noise-floor-drift:{noise_sd:.1f}dB")
        bad_rho = sum(1 for v in rho.values() if abs(v[0]) > 1.0)
        if bad_rho:
            flags.append(f"rho|>1-in-{bad_rho}-buckets(estimator-noise)")
        by_handshake = sum(1 for x in usable if x.attributed_by == "handshake")
        if by_handshake:
            flags.append(f"handshake-attributed:{by_handshake}")

        results.append({
            "sender": sender, "sf": sf,
            "n": len(ss), "n_usable": len(usable), "n_clipped": len(clipped),
            "snr_mean": mu, "snr_sigma": sigma,
            "snr_min": min(snrs) if snrs else None,
            "snr_max": max(snrs) if snrs else None,
            # Relative standard error of a sigma estimate from n samples.
            "sigma_rel_err": (1.0 / math.sqrt(2 * (len(snrs) - 1))) if len(snrs) > 1 else None,
            "rssi_mean": rssi_mu,
            "noise_floor_mean": noise_mu, "noise_floor_sd": noise_sd,
            "rho": {f"{v[2]:.3g}s": {"rho": round(v[0], 4), "pairs": v[1],
                                     "mean_lag_s": round(v[2], 3)}
                    for _k, v in sorted(rho.items())},
            "tau_ms": fit_tau_ms(rho),
            "cadence_ms": period_ms, "missing_slots": missing,
            "frame_types": sorted({x.ftype for x in ss}),
            "flags": flags,
        })
    results.sort(key=lambda r: (-(r["n_usable"] or 0), str(r["sender"])))
    return results


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------
def report_human(per_file, min_samples, clip_db, moving):
    any_usable = False
    for path, (rcv, results, st) in per_file.items():
        label = f"{os.path.basename(path)}  (receiver {rcv if rcv is not None else '?'}"
        if rcv is not None and moving and rcv in moving:
            label += ", MOVING"
        elif rcv is not None and moving:
            label += ", static"
        label += ")"
        print("=" * 100)
        print(label)
        print(f"  lines={st['lines']}  rx-with-snr={st['rx_lines']}  samples={st['samples']}"
              f"  unattributed={st['unattributed']}  no-sf={st['no_sf']}  no-time={st['no_time']}")
        # The trace is promiscuous, so to=self on only SOME frames is normal.
        # Zero is the real alarm: it means this file is probably not this node.
        if st["to_seen"]:
            frac = st["to_self"] / st["to_seen"]
            if st["to_self"] == 0:
                print(f"  !! NONE of {st['to_seen']} addressed frames were to={rcv}"
                      f" — receiver id is probably WRONG")
            else:
                print(f"  addressed-to-self: {st['to_self']}/{st['to_seen']}"
                      f" ({100*frac:.0f}%; the rest are overheard — the trace is promiscuous)")
        if st["samples"] == 0:
            print("  !! NO SAMPLES EXTRACTED. Either the trace format differs (older firmware)")
            print("     or this log has no rx lines. Nothing below is measurable.")
            continue
        print()
        hdr = (f"  {'sender':<12} {'sf':>3} {'n':>6} {'clip':>5} {'mean':>7} {'sigma':>7}"
               f" {'+-%':>5} {'tau':>10} {'cadence':>9}  flags")
        print(hdr)
        print("  " + "-" * (len(hdr) - 2))
        suppressed = []
        for r in results:
            if (r["n_usable"] or 0) < min_samples:
                # ★ FAIL LOUD: never let a series vanish without a reason. An
                # all-clipped series is the single most important thing this tool
                # can tell you (raise attenuation), so it must not be filtered
                # into silence by a sample-count threshold.
                if r["n_clipped"] and not r["n_usable"]:
                    why = f"ALL {r['n_clipped']} samples clipped (>= {clip_db:g} dB) — saturated"
                elif r["n_clipped"]:
                    why = (f"only {r['n_usable']} usable of {r['n']} "
                           f"({r['n_clipped']} clipped >= {clip_db:g} dB)")
                else:
                    why = f"only {r['n_usable']} samples (need {min_samples})"
                suppressed.append((r["sender"], r["sf"], why))
                continue
            any_usable = True
            tau = f"{r['tau_ms']/1000:.1f}s" if r["tau_ms"] else "~0 (iid)"
            cad = f"{r['cadence_ms']/1000:.0f}s" if r["cadence_ms"] else "-"
            sig = f"{r['snr_sigma']:.2f}" if r["snr_sigma"] is not None else "-"
            rel = f"{100*r['sigma_rel_err']:.0f}" if r["sigma_rel_err"] else "-"
            mu = f"{r['snr_mean']:.2f}" if r["snr_mean"] is not None else "-"
            print(f"  {str(r['sender']):<12} {r['sf']:>3} {r['n_usable']:>6} {r['n_clipped']:>5}"
                  f" {mu:>7} {sig:>7} {rel:>5} {tau:>10} {cad:>9}  {' '.join(r['flags'])}")
            if r["rho"]:
                bits = [f"{k}:{v['rho']:+.2f}(n={v['pairs']})" for k, v in r["rho"].items()]
                print(f"       rho by mean lag: {'  '.join(bits)}")
        if suppressed:
            print(f"  not reported ({len(suppressed)} series):")
            for sender, sf, why in suppressed:
                print(f"       {str(sender):<12} sf={sf}   {why}")
        print()

    print("=" * 100)
    if not any_usable:
        print(f"!! NOTHING met --min-samples {min_samples}. Either the capture is short, the")
        print("   links are saturated (raise attenuation), or the trace format is unrecognised.")
        return
    print("HOW TO READ THIS")
    print("  sigma  -> topology.links[].snr_std_dev for that DIRECTED link (dB).")
    print("  +-%    -> relative standard error of sigma. >20% means too few samples to trust.")
    print("  tau    -> simulation.radio.snr_coherence_ms. '~0 (iid)' means the autocorrelation")
    print("            shows no decay at the lags this traffic samples, so coherence 0 is the")
    print("            CORRECT setting at that cadence — not a measurement failure.")
    print("  clip   -> samples at/above the +{:.0f} dB saturation guard, excluded from sigma."
          .format(clip_db))
    print("  ★ sigma transfers to ANY scenario. The mean only transfers to scenarios with")
    print("    explicit per-link `snr`; position-based topologies derive their mean from the")
    print("    path-loss model instead.")
    print("  ★ A static link legitimately has sigma of a few tenths of a dB. Use --moving to")
    print("    label receivers in motion; the motion regime is the one worth modelling.")


def report_links(per_file, min_samples):
    """Emit a proposed sim topology patch: one directed link per usable series."""
    links, taus = [], []
    for path, (rcv, results, st) in per_file.items():
        if rcv is None:
            continue
        for r in results:
            if (r["n_usable"] or 0) < min_samples or r["snr_sigma"] is None:
                continue
            if any(f.startswith("UNUSABLE") for f in r["flags"]):
                continue
            links.append({
                "from": r["sender"], "to": rcv,
                "snr": round(r["snr_mean"], 2),
                "snr_std_dev": round(r["snr_sigma"], 2),
                "_sf": r["sf"], "_n": r["n_usable"],
                "_sigma_rel_err_pct": round(100 * r["sigma_rel_err"], 1) if r["sigma_rel_err"] else None,
                "_flags": r["flags"],
            })
            if r["tau_ms"]:
                taus.append(r["tau_ms"])
    out = {
        "_comment": ("MEASURED from firmware logs by tools/fading_from_logs.py. Directed links: "
                     "`from` is the transmitter, `to` the receiving node whose log was parsed. "
                     "Underscore fields are provenance, not sim config — strip before use. "
                     "snr transfers only to scenarios with explicit per-link snr."),
        "suggested_snr_coherence_ms": round(sum(taus) / len(taus)) if taus else 0,
        "_coherence_note": ("0 = the i.i.d. branch: no autocorrelation decay was observed at the "
                           "lags this traffic samples, which is the correct setting at that cadence."
                           if not taus else f"mean of {len(taus)} per-series fits"),
        "links": links,
    }
    print(json.dumps(out, indent=2))


def main():
    ap = argparse.ArgumentParser(
        description="Estimate LoRa fading parameters (sigma, tau) from firmware console logs.")
    ap.add_argument("paths", nargs="+", help="log files, or a directory of them (one per node)")
    ap.add_argument("--clip-db", type=float, default=10.0,
                    help="exclude samples at/above this SNR as saturated (default 10; LoRa "
                         "PktSnr reporting is unreliable above ~+10 dB and the sim models +12)")
    ap.add_argument("--min-samples", type=int, default=20,
                    help="minimum usable samples before a series is reported (default 20)")
    ap.add_argument("--max-lag-s", type=float, default=600.0,
                    help="longest lag considered for autocorrelation (default 600 s)")
    ap.add_argument("--moving", default="",
                    help="comma-separated receiver node ids that were IN MOTION during capture")
    ap.add_argument("--json", action="store_true", help="machine-readable full output")
    ap.add_argument("--links", action="store_true",
                    help="emit a proposed sim topology.links[] patch")
    args = ap.parse_args()

    files = []
    for p in args.paths:
        if os.path.isdir(p):
            files += [os.path.join(p, f) for f in sorted(os.listdir(p))
                      if not f.startswith(".") and os.path.isfile(os.path.join(p, f))]
        else:
            files.append(p)
    if not files:
        print("no input files found", file=sys.stderr)
        return 2

    moving = {int(x) for x in args.moving.split(",") if x.strip().isdigit()}

    per_file = {}
    for path in files:
        samples, st, rcv = parse_file(path)
        per_file[path] = (rcv, analyse(samples, args.clip_db, args.max_lag_s), st)

    if args.links:
        report_links(per_file, args.min_samples)
    elif args.json:
        print(json.dumps({
            os.path.basename(p): {"receiver": rcv, "parse_stats": st, "series": res}
            for p, (rcv, res, st) in per_file.items()
        }, indent=2))
    else:
        report_human(per_file, args.min_samples, args.clip_db, moving)
    return 0


if __name__ == "__main__":
    sys.exit(main())
