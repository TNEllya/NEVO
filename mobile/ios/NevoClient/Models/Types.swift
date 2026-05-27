import Foundation

enum ClientState: Int, Equatable {
    case disconnected = 0
    case connecting  = 1
    case connected   = 2
    case inChannel   = 3
}

enum UserStatus: Int {
    case offline  = 0
    case online   = 1
    case away     = 2
    case muted    = 3
    case deafened = 4
}

enum NevoResultCode: Int {
    case ok                    = 0
    case unknown               = 1
    case authFailed            = 2
    case permissionDenied      = 3
    case channelNotFound       = 4
    case alreadyInChannel      = 5
    case invalidRequest        = 6
    case connectionFailed      = 7
    case timeout               = 8
    case cryptoError           = 10
    case natTraversalFailed    = 11
    case userNotFound          = 12
}

enum NevoMessageType: Int {
    case loginRequest        = 1
    case loginResponse       = 2
    case joinChannel         = 3
    case leaveChannel        = 4
    case createChannel       = 5
    case deleteChannel       = 6
    case channelList         = 7
    case userJoined          = 8
    case userLeft            = 9
    case userSpeaking        = 10
    case pttToggle           = 11
    case muteToggle          = 12
    case serverMessage       = 13
    case stunBindRequest     = 14
    case stunBindResponse    = 15
    case udpPingRequest      = 16
    case udpPingResponse     = 17
    case keyRotationRequest  = 18
    case keyRotationResponse = 19
    case adminAuthRequest    = 20
    case adminAuthResponse   = 21
    case setAdminRequest     = 22
    case setAdminResponse    = 23
    case kickUserRequest     = 24
    case kickUserResponse    = 25
    case banUserRequest      = 26
    case banUserResponse     = 27
    case moveUserRequest     = 28
    case moveUserResponse    = 29
    case chatSend            = 30
    case chatBroadcast       = 31
    case setServerNameRequest   = 32
    case setServerNameResponse  = 33
    case renameChannelRequest   = 34
    case renameChannelResponse  = 35
    case speakingState       = 36
    case fileListRequest     = 40
    case fileListResponse    = 41
    case fileUploadRequest   = 42
    case fileUploadResponse  = 43
    case fileDeleteRequest   = 49
    case fileDeleteResponse  = 50
    case screenShareStart    = 60
    case screenShareStop     = 61
    case screenShareState    = 62
    case bindOwnerRequest    = 70
    case bindOwnerResponse   = 71
}

struct UserInfo: Identifiable, Equatable {
    let id: UInt64
    let username: String
    let status: UserStatus
    let muted: Bool
    let deafened: Bool
    let groupId: UInt32

    static func == (lhs: UserInfo, rhs: UserInfo) -> Bool {
        lhs.id == rhs.id
    }
}

struct ChannelInfo: Identifiable {
    let id: UInt64
    let name: String
    let parentId: UInt64
    var children: [ChannelInfo]
    var users: [UserInfo]
}

struct NevoError: Error, LocalizedError {
    let code: NevoResultCode
    let message: String

    var errorDescription: String? { message }
}

struct AudioDeviceInfo: Identifiable {
    let id: String
    let name: String
    let isDefault: Bool
    let isInput: Bool
}

enum InputMode: String, CaseIterable {
    case continuous = "continuous"
    case ptt = "ptt"
    case vad = "vad"

    var displayName: String {
        switch self {
        case .continuous: return "Continuous"
        case .ptt:        return "Push-to-Talk"
        case .vad:        return "Voice Activity"
        }
    }
}