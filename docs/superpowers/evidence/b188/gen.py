#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""B188 — the ROLLING DUTY WINDOW's boundary behaviour, made reachable.

⛔⛔ NON-CORPUS DIAGNOSTIC FIXTURE. These scenarios must NEVER be added to `simulation/`, the 36-row
runner, `BASELINE.md`'s anchor table, or any delivery-floor computation. They exist to exercise a
behaviour the corpus is measurably dark on, and nothing here re-tunes any corpus scenario.

WHY IT EXISTS (measured 2026-08-12): 36 of 36 corpus scenarios are in the ONE-SHOT duty regime — not one
sets `duty_cycle_window_ms`, all inherit the 1 h default, and every `duration_ms` is <= 1 h. A budget that
never rolls is entered once and never left, so the moment a rolling window RECLAIMS budget is untested
corpus-wide. The method used here is an EXPLICITLY COMPRESSED `duty_cycle_window_ms`: no corpus scenario's
window is shortened and no scenario is lengthened.

THE ARITHMETIC, stated so the verifier is checking a number and not a vibe:
  `simulation.radio.duty_cycle` = 1 PERCENT and `simulation.radio.duty_cycle_window_ms` = WINDOW_MS
  ⇒ budget = 0.01 * WINDOW_MS. At WINDOW_MS = 30 000 that is a 300 ms allowance per rolling 30 s.
  ⇒ over a 600 000 ms run the window rolls 20 times, so budget RECLAMATION happens repeatedly.
  ⓘ The global key drives BOTH sides: `SimController` injects `_sim_duty_cycle_window_ms` into every node
    config, which `NodeRuntimeWrapper::map_duty_cycle_window_ms` maps onto `NodeConfig::duty_cycle_window_ms`
    — the value the FIRMWARE's own `duty_over_budget()` pre-check uses. So the firmware and the simulator
    enforce the same window, exactly as they do at the 1 h default.

THE THREE SCENARIOS, one job each (the b183 pattern: a subject per mechanism plus a control):
  A `b188_a_rolling_mobile` — the SIMULATOR's ASYNCHRONOUS duty hard-block, which is the only refusal that
    carries `busy_until_ms`. It is reached by a mobile J frame (DISCOVER / OFFER / CLAIM / re-CLAIM):
    those are `FrameTag::beacon`, so `retry_slot_of` is -1 and `tx_with_retry`'s duty PRE-CHECK (gated on
    `slot >= 0`) does not apply to them — the modem is asked for airtime the firmware never pre-checked.
    ⇒ refusal at exhaustion + `busy_until` exactness + incremental rolling expiry.
  B `b188_b_rolling_data` — the FIRMWARE's own duty PRE-CHECK on a DM (`duty_cycle_blocked{wait_ms}`) and
    the `kDutyDeferTimerId` re-run that later AIRS the deferred frame. ⇒ resumed transmission once budget
    becomes available, with the wait it was promised.
  C `b188_c_control_oneshot` — B with `duty_cycle_window_ms` LEFT AT THE 1 h DEFAULT and NOTHING else
    changed. This is the corpus regime reproduced deliberately: the same load must produce ZERO duty
    refusals, which is what proves A/B's refusals come from the COMPRESSED WINDOW and not from the load.

Regenerate: python3 gen.py [outdir]   (default: this directory)
"""
import json, copy, os, sys

WINDOW_MS = 30000          # the compressed rolling window (A and B); C omits the key entirely
DUR       = 600000         # 20 compressed windows
DM_EVERY  = 15000
DM_FIRST  = 60000

HOME_KEY = "0x33333333"
PEER_KEY = "0x22222222"
MOB_KEY  = "0xe27bc270"


def base(desc):
    return {
        "_name": "b188",
        "_desc": desc,
        "simulation": {
            "duration_ms": DUR, "step_ms": 1, "warmup_ms": 0,
            "beacon_period_ms": 20000, "seed": 188, "node_startup_jitter_ms": 0,
            # duty_cycle is a PERCENT (1 = 1%). duty_cycle_window_ms is added per scenario below.
            "radio": {"sf": 8, "bw": 62.5, "cr": 5, "max_packet_bytes": 255,
                      "snr_coherence_ms": 0, "duty_cycle": 1},
            # Deterministic propagation: every sigma is 0 so a re-run reproduces byte-for-byte and the
            # only variable between the subjects and the control is the duty window.
            "path_loss": {"model": "log_distance", "mobile_only": True, "alpha": 3.0, "sigma_db": 0.0,
                          "ref_distance_m": 1.0, "ref_loss_db": 40.0, "noise_floor_db": -120.0,
                          "tx_power_dbm": 14.0, "node_tx_offset_sigma_db": 0.0,
                          "node_rx_offset_sigma_db": 0.0, "asymmetry_coherence_ms": 60000},
        },
        "commands": [],
        "expect": [],
        "nodes": [],
        "topology": {"links": []},
    }


def static_node(name, node_id, key, lat, lon):
    return {"name": name, "script": "scenarios/dv_dual_sf.lua", "lat": lat, "lon": lon,
            "start_at_ms": 1000, "node_id": node_id, "key_hash32": key,
            "config": {"routing_sf": 8, "allowed_data_sfs": [7, 9, 10], "beacon_period_ms": 20000}}


def mobile_node(name, node_id, key, lat, lon):
    n = static_node(name, node_id, key, lat, lon)
    # velocity_mps > 0 is MANDATORY: SimController::isMobileNode() tests exactly that, and a "mobile" at
    # 0.0 is treated as static — its links fall back to the (empty) topology table and it hears nothing.
    # (That trap cost the B183 fixture a failed positive control; recorded there, not re-learned here.)
    n["velocity_mps"] = 0.01
    n["direction_deg"] = 0.0
    n["config"].update({"is_mobile": True, "discovery_min_routes": 1, "req_sync_min_routes": 1})
    return n


def link(a, b):
    return [{"from": a, "to": b, "snr": 38.0, "rssi": -82.0, "snr_std_dev": 0, "bidir": True}]


def dm_stream(sender):
    at = DM_FIRST
    out = []
    while at < DUR:
        out.append({"at_ms": at, "node": sender, "command": "send_hash 22222222 b188_probe"})
        at += DM_EVERY
    return out


def scen_a():
    s = base("B188 (NON-CORPUS) A — COMPRESSED rolling window (%d ms, 1%% => %d ms budget). A mobile's J "
             "frames are not duty-pre-checked by the firmware, so the SIMULATOR's asynchronous duty "
             "hard-block fires and carries busy_until_ms." % (WINDOW_MS, WINDOW_MS // 100))
    s["_name"] = "b188_a_rolling_mobile"
    s["simulation"]["radio"]["duty_cycle_window_ms"] = WINDOW_MS
    s["nodes"] = [static_node("Home", 19, HOME_KEY, 47.60, -122.300),
                  static_node("Peer", 18, PEER_KEY, 47.60, -122.301),
                  mobile_node("MobileM", 50, MOB_KEY, 47.6003, -122.3005)]
    s["topology"]["links"] = link("Home", "Peer")
    return s


def scen_b(compressed):
    if compressed:
        s = base("B188 (NON-CORPUS) B — COMPRESSED rolling window (%d ms, 1%% => %d ms budget) + a DM every "
                 "%d ms. Exercises the FIRMWARE duty pre-check, its promised wait, and the deferred DATA "
                 "actually flying once the window rolls." % (WINDOW_MS, WINDOW_MS // 100, DM_EVERY))
        s["_name"] = "b188_b_rolling_data"
        s["simulation"]["radio"]["duty_cycle_window_ms"] = WINDOW_MS
    else:
        s = base("B188 (NON-CORPUS) C CONTROL — IDENTICAL to B except duty_cycle_window_ms is left at the "
                 "1 h DEFAULT, i.e. the ONE-SHOT regime all 36 corpus scenarios are in. The same load must "
                 "produce ZERO duty refusals: that is what attributes B's refusals to the window.")
        s["_name"] = "b188_c_control_oneshot"
    s["nodes"] = [static_node("Home", 19, HOME_KEY, 47.60, -122.300),
                  static_node("Peer", 18, PEER_KEY, 47.60, -122.301)]
    s["topology"]["links"] = link("Home", "Peer")
    s["commands"] = dm_stream("Home")
    return s


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
    for s in (scen_a(), scen_b(True), scen_b(False)):
        p = os.path.join(outdir, s["_name"] + ".json")
        with open(p, "w") as f:
            json.dump(s, f, indent=1)
            f.write("\n")
        print("wrote", p)


if __name__ == "__main__":
    main()
