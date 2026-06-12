// MeshRouteCore — NodeTimeAnchor.swift
//
// The node has no RTC: inbox records carry rx_ms = node UPTIME at receive. The `ready` / `inbox_end`
// JSON now carries now_ms (uptime at emit); pairing it with the phone's wall clock at decode time
// anchors every pulled record:  wall(rx_ms) = capturedAt − (nodeNowMs − rx_ms).
// Drift within one connection is negligible; a node reboot resets uptime AND bumps the inbox epoch,
// so an anchor never spans a reboot (the epoch-reset re-pull arrives under a fresh anchor).

import Foundation

public struct NodeTimeAnchor: Hashable, Sendable {
    public let nodeNowMs: UInt64    // node uptime when the anchor was taken
    public let capturedAt: Date     // the phone's wall clock at that same moment

    public init(nodeNowMs: UInt64, capturedAt: Date = .now) {
        self.nodeNowMs = nodeNowMs
        self.capturedAt = capturedAt
    }

    /// Wall-clock time for a record received at node-uptime `rxMs`. An rxMs in the anchor's "future"
    /// (stale anchor / clock weirdness) clamps to the anchor time rather than inventing one.
    public func wallClock(rxMs: UInt64) -> Date {
        guard rxMs <= nodeNowMs else { return capturedAt }
        return capturedAt.addingTimeInterval(-Double(nodeNowMs - rxMs) / 1000)
    }
}
