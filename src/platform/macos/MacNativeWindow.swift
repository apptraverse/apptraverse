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
        private var moveStart: NSRect?
        private var resizeStart: NSRect?
        private var mouseUpMonitor: Any?

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
            stopTrackingMove()
            resizeStart = nil
            attachedWindow = window
            guard let window else { return }
            window.tabbingMode = .disallowed
            let id = sceneID
            observe(NSWindow.willCloseNotification, window) { $0.nativeWindowClosed(id, $1) }
            observe(NSWindow.didBecomeKeyNotification, window) { $0.nativeWindowFocused(id, $1) }
            observe(NSWindow.willMoveNotification, window) { [weak self] host, window in
                guard NSEvent.pressedMouseButtons & 1 != 0 else { return }
                self?.trackMove(from: window.frame)
                host.nativeWindowWillMove(id, window)
            }
            observe(NSWindow.didMoveNotification, window) { [weak self] host, window in
                host.nativeWindowChanged(id, window, commit: false)
                if NSEvent.pressedMouseButtons & 1 == 0 { self?.finishMove() }
            }
            observe(NSWindow.willStartLiveResizeNotification, window) { [weak self] _, window in
                self?.resizeStart = window.frame
            }
            observe(NSWindow.didResizeNotification, window) { host, window in
                // SwiftUI solves the viewport live. Save the normal desktop
                // placement once the user finishes tracking the resize.
                if !window.inLiveResize {
                    host.nativeWindowChanged(id, window, commit: false)
                }
            }
            observe(NSWindow.didEndLiveResizeNotification, window) { [weak self] host, window in
                let changed = self?.resizeStart.map { $0 != window.frame } == true
                self?.resizeStart = nil
                host.nativeWindowInteractionFinished(id, window, commit: changed)
            }
            observe(NSWindow.didDeminiaturizeNotification, window) {
                $0.nativeWindowInteractionFinished(id, $1, commit: false)
            }
            observe(NSWindow.didExitFullScreenNotification, window) {
                $0.nativeWindowInteractionFinished(id, $1, commit: false)
            }
            observe(NSWindow.didChangeScreenNotification, window) { $0.nativeWindowChanged(id, $1, commit: false) }
            observe(NSWindow.didChangeBackingPropertiesNotification, window) { $0.nativeWindowChanged(id, $1, commit: false) }
            DispatchQueue.main.async { [weak self, weak window] in
                guard let self, let window, self.attachedWindow === window else { return }
                self.host?.nativeWindowAttached(self.sceneID, window)
                if window.isKeyWindow { self.host?.nativeWindowFocused(self.sceneID, window) }
            }
        }

        private func trackMove(from frame: NSRect) {
            guard moveStart == nil else { return }
            moveStart = frame
            mouseUpMonitor = NSEvent.addLocalMonitorForEvents(matching: .leftMouseUp) { [weak self] event in
                DispatchQueue.main.async { self?.finishMove() }
                return event
            }
            // AppKit's title-bar drag can consume mouse-up in its tracking loop.
            RunLoop.main.perform(inModes: [.default]) { [weak self] in
                if NSEvent.pressedMouseButtons & 1 == 0 { self?.finishMove() }
            }
        }

        private func finishMove() {
            guard let start = moveStart, let window = attachedWindow else { return }
            stopTrackingMove()
            host?.nativeWindowInteractionFinished(sceneID, window, commit: start != window.frame)
        }

        private func stopTrackingMove() {
            if let monitor = mouseUpMonitor { NSEvent.removeMonitor(monitor) }
            mouseUpMonitor = nil
            moveStart = nil
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

        deinit {
            stopTrackingMove()
            observers.forEach(NotificationCenter.default.removeObserver)
        }
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
