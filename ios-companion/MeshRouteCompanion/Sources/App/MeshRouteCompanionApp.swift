// MeshRouteCompanion — app entry point.

import SwiftUI
import SwiftData

@main
struct MeshRouteCompanionApp: App {
    @State private var model: AppModel
    private let container: ModelContainer

    init() {
        let schema: [any PersistentModel.Type] = [NodeEntity.self, MessageEntity.self, NodeProfileEntity.self,
                                                  ChannelLabelEntity.self, ContactEntity.self]   // ContactEntity = legacy (migrated → NodeEntity)
        let container: ModelContainer
        do {
            container = try ModelContainer(for: Schema(schema))
        } catch {
            // Fall back to in-memory so the app still launches if the on-disk store can't open.
            container = try! ModelContainer(for: Schema(schema),
                                            configurations: ModelConfiguration(isStoredInMemoryOnly: true))
        }
        self.container = container
        _model = State(initialValue: AppModel(modelContainer: container))
    }

    var body: some Scene {
        WindowGroup {
            RootView().environment(model)
        }
        .modelContainer(container)
    }
}
