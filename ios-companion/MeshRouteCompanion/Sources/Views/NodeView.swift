// MeshRouteCompanion — NodeView: connect/pair, identity, diagnostics, and a raw console.

import SwiftUI
import MeshRouteWire
import MeshRouteCore

struct NodeView: View {
    @Environment(AppModel.self) private var model
    @State private var resolveHex = ""
    @State private var consoleInput = ""
    @State private var simBody = "ping from a peer"

    var body: some View {
        @Bindable var model = model
        NavigationStack {
            List {
                // ---- connection ----
                Section("Connection") {
                    Picker("Transport", selection: $model.backend) {
                        Text("Mock node").tag(AppModel.Backend.mock)
                        Text("Bluetooth").tag(AppModel.Backend.ble)
                    }
                    .disabled(model.linkState != .disconnected)

                    HStack {
                        Circle().fill(statusColor).frame(width: 10, height: 10)
                        Text(model.statusText).foregroundStyle(.secondary)
                        Spacer()
                        Button(model.isConnected || isBusy ? "Disconnect" : "Connect") {
                            model.isConnected || isBusy ? model.disconnect() : model.connect()
                        }
                        .buttonStyle(.borderedProminent)
                    }
                }

                // ---- identity ----
                if let id = model.nodeIdentity {
                    Section("Node identity") {
                        if let n = id.name { LabeledContent("Name", value: n) }
                        LabeledContent("Short id", value: "\(id.id)")
                        LabeledContent("key_hash32", value: "0x" + id.key.hex8)
                        LabeledContent("Control SF", value: "\(id.routingSF)")
                        LabeledContent("Mode", value: id.mode)
                        LabeledContent("Gateway", value: id.gateway ? "yes" : "no")
                    }
                }

                // ---- diagnostics ----
                Section("Diagnostics") {
                    Button("whoami") { model.sendRaw("whoami") }
                    Button("status") { model.sendRaw("status") }
                    HStack {
                        TextField("resolve hash (hex)", text: $resolveHex)
                            .autocorrectionDisabled().textInputAutocapitalization(.never).monospaced()
                        Button("Resolve") {
                            if let h = KeyHash(hex: resolveHex) { model.resolve(h); resolveHex = "" }
                        }.disabled(KeyHash(hex: resolveHex) == nil)
                    }
                }

                // ---- mock demo ----
                if model.canSimulateInbound {
                    Section("Mock demo") {
                        TextField("incoming body", text: $simBody)
                        Button("Simulate inbound DM from id 2") { model.simulateInbound(fromID: 2, body: simBody) }
                    }
                }

                // ---- raw console ----
                Section("Console") {
                    HStack {
                        TextField("raw line, e.g. send 2 hi", text: $consoleInput)
                            .autocorrectionDisabled().textInputAutocapitalization(.never).monospaced()
                        Button("Send") { model.sendRaw(consoleInput); consoleInput = "" }
                            .disabled(consoleInput.isEmpty || !model.isConnected)
                    }
                    if model.consoleLog.isEmpty {
                        Text("No traffic yet.").font(.caption).foregroundStyle(.tertiary)
                    } else {
                        ForEach(Array(model.consoleLog.suffix(40).reversed())) { line in
                            Text(line.text)
                                .font(.caption.monospaced())
                                .foregroundStyle(line.kind == .outgoing ? Color.accentColor : Color.primary)
                        }
                    }
                }
            }
            .navigationTitle("Node")
        }
    }

    private var isBusy: Bool {
        switch model.linkState {
        case .scanning, .waitingForWindow, .connecting, .pairing: return true
        default: return false
        }
    }
    private var statusColor: Color {
        switch model.linkState {
        case .connected:           return .green
        case .failed:              return .red
        case .disconnected:        return .gray
        default:                   return .orange
        }
    }
}

/// A compact connection indicator for other tabs' toolbars.
struct ConnectionPill: View {
    @Environment(AppModel.self) private var model
    var body: some View {
        HStack(spacing: 5) {
            Circle().fill(dotColor).frame(width: 8, height: 8)
            Text(shortStatus).font(.caption2).foregroundStyle(.secondary)
        }
    }
    private var dotColor: Color {
        switch model.linkState {
        case .connected: return .green
        case .failed:    return .red
        case .disconnected: return .gray
        default:         return .orange
        }
    }
    private var shortStatus: String { model.isConnected ? "Online" : "Offline" }
}
