// MeshRouteCompanion — ContactsView: the name↔hash map the app owns (the reason the app exists).

import SwiftUI
import SwiftData
import MeshRouteWire
import MeshRouteCore

struct ContactsView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.modelContext) private var context
    @Query(sort: \ContactEntity.name) private var contacts: [ContactEntity]
    @State private var showAdd = false
    @State private var showMyCard = false
    @State private var showScanner = false

    var body: some View {
        NavigationStack {
            Group {
                if contacts.isEmpty {
                    ContentUnavailableView {
                        Label("No contacts", systemImage: "person.crop.circle.badge.plus")
                    } description: {
                        Text("The node knows only hashes + short ids — add a peer by their key_hash32 to give them a name.")
                    } actions: {
                        Button("Add contact") { showAdd = true }
                        Button("Scan a QR card") { showScanner = true }
                    }
                } else {
                    List {
                        ForEach(contacts) { c in
                            NavigationLink(value: ThreadKey.dm(c.keyHash)) {
                                ContactRow(contact: c)
                            }
                            .swipeActions(edge: .leading) {
                                Button { model.resolve(c.keyHash) } label: { Label("Resolve", systemImage: "location.magnifyingglass") }
                                    .tint(.blue)
                            }
                        }
                        .onDelete(perform: delete)
                    }
                }
            }
            .navigationTitle("Contacts")
            .navigationDestination(for: ThreadKey.self) { ThreadView(thread: $0) }
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button { showMyCard = true } label: { Image(systemName: "qrcode") }
                }
                ToolbarItemGroup(placement: .topBarTrailing) {
                    Button { showScanner = true } label: { Image(systemName: "qrcode.viewfinder") }
                    Button { showAdd = true } label: { Image(systemName: "plus") }
                }
            }
            .sheet(isPresented: $showAdd) { AddContactView() }
            .sheet(isPresented: $showMyCard) { MyCardView() }
            .sheet(isPresented: $showScanner) { ScanContactView() }
        }
    }

    private func delete(_ offsets: IndexSet) {
        for i in offsets { context.delete(contacts[i]) }
        try? context.save()
    }
}

struct ContactRow: View {
    let contact: ContactEntity
    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: "person.crop.circle.fill").font(.title2).foregroundStyle(Color.accentColor)
            VStack(alignment: .leading, spacing: 2) {
                Text(contact.name).font(.headline)
                Text("0x" + contact.keyHash.hex8 + (contact.lastKnownID.map { " · id \($0)" } ?? " · id unknown"))
                    .font(.caption).foregroundStyle(.secondary).monospaced()
            }
        }
        .padding(.vertical, 2)
    }
}
