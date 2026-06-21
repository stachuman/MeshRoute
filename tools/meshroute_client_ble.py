#!/usr/bin/env python3
# MeshRoute — tools/meshroute_client_ble.py
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# BLE (Nordic UART Service) host client for a MeshRoute node — the wireless twin of meshroute_client.py.
# It is BOTH a desktop bench harness AND the reference implementation for the iOS companion transport:
# the connect -> pair -> subscribe-TX -> write-RX -> parse-NDJSON flow here is exactly what the app does.
#
# Requires: bleak (`pip install -r tools/requirements-ble.txt`). Tested against the Step-5/6 firmware.
#
# ---- THE WIRE (host <-> node over NUS; mirrors docs/specs/2026-05-30-device-console-design.md §4) -------
# Service 6E400001-B5A3-F393-E0A9-E50E24DCCA9E (the device advertises THIS uuid — we scan for it).
#   RXD 6E400002 (write)  : host -> node. A console line, ASCII, e.g. "send 2 hello\n" or "whoami\n".
#   TXD 6E400003 (notify) : node -> host. One NDJSON object per line, e.g.
#       {"ack":"queued","ctr":7,"qd":1}                 {"ev":"msg_recv","origin":3,"ctr":7,"body":"hi"}
#       {"ev":"send_acked","dst":5,"ctr":7}             {"ev":"channel_recv","origin":3,"channel_id":2,"body":"hi"}
#       {"ev":"ready","id":1,"key":"a1b2c3d4",...}      {"err":"parse","code":"unknown_cmd"}
#   The node sends ONLY JSON over BLE (the human plain-text dumps stay on USB); commands in are the same
#   line grammar as USB (`send <id|hash> "..." [-a] [-e]`/`send_channel`/`send_layer`/`resolve`/`whoami`).
#
# ---- PAIRING (the firmware requires an encrypted + MITM-bonded link — spec §A.3) ------------------------
# The node serves its GATT chars at SECMODE_ENC_WITH_MITM with a STATIC 6-digit passkey (default 123456,
# `cfg set ble_pin` to change). The peer must pair with that PIN before RXD/TXD work:
#   - macOS (CoreBluetooth): pairing is automatic — the OS pops a PIN dialog on the first encrypted-char
#     touch (our start_notify on TXD). `client.pair()` is a no-op there; just enter the PIN when prompted.
#   - Linux (BlueZ): bleak's `client.pair()` uses the system agent. For a static-PIN MITM peripheral the
#     most reliable path is to pre-bond ONCE with bluetoothctl, then this client reconnects to the bond:
#         $ bluetoothctl
#         [bluetooth]# agent KeyboardDisplay
#         [bluetooth]# default-agent
#         [bluetooth]# scan on            (find the MeshRoute-<id> address)
#         [bluetooth]# pair <ADDR>        (enter PIN 123456 when asked)
#         [bluetooth]# trust <ADDR>
#     After that, `meshroute_client_ble.py monitor --addr <ADDR>` connects to the bonded device.
#   - Windows (WinRT): bleak's `client.pair()` does NOT do the passkey ceremony (it returns None without
#     bonding). Pair ONCE via Settings > Bluetooth & devices > Add device > "MeshRoute-<id>" > enter the PIN,
#     then run this client with `--no-pair` (Windows reconnects to the bond automatically).
# This client calls pair() best-effort, then if the encrypted-char subscribe fails for lack of a bond it
# prints the OS-pairing recipe (above) and exits — never a silent hang, never an insecure shortcut.
#
# Usage:
#   meshroute_client_ble.py scan                         # list advertising MeshRoute nodes
#   meshroute_client_ble.py monitor [--addr A | --name N]# connect + stream the node's JSON
#   meshroute_client_ble.py repl    [--addr A]           # interactive: type lines, see JSON
#   meshroute_client_ble.py send 2 hello [--addr A]      # one-shot DM, wait for the ack
#   meshroute_client_ble.py --pin 654321 monitor         # non-default pairing PIN
#   meshroute_client_ble.py --selftest                   # JSON-line parser checks, no device needed

import sys

# WINDOWS/WinRT FIX — MUST run before `import bleak` (or anything pulling in pythoncom). In a console app the
# COM apartment can default to/be forced to STA (single-threaded), and then WinRT async event callbacks NEVER
# dispatch — connect() + start_notify() succeed and the peripheral reports the CCCD enabled, but the
# notification handler is silent (the exact symptom we hit). Forcing MTA makes the callbacks fire. No-op off
# Windows. (See bleak troubleshooting: COM apartment / Windows GUI.)
if sys.platform == "win32":
    sys.coinit_flags = 0   # 0 = COINIT_MULTITHREADED

import argparse
import asyncio
import json

NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX      = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   # write  (host -> node)
NUS_TX      = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   # notify (node -> host)

DEFAULT_PIN  = "123456"
NAME_PREFIX  = "MeshRoute"     # the advert name the firmware uses by default ("MeshRoute-<id>")


# ---- NDJSON line formatting (the schema the firmware emits) -------------------------------------------
def pretty(obj):
    """One firmware NDJSON object -> a human line. Mirrors meshroute_client.py's pretty() vocabulary so the
    BLE and USB monitors read alike. `obj` is the already-parsed dict (or the raw str if it didn't parse)."""
    if isinstance(obj, str):
        return f"  ? {obj}"                                   # not JSON — show raw (shouldn't happen on BLE)
    if "ack" in obj:
        return f"  ✓ ack {obj['ack']} ctr={obj.get('ctr', 0)} qd={obj.get('qd', 0)}"
    if "err" in obj:
        extra = obj.get("code") or obj.get("msg") or ""
        return f"  ⚠ err {obj['err']}{(' ' + str(extra)) if extra else ''}"
    ev = obj.get("ev")
    if ev == "msg_recv":
        return f"  ★ RECV from {obj.get('origin')}: {obj.get('body','')}  (ctr={obj.get('ctr')})"
    if ev == "channel_recv":
        return f"  # CH{obj.get('channel_id')} from {obj.get('origin')}: {obj.get('body','')}"
    if ev == "send_acked":
        return f"  ✓ ACKED dst={obj.get('dst')} ctr={obj.get('ctr')}"
    if ev == "send_failed":
        return f"  ✗ FAILED dst={obj.get('dst')} ctr={obj.get('ctr')}"
    if ev == "hash_resolved":
        node = obj.get("node", 0)
        where = "timeout" if node == 0 else ("auth" if obj.get("auth") else "cached")
        return f"  → RESOLVED hash=0x{obj.get('hash', 0):08x} node={node} ({where})"
    if ev == "ready":
        return (f"  ● READY id={obj.get('id')} key={obj.get('key')} leaf={obj.get('leaf_id')} "
                f"mode={obj.get('mode')} gw={obj.get('gateway')} sf={obj.get('routing_sf')}")
    return "  · " + json.dumps(obj, separators=(",", ":"))


class LineBuffer:
    """Accumulate TXD notification chunks and yield complete '\\n'-terminated NDJSON lines. The node frames
    one JSON object per line; BLE may split a line across notifications (or batch several), so we buffer."""
    def __init__(self):
        self._buf = bytearray()

    def feed(self, data):
        self._buf.extend(data)
        out = []
        while True:
            nl = self._buf.find(b"\n")
            if nl < 0:
                break
            line = bytes(self._buf[:nl]).decode("utf-8", "replace").strip()
            del self._buf[:nl + 1]
            if line:
                out.append(line)
        return out


def parse_json_line(line):
    try:
        return json.loads(line)
    except (ValueError, TypeError):
        return line   # surface the raw line rather than dropping it (fail visible, not silent)


# ---- BLE plumbing ------------------------------------------------------------------------------------
def _import_bleak():
    try:
        from bleak import BleakClient, BleakScanner
        return BleakClient, BleakScanner
    except ImportError:
        sys.exit("error: bleak is not installed.  pip install -r tools/requirements-ble.txt")


async def discover(timeout=6.0, want_name=None):
    """Return [(address, name, rssi)] for devices advertising the NUS service (optionally name-filtered)."""
    _, BleakScanner = _import_bleak()
    found = await BleakScanner.discover(timeout=timeout, return_adv=True)
    out = []
    for addr, (dev, adv) in found.items():
        uuids = [u.lower() for u in (adv.service_uuids or [])]
        name = adv.local_name or dev.name or ""
        is_mr = (NUS_SERVICE in uuids) or name.startswith(NAME_PREFIX)
        if not is_mr:
            continue
        if want_name and want_name.lower() not in name.lower():
            continue
        out.append((addr, name, adv.rssi))
    out.sort(key=lambda r: (r[2] is None, -(r[2] or -999)))   # strongest signal first
    return out


async def _resolve_address(args):
    if getattr(args, "addr", None):
        return args.addr
    nodes = await discover(want_name=getattr(args, "name", None))
    if not nodes:
        sys.exit("error: no MeshRoute node found advertising. Is ble_mode on + advertising? Try `scan`.")
    addr, name, rssi = nodes[0]
    print(f"  using {name or '(unnamed)'} [{addr}] rssi={rssi}", file=sys.stderr)
    return addr


def _is_auth_error(e):
    """True if an exception is the SoftDevice rejecting an un-bonded link (the §A.3 gate doing its job)."""
    m = str(e).lower()
    return any(k in m for k in ("insufficient authentication", "insufficient encryption",
                                "authenticat", "encryption", "not paired", "access denied"))


def _pairing_help(addr, pin):
    """The actionable message when the link isn't bonded. Pairing a static-PIN MITM peripheral is an OS-level
    ceremony (the OS shows the PIN dialog) — bleak's pair() can't supply the passkey on every backend. So pair
    ONCE via the OS, then reconnect with --no-pair (the bond persists)."""
    return (
        "\n  ⚠ The node requires an encrypted + MITM-bonded link before GATT (spec §A.3) — this is the\n"
        "    firmware's security gate working correctly; the link just isn't bonded yet.\n\n"
        f"    Pair ONCE via your OS (it shows the PIN dialog — enter {pin}), then reconnect with --no-pair:\n"
        f"      Windows: Settings > Bluetooth & devices > Add device > Bluetooth > \"MeshRoute-…\" > PIN {pin}\n"
        f"      Linux:   bluetoothctl -> agent KeyboardDisplay; default-agent; pair {addr} (PIN {pin}); trust {addr}\n"
        f"      macOS:   accept the system PIN prompt ({pin})\n\n"
        "    Then:  python tools/meshroute_client_ble.py --no-pair repl\n"
        "    (Once bonded, the OS reconnects to the bond automatically — no re-PIN.)")


async def _connect(args, on_json):
    """Connect, best-effort pair, subscribe TXD. Returns an open BleakClient (caller disconnects). `on_json`
    is called with each parsed NDJSON object as it arrives. This IS the iOS-reference sequence."""
    BleakClient, _ = _import_bleak()
    addr = await _resolve_address(args)
    # Windows: force a fresh (UNCACHED) GATT read so we don't subscribe against a stale cached characteristic
    # from the Settings bond (a DIY nRF52 whose GATT shifts between flashes is the classic stale-cache case).
    # NB the option lives INSIDE the `winrt=` dict — a bare `use_cached_services=` is silently ignored by bleak.
    client_kwargs = {"winrt": {"use_cached_services": False}} if sys.platform == "win32" else {}
    client = BleakClient(addr, **client_kwargs)
    await client.connect()
    print(f"  connected [{addr}]", file=sys.stderr)

    # Pair (best-effort). The OS / agent supplies the static PIN (see header). macOS auto-pairs on the
    # encrypted-char touch below, so pair() may be a no-op or NotImplementedError there.
    if not getattr(args, "no_pair", False):
        try:
            ok = await client.pair()
            print(f"  pair() -> {ok}  (PIN {args.pin}; enter it if the OS prompts)", file=sys.stderr)
        except NotImplementedError:
            print("  pair() not implemented on this OS — the OS handles pairing on first char access",
                  file=sys.stderr)
        except Exception as e:                                 # noqa: BLE001 — surface, don't crash the session
            print(f"  pair() warning: {e}  (continuing; may already be bonded)", file=sys.stderr)

    buf = LineBuffer()

    def _notify(_sender, data):
        for line in buf.feed(data):
            on_json(parse_json_line(line))

    # Subscribing to the encrypted TXD is the readiness gate (and the trigger for OS pairing) — exactly the
    # iOS contract: the app sends nothing until this succeeds (post-bond). A failure here = not bonded.
    try:
        await client.start_notify(NUS_TX, _notify)
    except Exception as e:                                     # noqa: BLE001
        if _is_auth_error(e):                                  # the §A.3 gate rejected an un-bonded link
            await client.disconnect()
            sys.exit(_pairing_help(addr, args.pin))           # actionable guidance, not a traceback
        raise
    print("  subscribed TXD — link READY", file=sys.stderr)
    return client


async def send_line(client, line):
    # Write WITH response (an ATT Write Request, acknowledged) — the command channel wants reliable delivery,
    # not fire-and-forget. Write-without-response was silently dropped on WinRT (the node never saw the command),
    # while the CCCD subscribe — also a Write Request — worked; that mismatch was the tell. iOS NOTE: use
    # writeValue:forCharacteristic:type:CBCharacteristicWriteWithResponse for RXD.
    data = (line.rstrip("\n") + "\n").encode("utf-8")
    await client.write_gatt_char(NUS_RX, data, response=True)


# ---- subcommands -------------------------------------------------------------------------------------
async def cmd_scan(args):
    nodes = await discover(timeout=args.timeout, want_name=getattr(args, "name", None))
    if not nodes:
        print("no MeshRoute nodes found (advertising NUS). Is a node in range with ble_mode on?")
        return
    print(f"found {len(nodes)} MeshRoute node(s):")
    for addr, name, rssi in nodes:
        print(f"  {addr}  rssi={rssi:>4}  {name or '(unnamed)'}")


async def cmd_monitor(args):
    client = await _connect(args, lambda obj: print(pretty(obj), flush=True))
    try:
        print("  monitoring — Ctrl+C to quit", file=sys.stderr)
        while client.is_connected:
            await asyncio.sleep(0.5)
    finally:
        await client.disconnect()


async def cmd_repl(args):
    client = await _connect(args, lambda obj: print("\r" + pretty(obj), flush=True))
    loop = asyncio.get_running_loop()
    try:
        print("  repl — type a console line (send/whoami/...), Ctrl+D to quit", file=sys.stderr)
        while client.is_connected:
            line = await loop.run_in_executor(None, sys.stdin.readline)
            if not line:                                       # EOF (Ctrl+D)
                break
            line = line.strip()
            if line:
                await send_line(client, line)
    finally:
        await client.disconnect()


async def cmd_send(args):
    got = asyncio.Event()
    result = {}

    def _on(obj):
        print(pretty(obj), flush=True)
        if isinstance(obj, dict) and ("ack" in obj or "err" in obj):
            result.update(obj if isinstance(obj, dict) else {})
            got.set()

    client = await _connect(args, _on)
    try:
        await send_line(client, f"send {args.dst} {args.text}")
        try:
            await asyncio.wait_for(got.wait(), timeout=args.timeout)
        except asyncio.TimeoutError:
            print(f"  (no ack within {args.timeout}s)", file=sys.stderr)
    finally:
        await client.disconnect()


# ---- selftest (no device) ----------------------------------------------------------------------------
def selftest():
    n = 0

    def check(cond, msg):
        nonlocal n
        n += 1
        if not cond:
            print(f"FAIL: {msg}"); sys.exit(1)

    # LineBuffer reassembles split + batched lines, drops blanks, keeps order.
    b = LineBuffer()
    check(b.feed(b'{"ack":"queued"') == [], "partial line yields nothing")
    check(b.feed(b',"ctr":7}\n{"ev":"send_acked","dst":5,"ctr":7}\n') ==
          ['{"ack":"queued","ctr":7}', '{"ev":"send_acked","dst":5,"ctr":7}'], "split+batched reassembly")
    check(b.feed(b"\n\n") == [], "blank lines dropped")

    # pretty() covers every schema variant.
    check("ACKED dst=5 ctr=7" in pretty({"ev": "send_acked", "dst": 5, "ctr": 7}), "send_acked")
    check("RECV from 3" in pretty({"ev": "msg_recv", "origin": 3, "ctr": 7, "body": "hi"}), "msg_recv")
    check("CH2 from 3" in pretty({"ev": "channel_recv", "origin": 3, "channel_id": 2, "body": "hi"}), "channel_recv")
    check("ack queued" in pretty({"ack": "queued", "ctr": 7, "qd": 1}), "ack")
    check("err parse" in pretty({"err": "parse", "code": "unknown_cmd"}), "err")
    check("0x000000ff" in pretty({"ev": "hash_resolved", "node": 4, "auth": 1, "hash": 255}), "hash hex")
    check("READY id=1" in pretty({"ev": "ready", "id": 1, "key": "a1b2c3d4"}), "ready")
    check(parse_json_line("not json") == "not json", "bad json surfaces raw")
    print(f"selftest OK ({n} checks)")


def main(argv=None):
    ap = argparse.ArgumentParser(description="MeshRoute BLE (NUS) client — scan, monitor, drive a node over BLE.")
    ap.add_argument("--selftest", action="store_true", help="run parser checks (no device) and exit")
    ap.add_argument("--pin", default=DEFAULT_PIN, help="pairing passkey (default 123456)")
    ap.add_argument("--addr", help="connect to this BLE address (skip scanning)")
    ap.add_argument("--name", help="match a node whose advert name contains this")
    ap.add_argument("--no-pair", action="store_true", help="skip pair() (device already bonded)")
    sub = ap.add_subparsers(dest="cmd")

    sc = sub.add_parser("scan", help="list advertising MeshRoute nodes")
    sc.add_argument("--timeout", type=float, default=6.0)

    sub.add_parser("monitor", help="connect + stream the node's JSON")
    sub.add_parser("repl", help="interactive: type lines, see JSON")

    ps = sub.add_parser("send", help="one-shot DM: send <dst> <text...>")
    ps.add_argument("dst")
    ps.add_argument("text", nargs=argparse.REMAINDER)
    ps.add_argument("--timeout", type=float, default=10.0, help="seconds to wait for the ack")
    # dispatch is by the `coro` table below (these are async), not argparse func defaults

    args = ap.parse_args(argv)
    if args.selftest:
        return selftest()
    if not getattr(args, "cmd", None):
        ap.print_help(); return
    if args.cmd == "send":
        args.text = " ".join(args.text)
        if not args.text:
            sys.exit("usage: send <dst> <text...>")

    coro = {"scan": cmd_scan, "monitor": cmd_monitor, "repl": cmd_repl, "send": cmd_send}[args.cmd]
    try:
        asyncio.run(coro(args))
    except KeyboardInterrupt:
        print("\n  bye", file=sys.stderr)


if __name__ == "__main__":
    main()
