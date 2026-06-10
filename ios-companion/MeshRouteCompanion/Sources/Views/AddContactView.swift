// MeshRouteCompanion — AddContactView: add a peer by key_hash32 (QR is a later enhancement).

import SwiftUI
import MeshRouteWire

struct AddContactView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    @State private var name = ""
    @State private var hashText = ""

    var body: some View {
        NavigationStack {
            Form {
                Section("Name") {
                    TextField("e.g. Bench-2", text: $name)
                }
                Section("key_hash32") {
                    TextField("8 hex digits, e.g. 8a3f1c02", text: $hashText)
                        .autocorrectionDisabled().textInputAutocapitalization(.never).monospaced()
                    if !hashText.isEmpty && parsedHash == nil {
                        Text("Not a valid hash (1–8 hex digits).").font(.caption).foregroundStyle(.red)
                    }
                }
                Section {
                    Text("The peer reads their own hash with `whoami` and shares it. Sending a DM addresses by hash, so the node resolves the current short id for you.")
                        .font(.caption).foregroundStyle(.secondary)
                }
            }
            .navigationTitle("Add contact")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Add") { add() }.disabled(!valid)
                }
            }
        }
    }

    private var parsedHash: KeyHash? { KeyHash(hex: hashText) }
    private var valid: Bool { !name.trimmingCharacters(in: .whitespaces).isEmpty && parsedHash != nil }

    private func add() {
        guard let hash = parsedHash else { return }
        model.addContact(name: name.trimmingCharacters(in: .whitespaces), hash: hash)
        dismiss()
    }
}
