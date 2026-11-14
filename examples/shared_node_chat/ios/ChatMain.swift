import SwiftUI
import UIKit

final class PhoneChatHost: ObservableObject {
    let presenter: ChatPresenter
    private var started = false
    private var checkpointGeneration: UInt64 = 0
    private var backgroundTask: UIBackgroundTaskIdentifier = .invalid

    init() {
        let args = CommandLine.arguments
        func value(_ flag: String) -> String? {
            guard let i = args.firstIndex(of: flag), i + 1 < args.count,
                  !args[i + 1].hasPrefix("--") else { return nil }
            return args[i + 1]
        }
        let role: ChatRole = args.contains("--host") ? .host : .client
        let profile = value("--profile") ?? FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("AppTraverse/SharedNodeChat/" + (role == .host ? "host" : "client")).path
        presenter = ChatPresenter(profile: profile, name: value("--name") ?? "iPhone", role: role,
                                  initialHost: value("--client") ?? "")
    }
    deinit {
        if backgroundTask != .invalid { UIApplication.shared.endBackgroundTask(backgroundTask) }
        presenter.stop()
    }
    func start() {
        guard !started else { return }
        guard presenter.start() else {
            preconditionFailure("Cannot open chat profile.")
        }
        started = true
    }
    func checkpoint() {
        guard started, backgroundTask == .invalid else { return }
        // Keep only enough background execution time to drain the model queue
        // and persist USER. Never stop/recreate the session on deactivation.
        checkpointGeneration &+= 1
        let generation = checkpointGeneration
        backgroundTask = UIApplication.shared.beginBackgroundTask(withName: "Save chat") { [weak self] in
            self?.finishCheckpoint(generation)
        }
        presenter.checkpoint { [weak self] ok in
            guard let self else { return }
            if !ok { fatalError("Could not save chat USER state") }
            self.finishCheckpoint(generation)
        }
    }
    private func finishCheckpoint(_ generation: UInt64) {
        guard generation == checkpointGeneration, backgroundTask != .invalid else { return }
        let task = backgroundTask; backgroundTask = .invalid
        UIApplication.shared.endBackgroundTask(task)
    }
}

struct PhoneChatView: View {
    @ObservedObject var presenter: ChatPresenter
    var body: some View { ChatPanel(presenter: presenter) }
}

@main
struct PhoneChatApp: App {
    @StateObject private var host = PhoneChatHost()
    @Environment(\.scenePhase) private var scenePhase
    var body: some Scene {
        WindowGroup {
            PhoneChatView(presenter: host.presenter).task { host.start() }
                .onChange(of: scenePhase) { _, phase in
                    if phase == .inactive || phase == .background { host.checkpoint() }
                }
        }
    }
}
