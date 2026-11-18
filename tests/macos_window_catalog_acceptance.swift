import AppKit
import Foundation
import SwiftUI
@testable import AppTraverseMacUI

private struct SavedCatalog: Codable {
    let rows: [UInt64]
    let mainFrame: CGRect
}

private final class CatalogScenario {
    let host: MacApplicationHost
    let directory: URL
    let evidence: URL
    let cold: Bool

    init(directory: URL, evidence: URL, cold: Bool) {
        self.directory = directory
        self.evidence = evidence
        self.cold = cold
        host = MacApplicationHost {
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
        return window.clientSize != nil && host.nativeWindows[window.sceneID] != nil
    }

    private func button(_ title: String) -> MacButtonAdapter {
        main!.buttons.first { $0.title == title }!
    }

    private func close(_ window: MacWindowAdapter) {
        host.nativeWindows.performClose(window.sceneID)
    }

    private func select(_ id: UInt64) { list!.selection.wrappedValue = id }

    private func clickRow(_ row: Int, count: Int = 1) {
        func table(in view: NSView) -> NSTableView? {
            if let table = view as? NSTableView { return table }
            return view.subviews.lazy.compactMap(table(in:)).first
        }
        let window = host.nativeWindows[host.primarySceneID]!
        let listView = table(in: window.contentView!)!
        let rowFrame = listView.rect(ofRow: row)
        precondition(!rowFrame.isEmpty)
        let point = listView.convert(NSPoint(x: rowFrame.maxX - 12, y: rowFrame.midY), to: nil)
        let stamp = ProcessInfo.processInfo.systemUptime
        for click in 1...count {
            let timestamp = stamp + Double(click - 1) * 0.08
            NSApp.postEvent(NSEvent.mouseEvent(with: .leftMouseDown, location: point,
                modifierFlags: [], timestamp: timestamp, windowNumber: window.windowNumber,
                context: nil, eventNumber: click * 2, clickCount: click, pressure: 1)!, atStart: false)
            NSApp.postEvent(NSEvent.mouseEvent(with: .leftMouseUp, location: point,
                modifierFlags: [], timestamp: timestamp + 0.02, windowNumber: window.windowNumber,
                context: nil, eventNumber: click * 2 + 1, clickCount: click, pressure: 0)!, atStart: false)
        }
    }

    private func captureLayout(_ window: NSWindow, _ name: String) {
        let file = evidence.appendingPathComponent(name + ".png")
        if let image = CGWindowListCreateImage(.null, .optionIncludingWindow,
            CGWindowID(window.windowNumber), [.boundsIgnoreFraming, .bestResolution]) {
            try! NSBitmapImageRep(cgImage: image).representation(using: .png, properties: [:])!.write(to: file)
        }
    }

    private func savedFiles() -> [URL] {
        let files = FileManager.default.enumerator(at: directory, includingPropertiesForKeys: nil)
        return (files?.allObjects as? [URL] ?? []).filter { $0.lastPathComponent == "user.bin" }
    }

    @MainActor private func wait(_ name: String, until ready: () -> Bool) async {
        let deadline = Date().addingTimeInterval(18)
        while !ready() {
            if Date() >= deadline {
                print("Activation: active=\(NSApp.isActive), policy=\(NSApp.activationPolicy().rawValue), key=\(String(describing: NSApp.keyWindow?.title)), order=\(NSApp.orderedWindows.map(\.title))")
                fflush(stdout)
            }
            precondition(Date() < deadline, "Timed out: \(name). Windows: \(host.windows.values.map(\.title)), sizes: \(host.windows.values.map { ($0.title, $0.clientSize) })")
            try! await Task.sleep(nanoseconds: 25_000_000)
        }
        print("PASS: \(name)")
        fflush(stdout)
    }

    @MainActor func run() async {
        precondition(ATMacAcceptanceCheckLateNativeReports())

        let snapshot = evidence.appendingPathComponent("catalog.json")
        try! FileManager.default.createDirectory(at: evidence, withIntermediateDirectories: true)
        if cold {
            let saved = try! JSONDecoder().decode(SavedCatalog.self, from: Data(contentsOf: snapshot))
            let expected = saved.rows
            await wait("cold launch restores SwiftUI scenes, selected language, and lazy row identities") {
                self.mounted(self.main) && self.main?.title == "Каталог окон"
                    && self.mounted(self.child(2)) && self.child(1) == nil
                    && self.list?.rows.map(\.id) == expected
                    && self.host.menuGroups.last?.items.last?.isSelected == true
                    && self.main?.softenDark == true
            }
            let restoredWindow = host.nativeWindows[host.primarySceneID]!
            let workArea = restoredWindow.screen!.visibleFrame
            // The Windows presenter also fits saved placements to the work area.
            let width = min(saved.mainFrame.width, workArea.width)
            let height = min(saved.mainFrame.height, workArea.height)
            let expectedFrame = CGRect(
                x: min(max(saved.mainFrame.minX, workArea.minX), workArea.maxX - width),
                y: min(max(saved.mainFrame.maxY - height, workArea.minY), workArea.maxY - height),
                width: width, height: height)
            let restoredFrame = restoredWindow.frame
            precondition(abs(restoredFrame.minX - expectedFrame.minX) <= 1 &&
                         abs(restoredFrame.minY - expectedFrame.minY) <= 1 &&
                         abs(restoredFrame.width - expectedFrame.width) <= 1 &&
                         abs(restoredFrame.height - expectedFrame.height) <= 1,
                         "LOCAL restored \(restoredFrame), expected \(expectedFrame)")
            select(expected[0])
            await wait("SwiftUI list binding selects the closed row") { self.list?.selectedRow == 0 }
            button("Открыть").click()
            await wait("reopening the cold child mounts its SwiftUI scene") {
                self.mounted(self.child(1)) && self.list?.rows.first?.id == expected[0]
            }
            host.nativeWindows.performClose(host.primarySceneID)
        } else {
            await wait("fresh SwiftUI scene mounts the catalog and three controls") {
                self.mounted(self.main) && self.main?.title == "Window Catalog"
                    && self.main?.buttons.count == 3 && self.list?.rows.isEmpty == true
                    && self.host.menuGroups.last?.items.first?.isSelected == true
            }
            let window = host.nativeWindows[main!.sceneID]!
            let listID = NSNumber(value: list!.controlID)
            let buttonID = NSNumber(value: button("Add").controlID)
            let ids = (main!.buttons.map(\.controlID) + main!.lists.map(\.controlID)).map { NSNumber(value: $0) }
            func solved() -> [NSNumber: NSValue] {
                main!.layout.solveViewport(main!.clientSize!, controlIDs: ids) { _, _ in .zero }.frames
            }
            window.setContentSize(CGSize(width: 780, height: 460))
            await wait("wide native resize reflows controls beside the list") {
                guard self.main?.clientSize?.width == 780 && self.main?.clientSize == window.contentLayoutRect.size else { return false }
                let frames = solved()
                return frames[buttonID]!.rectValue.minX > frames[listID]!.rectValue.maxX
            }
            captureLayout(window, "wide")
            window.setContentSize(CGSize(width: 310, height: 570))
            await wait("tall native resize reflows controls below the list") {
                guard self.main?.clientSize?.width == 310 && self.main?.clientSize == window.contentLayoutRect.size else { return false }
                let frames = solved()
                return frames[buttonID]!.rectValue.minY > frames[listID]!.rectValue.maxY
            }
            captureLayout(window, "tall")
            // Fixture sizes setContentSize programmatically, then submits the
            // same completed-resize callback that AppKit sends after tracking.
            host.nativeWindowChanged(host.primarySceneID, window, commit: true)
            precondition(!button("Open").enabled && !button("Delete").enabled)
            button("Add").click()
            await wait("Add opens an independent SwiftUI child presentation") {
                self.mounted(self.child(1)) && self.list?.selectedRow == 0
            }
            let nativeMain = host.nativeWindows[main!.sceneID]!
            let nativeChild = host.nativeWindows[child(1)!.sceneID]!
            precondition(nativeMain !== nativeChild && nativeMain.tabGroup?.windows.count ?? 1 == 1)
            let first = list!.rows[0].id
            let firstScene = child(1)!.sceneID
            ATMacAcceptanceRequestFrame(child(1)!.id, 160, 140, 400, 280)
            await wait("model frame request delivers its size to SwiftUI layout") {
                self.child(1)?.clientSize == CGSize(width: 400, height: 280)
            }
            nativeChild.setContentSize(CGSize(width: 455, height: 320))
            await wait("native resize changes only the child viewport") {
                self.child(1)?.clientSize?.width == 455
            }
            ATMacAcceptanceRequestFrame(child(1)!.id, 160, 140, 400, 280)
            await wait("same semantic frame request still reapplies after native resize") {
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
            await wait("second Add mounts an independent scene") {
                self.mounted(self.child(2)) && self.list?.selectedRow == 1
            }
            nativeMain.makeKeyAndOrderFront(nil)
            clickRow(0)
            await wait("native list click selects the row by identity") { self.list?.selectedRow == 0 }
            button("Open").click()
            await wait("Open activates the existing independent window") {
                self.host.nativeWindows[self.child(1)!.sceneID]?.isKeyWindow == true
            }
            nativeMain.makeKeyAndOrderFront(nil)
            clickRow(1, count: 2)
            await wait("native list double-click activates its existing window") {
                self.list?.selectedRow == 1 && self.host.nativeWindows[self.child(2)!.sceneID]?.isKeyWindow == true
            }
            host.executeMenu(group: 1, item: 1)
            await wait("SwiftUI Language command localizes all mounted windows and rows") {
                self.main?.title == "Каталог окон" && self.child(1)?.title == "Окно 1"
                    && self.child(2)?.title == "Окно 2"
                    && self.list?.rows.allSatisfy { $0.text.hasPrefix("Окно ") } == true
                    && self.host.menuGroups.last?.items.last?.isSelected == true
            }
            close(child(1)!)
            await wait("closing a localized child retains its identity") {
                self.child(1) == nil && self.list?.rows.first?.id == first
            }
            button("Добавить").click()
            await wait("Add opens a third independent desktop window") {
                self.mounted(self.child(3)) && self.list?.selectedRow == 2
            }
            let deleted = host.nativeWindows[child(3)!.sceneID]!
            button("Удалить").click()
            await wait("Delete removes the selected open row and closes its actual desktop window") {
                self.child(3) == nil && self.list?.rows.count == 2 && !deleted.isVisible
            }
            host.executeMenu(group: 0, item: 3)
            await wait("shared Theme command applies Soft dark on both windows") {
                self.main?.softenDark == true && self.child(2)?.softenDark == true
                    && self.host.menuGroups.first?.items.last?.isSelected == true
            }
            precondition(savedFiles().isEmpty)
            let saved = SavedCatalog(rows: list!.rows.map(\.id), mainFrame: nativeMain.frame)
            try! JSONEncoder().encode(saved).write(to: snapshot)
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
