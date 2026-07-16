// MeshRouteCompanion — MobileViews: the mobile roam screen + the Add-teammate sheet (D30 c2).
// Phone-first by design: if the screenless T1000-E tracker ships, this IS its management UI.

import SwiftUI
import MeshRouteWire
import MeshRouteCore

/// Roam screen: this mobile's registration + the learned-networks directory (`mobile status` +
/// `mobile gateways`). Tap a network to re-register onto it — all live, no reboot.
struct MobileRoamView: View {
    @Environment(AppModel.self) private var model
    @State private var registerTarget: MobileNetRow?

    var body: some View {
        List {
            Section("Registration") {
                if let ms = model.mobileState {
                    HStack {
                        Image(systemName: ms.registered ? "antenna.radiowaves.left.and.right.circle.fill"
                                                        : "antenna.radiowaves.left.and.right.slash")
                            .foregroundStyle(ms.registered ? Color.green : .orange)
                        Text(ms.label)
                    }
                }
                if let s = model.latestMobileStatus {
                    LabeledContent("PHY", value: String(format: "%.4g MHz · SF%d · %.4g kHz",
                                                        Double(s.freqKHz) / 1000, s.sf, Double(s.bwHz) / 1000))
                    LabeledContent("Layer", value: "\(s.layer)")
                    LabeledContent("Auto-register", value: s.autoregister ? "on (node roams itself)" : "off (app-driven)")
                    if s.registered { LabeledContent("Config epoch", value: "\(s.epoch)") }
                }
                Button { model.mobileRegister() } label: {
                    Label("Re-register", systemImage: "arrow.clockwise.circle")
                }
                Button { model.mobileRegisterScan() } label: {
                    Label("Scan networks", systemImage: "dot.radiowaves.left.and.right")
                }
            }

            Section {
                if model.mobileNets.isEmpty {
                    Text("No networks learned yet — Scan, or move into range of a beaconing layer.")
                        .font(.caption).foregroundStyle(.secondary)
                } else {
                    ForEach(model.mobileNets) { net in
                        Button { registerTarget = net } label: {
                            VStack(alignment: .leading, spacing: 2) {
                                Text(net.label).font(.headline).foregroundStyle(.primary)
                                Text("L\(net.layer) · \(net.phyLabel)").font(.caption).foregroundStyle(.secondary)
                            }
                        }
                    }
                }
            } header: {
                Text("Learned networks\(model.mobileGateways.isEmpty ? "" : " · \(model.mobileGateways.count) gateway\(model.mobileGateways.count == 1 ? "" : "s")")")
            } footer: {
                Text("Tap a network to register onto it (`mobile register freq= sf= bw=`).")
            }
        }
        .navigationTitle("Mobile roaming")
        .navigationBarTitleDisplayMode(.inline)
        .onAppear { model.refreshMobile() }
        .refreshable { model.refreshMobile() }
        .confirmationDialog(registerTarget.map { "Register onto \($0.label)?" } ?? "",
                            isPresented: Binding(get: { registerTarget != nil },
                                                 set: { if !$0 { registerTarget = nil } }),
                            titleVisibility: .visible) {
            Button("Register") {
                if let net = registerTarget { model.mobileRegister(to: net) }
                registerTarget = nil
            }
        }
    }
}

/// Add a TEAMMATE by their team-local id (D30 three-plane contacts): fires the bare-id team-scoped
/// `reqpubkey <id>` — the mutual handshake answers with the teammate's key + name, and the directory
/// row is stamped `{team_id, team_local_id}`. DMs to them then ride the team plane (`-t`) automatically.
struct AddTeammateSheet: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    @State private var localID = ""

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    LabeledContent("Teammate id (1–254)") {
                        TextField("9", text: $localID).keyboardType(.numberPad).multilineTextAlignment(.trailing)
                    }
                } footer: {
                    Text("Their id on the team overlay — shown on their Device tab as “Team … · id N”. Requesting keys also exchanges names, so the contact labels itself.")
                }
            }
            .navigationTitle("Add teammate").navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Request") {
                        if let id = idValue { model.addTeammate(localID: id); dismiss() }
                    }.disabled(idValue == nil)
                }
            }
        }
    }
    private var idValue: Int? { Int(localID).flatMap { (1...254).contains($0) ? $0 : nil } }
}
