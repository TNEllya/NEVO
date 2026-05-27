import Foundation
import Combine

class NevoClientService: ObservableObject {
    @Published var state: ClientState = .disconnected
    @Published var serverName: String = "NEVO Server"
    @Published var channels: [ChannelInfo] = []
    @Published var myUser: UserInfo?
    @Published var currentChannelId: UInt64?
    @Published var chatMessages: [ChatMessage] = []
    @Published var speakingUsers: Set<UInt64> = []
    @Published var isMuted = false
    @Published var isDeafened = false
    @Published var isPTT = false
    @Published var inputMode: InputMode = .continuous
    @Published var connectionError: String?
    @Published var isAdmin = false
    @Published var serverAddress: String = ""
    @Published var serverPort: UInt16 = 8899

    private var tcp = TCPConnection()
    private var udp = UDPVoiceSocket()
    private var crypto = VoiceCrypto()
    private var audio = AudioEngine()
    private var clientKeyPair: (publicKey: Data, secretKey: Data)?
    private var sessionToken: String = ""
    private var serverPublicKey: Data = Data()
    private var sessionKey: Data = Data()
    private var keyEpoch: UInt64 = 0
    private var voiceSequence: UInt32 = 0
    private var voiceNonceGenerator = UInt64.random(in: 0...(1<<48 - 1))
    private var udpPort: UInt16 = 0
    private var videoUdpPort: UInt16 = 0
    private var serverUdpPort: UInt16 = 0
    private var serverVideoUdpPort: UInt16 = 0
    private var udpReady = false

    struct ChatMessage: Identifiable {
        let id = UUID()
        let senderId: UInt64
        let senderName: String
        let channelId: UInt64
        let text: String
        let timestamp: Date
    }

    init() {
        tcp.delegate = self
        udp.delegate = self
        audio.delegate = self
    }

    convenience init(serverAddress: String, serverPort: UInt16, username: String, password: String) {
        self.init()
        connect(server: serverAddress, port: serverPort, username: username, password: password)
    }

    func connect(server host: String, port: UInt16, username: String, password: String) {
        guard state == .disconnected else { return }
        serverAddress = host
        serverPort = port
        state = .connecting
        connectionError = nil

        clientKeyPair = crypto.generateKeyPair()
        guard clientKeyPair != nil else {
            connectionError = "Key generation failed"
            state = .disconnected
            return
        }

        do {
            try audio.setup()
        } catch {
            connectionError = "Audio setup failed: \(error.localizedDescription)"
            state = .disconnected
            return
        }

        udpPort = 0
        udp.bind(port: udpPort)
        tcp.connect(host: host, port: port)
    }

    func disconnect() {
        tcp.disconnect()
        udp.close()
        audio.cleanup()
        state = .disconnected
        channels.removeAll()
        chatMessages.removeAll()
        speakingUsers.removeAll()
        currentChannelId = nil
        myUser = nil
        isAdmin = false
    }

    func joinChannel(_ channelId: UInt64) {
        let payload = WireFormat.encodeJoinChannel(channelId)
        tcp.send(type: .joinChannel, payload: payload)
    }

    func leaveChannel() {
        tcp.send(type: .leaveChannel, payload: Data())
        currentChannelId = nil
        audio.stop()
        try? audio.start()
    }

    func sendChat(_ text: String) {
        guard let channelId = currentChannelId else { return }
        let payload = WireFormat.encodeChatSend(channelId: channelId, text: text)
        tcp.send(type: .chatSend, payload: payload)
    }

    func toggleMute() {
        isMuted.toggle()
        audio.setMuted(isMuted)
        tcp.send(type: .muteToggle, payload: WireFormat.encodeMuteToggle(isMuted))
    }

    func toggleDeafen() {
        isDeafened.toggle()
        audio.setDeafened(isDeafened)
    }

    func setInputMode(_ mode: InputMode) {
        inputMode = mode
        audio.setInputMode(mode)
    }

    func setPTT(_ active: Bool) {
        isPTT = active
        audio.setPtt(active)
        tcp.send(type: .pttToggle, payload: WireFormat.encodePttToggle(active))
    }

    func createChannel(parentId: UInt64, name: String) {
        tcp.send(type: .createChannel, payload: WireFormat.encodeCreateChannel(parentId: parentId, name: name))
    }

    func deleteChannel(_ channelId: UInt64) {
        tcp.send(type: .deleteChannel, payload: WireFormat.encodeDeleteChannel(channelId))
    }

    func renameChannel(_ channelId: UInt64, name: String) {
        var payload = Data()
        payload.append(contentsOf: withUnsafeBytes(of: channelId.littleEndian) { Data($0) })
        payload.append(WireFormat.encodeCreateChannel(parentId: 0, name: name).dropFirst(8))
        tcp.send(type: .renameChannelRequest, payload: payload)
    }

    func adminAuth(password: String) {
        tcp.send(type: .adminAuthRequest, payload: WireFormat.encodeAdminAuth(password: password))
    }

    func kickUser(_ userId: UInt64, reason: String = "") {
        tcp.send(type: .kickUserRequest, payload: WireFormat.encodeKickUser(userId: userId, reason: reason))
    }

    func banUser(_ userId: UInt64, reason: String = "", expiresAt: UInt64 = 0) {
        tcp.send(type: .banUserRequest, payload: WireFormat.encodeBanUser(userId: userId, reason: reason, expiresAt: expiresAt))
    }

    func moveUser(_ userId: UInt64, to channelId: UInt64) {
        tcp.send(type: .moveUserRequest, payload: WireFormat.encodeMoveUser(userId: userId, channelId: channelId))
    }

    func bindOwner(bindKey: String) {
        tcp.send(type: .bindOwnerRequest, payload: WireFormat.encodeBindOwner(bindKey: bindKey))
    }

    func setServerName(_ name: String) {
        var payload = Data()
        let nameData = name.data(using: .utf8) ?? Data()
        payload.append(contentsOf: withUnsafeBytes(of: UInt32(nameData.count).littleEndian) { Data($0) })
        payload.append(nameData)
        tcp.send(type: .setServerNameRequest, payload: payload)
    }

    private func startVoiceTransmission() {
        guard let sessionKey = crypto.randomBytes(count: 32) as Data?, udpReady else { return }
        self.sessionKey = sessionKey
        crypto.setSessionKey(sessionKey, epoch: keyEpoch)
        try? audio.start()
    }
}

extension NevoClientService: TCPConnectionDelegate {
    func tcpConnectionDidConnect(_ connection: TCPConnection) {
        guard let keyPair = clientKeyPair else { return }
        let payload = WireFormat.encodeLoginRequest(
            username: myUser?.username ?? "iOSUser",
            password: "",
            publicKey: keyPair.publicKey,
            udpPort: udpPort,
            videoUdpPort: videoUdpPort
        )
        tcp.send(type: .loginRequest, payload: payload)
    }

    func tcpConnectionDidDisconnect(_ connection: TCPConnection, error: Error?) {
        DispatchQueue.main.async {
            self.state = .disconnected
            self.connectionError = error?.localizedDescription ?? "Connection lost"
            self.udp.close()
            self.audio.cleanup()
        }
    }

    func tcpConnection(_ connection: TCPConnection, didReceiveMessage type: NevoMessageType, payload: Data) {
        DispatchQueue.main.async {
            self.handleMessage(type: type, payload: payload)
        }
    }

    private func handleMessage(type: NevoMessageType, payload: Data) {
        switch type {
        case .loginResponse:
            let (result, userInfo, token, srvPubKey, encryptedKey, ownerExists, srvUdpPort, srvVidPort) = WireFormat.decodeLoginResponse(payload)
            if result == 0, let user = userInfo {
                myUser = user
                sessionToken = token
                serverPublicKey = srvPubKey
                serverUdpPort = srvUdpPort
                serverVideoUdpPort = srvVidPort
                if let keyPair = clientKeyPair, let sessionKey = crypto.randomBytes(count: 32) as Data? {
                    self.sessionKey = sessionKey
                }
                state = .connected
                udp.connect(host: serverAddress, port: serverUdpPort)
                if serverUdpPort > 0 {
                    let ping = WireFormat.encodeUdpPing(sequence: 1, clientUdpKey: sessionKey)
                    udp.sendToServer(ping)
                }
            } else {
                state = .disconnected
                connectionError = "Login failed (code \(result))"
            }

        case .channelList:
            channels = WireFormat.decodeChannelListUpdate(payload)

        case .userJoined:
            let (user, channelId) = WireFormat.decodeUserJoined(payload)
            addUserToChannel(user, channelId: channelId)

        case .userLeft:
            let (userId, channelId) = WireFormat.decodeUserLeft(payload)
            removeUserFromChannel(userId, channelId: channelId)

        case .userSpeaking, .speakingState:
            let (userId, speaking) = WireFormat.decodeUserSpeaking(payload)
            if speaking { speakingUsers.insert(userId) }
            else { speakingUsers.remove(userId) }

        case .chatBroadcast:
            let (senderId, senderName, channelId, text, timestamp) = WireFormat.decodeChatBroadcast(payload)
            let msg = ChatMessage(senderId: senderId, senderName: senderName, channelId: channelId, text: text, timestamp: Date(timeIntervalSince1970: TimeInterval(timestamp)))
            chatMessages.append(msg)
            if chatMessages.count > 500 { chatMessages.removeFirst(chatMessages.count - 500) }

        case .serverMessage:
            let msg = WireFormat.decodeServerMessage(payload)
            chatMessages.append(ChatMessage(senderId: 0, senderName: "Server", channelId: 0, text: msg, timestamp: Date()))

        case .keyRotationRequest:
            let (newSrvPubKey, epoch, encKey) = WireFormat.decodeKeyRotationRequest(payload)
            if let keyPair = clientKeyPair, let newSessionKey = crypto.randomBytes(count: 32) as Data? {
                self.serverPublicKey = newSrvPubKey
                self.keyEpoch = epoch
                crypto.setSessionKey(newSessionKey, epoch: epoch)
                self.sessionKey = newSessionKey
            }

        case .adminAuthResponse:
            let (result, msg) = WireFormat.decodeAdminAuthResponse(payload)
            if result == 0 { isAdmin = true }
            chatMessages.append(ChatMessage(senderId: 0, senderName: "System", channelId: 0, text: "Admin: \(msg)", timestamp: Date()))

        case .setAdminResponse, .kickUserResponse, .banUserResponse, .moveUserResponse:
            let (_, msg) = WireFormat.decodeAdminActionResponse(payload)
            if !msg.isEmpty {
                chatMessages.append(ChatMessage(senderId: 0, senderName: "System", channelId: 0, text: msg, timestamp: Date()))
            }

        case .bindOwnerResponse:
            let (result, msg, _) = WireFormat.decodeBindOwnerResponse(payload)
            chatMessages.append(ChatMessage(senderId: 0, senderName: "System", channelId: 0, text: "Owner: \(msg)", timestamp: Date()))
            if result == 0 { isAdmin = true }

        default:
            break
        }
    }

    private func addUserToChannel(_ user: UserInfo, channelId: UInt64) {
        for i in 0..<channels.count {
            if channels[i].id == channelId {
                var updated = channels[i]
                if !updated.users.contains(where: { $0.id == user.id }) {
                    updated.users.append(user)
                }
                channels[i] = updated
                break
            }
            updateChildChannel(in: &channels, channelId: channelId, addUser: user)
        }
    }

    private func updateChildChannel(in channels: inout [ChannelInfo], channelId: UInt64, addUser user: UserInfo) {
        for i in 0..<channels.count {
            if channels[i].id == channelId {
                if !channels[i].users.contains(where: { $0.id == user.id }) {
                    channels[i].users.append(user)
                }
                return
            }
            updateChildChannel(in: &channels[i].children, channelId: channelId, addUser: user)
        }
    }

    private func removeUserFromChannel(_ userId: UInt64, channelId: UInt64) {
        for i in 0..<channels.count {
            if channels[i].id == channelId {
                var updated = channels[i]
                updated.users.removeAll { $0.id == userId }
                channels[i] = updated
                return
            }
            removeChildUser(from: &channels, channelId: channelId, userId: userId)
        }
    }

    private func removeChildUser(from channels: inout [ChannelInfo], channelId: UInt64, userId: UInt64) {
        for i in 0..<channels.count {
            if channels[i].id == channelId {
                channels[i].users.removeAll { $0.id == userId }
                return
            }
            removeChildUser(from: &channels[i].children, channelId: channelId, userId: userId)
        }
    }
}

extension NevoClientService: UDPVoiceSocketDelegate {
    func udpVoiceSocketDidBecomeReady(_ socket: UDPVoiceSocket) {
        udpReady = true
        if state == .connected { startVoiceTransmission() }
    }

    func udpVoiceSocket(_ socket: UDPVoiceSocket, didReceiveEncryptedData data: Data, from endpoint: NWEndpoint) {
        guard data.count >= 24 + 1 + 16 else { return }
        let nonce = data.prefix(24)
        let ciphertextAndTag = data.suffix(from: 24)
        if let plaintext = crypto.decryptVoiceFrame(Data(ciphertextAndTag), nonce: Data(nonce)) {
            audio.receiveEncodedFrame(plaintext)
        }
    }

    func udpVoiceSocket(_ socket: UDPVoiceSocket, didEncounterError error: Error) {
        print("[UDP] Error: \(error)")
    }
}

extension NevoClientService: AudioEngineDelegate {
    func audioEngine(_ engine: AudioEngine, didCaptureEncodedFrame data: Data) {
        guard udpReady, crypto.hasKey, serverUdpPort > 0 else { return }
        let nonce = generateVoiceNonce()
        guard let encrypted = crypto.encryptVoiceFrame(data, nonce: nonce) else { return }
        var packet = Data()
        packet.append(nonce)
        packet.append(encrypted)
        udp.sendToServer(packet)
    }

    func audioEngine(_ engine: AudioEngine, didDetectVoiceActivity speaking: Bool) {
        tcp.send(type: .speakingState, payload: WireFormat.encodeSpeakingState(speaking))
    }

    private func generateVoiceNonce() -> Data {
        voiceNonceGenerator += 1
        var nonce = Data(count: 24)
        nonce[0] = 0
        let seq = voiceSequence.littleEndian
        voiceSequence &+= 1
        nonce.replaceSubrange(1..<5, with: withUnsafeBytes(of: seq) { Data($0) })
        nonce.replaceSubrange(5..<13, with: withUnsafeBytes(of: voiceNonceGenerator.littleEndian) { Data($0) })
        return nonce
    }
}