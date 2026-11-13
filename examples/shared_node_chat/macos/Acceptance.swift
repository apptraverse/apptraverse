#if CHAT_ACCEPTANCE
import AppKit

extension ChatHost {
    func installCommands() {
        ATChatEnableAccessibility()
        presenter.published = { [weak self] value in
            DispatchQueue.main.async { self?.emit(value) }
        }
        _ = fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK)
        let source = DispatchSource.makeReadSource(fileDescriptor: STDIN_FILENO, queue: .main)
        source.setEventHandler { [weak self] in self?.readCommands() }
        commandSource = source; source.resume()
    }
    func emit(_ value: [String: Any]) {
        content.layoutSubtreeIfNeeded()
        var state = value
        state["rendered"] = presenter.messages.joined(separator: "\n")
        state["visible_strings"] = ATChatVisibleStrings(content!)
        state["join_enabled"] = ATChatEnabled(content!, "chat.join")
        state["role_ready"] = ATChatEnabled(content!, presenter.role == .host ? "chat.copy" : "chat.join")
        state["send_enabled"] = ATChatEnabled(content!, "chat.send")
        let frame = ATChatFrame(content!, "chat.messages")
        state["messages_width"] = frame.width
        state["messages_height"] = frame.height
        let data = try! JSONSerialization.data(withJSONObject: state)
        print("CHATUI " + String(decoding: data, as: UTF8.self)); fflush(stdout)
    }
    func readCommands() {
        var bytes = [UInt8](repeating: 0, count: 4096)
        let count = read(STDIN_FILENO, &bytes, bytes.count)
        if count == 0 { commandSource?.cancel(); NSApp.terminate(nil); return }
        if count < 0 { return }
        pending.append(contentsOf: bytes.prefix(count))
        while let end = pending.firstIndex(of: 10) {
            let line = pending.prefix(upTo: end); pending.removeSubrange(...end)
            let command = try! JSONSerialization.jsonObject(with: line) as! [String: String]
            switch command["action"]! {
            case "join", "send":
                let action = command["action"]!
                if action == "join" { presenter.invite = command["text"]! }
                if action == "send" { presenter.input = command["text"]! }
                DispatchQueue.main.async { [self] in
                    content.layoutSubtreeIfNeeded()
                    precondition(ATChatPress(content!, "chat." + action), "SwiftUI button not accessible: " + action)
                }
            case "snapshot": presenter.refresh()
            case "screenshot":
                content.layoutSubtreeIfNeeded()
                let captured = CGWindowListCreateImage(.null, .optionIncludingWindow,
                    CGWindowID(window.windowNumber), [.boundsIgnoreFraming, .bestResolution])!
                let bitmap = NSBitmapImageRep(cgImage: captured)
                try! bitmap.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: command["path"]!))
            case "quit": NSApp.terminate(nil)
            default: preconditionFailure("Unknown acceptance command")
            }
        }
    }
}
#endif
