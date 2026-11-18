import AppTraverseMacUI
import Foundation
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
private struct MinimalAppMain: App {
    @StateObject private var host: MacApplicationHost

    init() {
        ATRegisterMinimalMacApplication()
        let directory = userDirectory()
        _host = StateObject(wrappedValue: MacApplicationHost {
            ATCreateMinimalApplicationBridge(directory.path, $0)
        })
    }

    var body: some Scene { MacApplicationScenes(host: host) }
}
