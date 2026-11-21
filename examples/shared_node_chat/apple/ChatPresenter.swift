import SwiftUI
#if os(macOS)
import AppKit
#else
import UIKit
#endif

enum ChatRole { case host, client }
struct ChatControlView {
    let text: String, rows: [String]
    let enabled: Bool, visible: Bool, readOnly: Bool
    init(_ v: [String: Any]) {
        text = v["text"] as! String; rows = v["rows"] as! [String]
        enabled = v["enabled"] as! Bool; visible = v["visible"] as! Bool; readOnly = v["readOnly"] as! Bool
    }
}
final class ChatPresenter: ObservableObject {
    private(set) var role: ChatRole
    @Published var controls: [ChatControlView] = []
    @Published var input = "" { didSet {
        if !applying { submittedEdit = max(submittedEdit, acknowledgedEdit) + 1
            pendingInput = submittedEdit; bridge.editDraft(input) }
    } }
    @Published var invite = "" { didSet {
        if !applying { submittedEdit = max(submittedEdit, acknowledgedEdit) + 1
            pendingInvite = submittedEdit; bridge.editInvite(invite) }
    } }
    @Published var title = ""
    @Published var uid = ""
    @Published var status = ""
    @Published var participants: [String] = []
    @Published var messages: [String] = []
    @Published var ready = false
    @Published var inRoom = false
    @Published var labels: [String] = []
    var x: CGFloat = 0, y: CGFloat = 0, restoreWidth: CGFloat = 0, restoreHeight: CGFloat = 0
    var hasExplicitPlacement = false
    var chromeChanged: (() -> Void)?
    private var applying = false
    private var submittedEdit: UInt64 = 0
    private var acknowledgedEdit: UInt64 = 0
    private var pendingInput: UInt64 = 0
    private var pendingInvite: UInt64 = 0
    private var consumedCopy: UInt64 = 0
    private var bridge: ATChatBridge!
    private(set) var stopped = false
    #if CHAT_ACCEPTANCE
    var published: (([String: Any]) -> Void)?
    #endif
    init(profile: String, name: String, role: ChatRole, initialHost: String) {
        self.role = role
        bridge = ATChatBridge(profile: profile, name: name, host: role == .host, initialHost: initialHost,
                              loadOnly: CommandLine.arguments.contains("--load-only")) { [weak self] in self?.refresh() }
    }
    func registerApplePushToken(_ token: String) { bridge.registerApplePushToken(token) }
    func start() -> Bool { bridge.start() }
    func checkpoint(_ completion: @escaping (Bool) -> Void) { bridge.checkpoint(completion) }
    func stop() { guard !stopped else { return }; stopped = true; bridge.stop() }
    func join() { bridge.join() }
    #if os(macOS)
    func observeEnvironment() { bridge.observeEnvironment() }
    func observeWindow(_ window: NSWindow, commit: Bool) { bridge.observeWindow(window, commit: commit) }
    func applyWindowPlacement(_ window: NSWindow) -> Bool { bridge.applyWindowPlacement(window) }
    func resetWindowPlacement() { bridge.resetWindowPlacement() }
    #else
    func observeMobileSettings(_ settings: ATIosDeviceSettings) { bridge.observeMobileSettings(settings) }
    func reportMobileViewport(_ size: CGSize) {
        guard size.width.isFinite, size.height.isFinite,
              size.width >= 1, size.height >= 1,
              size.width <= CGFloat(Int32.max), size.height <= CGFloat(Int32.max) else { return }
        bridge.reportMobileViewport(width: Int32(size.width.rounded()), height: Int32(size.height.rounded()))
    }
    #endif
    func send() { bridge.send() }
    func copyID() {
        guard !controls.isEmpty, controls[2].enabled, !stopped else { return }
        bridge.copyID()
    }
    private func applyCopyRequest(_ request: UInt64) {
        guard request != consumedCopy else { return }
        consumedCopy = request
        #if os(macOS)
        NSPasteboard.general.clearContents()
        bridge.clipboardResult(NSPasteboard.general.setString(controls[1].text, forType: .string))
        #else
        UIPasteboard.general.string = controls[1].text; bridge.clipboardResult(true)
        #endif
    }
    func refresh() {
        guard !stopped else { return }
        let value = bridge.snapshot() as! [String: Any]
        guard value["loaded"] as! Bool else { return }
        applying = true
        controls = (value["controls"] as! [[String: Any]]).map(ChatControlView.init)
        acknowledgedEdit = (value["editRevision"] as! NSNumber).uint64Value
        submittedEdit = max(submittedEdit, acknowledgedEdit)
        // Keep the native text field and caret until the model has consumed
        // its latest edit. Avoid assigning unchanged text to the binding.
        if acknowledgedEdit >= pendingInput && input != controls[9].text { input = controls[9].text }
        if acknowledgedEdit >= pendingInvite && invite != controls[3].text { invite = controls[3].text }
        labels = value["labels"] as! [String]; title = value["title"] as! String
        uid = value["uid"] as! String; status = value["status"] as! String
        participants = value["participants"] as! [String]; messages = value["messages"] as! [String]
        ready = value["ready"] as! Bool; inRoom = value["in_room"] as! Bool
        role = (value["host"] as! Bool) ? .host : .client
        x = CGFloat((value["x"] as! NSNumber).doubleValue); y = CGFloat((value["y"] as! NSNumber).doubleValue)
        restoreWidth = CGFloat((value["restoreWidth"] as! NSNumber).doubleValue)
        restoreHeight = CGFloat((value["restoreHeight"] as! NSNumber).doubleValue)
        hasExplicitPlacement = value["hasExplicitPlacement"] as! Bool
        applying = false; chromeChanged?()
        applyCopyRequest((value["copyRequest"] as! NSNumber).uint64Value)
        #if CHAT_ACCEPTANCE
        published?(value)
        #endif
    }
}
private enum ChatSlot: Int {
    case idLabel, ownID, copy, invite, join, peopleLabel, people
    case messagesLabel, messages, input, send, status
}

// The model owns each control's content and state. SwiftUI measures the
// current font and arranges the controls within the available viewport.
struct ChatPanel: View {
    @ObservedObject var presenter: ChatPresenter

    private func state(_ slot: ChatSlot) -> ChatControlView {
        presenter.controls[slot.rawValue]
    }

    private var roomAddress: some View {
        VStack(alignment: .leading) {
            if state(.idLabel).visible {
                Text(state(.idLabel).text).font(.subheadline)
            }
            HStack {
                if state(.ownID).visible {
                    Text(state(.ownID).text).textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                if state(.invite).visible {
                    TextField(state(.idLabel).text, text: $presenter.invite)
                        .textFieldStyle(.roundedBorder)
                        .onSubmit(presenter.join)
                        .disabled(state(.invite).readOnly || !state(.invite).enabled)
                        .accessibilityIdentifier("chat.invite")
                }
                if state(.copy).visible {
                    Button(state(.copy).text, action: presenter.copyID)
                        .disabled(!state(.copy).enabled)
                        .accessibilityIdentifier("chat.copy")
                }
                if state(.join).visible {
                    Button(state(.join).text, action: presenter.join)
                        .disabled(!state(.join).enabled)
                        .accessibilityIdentifier("chat.join")
                }
            }
        }
    }

    @ViewBuilder
    private func history(_ slot: ChatSlot, label: ChatSlot, id: String) -> some View {
        let rows = state(slot).rows
        let title = state(label).text
        VStack(alignment: .leading) {
            if state(label).visible {
                Text(title).font(.headline).accessibilityAddTraits(.isHeader)
            }
            ScrollViewReader { reader in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 6) {
                        ForEach(Array(rows.enumerated()), id: \.offset) { index, text in
                            Text(text).textSelection(.enabled)
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .id(index)
                        }
                    }.frame(maxWidth: .infinity, alignment: .leading)
                }
                .accessibilityLabel(title)
                .accessibilityIdentifier(id)
                .disabled(!state(slot).enabled)
                .onAppear { if let last = rows.indices.last { reader.scrollTo(last, anchor: .bottom) } }
                .onChange(of: rows.count) { count in
                    if count > 0 { reader.scrollTo(count - 1, anchor: .bottom) }
                }
            }
        }
    }

    private var composer: some View {
        HStack {
            if state(.input).visible {
                TextField(presenter.labels[8], text: $presenter.input)
                    .textFieldStyle(.roundedBorder)
                    .onSubmit(presenter.send)
                    .disabled(state(.input).readOnly || !state(.input).enabled)
                    .accessibilityIdentifier("chat.input")
            }
            if state(.send).visible {
                Button(state(.send).text, action: presenter.send)
                    .disabled(!state(.send).enabled)
                    .accessibilityIdentifier("chat.send")
            }
        }
    }

    var body: some View {
        if presenter.controls.isEmpty {
            ProgressView().frame(maxWidth: .infinity, maxHeight: .infinity)
        } else {
            VStack(alignment: .leading, spacing: 12) {
                roomAddress
                #if os(macOS)
                HStack(alignment: .top, spacing: 16) {
                    if state(.people).visible {
                        history(.people, label: .peopleLabel, id: "chat.participants")
                            .frame(minWidth: 100, idealWidth: 160, maxWidth: 220)
                    }
                    if state(.messages).visible {
                        history(.messages, label: .messagesLabel, id: "chat.messages")
                            .frame(maxWidth: .infinity, maxHeight: .infinity)
                    }
                }
                #else
                if state(.people).visible {
                    history(.people, label: .peopleLabel, id: "chat.participants")
                        .frame(height: 84)
                }
                if state(.messages).visible {
                    history(.messages, label: .messagesLabel, id: "chat.messages")
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
                #endif
                composer
                if state(.status).visible {
                    Text(state(.status).text).font(.caption)
                        .accessibilityIdentifier("chat.status")
                }
            }
            .padding()
        }
    }
}
