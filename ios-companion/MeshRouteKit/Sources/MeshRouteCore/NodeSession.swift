// MeshRouteCore — NodeSession.swift
//
// The typed conversation with a node, on top of any NodeLink. It encodes Command → line on the
// way out and decodes line → Inbound (via the wire PushDecoder) on the way in, surfacing a single
// `events` stream the app's view model consumes. An actor: no UI assumptions, fully testable.

import Foundation
import MeshRouteWire

public enum SessionEvent: Sendable {
    case link(LinkState)
    case inbound(Inbound)
}

public actor NodeSession {
    private let link: NodeLink
    public nonisolated let events: AsyncStream<SessionEvent>
    private let continuation: AsyncStream<SessionEvent>.Continuation
    private var pump: Task<Void, Never>?
    public private(set) var state: LinkState = .disconnected

    public init(link: NodeLink) {
        self.link = link
        (self.events, self.continuation) = AsyncStream<SessionEvent>.makeStream()
        Task { await self.start() }
    }

    private func start() {
        pump = Task { [link, continuation] in
            for await ev in link.events {
                switch ev {
                case .state(let s):
                    self.setState(s)               // pump Task inherits actor isolation → same-actor call
                    continuation.yield(.link(s))
                case .line(let line):
                    if let inbound = PushDecoder.decode(line: line) {
                        continuation.yield(.inbound(inbound))
                    }
                }
            }
            continuation.finish()
        }
    }

    private func setState(_ s: LinkState) { state = s }

    public func connect() async { await link.connect() }
    public func disconnect() async { await link.disconnect() }

    /// Encode + send a typed command. Throws if the body overflows the node's inner buffer.
    public func send(_ command: Command) async throws {
        guard command.bodyFits else { throw SessionError.bodyTooLong(limit: command.bodyByteLimit ?? 0) }
        try await link.send(line: command.line)
    }

    /// Escape hatch for the debug console: send a raw line as-is.
    public func sendRaw(_ line: String) async throws { try await link.send(line: line) }

    deinit { pump?.cancel() }
}

public enum SessionError: Error, Sendable {
    case bodyTooLong(limit: Int)
}
