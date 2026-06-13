// MeshRouteCompanion — RootView: the three-tab shell.

import SwiftUI
import SwiftData

struct RootView: View {
    @Environment(AppModel.self) private var model
    @Query(filter: #Predicate<MessageEntity> { $0.directionRaw == "incoming" && !$0.isRead })
    private var unread: [MessageEntity]

    var body: some View {
        TabView {
            ThreadsListView()
                .tabItem { Label("Messages", systemImage: "bubble.left.and.bubble.right") }
                .badge(unread.count)
            ContactsView()
                .tabItem { Label("Contacts", systemImage: "person.2") }
            NodeView()
                .tabItem { Label("Node", systemImage: "antenna.radiowaves.left.and.right") }
        }
        .tint(.accentColor)
        .onAppear { model.startDemoIfRequested() }
    }
}
