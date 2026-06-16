// MeshRouteCompanion — NodeView: connect/pair, identity, diagnostics, and a raw console.

import SwiftUI
import MeshRouteWire
import MeshRouteCore

struct NodeView: View {
    @Environment(AppModel.self) private var model
    @State private var resolveHex = ""
    @State private var consoleInput = ""
    @State private var simBody = "ping from a peer"
    @AppStorage("encryptDefault") private var encryptDefault = false   // shared with the compose lock toggle
    @FocusState private var fieldFocused: Bool      // drives the keyboard "Done" dismiss (no other tappable area)

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

                // ---- live status + config + network (Theme D) ----
                if let s = model.latestStatus { StatusSection(status: s) }
                if let c = model.latestConfig { ConfigSection(cfg: c) }
                if model.isConnected {
                    Section {
                        NavigationLink { RoutesView() } label: {
                            Label("Network · \(model.latestStatus?.routes ?? model.routes.count) routes",
                                  systemImage: "point.3.connected.trianglepath.dotted")
                        }
                    }
                    // ---- security (E2E) ----
                    Section {
                        Toggle("Encrypt DMs by default", isOn: Binding(
                            get: { encryptDefault },
                            set: { encryptDefault = $0; model.setNodeEncryptDefault($0) }))   // app default + `cfg set e2e_dm`
                    } header: {
                        Text("Security")
                    } footer: {
                        Text("New DMs default to encrypted (E2E). Toggle the lock per message to override.")
                    }
                }

                // ---- diagnostics ----
                Section("Diagnostics") {
                    HStack {
                        TextField("resolve hash (hex)", text: $resolveHex)
                            .autocorrectionDisabled().textInputAutocapitalization(.never).monospaced()
                            .focused($fieldFocused)
                        Button("Resolve") {
                            if let h = KeyHash(hex: resolveHex) { model.resolve(h); resolveHex = "" }
                        }.disabled(KeyHash(hex: resolveHex) == nil)
                    }
                }

                // ---- mock demo ----
                if model.canSimulateInbound {
                    Section("Mock demo") {
                        TextField("incoming body", text: $simBody).focused($fieldFocused)
                        Button("Simulate inbound DM from id 2") { model.simulateInbound(fromID: 2, body: simBody) }
                    }
                }

                // ---- raw console ----
                Section("Console") {
                    HStack {
                        TextField("raw line, e.g. send 2 hi", text: $consoleInput)
                            .autocorrectionDisabled().textInputAutocapitalization(.never).monospaced()
                            .focused($fieldFocused)
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
            .scrollDismissesKeyboard(.interactively)   // swipe the list down to dismiss the keyboard too
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button { model.refreshNodeInfo() } label: { Image(systemName: "arrow.clockwise") }
                        .disabled(!model.isConnected)
                }
                ToolbarItemGroup(placement: .keyboard) {   // the explicit dismiss the screen lacked
                    Spacer()
                    Button("Done") { fieldFocused = false }
                }
            }
            .onChange(of: model.isConnected) { _, connected in
                if connected { model.refreshNodeInfo() }   // auto-pull status/cfg/routes once the link is up
            }
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
