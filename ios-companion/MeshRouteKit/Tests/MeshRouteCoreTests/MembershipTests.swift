// MeshRouteCoreTests — MembershipTests.swift
// LeafMembership state machine (R6 / D26): unmanaged · joining · member + chip labels.

import XCTest
@testable import MeshRouteCore

final class MembershipTests: XCTestCase {

    func testUnmanaged() {
        let m = LeafMembership.adopted(lineage: 0, epoch: 0, leaf: nil, level: nil)
        XCTAssertEqual(m.state, .unmanaged)
        XCTAssertFalse(m.isManaged)
        XCTAssertEqual(m.label, "Unmanaged · standalone")
    }

    func testJoining() {
        // managed (lineage != 0) but not yet synced (epoch 0) → joining
        let m = LeafMembership(lineage: 41153, epoch: 0, leaf: "north field", level: 2, synced: false)
        XCTAssertEqual(m.state, .joining)
        XCTAssertTrue(m.isManaged)
        XCTAssertEqual(m.label, "Joining north field…")
    }

    func testMember() {
        let m = LeafMembership.adopted(lineage: 41153, epoch: 3, leaf: "north field", level: 2)
        XCTAssertEqual(m.state, .member)        // synced derived true (epoch > 0)
        XCTAssertEqual(m.label, "Member of north field")
    }

    func testMemberFallsBackToLevelWhenLeafless() {
        let m = LeafMembership.adopted(lineage: 41153, epoch: 3, leaf: nil, level: 5)
        XCTAssertEqual(m.label, "Member of leaf 5")
    }
}
