// MeshRouteWireTests — NodeInfoWireTests.swift
// Pin the Theme-D Node/Network wire contract: enriched status, the routes stream, and cfg.

import XCTest
@testable import MeshRouteWire

final class NodeInfoWireTests: XCTestCase {

    func testStatusCommandsEncode() {
        XCTAssertEqual(Command.status.line, "status")
        XCTAssertEqual(Command.routes.line, "routes")
        XCTAssertEqual(Command.config.line, "cfg")
    }

    func testEnrichedStatusDecodes() {
        guard case .status(let s)? = PushDecoder.decode(
            line: #"{"ev":"status","id":5,"key":"ff609d5c","state":"operating","leaf_id":0,"gateway":false,"routing_sf":7,"uptime_ms":123456,"duty_ms":1240,"txq":0,"txdrop":0,"rx":42,"tx":17,"routes":3,"pending":false,"lbt":true,"batt_mv":4050}"#) else {
            return XCTFail("not status")
        }
        XCTAssertEqual(s.id, 5)
        XCTAssertEqual(s.uptimeMs, 123456)
        XCTAssertEqual(s.routes, 3)
        XCTAssertEqual(s.battMv, 4050)
        XCTAssertEqual(s.lbt, true)
        XCTAssertEqual(s.pending, false)
    }

    func testTerseStatusStillDecodes() {     // an older node omits the runtime fields
        guard case .status(let s)? = PushDecoder.decode(
            line: #"{"ev":"status","id":5,"key":"ff609d5c","state":"up","leaf_id":0,"gateway":false,"routing_sf":7}"#) else {
            return XCTFail("not status")
        }
        XCTAssertEqual(s.id, 5)
        XCTAssertNil(s.uptimeMs)
        XCTAssertNil(s.battMv)               // no battery field → nil, UI hides the row
    }

    func testRouteStreamDecodes() {
        guard case .route(let r)? = PushDecoder.decode(
            line: #"{"ev":"route","dest":2,"next":4,"hops":2,"score":-48,"gw":true,"leaf":7,"age_ms":5000,"cand":1}"#) else {
            return XCTFail("not route")
        }
        XCTAssertEqual(r.dest, 2); XCTAssertEqual(r.next, 4); XCTAssertEqual(r.hops, 2)
        XCTAssertEqual(r.score, -48); XCTAssertTrue(r.gw); XCTAssertEqual(r.leaf, 7)

        guard case .routesEnd(let count)? = PushDecoder.decode(line: #"{"ev":"routes_end","count":3}"#) else {
            return XCTFail("not routes_end")
        }
        XCTAssertEqual(count, 3)
    }

    func testCfgDecodesAndDerives() {
        guard case .cfg(let c)? = PushDecoder.decode(
            line: #"{"ev":"cfg","node_id":5,"freq_hz":869462500,"routing_sf":7,"sf_list":"7,12","bw_hz":125000,"cr":5,"tx_power":22,"duty_x1000":100,"lbt":true,"beacon_ms":900000,"hop_cap":16,"leaf_id":0,"gateway":false,"mobile":false,"ble_mode":"on","ble_period":15,"ble_pin":123456,"lat_e7":522297000,"lon_e7":-41000000}"#) else {
            return XCTFail("not cfg")
        }
        XCTAssertEqual(c.nodeID, 5)
        XCTAssertEqual(c.sfList, "7,12")
        XCTAssertEqual(c.freqMHz, 869.4625, accuracy: 0.0001)   // freq_hz → MHz
        XCTAssertEqual(c.dutyPercent, 10.0, accuracy: 0.001)    // duty_x1000 → %
        XCTAssertEqual(c.bleMode, "on")
        XCTAssertTrue(c.hasPosition)
        XCTAssertEqual(c.latitude, 52.2297, accuracy: 1e-6)     // lat_e7 → degrees
        XCTAssertEqual(c.longitude, -4.1, accuracy: 1e-6)       // signed
    }

    func testConfigAdoptedAndJoinRefused() {     // R6 / D26
        guard case .configAdopted(let lineage, let epoch, let leaf, let layer)? = PushDecoder.decode(
            line: #"{"ev":"config_adopted","lineage":41153,"epoch":3,"leaf":"north field","layer":2}"#) else {
            return XCTFail("not config_adopted")
        }
        XCTAssertEqual(lineage, 41153); XCTAssertEqual(epoch, 3); XCTAssertEqual(leaf, "north field"); XCTAssertEqual(layer, 2)
        guard case .joinRefused(let r, let their, let my)? = PushDecoder.decode(
            line: #"{"ev":"join_refused","reason":"wire_version","their_ver":2,"my_ver":1}"#) else {
            return XCTFail("not join_refused")
        }
        XCTAssertEqual(r, "wire_version"); XCTAssertEqual(their, 2); XCTAssertEqual(my, 1)
        // leaf_full carries no versions
        guard case .joinRefused(let r2, let their2, _)? = PushDecoder.decode(
            line: #"{"ev":"join_refused","reason":"leaf_full"}"#) else { return XCTFail() }
        XCTAssertEqual(r2, "leaf_full"); XCTAssertNil(their2)
    }

    func testReadyMembershipFields() {           // R6 / D26: ready carries lineage/epoch/leaf/level/synced
        guard case .ready(let r)? = PushDecoder.decode(
            line: #"{"ev":"ready","id":1,"key":"8a3f1c02","leaf_id":2,"mode":"node","gateway":false,"routing_sf":7,"lineage":41153,"epoch":3,"leaf":"north field","layer":2,"synced":true}"#) else {
            return XCTFail("not ready")
        }
        XCTAssertEqual(r.lineage, 41153); XCTAssertEqual(r.configEpoch, 3)
        XCTAssertEqual(r.leaf, "north field"); XCTAssertEqual(r.layer, 2); XCTAssertEqual(r.synced, true)
        // pre-R6 firmware omits them → nil (membership unknown)
        guard case .ready(let old)? = PushDecoder.decode(
            line: #"{"ev":"ready","id":1,"key":"8a3f1c02","leaf_id":0,"mode":"node","gateway":false,"routing_sf":7}"#) else { return XCTFail() }
        XCTAssertNil(old.lineage); XCTAssertNil(old.synced)
    }

    func testDutyReadout() {     // D27
        guard case .duty(let pct, let avail, let enabled)? = PushDecoder.decode(
            line: #"{"ev":"duty","pct":100,"avail_ms":73000,"enabled":true}"#) else {
            return XCTFail("not duty")
        }
        XCTAssertEqual(pct, 100); XCTAssertEqual(avail, 73000); XCTAssertTrue(enabled)   // silent + countdown
        guard case .duty(_, _, let off)? = PushDecoder.decode(line: #"{"ev":"duty","pct":0,"avail_ms":0,"enabled":false}"#) else { return XCTFail() }
        XCTAssertFalse(off)   // unlimited
        // ready seeds a starting duty value on connect
        guard case .ready(let r)? = PushDecoder.decode(
            line: #"{"ev":"ready","id":1,"key":"8a3f1c02","leaf_id":0,"mode":"node","gateway":false,"routing_sf":7,"duty_pct":42,"duty_avail_ms":0}"#) else { return XCTFail() }
        XCTAssertEqual(r.dutyPct, 42); XCTAssertEqual(r.dutyAvailMs, 0)
    }

    func testCfgWithoutLocationDecodes() {     // older firmware: no lat_e7/lon_e7 → hasPosition false
        guard case .cfg(let c)? = PushDecoder.decode(
            line: #"{"ev":"cfg","node_id":5,"freq_hz":869462500,"routing_sf":7,"sf_list":"7,12","bw_hz":125000,"cr":5,"tx_power":22,"duty_x1000":100,"lbt":true,"beacon_ms":900000,"hop_cap":16,"leaf_id":0,"gateway":false,"mobile":false,"ble_mode":"on","ble_period":15,"ble_pin":123456}"#) else {
            return XCTFail("not cfg")
        }
        XCTAssertFalse(c.hasPosition)
    }
}
