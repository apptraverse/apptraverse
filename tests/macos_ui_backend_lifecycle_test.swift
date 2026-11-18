import Foundation
import SwiftUI
@testable import AppTraverseMacUI

private final class CommandSink: MacUiCommandSink {
    var buttonClicks: [UInt64] = []
    var selections: [Int] = []
    var activations: [Int] = []

    func clickButton(_ token: UInt64) { buttonClicks.append(token) }
    func selectListRow(_ row: Int, token: UInt64) { selections.append(row) }
    func activateListRow(_ row: Int, token: UInt64) { activations.append(row) }
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
        let first = MacListRowSnapshot(id: 1001, text: "Same label")
        let second = MacListRowSnapshot(id: 1002, text: "Same label")
        list.sync(rows: [first, second], selectedRow: 1)
        precondition(binding.wrappedValue == second.id)
        list.sync(rows: [second], selectedRow: 0)
        binding.wrappedValue = first.id
        list.activate(first.id)
        precondition(sink.selections.isEmpty && sink.activations.isEmpty)
        binding.wrappedValue = second.id
        list.activate(second.id)
        list.disconnect()
        binding.wrappedValue = second.id
        precondition(sink.selections == [0] && sink.activations == [0])

    }
}
