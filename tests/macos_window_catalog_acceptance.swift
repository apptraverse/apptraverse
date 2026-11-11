import AppKit
import Foundation
@testable import AppTraverseMacUI

private func require(_ value: @autoclosure () -> Bool, _ message: String) {
    if !value() { fatalError("macOS WindowCatalog acceptance: " + message) }
}

private struct SavedExpectations: Codable {
    let rowIDs: [UInt64]
    let rowTexts: [String]
    let mainWidth: Double
    let mainHeight: Double
    let childWidth: Double
    let childHeight: Double
    let desktopOrder: [String]
}

// This harness runs the production host, bridge, presenters, model thread and
// application composition. It sends native adapter intents and operates actual
// NSWindows; no prepared mirror snapshot substitutes for a model publication.
private final class Acceptance {
    struct Step {
        let name: String
        let action: () -> Void
        let ready: () -> Bool
        let check: () -> Void
    }

    let directory: URL
    let artifacts: URL
    let cold: Bool
    let host: MacApplicationHost
    var steps: [Step] = []
    var index = 0
    var deadline = Date.distantPast
    var finished = false
    var firstRowID: UInt64 = 0
    var firstChildSize = NSSize.zero
    var expectations: SavedExpectations?

    init(directory: URL, artifacts: URL, cold: Bool) {
        self.directory = directory
        self.artifacts = artifacts
        self.cold = cold
        host = MacApplicationHost { delegate in
            ATCreateWindowCatalogApplicationBridge(directory.path, delegate)
        }
        try! FileManager.default.createDirectory(
            at: artifacts, withIntermediateDirectories: true)
    }

    var main: MacWindowAdapter? {
        host.windows.values.first {
            $0.title == "Window Catalog" || $0.title == "Каталог окон"
        }
    }
    var list: MacListAdapter? { host.lists.values.first }
    func button(_ title: String) -> MacButtonAdapter? {
        host.buttons.values.first { $0.title == title }
    }
    func child(_ number: Int) -> MacWindowAdapter? {
        host.windows.values.first {
            $0.title == "Window \(number)" || $0.title == "Окно \(number)"
        }
    }
    func click(_ title: String) {
        let control = button(title)!
        require(control.enabled, "attempt to click disabled \(title)")
        control.click()
    }
    func desktopOrder() -> [String] {
        let native = Set(host.windows.values.map { ObjectIdentifier($0.window) })
        return NSApp.orderedWindows.filter { native.contains(ObjectIdentifier($0)) }.map(\.title)
    }
    func nativeListTable() -> NSTableView? {
        func find(_ view: NSView) -> NSTableView? {
            if let table = view as? NSTableView { return table }
            for child in view.subviews {
                if let table = find(child) { return table }
            }
            return nil
        }
        guard let content = main?.window.contentView else { return nil }
        return find(content)
    }
    func nativeClickRow(_ row: Int, double: Bool = false,
                        atTrailingEdge: Bool = false) {
        let table = nativeListTable()!
        let window = table.window!
        let rowFrame = table.rect(ofRow: row)
        let point = table.convert(NSPoint(
            x: atTrailingEdge ? table.bounds.maxX - 12 : rowFrame.minX + 20,
            y: rowFrame.midY), to: nil)
        let stamp = ProcessInfo.processInfo.systemUptime
        for count in 1...(double ? 2 : 1) {
            let down = NSEvent.mouseEvent(with: .leftMouseDown, location: point,
                modifierFlags: [], timestamp: stamp + Double(count - 1) * 0.08,
                windowNumber: window.windowNumber, context: nil,
                eventNumber: count * 2, clickCount: count, pressure: 1)!
            let up = NSEvent.mouseEvent(with: .leftMouseUp, location: point,
                modifierFlags: [], timestamp: stamp + Double(count - 1) * 0.08 + 0.02,
                windowNumber: window.windowNumber, context: nil,
                eventNumber: count * 2 + 1, clickCount: count, pressure: 0)!
            NSApp.postEvent(down, atStart: false)
            NSApp.postEvent(up, atStart: false)
        }
    }
    func users() -> [URL] {
        let enumerator = FileManager.default.enumerator(
            at: directory, includingPropertiesForKeys: nil)
        return (enumerator?.allObjects as? [URL] ?? []).filter {
            $0.lastPathComponent == "user.bin"
        }
    }
    func record(_ name: String) {
        print("PASS: " + name)
        fflush(stdout)
    }
    func snapshot(_ adapter: MacWindowAdapter, name: String) {
        let view = adapter.window.contentView!
        view.layoutSubtreeIfNeeded()
        view.displayIfNeeded()
        if let list = adapter.lists.first, !list.rows.isEmpty {
            let table = nativeListTable()!
            let cell = table.view(atColumn: 0, row: 0,
                                  makeIfNecessary: true) as! NSTableCellView
            cell.layoutSubtreeIfNeeded()
            require(cell.textField!.stringValue == list.rows[0].text,
                    "native row text differs from the publication")
            require(cell.textField!.bounds.width > 100 &&
                    cell.textField!.bounds.height >= 12,
                    "native row text has no visible layout")
        }
        let bitmap = view.bitmapImageRepForCachingDisplay(in: view.bounds)!
        view.cacheDisplay(in: view.bounds, to: bitmap)
        let png = bitmap.representation(using: .png, properties: [:])!
        try! png.write(to: artifacts.appendingPathComponent(name + ".png"))
    }
    func addStep(_ name: String, action: @escaping () -> Void = {},
                 ready: @escaping () -> Bool,
                 check: @escaping () -> Void = {}) {
        steps.append(Step(name: name, action: action, ready: ready, check: check))
    }

    func prepare() {
        ATRegisterWindowCatalogApplication()
        require(ATMacAcceptanceCheckLateNativeReports(),
                "queued native report mishandled an unloaded/live model target")
        record("late native frame/screen reports respect model lifecycle")
        if cold {
            expectations = try! JSONDecoder().decode(
                SavedExpectations.self,
                from: Data(contentsOf: artifacts.appendingPathComponent("expected.json")))
            addColdSteps()
        } else {
            require(users().isEmpty, "seed profile must have no prior USER")
            addSeedSteps()
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) { self.beginStep() }
    }

    func beginStep() {
        if index == steps.count {
            finished = true
            if cold { host.requestStop() }
            else { main!.window.performClose(nil) }
            return
        }
        deadline = Date().addingTimeInterval(20)
        steps[index].action()
        poll()
    }
    func poll() {
        let step = steps[index]
        if step.ready() {
            step.check()
            record(step.name)
            index += 1
            // Allow SwiftUI/AppKit layout to observe each settled publication.
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) { self.beginStep() }
            return
        }
        if Date() > deadline {
            if let main { snapshot(main, name: "failure-main") }
            print("NATIVE_TABLE selected=\(nativeListTable()?.selectedRow.description ?? "missing")")
            print("TIMEOUT activity=\(NSApp.isActive) key=\(NSApp.keyWindow?.title ?? "nil") main=\(NSApp.mainWindow?.title ?? "nil") order=\(desktopOrder())")
            for window in host.windows.values {
                print("WINDOW \(window.title) key=\(window.window.isKeyWindow) eligible=\(window.window.canBecomeKey) visible=\(window.window.isVisible)")
            }
            fflush(stdout)
            fatalError("Timed out waiting for " + step.name)
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.02) { self.poll() }
    }

    func addSeedSteps() {
        addStep("initial native host and required controls", ready: {
            self.main != nil && self.list != nil && self.host.buttons.count == 4
        }, check: {
            require(self.host.windows.count == 1, "unexpected initial child window")
            require(self.list!.rows.isEmpty, "fresh catalog is not empty")
            require(self.button("Open")?.enabled == false, "Open initially enabled")
            require(self.button("Delete")?.enabled == false, "Delete initially enabled")
            require(self.main!.window.contentView != nil, "missing SwiftUI content")
            self.snapshot(self.main!, name: "initial-main")
        })
        addStep("SwiftUI list mounts a real native table", ready: {
            self.nativeListTable() != nil
        }, check: {
            require(self.nativeListTable()!.acceptsFirstMouse(for: nil),
                    "native list must accept its first activation click")
        })
        addStep("Add reaches model and publishes native child", action: {
            self.click("Add")
        }, ready: {
            self.list!.rows.count == 1 && self.child(1) != nil
        }, check: {
            self.firstRowID = self.list!.rows[0].id
            require(self.list!.selectedRow == 0, "new child not selected")
            require(self.button("Delete")!.enabled, "Delete did not enable")
            self.snapshot(self.child(1)!, name: "initial-child")
        })
        addStep("model selection is rendered by native table", ready: {
            self.list!.selectedRow == 0 && self.nativeListTable()!.selectedRow == 0
        })
        addStep("native child resize reaches mirror", action: {
            let child = self.child(1)!
            child.window.setContentSize(NSSize(width: 420, height: 260))
            child.window.setFrameOrigin(NSPoint(x: 140, y: 180))
            self.firstChildSize = child.window.contentLayoutRect.size
        }, ready: {
            let state = self.host.bridge.windowState(self.child(1)!.id)
            return abs(state.width - self.firstChildSize.width) < 2 &&
                abs(state.height - self.firstChildSize.height) < 2
        })
        addStep("native Close retains lazy catalog row", action: {
            self.child(1)!.window.performClose(nil)
        }, ready: {
            self.child(1) == nil && self.list!.rows[0].text.contains("Closed")
        }, check: {
            require(self.list!.rows[0].id == self.firstRowID, "Close replaced row identity")
            require(self.users().isEmpty, "child Close wrote USER to disk")
        })
        addStep("Open restores child geometry and identity", action: {
            self.click("Open")
        }, ready: { self.child(1) != nil && !self.list!.rows[0].text.contains("Closed") }, check: {
            require(self.list!.rows[0].id == self.firstRowID, "Open replaced row identity")
            let size = self.child(1)!.window.contentLayoutRect.size
            require(abs(size.width - self.firstChildSize.width) < 2 &&
                    abs(size.height - self.firstChildSize.height) < 2,
                    "Open lost saved native size")
        })
        addStep("coalesced native Close/Open preserves live activation", action: {
            self.child(1)!.window.performClose(nil)
            self.list!.activate(0)
            // A later locale command is the publication barrier: observing its
            // result proves the preceding Close/Open intents were processed.
            self.click("RU")
        }, ready: {
            self.main?.title == "Каталог окон" && self.child(1)?.title == "Окно 1" &&
                !self.list!.rows[0].text.contains("закрыто")
        })
        addStep("native Close still works after coalesced Open", action: {
            self.child(1)!.window.performClose(nil)
        }, ready: {
            self.child(1) == nil && self.list!.rows[0].text.contains("закрыто")
        })
        addStep("coalesced-close child reopens without identity repair", action: {
            self.click("Открыть")
        }, ready: { self.child(1) != nil }, check: {
            require(self.list!.rows[0].id == self.firstRowID, "coalesced Open replaced row")
        })
        addStep("locale restores through normal button intent", action: {
            self.click("EN")
        }, ready: {
            self.main?.title == "Window Catalog" && self.child(1)?.title == "Window 1"
        })
        addStep("second independent clone", action: { self.click("Add") }, ready: {
            self.list!.rows.count == 2 && self.child(2) != nil &&
                self.list!.selectedRow == 1 && self.nativeListTable()!.selectedRow == 1
        }, check: {
            require(self.list!.rows[0].id != self.list!.rows[1].id, "clones share identity")
        })
        addStep("native single click selects through model", action: {
            self.nativeClickRow(0, atTrailingEdge: true)
        }, ready: {
            self.list!.selectedRow == 0 && self.nativeListTable()!.selectedRow == 0
        })
        addStep("native double click focuses existing child", action: {
            self.nativeClickRow(0, double: true, atTrailingEdge: true)
        }, ready: { self.child(1)!.window.isKeyWindow })
        addStep("locale publication updates all native windows", action: {
            self.click("RU")
        }, ready: {
            self.main?.title == "Каталог окон" && self.button("Добавить") != nil &&
                self.child(1)?.title == "Окно 1" && self.child(2)?.title == "Окно 2"
        }, check: {
            require(self.list!.rows.allSatisfy { $0.text.hasPrefix("Окно ") },
                    "list labels did not localize")
        })
        addStep("native main resize updates model-owned chrome layout", action: {
            self.main!.window.setContentSize(NSSize(width: 900, height: 360))
        }, ready: {
            let size = self.main!.window.contentLayoutRect.size
            return abs(size.width - 900) < 2 && abs(size.height - 360) < 2 &&
                self.list!.frame.height > 300 && self.list!.frame.width > 400
        }, check: {
            let frame = self.list!.frame
            require(frame.x >= 0 && frame.y >= 0 &&
                    frame.x + frame.width <= 900 && frame.y + frame.height <= 360,
                    "list escaped resized client area")
            for button in self.host.buttons.values {
                require(button.frame.x + button.frame.width <= 901 &&
                        button.frame.y + button.frame.height <= 361,
                        "button escaped resized client area")
            }
        })
        addStep("explicit model frame request reaches native child", action: {
            ATMacAcceptanceRequestFrame(self.child(2)!.id, 160, 140, 400, 280)
        }, ready: {
            let size = self.child(2)!.window.contentLayoutRect.size
            return abs(size.width - 400) < 2 && abs(size.height - 280) < 2
        }, check: {
            let state = self.host.bridge.windowState(self.child(2)!.id)
            require(abs(state.x - 160) < 2 && abs(state.y - 140) < 2,
                    "model request coordinates did not survive publication")
            let size = self.main!.window.contentLayoutRect.size
            require(abs(size.width - 900) < 2 && abs(size.height - 360) < 2,
                    "later publication snapped native main size back")
        })
        addStep("Delete removes selected child and row", action: {
            self.click("Удалить")
        }, ready: {
            self.list!.rows.count == 1 && self.child(1) == nil &&
                self.nativeListTable()!.selectedRow == (self.list!.selectedRow ?? -1)
        }, check: {
            require(self.list!.rows[0].text == "Окно 2", "Delete removed wrong child")
        })
        addStep("next clone remains independent after Delete", action: {
            self.click("Добавить")
        }, ready: { self.list!.rows.count == 2 && self.child(3) != nil })
        addStep("final closed child survives to shutdown", action: {
            self.child(3)!.window.performClose(nil)
        }, ready: {
            self.child(3) == nil && self.list!.rows[1].text.contains("закрыто")
        }, check: {
            require(self.users().isEmpty, "non-shutdown operation wrote USER")
        })
        addStep("final native selection and active visual evidence", action: {
            self.nativeListTable()!.selectRowIndexes(
                IndexSet(integer: 0), byExtendingSelection: false)
            NSApp.activate(ignoringOtherApps: true)
            self.main!.window.makeKeyAndOrderFront(nil)
            require(self.main!.window.makeFirstResponder(self.nativeListTable()!),
                    "native list refused keyboard focus")
        }, ready: {
            self.list!.selectedRow == 0 && self.nativeListTable()!.selectedRow == 0 &&
                self.main!.window.isKeyWindow
        }, check: {
            let mainSize = self.main!.window.contentLayoutRect.size
            let childSize = self.child(2)!.window.contentLayoutRect.size
            let saved = SavedExpectations(
                rowIDs: self.list!.rows.map(\.id),
                rowTexts: self.list!.rows.map(\.text),
                mainWidth: mainSize.width, mainHeight: mainSize.height,
                childWidth: childSize.width, childHeight: childSize.height,
                desktopOrder: self.desktopOrder())
            try! JSONEncoder().encode(saved).write(
                to: self.artifacts.appendingPathComponent("expected.json"))
            self.snapshot(self.main!, name: "final-main")
            self.snapshot(self.child(2)!, name: "final-child")
        })
    }

    func addColdSteps() {
        addStep("cold USER restores native graph and lazy child", ready: {
            self.main?.title == "Каталог окон" && self.list?.rows.count == 2 &&
                self.child(2) != nil && self.host.buttons.count == 4 &&
                self.nativeListTable()?.selectedRow == 0
        }, check: {
            let expected = self.expectations!
            require(self.list!.rows.map(\.id) == expected.rowIDs, "cold row identities changed")
            require(self.list!.rows.map(\.text) == expected.rowTexts, "cold row texts changed")
            require(self.list!.selectedRow == 0, "cold selection lost")
            require(self.child(3) == nil, "cold load eagerly opened closed child")
            let size = self.main!.window.contentLayoutRect.size
            require(abs(size.width - expected.mainWidth) < 2 &&
                    abs(size.height - expected.mainHeight) < 2, "cold main geometry lost")
            let childSize = self.child(2)!.window.contentLayoutRect.size
            require(abs(childSize.width - expected.childWidth) < 2 &&
                    abs(childSize.height - expected.childHeight) < 2, "cold child geometry lost")
            require(self.desktopOrder() == expected.desktopOrder, "cold desktop stacking order lost")
            self.snapshot(self.main!, name: "cold-main")
        })
        addStep("cold closed child selects through native table binding", action: {
            self.nativeClickRow(1, atTrailingEdge: true)
        }, ready: {
            self.list!.selectedRow == 1 && self.nativeListTable()!.selectedRow == 1
        })
        addStep("cold lazy child reopens with original identity", action: {
            self.click("Открыть")
        }, ready: { self.child(3) != nil && !self.list!.rows[1].text.contains("закрыто") }, check: {
            require(self.list!.rows[1].id == self.expectations!.rowIDs[1],
                    "cold Open replaced lazy holder")
            self.snapshot(self.child(3)!, name: "cold-reopened-child")
        })
    }
}

@main
private enum MacWindowCatalogAcceptance {
    static func argument(_ name: String) -> String? {
        guard let index = CommandLine.arguments.firstIndex(of: name),
              CommandLine.arguments.indices.contains(index + 1) else { return nil }
        return CommandLine.arguments[index + 1]
    }
    static func main() {
        let directory = URL(fileURLWithPath: argument("--state-dir")!)
        let artifacts = URL(fileURLWithPath: argument("--artifacts")!)
        let cold = CommandLine.arguments.contains("--cold")
        let scenario = Acceptance(directory: directory, artifacts: artifacts, cold: cold)
        scenario.prepare()
        let exitStatus = scenario.host.run()
        require(exitStatus == 0, "model shutdown failed: \(exitStatus)")
        require(scenario.finished, "host stopped before acceptance finished")
        require(scenario.users().count == 1, "shutdown must produce exactly one user.bin")
        require(scenario.host.windows.isEmpty && scenario.host.buttons.isEmpty &&
                scenario.host.lists.isEmpty, "native adapters remained after teardown")
        print("PASS: native teardown and single-file shutdown USER")
    }
}
