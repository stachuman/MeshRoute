# MeshRoute lab harness — pull_inbox: the durable receive-ledger (DM + channel records).
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# `pull_inbox <dm_since> <chan_since>` streams DM-block then channel-block then inbox_end, one NDJSON object
# per line (console_json.cpp write_inbox_*). We json.loads each line (skipping any interleaved non-JSON
# console output) and bucket by `ev`. The reconciler matches on the `body` tag; ctr/channel_msg_id/sender_hash
# are the cross-check.
import json

try:
    from .manager import collect_burst   # noqa: F401  (only used via NodeManager.request; kept for symmetry)
except ImportError:
    pass


def parse_inbox_lines(lines):
    """Bucket a pull_inbox burst -> {"dm":[obj…], "chan":[obj…], "end":obj|None}. Robust to interleaved noise."""
    dm, chan, end = [], [], None
    for ln in lines:
        s = ln.strip()
        if not s.startswith("{"):
            continue                       # skip an interleaved RECV/CH/beacon line
        try:
            o = json.loads(s)
        except ValueError:
            continue
        ev = o.get("ev")
        if ev == "inbox_dm":
            dm.append(o)
        elif ev == "inbox_channel":
            chan.append(o)
        elif ev == "inbox_end":
            end = o
            break
    return {"dm": dm, "chan": chan, "end": end}


def pull_inbox(manager, node, dm_since=0, chan_since=0, timeout=6.0):
    """Marker-aware (inbox_end) request + parse -> {"dm":[…], "chan":[…], "end":{…}}."""
    lines = manager.request(node, f"pull_inbox {dm_since} {chan_since}", "inbox_end", timeout)
    return parse_inbox_lines(lines)


def _selftest():
    burst = [
        " t=10 ms «rx BCN from=2 ...",                                  # interleaved noise -> skipped
        '{"ev":"inbox_dm","seq":1,"origin":222,"layer_id":1,"ctr":7,"sender_hash":305419896,"rx_ms":1000,"body":"Ta1S222#0"}',
        '{"ev":"inbox_dm","seq":2,"origin":166,"layer_id":1,"ctr":3,"sender_hash":0,"rx_ms":1100,"body":"Ta1S166#0"}',
        '{"ev":"inbox_channel","seq":1,"origin":254,"layer_id":1,"channel_id":0,"channel_msg_id":4274165505,"rx_ms":1200,"body":"Ta1S254#0"}',
        '{"ev":"inbox_end","dm_seq":2,"chan_seq":1,"epoch":3,"count":3,"now_ms":1300}',
        '{"ev":"inbox_dm","seq":99,"body":"AFTER-END-ignored"}',         # after inbox_end -> not read
    ]
    r = parse_inbox_lines(burst)
    assert len(r["dm"]) == 2 and r["dm"][0]["body"] == "Ta1S222#0" and r["dm"][0]["ctr"] == 7, r
    assert len(r["chan"]) == 1 and r["chan"][0]["channel_msg_id"] == 4274165505, r
    assert r["end"]["count"] == 3 and r["end"]["dm_seq"] == 2, r
    assert parse_inbox_lines([])["end"] is None
    print("inbox selftest OK")


if __name__ == "__main__":
    _selftest()
