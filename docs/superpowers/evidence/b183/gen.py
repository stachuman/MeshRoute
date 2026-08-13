#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""B183 minimal controlled reattachment scenario + its controls.

Shape (brief §4): ONE unprovisioned mobile (mobile_autoregister defaults TRUE, so
the service trigger is the automatic boot arm -- no manual command is used), ONE
initial home that deliberately dies, ONE eligible replacement static that stays
alive. GLOBAL plane only (no team_id anywhere). Neither static is a gateway, so
`can_host_mobiles()` = host_mobiles && !is_mobile && !is_gateway && n_layers==1
is satisfied by default on both. No DM is sent: the presence transaction alone is
under test.

⛔ DIAGNOSTIC FIXTURE, NOT A CORPUS ROW. Never add to simulation/ or the 36-row runner.
"""
import json, copy, sys, os

MOB_KEY = "0xe27bc270"      # the mobile's stable key hash -- the ONLY identity used in assertions
DIE_MS = 120000
DUR = 1500000

def base():
    return {
        "_name": "b183_reattach_min",
        "_desc": ("B183 diagnosis fixture (NON-CORPUS). 1 mobile + initial home HomeA "
                  "(dies) + eligible replacement HomeB. Presence plane only."),
        "simulation": {
            "duration_ms": DUR, "step_ms": 1, "warmup_ms": 0,
            "beacon_period_ms": 20000, "seed": 183, "node_startup_jitter_ms": 0,
            "radio": {"sf": 8, "bw": 62.5, "cr": 5, "max_packet_bytes": 255,
                      "snr_coherence_ms": 0, "duty_cycle": 1},
            "path_loss": {"model": "log_distance", "mobile_only": True, "alpha": 3.0,
                          "sigma_db": 0.0, "ref_distance_m": 1.0, "ref_loss_db": 40.0,
                          "noise_floor_db": -120.0, "tx_power_dbm": 14.0,
                          "node_tx_offset_sigma_db": 0.0, "node_rx_offset_sigma_db": 0.0,
                          "asymmetry_coherence_ms": 60000},
        },
        "commands": [],
        "expect": [],
        "nodes": [
            {"name": "HomeA", "script": "scenarios/dv_dual_sf.lua",
             "lat": 47.6000, "lon": -122.3000, "start_at_ms": 1000,
             "node_id": 19, "key_hash32": "0x33333333",
             "config": {"routing_sf": 8, "allowed_data_sfs": [7, 9, 10],
                        "beacon_period_ms": 20000}},
            # ★ HomeB starts AFTER the mobile's first attachment window and BEFORE HomeA dies.
            # The firmware sees an SNR saturated at +12 dB (shapeReportedSnr), so two strong
            # OFFERs TIE and arrival order decides -- geometry cannot bias the choice. Staggering
            # the start is the only deterministic way to make the mobile attach to the node that
            # later dies, while leaving HomeB a live, strong, eligible replacement at loss time.
            {"name": "HomeB", "script": "scenarios/dv_dual_sf.lua",
             "lat": 47.6000, "lon": -122.3010, "start_at_ms": 60000,
             "node_id": 20, "key_hash32": "0x44444444",
             "config": {"routing_sf": 8, "allowed_data_sfs": [7, 9, 10],
                        "beacon_period_ms": 20000}},
            {"name": "MobileM", "script": "scenarios/dv_dual_sf.lua",
             "lat": 47.60030, "lon": -122.30050, "start_at_ms": 1000,
             "node_id": 50, "key_hash32": MOB_KEY,
             "velocity_mps": 0.01, "direction_deg": 0.0,
             "config": {"routing_sf": 8, "allowed_data_sfs": [7, 9, 10],
                        "beacon_period_ms": 20000, "is_mobile": True,
                        "discovery_min_routes": 1, "req_sync_min_routes": 1}},
        ],
        "topology": {"links": [
            {"from": "HomeA", "to": "HomeB", "snr": 38.0, "rssi": -82.0, "snr_std_dev": 0, "bidir": True},
        ]},
    }

def node(c, name):
    return next(n for n in c["nodes"] if n["name"] == name)

OUT = sys.argv[1] if len(sys.argv) > 1 else '.'

# ---- C1: healthy positive control -- no home loss at all
c1 = base()
c1["_name"] = "b183_c1_healthy"
c1["_desc"] += " C1 HEALTHY POSITIVE CONTROL: no home loss."
c1["expect"] = [{"type": "script_emit_contains", "node": "MobileM",
                 "emit_type": "mobile_attach_confirmed", "value": "\"home\""}]

# ---- C2: post-loss positive control -- HomeA dies, HomeB free and duty-idle
c2 = base()
c2["_name"] = "b183_c2_postloss"
c2["_desc"] += f" C2 POST-LOSS POSITIVE CONTROL: HomeA dies_at_ms={DIE_MS}, HomeB eligible + duty-idle."
node(c2, "HomeA")["dies_at_ms"] = DIE_MS

# ---- C3: eligibility mutation -- HomeB opts out of hosting
c3 = copy.deepcopy(c2)
c3["_name"] = "b183_c3_ineligible"
c3["_desc"] += " C3 ELIGIBILITY MUTATION: HomeB host_mobiles=false."
node(c3, "HomeB")["config"]["host_mobiles"] = False

# ---- C4: edge-specific mutation -- HomeB's TX duty budget exhausted
c4 = copy.deepcopy(c2)
c4["_name"] = "b183_c4_hostduty"
c4["_desc"] += (" C4 EDGE-SPECIFIC MUTATION (host side): HomeB duty_cycle=0.0003 (fraction) "
                "so its modem refuses TX once ~1080 ms of airtime is spent. RX unaffected.")
node(c4, "HomeB")["config"]["duty_cycle"] = 0.0003

# ---- C5: reciprocal edge mutation -- the MOBILE's TX duty budget exhausted
c5 = copy.deepcopy(c2)
c5["_name"] = "b183_c5_mobileduty"
c5["_desc"] += (" C5 RECIPROCAL EDGE MUTATION (mobile side): MobileM duty_cycle=0.0014 so its own "
                "modem refuses the DISCOVER once its airtime is spent.")
node(c5, "MobileM")["config"]["duty_cycle"] = 0.0014

for c in (c1, c2, c3, c4, c5):
    p = os.path.join(OUT, c["_name"] + ".json")
    json.dump(c, open(p, "w"), indent=1)
    print("wrote", p)
