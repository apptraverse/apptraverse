import SwiftUI
#if os(macOS)
import AppKit
#else
import UIKit
#endif

// Only value publications cross the C++ worker boundary. Editing fields is UI
// state; actions delegate to ChatSession's ModelObjectProxy command queue.
enum ChatRole { case host, client }

final class ChatPresenter: ObservableObject {
    let role: ChatRole
    private let initialHost: String
    private var initialActionSent = false
    @Published var uid = "Connecting..."
    @Published var invite = ""
    @Published var input = ""
    @Published var status = "Connecting to Aether..."
    @Published var inputError = ""
    @Published var participants: [String] = []
    @Published var messages: [String] = []
    @Published var ready = false
    @Published var inRoom = false
    private var shownInvite = ""
    private var bridge: ATChatBridge!
    private(set) var stopped = false
    #if CHAT_ACCEPTANCE
    var published: (([String: Any]) -> Void)?
    #endif

    init(profile: String, name: String, role: ChatRole, initialHost: String) {
        self.role = role; self.initialHost = initialHost; invite = initialHost
        bridge = ATChatBridge(profile: profile, name: name) { [weak self] in self?.refresh() }
    }
    func start() -> Bool { bridge.start() }
    func checkpoint(_ completion: @escaping (Bool) -> Void) { bridge.checkpoint(completion) }
    func stop() { guard !stopped else { return }; stopped = true; bridge.stop() }
    func copyID() {
        guard ready else { return }
        #if os(macOS)
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(uid, forType: .string)
        #else
        UIPasteboard.general.string = uid
        #endif
    }
    func join() { if !stopped && ready && !inRoom { bridge.joinRoom(invite) } }
    func send() {
        guard !stopped && ready && inRoom,
              !input.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return }
        guard input.utf8.count <= 8192 else {
            inputError = "Message is too long (maximum 8192 UTF-8 bytes)."; return
        }
        inputError = ""; bridge.send(input); input = ""
    }
    func refresh() {
        guard !stopped else { return }
        let value = bridge.snapshot() as! [String: Any]
        let local = value["uid"] as! String
        let nextUID = local.isEmpty ? "Connecting..." : local
        let nextReady = value["ready"] as! Bool
        let nextRoom = value["in_room"] as! Bool
        let nextStatus = ((value["online"] as! Bool) ? "Online | " : "Offline | ") + (value["status"] as! String)
        // Diagnostic ACK/queue changes do not invalidate unchanged UI fields.
        if uid != nextUID { uid = nextUID }
        if ready != nextReady { ready = nextReady }
        if inRoom != nextRoom { inRoom = nextRoom }
        if status != nextStatus { status = nextStatus }
        let nextInvite = value["invite"] as! String
        if !nextInvite.isEmpty && nextInvite != shownInvite { shownInvite = nextInvite; invite = nextInvite }
        let nextPeople = value["participants"] as! [String]
        let nextMessages = value["messages"] as! [String]
        if participants != nextPeople { participants = nextPeople }
        if messages != nextMessages { messages = nextMessages }
        if ready && !inRoom && !initialActionSent {
            initialActionSent = true
            if role == .host { bridge.createRoom() }
            else if !initialHost.isEmpty { bridge.joinRoom(initialHost) }
        }
        #if CHAT_ACCEPTANCE
        published?(value)
        #endif
    }
}

