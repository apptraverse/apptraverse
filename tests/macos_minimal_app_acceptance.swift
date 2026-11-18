import Foundation
import SwiftUI
@testable import AppTraverseMacUI

@main
private struct MacMinimalAppAcceptance: App {
    @StateObject private var host: MacApplicationHost

    init() {
        ATRegisterMinimalMacApplication()
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent("apptraverse-minimal-\(UUID())", isDirectory: true)
        let host = MacApplicationHost { ATCreateMinimalApplicationBridge(directory.path, $0) }
        _host = StateObject(wrappedValue: host)
        host.onFinished = { [weak host] status in
            precondition(status == 0 && host?.windows.isEmpty == true)
            let files = FileManager.default.enumerator(at: directory, includingPropertiesForKeys: nil)
            let saved = (files?.allObjects as? [URL] ?? []).first { $0.lastPathComponent == "user.bin" }
            precondition(saved != nil && !(try! Data(contentsOf: saved!)).isEmpty)
            try! FileManager.default.removeItem(at: directory)
            print("PASS: MinimalApp SwiftUI primary close saves USER and tears down")
            exit(status)
        }
        Task { @MainActor in
            let deadline = Date().addingTimeInterval(30)
            func wait(_ step: String, until ready: () -> Bool) async {
                while !ready() {
                    precondition(Date() < deadline, "MinimalApp timed out: \(step)")
                    try! await Task.sleep(nanoseconds: 25_000_000)
                }
                print("PASS: \(step)")
                fflush(stdout)
            }
            await wait("MinimalApp mounts its primary SwiftUI scene") {
                host.window(for: host.primarySceneID)?.clientSize != nil
            }
            let main = host.window(for: host.primarySceneID)!
            main.buttons.first { $0.title == "Open Detail" }!.click()
            var detail: MacWindowAdapter? {
                host.windows.values.first { $0.title == "Detail Panel" }
            }
            await wait("MinimalApp mounts the Detail Panel scene") {
                guard let detail else { return false }
                return detail.clientSize != nil
            }
            let detailID = detail!.sceneID
            detail!.buttons.first { $0.title == "Close Detail" }!.click()
            await wait("MinimalApp removes a model-closed detail scene") {
                detail == nil && host.sceneDismissals[detailID] == nil
            }
            precondition(host.window(for: host.primarySceneID) === main)
            host.requestClose(host.primarySceneID)
        }
    }

    var body: some Scene { MacApplicationScenes(host: host) }
}
