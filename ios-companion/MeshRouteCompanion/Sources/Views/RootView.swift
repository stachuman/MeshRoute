// MeshRouteCompanion — RootView: the three-tab shell.

import SwiftUI
import SwiftData

struct RootView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.scenePhase) private var scenePhase
    @Query(filter: #Predicate<MessageEntity> { $0.directionRaw == "incoming" && !$0.isRead })
    private var unread: [MessageEntity]

    /// Unread that should actually nag: muted threads still show in-list but never badge (2026-07-31).
    private var badgeCount: Int { unread.filter { model.countsTowardBadge($0.threadKey) }.count }

    var body: some View {
        @Bindable var model = model
        TabView(selection: $model.selectedTab) {
            ThreadsListView()
                .tabItem { Label("Messages", systemImage: "bubble.left.and.bubble.right") }
                .badge(badgeCount)
                .tag(0)
            ContactsView()
                .tabItem { Label("Contacts", systemImage: "person.2") }
                .tag(1)
            MeshView()
                .tabItem { Label("Mesh", systemImage: "point.3.connected.trianglepath.dotted") }
                .tag(2)
            NodeView()
                .tabItem { Label("Device", systemImage: "antenna.radiowaves.left.and.right") }
                .tag(3)
        }
        .tint(.accentColor)
        .onAppear {
            model.startDemoIfRequested()
            model.requestNotificationAuthorization()           // first launch → the iOS permission prompt
            model.setAppBadge(badgeCount)
        }
        .onChange(of: badgeCount) { _, c in model.setAppBadge(c) }   // app-icon badge mirrors UNMUTED unread
        .onChange(of: scenePhase) { _, phase in
            switch phase {
            case .active:            model.handleForeground()   // catch up anything missed while suspended
            case .background, .inactive: model.handleBackground()
            @unknown default:        break
            }
        }
    }
}
