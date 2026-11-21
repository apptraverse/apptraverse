import SwiftUI
import UIKit

final class PhonePushDelegate: NSObject, UIApplicationDelegate {
    var push: ChatPush?
    weak var host: PhoneChatHost?
    func application(_ application: UIApplication, didRegisterForRemoteNotificationsWithDeviceToken token: Data) { push?.token(token) }
    func application(_ application: UIApplication, didFailToRegisterForRemoteNotificationsWithError error: Error) {
        NSLog("APNs registration failed: %@", error.localizedDescription)
    }
    func applicationWillTerminate(_ application: UIApplication) { host?.stop() }
}

final class PhoneChatHost: ObservableObject {
    let presenter: ChatPresenter
    lazy var push = ChatPush(register: { [weak self] in self?.presenter.registerApplePushToken($0) }, open: {})
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
    func start(settings: ATIosDeviceSettings, viewport: CGSize) {
        presenter.observeMobileSettings(settings)
        presenter.reportMobileViewport(viewport)
        guard !started else { return }
        guard presenter.start() else {
            preconditionFailure("Cannot open chat profile.")
        }
        started = true
        push.start()
    }
    func stop() {
        started = false
        checkpointGeneration &+= 1
        presenter.stop()
        if backgroundTask != .invalid {
            let task = backgroundTask; backgroundTask = .invalid
            UIApplication.shared.endBackgroundTask(task)
        }
    }
    func checkpoint() {
        guard started, backgroundTask == .invalid else { return }
        // Keep only enough background execution time to drain the model queue
        // and persist device-local UI state. Chat business data stays in RAM.
        // Never stop/recreate the session on ordinary deactivation.
        checkpointGeneration &+= 1
        let generation = checkpointGeneration
        backgroundTask = UIApplication.shared.beginBackgroundTask(withName: "Save chat LOCAL") { [weak self] in
            guard let self, generation == self.checkpointGeneration,
                  self.backgroundTask != .invalid else { return }
            fatalError("Chat LOCAL checkpoint exceeded background execution")
        }
        presenter.checkpoint { [weak self] ok in
            guard let self else { return }
            if !ok { fatalError("Could not save chat LOCAL state") }
            self.finishCheckpoint(generation)
        }
    }
    private func finishCheckpoint(_ generation: UInt64) {
        guard generation == checkpointGeneration, backgroundTask != .invalid else { return }
        let task = backgroundTask; backgroundTask = .invalid
        UIApplication.shared.endBackgroundTask(task)
    }
}

private struct PhoneDeviceSettings: Equatable {
    let displayScale: Double, bodyTextScale: Double
    let dark: Bool, highContrast: Bool, reducedMotion: Bool, voiceOver: Bool
    var native: ATIosDeviceSettings {
        ATIosDeviceSettings(displayScale: displayScale, bodyTextScale: bodyTextScale,
            dark: dark, highContrast: highContrast, reducedMotion: reducedMotion, voiceOver: voiceOver)
    }
}

private struct PhoneChatView: View {
    @ObservedObject var presenter: ChatPresenter
    let mount: (ATIosDeviceSettings, CGSize) -> Void
    @Environment(\.displayScale) private var displayScale
    @Environment(\.colorScheme) private var scheme
    @Environment(\.colorSchemeContrast) private var contrast
    @Environment(\.accessibilityReduceMotion) private var reducedMotion
    @Environment(\.accessibilityVoiceOverEnabled) private var voiceOver
    @ScaledMetric(relativeTo: .body) private var bodyTextScale = 1.0
    private var settings: PhoneDeviceSettings {
        PhoneDeviceSettings(displayScale: displayScale, bodyTextScale: bodyTextScale,
            dark: scheme == .dark, highContrast: contrast == .increased,
            reducedMotion: reducedMotion, voiceOver: voiceOver)
    }
    var body: some View {
        GeometryReader { geometry in
            ChatPanel(presenter: presenter)
                .onAppear { mount(settings.native, geometry.size) }
                .onChange(of: settings) { _, settings in presenter.observeMobileSettings(settings.native) }
                .onChange(of: geometry.size) { _, size in presenter.reportMobileViewport(size) }
        }
    }
}

@main
struct PhoneChatApp: App {
    @UIApplicationDelegateAdaptor(PhonePushDelegate.self) private var delegate
    @StateObject private var host = PhoneChatHost()
    @Environment(\.scenePhase) private var scenePhase
    var body: some Scene {
        WindowGroup {
            PhoneChatView(presenter: host.presenter) { settings, viewport in
                delegate.host = host
                delegate.push = host.push
                host.start(settings: settings, viewport: viewport)
            }
                .onChange(of: scenePhase) { _, phase in
                    if phase == .active { host.push.active() }
                    if phase == .inactive || phase == .background { host.checkpoint() }
                }
        }
    }
}
