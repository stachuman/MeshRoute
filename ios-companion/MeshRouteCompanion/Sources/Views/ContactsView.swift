// MeshRouteCompanion — ContactsView: the name↔hash map the app owns (the reason the app exists).

import SwiftUI
import SwiftData
import MeshRouteWire
import MeshRouteCore

struct ContactsView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.modelContext) private var context
    // Contacts = the NAMED / favorited nodes in the directory (D28 / Option A).
    @Query(filter: #Predicate<NodeEntity> { $0.name != nil || $0.favorite }, sort: \NodeEntity.name)
    private var contacts: [NodeEntity]
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
        // "Remove contact" in the unified model = UN-NAME: it leaves Contacts but stays in the Mesh directory.
        for i in offsets { let n = contacts[i]; n.name = nil; n.favorite = false }
        try? context.save()
    }
}

struct ContactRow: View {
    let contact: NodeEntity
    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: "person.crop.circle.fill").font(.title2).foregroundStyle(Color.accentColor)
            VStack(alignment: .leading, spacing: 2) {
                Text(contact.displayName()).font(.headline)
                Text("0x" + contact.keyHash.hex8 + (contact.lastKnownID.map { " · id \($0)" } ?? " · id unknown"))
                    .font(.caption).foregroundStyle(.secondary).monospaced()
            }
        }
        .padding(.vertical, 2)
    }
}
