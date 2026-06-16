// MeshRouteCompanion — QRViews: in-person contact exchange (roadmap B1).
// MyCardView SHOWS the connected node's identity as a QR; ScanContactView SCANS a peer's card and
// adds/renames the contact. Physical presence is the trust ceremony — no signatures (D6: the app is
// crypto-free; a future v2 card carries the pubkey as opaque bytes).

import SwiftUI
import VisionKit
import CoreImage.CIFilterBuiltins
import MeshRouteWire
import MeshRouteCore

// ---- show my card ----

struct MyCardView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            Group {
                if let id = model.nodeIdentity {
                    // Include the node's ed_pub (E2E) so a scanner can install a PINNED key → sealed DMs (2026-06-16).
                    let card = ContactCard(name: id.name ?? "Node \(id.id)", hash: id.key, pubkeyHex: id.pubkey)
                    VStack(spacing: 16) {
                        if let img = qrUIImage(card.qrString) {
                            Image(uiImage: img)
                                .interpolation(.none)            // crisp modules, no smoothing
                                .resizable().scaledToFit()
                                .frame(maxWidth: 280)
                                .padding(12)
                                .background(RoundedRectangle(cornerRadius: 16).fill(.white))
                        }
                        Text(card.name).font(.title3.bold())
                        Text("0x" + card.hash.hex8).font(.callout).monospaced().foregroundStyle(.secondary)
                        Text("Have your peer scan this from their Contacts tab.")
                            .font(.footnote).foregroundStyle(.secondary).multilineTextAlignment(.center)
                    }
                    .padding()
                } else {
                    ContentUnavailableView("Not connected", systemImage: "qrcode",
                        description: Text("Connect to your node first — the card is the node's identity (name + key hash)."))
                }
            }
            .navigationTitle("My card")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } } }
        }
    }
}

func qrUIImage(_ string: String) -> UIImage? {
    let filter = CIFilter.qrCodeGenerator()
    filter.message = Data(string.utf8)
    filter.correctionLevel = "M"
    guard let ci = filter.outputImage else { return nil }
    let scaled = ci.transformed(by: CGAffineTransform(scaleX: 12, y: 12))
    guard let cg = CIContext().createCGImage(scaled, from: scaled.extent) else { return nil }
    return UIImage(cgImage: cg)
}

// ---- scan a peer's card ----

struct ScanContactView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    @State private var scanned: ContactCard?
    @State private var name = ""

    var body: some View {
        NavigationStack {
            Group {
                if let card = scanned {
                    Form {
                        Section("Scanned card") {
                            LabeledContent("key_hash32", value: "0x" + card.hash.hex8)
                            TextField("Name", text: $name)
                        }
                        Section {
                            Button("Add contact") {
                                model.addContact(name: name.isEmpty ? "0x" + card.hash.hex8 : name,
                                                 hash: card.hash)
                                if let p = card.pubkeyHex { model.provisionPeerKey(p) }   // E2E: PIN the verified key
                                dismiss()
                            }
                        } footer: {
                            Text(card.pubkeyHex != nil
                                 ? "Includes a verified key — encrypted DMs to this contact seal immediately."
                                 : "If this hash is already a contact, it is renamed.")
                        }
                    }
                } else if QRScannerView.isSupported {
                    ZStack(alignment: .bottom) {
                        QRScannerView { payload in
                            guard scanned == nil, let card = ContactCard(qrString: payload) else { return }
                            scanned = card
                            name = card.name
                        }
                        Text("Point at a MeshRoute contact QR")
                            .font(.footnote).padding(8)
                            .background(.thinMaterial, in: Capsule())
                            .padding(.bottom, 24)
                    }
                } else {
                    ContentUnavailableView("Camera scanning unavailable", systemImage: "qrcode.viewfinder",
                        description: Text("This device can't scan (Simulator?). Add the contact manually with + instead."))
                }
            }
            .navigationTitle("Scan contact")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } } }
        }
    }
}

/// VisionKit QR scanner (camera permission prompts on first start; needs real hardware).
struct QRScannerView: UIViewControllerRepresentable {
    let onScan: (String) -> Void
    static var isSupported: Bool { DataScannerViewController.isSupported }

    func makeUIViewController(context: Context) -> DataScannerViewController {
        let scanner = DataScannerViewController(recognizedDataTypes: [.barcode(symbologies: [.qr])],
                                                qualityLevel: .balanced,
                                                isHighlightingEnabled: true)
        scanner.delegate = context.coordinator
        try? scanner.startScanning()
        return scanner
    }
    func updateUIViewController(_ vc: DataScannerViewController, context: Context) {}
    func makeCoordinator() -> Coordinator { Coordinator(onScan: onScan) }

    final class Coordinator: NSObject, DataScannerViewControllerDelegate {
        let onScan: (String) -> Void
        init(onScan: @escaping (String) -> Void) { self.onScan = onScan }
        func dataScanner(_ scanner: DataScannerViewController,
                         didAdd addedItems: [RecognizedItem], allItems: [RecognizedItem]) {
            for case .barcode(let code) in addedItems {
                if let payload = code.payloadStringValue { onScan(payload) }
            }
        }
    }
}
