import Foundation

/// Plain Swift chat backend used by the shared Apple UI.
///
/// Product hosts inject `AppleChatBackend`. `FakeChatBackend` remains only as a
/// UI-only stand-in when the native bridge is not linked.
public protocol ChatBackend: AnyObject {
    var localUID: String { get }
    var timelineRows: [String] { get }
    var onChange: (() -> Void)? { get set }

    func addPeer(uid: String)
    func submitText(_ text: String)
}

/// In-memory fake backend for the Apple exploration slice.
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
