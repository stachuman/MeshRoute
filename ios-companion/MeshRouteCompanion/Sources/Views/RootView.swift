// MeshRouteCompanion — RootView: the three-tab shell.

import SwiftUI
import SwiftData

struct RootView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.scenePhase) private var scenePhase
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
        .onAppear {
            model.startDemoIfRequested()
            model.requestNotificationAuthorization()           // first launch → the iOS permission prompt
        }
        .onChange(of: scenePhase) { _, phase in
            switch phase {
            case .active:            model.handleForeground()   // catch up anything missed while suspended
            case .background, .inactive: model.handleBackground()
            @unknown default:        break
            }
        }
    }
}
