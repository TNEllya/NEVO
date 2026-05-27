import Foundation
import Network

protocol UDPVoiceSocketDelegate: AnyObject {
    func udpVoiceSocket(_ socket: UDPVoiceSocket, didReceiveEncryptedData data: Data, from endpoint: NWEndpoint)
    func udpVoiceSocketDidBecomeReady(_ socket: UDPVoiceSocket)
    func udpVoiceSocket(_ socket: UDPVoiceSocket, didEncounterError error: Error)
}

class UDPVoiceSocket {
    weak var delegate: UDPVoiceSocketDelegate?
    private var connection: NWConnection?
    private let queue = DispatchQueue(label: "com.nevo.udp", qos: .userInitiated)
    private(set) var isReady = false
    private var host: String = ""
    private var port: UInt16 = 0

    func bind(port: UInt16) {
        let udpParams = NWProtocolUDP.Options()
        let params = NWParameters(dtls: nil, udp: udpParams)
        params.allowLocalEndpointReuse = true
        let endpoint = NWEndpoint.hostPort(host: "0.0.0.0", port: NWEndpoint.Port(rawValue: port)!)
        connection = NWConnection(to: endpoint, using: params)
        connection?.stateUpdateHandler = { [weak self] state in
            guard let self = self else { return }
            switch state {
            case .ready:
                self.isReady = true
                DispatchQueue.main.async { self.delegate?.udpVoiceSocketDidBecomeReady(self) }
                self.receiveNext()
            case .failed(let error):
                DispatchQueue.main.async { self.delegate?.udpVoiceSocket(self, didEncounterError: error) }
            default:
                break
            }
        }
        connection?.start(queue: queue)
    }

    func connect(host: String, port: UInt16) {
        self.host = host
        self.port = port
    }

    func send(data: Data, to host: String, port: UInt16) {
        guard let conn = connection, isReady else { return }
        let endpoint = NWEndpoint.hostPort(host: NWEndpoint.Host(host), port: NWEndpoint.Port(rawValue: port)!)
        conn.send(content: data, to: endpoint, completion: .contentProcessed { _ in })
    }

    func sendToServer(_ data: Data) {
        guard !host.isEmpty, port != 0 else { return }
        send(data: data, to: host, port: port)
    }

    func close() {
        connection?.cancel()
        connection = nil
        isReady = false
    }

    private func receiveNext() {
        connection?.receiveMessage { [weak self] data, _, _, error in
            guard let self = self else { return }
            if let error = error {
                DispatchQueue.main.async { self.delegate?.udpVoiceSocket(self, didEncounterError: error) }
                return
            }
            if let data = data {
                DispatchQueue.main.async { self.delegate?.udpVoiceSocket(self, didReceiveEncryptedData: data, from: NWEndpoint.hostPort(host: "0.0.0.0", port: 0)) }
            }
            if self.isReady { self.receiveNext() }
        }
    }
}