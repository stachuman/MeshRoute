# MeshRoute lab harness — payload tag: T<run>S<src>#<n>  (run = host-side run id; nodes have no clock).
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# The oracle embeds this tag verbatim in every DM/channel payload, then reconciles sent vs received BY TAG
# (the on-wire ctr / channel_msg_id / sender_hash are the cross-check). The tag is the whole oracle payload.
import re

_TAG_RE = re.compile(r"T([0-9A-Za-z]+)S(\d+)#(\d+)")
_SENDMS_RE = re.compile(r"T[0-9A-Za-z]+S\d+#\d+@(\d+)")   # the firmware scheduled-send transmit-time suffix `…#<n>@<ms>`


def make_tag(run, src, n):
    return f"T{run}S{src}#{n}"


def parse_sendms(s):
    """The firmware's `@<sendms>` transmit-time suffix (the node's millis() at TX) from a scheduled-send body -> int,
    or None if absent. parse_tag already ignores the suffix (it matches the `T…#<n>` prefix), so the canonical tag
    still reconciles; this pulls the send timestamp out for the (clock-aligned, approximate) latency."""
    if not s:
        return None
    m = _SENDMS_RE.search(s)
    return int(m.group(1)) if m else None


def parse_tag(s):
    """Find a tag anywhere in `s` -> {run, src:int, n:int}, or None."""
    if not s:
        return None
    m = _TAG_RE.search(s)
    if not m:
        return None
    return {"run": m.group(1), "src": int(m.group(2)), "n": int(m.group(3))}


def find_tag(body, run=None):
    """parse_tag + optional run-id filter (ignore a tag from a different run — run-isolation)."""
    t = parse_tag(body)
    if t is None or (run is not None and t["run"] != run):
        return None
    return t


def _selftest():
    assert make_tag("a1", 254, 0) == "Ta1S254#0"
    assert parse_tag("Ta1S254#7") == {"run": "a1", "src": 254, "n": 7}
    assert parse_tag("prefix Ta1S3#2 suffix") == {"run": "a1", "src": 3, "n": 2}   # found anywhere
    assert parse_tag("Ta1S3#2@12345") == {"run": "a1", "src": 3, "n": 2}           # the @sendms suffix is ignored (canonical tag)
    assert parse_sendms("Ta1S3#2@12345") == 12345 and parse_sendms("Ta1S3#2") is None
    assert parse_tag("no tag here") is None
    assert find_tag("Ta1S3#0", run="a1") == {"run": "a1", "src": 3, "n": 0}
    assert find_tag("Tb2S3#0", run="a1") is None                                   # different run -> ignored
    print("tag selftest OK")


if __name__ == "__main__":
    _selftest()
