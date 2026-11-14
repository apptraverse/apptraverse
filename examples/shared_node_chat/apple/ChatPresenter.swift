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
    let x: CGFloat, y: CGFloat, width: CGFloat, height: CGFloat
    init(_ v: [String: Any]) {
        text = v["text"] as! String; rows = v["rows"] as! [String]
        enabled = v["enabled"] as! Bool; visible = v["visible"] as! Bool; readOnly = v["readOnly"] as! Bool
        x = CGFloat((v["x"] as! NSNumber).doubleValue); y = CGFloat((v["y"] as! NSNumber).doubleValue)
        width = CGFloat((v["width"] as! NSNumber).doubleValue); height = CGFloat((v["height"] as! NSNumber).doubleValue)
    }
}
final class ChatPresenter: ObservableObject {
    let role: ChatRole
    @Published var controls: [ChatControlView] = []
    @Published var input = "" { didSet { if !applying { bridge.editDraft(input) } } }
    @Published var invite = "" { didSet { if !applying { bridge.editInvite(invite) } } }
    @Published var title = ""
    @Published var uid = ""
    @Published var status = ""
    @Published var participants: [String] = []
    @Published var messages: [String] = []
    @Published var ready = false
    @Published var inRoom = false
    var labels: [String] = []
    var width: CGFloat = 0, height: CGFloat = 0, minimumWidth: CGFloat = 0, minimumHeight: CGFloat = 0, gap: CGFloat = 0
    var chromeChanged: (() -> Void)?
    private var applying = false
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
    func start() -> Bool { bridge.start() }
    func checkpoint(_ completion: @escaping (Bool) -> Void) { bridge.checkpoint(completion) }
    func stop() { guard !stopped else { return }; stopped = true; bridge.stop() }
    func resize(_ size: CGSize) { if size.width > 0 && size.height > 0 { bridge.resizeWidth(Int32(size.width), height: Int32(size.height)) } }
    func join() { bridge.join() }
    func send() { bridge.send() }
    func copyID() {
        guard controls.count > 2, controls[2].enabled else { return }
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
        input = controls[9].text; invite = controls[3].text
        labels = value["labels"] as! [String]; title = value["title"] as! String
        uid = value["uid"] as! String; status = value["status"] as! String
        participants = value["participants"] as! [String]; messages = value["messages"] as! [String]
        ready = value["ready"] as! Bool; inRoom = value["in_room"] as! Bool
        width = CGFloat((value["width"] as! NSNumber).doubleValue); height = CGFloat((value["height"] as! NSNumber).doubleValue)
        minimumWidth = CGFloat((value["minimumWidth"] as! NSNumber).doubleValue); minimumHeight = CGFloat((value["minimumHeight"] as! NSNumber).doubleValue)
        gap = CGFloat((value["gap"] as! NSNumber).doubleValue)
        applying = false; chromeChanged?()
        #if CHAT_ACCEPTANCE
        published?(value)
        #endif
    }
}
// Control visibility, enabled state, text and rectangles come from native UI
// mirror presenters. SwiftUI provides native styling and value input bindings.
struct ChatPanel: View {
    @ObservedObject var presenter: ChatPresenter
    @ViewBuilder private func control(_ index: Int, _ value: ChatControlView) -> some View {
        switch index {
        case 1: Text(value.text).textSelection(.enabled)
        case 2: Button(value.text, action: presenter.copyID).accessibilityIdentifier("chat.copy")
        case 3: TextField(presenter.labels[2], text: $presenter.invite).onSubmit(presenter.join)
                .disabled(value.readOnly).accessibilityIdentifier("chat.invite")
        case 4: Button(value.text, action: presenter.join).accessibilityIdentifier("chat.join")
        case 6, 8:
            ScrollViewReader { reader in
                ScrollView {
                    VStack(alignment: .leading, spacing: presenter.gap) {
                        ForEach(Array(value.rows.enumerated()), id: \.offset) { index, text in
                            Text(text).textSelection(.enabled).frame(maxWidth: .infinity, alignment: .leading).id(index)
                        }
                    }.frame(maxWidth: .infinity, alignment: .leading)
                }.accessibilityIdentifier(index == 6 ? "chat.participants" : "chat.messages")
                 .onChange(of: value.rows.count) { count in if count > 0 { reader.scrollTo(count - 1, anchor: .bottom) } }
            }
        case 9: TextField(presenter.labels[8], text: $presenter.input).onSubmit(presenter.send).accessibilityIdentifier("chat.input")
        case 10: Button(value.text, action: presenter.send).accessibilityIdentifier("chat.send")
        case 11: Text(value.text).font(.caption).accessibilityIdentifier("chat.status")
        default: Text(value.text)
        }
    }
    var body: some View {
        GeometryReader { geometry in
            ZStack(alignment: .topLeading) {
                ForEach(Array(presenter.controls.enumerated()), id: \.offset) { index, value in
                    if value.visible {
                        control(index, value).disabled(!value.enabled)
                            .frame(width: value.width, height: value.height, alignment: .leading)
                            .position(x: value.x + value.width / 2, y: value.y + value.height / 2)
                    }
                }
            }.onAppear { presenter.resize(geometry.size) }
             .onChange(of: geometry.size) { size in presenter.resize(size) }
        }
    }
}
