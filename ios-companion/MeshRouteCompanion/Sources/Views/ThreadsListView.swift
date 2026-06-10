// MeshRouteCompanion — ThreadsListView: DM threads + channel feeds, latest first.

import SwiftUI
import SwiftData
import MeshRouteWire
import MeshRouteCore

struct ThreadsListView: View {
    @Environment(AppModel.self) private var model
    @Query(sort: \MessageEntity.timestamp, order: .reverse) private var messages: [MessageEntity]
    @Query private var contacts: [ContactEntity]
    @State private var showNewChannel = false

    var body: some View {
        NavigationStack {
            Group {
                if summaries.isEmpty {
                    ContentUnavailableView("No messages yet", systemImage: "tray",
                        description: Text("Connect to your node on the Node tab, then send or receive a message."))
                } else {
                    List(summaries) { s in
                        NavigationLink(value: s.key) { ThreadRow(summary: s) }
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
        }
    }

    private var contactsByHash: [UInt32: ContactEntity] {
        Dictionary(contacts.map { ($0.hashValue32, $0) }, uniquingKeysWith: { a, _ in a })
    }

    private var summaries: [ThreadSummary] {
        var latest: [ThreadKey: ThreadSummary] = [:]
        for m in messages where latest[m.threadKey] == nil {   // messages are reverse-sorted → first = latest
            let key = m.threadKey
            latest[key] = ThreadSummary(key: key,
                                        title: threadTitle(key, contactsByHash: contactsByHash),
                                        lastBody: m.body, lastDate: m.timestamp,
                                        outgoing: m.direction == .outgoing)
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
                Text(summary.title).font(.headline)
                Text((summary.outgoing ? "You: " : "") + summary.lastBody)
                    .font(.subheadline).foregroundStyle(.secondary).lineLimit(1)
            }
            Spacer()
            Text(summary.lastDate.shortRelative).font(.caption2).foregroundStyle(.tertiary)
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
