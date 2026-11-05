import AppKit
import SwiftUI
@testable import AppTraverseMacUI

private func require(_ condition: @autoclosure () -> Bool,
                     _ message: String) {
    if !condition() {
        fatalError(message)
    }
}

private final class CommandSink: MacUiCommandSink {
    var buttonClicks: [UInt64] = []
    var selections: [(token: UInt64, row: Int)] = []
    var frames: [(token: UInt64, frame: CGRect)] = []

    func clickButton(_ token: UInt64) {
        buttonClicks.append(token)
    }

    func selectListRow(_ row: Int, token: UInt64) {
        selections.append((token, row))
    }

    func submitWindowFrame(_ frame: CGRect, token: UInt64) {
        frames.append((token, frame))
    }
}

private final class WindowDelegate: NSObject, NSWindowDelegate {}

@main
private enum MacUiBackendLifecycleTest {
    static func main() {
        _ = NSApplication.shared
        testDetachedControlCallbacks()
        testWindowHostingCycleRelease()
        testStableListRowIdentity()
    }

    private static func testDetachedControlCallbacks() {
        let sink = CommandSink()

        let oldButton = MacButtonAdapter(token: 11, commandSink: sink)
        let oldList = MacListAdapter(token: 12, commandSink: sink)
        let oldSelection = oldList.selection
        oldButton.disconnect()
        oldList.disconnect()

        oldButton.click()
        oldSelection.wrappedValue = 1
        require(sink.buttonClicks.isEmpty,
                "Detached button callback reached the command sink")
        require(sink.selections.isEmpty,
                "Detached list binding reached the command sink")

        let newButton = MacButtonAdapter(token: 21, commandSink: sink)
        let newList = MacListAdapter(token: 22, commandSink: sink)
        newButton.click()
        newList.selection.wrappedValue = 0
        require(sink.buttonClicks == [21],
                "Replacement button did not reach the command sink")
        require(sink.selections.count == 1 &&
                sink.selections[0].token == 22 &&
                sink.selections[0].row == 0,
                "Replacement list did not reach the command sink")
    }

    private static func testWindowHostingCycleRelease() {
        weak var releasedAdapter: MacWindowAdapter?
        weak var releasedHostingView: NSView?

        autoreleasepool {
            let sink = CommandSink()
            let delegate = WindowDelegate()
            var adapter: MacWindowAdapter? =
                MacWindowAdapter(token: 31, commandSink: sink,
                                 delegate: delegate)
            releasedAdapter = adapter
            releasedHostingView = adapter!.window.contentView
            adapter!.disconnect()
            adapter = nil
        }

        require(releasedAdapter == nil,
                "Window adapter remained retained after unload")
        require(releasedHostingView == nil,
                "SwiftUI hosting content remained retained after unload")
    }

    private static func testStableListRowIdentity() {
        let sink = CommandSink()
        let adapter = MacListAdapter(token: 41, commandSink: sink)
        let frame = MacControlFrame(x: 0, y: 0, width: 200, height: 120)
        let rowA = MacListRowSnapshot(id: 1001, text: "Same label")
        let rowB = MacListRowSnapshot(id: 1002, text: "Same label")

        adapter.sync(rows: [rowA, rowB], selectedRow: nil, frame: frame)
        adapter.sync(rows: [rowB], selectedRow: nil, frame: frame)
        require(adapter.rows == [rowB],
                "Structural update did not preserve Row B identity")

        adapter.selection.wrappedValue = 0
        require(sink.selections.count == 1 &&
                sink.selections[0].row == 0,
                "Selection did not use Row B's current index")
    }
}
