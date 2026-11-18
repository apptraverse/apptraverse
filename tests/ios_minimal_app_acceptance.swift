import Foundation
import SwiftUI
@testable import AppTraverseIosUI

private final class MinimalScenario {
    let host: IosApplicationHost
    let directory: URL
    private var started = false

    init(directory: URL) {
        self.directory = directory
        host = IosApplicationHost { ATCreateMinimalApplicationBridge(directory.path, $0) }
    }

    private func button(_ title: String) -> IosButtonAdapter {
        host.activeScene!.buttons.first { $0.title == title }!
    }

    @MainActor private func wait(_ name: String, until ready: () -> Bool) async {
        let deadline = Date().addingTimeInterval(30)
        while !ready() {
            precondition(Date() < deadline, "MinimalApp timed out: \(name)")
            try! await Task.sleep(nanoseconds: 30_000_000)
        }
        print("PASS: " + name)
        fflush(stdout)
    }

    @MainActor func run() async {
        guard !started else { return }
        started = true
        await wait("MinimalApp SwiftUI main layout mounts") {
            self.host.activeScene?.title == "Minimal App" &&
                self.host.activeScene?.snapshot?.buttons.count == 2 &&
                self.host.activeScene?.lists.first?.rows.count == 2 &&
                self.host.latestViewport.width > 0
        }
        let main = host.activeScene!
        let list = main.lists[0]
        let row = list.rows[1].id
        list.select(row)
        await wait("MinimalApp list uses model selection") { list.selectedIdentity == row }
        list.activate(row)
        await wait("MinimalApp row activation updates the title") { main.title == "Activated Row B" }
        button("Apply").click()
        await wait("MinimalApp Apply updates the title, list and enabled state") {
            main.title == "Applied" && !self.button("Apply").enabled && list.selectedIdentity == nil
        }
        button("Open Detail").click()
        await wait("MinimalApp Detail mounts through the shared SwiftUI host") {
            self.host.activeScene?.title == "Detail Panel" &&
                self.host.activeScene?.snapshot?.buttons.count == 2 &&
                self.host.activeScene?.lists.first?.rows.count == 2
        }
        let detail = host.activeScene!
        precondition(detail !== main && host.pageTokens.count == 1)
        button("Close Detail").click()
        await wait("MinimalApp Close releases Detail and restores the same main state") {
            self.host.activeScene === main && self.host.scenes[detail.id] == nil &&
                main.title == "Applied" && !self.button("Apply").enabled
        }
        host.requestBackgroundCheckpoint()
        await wait("MinimalApp saves separate USER and LOCAL on the continuing session") {
            self.host.lastBackgroundSaveExitCode() == 0 && self.host.activeScene === main
        }
        let files = (FileManager.default.enumerator(at: directory, includingPropertiesForKeys: nil)?
            .allObjects as? [URL] ?? []).filter { $0.lastPathComponent == "user.bin" }
        precondition(files.count == 2 && files.contains {
            $0.deletingLastPathComponent().lastPathComponent == "local"
        })
        precondition(files.allSatisfy { !(try! Data(contentsOf: $0)).isEmpty })
        host.requestStop()
        await wait("MinimalApp Debug stops and releases all presenters") {
            self.host.lastModelExitCode() == 0 && self.host.scenes.isEmpty &&
                self.host.buttons.isEmpty && self.host.lists.isEmpty
        }
        try! Data("PASS".utf8).write(to: directory.appendingPathComponent("result.txt"))
        exit(0)
    }
}

@main
private struct MinimalIosAcceptance: App {
    @StateObject private var host: IosApplicationHost
    private let scenario: MinimalScenario

    init() {
        ATRegisterMinimalIosApplication()
        let arguments = CommandLine.arguments
        let run = arguments[arguments.firstIndex(of: "--run-id")! + 1]
        let directory = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("MinimalAppAcceptance")
            .appendingPathComponent(run)
        let scenario = MinimalScenario(directory: directory)
        self.scenario = scenario
        _host = StateObject(wrappedValue: scenario.host)
    }

    var body: some Scene {
        WindowGroup {
            IosApplicationRootView(host: host).task { await scenario.run() }
        }
    }
}
