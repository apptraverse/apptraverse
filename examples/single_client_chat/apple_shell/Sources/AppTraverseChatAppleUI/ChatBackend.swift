import Foundation

/// Plain Swift chat backend used by the shared Apple UI.
///
/// Product hosts always inject `AppleChatBackend`. The in-memory fake lives in
/// `FakeChatBackend.swift` and is compiled only into the UI-only Swift package.
public protocol ChatBackend: AnyObject {
    var localUID: String { get }
    var timelineRows: [String] { get }
    var onChange: (() -> Void)? { get set }

    func addPeer(uid: String)
    func submitText(_ text: String)
}
