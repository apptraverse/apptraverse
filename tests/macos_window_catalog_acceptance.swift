import Foundation
import SwiftUI
@testable import AppTraverseMacUI

private final class CatalogScenario {
    let host: MacApplicationHost
    let directory: URL
    let evidence: URL
    let cold: Bool

    init(directory: URL, evidence: URL, cold: Bool) {
        self.directory = directory
        self.evidence = evidence
        self.cold = cold
        host = MacApplicationHost(followsSystemAppearance: true) {
            ATCreateWindowCatalogApplicationBridge(directory.path, $0)
        }
    }

    private var main: MacWindowAdapter? { host.window(for: host.primarySceneID) }
    private var list: MacListAdapter? { main?.lists.first }

    private func child(_ index: Int) -> MacWindowAdapter? {
        host.windows.values.first { $0.title == "Window \(index)" || $0.title == "Окно \(index)" }
    }

    private func mounted(_ window: MacWindowAdapter?) -> Bool {
        guard let window else { return false }
        return window.clientSize != nil
    }

    private func button(_ title: String) -> MacButtonAdapter {
        main!.buttons.first { $0.title == title }!
    }

    private func close(_ window: MacWindowAdapter) {
        if #available(macOS 14.0, *) {
            host.sceneDismissals[window.sceneID]!()
        } else {
            host.requestClose(window.sceneID)
        }
    }

    private func select(_ id: UInt64) { list!.selection.wrappedValue = id }

    private func savedFiles() -> [URL] {
        let files = FileManager.default.enumerator(at: directory, includingPropertiesForKeys: nil)
        return (files?.allObjects as? [URL] ?? []).filter { $0.lastPathComponent == "user.bin" }
    }

    private func wait(_ name: String, until ready: () -> Bool) async {
        let deadline = Date().addingTimeInterval(18)
        while !ready() {
            precondition(Date() < deadline, "Timed out: \(name). Windows: \(host.windows.values.map(\.title)), scenes: \(host.sceneDismissals.keys), sizes: \(host.windows.values.map { ($0.title, $0.clientSize, $0.requestedSize) })")
            try! await Task.sleep(nanoseconds: 25_000_000)
        }
        print("PASS: \(name)")
        fflush(stdout)
    }

    @MainActor func run() async {
        precondition(ATMacAcceptanceCheckLateNativeReports())

        let identities = evidence.appendingPathComponent("rows.json")
        try! FileManager.default.createDirectory(at: evidence, withIntermediateDirectories: true)
        if cold {
            let expected = try! JSONDecoder().decode([UInt64].self, from: Data(contentsOf: identities))
            await wait("cold launch restores SwiftUI scenes, selected language, and lazy row identities") {
                self.mounted(self.main) && self.main?.title == "Каталог окон"
                    && self.mounted(self.child(2)) && self.child(1) == nil
                    && self.list?.rows.map(\.id) == expected
                    && self.host.menuGroups.first?.items.last?.isSelected == true
            }
            select(expected[0])
            await wait("SwiftUI list binding selects the closed row") { self.list?.selectedRow == 0 }
            button("Открыть").click()
            await wait("reopening the cold child mounts its SwiftUI scene") {
                self.mounted(self.child(1)) && self.list?.rows.first?.id == expected[0]
            }
            host.requestClose(host.primarySceneID)
        } else {
            await wait("fresh SwiftUI scene mounts the catalog and three controls") {
                self.mounted(self.main) && self.main?.title == "Window Catalog"
                    && self.main?.buttons.count == 3 && self.list?.rows.isEmpty == true
                    && self.host.menuGroups.first?.items.first?.isSelected == true
            }
            precondition(!button("Open").enabled && !button("Delete").enabled)
            button("Add").click()
            await wait("Add opens an independent SwiftUI child presentation") {
                self.mounted(self.child(1)) && self.list?.selectedRow == 0
            }
            let first = list!.rows[0].id
            let firstScene = child(1)!.sceneID
            ATMacAcceptanceRequestFrame(child(1)!.id, 160, 140, 400, 280)
            await wait("model frame request delivers its size to SwiftUI layout") {
                self.child(1)?.clientSize == CGSize(width: 400, height: 280)
            }
            close(child(1)!)
            await wait("closing the SwiftUI child leaves its lazy model row") {
                self.child(1) == nil && self.list?.rows.first?.text.contains("Closed") == true
            }
            precondition(savedFiles().isEmpty)
            precondition(list!.rows.first?.id == first)
            button("Open").click()
            await wait("Open remounts the same row in a new SwiftUI presentation") {
                self.mounted(self.child(1)) && self.child(1)?.sceneID != firstScene
                    && self.list?.rows.first?.id == first
                    && self.child(1)?.clientSize == CGSize(width: 400, height: 280)
            }
            button("Add").click()
            await wait("second Add mounts an independent scene") { self.mounted(self.child(2)) }
            select(first)
            await wait("SwiftUI selection uses row identity") { self.list?.selectedRow == 0 }
            list!.activate(first)
            host.executeMenu(group: 0, item: 1)
            await wait("SwiftUI Language command localizes all mounted windows and rows") {
                self.main?.title == "Каталог окон" && self.child(1)?.title == "Окно 1"
                    && self.child(2)?.title == "Окно 2"
                    && self.list?.rows.allSatisfy { $0.text.hasPrefix("Окно ") } == true
                    && self.host.menuGroups.first?.items.last?.isSelected == true
            }
            close(child(1)!)
            await wait("closing a localized child retains its identity") {
                self.child(1) == nil && self.list?.rows.first?.id == first
            }
            precondition(savedFiles().isEmpty)
            try! JSONEncoder().encode(list!.rows.map(\.id)).write(to: identities)
            host.requestStop()
        }
    }

    func verifyShutdown(_ status: Int32) {
        precondition(status == 0, "Model shutdown failed: \(status)")
        let files = savedFiles()
        precondition(files.count == 2 && files.contains { $0.deletingLastPathComponent().lastPathComponent == "local" })
        precondition(files.allSatisfy { !(try! Data(contentsOf: $0)).isEmpty })
        precondition(host.windows.isEmpty && host.buttons.isEmpty && host.lists.isEmpty)
        print("PASS: SwiftUI teardown saved separate USER and LOCAL")
        exit(status)
    }
}

@main
private struct MacWindowCatalogAcceptance: App {
    @StateObject private var host: MacApplicationHost

    init() {
        ATRegisterWindowCatalogApplication()
        func argument(_ name: String) -> URL {
            let arguments = CommandLine.arguments
            return URL(fileURLWithPath: arguments[arguments.firstIndex(of: name)! + 1])
        }
        let scenario = CatalogScenario(directory: argument("--state-dir"), evidence: argument("--artifacts"),
            cold: CommandLine.arguments.contains("--cold"))
        _host = StateObject(wrappedValue: scenario.host)
        scenario.host.onFinished = { scenario.verifyShutdown($0) }
        Task { await scenario.run() }
    }

    var body: some Scene { MacApplicationScenes(host: host) }
}
