import AppTraverseIosUI
import Foundation
import SwiftUI

private func stateDirectory() -> URL {
    if let index = CommandLine.arguments.firstIndex(of: "--state-dir"),
       CommandLine.arguments.indices.contains(index + 1) {
        return URL(fileURLWithPath: CommandLine.arguments[index + 1])
    }
    return FileManager.default.urls(
        for: .applicationSupportDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("AppTraverse/WindowCatalog", isDirectory: true)
}

@main
struct WindowCatalogIosApp: App {
    @StateObject private var host: IosApplicationHost

    init() {
        ATRegisterWindowCatalogIosApplication()
        let application = ATWindowCatalogIosApplication(
            userDirectory: stateDirectory().path)
        _host = StateObject(wrappedValue: IosApplicationHost { delegate in
            application.makeBridge(delegate)
        })
    }

    var body: some Scene {
        WindowGroup { IosApplicationRootView(host: host) }
    }
}
