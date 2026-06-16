// MeshRouteCompanion — ThreadView: one conversation (DM or channel) + compose bar.

import SwiftUI
import SwiftData
import MeshRouteWire
import MeshRouteCore

struct ThreadView: View {
    @Environment(AppModel.self) private var model
    @Query private var messages: [MessageEntity]
    @Query private var contacts: [ContactEntity]
    @Query private var labels: [ChannelLabelEntity]
    let thread: ThreadKey
    @State private var draft = ""
    @State private var requestAck = false       // per-message E2E delivery-ack toggle (DM only, D16)
    private var isDM: Bool { if case .dm = thread { return true }; return false }

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
            ComposeBar(text: $draft, byteLimit: byteLimit,
                       requestAck: isDM ? $requestAck : nil, onSend: send)
        }
        .navigationTitle(title)
        .navigationBarTitleDisplayMode(.inline)
        .onAppear { model.markThreadRead(thread) }
        .onChange(of: messages.count) { _, _ in model.markThreadRead(thread) }   // arrivals while viewing
    }

    private var byteLimit: Int {
        if case .channel = thread { return WireConstants.channelMaxBodyBytes }
        return WireConstants.dmMaxBodyBytes
    }

    private var title: String {
        let byHash = Dictionary(contacts.map { ($0.hashValue32, $0) }, uniquingKeysWith: { a, _ in a })
        let byChannel = Dictionary(labels.map { ($0.channelID, $0.name) }, uniquingKeysWith: { a, _ in a })
        return threadTitle(thread, contactsByHash: byHash, channelLabels: byChannel)
    }

    private func send() {
        let body = draft.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !body.isEmpty else { return }
        switch thread {
        case .dm:             model.sendDM(to: thread, body: body, requestAck: requestAck)
        case .channel(let c): model.sendChannel(c, body: body)
        }
        draft = ""
    }
}

struct MessageBubble: View {
    @Environment(AppModel.self) private var model
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
                    // crypted/plaintext marker (E2E): a closed lock when encrypted, open when not
                    Image(systemName: message.crypted ? "lock.fill" : "lock.open")
                        .font(.system(size: 9))
                        .foregroundStyle(message.crypted ? Color.green : Color.secondary)
                    if outgoing && message.ackRequested {       // an E2E delivery ack was requested (D16)
                        Image(systemName: "checkmark.seal").font(.system(size: 9)).foregroundStyle(.tertiary)
                    }
                    if let c = message.ctr {                    // the node message counter, small
                        Text("#\(c)").font(.caption2).foregroundStyle(.tertiary).monospaced()
                    }
                    Text(message.timestamp.shortRelative).font(.caption2).foregroundStyle(.tertiary)
                    if outgoing { DeliveryBadge(state: message.state).font(.caption2) }
                }
                if outgoing && message.state == .failed {
                    Button { model.retry(message) } label: {
                        Label("Tap to retry", systemImage: "arrow.clockwise")
                            .font(.caption2).foregroundStyle(.red)
                    }
                    .buttonStyle(.plain)
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
    var requestAck: Binding<Bool>?    // nil = no toggle (channels); a binding = show the per-message ack toggle
    var onSend: () -> Void

    var body: some View {
        VStack(spacing: 2) {
            if overBudget {
                Text("\(used)/\(byteLimit) bytes — too long")
                    .font(.caption2).foregroundStyle(.red).frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.horizontal, 8)
            }
            HStack(spacing: 8) {
                if let ack = requestAck {     // request E2E delivery confirmation for this message (off by default, D16)
                    Button { ack.wrappedValue.toggle() } label: {
                        Image(systemName: ack.wrappedValue ? "checkmark.seal.fill" : "checkmark.seal")
                            .font(.title3).foregroundStyle(ack.wrappedValue ? Color.accentColor : .secondary)
                    }
                    .accessibilityLabel("Request delivery confirmation")
                }
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
