import Foundation
import Network

protocol TCPConnectionDelegate: AnyObject {
    func tcpConnectionDidConnect(_ connection: TCPConnection)
    func tcpConnectionDidDisconnect(_ connection: TCPConnection, error: Error?)
    func tcpConnection(_ connection: TCPConnection, didReceiveMessage type: NevoMessageType, payload: Data)
}

class TCPConnection {
    weak var delegate: TCPConnectionDelegate?
    private var connection: NWConnection?
    private let queue = DispatchQueue(label: "com.nevo.tcp", qos: .userInitiated)
    private var receiveBuffer = Data()
    private var isConnected = false
    private var reconnectAttempts = 0
    private let maxReconnectAttempts = 5
    private var host: String = ""
    private var port: UInt16 = 0

    var connected: Bool { isConnected }

    func connect(host: String, port: UInt16) {
        self.host = host
        self.port = port
        reconnectAttempts = 0
        establishConnection()
    }

    func disconnect() {
        connection?.cancel()
        connection = nil
        isConnected = false
    }

    func send(type: NevoMessageType, payload: Data) {
        guard let conn = connection, isConnected else { return }
        let typeByte = UInt8(type.rawValue)
        let payloadLen = UInt32(payload.count).littleEndian
        let reserved = UInt32(0).littleEndian
        let headerLen = typeByte + Data(Data(bytes: &payloadLen, count: MemoryLayout<UInt32>.size).reversed()) + Data(Data(bytes: &reserved, count: MemoryLayout<UInt32>.size).reversed())
        var frame = Data()
        frame.append(contentsOf: headerLen)
        frame.append(payload)
        let length = UInt32(frame.count).littleEndian
        var packet = Data()
        packet.append(contentsOf: Data(bytes: &length, count: MemoryLayout<UInt32>.size).reversed())
        packet.append(frame)
        conn.send(content: packet, completion: .contentProcessed { [weak self] error in
            if let error = error { self?.handleError(error) }
        })
    }

    private func establishConnection() {
        let tcpParams = NWProtocolTCP.Options()
        tcpParams.noDelay = true
        tcpParams.keepaliveIdle = 30
        tcpParams.keepaliveCount = 3
        tcpParams.keepaliveInterval = 5
        let params = NWParameters(tls: nil, tcp: tcpParams)
        params.allowLocalEndpointReuse = true
        let endpoint = NWEndpoint.hostPort(host: NWEndpoint.Host(host), port: NWEndpoint.Port(rawValue: port)!)
        connection = NWConnection(to: endpoint, using: params)
        connection?.stateUpdateHandler = { [weak self] state in
            guard let self = self else { return }
            switch state {
            case .ready:
                self.isConnected = true
                self.reconnectAttempts = 0
                self.receiveBuffer.removeAll()
                DispatchQueue.main.async { self.delegate?.tcpConnectionDidConnect(self) }
                self.receiveNext()
            case .failed(let error):
                self.isConnected = false
                if self.reconnectAttempts < self.maxReconnectAttempts {
                    self.reconnectAttempts += 1
                    DispatchQueue.global().asyncAfter(deadline: .now() + 1.0) { self.establishConnection() }
                } else {
                    DispatchQueue.main.async { self.delegate?.tcpConnectionDidDisconnect(self, error: error) }
                }
            case .cancelled:
                self.isConnected = false
            case .waiting(let error):
                DispatchQueue.main.async { self.delegate?.tcpConnectionDidDisconnect(self, error: error) }
            default:
                break
            }
        }
        connection?.start(queue: queue)
    }

    private func receiveNext() {
        connection?.receive(minimumIncompleteLength: 1, maximumLength: 65536) { [weak self] data, _, isComplete, error in
            guard let self = self else { return }
            if let error = error {
                self.handleError(error)
                return
            }
            if isComplete {
                self.isConnected = false
                DispatchQueue.main.async { self.delegate?.tcpConnectionDidDisconnect(self, error: nil) }
                return
            }
            if let data = data { self.processReceivedData(data) }
            if self.isConnected { self.receiveNext() }
        }
    }

    private func processReceivedData(_ data: Data) {
        receiveBuffer.append(data)
        while receiveBuffer.count >= 4 {
            let lengthData = receiveBuffer.prefix(4)
            let length = UInt32(bigEndian: lengthData.withUnsafeBytes { $0.load(as: UInt32.self) })
            guard receiveBuffer.count >= 4 + Int(length) else { break }
            receiveBuffer.removeFirst(4)
            let frame = receiveBuffer.prefix(Int(length))
            receiveBuffer.removeFirst(Int(length))
            guard frame.count >= 9 else { continue }
            let typeByte = frame[0]
            let type = NevoMessageType(rawValue: Int(typeByte)) ?? .serverMessage
            let payload = frame.subdata(in: 9..<frame.count)
            DispatchQueue.main.async { self.delegate?.tcpConnection(self, didReceiveMessage: type, payload: payload) }
        }
    }

    private func handleError(_ error: Error) {
        isConnected = false
        DispatchQueue.main.async { self.delegate?.tcpConnectionDidDisconnect(self, error: error) }
    }
}