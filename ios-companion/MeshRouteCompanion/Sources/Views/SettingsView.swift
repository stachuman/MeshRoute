// MeshRouteCompanion — SettingsView: APP preferences (roadmap §7.6 — the long-missing screen).
//
// Scope rule: this screen holds settings that live on the PHONE. Anything that configures the NODE
// (encrypt-by-default, location, leaf/team membership, radio) stays on the Device tab, where it is
// obviously a node write — mixing the two is how people mis-set a fleet.

import SwiftUI
import MeshRouteWire
import MeshRouteCore

struct SettingsView: View {
    @Environment(AppModel.self) private var model
    @AppStorage("notifyDMs") private var notifyDMs = true
    @AppStorage("notifyTeam") private var notifyTeam = true
    @AppStorage("badgeUnread") private var badgeUnread = true
    @AppStorage("useMiles") private var useMiles = false
    @AppStorage("showCounters") private var showCounters = true
    @AppStorage("encryptDefault") private var encryptDefault = false   // mirrors the Device-tab toggle (node cfg)

    var body: some View {
        List {
            Section {
                Toggle("Direct messages", isOn: $notifyDMs)
                Toggle("Team messages", isOn: $notifyTeam)
                Toggle("Unread badge", isOn: $badgeUnread)
            } header: {
                Text("Notifications")
            } footer: {
                Text("Banners appear when a message arrives and the app isn't on screen. iOS permission is requested on first launch.")
            }

            Section {
                Picker("Distance", selection: $useMiles) {
                    Text("Kilometres").tag(false)
                    Text("Miles").tag(true)
                }
                Toggle("Show message counters (#ctr)", isOn: $showCounters)
            } header: {
                Text("Display")
            } footer: {
                Text("Counters are the node's per-message ids — useful when comparing against the console.")
            }

            Section {
                LabeledContent("Encrypt DMs by default") {
                    Text(encryptDefault ? "On" : "Off").foregroundStyle(.secondary)
                }
                if model.teamID != nil {
                    LabeledContent("Team key") {
                        switch model.teamHasKey {
                        case true?:  Label("Held", systemImage: "key.fill").foregroundStyle(.green)
                        case false?: Label("Not held", systemImage: "key.slash").foregroundStyle(.orange)
                        case nil:    Text("Unknown").foregroundStyle(.secondary)
                        }
                    }
                }
            } header: {
                Text("Security")
            } footer: {
                Text("These reflect the connected node — change them on the Device tab (they are node settings, not phone settings).")
            }

            Section("About") {
                LabeledContent("App", value: appVersion)
                LabeledContent("Bundle", value: Bundle.main.bundleIdentifier ?? "—")
                if let id = model.nodeIdentity {
                    LabeledContent("Node", value: id.name ?? "0x" + id.key.hex8)
                    LabeledContent("Firmware id", value: "0x" + id.key.hex8)
                }
                Link(destination: URL(string: "https://meshroute.eu")!) {
                    LabeledContent("Project", value: "meshroute.eu")
                }
            }
        }
        .navigationTitle("Settings")
        .navigationBarTitleDisplayMode(.inline)
    }

    private var appVersion: String {
        let v = Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "?"
        let b = Bundle.main.infoDictionary?["CFBundleVersion"] as? String ?? "?"
        return "\(v) (\(b))"
    }
}
