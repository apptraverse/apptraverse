import AppKit
import SwiftUI

typealias MacNativeWindowHandle = NSWindow

// SwiftUI owns the scene and content. This attachment only supplies native
// desktop commands and notifications that WindowGroup does not expose.
struct MacNativeWindowAttachment: NSViewRepresentable {
    let sceneID: UUID
    let host: MacApplicationHost

    func makeNSView(context: Context) -> AttachmentView {
        AttachmentView(sceneID: sceneID, host: host)
    }

    func updateNSView(_ view: AttachmentView, context: Context) {}

    final class AttachmentView: NSView {
        private let sceneID: UUID
        private weak var host: MacApplicationHost?
        private weak var attachedWindow: NSWindow?
        private var observers: [NSObjectProtocol] = []

        init(sceneID: UUID, host: MacApplicationHost) {
            self.sceneID = sceneID
            self.host = host
            super.init(frame: .zero)
        }

        required init?(coder: NSCoder) { fatalError("Window attachments are not decoded") }
        override func hitTest(_ point: NSPoint) -> NSView? { nil }

        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            guard window !== attachedWindow else { return }
            observers.forEach(NotificationCenter.default.removeObserver)
            observers.removeAll()
            attachedWindow = window
            guard let window else { return }
            window.tabbingMode = .disallowed
            let id = sceneID
            observe(NSWindow.willCloseNotification, window) { $0.nativeWindowClosed(id, $1) }
            observe(NSWindow.didBecomeKeyNotification, window) { $0.nativeWindowFocused(id, $1) }
            observe(NSWindow.didMoveNotification, window) { host, window in
                if let event = NSApp.currentEvent, event.window === window,
                   event.type == .leftMouseDragged || event.type == .leftMouseUp {
                    host.nativeWindowChanged(id, window, commit: true)
                } else {
                    host.nativeWindowChanged(id, window, commit: false)
                }
            }
            observe(NSWindow.didResizeNotification, window) { host, window in
                // SwiftUI solves the viewport live. Save the normal desktop
                // placement once the user finishes tracking the resize.
                if !window.inLiveResize {
                    host.nativeWindowChanged(id, window, commit: false)
                }
            }
            observe(NSWindow.didEndLiveResizeNotification, window) { $0.nativeWindowChanged(id, $1, commit: true) }
            observe(NSWindow.didChangeScreenNotification, window) { $0.nativeWindowChanged(id, $1, commit: false) }
            observe(NSWindow.didChangeBackingPropertiesNotification, window) { $0.nativeWindowChanged(id, $1, commit: false) }
            DispatchQueue.main.async { [weak self, weak window] in
                guard let self, let window, self.attachedWindow === window else { return }
                self.host?.nativeWindowAttached(self.sceneID, window)
                if window.isKeyWindow { self.host?.nativeWindowFocused(self.sceneID, window) }
            }
        }

        private func observe(_ name: Notification.Name, _ window: NSWindow,
                             _ action: @escaping (MacApplicationHost, NSWindow) -> Void) {
            observers.append(NotificationCenter.default.addObserver(
                forName: name, object: window, queue: .main
            ) { [weak self, weak window] _ in
                guard let host = self?.host, let window else { return }
                action(host, window)
            })
        }

        deinit { observers.forEach(NotificationCenter.default.removeObserver) }
    }
}

// Keep references weak: SwiftUI owns each NSWindow's lifetime.
final class MacNativeWindows {
    private let windows = NSMapTable<NSUUID, NSWindow>.strongToWeakObjects()

    private var environmentObservers: [(NotificationCenter, NSObjectProtocol)] = []

    func observeEnvironment(_ changed: @escaping () -> Void) {
        let sources = [
            (NotificationCenter.default, NSApplication.didChangeScreenParametersNotification),
            (NotificationCenter.default, NSApplication.didBecomeActiveNotification),
            (NSWorkspace.shared.notificationCenter, NSWorkspace.accessibilityDisplayOptionsDidChangeNotification),
        ]
        for (center, name) in sources {
            let token = center.addObserver(forName: name, object: nil, queue: .main) { _ in changed() }
            environmentObservers.append((center, token))
        }
    }

    deinit {
        for (center, observer) in environmentObservers { center.removeObserver(observer) }
    }

    static func disableTabbing() { NSWindow.allowsAutomaticWindowTabbing = false }

    subscript(id: UUID) -> NSWindow? {
        get { windows.object(forKey: id as NSUUID) }
        set {
            if let newValue { windows.setObject(newValue, forKey: id as NSUUID) }
            else { windows.removeObject(forKey: id as NSUUID) }
        }
    }

    func raise(_ window: NSWindow, activate: Bool) {
        if window.isMiniaturized { window.deminiaturize(nil) }
        if activate {
            // Command-line acceptance executables have no regular bundle
            // activation policy until the desktop host promotes them.
            if NSApp.activationPolicy() != .regular {
                NSApp.setActivationPolicy(.regular)
            }
            window.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
        } else {
            window.orderFront(nil)
        }
    }

    func close(_ id: UUID) {
        let window = self[id]
        self[id] = nil
        DispatchQueue.main.async { window?.close() }
    }

    func performClose(_ id: UUID) { self[id]?.performClose(nil) }

    func orderedSceneIDs(_ ids: [UUID]) -> [UUID] {
        NSApp.orderedWindows.compactMap { window in
            ids.first { self[$0] === window }
        }
    }
}
