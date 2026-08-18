import SwiftUI

/// Shared SwiftUI chat shell compiled for both macOS and iOS.
public struct ChatView: View {
    @StateObject private var model: ChatViewModel

    public init(model: ChatViewModel = ChatViewModel()) {
        _model = StateObject(wrappedValue: model)
    }

    public var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("AppTraverse Chat")
                .font(.title2)
                .bold()

            HStack(alignment: .firstTextBaseline, spacing: 8) {
                Text("Local UID")
                    .foregroundStyle(.secondary)
                Text(model.localUID)
                    .textSelection(.enabled)
                    .lineLimit(1)
                    .truncationMode(.middle)
            }

            HStack(spacing: 8) {
                remoteUIDField
                Button("Add") {
                    model.addPeer()
                }
                .disabled(trimmed(model.remoteUID).isEmpty)
                .accessibilityIdentifier("addPeerButton")
            }

            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 4) {
                        ForEach(Array(model.timelineRows.enumerated()), id: \.offset) { index, row in
                            Text(row)
                                .textSelection(.enabled)
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .id(index)
                        }
                    }
                    .padding(8)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(Color.primary.opacity(0.05))
                .clipShape(RoundedRectangle(cornerRadius: 6))
                .accessibilityIdentifier("transcript")
                .onChange(of: model.timelineRows.count) { _ in
                    if let last = model.timelineRows.indices.last {
                        proxy.scrollTo(last, anchor: .bottom)
                    }
                }
            }

            HStack(spacing: 8) {
                messageField
                Button("Send") {
                    model.send()
                }
                .disabled(trimmed(model.draft).isEmpty)
                .accessibilityIdentifier("sendButton")
            }
        }
        .padding()
        .frame(minWidth: 420, minHeight: 320)
    }

    private var remoteUIDField: some View {
        let field = TextField("Remote UID", text: $model.remoteUID)
            .textFieldStyle(.roundedBorder)
            .accessibilityIdentifier("remoteUIDField")
        return applyPlainTextInput(field)
    }

    private var messageField: some View {
        let field = TextField("Message", text: $model.draft)
            .textFieldStyle(.roundedBorder)
            .onSubmit {
                model.send()
            }
            .accessibilityIdentifier("messageField")
        return applyPlainTextInput(field)
    }

    private func applyPlainTextInput<V: View>(_ view: V) -> some View {
        #if os(iOS)
        view
            .textInputAutocapitalization(.never)
            .autocorrectionDisabled(true)
        #else
        view
        #endif
    }

    private func trimmed(_ text: String) -> String {
        text.trimmingCharacters(in: .whitespacesAndNewlines)
    }
}
