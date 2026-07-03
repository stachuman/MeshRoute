// MeshRouteCompanion — NodeView: connect/pair, identity, diagnostics, and a raw console.

import SwiftUI
import MeshRouteWire
import MeshRouteCore

struct NodeView: View {
    @Environment(AppModel.self) private var model
    @AppStorage("encryptDefault") private var encryptDefault = false   // shared with the compose lock toggle
    @State private var renameNode = ""              // rename-this-node field (Identity)
    @State private var showJoin = false             // leaf-provisioning sheets (R6 / D26)
    @State private var showCreate = false
    @State private var showLeaveConfirm = false

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
                        HStack {
                            TextField("Rename this node", text: $renameNode)
                                .autocorrectionDisabled().submitLabel(.done)
                                .onSubmit { model.renameNode(renameNode); renameNode = "" }
                            Button("Set") { model.renameNode(renameNode); renameNode = "" }
                                .disabled(renameNode.trimmingCharacters(in: .whitespaces).isEmpty)
                        }
                    }
                }

                if model.isConnected {
                    // ---- network: membership + duty + routes (R6/D26 · D27) ----
                    Section {
                        if let refusal = model.joinRefusal {
                            HStack(alignment: .top, spacing: 8) {
                                Image(systemName: refusal.isBlocking ? "exclamationmark.octagon.fill" : "exclamationmark.triangle.fill")
                                    .foregroundStyle(refusal.isBlocking ? .red : .orange)
                                VStack(alignment: .leading, spacing: 3) {
                                    Text(refusal.message).font(.caption)
                                    Button("Dismiss") { model.dismissJoinRefusal() }.font(.caption2).buttonStyle(.plain)
                                }
                            }
                        }
                        HStack {
                            Image(systemName: membershipIcon).foregroundStyle(membershipColor)
                            Text(membershipText)
                            Spacer()
                            if model.membership?.isManaged == true, let lvl = model.membership?.level {
                                Text("level \(lvl)").font(.caption2).foregroundStyle(.secondary)
                            }
                        }
                        DutyGaugeRow()
                        Button { showJoin = true } label: { Label("Join network…", systemImage: "antenna.radiowaves.left.and.right") }
                        Button { showCreate = true } label: { Label("Create leaf…", systemImage: "plus.circle") }
                        if model.membership?.isManaged == true {
                            Button(role: .destructive) { showLeaveConfirm = true } label: {
                                Label("Leave network", systemImage: "rectangle.portrait.and.arrow.right")
                            }
                        }
                        NavigationLink { RoutesView() } label: {
                            Label("Routes · \(model.latestStatus?.routes ?? model.routes.count)",
                                  systemImage: "point.3.connected.trianglepath.dotted")
                        }
                    } header: {
                        Text("Network")
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

                // ---- details & advanced (de-cluttered into a subscreen) ----
                Section {
                    NavigationLink { AdvancedNodeView() } label: {
                        Label("Diagnostics & details", systemImage: "wrench.and.screwdriver")
                    }
                }
            }
            .navigationTitle("Device")
            .scrollDismissesKeyboard(.interactively)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button { model.refreshNodeInfo() } label: { Image(systemName: "arrow.clockwise") }
                        .disabled(!model.isConnected)
                }
            }
            .onChange(of: model.isConnected) { _, connected in
                if connected { model.refreshNodeInfo() }   // auto-pull status/cfg/routes once the link is up
            }
            .sheet(isPresented: $showJoin) { JoinNetworkSheet(currentFreqMHz: model.latestConfig?.freqMHz) }
            .sheet(isPresented: $showCreate) { CreateLeafSheet(currentFreqMHz: model.latestConfig?.freqMHz) }
            .confirmationDialog("Leave the network? The node resets to standalone (it keeps its frequency).",
                                isPresented: $showLeaveConfirm, titleVisibility: .visible) {
                Button("Leave network", role: .destructive) { model.leaveNetwork() }
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

    // ---- leaf membership chip (D26) ----
    private var membershipText: String {
        if model.joinInFlight && model.membership?.state != .member { return "Joining…" }
        return model.membership?.label ?? "Unknown (pre-R6 firmware)"
    }
    private var membershipIcon: String {
        if model.joinInFlight && model.membership?.state != .member { return "dot.radiowaves.left.and.right" }
        switch model.membership?.state {
        case .member:    return "checkmark.circle.fill"
        case .joining:   return "dot.radiowaves.left.and.right"
        case .unmanaged: return "circle.dashed"
        case .none:      return "questionmark.circle"
        }
    }
    private var membershipColor: Color {
        if model.joinInFlight && model.membership?.state != .member { return .orange }
        switch model.membership?.state {
        case .member:  return .green
        case .joining: return .orange
        default:       return .secondary
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

// MARK: - Leaf provisioning sheets (R6 / D26)

private let kBandwidthsKHz: [Double] = [7.81, 10.42, 15.63, 20.83, 31.25, 41.67, 62.5, 125, 250, 500]   // SX126x LoRa BWs (kHz); narrow BWs are FRACTIONAL
private let kSpreadingFactors = Array(7...12)

/// Join an existing managed leaf: set the radio floor, auto-DAD an id, auto-pull the leaf's data config.
struct JoinNetworkSheet: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    var currentFreqMHz: Double?
    @State private var freq = ""
    @State private var bwKHz: Double = 125
    @State private var ctrlSF = 7
    @State private var level = ""

    var body: some View {
        NavigationStack {
            Form {
                Section("Radio floor") {
                    LabeledContent("Frequency (MHz)") {
                        TextField("868.0", text: $freq).keyboardType(.decimalPad).multilineTextAlignment(.trailing)
                    }
                    Picker("Bandwidth (kHz)", selection: $bwKHz) { ForEach(kBandwidthsKHz, id: \.self) { Text(Command.freqToken($0)).tag($0) } }
                    Picker("Control SF", selection: $ctrlSF) { ForEach(kSpreadingFactors, id: \.self) { Text("SF\($0)").tag($0) } }
                    LabeledContent("Level (1–255)") {
                        TextField("2", text: $level).keyboardType(.numberPad).multilineTextAlignment(.trailing)
                    }
                }
                Section { Text("Sets the rendezvous floor and joins — the node auto-pulls the leaf's data SFs, duty, name, and rate limits.")
                    .font(.caption).foregroundStyle(.secondary) }
            }
            .scrollDismissesKeyboard(.interactively)
            .navigationTitle("Join network").navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Join") {
                        if let f = freqValue, let l = levelValue {
                            model.joinNetwork(freqMHz: f, bwKHz: bwKHz, ctrlSF: ctrlSF, level: l); dismiss()
                        }
                    }.disabled(freqValue == nil || levelValue == nil)
                }
            }
            .onAppear { if freq.isEmpty, let f = currentFreqMHz, f > 0 { freq = Command.freqToken(f) } }
        }
    }
    private var freqValue: Double? { Double(freq).flatMap { $0 > 0 ? $0 : nil } }
    private var levelValue: Int? { Int(level).flatMap { (1...255).contains($0) ? $0 : nil } }
}

/// Create (mint) a managed leaf — this node becomes the mother and sets the data config others pull.
struct CreateLeafSheet: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    var currentFreqMHz: Double?
    @State private var name = ""
    @State private var freq = ""
    @State private var bwKHz: Double = 125
    @State private var ctrlSF = 7
    @State private var level = ""
    @State private var dataSFs: Set<Int> = [7, 9]
    @State private var duty = "10"

    var body: some View {
        NavigationStack {
            Form {
                Section("Leaf") {
                    LabeledContent("Name") { TextField("north field", text: $name).multilineTextAlignment(.trailing) }
                }
                Section("Radio floor") {
                    LabeledContent("Frequency (MHz)") {
                        TextField("868.0", text: $freq).keyboardType(.decimalPad).multilineTextAlignment(.trailing)
                    }
                    Picker("Bandwidth (kHz)", selection: $bwKHz) { ForEach(kBandwidthsKHz, id: \.self) { Text(Command.freqToken($0)).tag($0) } }
                    Picker("Control SF", selection: $ctrlSF) { ForEach(kSpreadingFactors, id: \.self) { Text("SF\($0)").tag($0) } }
                    LabeledContent("Level (1–255)") {
                        TextField("2", text: $level).keyboardType(.numberPad).multilineTextAlignment(.trailing)
                    }
                }
                Section {
                    ForEach(kSpreadingFactors, id: \.self) { sf in
                        Toggle("Data SF\(sf)", isOn: Binding(
                            get: { dataSFs.contains(sf) },
                            set: { if $0 { dataSFs.insert(sf) } else { dataSFs.remove(sf) } }))
                    }
                    LabeledContent("Duty cycle (%)") {
                        TextField("10", text: $duty).keyboardType(.decimalPad).multilineTextAlignment(.trailing)   // fractional ok (0.1 = tight EU sub-band)
                    }
                } header: { Text("Data plane") } footer: { Text("Data SFs the leaf uses (e.g. 7 + 9); duty is the legal airtime budget. The leaf's message rate-limits use protocol defaults (shared by every member).") }
            }
            .scrollDismissesKeyboard(.interactively)
            .navigationTitle("Create leaf").navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Create") {
                        if let f = freqValue, let l = levelValue, let d = dutyValue {
                            model.createLeaf(freqMHz: f, bwKHz: bwKHz, ctrlSF: ctrlSF, level: l,
                                             sfList: sfListString, dutyPercent: d,
                                             name: name.trimmingCharacters(in: .whitespaces))
                            dismiss()
                        }
                    }.disabled(!isValid)
                }
            }
            .onAppear { if freq.isEmpty, let f = currentFreqMHz, f > 0 { freq = Command.freqToken(f) } }
        }
    }
    private var freqValue: Double? { Double(freq).flatMap { $0 > 0 ? $0 : nil } }
    private var levelValue: Int? { Int(level).flatMap { (1...255).contains($0) ? $0 : nil } }
    private var dutyValue: Double? { Double(duty).flatMap { $0 > 0 && $0 <= 100 ? $0 : nil } }   // fractional % (0.1..100)
    private var sfListString: String { dataSFs.sorted().map(String.init).joined(separator: ",") }
    private var isValid: Bool {
        freqValue != nil && levelValue != nil && dutyValue != nil && !dataSFs.isEmpty
            && !name.trimmingCharacters(in: .whitespaces).isEmpty
    }
}

// MARK: - Duty gauge (D27) + Advanced/diagnostics subscreen (D28 de-clutter)

/// The airtime-budget readout (D27) — a gauge that polls `duty` while it's on screen.
struct DutyGaugeRow: View {
    @Environment(AppModel.self) private var model
    var body: some View {
        Group {
            if let d = model.latestDuty {
                if d.enabled {
                    VStack(alignment: .leading, spacing: 5) {
                        HStack {
                            Label("Airtime budget", systemImage: d.pct >= 100 ? "wifi.slash" : "wifi")
                                .foregroundStyle(d.pct >= 100 ? Color.red : .primary)
                            Spacer()
                            Text(d.pct >= 100 ? "silent" : "\(d.pct)%")
                                .foregroundStyle(d.pct >= 100 ? Color.red : .secondary).monospaced()
                        }
                        Gauge(value: Double(min(d.pct, 100)), in: 0...100) { EmptyView() }
                            .gaugeStyle(.linearCapacity).tint(dutyTint(d.pct))
                        if d.pct >= 100 && d.availMs > 0 {
                            Text("can transmit again in ~\(max(1, d.availMs / 1000)) s")
                                .font(.caption2).foregroundStyle(.secondary)
                        }
                    }
                } else {
                    LabeledContent("Airtime budget", value: "unlimited")
                }
            } else {
                LabeledContent("Airtime budget", value: "—")
            }
        }
        .task(id: model.isConnected) {   // poll `duty` while this row is on screen + connected (D27)
            while !Task.isCancelled && model.isConnected {
                model.refreshDuty()
                try? await Task.sleep(for: .seconds((model.latestDuty?.pct ?? 0) >= 100 ? 3 : 12))
            }
        }
    }
    private func dutyTint(_ pct: Int) -> Color { pct >= 100 ? .red : (pct >= 80 ? .orange : .green) }
}

/// Node telemetry (status/config), the resolve tool, the mock demo, and the raw console — moved off the main
/// Device screen to de-clutter it (spec §4).
struct AdvancedNodeView: View {
    @Environment(AppModel.self) private var model
    @State private var resolveHex = ""
    @State private var consoleInput = ""
    @State private var simBody = "ping from a peer"
    @FocusState private var fieldFocused: Bool

    var body: some View {
        List {
            if let s = model.latestStatus { StatusSection(status: s) }
            if let c = model.latestConfig { ConfigSection(cfg: c) }

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

            if model.canSimulateInbound {
                Section("Mock demo") {
                    TextField("incoming body", text: $simBody).focused($fieldFocused)
                    Button("Simulate inbound DM from id 2") { model.simulateInbound(fromID: 2, body: simBody) }
                }
            }

            Section("Console") {
                HStack {
                    TextField("raw line, e.g. send 2 \"hi\"", text: $consoleInput)
                        .autocorrectionDisabled().textInputAutocapitalization(.never).monospaced()
                        .focused($fieldFocused)
                    Button("Send") { model.sendRaw(consoleInput); consoleInput = "" }
                        .disabled(consoleInput.isEmpty || !model.isConnected)
                }
                if model.consoleLog.isEmpty {
                    Text("No traffic yet.").font(.caption).foregroundStyle(.tertiary)
                } else {
                    ForEach(Array(model.consoleLog.suffix(40).reversed())) { line in
                        Text(line.text).font(.caption.monospaced())
                            .foregroundStyle(line.kind == .outgoing ? Color.accentColor : Color.primary)
                    }
                }
            }
        }
        .navigationTitle("Diagnostics & details")
        .navigationBarTitleDisplayMode(.inline)
        .scrollDismissesKeyboard(.interactively)
        .toolbar {
            ToolbarItemGroup(placement: .keyboard) { Spacer(); Button("Done") { fieldFocused = false } }
        }
    }
}
