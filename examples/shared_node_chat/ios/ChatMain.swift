import SwiftUI
import UIKit

final class PhoneChatHost: ObservableObject {
    let presenter: ChatPresenter
    private var started = false
    private var checkpointGeneration: UInt64 = 0
    private var backgroundTask: UIBackgroundTaskIdentifier = .invalid

    init() {
        let args = CommandLine.arguments
        func value(_ flag: String) -> String? {
            guard let i = args.firstIndex(of: flag), i + 1 < args.count,
                  !args[i + 1].hasPrefix("--") else { return nil }
            return args[i + 1]
        }
        let role: ChatRole = args.contains("--host") ? .host : .client
        let profile = value("--profile") ?? FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("AppTraverse/SharedNodeChat/" + (role == .host ? "host" : "client")).path
        presenter = ChatPresenter(profile: profile, name: value("--name") ?? "iPhone", role: role,
                                  initialHost: value("--client") ?? "")
    }
    func start() {
        guard !started else { return }
        started = true
        if !presenter.start() { presenter.inputError = "Cannot open chat profile." }
    }
    func checkpoint() {
        guard started, presenter.ready, backgroundTask == .invalid else { return }
        // Keep only enough background execution time to drain the model queue
        // and persist USER. Never stop/recreate the session on deactivation.
        checkpointGeneration &+= 1
        let generation = checkpointGeneration
        backgroundTask = UIApplication.shared.beginBackgroundTask(withName: "Save chat") { [weak self] in
            self?.finishCheckpoint(generation)
        }
        presenter.checkpoint { [weak self] ok in
            guard let self else { return }
            if !ok { self.presenter.inputError = "Could not save chat state." }
            self.finishCheckpoint(generation)
        }
    }
    private func finishCheckpoint(_ generation: UInt64) {
        guard generation == checkpointGeneration, backgroundTask != .invalid else { return }
        let task = backgroundTask; backgroundTask = .invalid
        UIApplication.shared.endBackgroundTask(task)
    }
}

struct PhoneChatView: View {
    @ObservedObject var presenter: ChatPresenter
    @State private var showParticipants = false
    var body: some View {
        NavigationStack {
            VStack(spacing: 10) {
                VStack(alignment: .leading, spacing: 6) {
                    Text(presenter.role == .host ? "My Aether ID" : "Host Aether ID").font(.caption).foregroundStyle(.secondary)
                    HStack {
                        if presenter.role == .host {
                            Text(presenter.uid).font(.footnote).textSelection(.enabled)
                                .frame(maxWidth: .infinity, alignment: .leading)
                            Button("Copy", action: presenter.copyID).disabled(!presenter.ready)
                                .accessibilityIdentifier("chat.copy")
                        } else {
                            if presenter.inRoom {
                                Text(presenter.invite).font(.footnote).textSelection(.enabled)
                                    .frame(maxWidth: .infinity, alignment: .leading)
                            } else {
                                TextField("Host Aether ID", text: $presenter.invite)
                                    .textInputAutocapitalization(.never).autocorrectionDisabled()
                                    .textFieldStyle(.roundedBorder).onSubmit(presenter.join)
                                    .accessibilityIdentifier("chat.invite")
                            }
                            Button("Join", action: presenter.join).disabled(!presenter.ready || presenter.inRoom)
                                .accessibilityIdentifier("chat.join")
                        }
                    }
                }.padding(.horizontal)
                ScrollViewReader { reader in
                    ScrollView {
                        LazyVStack(alignment: .leading, spacing: 12) {
                            ForEach(Array(presenter.messages.enumerated()), id: \.offset) { index, message in
                                Text(message).textSelection(.enabled)
                                    .frame(maxWidth: .infinity, alignment: .leading).id(index)
                            }
                        }.frame(maxWidth: .infinity, alignment: .leading).padding()
                    }.scrollDismissesKeyboard(.interactively).accessibilityIdentifier("chat.messages")
                        .background(Color(uiColor: .secondarySystemBackground))
                        .onChange(of: presenter.messages.count) { _, count in
                            if count > 0 { reader.scrollTo(count - 1, anchor: .bottom) }
                        }
                }
                HStack(alignment: .bottom) {
                    TextField("Message", text: $presenter.input, axis: .vertical)
                        .lineLimit(1...4).textFieldStyle(.roundedBorder)
                        .disabled(!presenter.ready || !presenter.inRoom).accessibilityIdentifier("chat.input")
                    Button("Send", action: presenter.send).buttonStyle(.borderedProminent)
                        .disabled(!presenter.ready || !presenter.inRoom).accessibilityIdentifier("chat.send")
                }.padding(.horizontal)
                Text(presenter.inputError.isEmpty ? presenter.status : presenter.inputError)
                    .font(.caption).foregroundStyle(.secondary).frame(maxWidth: .infinity, alignment: .leading)
                    .accessibilityIdentifier("chat.status").padding(.horizontal)
            }.padding(.bottom, 8).navigationTitle("SharedNode Chat").navigationBarTitleDisplayMode(.inline)
                .toolbar {
                    ToolbarItem(placement: .topBarTrailing) {
                        Button { showParticipants = true } label: { Label("Participants", systemImage: "person.2") }
                            .accessibilityIdentifier("chat.participants")
                    }
                }.sheet(isPresented: $showParticipants) {
                    NavigationStack {
                        List(Array(presenter.participants.enumerated()), id: \.offset) { _, name in Text(name) }
                            .navigationTitle("Participants").navigationBarTitleDisplayMode(.inline)
                            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { showParticipants = false } } }
                    }.presentationDetents([.medium, .large])
                }
        }
    }
}

@main
struct PhoneChatApp: App {
    @StateObject private var host = PhoneChatHost()
    @Environment(\.scenePhase) private var scenePhase
    var body: some Scene {
        WindowGroup {
            PhoneChatView(presenter: host.presenter).task { host.start() }
                .onChange(of: scenePhase) { _, phase in
                    if phase == .inactive || phase == .background { host.checkpoint() }
                }
        }
    }
}
