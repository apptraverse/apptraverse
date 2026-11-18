import SwiftUI

struct ChatView: View {
    @ObservedObject var presenter: ChatPresenter
    var body: some View {
        ChatPanel(presenter: presenter)
            .frame(minWidth: presenter.minimumWidth, minHeight: presenter.minimumHeight)
    }
}

@main
struct ChatMain: App {
    @NSApplicationDelegateAdaptor(ChatHost.self) private var host
    var body: some Scene {
        Window("SharedNode Chat", id: "chat") {
            ChatView(presenter: host.presenter)
                .background(ChatWindowAttachment(host: host).frame(width: 0, height: 0))
        }
        .defaultSize(width: 840, height: 560)
    }
}
