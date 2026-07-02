// MeshRouteCompanion — shared view helpers.

import SwiftUI
import MeshRouteWire
import MeshRouteCore

/// One row's worth of a thread (latest message + a display title + the unread count).
struct ThreadSummary: Identifiable {
    let key: ThreadKey
    let title: String
    let lastBody: String
    let lastDate: Date
    let outgoing: Bool
    var unread: Int = 0
    var id: ThreadKey { key }
}

/// A human title for a thread: the contact's name, an unresolved "Node N", a raw hash, or the
/// channel's local label ("Sailing crew") falling back to "Channel N".
func threadTitle(_ key: ThreadKey, nodesByHash: [UInt32: NodeEntity],
                 channelLabels: [Int: String] = [:]) -> String {
    switch key {
    case .dm(let h):
        if let n = nodesByHash[h.value] { return n.displayName() }
        if h.value <= 254 { return "Node \(h.value)" }      // unresolved id-only DM thread
        return "0x" + h.hex8
    case .channel(let c):
        return channelLabels[Int(c)] ?? "Channel \(c)"
    }
}

func threadSubtitle(_ key: ThreadKey) -> String {
    switch key {
    case .dm(let h):      return "0x" + h.hex8
    case .channel(let c): return "broadcast · ch \(c)"
    }
}

/// Delivery-state glyph for an outgoing message.
struct DeliveryBadge: View {
    let state: DeliveryState
    var body: some View {
        switch state {
        case .outbox:   Image(systemName: "tray.and.arrow.up").foregroundStyle(.secondary)
        case .sending:  Image(systemName: "clock").foregroundStyle(.secondary)
        case .queued:   Image(systemName: "arrow.up.circle").foregroundStyle(.secondary)
        case .acked:        Image(systemName: "checkmark.circle.fill").foregroundStyle(.green)
        case .deliveredE2E: Image(systemName: "checkmark.seal.fill").foregroundStyle(.green)   // recipient confirmed end-to-end (D25)
        case .failed:       Image(systemName: "exclamationmark.triangle.fill").foregroundStyle(.red)
        case .received:     EmptyView()
        }
    }
}

extension Date {
    var shortRelative: String {
        let f = RelativeDateTimeFormatter(); f.unitsStyle = .abbreviated
        return f.localizedString(for: self, relativeTo: .now)
    }
}
