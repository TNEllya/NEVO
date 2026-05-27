import SwiftUI

struct ConnectView: View {
    @ObservedObject var service: NevoClientService
    @State private var host = "localhost"
    @State private var port = "8899"
    @State private var username = ""
    @State private var password = ""
    @State private var showAdvanced = false
    @FocusState private var focusedField: Field?

    enum Field: Hashable { case host, port, username, password }

    var body: some View {
        ZStack {
            Color(.systemGroupedBackground).ignoresSafeArea()

            VStack(spacing: 0) {
                HStack {
                    Image(systemName: "waveform")
                        .font(.system(size: 48))
                        .foregroundColor(.accentColor)
                    Text("NEVO")
                        .font(.system(size: 36, weight: .bold))
                        .foregroundColor(.accentColor)
                }
                .padding(.top, 60)
                .padding(.bottom, 4)

                Text("Encrypted VoIP")
                    .font(.subheadline)
                    .foregroundColor(.secondary)
                    .padding(.bottom, 40)

                VStack(spacing: 16) {
                    VStack(alignment: .leading, spacing: 4) {
                        Text("Username")
                            .font(.caption)
                            .foregroundColor(.secondary)
                        TextField("Enter username", text: $username)
                            .textFieldStyle(.roundedBorder)
                            .autocorrectionDisabled()
                            .focused($focusedField, equals: .username)
                            .submitLabel(.next)
                            .onSubmit { focusedField = .host }
                    }

                    VStack(alignment: .leading, spacing: 4) {
                        Text("Password (optional)")
                            .font(.caption)
                            .foregroundColor(.secondary)
                        SecureField("Enter password", text: $password)
                            .textFieldStyle(.roundedBorder)
                            .focused($focusedField, equals: .password)
                            .submitLabel(.next)
                            .onSubmit { focusedField = .host }
                    }

                    DisclosureGroup("Server Settings", isExpanded: $showAdvanced) {
                        VStack(spacing: 12) {
                            VStack(alignment: .leading, spacing: 4) {
                                Text("Host")
                                    .font(.caption)
                                    .foregroundColor(.secondary)
                                TextField("Server address", text: $host)
                                    .textFieldStyle(.roundedBorder)
                                    .autocorrectionDisabled()
                                    .focused($focusedField, equals: .host)
                                    .submitLabel(.next)
                                    .onSubmit { focusedField = .port }
                            }
                            VStack(alignment: .leading, spacing: 4) {
                                Text("Port")
                                    .font(.caption)
                                    .foregroundColor(.secondary)
                                TextField("Port", text: $port)
                                    .textFieldStyle(.roundedBorder)
                                    .keyboardType(.numberPad)
                                    .focused($focusedField, equals: .port)
                            }
                        }
                        .padding(.top, 8)
                    }
                }
                .padding(.horizontal, 32)

                if let error = service.connectionError {
                    Text(error)
                        .font(.caption)
                        .foregroundColor(.red)
                        .padding(.top, 12)
                        .padding(.horizontal, 32)
                }

                Button(action: connect) {
                    if service.state == .connecting {
                        ProgressView()
                            .progressViewStyle(CircularProgressViewStyle(tint: .white))
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 14)
                    } else {
                        Text("Connect")
                            .font(.headline)
                            .foregroundColor(.white)
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 14)
                    }
                }
                .background(canConnect ? Color.accentColor : Color.gray)
                .cornerRadius(12)
                .disabled(!canConnect)
                .padding(.horizontal, 32)
                .padding(.top, 24)

                Text("Secure Voice • Low Latency • Encrypted")
                    .font(.caption2)
                    .foregroundColor(.tertiaryLabel)
                    .padding(.top, 30)

                Spacer()
            }
        }
    }

    private var canConnect: Bool {
        !username.isEmpty && !host.isEmpty && !port.isEmpty &&
        service.state != .connecting
    }

    private func connect() {
        guard canConnect, let portNum = UInt16(port) else { return }
        service.connect(server: host, port: portNum, username: username, password: password)
    }
}