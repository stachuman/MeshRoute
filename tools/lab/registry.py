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

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))   # so `import meshroute_client` works
from meshroute_client import MeshRouteClient   # noqa: E402  (reused: serial + reader thread + _rxq/_on_line)

from .manager import collect_burst
from . import parsers


class NodeInfo:
    def __init__(self, port, client):
        self.port = port
        self.client = client
        self.lock = threading.Lock()      # writer-lock: one command in flight per node
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
    nodes = []
    for p in ports:
        try:
            client = MeshRouteClient(p).open()
        except Exception as e:
            ni = NodeInfo(p, None)
            ni.error = f"open failed: {e}"
            nodes.append(ni)
            continue
        ni = NodeInfo(p, client)
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
