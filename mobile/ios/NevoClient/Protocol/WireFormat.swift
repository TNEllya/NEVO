import Foundation

class ByteBuffer {
    private(set) var data: Data
    private var readOffset: Int = 0

    init(data: Data = Data()) {
        self.data = data
    }

    var remaining: Int { data.count - readOffset }

    func writeUInt16(_ value: UInt16) { data.append(contentsOf: withUnsafeBytes(of: value.littleEndian) { Data($0) }) }
    func writeUInt32(_ value: UInt32) { data.append(contentsOf: withUnsafeBytes(of: value.littleEndian) { Data($0) }) }
    func writeUInt64(_ value: UInt64) { data.append(contentsOf: withUnsafeBytes(of: value.littleEndian) { Data($0) }) }
    func writeBool(_ value: Bool) { data.append(value ? 1 : 0) }
    func writeString(_ value: String) {
        let encoded = value.data(using: .utf8) ?? Data()
        writeUInt32(UInt32(encoded.count))
        data.append(encoded)
    }
    func writeBytes(_ value: Data) {
        writeUInt32(UInt32(value.count))
        data.append(value)
    }
    func writeRaw(_ value: Data) { data.append(value) }

    func readUInt16() -> UInt16 {
        let val: UInt16 = data.subdata(in: readOffset..<readOffset+2).withUnsafeBytes { $0.load(as: UInt16.self) }
        readOffset += 2
        return UInt16(littleEndian: val)
    }
    func readUInt32() -> UInt32 {
        let val: UInt32 = data.subdata(in: readOffset..<readOffset+4).withUnsafeBytes { $0.load(as: UInt32.self) }
        readOffset += 4
        return UInt32(littleEndian: val)
    }
    func readUInt64() -> UInt64 {
        let val: UInt64 = data.subdata(in: readOffset..<readOffset+8).withUnsafeBytes { $0.load(as: UInt64.self) }
        readOffset += 8
        return UInt64(littleEndian: val)
    }
    func readBool() -> Bool { let v = data[readOffset]; readOffset += 1; return v != 0 }
    func readString() -> String {
        let len = Int(readUInt32())
        let strData = data.subdata(in: readOffset..<readOffset+len)
        readOffset += len
        return String(data: strData, encoding: .utf8) ?? ""
    }
    func readBytes() -> Data {
        let len = Int(readUInt32())
        let bytes = data.subdata(in: readOffset..<readOffset+len)
        readOffset += len
        return bytes
    }
    func readRaw(_ count: Int) -> Data {
        let bytes = data.subdata(in: readOffset..<readOffset+count)
        readOffset += count
        return bytes
    }
}

enum WireFormat {
    static let tcpHeaderSize = 12

    static func serializeUserInfo(_ user: UserInfo) -> Data {
        let buf = ByteBuffer()
        buf.writeUInt64(user.id)
        buf.writeString(user.username)
        buf.writeUInt32(UInt32(user.status.rawValue))
        buf.writeBool(user.muted)
        buf.writeBool(user.deafened)
        buf.writeUInt32(user.groupId)
        return buf.data
    }

    static func deserializeUserInfo(_ data: Data) -> UserInfo {
        let buf = ByteBuffer(data: data)
        return UserInfo(
            id: buf.readUInt64(),
            username: buf.readString(),
            status: UserStatus(rawValue: Int(buf.readUInt32())) ?? .offline,
            muted: buf.readBool(),
            deafened: buf.readBool(),
            groupId: buf.readUInt32()
        )
    }

    static func serializeChannelInfo(_ channel: ChannelInfo) -> Data {
        let buf = ByteBuffer()
        buf.writeUInt64(channel.id)
        buf.writeString(channel.name)
        buf.writeUInt64(channel.parentId)
        buf.writeUInt32(UInt32(channel.children.count))
        for child in channel.children {
            let childData = serializeChannelInfo(child)
            buf.writeUInt32(UInt32(childData.count))
            buf.writeRaw(childData)
        }
        buf.writeUInt32(UInt32(channel.users.count))
        for user in channel.users {
            let userData = serializeUserInfo(user)
            buf.writeUInt32(UInt32(userData.count))
            buf.writeRaw(userData)
        }
        return buf.data
    }

    static func deserializeChannelInfo(_ data: Data) -> ChannelInfo {
        let buf = ByteBuffer(data: data)
        let id = buf.readUInt64()
        let name = buf.readString()
        let parentId = buf.readUInt64()
        let childrenCount = Int(buf.readUInt32())
        var children: [ChannelInfo] = []
        for _ in 0..<childrenCount {
            let size = Int(buf.readUInt32())
            children.append(deserializeChannelInfo(buf.readRaw(size)))
        }
        let usersCount = Int(buf.readUInt32())
        var users: [UserInfo] = []
        for _ in 0..<usersCount {
            let size = Int(buf.readUInt32())
            users.append(deserializeUserInfo(buf.readRaw(size)))
        }
        return ChannelInfo(id: id, name: name, parentId: parentId, children: children, users: users)
    }
}

extension WireFormat {

    static func encodeLoginRequest(username: String, password: String, publicKey: Data, udpPort: UInt16, videoUdpPort: UInt16) -> Data {
        let buf = ByteBuffer()
        buf.writeString(username)
        buf.writeBytes(password.data(using: .utf8) ?? Data())
        let methods = ["X25519+crypto_box_seal", "X25519"]
        buf.writeUInt32(UInt32(methods.count))
        for m in methods { buf.writeString(m) }
        buf.writeBytes(publicKey)
        buf.writeUInt16(udpPort)
        buf.writeUInt16(videoUdpPort)
        return buf.data
    }

    static func decodeLoginResponse(_ data: Data) -> (result: Int, userInfo: UserInfo?, sessionToken: String, serverPublicKey: Data, encryptedSessionKey: Data, ownerExists: Bool, serverUdpPort: UInt16, serverVideoUdpPort: UInt16) {
        let buf = ByteBuffer(data: data)
        let result = Int(buf.readUInt32())
        var userInfo: UserInfo?
        if result == 0 {
            userInfo = UserInfo(
                id: buf.readUInt64(),
                username: buf.readString(),
                status: UserStatus(rawValue: Int(buf.readUInt32())) ?? .online,
                muted: buf.readBool(),
                deafened: buf.readBool(),
                groupId: buf.readUInt32()
            )
        }
        let sessionToken = buf.readString()
        let serverPublicKey = buf.readBytes()
        let keyExchangeMethod = buf.readString()
        let encryptedSessionKey = buf.readBytes()
        let ownerExists = buf.readBool()
        var serverUdpPort: UInt16 = 0
        var serverVideoUdpPort: UInt16 = 0
        if buf.remaining >= 2 { serverUdpPort = buf.readUInt16() }
        if buf.remaining >= 2 { serverVideoUdpPort = buf.readUInt16() }
        return (result, userInfo, sessionToken, serverPublicKey, encryptedSessionKey, ownerExists, serverUdpPort, serverVideoUdpPort)
    }

    static func encodeJoinChannel(_ channelId: UInt64) -> Data {
        let buf = ByteBuffer()
        buf.writeUInt64(channelId)
        return buf.data
    }

    static func encodeLeaveChannel() -> Data { Data() }

    static func encodeChatSend(channelId: UInt64, text: String) -> Data {
        let buf = ByteBuffer()
        buf.writeUInt64(channelId)
        buf.writeString(text)
        return buf.data
    }

    static func decodeChatBroadcast(_ data: Data) -> (senderId: UInt64, senderName: String, channelId: UInt64, text: String, timestamp: UInt64) {
        let buf = ByteBuffer(data: data)
        return (buf.readUInt64(), buf.readString(), buf.readUInt64(), buf.readString(), buf.readUInt64())
    }

    static func decodeChannelListUpdate(_ data: Data) -> [ChannelInfo] {
        let buf = ByteBuffer(data: data)
        let count = Int(buf.readUInt32())
        var channels: [ChannelInfo] = []
        for _ in 0..<count {
            let size = Int(buf.readUInt32())
            channels.append(deserializeChannelInfo(buf.readRaw(size)))
        }
        return channels
    }

    static func decodeUserJoined(_ data: Data) -> (UserInfo, UInt64) {
        let userData = deserializeUserInfo(data)
        let buf = ByteBuffer(data: data)
        _ = buf.readUInt64(); _ = buf.readString(); _ = buf.readUInt32(); _ = buf.readBool(); _ = buf.readBool(); _ = buf.readUInt32()
        let channelId = buf.remaining >= 8 ? buf.readUInt64() : 0
        return (userData, channelId)
    }

    static func decodeUserLeft(_ data: Data) -> (userId: UInt64, channelId: UInt64) {
        let buf = ByteBuffer(data: data)
        return (buf.readUInt64(), buf.remaining >= 8 ? buf.readUInt64() : 0)
    }

    static func decodeUserSpeaking(_ data: Data) -> (userId: UInt64, speaking: Bool) {
        let buf = ByteBuffer(data: data)
        return (buf.readUInt64(), buf.readBool())
    }

    static func decodeServerMessage(_ data: Data) -> String {
        let buf = ByteBuffer(data: data)
        return buf.readString()
    }

    static func encodeMuteToggle(_ muted: Bool) -> Data {
        let buf = ByteBuffer()
        buf.writeBool(muted)
        return buf.data
    }

    static func encodeSpeakingState(_ speaking: Bool) -> Data {
        let buf = ByteBuffer()
        buf.writeBool(speaking)
        return buf.data
    }

    static func encodePttToggle(_ active: Bool) -> Data {
        let buf = ByteBuffer()
        buf.writeBool(active)
        return buf.data
    }

    static func encodeCreateChannel(parentId: UInt64, name: String) -> Data {
        let buf = ByteBuffer()
        buf.writeUInt64(parentId)
        buf.writeString(name)
        return buf.data
    }

    static func encodeDeleteChannel(_ channelId: UInt64) -> Data {
        let buf = ByteBuffer()
        buf.writeUInt64(channelId)
        return buf.data
    }

    static func encodeAdminAuth(password: String) -> Data {
        let buf = ByteBuffer()
        buf.writeString(password)
        return buf.data
    }

    static func decodeAdminAuthResponse(_ data: Data) -> (result: Int, message: String) {
        let buf = ByteBuffer(data: data)
        return (Int(buf.readUInt32()), buf.readString())
    }

    static func encodeSetAdmin(userId: UInt64, setAdmin: Bool) -> Data {
        let buf = ByteBuffer()
        buf.writeUInt64(userId)
        buf.writeBool(setAdmin)
        return buf.data
    }

    static func decodeAdminActionResponse(_ data: Data) -> (result: Int, message: String) {
        let buf = ByteBuffer(data: data)
        return (Int(buf.readUInt32()), buf.readString())
    }

    static func encodeKickUser(userId: UInt64, reason: String) -> Data {
        let buf = ByteBuffer()
        buf.writeUInt64(userId)
        buf.writeString(reason)
        return buf.data
    }

    static func encodeBanUser(userId: UInt64, reason: String, expiresAt: UInt64) -> Data {
        let buf = ByteBuffer()
        buf.writeUInt64(userId)
        buf.writeString(reason)
        buf.writeUInt64(expiresAt)
        return buf.data
    }

    static func encodeMoveUser(userId: UInt64, channelId: UInt64) -> Data {
        let buf = ByteBuffer()
        buf.writeUInt64(userId)
        buf.writeUInt64(channelId)
        return buf.data
    }

    static func decodeKeyRotationRequest(_ data: Data) -> (newServerPublicKey: Data, keyEpoch: UInt64, encryptedSessionKey: Data) {
        let buf = ByteBuffer(data: data)
        return (buf.readBytes(), buf.readUInt64(), buf.remaining > 0 ? buf.readBytes() : Data())
    }

    static func encodeUdpPing(sequence: UInt32, clientUdpKey: Data) -> Data {
        let buf = ByteBuffer()
        buf.writeUInt32(sequence)
        buf.writeBytes(clientUdpKey)
        return buf.data
    }

    static func decodeUdpPingResponse(_ data: Data) -> (sequence: UInt32, udpReachable: Bool) {
        let buf = ByteBuffer(data: data)
        return (buf.readUInt32(), buf.readBool())
    }

    static func encodeBindOwner(bindKey: String) -> Data {
        let buf = ByteBuffer()
        buf.writeString(bindKey)
        return buf.data
    }

    static func decodeBindOwnerResponse(_ data: Data) -> (result: Int, message: String, ownerUserId: UInt64) {
        let buf = ByteBuffer(data: data)
        return (Int(buf.readUInt32()), buf.readString(), buf.remaining >= 8 ? buf.readUInt64() : 0)
    }
}