import SwiftUI

struct ChannelListView: View {
    @ObservedObject var service: NevoClientService
    @State private var expandedChannels: Set<UInt64> = []
    @State private var showCreateAlert = false
    @State private var showDeleteAlert = false
    @State private var newChannelName = ""
    @State private var selectedParentId: UInt64 = 0
    @State private var targetChannelId: UInt64 = 0
    @State private var showRenameAlert = false
    @State private var renameText = ""

    var body: some View {
        NavigationStack {
            List {
                ForEach(service.channels) { channel in
                    channelRow(channel, indent: 0)
                }
            }
            .listStyle(.plain)
            .navigationTitle(service.serverName)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    if service.isAdmin {
                        Menu {
                            Button(action: { showCreateAlert = true; selectedParentId = 0 }) {
                                Label("Create Root Channel", systemImage: "plus.square")
                            }
                        } label: {
                            Image(systemName: "plus")
                        }
                    }
                }
                ToolbarItem(placement: .navigationBarLeading) {
                    HStack(spacing: 2) {
                        Circle()
                            .fill(service.state == .inChannel ? Color.green : Color.yellow)
                            .frame(width: 8, height: 8)
                        Text(service.state == .inChannel ? "Connected" : "Idle")
                            .font(.caption2)
                            .foregroundColor(.secondary)
                    }
                }
            }
            .alert("Create Channel", isPresented: $showCreateAlert) {
                TextField("Channel name", text: $newChannelName)
                Button("Create") {
                    service.createChannel(parentId: selectedParentId, name: newChannelName)
                    newChannelName = ""
                }
                Button("Cancel", role: .cancel) {}
            }
            .alert("Rename Channel", isPresented: $showRenameAlert) {
                TextField("New name", text: $renameText)
                Button("Rename") {
                    service.renameChannel(targetChannelId, name: renameText)
                    renameText = ""
                }
                Button("Cancel", role: .cancel) {}
            }
            .alert("Delete Channel", isPresented: $showDeleteAlert) {
                Button("Delete", role: .destructive) {
                    service.deleteChannel(targetChannelId)
                }
                Button("Cancel", role: .cancel) {}
            }
        }
    }

    @ViewBuilder
    private func channelRow(_ channel: ChannelInfo, indent: Int) -> some View {
        let isExpanded = expandedChannels.contains(channel.id)
        let hasChildren = !channel.children.isEmpty
        let isCurrent = service.currentChannelId == channel.id

        DisclosureGroup(isExpanded: Binding(
            get: { isExpanded || hasChildren },
            set: { newVal in
                if newVal { expandedChannels.insert(channel.id) }
                else { expandedChannels.remove(channel.id) }
            }
        )) {
            ForEach(channel.children) { child in
                channelRow(child, indent: indent + 1)
            }
            ForEach(channel.users) { user in
                HStack {
                    Text(padding(indent + 1) + "  ")
                        .foregroundColor(.clear)
                    Image(systemName: "circle.fill")
                        .font(.system(size: 8))
                        .foregroundColor(user.status == .online ? .green : .gray)
                    Text(user.username)
                        .font(.subheadline)
                    Spacer()
                    if service.speakingUsers.contains(user.id) {
                        Image(systemName: "waveform")
                            .foregroundColor(.green)
                            .font(.caption)
                    }
                }
                .contentShape(Rectangle())
                .contextMenu {
                    if service.isAdmin {
                        Button(role: .destructive) { service.kickUser(user.id) } label: { Label("Kick", systemImage: "xmark") }
                        Button(role: .destructive) { service.banUser(user.id) } label: { Label("Ban", systemImage: "hand.raised") }
                    }
                }
            }
        } label: {
            HStack {
                Text(padding(indent))
                    .foregroundColor(.clear)
                Image(systemName: hasChildren ? (isExpanded ? "chevron.down" : "chevron.right") : "number")
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .frame(width: 14)
                Text(channel.name)
                    .fontWeight(isCurrent ? .bold : .regular)
                Spacer()
                Text("\(channel.users.count)")
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .padding(.horizontal, 6)
                    .padding(.vertical, 2)
                    .background(Color(.systemGray5))
                    .cornerRadius(4)
            }
            .contentShape(Rectangle())
            .onTapGesture {
                service.joinChannel(channel.id)
            }
            .contextMenu {
                if service.isAdmin {
                    Button { selectedParentId = channel.id; newChannelName = ""; showCreateAlert = true } label: { Label("Create Sub-channel", systemImage: "plus.square") }
                    Button { targetChannelId = channel.id; renameText = channel.name; showRenameAlert = true } label: { Label("Rename", systemImage: "pencil") }
                    if !channel.children.isEmpty || !channel.users.isEmpty {
                        Button(role: .destructive) { } label: { Label("Delete (not empty)", systemImage: "trash") }.disabled(true)
                    } else {
                        Button(role: .destructive) { targetChannelId = channel.id; showDeleteAlert = true } label: { Label("Delete", systemImage: "trash") }
                    }
                }
            }
        }
    }

    private func padding(_ level: Int) -> String {
        String(repeating: "    ", count: max(0, level))
    }
}