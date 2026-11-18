import UIKit

// Hold the process briefly while the model saves after SwiftUI deactivation.
final class IosSuspensionAssertion {
    private var task = UIBackgroundTaskIdentifier.invalid

    func begin() {
        guard task == .invalid else { return }
        task = UIApplication.shared.beginBackgroundTask(withName: "AppTraverse state save") {
            [weak self] in self?.end()
        }
    }

    func end() {
        guard task != .invalid else { return }
        UIApplication.shared.endBackgroundTask(task)
        task = .invalid
    }

    deinit { end() }
}
