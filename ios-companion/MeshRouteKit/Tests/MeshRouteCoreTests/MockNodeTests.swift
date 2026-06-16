// MeshRouteCoreTests — MockNodeTests.swift
// The fake node speaks the real contract: line-ASCII commands in → JSON pushes out. These tests
// drive it the way the app will, proving the whole stack works with no firmware/BLE.

import XCTest
@testable import MeshRouteCore
import MeshRouteWire

/// Drain `.line` events from a link into decoded Inbound values until `count` lines arrive.
/// Safe because the mock yields synchronously during the awaited connect()/send() calls (unbounded
/// buffer), so a single iterator drains them deterministically.
private func drainInbound(_ link: NodeLink, until count: Int) async -> [Inbound] {
    var out: [Inbound] = []
    for await ev in link.events {
        if case .line(let l) = ev, let inb = PushDecoder.decode(line: l) {
            out.append(inb)
            if out.count >= count { break }
        }
    }
    return out
}

final class MockNodeTests: XCTestCase {

    func testConnectGreetsWithReady() async throws {
        let mock = MockNodeLink()
        await mock.connect()
        let inbound = await drainInbound(mock, until: 1)
        guard case .ready(let r) = inbound[0] else { return XCTFail("expected ready, got \(inbound)") }
        XCTAssertEqual(r.id, 1)
        XCTAssertEqual(r.routingSF, 7)
    }

    func testSendGetsQueuedAckThenSendAcked() async throws {
        let mock = MockNodeLink()
        await mock.connect()                            // → ready
        try await mock.send(line: "send 2 hi")          // → ack(queued), send_acked
        let inbound = await drainInbound(mock, until: 3) // ready, ack, send_acked
        guard case .ack(let ack) = inbound[1] else { return XCTFail("expected ack, got \(inbound)") }
        XCTAssertEqual(ack.code, .queued)
        guard case .sendAcked(let dst, let ctr) = inbound[2] else { return XCTFail("expected send_acked") }
        XCTAssertEqual(dst, 2)
        XCTAssertEqual(ctr, ack.ctr)                    // the link ack references the queued send's ctr
    }

    func testSendToUnknownIDErrors() async throws {
        let mock = MockNodeLink()
        await mock.connect()
        try await mock.send(line: "send 0 hi")          // id 0 = unknown dst in the mock
        let inbound = await drainInbound(mock, until: 2) // ready, ack(err)
        guard case .ack(let ack) = inbound[1] else { return XCTFail() }
        XCTAssertEqual(ack.code, .errUnknownDst)
        XCTAssertTrue(ack.code.isError)
    }

    func testResolveKnownHash() async throws {
        let mock = MockNodeLink()
        await mock.connect()
        try await mock.send(line: "resolve 8a3f1c02")
        let inbound = await drainInbound(mock, until: 2) // ready, hash_resolved
        guard case .hashResolved(let node, let auth, let hash) = inbound[1] else { return XCTFail() }
        XCTAssertEqual(node, 2)
        XCTAssertTrue(auth)
        XCTAssertEqual(hash.hex8, "8a3f1c02")
    }

    func testSimulatedIncomingDMDecodes() async throws {
        let mock = MockNodeLink()
        await mock.connect()
        await mock.simulateIncomingDM(fromID: 2, body: "ping")
        let inbound = await drainInbound(mock, until: 2) // ready, msg_recv
        guard case .messageReceived(let origin, let ctr, _, _, _, _, let body) = inbound[1] else { return XCTFail() }
        XCTAssertEqual(origin, 2); XCTAssertEqual(ctr, 1); XCTAssertEqual(body, "ping")
    }

    // ---- through the full NodeSession pump (encode + decode), with a timeout-bounded poll ----

    func testSessionRoundTrip() async throws {
        let mock = MockNodeLink()
        let session = NodeSession(link: mock)
        let collector = EventCollector()
        let consume = Task { for await ev in session.events { await collector.append(ev) } }
        defer { consume.cancel() }

        await session.connect()
        try await session.send(.sendDM(.init(target: .id(2), body: "hi")))

        let sawAck = await collector.waitUntil(timeout: 2.0) { events in
            events.contains { if case .inbound(.ack(let a)) = $0 { return a.code == .queued } else { return false } }
        }
        let sawAcked = await collector.waitUntil(timeout: 2.0) { events in
            events.contains { if case .inbound(.sendAcked) = $0 { return true } else { return false } }
        }
        XCTAssertTrue(sawAck, "expected a queued ack through the session pump")
        XCTAssertTrue(sawAcked, "expected a send_acked through the session pump")
    }

    func testSessionRejectsOverlongBody() async throws {
        let mock = MockNodeLink()
        let session = NodeSession(link: mock)
        await session.connect()
        let tooBig = String(repeating: "a", count: WireConstants.dmMaxBodyBytes + 1)
        do {
            try await session.send(.sendDM(.init(target: .id(2), body: tooBig)))
            XCTFail("expected bodyTooLong")
        } catch SessionError.bodyTooLong { /* expected */ }
    }
}

/// Collects session events off the actor and polls a predicate with a timeout (no flaky fixed sleeps).
actor EventCollector {
    private(set) var events: [SessionEvent] = []
    func append(_ e: SessionEvent) { events.append(e) }
    func waitUntil(timeout: TimeInterval, _ predicate: @Sendable ([SessionEvent]) -> Bool) async -> Bool {
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            if predicate(events) { return true }
            try? await Task.sleep(nanoseconds: 15_000_000)
        }
        return predicate(events)
    }
}
