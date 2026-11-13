import AppTraverseIosUI
import SwiftUI

private func userDirectory() -> URL {
    if let index = CommandLine.arguments.firstIndex(of: "--state-dir"),
       CommandLine.arguments.indices.contains(index + 1) {
        return URL(fileURLWithPath: CommandLine.arguments[index + 1])
    }
    return FileManager.default.urls(
        for: .applicationSupportDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("AppTraverse/MinimalApp", isDirectory: true)
}

@main
struct MinimalAppIosApp: App {
    init() { ATRegisterMinimalIosApplication() }
    @StateObject private var host = IosApplicationHost { delegate in
        ATCreateMinimalApplicationBridge(userDirectory().path, delegate)
    }

    var body: some Scene {
        WindowGroup {
            IosApplicationRootView(host: host)
        }
    }
}
