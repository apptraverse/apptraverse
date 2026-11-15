import Foundation
import UserNotifications
#if os(macOS)
import AppKit
#else
import UIKit
#endif

// Platform notification lifecycle only. Aether registration enters its model
// queue through the bridge; notification callbacks never mutate the UI graph.
final class ChatPush: NSObject, UNUserNotificationCenterDelegate {
    private var started = false
    private let register: (String) -> Void
    private let open: () -> Void
    init(register: @escaping (String) -> Void, open: @escaping () -> Void) {
        self.register = register; self.open = open
    }
    func start() {
        guard !started, Bundle.main.object(forInfoDictionaryKey: "ATPushEnvironment") as? String != nil else { return }
        started = true
        let center = UNUserNotificationCenter.current()
        center.delegate = self
        center.requestAuthorization(options: [.alert, .sound, .badge]) { granted, error in
            if let error { NSLog("Push authorization failed: %@", error.localizedDescription) }
            guard granted else { return }
            DispatchQueue.main.async {
                self.active()
                #if os(macOS)
                NSApplication.shared.registerForRemoteNotifications()
                #else
                UIApplication.shared.registerForRemoteNotifications()
                #endif
            }
        }
    }
    func token(_ data: Data) {
        guard let environment = Bundle.main.object(forInfoDictionaryKey: "ATPushEnvironment") as? String,
              ["sandbox", "production"].contains(environment), let topic = Bundle.main.bundleIdentifier else {
            preconditionFailure("Missing Apple push bundle configuration")
        }
        register(environment + "|" + topic + "|" + data.map { String(format: "%02x", $0) }.joined())
    }
    func active() {
        guard Bundle.main.object(forInfoDictionaryKey: "ATPushEnvironment") as? String != nil else { return }
        let center = UNUserNotificationCenter.current()
        center.removeAllDeliveredNotifications()
        center.removeAllPendingNotificationRequests()
        #if os(iOS)
        UIApplication.shared.applicationIconBadgeNumber = 0
        #endif
    }
    func userNotificationCenter(_ center: UNUserNotificationCenter, willPresent notification: UNNotification,
                                withCompletionHandler completion: @escaping (UNNotificationPresentationOptions) -> Void) {
        // Visible chat consumes ordinary SharedNode data; suppress duplicate alerts.
        completion([])
    }
    func userNotificationCenter(_ center: UNUserNotificationCenter, didReceive response: UNNotificationResponse,
                                withCompletionHandler completion: @escaping () -> Void) {
        DispatchQueue.main.async { self.open(); self.active(); completion() }
    }
}
