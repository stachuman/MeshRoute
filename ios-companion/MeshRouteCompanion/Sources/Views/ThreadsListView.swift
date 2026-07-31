// MeshRouteCompanion — ThreadsListView: DM threads + channel feeds, latest first.

import SwiftUI
import SwiftData
import MeshRouteWire
import MeshRouteCore

struct ThreadsListView: View {
    @Environment(AppModel.self) private var model
    @Query(sort: \MessageEntity.timestamp, order: .reverse) private var messages: [MessageEntity]
    @Query private var nodes: [NodeEntity]
    @Query private var labels: [ChannelLabelEntity]
    @State private var showNewMessage = false
    @State private var renameTarget: UInt8?         // channel id being renamed (drives the alert)
    @State private var renameText = ""

    var body: some View {
        @Bindable var model = model
        NavigationStack(path: $model.messagesPath) {   // bound so a notification tap can push a thread
            Group {
                if summaries.isEmpty && pinnedTeam == nil {
                    ContentUnavailableView("No messages yet", systemImage: "tray",
                        description: Text("Connect to your node on the Device tab, then send or receive a message."))
                } else {
                    List {
                        // ★ The team conversation is PINNED (D31/P3): on a team it is the primary thread, so it
                        // never drifts down the chronological list. Shown even when empty — it always exists.
                        if let team = pinnedTeam {
                            Section {
                                NavigationLink(value: team.key) { ThreadRow(summary: team) }
                                    .contextMenu {
                                        Button { model.toggleMute(team.key) } label: {
                                            Label(model.isMuted(team.key) ? "Unmute" : "Mute",
                                                  systemImage: model.isMuted(team.key) ? "bell" : "bell.slash")
                                        }
                                    }
                            }
                        }
                        Section {
                            ForEach(otherSummaries) { s in
                                NavigationLink(value: s.key) { ThreadRow(summary: s) }
                                    .contextMenu {
                                        if case .channel(let c) = s.key {
                                            Button { startRename(c) } label: { Label("Rename channel", systemImage: "pencil") }
                                        }
                                        Button { model.toggleMute(s.key) } label: {
                                            Label(model.isMuted(s.key) ? "Unmute" : "Mute",
                                                  systemImage: model.isMuted(s.key) ? "bell" : "bell.slash")
                                        }
                                    }
                            }
                        }
                    }
                }
            }
            .navigationTitle("Messages")
            .navigationDestination(for: ThreadKey.self) { ThreadView(thread: $0) }
            .toolbar {
                ToolbarItem(placement: .topBarLeading) { ConnectionPill() }
                ToolbarItemGroup(placement: .topBarTrailing) {
                    if let team = model.teamChatThread() {   // ★ fixed-position shortcut: OPENS the thread, no compose sheet
                        Button { model.messagesPath = [team] } label: { Image(systemName: "person.3.fill") }
                            .accessibilityLabel("Open team chat")
                    }
                    Button { showNewMessage = true } label: { Image(systemName: "square.and.pencil") }
                        .accessibilityLabel("New message")
                }
            }
            .sheet(isPresented: $showNewMessage) { NewMessageSheet() }
            .alert("Channel name", isPresented: Binding(get: { renameTarget != nil },
                                                        set: { if !$0 { renameTarget = nil } })) {
                TextField("Name (empty to clear)", text: $renameText)
                Button("Save") {
                    if let c = renameTarget { model.setChannelLabel(Int(c), name: renameText) }
                    renameTarget = nil
                }
                Button("Cancel", role: .cancel) { renameTarget = nil }
            } message: {
                Text("A local label for channel \(renameTarget.map(String.init) ?? "") — only on this phone.")
            }
        }
    }

    private func startRename(_ channel: UInt8) {
        renameText = channelLabels[Int(channel)] ?? ""
        renameTarget = channel
    }

    private var nodesByHash: [UInt32: NodeEntity] {
        Dictionary(nodes.map { ($0.hash32, $0) }, uniquingKeysWith: { a, _ in a })
    }
    private var channelLabels: [Int: String] {
        Dictionary(labels.map { ($0.channelID, $0.name) }, uniquingKeysWith: { a, _ in a })
    }

    private var summaries: [ThreadSummary] {
        var unread: [ThreadKey: Int] = [:]
        for m in messages where m.direction == .incoming && !m.isRead { unread[m.threadKey, default: 0] += 1 }
        var latest: [ThreadKey: ThreadSummary] = [:]
        for m in messages where latest[m.threadKey] == nil {   // messages are reverse-sorted → first = latest
            let key = m.threadKey
            latest[key] = ThreadSummary(key: key,
                                        title: threadTitle(key, nodesByHash: nodesByHash,
                                                           channelLabels: channelLabels),
                                        lastBody: m.body, lastDate: m.timestamp,
                                        outgoing: m.direction == .outgoing,
                                        unread: unread[key] ?? 0)
            }
        return latest.values.sorted { $0.lastDate > $1.lastDate }
    }

    /// The pinned team conversation — the node's team thread, synthesised when it has no messages yet so the
    /// row is always there to tap. nil when the node is not on a team (a static node's list is unchanged).
    private var pinnedTeam: ThreadSummary? {
        guard let key = model.teamChatThread() else { return nil }
        if let existing = summaries.first(where: { $0.key == key }) { return existing }
        return ThreadSummary(key: key,
                             title: threadTitle(key, nodesByHash: nodesByHash, channelLabels: channelLabels),
                             lastBody: "No messages yet — say hello to your team.",
                             lastDate: .distantPast, outgoing: false, unread: 0)
    }
    /// Everything except the pinned team thread (it is rendered in its own section above), minus BLOCKED
    /// peers' threads — their messages stay archived, they just never surface here.
    private var otherSummaries: [ThreadSummary] {
        let teamKey = model.teamChatThread()
        return summaries.filter { s in
            if let teamKey, s.key == teamKey { return false }
            if case .dm(let h) = s.key { return !model.isBlocked(threadHash: h.value) }
            return true
        }
    }
}

/// Compose entry point (2026-07-31): ONE button, two paths. The DM path is what the screen was missing —
/// until now a conversation could only be started from Contacts/Mesh.
private struct NewMessageSheet: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    @Query(filter: #Predicate<NodeEntity> { $0.name != nil || $0.favorite }, sort: \NodeEntity.name)
    private var contacts: [NodeEntity]
    @Query private var labels: [ChannelLabelEntity]
    @State private var channelID = ""
    @State private var draft = ""

    var body: some View {
        NavigationStack {
            Form {
                Section("Direct message") {
                    if contacts.isEmpty {
                        Text("No contacts yet — add one from the Mesh or Contacts tab.")
                            .font(.caption).foregroundStyle(.secondary)
                    } else {
                        ForEach(contacts) { c in
                            Button {
                                dismiss()
                                model.openConversation(threadHash: c.hash32)   // straight into the thread
                            } label: {
                                HStack(spacing: 10) {
                                    Image(systemName: c.teamID != nil ? "person.3.fill" : "person.crop.circle.fill")
                                        .foregroundStyle(c.teamID != nil ? Color.teal : Color.accentColor)
                                    VStack(alignment: .leading, spacing: 1) {
                                        Text(c.displayName()).foregroundStyle(.primary)
                                        Text("0x" + c.keyHash.hex8).font(.caption2).monospaced()
                                            .foregroundStyle(.secondary)
                                    }
                                }
                            }
                        }
                    }
                }

                Section {
                    LabeledContent("Channel") {
                        TextField(defaultChannelPlaceholder, text: $channelID)
                            .keyboardType(.numberPad).multilineTextAlignment(.trailing)
                    }
                    TextField("Message", text: $draft, axis: .vertical).lineLimit(1...4)
                    Button("Post") {
                        if let c = effectiveChannel, !draft.trimmingCharacters(in: .whitespaces).isEmpty {
                            model.sendChannel(c, body: draft)
                            dismiss()
                        }
                    }
                    .disabled(effectiveChannel == nil || draft.trimmingCharacters(in: .whitespaces).isEmpty)
                } header: {
                    Text(model.teamID != nil ? "Team post" : "Channel post")
                } footer: {
                    Text(model.teamID != nil
                         ? "Goes to your team\(model.teamChannelWillSeal ? ", encrypted" : " in clear (no team key)"). Leave the channel blank to use the team's default."
                         : "A broadcast to everyone on this channel in your leaf.")
                }
            }
            .navigationTitle("New message").navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } } }
        }
    }

    /// On a team, blank = the team's default channel (the same one the pinned thread uses).
    private var effectiveChannel: UInt8? {
        if let v = UInt8(channelID.trimmingCharacters(in: .whitespaces)) { return v }
        guard channelID.trimmingCharacters(in: .whitespaces).isEmpty, model.teamID != nil else { return nil }
        if case .teamChannel(_, let c)? = model.teamChatThread() { return c }
        return 1
    }
    private var defaultChannelPlaceholder: String {
        guard model.teamID != nil, case .teamChannel(_, let c)? = model.teamChatThread() else { return "3" }
        return "\(c) (team)"
    }
}

struct ThreadRow: View {
    @Environment(AppModel.self) private var model
    let summary: ThreadSummary
    /// ★ A DM outranks broadcast traffic (owner ruling 2026-07-31): an unread DM gets the full-weight
    /// treatment — accent badge, bold title, an unread dot. Channel/team unread stays deliberately quieter
    /// so a chatty team can never drown out a person messaging you. A MUTED thread is quieter still.
    private var isDM: Bool { summary.key.isDirect }
    private var muted: Bool { model.isMuted(summary.key) }
    private var hasUnread: Bool { summary.unread > 0 }
    private var loud: Bool { hasUnread && isDM && !muted }

    var body: some View {
        HStack(spacing: 12) {
            if loud {   // a leading dot only for what deserves immediate attention
                Circle().fill(Color.accentColor).frame(width: 8, height: 8)
            } else {
                Circle().fill(.clear).frame(width: 8, height: 8)
            }
            Image(systemName: rowIcon).font(.title2).foregroundStyle(rowColor)
            VStack(alignment: .leading, spacing: 2) {
                HStack(spacing: 4) {
                    Text(summary.title)
                        .font(.headline)
                        .fontWeight(loud ? .bold : (hasUnread && !muted ? .semibold : .regular))
                    if muted {
                        Image(systemName: "bell.slash.fill").font(.caption2).foregroundStyle(.tertiary)
                    }
                }
                Text((summary.outgoing ? "You: " : "") + summary.lastBody)
                    .font(.subheadline).lineLimit(1)
                    .foregroundStyle(loud ? Color.primary : Color.secondary)
                    .fontWeight(loud ? .semibold : .regular)
            }
            Spacer()
            VStack(alignment: .trailing, spacing: 4) {
                Text(summary.lastDate.shortRelative).font(.caption2).foregroundStyle(.tertiary)
                if hasUnread {
                    Text("\(summary.unread)")
                        .font(.caption2.bold())
                        .foregroundStyle(loud ? Color.white : Color.secondary)
                        .padding(.horizontal, 7).padding(.vertical, 2)
                        .background(Capsule().fill(loud ? Color.accentColor : Color.secondary.opacity(0.18)))
                }
            }
        }
        .padding(.vertical, 2)
    }
    private var rowIcon: String {
        switch summary.key {
        case .dm:          return "person.crop.circle.fill"
        case .channel:     return "number.circle.fill"
        case .teamChannel: return "person.3.fill"          // D30: team group chat
        }
    }
    private var rowColor: Color {
        switch summary.key {
        case .dm:          return Color.accentColor
        case .channel:     return .orange
        case .teamChannel: return .teal
        }
    }
}
