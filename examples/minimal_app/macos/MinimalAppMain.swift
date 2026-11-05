import AppTraverseMacUI
import Foundation

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
private enum MinimalAppMain {
    static func main() {
        let directory = userDirectory()
        let host = MacApplicationHost { delegate in
            ATCreateMinimalApplicationBridge(directory.path, delegate)
        }
        host.run()
    }
}
