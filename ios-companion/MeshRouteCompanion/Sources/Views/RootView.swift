// MeshRouteCompanion — RootView: the three-tab shell.

import SwiftUI

struct RootView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        TabView {
            ThreadsListView()
                .tabItem { Label("Messages", systemImage: "bubble.left.and.bubble.right") }
            ContactsView()
                .tabItem { Label("Contacts", systemImage: "person.2") }
            NodeView()
                .tabItem { Label("Node", systemImage: "antenna.radiowaves.left.and.right") }
        }
        .tint(.accentColor)
        .onAppear { model.startDemoIfRequested() }
    }
}
