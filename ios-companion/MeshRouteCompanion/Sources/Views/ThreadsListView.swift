// MeshRouteCompanion — ThreadsListView: DM threads + channel feeds, latest first.

import SwiftUI
import SwiftData
import MeshRouteWire
import MeshRouteCore

struct ThreadsListView: View {
    @Environment(AppModel.self) private var model
    @Query(sort: \MessageEntity.timestamp, order: .reverse) private var messages: [MessageEntity]
    @Query private var contacts: [ContactEntity]
    @Query private var labels: [ChannelLabelEntity]
    @State private var showNewChannel = false
    @State private var renameTarget: UInt8?         // channel id being renamed (drives the alert)
    @State private var renameText = ""

    var body: some View {
        @Bindable var model = model
        NavigationStack(path: $model.messagesPath) {   // bound so a notification tap can push a thread
            Group {
                if summaries.isEmpty {
                    ContentUnavailableView("No messages yet", systemImage: "tray",
                        description: Text("Connect to your node on the Node tab, then send or receive a message."))
                } else {
                    List(summaries) { s in
                        NavigationLink(value: s.key) { ThreadRow(summary: s) }
                            .contextMenu {
                                if case .channel(let c) = s.key {
                                    Button { startRename(c) } label: { Label("Rename channel", systemImage: "pencil") }
                                }
                            }
                    }
                }
            }
            .navigationTitle("Messages")
            .navigationDestination(for: ThreadKey.self) { ThreadView(thread: $0) }
            .toolbar {
                ToolbarItem(placement: .topBarLeading) { ConnectionPill() }
                ToolbarItem(placement: .topBarTrailing) {
                    Button { showNewChannel = true } label: { Image(systemName: "square.and.pencil") }
                }
            }
            .sheet(isPresented: $showNewChannel) { NewChannelSheet() }
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

    private var contactsByHash: [UInt32: ContactEntity] {
        Dictionary(contacts.map { ($0.hashValue32, $0) }, uniquingKeysWith: { a, _ in a })
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
                                        title: threadTitle(key, contactsByHash: contactsByHash,
                                                           channelLabels: channelLabels),
                                        lastBody: m.body, lastDate: m.timestamp,
                                        outgoing: m.direction == .outgoing,
                                        unread: unread[key] ?? 0)
            }
        return latest.values.sorted { $0.lastDate > $1.lastDate }
    }
}

struct ThreadRow: View {
    let summary: ThreadSummary
    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: isChannel ? "number.circle.fill" : "person.crop.circle.fill")
                .font(.title2).foregroundStyle(isChannel ? Color.orange : Color.accentColor)
            VStack(alignment: .leading, spacing: 2) {
                Text(summary.title).font(.headline).fontWeight(summary.unread > 0 ? .bold : .regular)
                Text((summary.outgoing ? "You: " : "") + summary.lastBody)
                    .font(.subheadline).lineLimit(1)
                    .foregroundStyle(summary.unread > 0 ? Color.primary : Color.secondary)
                    .fontWeight(summary.unread > 0 ? .semibold : .regular)
            }
            Spacer()
            VStack(alignment: .trailing, spacing: 4) {
                Text(summary.lastDate.shortRelative).font(.caption2).foregroundStyle(.tertiary)
                if summary.unread > 0 {
                    Text("\(summary.unread)")
                        .font(.caption2.bold()).foregroundStyle(.white)
                        .padding(.horizontal, 7).padding(.vertical, 2)
                        .background(Capsule().fill(Color.accentColor))
                }
            }
        }
        .padding(.vertical, 2)
    }
    private var isChannel: Bool { if case .channel = summary.key { return true }; return false }
}

private struct NewChannelSheet: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    @State private var channelID = "0"
    @State private var body_ = ""

    var body: some View {
        NavigationStack {
            Form {
                Section("Channel") {
                    TextField("Channel id (0–255)", text: $channelID).keyboardType(.numberPad)
                }
                Section("Message") {
                    TextField("Body", text: $body_, axis: .vertical).lineLimit(1...4)
                }
            }
            .navigationTitle("New channel post")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Post") { post() }.disabled(!valid)
                }
            }
        }
    }
    private var valid: Bool { UInt8(channelID) != nil && !body_.isEmpty }
    private func post() {
        guard let ch = UInt8(channelID) else { return }
        model.sendChannel(ch, body: body_); dismiss()
    }
}
