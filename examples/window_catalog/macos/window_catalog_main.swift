import AppTraverseMacUI
import Foundation
import SwiftUI

@main
private struct WindowCatalogMain: App {
    @StateObject private var host: MacApplicationHost

    init() {
        ATRegisterWindowCatalogApplication()
        let arguments = CommandLine.arguments
        if let index = arguments.firstIndex(of: "--distill-only"),
           arguments.indices.contains(index + 1) {
            exit(ATDistillWindowCatalogApplication(arguments[index + 1]) ? 0 : 2)
        }
        let directory: URL
        if let index = arguments.firstIndex(of: "--state-dir"),
           arguments.indices.contains(index + 1) {
            directory = URL(fileURLWithPath: arguments[index + 1])
        } else {
            directory = FileManager.default.urls(
                for: .applicationSupportDirectory, in: .userDomainMask)[0]
                .appendingPathComponent("AppTraverse/WindowCatalog", isDirectory: true)
        }
        _host = StateObject(wrappedValue: MacApplicationHost {
            ATCreateWindowCatalogApplicationBridge(directory.path, $0)
        })
    }

    var body: some Scene { MacApplicationScenes(host: host) }
}
