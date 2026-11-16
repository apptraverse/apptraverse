import AppTraverseMacUI
import Foundation

@main
private enum WindowCatalogMain {
    static func main() {
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
        let host = MacApplicationHost { delegate in
            ATCreateWindowCatalogApplicationBridge(directory.path, delegate)
        }
        exit(host.run())
    }
}
