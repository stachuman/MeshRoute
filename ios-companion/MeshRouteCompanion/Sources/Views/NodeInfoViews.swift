// MeshRouteCompanion — NodeInfoViews: the Status / Configuration sections + the Routes (Network) screen
// (roadmap Theme D). All read-only v1 — fed by status/cfg/routes over BLE; the mock serves them too.

import SwiftUI
import MeshRouteWire
import MeshRouteCore

// ---- Status section (live telemetry) ----

struct StatusSection: View {
    let status: NodeStatusSnapshot
    var body: some View {
        Section("Status") {
            LabeledContent("State", value: status.state)
            if let up = status.uptimeMs { LabeledContent("Uptime", value: formatUptime(up)) }
            if let mv = status.battMv { LabeledContent("Battery", value: formatBattery(mv)) }
            if let duty = status.dutyMs {
                LabeledContent("Airtime (1 h)", value: "\(duty / 1000)s · \(String(format: "%.1f", Double(duty) / 36000))%")
            }
            if let r = status.routes { LabeledContent("Routes", value: "\(r)") }
            if let rx = status.rx, let tx = status.tx { LabeledContent("RX / TX", value: "\(rx) / \(tx)") }
            if let q = status.txq, let d = status.txdrop, (q > 0 || d > 0) {
                LabeledContent("TX queue", value: "\(q)\(d > 0 ? " · \(d) dropped" : "")")
                    .foregroundStyle(d > 0 ? .red : .primary)
            }
            if let lbt = status.lbt { LabeledContent("LBT", value: lbt ? "on" : "off") }
            if status.pending == true { LabeledContent("Flight", value: "in progress").foregroundStyle(.orange) }
        }
    }
}

// ---- Configuration section (read-only) ----

struct ConfigSection: View {
    let cfg: NodeConfigInfo
    @State private var showLocation = false
    var body: some View {
        Section("Configuration") {
            LabeledContent("Frequency", value: String(format: "%.4f MHz", cfg.freqMHz))
            LabeledContent("Control SF", value: "\(cfg.routingSF)")
            LabeledContent("Data SFs", value: cfg.sfList.isEmpty ? "—" : cfg.sfList)
            LabeledContent("Bandwidth", value: "\(cfg.bwHz / 1000) kHz")
            LabeledContent("Coding rate", value: "4/\(cfg.cr)")
            LabeledContent("TX power", value: "\(cfg.txPower) dBm")
            LabeledContent("Duty cycle", value: cfg.dutyX1000 == 0 ? "off" : String(format: "%.1f%%", cfg.dutyPercent))
            LabeledContent("Beacon", value: "\(cfg.beaconMs / 1000)s")
            LabeledContent("Leaf id", value: "\(cfg.leafID)")
            if cfg.gateway { LabeledContent("Gateway", value: "yes") }
            if cfg.mobile { LabeledContent("Mobile", value: "yes") }
            LabeledContent("BLE", value: "\(cfg.bleMode)\(cfg.bleMode == "periodic" ? " · \(cfg.blePeriod) min" : "")")
            // Location — tap to set; a map preview when set (Theme C)
            Button { showLocation = true } label: {
                HStack {
                    Text("Location").foregroundStyle(.primary)
                    Spacer()
                    Text(cfg.hasPosition ? String(format: "%.5f, %.5f", cfg.latitude, cfg.longitude) : "Set…")
                        .foregroundStyle(.secondary)
                    Image(systemName: "chevron.right").font(.caption).foregroundStyle(.tertiary)
                }
            }
            if cfg.hasPosition {
                NodeMapPreview(lat: cfg.latitude, lon: cfg.longitude)
                    .frame(height: 140).listRowInsets(EdgeInsets())
            }
        }
        .sheet(isPresented: $showLocation) { NodeLocationView(current: cfg) }
    }
}

// ---- Routes (Network) screen ----

struct RoutesView: View {
    @Environment(AppModel.self) private var model
    var body: some View {
        List {
            if model.routes.isEmpty {
                ContentUnavailableView("No routes", systemImage: "point.3.connected.trianglepath.dotted",
                    description: Text(model.isConnected ? "This node hasn't learned any routes yet."
                                                        : "Connect to the node to see its routing table."))
            } else {
                Section("\(model.routes.count) reachable") {
                    ForEach(model.routes, id: \.dest) { RouteRowView(route: $0) }
                }
            }
        }
        .navigationTitle("Network")
        .toolbar { ToolbarItem(placement: .topBarTrailing) {
            Button { model.refreshRoutes() } label: { Image(systemName: "arrow.clockwise") }
                .disabled(!model.isConnected)
        } }
        .onAppear { model.refreshRoutes() }
    }
}

struct RouteRowView: View {
    let route: RouteInfo
    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: route.gw ? "arrow.triangle.branch" : "antenna.radiowaves.left.and.right")
                .foregroundStyle(route.gw ? Color.orange : Color.accentColor)
            VStack(alignment: .leading, spacing: 2) {
                HStack(spacing: 6) {
                    Text("Node \(route.dest)").font(.headline)
                    if route.gw { Text("gateway").font(.caption2).padding(.horizontal, 5).padding(.vertical, 1)
                        .background(Capsule().fill(Color.orange.opacity(0.2))) }
                }
                Text("via \(route.next) · \(route.hops) hop\(route.hops == 1 ? "" : "s") · \(dbString(route.score)) dB")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
            Text(formatAge(route.ageMs)).font(.caption2).foregroundStyle(.tertiary)
        }
        .padding(.vertical, 2)
    }
}

// ---- formatting helpers ----

func formatUptime(_ ms: UInt64) -> String {
    let s = ms / 1000, d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60
    if d > 0 { return "\(d)d \(h)h" }
    if h > 0 { return "\(h)h \(m)m" }
    return "\(m)m \(s % 60)s"
}

/// LiPo voltage → "4.05 V · ~78%" (linear 3.3–4.2 V approximation; clamped).
func formatBattery(_ mv: Int) -> String {
    let v = Double(mv) / 1000
    let pct = max(0, min(100, Int((v - 3.3) / (4.2 - 3.3) * 100)))
    return String(format: "%.2f V · ~%d%%", v, pct)
}

func formatAge(_ ms: UInt32) -> String {
    let s = Int(ms) / 1000
    if s < 60 { return "\(s)s ago" }
    if s < 3600 { return "\(s / 60)m ago" }
    return "\(s / 3600)h ago"
}

/// Q4 dB (1/16 dB units) → a one-decimal dB string.
func dbString(_ q4: Int) -> String { String(format: "%.1f", Double(q4) / 16) }
