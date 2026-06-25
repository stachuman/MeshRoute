# MeshRoute lab harness — multi-node orchestration over N MeshRouteClient serial links.
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# One writer-lock per node (the console is line-oriented: never two commands in flight to one node). Reads
# the client's _rxq (queue mode) for the quiet-burst response collection. Live callbacks (on_live) set the
# client's _on_line for streaming RECV/CH/ACKED in later phases — NOT while a request() is mid-flight.
import time
from queue import Empty
from concurrent.futures import ThreadPoolExecutor, as_completed


def collect_burst(client, cmd, expect, timeout=2.0, tail_quiet=0.2, tail_max=1.5):
    """Marker-aware burst collect (Amendment 2). Send `cmd`, then:
      1) collect lines until one CONTAINS `expect` (the real response arrived) OR `timeout`,
      2) then collect the tail until `tail_quiet` s of silence (the rest of a multi-line reply like [cfg]),
         bounded by `tail_max`.
    Robust to unsolicited lines (beacons / DAD traces) that PRECEDE the response during provisioning churn —
    the old first-quiet-gap logic returned that noise with no marker line. If `expect` never arrives within
    `timeout`, return whatever was collected (the caller's parser yields None = a transient read-miss, retried
    next poll — NOT a hard failure). Requires queue mode (client._on_line is None)."""
    client._flush()
    client.send_line(cmd)
    lines, deadline, got = [], time.time() + timeout, False
    while time.time() < deadline:                              # phase 1: wait for the marker
        try:
            _, line = client._rxq.get(timeout=max(0.0, deadline - time.time()))
        except Empty:
            break
        lines.append(line)
        if expect in line:
            got = True
            break
    if not got:
        return lines                                          # marker never came -> transient miss
    tail_deadline = time.time() + tail_max
    while time.time() < tail_deadline:                        # phase 2: the rest of the (multi-line) reply
        try:
            _, line = client._rxq.get(timeout=tail_quiet)
        except Empty:
            break
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

    def request(self, node, cmd, expect, timeout=2.0):
        """One command in flight per node (writer-lock); marker-aware (Amendment 2: `expect` = the substring
        that marks the real response, e.g. '[cfg]' / '[whoami]' / '> '). Returns the raw response-burst lines (or []
        if the port dropped mid-exchange — the node is marked DEAD, never crashes the caller)."""
        with node.lock:
            if node.client is None:
                return []
            if node.client._on_line is not None:
                raise RuntimeError(f"{node.port}: client in live mode — stop_live() before request()")
            try:
                return collect_burst(node.client, cmd, expect, timeout)
            except Exception as e:                       # serial.SerialException / OSError / termios.error -> port gone
                node.responsive = False
                node.error = f"request: {e}"
                return []

    def send(self, node, line):
        """Write-only under the writer-lock, for use IN LIVE MODE (no collect — the ack/RECV/CH arrive async on the
        live stream). One command in flight per node still holds (the lock). A serial-write failure (the USB-CDC link
        dropped — the node reset/re-enumerated, a hub glitch, or the USB-CDC wedge) marks the node DEAD and is
        SWALLOWED: a lost port must NOT crash the fleet (the same resilience broadcast() has). Returns True on write."""
        with node.lock:
            if node.client is None:
                return False
            try:
                node.client.send_line(line)
                return True
            except Exception as e:                       # serial.SerialException / OSError / termios.error -> port gone
                node.responsive = False
                node.error = f"send: {e}"
                return False

    def broadcast(self, cmd, expect, timeout=2.0, nodes=None):
        """request() to every (or `nodes`) responsive node CONCURRENTLY. Returns {node.port: lines}
        (keyed by port, NOT node_id — unprovisioned nodes share node_id=0)."""
        targets = nodes if nodes is not None else self.responsive()
        out = {}
        if not targets:
            return out
        with ThreadPoolExecutor(max_workers=min(self._max_workers, len(targets))) as ex:
            futs = {ex.submit(self.request, n, cmd, expect, timeout): n for n in targets}
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


# --------------------------------------------------------------------------------------------------
# Offline self-test (no device): `python3 tools/lab/manager.py`  — Amendment 2 marker-aware collection.
# --------------------------------------------------------------------------------------------------
def _selftest():
    from queue import Queue

    class _FakeClient:                       # send_line enqueues a canned device reply; reader is the queue
        def __init__(self, reply):
            self._reply = reply
            self._rxq = Queue()
            self._on_line = None

        def _flush(self):
            while not self._rxq.empty():
                try:
                    self._rxq.get_nowait()
                except Empty:
                    break

        def send_line(self, cmd):
            for ln in self._reply:
                self._rxq.put((0.0, ln))

    # an unsolicited BCN/trace line PRECEDES the [cfg] reply (the churn case the old logic dropped)
    reply = [
        " t=1234 ms «rx BCN from=222 n=0 flags= len=14 sf=9",            # noise before the response
        "[cfg] node_id=73",
        "      radio : freq=869.0000 routing_sf=8 sf_list=7,9 bw=125000 cr=5 tx_power=22",
        "      leaf  : leaf_id=1 level_id=1 (→nibble 1) gateway=0 gateway_only=0 mobile=0",
    ]
    lines = collect_burst(_FakeClient(reply), "cfg", "[cfg]", timeout=1.0)
    assert any(l.startswith("[cfg]") for l in lines), lines          # marker line captured despite the leading BCN
    assert any("sf_list=7,9" in l for l in lines), lines             # the multi-line tail captured too

    # marker never arrives -> return what we got (transient miss), do NOT hang past timeout
    only_noise = collect_burst(_FakeClient([" t=1 ms «rx BCN ..."]), "cfg", "[cfg]", timeout=0.4)
    assert not any(l.startswith("[cfg]") for l in only_noise), only_noise
    print("manager selftest OK")


if __name__ == "__main__":
    _selftest()
