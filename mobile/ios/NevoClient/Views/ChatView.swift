import SwiftUI

struct ChatView: View {
    @ObservedObject var service: NevoClientService
    @State private var messageText = ""
    @FocusState private var isFocused: Bool
    @State private var scrollProxy: ScrollViewProxy?

    var body: some View {
        VStack(spacing: 0) {
            if service.currentChannelId != nil {
                ScrollViewReader { proxy in
                    ScrollView {
                        LazyVStack(alignment: .leading, spacing: 6) {
                            ForEach(service.chatMessages) { msg in
                                chatBubble(msg)
                                    .id(msg.id)
                            }
                        }
                        .padding(.horizontal, 12)
                        .padding(.vertical, 8)
                    }
                    .onAppear { scrollProxy = proxy }
                    .onChange(of: service.chatMessages.count) { _ in
                        if let last = service.chatMessages.last {
                            withAnimation { proxy.scrollTo(last.id, anchor: .bottom) }
                        }
                    }
                }

                Divider()

                HStack(spacing: 8) {
                    TextField("Message", text: $messageText, axis: .vertical)
                        .textFieldStyle(.roundedBorder)
                        .focused($isFocused)
                        .lineLimit(1...4)
                        .submitLabel(.send)
                        .onSubmit { sendMessage() }

                    Button(action: sendMessage) {
                        Image(systemName: "arrow.up.circle.fill")
                            .font(.system(size: 28))
                            .foregroundColor(messageText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty ? .gray : .accentColor)
                    }
                    .disabled(messageText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                }
                .padding(.horizontal, 12)
                .padding(.vertical, 8)
            } else {
                VStack(spacing: 12) {
                    Image(systemName: "bubble.left.and.text.bubble.right")
                        .font(.system(size: 48))
                        .foregroundColor(.secondary)
                    Text("Select a channel to chat")
                        .font(.headline)
                        .foregroundColor(.secondary)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
    }

    @ViewBuilder
    private func chatBubble(_ msg: NevoClientService.ChatMessage) -> some View {
        let isMine = msg.senderId == (service.myUser?.id ?? 0)
        let isServer = msg.senderId == 0

        HStack {
            if isMine { Spacer() }

            VStack(alignment: isMine ? .trailing : .leading, spacing: 2) {
                if !isMine {
                    Text(isServer ? "📢 \(msg.senderName)" : msg.senderName)
                        .font(.caption)
                        .foregroundColor(isServer ? .orange : .accentColor)
                }
                Text(msg.text)
                    .font(.subheadline)
                    .padding(.horizontal, 12)
                    .padding(.vertical, 8)
                    .background(isMine ? Color.accentColor : Color(.systemGray5))
                    .foregroundColor(isMine ? .white : .primary)
                    .cornerRadius(12)
            }

            if !isMine { Spacer() }
        }
    }

    private func sendMessage() {
        let text = messageText.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return }
        service.sendChat(text)
        messageText = ""
    }
}