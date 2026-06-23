# MeshRoute lab harness — console line parsers (PURE, no serial -> unit-testable offline).
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# Each parse_*(lines) takes a response burst (list of console lines) and returns a dict, or None if the
# expected line wasn't in the burst. Formats are the VERIFIED firmware contracts (fw_main.cpp):
#   [whoami] id=<N> hash=0x<HEX> [name="…"] leaf=<L> gw=<0|1> gwonly=<0|1> mobile=<0|1>
#   [cfg] node_id=<N>  then  "  radio : … sf_list=a,b …"  "  proto : duty=… …"  "  leaf  : leaf_id=… level_id=… …"
#   [status] uptime_ms=… rx=… tx=… txq=… routes=<R> pending=<0|1> …
#   [duty] <pct>%  [" — SILENT, ~<N> s to availability"]   |   [duty] disabled (no duty limit)
import re


def _kv(s):
    """`key=value key2=value2 …` -> dict (values stay strings; non-kv tokens like `(→nibble 15)` are skipped)."""
    out = {}
    for tok in s.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    return out


def parse_whoami(lines):
    for ln in lines:
        s = ln.strip()
        if not s.startswith("[whoami]"):
            continue
        name = None
        m = re.search(r'name="([^"]*)"', s)
        if m:
            name = m.group(1)
        kv = _kv(re.sub(r'name="[^"]*"', "", s[len("[whoami]"):]))   # drop name="…" (may contain spaces) before kv
        if "id" not in kv:
            return None
        return {
            "node_id": int(kv["id"]),
            "hash":    kv.get("hash"),                # keep the "0x…" string (Arduino HEX: uppercase, no leading 0s)
            "leaf":    int(kv["leaf"]) if "leaf" in kv else None,
            "gw":      kv.get("gw") == "1",
            "gwonly":  kv.get("gwonly") == "1",
            "mobile":  kv.get("mobile") == "1",
            "name":    name,
        }
    return None


def parse_cfg(lines):
    """Flatten [cfg] + its `tag : k=v …` sub-lines into one dict (keys are unique across sub-lines)."""
    out = {}
    seen_header = False
    for ln in lines:
        s = ln.strip()
        if s.startswith("[cfg]"):
            out.update(_kv(s[len("[cfg]"):]))         # node_id=<N>
            seen_header = True
        elif ":" in s and seen_header:
            tag, _, rest = s.partition(":")
            if tag.strip() in ("radio", "proto", "leaf", "ble", "loc"):
                out.update(_kv(rest))                  # radio: freq/routing_sf/sf_list/bw/cr/tx_power; leaf: leaf_id/level_id/…
    return out if "node_id" in out else None


def parse_status(lines):
    for ln in lines:
        s = ln.strip()
        if s.startswith("[status]"):
            return _kv(s[len("[status]"):])            # routes, rx, tx, uptime_ms, txq, pending, … (all strings)
    return None


def parse_routes(lines):
    """[route] dest=… next=… hops=… score=… … lines -> [dict per route]; skips the [route]   gw_sched sub-lines."""
    out = []
    for ln in lines:
        s = ln.strip()
        if s.startswith("[route]") and "dest=" in s and "gw_sched" not in s:
            out.append(_kv(s[len("[route]"):]))
    return out


def parse_duty(lines):
    for ln in lines:
        s = ln.strip()
        if not s.startswith("[duty]"):
            continue
        if "disabled" in s:
            return {"enabled": False, "pct": None, "silent": False, "avail_s": None}
        mp = re.search(r"\[duty\]\s+(\d+)%", s)
        if not mp:
            return None
        pct = int(mp.group(1))
        ma = re.search(r"~\s*(\d+)\s*s", s)            # em-dash-agnostic: just find "~<N> s"
        return {
            "enabled": True,
            "pct":     pct,
            "silent":  ("SILENT" in s) or pct >= 100,
            "avail_s": int(ma.group(1)) if ma else 0,
        }
    return None


def normalize_sf_list(s):
    """Canonical sf_list for comparison: '9,7' / '7, 9' / '7,9' -> '7,9'; '-' / '' -> '' (no data SF)."""
    if s is None:
        return ""
    s = s.strip()
    if s in ("", "-"):
        return ""
    try:
        return ",".join(str(n) for n in sorted(int(x) for x in s.split(",") if x.strip()))
    except ValueError:
        return s


# --------------------------------------------------------------------------------------------------
# Offline self-test (no device): `python3 tools/lab/parsers.py`
# --------------------------------------------------------------------------------------------------
def _selftest():
    w = parse_whoami(['[whoami] id=254 hash=0x1A2B3C4D name="bench 1" leaf=1 gw=0 gwonly=0 mobile=0'])
    assert w == {"node_id": 254, "hash": "0x1A2B3C4D", "leaf": 1, "gw": False, "gwonly": False,
                 "mobile": False, "name": "bench 1"}, w
    w0 = parse_whoami(["[whoami] id=0 hash=0xDEAD leaf=1 gw=0 gwonly=0 mobile=1"])   # unprovisioned, no name
    assert w0["node_id"] == 0 and w0["mobile"] is True and w0["name"] is None, w0

    cfg = parse_cfg([
        "[cfg] node_id=73",
        "      radio : freq=869.0000 routing_sf=8 sf_list=6,7 bw=125000 cr=5 tx_power=22",
        "      proto : duty=0.100 beacon_ms=900000 hop_cap=16 lbt=0 nav=1 nav_ignore=0",
        "      leaf  : leaf_id=15 level_id=191 (→nibble 15) gateway=0 gateway_only=0 mobile=0",
    ])
    assert cfg["node_id"] == "73" and cfg["sf_list"] == "6,7" and cfg["leaf_id"] == "15" \
        and cfg["level_id"] == "191" and cfg["duty"] == "0.100" and cfg["freq"] == "869.0000", cfg
    cfg_empty = parse_cfg(["[cfg] node_id=0", "      radio : freq=869.0000 routing_sf=9 sf_list=- bw=125000 cr=5 tx_power=22"])
    assert cfg_empty["sf_list"] == "-" and normalize_sf_list(cfg_empty["sf_list"]) == "", cfg_empty

    st = parse_status(["[status] uptime_ms=1366813 rx=42 tx=10 isr=12 txq=0 txdrop=0 sleep=auto lbt=0 routes=4 pending=0"])
    assert st["routes"] == "4" and st["uptime_ms"] == "1366813" and st["pending"] == "0" and st["sleep"] == "auto", st

    rts = parse_routes([
        "[routes] n=2",
        "[route] dest=141 next=141 hops=1 score=228 pen=0 gw=0 layer=4 age_ms=100 cand=2",
        "[route] dest=76 next=141 hops=2 score=44 pen=0 gw=0 layer=4 age_ms=200 cand=1",
        "[route]   gw_sched period=1000ms heard_ms=5",   # sub-line — must be skipped
        "[routes] end",
    ])
    assert len(rts) == 2 and rts[0]["dest"] == "141" and rts[0]["hops"] == "1" \
        and rts[0]["score"] == "228" and rts[1]["hops"] == "2", rts

    assert parse_duty(["[duty] 42%"]) == {"enabled": True, "pct": 42, "silent": False, "avail_s": 0}
    d = parse_duty(["[duty] 100% — SILENT, ~73 s to availability"])
    assert d == {"enabled": True, "pct": 100, "silent": True, "avail_s": 73}, d
    assert parse_duty(["[duty] disabled (no duty limit)"]) == {"enabled": False, "pct": None, "silent": False, "avail_s": None}

    assert normalize_sf_list("9,7") == "7,9" and normalize_sf_list("7, 9") == "7,9" and normalize_sf_list("-") == ""
    # not-found cases
    assert parse_whoami(["garbage"]) is None and parse_cfg(["nope"]) is None and parse_duty([]) is None
    print("parsers selftest OK")


if __name__ == "__main__":
    _selftest()
