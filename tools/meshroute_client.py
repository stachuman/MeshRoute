#!/usr/bin/env python3
# MeshRoute — tools/meshroute_client.py
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# Serial host client for a MeshRoute node (XIAO nRF52840 / Heltec) over USB-CDC. Three jobs:
#   1. monitor / repl  — watch the node + drive it interactively (works against today's firmware).
#   2. diagnosis       — structured commands like `routes` (dump the routing table) for bench debugging.
#   3. configuration   — read/set node config (freq, routing/data SF, SF list, bw, cr, duty, lbt, ...).
#
# DESIGN: pure line parsers (testable with --selftest, no device) + a threaded reader + a
# request/response helper that filters the node's async chatter ([hb]/[rx]/RECV/...) while it waits.
#
# ---- WIRE PROTOCOL (host <-> node console, line-based ASCII) --------------------------------------
# The node ALREADY emits (parsed here today):
#   [hb] t=<s>s radio=<OK|FAIL> duty_ms=<n>     RECV from=<id>: <text>     ACKED ctr=<n>
#   [rx] len=<n> cmd=<n> snr=<f> rssi=<f>       FAILED ctr=<n>             > queued ctr=<n> depth=<n>
# and ACCEPTS: `send <id> <text>`.
#
# The structured commands below are the CONTRACT for the firmware debug seam (Node accessors printed
# by fw_main). Until the firmware implements them, the client sends them and reports "not supported"
# on a `> parse error`. Proposed firmware output:
#   routes  -> [routes] n=<k>
#              [route] dest=<id> next=<id> hops=<n> score=<q4> gw=<0|1> layer=<id> age_ms=<n>
#              [routes] end
#   cfg     -> [cfg] node_id=<id> freq=<MHz> routing_sf=<n> data_sf=<n> sf_list=<csv> bw=<hz> cr=<n> \
#                    duty=<f> lbt=<0|1> beacon_ms=<n>
#   cfg set <key> <val> -> > cfg <key>=<val> ok   |   > cfg err <reason>
#   status  -> [status] uptime_ms=<n> rx=<n> tx=<n> duty_ms=<n> routes=<n> pending=<0|1>
#
# Usage:
#   meshroute_client.py ports
#   meshroute_client.py -p COM3 monitor
#   meshroute_client.py -p COM3 repl
#   meshroute_client.py -p COM3 send 2 hello
#   meshroute_client.py -p COM3 routes
#   meshroute_client.py -p COM3 cfg
#   meshroute_client.py -p COM3 cfg set routing_sf 7
#   meshroute_client.py --selftest         # parser unit checks, no device needed

import argparse
import re
import sys
import threading
import time
from queue import Queue, Empty

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    serial = None
    list_ports = None

DEFAULT_BAUD = 115200
# USB VIDs that a MeshRoute board enumerates as (Seeed XIAO nRF52840 = 0x2886; Adafruit nRF52 = 0x239A).
KNOWN_VIDS = {0x2886, 0x239A, 0x303A}  # Seeed, Adafruit, Espressif (Heltec)

# §10 cmd nibble (byte0 high 4 bits) -> name, for decoding [rx]/[tx] frames on the wire.
CMD_NAMES = {0: "B/beacon", 1: "R/RTS", 2: "C/CTS", 3: "D/DATA", 4: "K/ACK",
             5: "N/NACK", 6: "Q", 7: "H", 8: "F/RREQ", 9: "J/join", 15: "EXT"}

# --------------------------------------------------------------------------------------------------
# Pure line parsers — classify one console line into (kind, fields dict). No I/O; --selftest covers it.
# --------------------------------------------------------------------------------------------------
_PATTERNS = [
    ("hb",       re.compile(r"^\[hb\]\s+t=(?P<t>\d+)s\s+radio=(?P<radio>\w+)\s+duty_ms=(?P<duty>\d+)")),
    ("rx",       re.compile(r"^\[rx\]\s+len=(?P<len>\d+)\s+cmd=(?P<cmd>\d+)\s+snr=(?P<snr>[-\d.]+)\s+rssi=(?P<rssi>[-\d.]+)")),
    ("tx",       re.compile(r"^\[tx\]\s+cmd=(?P<cmd>\d+)\s+len=(?P<len>\d+)")),
    ("recv",     re.compile(r"^RECV from=(?P<from>\d+):\s?(?P<text>.*)$")),
    ("acked",    re.compile(r"^ACKED ctr=(?P<ctr>\d+)")),
    ("failed",   re.compile(r"^FAILED ctr=(?P<ctr>\d+)")),
    ("ack",      re.compile(r"^>\s*(?P<status>queued|err) ctr=(?P<ctr>\d+) depth=(?P<depth>\d+)")),
    ("cfg_ack",  re.compile(r"^>\s*cfg\s+(?P<rest>.*)$")),
    ("parse_err", re.compile(r"^>\s*parse error")),
    ("route",    re.compile(r"^\[route\]\s+(?P<kv>.*)$")),
    ("routes_hdr", re.compile(r"^\[routes\]\s+n=(?P<n>\d+)")),
    ("routes_end", re.compile(r"^\[routes\]\s+end")),
    ("cfg",      re.compile(r"^\[cfg\]\s+(?P<kv>.*)$")),
    ("status",   re.compile(r"^\[status\]\s+(?P<kv>.*)$")),
    ("banner",   re.compile(r"^(MeshRoute firmware|\s+(node id|freq|sf/bw/cr|tx power|board|radio|node|config)\s*=)")),
]


def _kv(s):
    """Parse a `key=value key2=value2` blob into a dict (values stay strings)."""
    out = {}
    for tok in s.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    return out


# Bring-up debug lines print WITHOUT a trailing newline (device_radio.h), so they glom onto the front
# of the next line: `[rxdone irq=12][rx] len=12 ...`, `[pre][pre][hb] t=...`. Strip + capture them so the
# real line still classifies. (If those debug prints are removed from the firmware, this just no-ops.)
_PRE = re.compile(r"^\[pre\]")
_RXDONE = re.compile(r"^\[rxdone irq=(?P<irq>[0-9A-Fa-f]+)\]")


def parse_line(line):
    """line -> (kind, fields). Leading [pre]/[rxdone irq=..] debug prefixes are stripped and kept as
    fields pre=<count>, irq=<hex>. kind='other' if nothing matches; 'pre' for a bare preamble marker."""
    line = line.rstrip("\r\n")
    pre, irq = 0, None
    while line:
        m = _PRE.match(line)
        if m:
            pre += 1
            line = line[m.end():]
            continue
        m = _RXDONE.match(line)
        if m:
            irq = m.group("irq")
            line = line[m.end():]
            continue
        break

    def _tag(kind, fields):
        if irq is not None:
            fields["irq"] = irq
        if pre:
            fields["pre"] = pre
        return kind, fields

    if not line:
        return _tag("pre", {}) if pre else ("other", {"raw": ""})
    for kind, pat in _PATTERNS:
        m = pat.match(line)
        if m:
            fields = m.groupdict()
            if "kv" in fields:
                fields = _kv(fields["kv"])
            return _tag(kind, fields)
    return _tag("other", {"raw": line})


# --------------------------------------------------------------------------------------------------
# Serial client
# --------------------------------------------------------------------------------------------------
class MeshRouteClient:
    def __init__(self, port, baud=DEFAULT_BAUD):
        if serial is None:
            sys.exit("pyserial not installed — run: pip install pyserial")
        self.port = port
        self.baud = baud
        self._ser = None
        self._rxq = Queue()
        self._on_line = None          # set -> reader calls it; unset -> reader queues (for request())
        self._stop = threading.Event()
        self._reader = None

    def open(self):
        self._ser = serial.Serial(self.port, self.baud, timeout=0.2)
        self._stop.clear()
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()
        return self

    def close(self):
        self._stop.set()
        if self._reader:
            self._reader.join(timeout=1.0)
        if self._ser:
            self._ser.close()

    def __enter__(self):
        return self.open()

    def __exit__(self, *a):
        self.close()

    def _read_loop(self):
        buf = b""
        while not self._stop.is_set():
            try:
                chunk = self._ser.read(256)
            except (OSError, serial.SerialException):
                break
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = raw.decode("utf-8", "replace").rstrip("\r")
                if not line:
                    continue
                cb = self._on_line
                if cb:
                    cb(line)
                else:
                    self._rxq.put((time.time(), line))

    def send_line(self, text):
        self._ser.write((text + "\n").encode("utf-8"))
        self._ser.flush()

    def _flush(self):
        while not self._rxq.empty():
            try:
                self._rxq.get_nowait()
            except Empty:
                break

    def request(self, cmd, collect, timeout=2.0):
        """Send `cmd`, then feed each subsequent line to collect(kind, fields, line) which returns
        'keep' | 'skip' | 'done'. Returns the list of kept (kind, fields) until 'done' or timeout."""
        assert self._on_line is None, "request() needs queue mode (not monitor/repl)"
        self._flush()
        self.send_line(cmd)
        kept, deadline = [], time.time() + timeout
        while time.time() < deadline:
            try:
                _, line = self._rxq.get(timeout=max(0.0, deadline - time.time()))
            except Empty:
                break
            kind, fields = parse_line(line)
            verdict = collect(kind, fields, line)
            if verdict == "keep":
                kept.append((kind, fields))
            elif verdict == "done":
                kept.append((kind, fields))
                break
        return kept

    # ---- high-level commands ---------------------------------------------------------------------
    def send_message(self, dst, text, timeout=2.0):
        def collect(kind, f, line):
            if kind in ("ack", "parse_err"):
                return "done"
            return "skip"
        res = self.request(f"send {dst} {text}", collect, timeout)
        return res[-1] if res else None

    def get_routes(self, timeout=3.0):
        def collect(kind, f, line):
            if kind == "route":
                return "keep"
            if kind == "routes_end":
                return "done"
            if kind == "parse_err":
                return "done"
            return "skip"
        res = self.request("routes", collect, timeout)
        if res and res[-1][0] == "parse_err":
            return None  # firmware doesn't implement `routes` yet
        return [f for k, f in res if k == "route"]

    def get_config(self, timeout=2.0):
        def collect(kind, f, line):
            if kind == "cfg":
                return "done"
            if kind == "parse_err":
                return "done"
            return "skip"
        res = self.request("cfg", collect, timeout)
        if not res or res[-1][0] == "parse_err":
            return None
        return res[-1][1]

    def set_config(self, key, value, timeout=2.0):
        def collect(kind, f, line):
            if kind in ("cfg_ack", "parse_err"):
                return "done"
            return "skip"
        res = self.request(f"cfg set {key} {value}", collect, timeout)
        return res[-1] if res else None

    def get_status(self, timeout=2.0):
        def collect(kind, f, line):
            if kind == "status":
                return "done"
            if kind == "parse_err":
                return "done"
            return "skip"
        res = self.request("status", collect, timeout)
        if not res or res[-1][0] == "parse_err":
            return None
        return res[-1][1]


# --------------------------------------------------------------------------------------------------
# Pretty output for monitor/repl
# --------------------------------------------------------------------------------------------------
def pretty(line):
    kind, f = parse_line(line)
    if kind == "hb":
        return f"  · alive {f['t']}s  radio={f['radio']}  duty={f['duty']}ms"
    if kind == "rx":
        name = CMD_NAMES.get(int(f["cmd"]), "?")
        irq = f"  irq=0x{f['irq']}" if "irq" in f else ""
        return f"  «rx  cmd={f['cmd']} {name:8s} len={f['len']} snr={f['snr']} rssi={f['rssi']}{irq}"
    if kind == "tx":
        name = CMD_NAMES.get(int(f["cmd"]), "?")
        return f"  »tx  cmd={f['cmd']} {name:8s} len={f['len']}"
    if kind == "pre":
        return f"  ·· preamble x{f.get('pre', 1)}"
    if kind == "recv":
        return f"  ★ RECV from {f['from']}: {f['text']}"
    if kind == "acked":
        return f"  ✓ ACKED ctr={f['ctr']}"
    if kind == "failed":
        return f"  ✗ FAILED ctr={f['ctr']}"
    if kind == "ack":
        return f"  → {f['status']} ctr={f['ctr']} depth={f['depth']}"
    return f"    {line}"


def find_ports():
    if list_ports is None:
        return []
    return list(list_ports.comports())


def autodetect_port():
    cands = [p for p in find_ports() if (p.vid in KNOWN_VIDS)]
    if len(cands) == 1:
        return cands[0].device
    return None


# --------------------------------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------------------------------
def cmd_ports(_args):
    ports = find_ports()
    if not ports:
        print("no serial ports found")
        return
    for p in ports:
        mark = " <- likely MeshRoute" if (p.vid in KNOWN_VIDS) else ""
        vid = f"{p.vid:04x}" if p.vid else "----"
        pid = f"{p.pid:04x}" if p.pid else "----"
        print(f"  {p.device:12s} {vid}:{pid}  {p.description}{mark}")


def _resolve_port(args):
    port = args.port or autodetect_port()
    if not port:
        sys.exit("no port given and auto-detect was ambiguous — run `ports` and pass -p")
    return port


def cmd_monitor(args):
    with MeshRouteClient(_resolve_port(args), args.baud) as c:
        c._on_line = lambda ln: print(pretty(ln), flush=True)
        print(f"monitoring {c.port} @ {c.baud} — Ctrl+C to quit")
        try:
            while True:
                time.sleep(0.5)
        except KeyboardInterrupt:
            print()


def cmd_repl(args):
    with MeshRouteClient(_resolve_port(args), args.baud) as c:
        c._on_line = lambda ln: print(pretty(ln), flush=True)
        print(f"repl on {c.port} — type console lines (e.g. `send 2 hi`), Ctrl+C/Ctrl+D to quit")
        try:
            for raw in sys.stdin:
                line = raw.rstrip("\n")
                if line:
                    c.send_line(line)
        except KeyboardInterrupt:
            print()


def cmd_send(args):
    with MeshRouteClient(_resolve_port(args), args.baud) as c:
        r = c.send_message(args.dst, " ".join(args.text))
        if not r:
            print("no response (timeout)")
        elif r[0] == "parse_err":
            print("firmware rejected the command (parse error)")
        else:
            print(f"{r[1].get('status')} ctr={r[1].get('ctr')} depth={r[1].get('depth')}")


def cmd_routes(args):
    with MeshRouteClient(_resolve_port(args), args.baud) as c:
        routes = c.get_routes()
        if routes is None:
            print("`routes` not implemented in the firmware yet (got a parse error).\n"
                  "  -> next step: add the routes-dump debug command (see the protocol in this file's header).")
            return
        if not routes:
            print("routing table is empty (no routes learned)")
            return
        print(f"{'dest':>4} {'next':>4} {'hops':>4} {'score':>6} {'gw':>2} {'layer':>5} {'age_ms':>8}")
        for r in routes:
            print(f"{r.get('dest',''):>4} {r.get('next',''):>4} {r.get('hops',''):>4} "
                  f"{r.get('score',''):>6} {r.get('gw',''):>2} {r.get('layer',''):>5} {r.get('age_ms',''):>8}")


def cmd_cfg(args):
    with MeshRouteClient(_resolve_port(args), args.baud) as c:
        if args.set:
            key, value = args.set
            r = c.set_config(key, value)
            if not r:
                print("no response (timeout)")
            elif r[0] == "parse_err":
                print("`cfg set` not implemented in the firmware yet (parse error)")
            else:
                print(f"  {r[1].get('rest', 'ok')}")
        else:
            cfg = c.get_config()
            if cfg is None:
                print("`cfg` not implemented in the firmware yet (parse error).\n"
                      "  -> next step: add the cfg get/set debug command (see the header protocol).")
                return
            for k, v in cfg.items():
                print(f"  {k:14s} = {v}")


def cmd_status(args):
    with MeshRouteClient(_resolve_port(args), args.baud) as c:
        st = c.get_status()
        if st is None:
            print("`status` not implemented in the firmware yet (parse error)")
            return
        for k, v in st.items():
            print(f"  {k:14s} = {v}")


def cmd_raw(args):
    with MeshRouteClient(_resolve_port(args), args.baud) as c:
        lines = []
        c.request(" ".join(args.line), lambda k, f, ln: lines.append(ln) or "skip", timeout=args.wait)
        for ln in lines:
            print(f"  {ln}")
        if not lines:
            print("  (no output in window)")


def selftest():
    samples = {
        "[hb] t=15s radio=OK duty_ms=88":                       ("hb", {"t": "15", "radio": "OK", "duty": "88"}),
        "[rx] len=12 cmd=0 snr=14.2 rssi=-30":                  ("rx", {"len": "12", "cmd": "0", "snr": "14.2", "rssi": "-30"}),
        "[rxdone irq=12][rx] len=7 cmd=2 snr=13.5 rssi=-0":     ("rx", {"len": "7", "cmd": "2", "irq": "12"}),
        "[pre][pre][hb] t=20s radio=OK duty_ms=176":           ("hb", {"t": "20", "pre": 2}),
        "[tx] cmd=1 len=7":                                     ("tx", {"cmd": "1", "len": "7"}),
        "RECV from=1: hello world":                             ("recv", {"from": "1", "text": "hello world"}),
        "ACKED ctr=3":                                          ("acked", {"ctr": "3"}),
        "FAILED ctr=3":                                         ("failed", {"ctr": "3"}),
        "> queued ctr=1 depth=0":                               ("ack", {"status": "queued", "ctr": "1", "depth": "0"}),
        "> parse error":                                        ("parse_err", {}),
        "[routes] n=2":                                         ("routes_hdr", {"n": "2"}),
        "[route] dest=2 next=2 hops=1 score=160 gw=0 layer=0 age_ms=1234":
            ("route", {"dest": "2", "next": "2", "hops": "1", "score": "160", "gw": "0", "layer": "0", "age_ms": "1234"}),
        "[routes] end":                                         ("routes_end", {}),
        "[cfg] node_id=1 freq=869.4625 routing_sf=7 data_sf=12": ("cfg", {"node_id": "1", "freq": "869.4625", "routing_sf": "7", "data_sf": "12"}),
        "  node id   = 1":                                      ("banner", {}),
    }
    ok = True
    for line, (want_kind, want_fields) in samples.items():
        kind, fields = parse_line(line)
        bad = kind != want_kind or any(fields.get(k) != v for k, v in want_fields.items())
        flag = "FAIL" if bad else "ok"
        if bad:
            ok = False
            print(f"  {flag}: {line!r} -> {kind} {fields}  (wanted {want_kind} {want_fields})")
        else:
            print(f"  {flag}: {kind:11s} {line}")
    print("SELFTEST:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description="MeshRoute serial client — monitor, diagnose, configure.")
    ap.add_argument("-p", "--port", help="serial port (auto-detect if omitted)")
    ap.add_argument("-b", "--baud", type=int, default=DEFAULT_BAUD)
    ap.add_argument("--selftest", action="store_true", help="run the parser self-test (no device)")
    sub = ap.add_subparsers(dest="cmd")

    sub.add_parser("ports", help="list serial ports").set_defaults(func=cmd_ports)
    sub.add_parser("monitor", help="stream the node output").set_defaults(func=cmd_monitor)
    sub.add_parser("repl", help="interactive console").set_defaults(func=cmd_repl)
    sub.add_parser("routes", help="dump the routing table").set_defaults(func=cmd_routes)
    sub.add_parser("status", help="node status").set_defaults(func=cmd_status)

    ps = sub.add_parser("send", help="send a DM: send <dst> <text...>")
    ps.add_argument("dst", type=int)
    ps.add_argument("text", nargs="+")
    ps.set_defaults(func=cmd_send)

    pc = sub.add_parser("cfg", help="get config, or `cfg --set <key> <value>`")
    pc.add_argument("--set", nargs=2, metavar=("KEY", "VALUE"))
    pc.set_defaults(func=cmd_cfg)

    pr = sub.add_parser("raw", help="send a raw line + print the response window")
    pr.add_argument("line", nargs="+")
    pr.add_argument("--wait", type=float, default=2.0)
    pr.set_defaults(func=cmd_raw)

    args = ap.parse_args(argv)
    if args.selftest:
        return selftest()
    if not getattr(args, "func", None):
        ap.print_help()
        return 1
    return args.func(args) or 0


if __name__ == "__main__":
    sys.exit(main())
