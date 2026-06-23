# MeshRoute lab harness — multi-node orchestration over N MeshRouteClient serial links.
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# One writer-lock per node (the console is line-oriented: never two commands in flight to one node). Reads
# the client's _rxq (queue mode) for the quiet-burst response collection. Live callbacks (on_live) set the
# client's _on_line for streaming RECV/CH/ACKED in later phases — NOT while a request() is mid-flight.
import time
from queue import Empty
from concurrent.futures import ThreadPoolExecutor, as_completed


def collect_burst(client, cmd, timeout=2.0, quiet=0.25):
    """Send `cmd`; return the response burst (raw lines) until ~`quiet` s of silence AFTER the first line,
    or the overall `timeout` (dead-node guard). Requires queue mode (client._on_line is None)."""
    client._flush()
    client.send_line(cmd)
    lines, deadline = [], time.time() + timeout
    while time.time() < deadline:
        try:
            _, line = client._rxq.get(timeout=min(quiet, max(0.0, deadline - time.time())))
        except Empty:
            if lines:
                break          # got the burst, then a quiet gap -> done
            continue           # nothing yet -> keep waiting until the overall timeout
        lines.append(line)
    return lines


class NodeManager:
    """Wraps the registry's [NodeInfo] (duck-typed: .client, .lock, .port, .node_id, .responsive)."""

    def __init__(self, nodes, max_workers=12):
        self.nodes = nodes
        self._max_workers = max_workers

    def responsive(self):
        return [n for n in self.nodes if n.responsive]

    def by_id(self, node_id):
        return next((n for n in self.nodes if n.responsive and n.node_id == node_id), None)

    def request(self, node, cmd, timeout=2.0):
        """One command in flight per node (writer-lock); returns the raw response-burst lines."""
        with node.lock:
            if node.client is None:
                return []
            if node.client._on_line is not None:
                raise RuntimeError(f"{node.port}: client in live mode — stop_live() before request()")
            return collect_burst(node.client, cmd, timeout)

    def broadcast(self, cmd, timeout=2.0, nodes=None):
        """request() to every (or `nodes`) responsive node CONCURRENTLY. Returns {node.port: lines}
        (keyed by port, NOT node_id — unprovisioned nodes share node_id=0)."""
        targets = nodes if nodes is not None else self.responsive()
        out = {}
        if not targets:
            return out
        with ThreadPoolExecutor(max_workers=min(self._max_workers, len(targets))) as ex:
            futs = {ex.submit(self.request, n, cmd, timeout): n for n in targets}
            for f in as_completed(futs):
                n = futs[f]
                try:
                    out[n.port] = f.result()
                except Exception as e:                 # a hung/dead node must not stall the fleet
                    out[n.port] = [f"<error: {e}>"]
        return out

    # ---- live streaming (later phases: RECV / CH / ACKED) ----------------------------------------
    def on_live(self, node, cb):
        node.client._on_line = lambda ln: cb(node, time.time(), ln)

    def live_all(self, cb):
        for n in self.responsive():
            self.on_live(n, cb)

    def stop_live(self, node=None):
        for n in ([node] if node else self.nodes):
            if n.client is not None:
                n.client._on_line = None

    def close(self):
        for n in self.nodes:
            if n.client is not None:
                try:
                    n.client.close()
                except Exception:
                    pass

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()
