// MeshRouteCompanion — MeshView: the Known-Nodes Directory (D28 / spec §5).
// Phase 1 = the LIST over every node the app has heard of. Map ⇄ List (the Map) lands in Phase 2 with
// node positions from the firmware broadcast.

import SwiftUI
import SwiftData
import MeshRouteWire
import MeshRouteCore

struct MeshView: View {
    @Environment(AppModel.self) private var model
    @Query(sort: \NodeEntity.lastSeen, order: .reverse) private var nodes: [NodeEntity]
    @State private var filter: MeshFilter = .all
    @State private var search = ""
    @State private var showAddTeammate = false
    @State private var appliedTeamDefault = false   // D31 adaptive: default to the Team filter once per session

    enum MeshFilter: String, CaseIterable, Identifiable {
        case all = "All", team = "Team", contacts = "Contacts", reachable = "Reachable"
        var id: String { rawValue }
    }
    /// The Team segment only exists while the connected node is on a team.
    private var availableFilters: [MeshFilter] {
        model.teamID != nil ? MeshFilter.allCases : MeshFilter.allCases.filter { $0 != .team }
    }

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                if let team = model.teamID {   // D31/P3: the team status card — badge · pinned chat · add teammate
                    TeamStatusCard(teamID: team, memberCount: teamMemberCount,
                                   onAddTeammate: { showAddTeammate = true })
                        .padding(.horizontal).padding(.bottom, 6)
                }
                Picker("Filter", selection: $filter) {
                    ForEach(availableFilters) { Text($0.rawValue).tag($0) }
                }
                .pickerStyle(.segmented).padding(.horizontal).padding(.bottom, 6)

                Group {
                    if filtered.isEmpty {
                        ContentUnavailableView("No nodes yet", systemImage: "point.3.connected.trianglepath.dotted",
                            description: Text("Connect on the Device tab and exchange messages — every node the app hears lands here."))
                    } else {
                        List(filtered) { node in
                            NavigationLink(value: node.hash32) { MeshNodeRow(node: node) }
                        }
                    }
                }
            }
            .navigationTitle("Mesh")
            .navigationDestination(for: UInt32.self) { NodeDetailView(hash32: $0) }
            .navigationDestination(for: ThreadKey.self) { ThreadView(thread: $0) }   // the pinned team chat
            .searchable(text: $search, prompt: "Name or hash")
            .onAppear {
                if !appliedTeamDefault, model.teamID != nil { filter = .team; appliedTeamDefault = true }   // D31 adaptive default
            }
            .onChange(of: model.teamID) { _, team in
                if team == nil, filter == .team { filter = .all }     // the segment vanished under us — fall back
                else if team != nil, !appliedTeamDefault { filter = .team; appliedTeamDefault = true }
            }
            .toolbar {
                ToolbarItem(placement: .topBarLeading) { ConnectionPill() }
                if model.teamID != nil {          // D30: teammate bootstrap (bare-id team-scoped reqpubkey)
                    ToolbarItem(placement: .topBarTrailing) {
                        Button { showAddTeammate = true } label: { Image(systemName: "person.3.sequence") }
                    }
                }
            }
            .sheet(isPresented: $showAddTeammate) { AddTeammateSheet() }
        }
    }

    private var filtered: [NodeEntity] {
        nodes.filter { n in
            switch filter {
            case .all:       return true
            case .team:      return n.teamID != nil && n.teamID == model.teamID   // teammates on OUR team
            case .contacts:  return n.isContact
            case .reachable: return n.hops != nil
            }
        }.filter { n in
            search.isEmpty
                || n.displayName().localizedCaseInsensitiveContains(search)
                || n.keyHash.hex8.localizedCaseInsensitiveContains(search)
        }
    }
    private var teamMemberCount: Int {
        guard let t = model.teamID else { return 0 }
        return nodes.filter { $0.teamID == t }.count
    }
}

/// The P3 team home (D31): who we are on the team, whether the wider network is reachable (homed vs
/// off-grid), one tap into the pinned team chat, and the teammate bootstrap.
private struct TeamStatusCard: View {
    @Environment(AppModel.self) private var model
    let teamID: String
    let memberCount: Int
    var onAddTeammate: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 6) {
                Image(systemName: "person.3.fill").foregroundStyle(.teal)
                Text("Team \(teamID)").font(.headline).monospaced()
                Spacer()
                if let tl = model.teamLocal {
                    Text("you: id \(tl)").font(.caption).foregroundStyle(.secondary)
                }
            }
            HStack(spacing: 6) {
                Image(systemName: homed ? "antenna.radiowaves.left.and.right.circle.fill" : "point.3.filled.connected.trianglepath.dotted")
                    .foregroundStyle(homed ? Color.green : .orange)
                Text(homed ? "Team + network — homed via node \(model.mobileState?.home ?? 0)"
                           : "Team only — off-grid (no home)")
                    .font(.caption)
                Spacer()
                Text("\(memberCount) member\(memberCount == 1 ? "" : "s")")
                    .font(.caption).foregroundStyle(.secondary)
            }
            HStack(spacing: 10) {
                if let chat = model.teamChatThread() {
                    NavigationLink(value: chat) {
                        Label("Team chat", systemImage: "bubble.left.and.bubble.right.fill")
                            .font(.callout.weight(.medium))
                    }
                    .buttonStyle(.borderedProminent).tint(.teal)
                }
                Button(action: onAddTeammate) {
                    Label("Add teammate", systemImage: "person.3.sequence").font(.callout)
                }
                .buttonStyle(.bordered)
            }
        }
        .padding(12)
        .background(Color.teal.opacity(0.08), in: RoundedRectangle(cornerRadius: 12))
    }

    /// Homed ⇒ the wider network is reachable too (plain sends via the home); off-grid ⇒ team-plane only.
    private var homed: Bool { model.mobileState?.registered == true }
}

struct MeshNodeRow: View {
    let node: NodeEntity
    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: roleIcon).font(.title3)
                .foregroundStyle(node.isContact ? Color.accentColor : .secondary)
                .frame(width: 24)
            VStack(alignment: .leading, spacing: 2) {
                HStack(spacing: 5) {
                    Text(node.displayName()).font(.headline)
                    if node.verified { Image(systemName: "checkmark.seal.fill").font(.caption2).foregroundStyle(.green) }
                }
                Text(subtitle).font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
            if let b = node.battMv { Text("\(b) mV").font(.caption2).foregroundStyle(.secondary).monospaced() }
        }
        .padding(.vertical, 2)
    }
    private var roleIcon: String {
        if node.teamID != nil { return "person.3.fill" }   // a teammate (D30)
        switch node.role {
        case "gateway": return "arrow.triangle.branch"
        case "mobile":  return "figure.walk"
        default:        return node.isContact ? "person.crop.circle.fill" : "dot.radiowaves.left.and.right"
        }
    }
    private var subtitle: String {
        var parts = ["0x" + node.keyHash.hex8]
        if let tl = node.teamLocalID { parts.append("team id \(tl)") }
        else if let id = node.lastKnownID { parts.append("id \(id)") }
        if let leaf = node.leafName { parts.append(leaf) }
        if let h = node.hops { parts.append("\(h) hop\(h == 1 ? "" : "s")") }
        parts.append(node.lastSeen.shortRelative)
        return parts.joined(separator: " · ")
    }
}

/// One node's detail — the shared screen for Mesh + Contacts. Phase 1: info + message + name/resolve.
/// (Phase 2 adds verify/safety-numbers · block/mute · show-on-map · remote-diag.)
struct NodeDetailView: View {
    @Environment(AppModel.self) private var model
    let hash32: UInt32
    @Query private var nodes: [NodeEntity]
    @State private var showRename = false
    @State private var nameText = ""

    init(hash32: UInt32) {
        self.hash32 = hash32
        _nodes = Query(filter: #Predicate<NodeEntity> { $0.hash32 == hash32 })
    }

    var body: some View {
        Form {
            if let node = nodes.first {
                Section {
                    Button { model.openConversation(threadHash: node.hash32) } label: {
                        Label("Message", systemImage: "bubble.left")
                    }
                    Button { nameText = node.name ?? ""; showRename = true } label: {
                        Label(node.isContact ? "Rename contact" : "Add as contact",
                              systemImage: "person.crop.circle.badge.plus")
                    }
                    Button { model.resolve(node.keyHash) } label: {
                        Label("Resolve id (on-air)", systemImage: "location.magnifyingglass")
                    }
                    if model.teamID != nil {   // D30: the team-scoped handshake (provisions E2E + names, both ways)
                        Button { model.requestPubkey(node.keyHash, team: true) } label: {
                            Label("Request key (team)", systemImage: "person.3.sequence")
                        }
                    }
                }
                Section("Identity") {
                    LabeledContent("Name", value: node.displayName())
                    LabeledContent("key_hash32", value: "0x" + node.keyHash.hex8)
                    if let id = node.lastKnownID { LabeledContent("Short id", value: "\(id)") }
                    LabeledContent("Role", value: node.role)
                    LabeledContent("Key", value: node.verified ? "verified (scanned)" : "unverified (TOFU)")
                }
                if node.leafName != nil || node.layer != nil || node.teamID != nil {
                    Section("Membership") {
                        if let leaf = node.leafName { LabeledContent("Leaf", value: leaf) }
                        if let lyr = node.layer { LabeledContent("Layer", value: "\(lyr)") }
                        if let t = node.teamID {   // D30: a teammate — DMs ride the team plane (-t)
                            LabeledContent("Team", value: t)
                            if let tl = node.teamLocalID { LabeledContent("Team id", value: "\(tl)") }
                        }
                    }
                }
                Section("Seen") {
                    LabeledContent("Last seen", value: node.lastSeen.shortRelative)
                    if let h = node.hops { LabeledContent("Distance", value: "\(h) hop\(h == 1 ? "" : "s")") }
                    if let s = node.linkScoreQ4 { LabeledContent("Link", value: "\(s / 16) dB") }
                    if node.hasPosition, let lat = node.latE7, let lon = node.lonE7 {
                        LabeledContent("Position", value: String(format: "%.5f, %.5f", Double(lat) / 1e7, Double(lon) / 1e7))
                    }
                    if let b = node.battMv { LabeledContent("Battery", value: "\(b) mV") }
                }
            } else {
                ContentUnavailableView("Node not found", systemImage: "questionmark")
            }
        }
        .navigationTitle(nodes.first?.displayName() ?? "Node")
        .navigationBarTitleDisplayMode(.inline)
        .alert("Contact name", isPresented: $showRename) {
            TextField("Name", text: $nameText)
            Button("Save") {
                let n = nameText.trimmingCharacters(in: .whitespaces)
                if !n.isEmpty { model.addContact(name: n, hash: KeyHash(hash32)) }
            }
            Button("Cancel", role: .cancel) {}
        }
    }
}
