import Foundation
import SwiftUI
@testable import AppTraverseMacUI

private final class CommandSink: MacUiCommandSink {
    var buttonClicks: [UInt64] = []
    var selections: [Int] = []
    var activations: [Int] = []
    var sizes: [CGSize] = []

    func clickButton(_ token: UInt64) { buttonClicks.append(token) }
    func selectListRow(_ row: Int, token: UInt64) { selections.append(row) }
    func activateListRow(_ row: Int, token: UInt64) { activations.append(row) }
    func submitWindowSize(_ size: CGSize, token: UInt64) { sizes.append(size) }
}

@main
private enum MacUiBackendLifecycleTest {
    static func main() {
        let sink = CommandSink()
        let button = MacButtonAdapter(token: 11, commandSink: sink)
        button.click()
        precondition(sink.buttonClicks.isEmpty)
        button.enabled = true
        button.click()
        button.disconnect()
        button.click()
        precondition(sink.buttonClicks == [11])

        let list = MacListAdapter(token: 12, commandSink: sink)
        let binding = list.selection
        let frame = MacControlFrame(x: 0, y: 0, width: 200, height: 120)
        let first = MacListRowSnapshot(id: 1001, text: "Same label")
        let second = MacListRowSnapshot(id: 1002, text: "Same label")
        list.sync(rows: [first, second], selectedRow: 1, frame: frame)
        precondition(binding.wrappedValue == second.id)
        list.sync(rows: [second], selectedRow: 0, frame: frame)
        binding.wrappedValue = first.id
        list.activate(first.id)
        precondition(sink.selections.isEmpty && sink.activations.isEmpty)
        binding.wrappedValue = second.id
        list.activate(second.id)
        list.disconnect()
        binding.wrappedValue = second.id
        precondition(sink.selections == [0] && sink.activations == [0])

        let window = MacWindowAdapter(token: 13, sceneID: UUID(), commandSink: sink)
        let original = CGSize(width: 300, height: 200)
        let requested = CGSize(width: 410, height: 290)
        window.requestSize(requested)
        window.observeSize(original)
        precondition(sink.sizes.isEmpty && window.requestedSize == requested)
        window.observeSize(requested)
        precondition(window.requestedSize == nil)
        window.observeSize(original)
        window.observeSize(original)
        window.disconnect()
        window.observeSize(requested)
        precondition(sink.sizes == [requested, original])
    }
}
