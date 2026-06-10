// swift-tools-version: 6.0
// MeshRouteKit — the platform-agnostic core of the MeshRoute iOS companion.
//
// Mirrors the firmware's lib/core discipline: pure, transport-free, and unit-testable
// headlessly (`swift test`) on macOS. CoreBluetooth / SwiftData / SwiftUI live in the
// app target, NOT here — so the protocol codec and domain logic are proven without a
// simulator, exactly like the firmware proves lib/core against a FakeClock + MockRadio.
import PackageDescription

let package = Package(
    name: "MeshRouteKit",
    platforms: [.iOS(.v17), .macOS(.v14)],
    products: [
        .library(name: "MeshRouteWire", targets: ["MeshRouteWire"]),
        .library(name: "MeshRouteCore", targets: ["MeshRouteCore"]),
    ],
    targets: [
        // The wire contract: line-ASCII command encoder + JSON push decoder.
        .target(name: "MeshRouteWire"),
        // Domain models, the NodeTransport seam, NodeSession, and a MockNodeTransport.
        .target(name: "MeshRouteCore", dependencies: ["MeshRouteWire"]),
        .testTarget(name: "MeshRouteWireTests", dependencies: ["MeshRouteWire"]),
        .testTarget(name: "MeshRouteCoreTests", dependencies: ["MeshRouteCore"]),
    ],
    swiftLanguageModes: [.v5]
)
