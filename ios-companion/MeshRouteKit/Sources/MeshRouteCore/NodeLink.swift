// MeshRouteCore — NodeLink.swift
//
// The transport seam. A NodeLink moves raw '\n'-delimited lines to/from a node and reports
// connection state — nothing more. The CoreBluetooth NUS transport (in the app) and the
// MockNodeLink (here) both implement it, so NodeSession is identical on top of either.
// Mirrors the firmware's discipline: one core, swappable backends.

import Foundation

public enum LinkState: Hashable, Sendable {
    case disconnected
    case scanning            // looking for the node's advertisement
    case waitingForWindow    // node is `periodic`; waiting for its next advertising window
    case connecting
    case pairing             // exchanging the PIN / bonding
    case connected
    case failed(String)
}

public enum LinkEvent: Sendable {
    case state(LinkState)
    case line(String)        // one decoded inbound line (no terminator)
}

/// A bidirectional line link to a node. `events` is a single-consumer stream owned by the session.
public protocol NodeLink: Sendable {
    var events: AsyncStream<LinkEvent> { get }
    func connect() async
    func disconnect() async
    /// Write one command line. The link appends the '\n'.
    func send(line: String) async throws
}

public enum LinkError: Error, Sendable {
    case notConnected
    case writeFailed(String)
}
