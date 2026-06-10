// MeshRouteCompanion — ThreadView: one conversation (DM or channel) + compose bar.

import SwiftUI
import SwiftData
import MeshRouteWire
import MeshRouteCore

struct ThreadView: View {
    @Environment(AppModel.self) private var model
    @Query private var messages: [MessageEntity]
    @Query private var contacts: [ContactEntity]
    let thread: ThreadKey
    @State private var draft = ""

    init(thread: ThreadKey) {
        self.thread = thread
        let predicate: Predicate<MessageEntity>
        switch thread {
        case .dm(let h):
            let hv = h.value
            predicate = #Predicate<MessageEntity> { $0.threadKind == "dm" && $0.threadHash == hv }
        case .channel(let c):
            let ci = Int(c)
            predicate = #Predicate<MessageEntity> { $0.threadKind == "channel" && $0.threadChannel == ci }
        }
        _messages = Query(filter: predicate, sort: \MessageEntity.timestamp, order: .forward)
    }

    var body: some View {
        VStack(spacing: 0) {
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(spacing: 8) {
                        ForEach(messages) { MessageBubble(message: $0) }
                    }
                    .padding()
                }
                .onChange(of: messages.count) { _, _ in
                    if let last = messages.last {
                        withAnimation { proxy.scrollTo(last.id, anchor: .bottom) }
                    }
                }
            }
            Divider()
            ComposeBar(text: $draft, byteLimit: byteLimit, onSend: send)
        }
        .navigationTitle(title)
        .navigationBarTitleDisplayMode(.inline)
    }

    private var byteLimit: Int {
        if case .channel = thread { return WireConstants.channelMaxBodyBytes }
        return WireConstants.dmMaxBodyBytes
    }

    private var title: String {
        let byHash = Dictionary(contacts.map { ($0.hashValue32, $0) }, uniquingKeysWith: { a, _ in a })
        return threadTitle(thread, contactsByHash: byHash)
    }

    private func send() {
        let body = draft.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !body.isEmpty else { return }
        switch thread {
        case .dm:             model.sendDM(to: thread, body: body)
        case .channel(let c): model.sendChannel(c, body: body)
        }
        draft = ""
    }
}

struct MessageBubble: View {
    let message: MessageEntity
    private var outgoing: Bool { message.direction == .outgoing }

    var body: some View {
        HStack {
            if outgoing { Spacer(minLength: 40) }
            VStack(alignment: outgoing ? .trailing : .leading, spacing: 2) {
                Text(message.body)
                    .padding(.horizontal, 12).padding(.vertical, 8)
                    .background(outgoing ? Color.accentColor.opacity(0.9) : Color.gray.opacity(0.15))
                    .foregroundStyle(outgoing ? Color.white : Color.primary)
                    .clipShape(RoundedRectangle(cornerRadius: 16))
                HStack(spacing: 4) {
                    if let o = message.origin, !outgoing {
                        Text("id \(o)").font(.caption2).foregroundStyle(.tertiary)
                    }
                    Text(message.timestamp.shortRelative).font(.caption2).foregroundStyle(.tertiary)
                    if outgoing { DeliveryBadge(state: message.state).font(.caption2) }
                }
            }
            if !outgoing { Spacer(minLength: 40) }
        }
        .id(message.id)
    }
}

struct ComposeBar: View {
    @Binding var text: String
    var byteLimit: Int
    var onSend: () -> Void

    var body: some View {
        VStack(spacing: 2) {
            if overBudget {
                Text("\(used)/\(byteLimit) bytes — too long")
                    .font(.caption2).foregroundStyle(.red).frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.horizontal, 8)
            }
            HStack(spacing: 8) {
                TextField("Message", text: $text, axis: .vertical)
                    .lineLimit(1...4).textFieldStyle(.roundedBorder)
                Button(action: onSend) { Image(systemName: "arrow.up.circle.fill").font(.title2) }
                    .disabled(text.trimmingCharacters(in: .whitespaces).isEmpty || overBudget)
            }
        }
        .padding(8)
    }
    private var used: Int { text.utf8.count }
    private var overBudget: Bool { used > byteLimit }
}
