import Foundation
import SwiftUI
import UIKit
@testable import AppTraverseIosUI

private func require(_ value: @autoclosure () -> Bool, _ message: String) {
    if !value() { fatalError("iOS acceptance: " + message) }
}

private final class NativeListSink: IosUiCommandSink {
    var selected: [Int] = []
    var activated: [Int] = []
    func clickButton(_ token: UInt64) {}
    func selectListRow(_ row: Int, token: UInt64) { selected.append(row) }
    func activateListRow(_ row: Int, token: UInt64) { activated.append(row) }
}

private func testDisplayedRowIdentity() {
    let sink = NativeListSink()
    let adapter = IosListAdapter(token: 123, commandSink: sink)
    let a = IosListRowSnapshot(id: 1001, text: "A")
    let b = IosListRowSnapshot(id: 1002, text: "B")
    let coordinator = IosNativeList.Coordinator()
    coordinator.adapter = adapter
    coordinator.rows = [a, b]
    adapter.rows = [b]
    let table = UITableView()
    coordinator.tableView(table, didSelectRowAt: IndexPath(row: 0, section: 0))
    coordinator.activateDisplayedRow(0)
    require(sink.selected.isEmpty && sink.activated.isEmpty, "removed displayed row sent intent to replacement")
    coordinator.tableView(table, didSelectRowAt: IndexPath(row: 1, section: 0))
    coordinator.activateDisplayedRow(1)
    require(sink.selected == [0] && sink.activated == [0], "displayed identity did not resolve current mirror index")
    adapter.disconnect()
    coordinator.tableView(table, didSelectRowAt: IndexPath(row: 1, section: 0))
    coordinator.activateDisplayedRow(1)
    require(sink.selected == [0] && sink.activated == [0], "unloaded native callback reached command sink")
    print("PASS: native list identity and detached callback boundary"); fflush(stdout)
}

private struct Expected: Codable {
    var rows: [UInt64]
    var labels: [String]
    var active: UInt64
    var closed: UInt64
}

private final class Acceptance {
    struct Step {
        let name: String
        let action: () -> Void
        let ready: () -> Bool
        let check: () -> Void
    }
    let root: URL
    let cold: Bool
    let osBackground: Bool
    let host: IosApplicationHost
    var steps: [Step] = []
    var index = 0
    var deadline = Date.distantPast
    var started = false
    var firstIdentity: UInt64 = 0
    var thirdIdentity: UInt64 = 0
    var expected: Expected?
    init(root: URL, cold: Bool, osBackground: Bool, host: IosApplicationHost) {
        self.root = root; self.cold = cold; self.osBackground = osBackground; self.host = host
    }
    var list: IosListAdapter? { host.lists.values.first }
    var main: IosWindowSceneModel? { host.mainScene }
    func child(_ n: Int) -> IosWindowSceneModel? {
        host.scenes.values.first { $0.title == "Window \(n)" || $0.title == "Окно \(n)" }
    }
    func button(_ title: String) -> IosButtonAdapter? {
        host.activeScene?.buttons.first { $0.title == title && $0.enabled }
    }
    func click(_ title: String) { button(title)!.click() }
    func goMain() {
        let pager = pageControl(window!)!
        pager.currentPage = 0
        pager.sendActions(for: .valueChanged)
    }
    func pageControl(_ view: UIView) -> UIPageControl? {
        if let control = view as? UIPageControl { return control }
        return view.subviews.compactMap { pageControl($0) }.first
    }
    var activeControlsFit: Bool {
        guard let scene = host.activeScene, let window else { return false }
        let state = host.bridge.windowState(scene.id)
        let maxWidth = min(Double(window.bounds.width), state.width)
        let maxHeight = min(Double(window.bounds.height), state.height)
        return scene.buttons.allSatisfy {
            $0.frame.width > 0 && $0.frame.height > 0 && $0.frame.x >= 0 &&
            $0.frame.y >= 0 && $0.frame.x + $0.frame.width <= maxWidth + 1 &&
            $0.frame.y + $0.frame.height <= maxHeight + 1
        }
    }
    var window: UIWindow? {
        UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
            .flatMap(\.windows).first { $0.isKeyWindow }
    }
    func findTable(_ view: UIView) -> UITableView? {
        if let table = view as? UITableView, table.window != nil { return table }
        return view.subviews.compactMap { findTable($0) }.first
    }
    var table: UITableView? { window.flatMap { findTable($0) } }
    func rotate(_ orientations: UIInterfaceOrientationMask) {
        let scene = window!.windowScene!
        window!.rootViewController!.setNeedsUpdateOfSupportedInterfaceOrientations()
        scene.requestGeometryUpdate(.iOS(interfaceOrientations: orientations)) { error in
            print("GEOMETRY_REQUEST_ERROR: \(error)"); fflush(stdout)
        }
    }
    func nativeSelect(_ row: Int) {
        let table = self.table!
        let path = IndexPath(row: row, section: 0)
        table.selectRow(at: path, animated: false, scrollPosition: .none)
        table.delegate!.tableView?(table, didSelectRowAt: path)
    }
    func userFiles() -> [URL] {
        (FileManager.default.enumerator(at: root.appendingPathComponent("profile"),
            includingPropertiesForKeys: nil)?.allObjects as? [URL] ?? [])
            .filter { $0.lastPathComponent == "user.bin" }
    }
    func snapshot(_ name: String) {
        guard let window else { fatalError("No UIWindow for capture") }
        let renderer = UIGraphicsImageRenderer(bounds: window.bounds)
        let data = renderer.pngData { _ in
            window.drawHierarchy(in: window.bounds, afterScreenUpdates: true)
        }
        try! data.write(to: root.appendingPathComponent(name + ".png"))
    }
    func step(_ name: String, action: @escaping () -> Void = {},
              ready: @escaping () -> Bool, check: @escaping () -> Void = {}) {
        steps.append(Step(name: name, action: action, ready: ready, check: check))
    }
    func start() {
        guard !started else { return }; started = true
        testDisplayedRowIdentity()
        try! FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        if cold {
            expected = try! JSONDecoder().decode(Expected.self,
                from: Data(contentsOf: root.appendingPathComponent("expected.json")))
            step("cold mobile graph, locale and identity", ready: {
                self.main?.title == "Каталог окон" && self.list?.rows.count == 2 && self.host.activeScene != nil
            }, check: {
                require(self.list!.rows.map(\.id) == self.expected!.rows, "cold identity changed")
                require(self.list!.rows.map(\.text) == self.expected!.labels, "cold labels changed")
                require(self.host.activeScene!.identity == self.expected!.active, "cold active page lost")
                require(self.child(3) == nil, "cold closed child eagerly activated")
                self.snapshot("cold-restored")
            })
            step("cold main page", action: { self.goMain() }, ready: {
                self.host.activeToken == self.main?.id && self.table != nil
            })
            step("cold native list selection", action: { self.nativeSelect(1) }, ready: {
                self.list?.selectedRow == 1
            })
            step("cold closed page reopens", action: { self.click("Открыть") }, ready: {
                self.child(3) != nil && self.host.activeToken == self.child(3)?.id
            }, check: { require(self.child(3)!.identity == self.expected!.closed, "cold reopened window identity changed") })
        } else { seed() }
        if !osBackground || cold {
            step("orderly shutdown", action: { self.host.requestStop() }, ready: {
                self.host.lastModelExitCode() == 0 && self.host.scenes.isEmpty
            }, check: {
                require(self.userFiles().count == 1, "shutdown must write exactly one user.bin")
            })
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) { self.begin() }
    }
    func seed() {
        step("initial model-driven catalog", ready: {
            self.main != nil && self.host.activeToken == self.main!.id && self.table != nil && self.host.buttons.count == 4
        }, check: {
            require(self.list!.rows.isEmpty, "fresh catalog must be empty")
            require(self.userFiles().isEmpty, "fresh profile unexpectedly loaded")
            self.snapshot("initial-main")
        })
        step("Add and mobile active page", action: { self.click("Add") }, ready: {
            self.list?.rows.count == 1 && self.child(1) != nil && self.host.activeToken == self.child(1)?.id && self.activeControlsFit
        }, check: {
            self.firstIdentity = self.list!.rows[0].id
            require(self.host.pageTokens.count == 2, "mobile ring missing child")
            self.snapshot("child")
        })
        step("landscape viewport and model Close layout", action: { self.rotate(.landscapeLeft) }, ready: {
            self.window!.bounds.width > self.window!.bounds.height && self.activeControlsFit
        }, check: { self.snapshot("landscape-child") })
        step("portrait viewport and model Close layout", action: { self.rotate(.portrait) }, ready: {
            self.window!.bounds.height > self.window!.bounds.width && self.activeControlsFit
        })
        step("child Close through model button", action: { let button = self.button("Close")!; button.click(); button.click() }, ready: {
            self.child(1) == nil && self.list!.rows[0].text.contains("Closed") && self.host.activeToken == self.main?.id
        }, check: {
            require(self.userFiles().isEmpty, "child close wrote disk USER")
        })
        step("native list mounts after close", ready: { self.table != nil })
        step("Open restores same identity", action: { self.click("Open") }, ready: {
            self.child(1) != nil && self.host.activeToken == self.child(1)?.id
        }, check: { require(self.list!.rows[0].id == self.firstIdentity, "Open replaced holder") })
        step("page navigation is model driven", action: { self.goMain() }, ready: {
            self.host.activeToken == self.main?.id && self.table != nil
        })
        step("second independent clone", action: { self.click("Add") }, ready: {
            self.child(2) != nil && self.list!.rows.count == 2
        }, check: { require(self.list!.rows[0].id != self.list!.rows[1].id, "clone aliases holder") })
        step("return to main", action: { self.goMain() }, ready: {
            self.host.activeToken == self.main?.id && self.table != nil
        })
        step("UIKit row selection reaches model", action: { self.nativeSelect(0) }, ready: {
            self.list?.selectedRow == 0 && self.table?.indexPathForSelectedRow?.row == 0
        })
        step("localization across pages", action: { self.click("RU") }, ready: {
            self.main?.title == "Каталог окон" && self.child(1)?.title == "Окно 1" && self.child(2)?.title == "Окно 2"
        }, check: { self.snapshot("localized-main") })
        step("Delete removes selected page", action: { self.click("Удалить") }, ready: {
            self.list!.rows.count == 1 && self.child(1) == nil
        })
        step("third clone", action: { self.click("Добавить") }, ready: {
            self.child(3) != nil && self.host.activeToken == self.child(3)?.id
        }, check: { self.thirdIdentity = self.child(3)!.identity })
        step("localized Close and lazy page", action: { self.click("Закрыть") }, ready: {
            self.child(3) == nil && self.list!.rows.count == 2 && self.host.activeToken == self.child(2)?.id
        })
        step("main after predecessor selection", action: { self.goMain() }, ready: { self.host.activeToken == self.main?.id && self.table != nil })
        step("select retained open page", action: { self.nativeSelect(0) }, ready: { self.list?.selectedRow == 0 })
        step("final active page", action: { self.click("Открыть") }, ready: {
            self.host.activeToken == self.child(2)?.id
        }, check: {
            self.expected = Expected(rows: self.list!.rows.map(\.id), labels: self.list!.rows.map(\.text), active: self.host.activeScene!.identity, closed: self.thirdIdentity)
            try! JSONEncoder().encode(self.expected!).write(to: self.root.appendingPathComponent("expected.json"))
        })
        if osBackground { return }
        step("native deactivation notification writes single USER", action: {
            NotificationCenter.default.post(name: UIApplication.willResignActiveNotification,
                                            object: UIApplication.shared)
        }, ready: {
            self.host.lastModelExitCode() == 0 && self.host.scenes.isEmpty
        }, check: { require(self.userFiles().count == 1, "background boundary did not persist") })
        step("native activation notification resumes RAM profile", action: {
            NotificationCenter.default.post(name: UIApplication.didBecomeActiveNotification,
                                            object: UIApplication.shared)
        }, ready: {
            self.host.activeScene != nil && self.list?.rows.count == 2
        }, check: {
            require(self.host.activeScene!.identity == self.expected!.active, "foreground lost active identity")
            require(self.list!.rows.map(\.id) == self.expected!.rows, "foreground lost holder identity")
            require(self.list!.rows.map(\.text) == self.expected!.labels, "foreground lost localization/closed state")
            require(self.child(3) == nil, "foreground loaded closed child")
            self.snapshot("foreground-restored")
        })
    }
    func begin() {
        if index == steps.count {
            if osBackground && !cold {
                require(UIApplication.shared.applicationState == .active, "OS fixture must be active before background")
                require(userFiles().isEmpty, "OS fixture saved before the native lifecycle transition")
                try! Data("READY".utf8).write(to: root.appendingPathComponent("ready-os-background"))
                print("IOS_OS_BACKGROUND_READY \(index)"); fflush(stdout)
                // Stay alive. The external runner causes an actual OS app
                // switch, observes the resulting USER and terminates this
                // process before checking a separate cold launch.
                return
            }
            let result: [String: Any] = ["status": "PASS", "checkpoints": index, "cold": cold]
            try! JSONSerialization.data(withJSONObject: result, options: [.prettyPrinted]).write(to: root.appendingPathComponent(cold ? "result-cold.json" : "result-seed.json"))
            print("IOS_ACCEPTANCE_PASS \(index)"); fflush(stdout); exit(0)
        }
        deadline = Date().addingTimeInterval(30)
        steps[index].action(); poll()
    }
    func poll() {
        let item = steps[index]
        if item.ready() {
            item.check(); print("PASS: " + item.name); fflush(stdout); index += 1
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.15) { self.begin() }
        } else if Date() > deadline {
            snapshot("failure")
            fatalError("Timed out: " + item.name)
        } else {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.03) { self.poll() }
        }
    }
}

@main
private struct IosAcceptanceApp: App {
    @StateObject private var host: IosApplicationHost
    private let scenario: Acceptance
    init() {
        ATRegisterWindowCatalogIosApplication()
        let arguments = CommandLine.arguments
        let runIndex = arguments.firstIndex(of: "--run-id")!
        let root = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("WindowCatalogAcceptance/" + arguments[runIndex + 1])
        let application = ATWindowCatalogIosApplication(userDirectory: root.appendingPathComponent("profile").path)
        let host = IosApplicationHost { delegate in application.makeBridge(delegate) }
        _host = StateObject(wrappedValue: host)
        scenario = Acceptance(root: root, cold: arguments.contains("--cold"),
                              osBackground: arguments.contains("--os-background"), host: host)
    }
    var body: some Scene {
        WindowGroup {
            IosApplicationRootView(host: host).onAppear { scenario.start() }
        }
    }
}
