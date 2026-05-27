import SwiftUI

struct ContentView: View {
    @StateObject var service = NevoClientService()

    var body: some View {
        Group {
            if service.state == .disconnected {
                ConnectView(service: service)
            } else {
                TabView {
                    ChannelListView(service: service)
                        .tabItem {
                            Label("Channels", systemImage: "list.bullet")
                        }

                    ChatView(service: service)
                        .tabItem {
                            Label("Chat", systemImage: "bubble.left.and.bubble.right")
                        }

                    UserListView(service: service)
                        .tabItem {
                            Label("Users", systemImage: "person.2")
                        }
                }
                .toolbar {
                    ToolbarItemGroup(placement: .bottomBar) {
                        Button(action: { service.toggleMute() }) {
                            Image(systemName: service.isMuted ? "mic.slash.fill" : "mic.fill")
                                .foregroundColor(service.isMuted ? .red : .primary)
                        }

                        Button(action: { service.toggleDeafen() }) {
                            Image(systemName: service.isDeafened ? "ear.badge.waveform" : "ear")
                                .foregroundColor(service.isDeafened ? .red : .primary)
                        }

                        Spacer()

                        Picker("Input", selection: Binding(
                            get: { service.inputMode },
                            set: { service.setInputMode($0) }
                        )) {
                            ForEach(InputMode.allCases, id: \.self) { mode in
                                Text(mode.displayName).tag(mode)
                            }
                        }
                        .pickerStyle(.segmented)
                        .frame(maxWidth: 200)

                        Spacer()

                        if service.inputMode == .ptt {
                            Button(action: { service.setPTT(!service.isPTT) }) {
                                Text(service.isPTT ? "TALKING" : "PTT")
                                    .font(.caption)
                                    .fontWeight(.bold)
                                    .foregroundColor(.white)
                                    .padding(.horizontal, 12)
                                    .padding(.vertical, 6)
                                    .background(service.isPTT ? Color.green : Color.gray)
                                    .cornerRadius(6)
                            }
                        }

                        Spacer()

                        Button(action: { service.disconnect() }) {
                            Image(systemName: "phone.down.fill")
                                .foregroundColor(.red)
                        }

                        if service.isAdmin {
                            Button(action: { /* admin panel */ }) {
                                Image(systemName: "shield.fill")
                                    .foregroundColor(.orange)
                            }
                        }
                    }
                }
            }
        }
    }
}