// MeshRouteCompanion — LocationViews: show + set a node's location (roadmap Theme C).
// A FIXED node is set once — by typing coordinates or tapping "Use my current location" (stand at the
// node). Sends `cfg set lat/lon` (degrees); the node persists to /mrloc and echoes the fresh cfg.

import SwiftUI
import MapKit
import CoreLocation
import MeshRouteWire
import MeshRouteCore

// ---- a read-only map preview for the Config section ----

struct NodeMapPreview: View {
    let lat: Double
    let lon: Double
    var body: some View {
        let coord = CLLocationCoordinate2D(latitude: lat, longitude: lon)
        Map(initialPosition: .region(MKCoordinateRegion(
                center: coord, span: MKCoordinateSpan(latitudeDelta: 0.02, longitudeDelta: 0.02)))) {
            Marker("Node", coordinate: coord)
        }
        .allowsHitTesting(false)   // preview only — tap the row to edit
    }
}

// ---- the set-location sheet ----

struct NodeLocationView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    let current: NodeConfigInfo
    @State private var latText: String
    @State private var lonText: String
    @State private var provider = LocationProvider()

    init(current: NodeConfigInfo) {
        self.current = current
        _latText = State(initialValue: current.hasPosition ? String(format: "%.6f", current.latitude) : "")
        _lonText = State(initialValue: current.hasPosition ? String(format: "%.6f", current.longitude) : "")
    }

    private var parsed: CLLocationCoordinate2D? {
        guard let la = Double(latText), let lo = Double(lonText), abs(la) <= 90, abs(lo) <= 180 else { return nil }
        return CLLocationCoordinate2D(latitude: la, longitude: lo)
    }

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    if let c = parsed {
                        NodeMapPreview(lat: c.latitude, lon: c.longitude)
                            .frame(height: 200).listRowInsets(EdgeInsets())
                            .id("\(latText),\(lonText)")   // rebuild → recenter when the coordinates change
                    } else {
                        Text("Enter a valid latitude / longitude, or use your current location.")
                            .font(.footnote).foregroundStyle(.secondary)
                    }
                }
                Section {
                    TextField("Latitude (−90…90)", text: $latText).keyboardType(.numbersAndPunctuation)
                    TextField("Longitude (−180…180)", text: $lonText).keyboardType(.numbersAndPunctuation)
                    Button { provider.requestOnce() } label: {
                        Label("Use my current location", systemImage: "location.fill")
                    }
                } header: {
                    Text("Coordinates")
                } footer: {
                    Text("Stand at the node, then tap “Use my current location”. Saved on the node (persists across reboot).")
                }
            }
            .navigationTitle("Node location")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Save") {
                        if let c = parsed { model.setNodeLocation(latitude: c.latitude, longitude: c.longitude); dismiss() }
                    }
                    .disabled(parsed == nil)
                }
            }
            .onChange(of: provider.lastLocation?.latitude) { _, _ in
                if let c = provider.lastLocation {
                    latText = String(format: "%.6f", c.latitude); lonText = String(format: "%.6f", c.longitude)
                }
            }
        }
    }
}

// ---- CoreLocation: one-shot "where am I" ----

@Observable
final class LocationProvider: NSObject, CLLocationManagerDelegate {
    private let manager = CLLocationManager()
    var lastLocation: CLLocationCoordinate2D?

    override init() {
        super.init()
        manager.delegate = self
        manager.desiredAccuracy = kCLLocationAccuracyBest
    }
    func requestOnce() {
        switch manager.authorizationStatus {
        case .notDetermined:                          manager.requestWhenInUseAuthorization()
        case .authorizedWhenInUse, .authorizedAlways: manager.requestLocation()
        default:                                      break   // denied/restricted → fall back to manual entry
        }
    }
    func locationManagerDidChangeAuthorization(_ m: CLLocationManager) {
        if m.authorizationStatus == .authorizedWhenInUse || m.authorizationStatus == .authorizedAlways {
            m.requestLocation()
        }
    }
    func locationManager(_ m: CLLocationManager, didUpdateLocations locs: [CLLocation]) {
        if let l = locs.last { lastLocation = l.coordinate }
    }
    func locationManager(_ m: CLLocationManager, didFailWithError error: Error) {}
}
