import SwiftUI

struct UserListView: View {
    @ObservedObject var service: NevoClientService

    private var currentChannelUsers: [UserInfo] {
        guard let channelId = service.currentChannelId else { return [] }
        return findChannelUsers(in: service.channels, channelId: channelId)
    }

    var body: some View {
        NavigationStack {
            Group {
                if service.currentChannelId == nil {
                    VStack(spacing: 12) {
                        Image(systemName: "person.2")
                            .font(.system(size: 40))
                            .foregroundColor(.secondary)
                        Text("Join a channel to see users")
                            .foregroundColor(.secondary)
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                } else if currentChannelUsers.isEmpty {
                    VStack(spacing: 12) {
                        Image(systemName: "person.fill.questionmark")
                            .font(.system(size: 40))
                            .foregroundColor(.secondary)
                        Text("No users in this channel")
                            .foregroundColor(.secondary)
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                } else {
                    List {
                        if let me = service.myUser {
                            userCell(me, isMe: true)
                        }
                        ForEach(currentChannelUsers.filter { $0.id != service.myUser?.id }) { user in
                            userCell(user, isMe: false)
                                .contextMenu {
                                    if service.isAdmin && user.id != service.myUser?.id {
                                        Button(role: .destructive) { service.kickUser(user.id) } label: { Label("Kick", systemImage: "xmark.circle") }
                                        Button(role: .destructive) { service.banUser(user.id) } label: { Label("Ban", systemImage: "hand.raised") }
                                        Divider()
                                        ForEach(service.channels.filter { $0.id != service.currentChannelId }) { ch in
                                            Button { service.moveUser(user.id, to: ch.id) } label: { Label("Move to \(ch.name)", systemImage: "arrow.right") }
                                        }
                                    }
                                }
                        }
                    }
                }
            }
            .navigationTitle("Users (\(currentChannelUsers.count))")
        }
    }

    @ViewBuilder
    private func userCell(_ user: UserInfo, isMe: Bool) -> some View {
        HStack(spacing: 12) {
            ZStack {
                Circle()
                    .fill(user.status == .online ? Color.green : Color.gray)
                    .frame(width: 36, height: 36)
                Text(String(user.username.prefix(1)).uppercased())
                    .font(.caption)
                    .fontWeight(.bold)
                    .foregroundColor(.white)
            }

            VStack(alignment: .leading, spacing: 2) {
                Text(user.username + (isMe ? " (You)" : ""))
                    .font(.subheadline)
                    .fontWeight(.medium)
                HStack(spacing: 4) {
                    if service.speakingUsers.contains(user.id) {
                        Image(systemName: "waveform")
                            .font(.caption2)
                            .foregroundColor(.green)
                        Text("Speaking")
                            .font(.caption2)
                            .foregroundColor(.green)
                    } else {
                        Text(user.status == .online ? "Online" : "Offline")
                            .font(.caption2)
                            .foregroundColor(.secondary)
                    }
                    if user.muted { Text("• Muted").font(.caption2).foregroundColor(.orange) }
                    if user.deafened { Text("• Deafened").font(.caption2).foregroundColor(.red) }
                }
            }

            Spacer()
        }
        .padding(.vertical, 4)
    }

    private func findChannelUsers(in channels: [ChannelInfo], channelId: UInt64) -> [UserInfo] {
        for channel in channels {
            if channel.id == channelId { return channel.users }
            let found = findChannelUsers(in: channel.children, channelId: channelId)
            if !found.isEmpty { return found }
        }
        return []
    }
}