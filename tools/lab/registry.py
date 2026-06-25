# MeshRoute lab harness — node discovery: serial ports -> [NodeInfo].
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# Identity is by `whoami` id/hash, NEVER the ACM number (hub enumeration is unstable across replug/reboot).
# A port that fails to open OR doesn't answer `whoami` in time is kept as responsive=False (a DEAD row in
# `status`), so the fleet is fully visible. node_id=0 is valid = unprovisioned (provision will assign one).
import sys
import pathlib
import glob
import threading
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))   # so `import meshroute_client` works
from meshroute_client import MeshRouteClient, find_ports   # noqa: E402  (reused: serial link + the USB port list)

from .manager import collect_burst
from . import parsers


class NodeInfo:
    def __init__(self, port, client):
        self.port = port                  # STABLE logical key (set once at discover; the oracle keys its queues/logs by it)
        self.dev = port                   # the CURRENT physical device path; == port until a reattach() moves it (USB re-enumeration)
        self.client = client
        self.lock = threading.Lock()      # writer-lock: one command in flight per node
        self.serial = None                # USB serial number (host-side, from the descriptor) — on the XIAO nRF52840 it's
                                          #   the chip's FICR DEVICEID: STABLE across any firmware/factory reset, so it
                                          #   identifies the physical device even when node_id=0 / hash is freshly reset.
        self.node_id = None
        self.hash = None
        self.leaf = None
        self.gw = False
        self.gwonly = False
        self.mobile = False
        self.name = None
        self.responsive = False
        self.error = None

    def label(self):
        nid = self.node_id if self.node_id is not None else "-"
        return f"{self.port}(id={nid})"


def discover(ports=None, whoami_timeout=2.0):
    """Open each port, `whoami`-probe it, return [NodeInfo] (responsive + DEAD)."""
    if ports is None:
        ports = sorted(glob.glob("/dev/ttyACM*"))
    serials = {}                                                  # device-path -> USB serial number (host-side, no node interaction)
    try:
        for pi in find_ports():
            serials[pi.device] = pi.serial_number
    except Exception:
        pass
    nodes = []
    for p in ports:
        try:
            client = MeshRouteClient(p).open()
        except Exception as e:
            ni = NodeInfo(p, None)
            ni.serial = serials.get(p)
            ni.error = f"open failed: {e}"
            nodes.append(ni)
            continue
        ni = NodeInfo(p, client)
        ni.serial = serials.get(p)
        try:
            w = parsers.parse_whoami(collect_burst(client, "whoami", "[whoami]", timeout=whoami_timeout))
            if w:
                ni.node_id, ni.hash, ni.leaf = w["node_id"], w["hash"], w["leaf"]
                ni.gw, ni.gwonly, ni.mobile, ni.name = w["gw"], w["gwonly"], w["mobile"], w["name"]
                ni.responsive = True
            else:
                ni.error = "no [whoami] response"
        except Exception as e:
            ni.error = f"whoami error: {e}"
        nodes.append(ni)
    return nodes


def reattach(node, exclude=(), timeout=8.0, whoami_timeout=2.0, poll=0.5):
    """A node's serial port dropped + the device RE-ENUMERATES after a moment (a USB-CDC blip, or a reset). Find the
    SAME physical node — matched by its STABLE USB serial number (FICR DEVICEID; survives reset/re-mint), else by a
    whoami id/hash re-probe — on whatever /dev/ttyACM* it now appears, reopen it, and swap the live client into `node`.
    node.port (the harness's logical key, used by the oracle's per-node queues/logs) is kept STABLE; only node.dev +
    node.client (the physical link) move. The caller re-wires the live stream (manager.on_live) on success. `exclude`
    = device paths held by OTHER live nodes (don't probe them — a stray whoami would disturb their stream). Polls until
    `timeout`. Returns True once the node answers whoami again on the fresh link."""
    exclude = set(exclude)
    try:
        if node.client is not None:
            node.client.close()                 # release the stale fd so the kernel frees the old minor / re-enumerates
    except Exception:
        pass
    node.client = None
    want_serial, want_id, want_hash = node.serial, node.node_id, node.hash
    deadline = time.time() + timeout
    while time.time() < deadline:
        serials = {}                             # device path -> USB serial number (host-side descriptor, no node interaction)
        try:
            for pi in find_ports():
                serials[pi.device] = pi.serial_number
        except Exception:
            pass
        # Prefer the device whose USB serial matches (robust to a new ACM number AND a node-side id/hash re-mint);
        # if we never had a serial, fall back to probing every ACM port and matching the whoami id/hash.
        if want_serial:
            candidates = [d for d, s in serials.items() if s == want_serial and d not in exclude]
        else:
            candidates = [d for d in (sorted(serials) or sorted(glob.glob("/dev/ttyACM*"))) if d not in exclude]
        for dev in candidates:
            try:
                client = MeshRouteClient(dev).open()
            except Exception:
                continue                         # busy (another node) / not ready yet -> next
            try:
                w = parsers.parse_whoami(collect_burst(client, "whoami", "[whoami]", timeout=whoami_timeout))
            except Exception:
                w = None
            matched = bool(w) and ((want_serial and serials.get(dev) == want_serial)
                                   or (w["node_id"] == want_id and w["hash"] == want_hash))
            if matched:
                node.dev, node.client = dev, client
                node.node_id, node.hash, node.leaf = w["node_id"], w["hash"], w["leaf"]
                node.gw, node.gwonly, node.mobile, node.name = w["gw"], w["gwonly"], w["mobile"], w["name"]
                node.responsive, node.error = True, None
                return True
            try:
                client.close()
            except Exception:
                pass
        time.sleep(poll)
    return False


def _selftest():
    """Offline (no device): monkeypatch the serial layer + exercise reattach()'s serial-match / whoami-fallback /
    exclude / timeout + the stable-port-key invariant.  Run from tools/:  `python3 -m lab.registry`"""
    import types
    g = globals()
    saved = {k: g[k] for k in ("find_ports", "MeshRouteClient")}
    saved_pw = parsers.parse_whoami
    saved_cb = sys.modules[__name__].collect_burst
    try:
        devs = {"/dev/ttyACM5": "S170", "/dev/ttyACM6": "S999"}        # 170 re-appeared on ACM5 (it was on ACM3)
        ident = {"/dev/ttyACM5": dict(node_id=170, hash="abc", leaf=1, gw=False, gwonly=False, mobile=False, name=""),
                 "/dev/ttyACM6": dict(node_id=99,  hash="zzz", leaf=1, gw=False, gwonly=False, mobile=False, name="")}
        g["find_ports"] = lambda: [types.SimpleNamespace(device=d, serial_number=s) for d, s in devs.items()]
        class _FC:
            def __init__(self, dev): self.dev = dev
            def open(self): return self
            def close(self): pass
        g["MeshRouteClient"] = _FC
        g["collect_burst"] = lambda client, *a, **k: [client.dev]      # carry the device path through to parse_whoami
        parsers.parse_whoami = lambda lines: ident.get(lines[0]) if lines else None

        n = NodeInfo("/dev/ttyACM3", None); n.serial, n.node_id, n.hash = "S170", 170, "abc"
        assert reattach(n, timeout=1.0, poll=0.01) is True
        assert n.dev == "/dev/ttyACM5" and n.port == "/dev/ttyACM3" and n.responsive   # dev moved, the port KEY stayed stable

        n2 = NodeInfo("/dev/ttyACM3", None); n2.serial, n2.node_id, n2.hash = "S170", 170, "abc"
        assert reattach(n2, exclude={"/dev/ttyACM5"}, timeout=0.3, poll=0.01) is False  # its device excluded -> not found

        n3 = NodeInfo("/dev/ttyACM3", None); n3.serial, n3.node_id, n3.hash = None, 99, "zzz"
        assert reattach(n3, timeout=1.0, poll=0.01) is True and n3.dev == "/dev/ttyACM6"  # no serial -> whoami id/hash match
        print("registry reattach selftest OK")
    finally:
        for k, v in saved.items():
            g[k] = v
        parsers.parse_whoami = saved_pw
        g["collect_burst"] = saved_cb


if __name__ == "__main__":
    _selftest()
