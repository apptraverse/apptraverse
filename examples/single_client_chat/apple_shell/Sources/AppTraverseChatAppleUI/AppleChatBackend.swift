import Foundation

/// Real ChatBackend that talks to the Objective-C++ Apple chat bridge.
public final class AppleChatBackend: ChatBackend {
    public private(set) var localUID: String = ""
    public private(set) var timelineRows: [String] = []
    public var onChange: (() -> Void)?

    private let bridge: ATAppleChatBridge

    public init(stateDirectory: URL, clientName: String, localClientName: String) {
        try? FileManager.default.createDirectory(
            at: stateDirectory,
            withIntermediateDirectories: true
        )
        bridge = ATAppleChatBridge(
            stateDirectory: stateDirectory.path,
            clientName: clientName,
            localClientName: localClientName
        )
        bridge.setOnChange { [weak self] in
            self?.syncFromBridge()
        }
        syncFromBridge()
        bridge.start()
    }

    deinit {
        bridge.stop()
    }

    public func addPeer(uid: String) {
        bridge.addPeer(withUID: uid)
    }

    public func submitText(_ text: String) {
        bridge.submitText(text)
    }

    public static func makeHostBackend(clientName: String, localClientName: String) -> AppleChatBackend {
        AppleChatBackend(
            stateDirectory: applicationSupportDirectory(),
            clientName: clientName,
            localClientName: localClientName
        )
    }

    public static func applicationSupportDirectory() -> URL {
        let root = FileManager.default.urls(
            for: .applicationSupportDirectory,
            in: .userDomainMask
        ).first ?? URL(fileURLWithPath: NSTemporaryDirectory(), isDirectory: true)
        return root
            .appendingPathComponent("AppTraverse", isDirectory: true)
            .appendingPathComponent("SingleClientChat", isDirectory: true)
    }

    private func syncFromBridge() {
        localUID = bridge.localUID
        timelineRows = bridge.timelineRows
        onChange?()
    }
}
