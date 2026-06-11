// MeshRouteCompanion — BLENodeLink.swift
//
// The real transport: a CoreBluetooth central that connects to the node's Nordic UART Service
// (bleuart), writes command lines to the RX characteristic, and reassembles TX notifications into
// '\n'-delimited lines via the shared LineAccumulator. Implements the same NodeLink seam as the
// MockNodeLink, so nothing above it changes.
//
// Pairing: the node serves its GATT only over an encrypted, bonded link. iOS shows the system
// passkey (PIN) prompt automatically the first time we touch an encrypted characteristic — we don't
// handle the PIN ourselves. Bonding then persists.
//
// NOTE: CoreBluetooth needs real BLE hardware — in the iOS Simulator the manager reports
// `.unsupported` and this link emits `.failed`. Use MockNodeLink there (AppModel defaults to it).

import Foundation
import CoreBluetooth
import MeshRouteWire
import MeshRouteCore

final class BLENodeLink: NSObject, NodeLink, @unchecked Sendable {
    let events: AsyncStream<LinkEvent>
    private let continuation: AsyncStream<LinkEvent>.Continuation

    private let serviceUUID = CBUUID(string: WireConstants.nusServiceUUID)
    private let rxUUID = CBUUID(string: WireConstants.nusRXCharacteristicUUID)  // app → node (write)
    private let txUUID = CBUUID(string: WireConstants.nusTXCharacteristicUUID)  // node → app (notify)

    private let queue = DispatchQueue(label: "meshroute.ble", qos: .userInitiated)
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var rxChar: CBCharacteristic?
    private var accumulator = LineAccumulator()
    private var wantConnect = false

    override init() {
        (events, continuation) = AsyncStream<LinkEvent>.makeStream()
        super.init()
        central = CBCentralManager(delegate: self, queue: queue)
    }

    // ---- NodeLink ----

    func connect() async {
        queue.async { [self] in
            wantConnect = true
            startScanIfReady()
        }
    }

    func disconnect() async {
        queue.async { [self] in
            wantConnect = false
            if let p = peripheral { central.cancelPeripheralConnection(p) }
            central.stopScan()
            emit(.state(.disconnected))
        }
    }

    func send(line: String) async throws {
        let payload = Data((line + "\n").utf8)
        try await withCheckedThrowingContinuation { (cont: CheckedContinuation<Void, Error>) in
            queue.async { [self] in
                guard let p = peripheral, let rx = rxChar else {
                    cont.resume(throwing: LinkError.notConnected); return
                }
                // Chunk to the negotiated ATT MTU so long DM bodies aren't truncated.
                let maxLen = max(20, p.maximumWriteValueLength(for: .withoutResponse))
                var offset = 0
                while offset < payload.count {
                    let end = min(offset + maxLen, payload.count)
                    p.writeValue(payload.subdata(in: offset..<end), for: rx, type: .withoutResponse)
                    offset = end
                }
                cont.resume()
            }
        }
    }

    // ---- helpers (all on `queue`) ----

    private func emit(_ event: LinkEvent) { continuation.yield(event) }

    private func startScanIfReady() {
        guard wantConnect, central.state == .poweredOn, peripheral == nil else { return }
        emit(.state(.scanning))
        // Service-UUID-filtered scan (required for iOS background scanning + the right way to find a
        // periodic/windowed node). REQUIRES the firmware to advertise the NUS service UUID in its
        // advertising packet (Bluefruit: `Advertising.addService(bleuart)`) — a name-only advert won't
        // match this filter. See docs/superpowers/specs/2026-06-11-ble-companion-phase2-scope.md Step 5/8.
        central.scanForPeripherals(withServices: [serviceUUID], options: nil)
    }
}

extension BLENodeLink: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn: startScanIfReady()
        case .unsupported: emit(.state(.failed("Bluetooth unavailable (Simulator?) — use the mock node")))
        case .unauthorized: emit(.state(.failed("Bluetooth permission denied")))
        case .poweredOff: emit(.state(.failed("Bluetooth is off")))
        default: break
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        central.stopScan()
        self.peripheral = peripheral
        peripheral.delegate = self
        emit(.state(.connecting))
        central.connect(peripheral, options: nil)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        emit(.state(.pairing))                       // touching encrypted chars triggers the iOS PIN prompt
        peripheral.discoverServices([serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        emit(.state(.failed(error?.localizedDescription ?? "connect failed")))
        self.peripheral = nil
        startScanIfReady()
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        self.peripheral = nil; self.rxChar = nil
        accumulator = LineAccumulator()
        emit(.state(.disconnected))
        if wantConnect { startScanIfReady() }        // periodic node: reconnect on its next window
    }
}

extension BLENodeLink: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error { emit(.state(.failed("service discovery failed: \(error.localizedDescription)"))); return }
        guard let svc = peripheral.services?.first(where: { $0.uuid == serviceUUID }) else {
            emit(.state(.failed("node has no bleuart service"))); return
        }
        peripheral.discoverCharacteristics([rxUUID, txUUID], for: svc)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        if let error { emit(.state(.failed("characteristic discovery failed: \(error.localizedDescription)"))); return }
        for ch in service.characteristics ?? [] {
            if ch.uuid == rxUUID { rxChar = ch }
            if ch.uuid == txUUID { peripheral.setNotifyValue(true, for: ch) }   // touching the encrypted char triggers pairing
        }
        // We report `.connected` only once the TX subscription CONFIRMS (post-pairing) — see below — so the
        // app never writes before the bonded link + notify pipe are live (firmware §A.3 gates GATT on bonding).
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        guard characteristic.uuid == txUUID else { return }
        if let error {     // a wrong PIN / refused bond surfaces here as an auth error
            emit(.state(.failed("pairing failed (check the node PIN): \(error.localizedDescription)"))); return
        }
        if characteristic.isNotifying, rxChar != nil { emit(.state(.connected)) }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard characteristic.uuid == txUUID, let data = characteristic.value else { return }
        for line in accumulator.append(data) { emit(.line(line)) }
    }
}
