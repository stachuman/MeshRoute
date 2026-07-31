// MeshRouteCompanion — TeamQRViews: the TEAM QR (team-encrypted-channel spec T-K4).
// One scan = full team onboarding — PHY params AND the team content keypair.
//
// ⚠ This card carries a PRIVATE key. Both screens say so, and the share screen is deliberately gated
// behind an explicit reveal (you cannot flash it by accident). The payload is a custom-scheme QR, never
// an https URL — see TeamCard.swift for why.

import SwiftUI
import MeshRouteWire
import MeshRouteCore

/// Creator side: show the team QR so a vetted joiner can scan it. Gated behind an explicit reveal.
struct ShareTeamView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    @State private var revealed = false

    /// Built from the node's PHY + the keypair disclosed by `team exportkey` (held ephemerally, purged on
    /// dismiss). nil until the export answers — the app never invents key material.
    private var card: TeamCard? { model.teamShareCard() }

    var body: some View {
        NavigationStack {
            Group {
                if let reason = model.teamKeyExportError {
                    ContentUnavailableView(
                        reason == "no_key" ? "This node holds no team key" : "This node is not in a team",
                        systemImage: "key.slash",
                        description: Text(reason == "no_key"
                            ? "Create a team (which mints a key) or ask a teammate to grant you one."
                            : "Join or create a team first — a key without a team can't be shared."))
                } else if let card {
                    ScrollView {
                        VStack(spacing: 16) {
                            warning
                            if revealed {
                                if let img = qrUIImage(card.qrString) {
                                    Image(uiImage: img)
                                        .interpolation(.none).resizable().scaledToFit()
                                        .frame(maxWidth: 280).padding(12)
                                        .background(RoundedRectangle(cornerRadius: 16).fill(.white))
                                }
                                Text(card.teamName.isEmpty ? "Team \(card.teamIDHex)" : card.teamName)
                                    .font(.title3.bold())
                                Text("team \(card.teamIDHex) · \(String(format: "%.4g", card.freqMHz)) MHz · SF\(card.routingSF)")
                                    .font(.caption).monospaced().foregroundStyle(.secondary)
                                Button("Hide") { withAnimation { revealed = false } }
                                    .buttonStyle(.bordered)
                            } else {
                                Button {
                                    withAnimation { revealed = true }
                                } label: {
                                    Label("Reveal team QR", systemImage: "eye")
                                        .frame(maxWidth: .infinity).padding(.vertical, 6)
                                }
                                .buttonStyle(.borderedProminent).tint(.red)
                                Text("Only show this to someone you are adding to the team, in person.")
                                    .font(.footnote).foregroundStyle(.secondary).multilineTextAlignment(.center)
                            }
                        }
                        .padding()
                    }
                } else {
                    ProgressView("Reading the team key from the node…")
                }
            }
            .navigationTitle("Share team")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } } }
            .onAppear { model.requestTeamKeyExport() }     // `team exportkey` — the single disclosure verb
            .onDisappear { model.purgeExportedTeamKey() }  // ⚠ never outlive the share flow
        }
    }

    private var warning: some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: "exclamationmark.shield.fill").foregroundStyle(.red).font(.title3)
            VStack(alignment: .leading, spacing: 3) {
                Text("This QR contains the team's PRIVATE key").font(.subheadline.bold())
                Text("Anyone who scans it can read — and grant — all team traffic. There is no revocation: if it leaks, create a new key and re-share.")
                    .font(.caption).foregroundStyle(.secondary)
            }
        }
        .padding(12)
        .background(Color.red.opacity(0.08), in: RoundedRectangle(cornerRadius: 12))
    }
}

/// Joiner side: scan a team QR → review → provision this node (PHY + content key) in one action.
struct ScanTeamView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    @State private var scanned: TeamCard?

    var body: some View {
        NavigationStack {
            Group {
                if let card = scanned {
                    Form {
                        Section("Team") {
                            LabeledContent("Name", value: card.teamName.isEmpty ? "—" : card.teamName)
                            LabeledContent("team_id", value: card.teamIDHex)
                        }
                        Section("Radio") {
                            LabeledContent("Frequency", value: String(format: "%.4g MHz", card.freqMHz))
                            LabeledContent("Bandwidth", value: String(format: "%.4g kHz", card.bwKHz))
                            LabeledContent("Control SF", value: "SF\(card.routingSF)")
                            LabeledContent("Data SFs", value: card.sfList.map { "SF\($0)" }.joined(separator: ", "))
                        }
                        Section {
                            Button("Join this team") { model.provisionTeam(from: card); dismiss() }
                        } footer: {
                            Text("Provisions this node: radio parameters plus the team content key, so you can read and post encrypted team messages.")
                        }
                    }
                } else if QRScannerView.isSupported {
                    ZStack(alignment: .bottom) {
                        QRScannerView { payload in
                            guard scanned == nil, let card = TeamCard(qrString: payload) else { return }
                            scanned = card
                        }
                        Text("Point at a MeshRoute team QR")
                            .font(.footnote).padding(8)
                            .background(.thinMaterial, in: Capsule()).padding(.bottom, 24)
                    }
                } else {
                    ContentUnavailableView("Camera scanning unavailable", systemImage: "qrcode.viewfinder",
                        description: Text("This device can't scan (Simulator?)."))
                }
            }
            .navigationTitle("Scan team QR")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } } }
        }
    }
}
