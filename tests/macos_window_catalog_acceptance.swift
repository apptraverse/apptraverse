import AppKit
import Foundation
import SwiftUI
@testable import AppTraverseMacUI

private struct SavedCatalog: Codable {
    let rows: [UInt64]
    let mainFrame: CGRect
    let childFrame: CGRect
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

    private func hasClientSize(_ window: MacWindowAdapter?, _ size: CGSize) -> Bool {
        guard let window, let native = host.nativeWindows[window.sceneID],
              let viewport = window.clientSize,
              let chrome = window.chromeMeasurement else { return false }
        return viewport == size && chrome.width == size.width && native.contentLayoutRect.width == size.width
            && native.contentLayoutRect.height == size.height + chrome.height
    }

    private func isFullscreen(_ adapter: MacWindowAdapter?) -> Bool {
        guard let adapter, let native = host.nativeWindows[adapter.sceneID],
              let screen = native.screen else { return false }
        return adapter.snapshot?.state.display?.isFullscreen == true
            && native.titleVisibility == .hidden && native.frame == screen.visibleFrame
    }

    private func frameMatches(_ actual: CGRect, _ expected: CGRect) -> Bool {
        abs(actual.minX - expected.minX) <= 1 && abs(actual.minY - expected.minY) <= 1
            && abs(actual.width - expected.width) <= 1 && abs(actual.height - expected.height) <= 1
    }

    private func pressFullscreenKey(_ characters: String, code: UInt16, in window: NSWindow) {
        NSApp.postEvent(NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [],
            timestamp: ProcessInfo.processInfo.systemUptime, windowNumber: window.windowNumber,
            context: nil, characters: characters, charactersIgnoringModifiers: characters,
            isARepeat: false, keyCode: code)!, atStart: false)
    }

    @MainActor private func focus(_ window: NSWindow) async {
        host.nativeWindows.raise(window, activate: true)
        await wait("native input target is the key window") { NSApp.isActive && window.isKeyWindow }
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
            let restoredChild = host.nativeWindows[child(2)!.sceneID]!
            await wait("cold launch restores both independently fullscreen windows") {
                self.isFullscreen(self.main) && self.isFullscreen(self.child(2))
            }
            host.setFullscreen(false, window: main!.id)
            host.setFullscreen(false, window: child(2)!.id)
            await wait("exiting fullscreen restores both normal native frames") {
                restoredWindow.titleVisibility == .visible && restoredChild.titleVisibility == .visible
                    && self.frameMatches(restoredChild.frame, saved.childFrame)
                    && self.hasClientSize(self.child(2), CGSize(width: 210, height: 260))
            }
            let workArea = restoredWindow.screen!.visibleFrame
            // The Windows presenter also fits saved placements to the work area.
            let width = min(saved.mainFrame.width, workArea.width)
            let height = min(saved.mainFrame.height, workArea.height)
            let expectedFrame = CGRect(
                x: min(max(saved.mainFrame.minX, workArea.minX), workArea.maxX - width),
                y: min(max(saved.mainFrame.maxY - height, workArea.minY), workArea.maxY - height),
                width: width, height: height)
            let restoredFrame = restoredWindow.frame
            precondition(frameMatches(restoredFrame, expectedFrame),
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
            await wait("initial catalog semantic size reaches the exact client viewport") {
                guard let state = self.main?.snapshot?.state else { return false }
                return self.hasClientSize(self.main, CGSize(width: state.width, height: state.height))
            }
            let listID = list!.controlID
            let buttonID = button("Add").controlID
            let ids = (main!.buttons.map(\.controlID) + main!.lists.map(\.controlID)).map { NSNumber(value: $0) }
            func solved() -> ATAppleLayoutResult {
                main!.layout.solveViewport(main!.clientSize!, controlIDs: ids) { _, _ in .zero }
            }
            window.setContentSize(CGSize(width: 780, height: 460))
            await wait("wide native resize reflows controls beside the list") {
                guard self.main?.clientSize?.width == 780,
                      self.main!.clientSize!.height < window.contentLayoutRect.height else { return false }
                let frames = solved()
                return frames.frame(forControl: buttonID).minX > frames.frame(forControl: listID).maxX
            }
            captureLayout(window, "wide")
            await wait("main window receives the Windows layout commands") {
                self.host.contextMenuItems.map(\.title) == [
                    "Layout: Automatic", "Layout: Horizontal", "Layout: Vertical",
                    "Reset layout to application default"
                ] && self.host.contextMenuItems.last?.startsSection == true
            }
            host.executeContextMenu(item: 2)
            await wait("Vertical layout stacks controls even in a wide window") {
                let frames = solved()
                return self.host.contextMenuItems[2].isSelected
                    && frames.frame(forControl: buttonID).minY > frames.frame(forControl: listID).maxY
            }
            host.executeContextMenu(item: 1)
            await wait("Horizontal layout puts controls beside the list") {
                let frames = solved()
                return self.host.contextMenuItems[1].isSelected
                    && frames.frame(forControl: buttonID).minX > frames.frame(forControl: listID).maxX
            }
            host.executeContextMenu(item: 3)
            await wait("Reset layout restores the adaptive application default") {
                self.host.contextMenuItems[0].isSelected
            }
            window.setContentSize(CGSize(width: 310, height: 570))
            await wait("tall native resize reflows controls below the list") {
                guard self.main?.clientSize?.width == 310,
                      self.main!.clientSize!.height < window.contentLayoutRect.height else { return false }
                let frames = solved()
                return frames.frame(forControl: buttonID).minY > frames.frame(forControl: listID).maxY
            }
            captureLayout(window, "tall")
            // Fixture sizes setContentSize programmatically, then submits the
            // same completed-resize callback that AppKit sends after tracking.
            host.nativeWindowChanged(host.primarySceneID, window, commit: true)
            precondition(!button("Open").enabled && !button("Delete").enabled)
            button("Add").click()
            await wait("Add opens an independent SwiftUI child presentation") {
                self.mounted(self.child(1)) && self.list?.selectedRow == 0
                    && self.hasClientSize(self.child(1), CGSize(width: 480, height: 320))
            }
            let nativeMain = host.nativeWindows[main!.sceneID]!
            let nativeChild = host.nativeWindows[child(1)!.sceneID]!
            precondition(nativeMain !== nativeChild && nativeMain.tabGroup?.windows.count ?? 1 == 1)
            let first = list!.rows[0].id
            let firstScene = child(1)!.sceneID
            ATMacAcceptanceRequestFrame(child(1)!.id, 160, 140, 400, 280)
            await wait("model frame request delivers its size to SwiftUI layout") {
                self.hasClientSize(self.child(1), CGSize(width: 400, height: 280))
            }
            nativeChild.setContentSize(CGSize(width: 455, height: 320))
            await wait("native resize changes only the child viewport") {
                self.child(1)?.clientSize?.width == 455
            }
            ATMacAcceptanceRequestFrame(child(1)!.id, 160, 140, 400, 280)
            await wait("same semantic frame request still reapplies after native resize") {
                self.hasClientSize(self.child(1), CGSize(width: 400, height: 280))
            }
            host.setFullscreen(true, window: child(1)!.id)
            await wait("child enters fullscreen before a deferred semantic request") {
                self.isFullscreen(self.child(1))
            }
            ATMacAcceptanceRequestFrame(child(1)!.id, 170, 150, 410, 290)
            await wait("fullscreen defers the requested normal client size") {
                self.child(1)?.snapshot?.state.width == 410
                    && self.child(1)?.snapshot?.state.height == 290 && self.isFullscreen(self.child(1))
            }
            host.setFullscreen(false, window: child(1)!.id)
            await wait("fullscreen exit applies the deferred exact client size") {
                self.child(1)?.snapshot?.state.display?.isFullscreen == false
                    && self.hasClientSize(self.child(1), CGSize(width: 410, height: 290))
            }
            ATMacAcceptanceRequestFrame(child(1)!.id, 160, 140, 400, 280)
            await wait("normal semantic request restores the exact size before remount") {
                self.hasClientSize(self.child(1), CGSize(width: 400, height: 280))
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
                    && self.hasClientSize(self.child(1), CGSize(width: 400, height: 280))
            }
            button("Add").click()
            await wait("second Add mounts an independent scene") {
                self.mounted(self.child(2)) && self.list?.selectedRow == 1
            }
            await focus(nativeMain)
            clickRow(0)
            await wait("native list click selects the row by identity") { self.list?.selectedRow == 0 }
            button("Open").click()
            await wait("Open activates the existing independent window") {
                self.host.nativeWindows[self.child(1)!.sceneID]?.isKeyWindow == true
            }
            await focus(nativeMain)
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
                    && self.host.contextMenuItems.first?.title == "Раскладка: автоматически"
            }
            ATMacAcceptanceRequestFrame(child(2)!.id, 180, 160, 210, 260)
            await wait("localized measured toolbar preserves the exact narrow client size") {
                self.hasClientSize(self.child(2), CGSize(width: 210, height: 260))
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
            let nativeSecond = host.nativeWindows[child(2)!.sceneID]!
            let normalChild = nativeSecond.frame
            let saved = SavedCatalog(rows: list!.rows.map(\.id), mainFrame: nativeMain.frame,
                                     childFrame: normalChild)
            await focus(nativeSecond)
            pressFullscreenKey("\u{F70E}", code: 103, in: nativeSecond)
            await wait("fullscreen is localized and applies only to the requested child") {
                self.isFullscreen(self.child(2)) && nativeSecond.isKeyWindow
                    && !self.isFullscreen(self.main)
                    && self.child(2)?.snapshot?.state.display?.actionTitle == "Выйти из полноэкранного режима"
            }
            pressFullscreenKey("\u{1B}", code: 53, in: nativeSecond)
            await wait("child exits fullscreen to the same normal frame") {
                nativeSecond.titleVisibility == .visible && self.frameMatches(nativeSecond.frame, normalChild)
            }
            host.setFullscreen(true, window: main!.id)
            host.setFullscreen(true, window: child(2)!.id)
            await wait("main and child can enter fullscreen independently") {
                self.isFullscreen(self.main) && self.isFullscreen(self.child(2))
            }
            precondition(savedFiles().isEmpty)
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
