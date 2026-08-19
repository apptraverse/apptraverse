import Foundation

/// In-memory backend for SwiftUI previews and the UI-only Swift package.
/// Product macOS/iOS targets do not compile this file.
public final class FakeChatBackend: ChatBackend {
    public let localUID: String
    public private(set) var timelineRows: [String]
    public var onChange: (() -> Void)?

    public init(localUID: String = "APPLE-LOCAL") {
        self.localUID = localUID
        self.timelineRows = ["* Apple joined"]
    }

    public func addPeer(uid: String) {
        let trimmed = uid.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else {
            return
        }
        timelineRows.append("* Peer added: \(trimmed)")
        onChange?()
    }

    public func submitText(_ text: String) {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else {
            return
        }
        timelineRows.append("Apple: \(trimmed)")
        onChange?()
    }
}

public extension ChatViewModel {
    convenience init() {
        self.init(backend: FakeChatBackend())
    }
}
