import Combine
import Foundation

/// Observable Apple-side view model over a `ChatBackend`.
///
/// SwiftUI → ChatViewModel → ChatBackend → Objective-C++ bridge → C++ runtime
public final class ChatViewModel: ObservableObject {
    @Published public private(set) var localUID: String
    @Published public private(set) var timelineRows: [String]
    @Published public var remoteUID: String = ""
    @Published public var draft: String = ""

    private let backend: ChatBackend

    public init(backend: ChatBackend) {
        self.backend = backend
        self.localUID = backend.localUID
        self.timelineRows = backend.timelineRows
        backend.onChange = { [weak self] in
            guard let self else {
                return
            }
            DispatchQueue.main.async {
                self.localUID = self.backend.localUID
                self.timelineRows = self.backend.timelineRows
            }
        }
    }

    public func addPeer() {
        backend.addPeer(uid: remoteUID)
        remoteUID = ""
        localUID = backend.localUID
        timelineRows = backend.timelineRows
    }

    public func send() {
        backend.submitText(draft)
        draft = ""
        localUID = backend.localUID
        timelineRows = backend.timelineRows
    }
}
