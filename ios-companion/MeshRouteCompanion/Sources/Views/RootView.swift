// MeshRouteCompanion — RootView: the three-tab shell.

import SwiftUI
import SwiftData

struct RootView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.scenePhase) private var scenePhase
    @Query(filter: #Predicate<MessageEntity> { $0.directionRaw == "incoming" && !$0.isRead })
    private var unread: [MessageEntity]

    var body: some View {
        @Bindable var model = model
        TabView(selection: $model.selectedTab) {
            ThreadsListView()
                .tabItem { Label("Messages", systemImage: "bubble.left.and.bubble.right") }
                .badge(unread.count)
                .tag(0)
            ContactsView()
                .tabItem { Label("Contacts", systemImage: "person.2") }
                .tag(1)
            NodeView()
                .tabItem { Label("Node", systemImage: "antenna.radiowaves.left.and.right") }
                .tag(2)
        }
        .tint(.accentColor)
        .onAppear {
            model.startDemoIfRequested()
            model.requestNotificationAuthorization()           // first launch → the iOS permission prompt
            model.setAppBadge(unread.count)
        }
        .onChange(of: unread.count) { _, c in model.setAppBadge(c) }   // app-icon badge mirrors unread
        .onChange(of: scenePhase) { _, phase in
            switch phase {
            case .active:            model.handleForeground()   // catch up anything missed while suspended
            case .background, .inactive: model.handleBackground()
            @unknown default:        break
            }
        }
    }
}
