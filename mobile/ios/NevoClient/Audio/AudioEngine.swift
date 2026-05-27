import Foundation
import AVFoundation

protocol AudioEngineDelegate: AnyObject {
    func audioEngine(_ engine: AudioEngine, didCaptureEncodedFrame data: Data)
    func audioEngine(_ engine: AudioEngine, didDetectVoiceActivity speaking: Bool)
}

class AudioEngine {
    weak var delegate: AudioEngineDelegate?

    private let audioEngine = AVAudioEngine()
    private let inputNode: AVAudioInputNode
    private let outputNode = AVAudioMixerNode()
    private let outputFormat = AVAudioFormat(standardFormatWithSampleRate: 48000, channels: 1)!

    private var isRunning = false
    private var isMuted = false
    private var isDeafened = false
    private var inputMode: InputMode = .continuous
    private var pttActive = false
    private var vadThreshold: Float = 0.015
    private var shouldTransmit: Bool { !isMuted && (inputMode == .continuous || (inputMode == .ptt && pttActive)) }

    private let opusFrameSize = 960
    private let opusMaxPacketSize = 4000

    private var opusEncoder: OpusCodec?
    private var opusDecoder: OpusCodec?
    private var jitterBuffer = RingBuffer<Data>(capacity: 20)
    private var jitterDecodeQueue = DispatchQueue(label: "com.nevo.jitter", qos: .userInitiated)

    var micVolume: Float = 1.0 { didSet { updateAudioPipeline() } }
    var outputVolume: Float = 0.5 { didSet { updateAudioPipeline() } }

    init() {
        inputNode = audioEngine.inputNode
        audioEngine.attach(outputNode)
    }

    func setup() throws {
        let session = AVAudioSession.sharedInstance()
        try session.setCategory(.playAndRecord, options: [.allowBluetooth, .allowBluetoothA2DP, .defaultToSpeaker])
        try session.setPreferredSampleRate(48000)
        try session.setPreferredIOBufferDuration(0.02)
        try session.setActive(true)

        opusEncoder = OpusCodec(sampleRate: 48000, channels: 1, application: .voip)
        opusDecoder = OpusCodec(sampleRate: 48000, channels: 1, application: .voip)

        let inputFormat = inputNode.outputFormat(forBus: 0)
        audioEngine.connect(audioEngine.mainMixerNode, to: outputNode, format: inputFormat)
        audioEngine.connect(outputNode, to: audioEngine.outputNode, format: inputFormat)
    }

    func start() throws {
        guard !isRunning else { return }
        let inputFormat = inputNode.outputFormat(forBus: 0)
        guard let resampleFormat = AVAudioFormat(standardFormatWithSampleRate: 48000, channels: 1) else {
            throw NSError(domain: "NevoAudio", code: -1, userInfo: [NSLocalizedDescriptionKey: "Failed to create resample format"])
        }
        let converter = AVAudioConverter(from: inputFormat, to: resampleFormat)!

        inputNode.installTap(onBus: 0, bufferSize: 960, format: inputFormat) { [weak self] buffer, _ in
            guard let self = self else { return }
            let frameCount = AVAudioFrameCount(Double(buffer.frameLength) * 48000 / inputFormat.sampleRate)
            guard let resampledBuffer = AVAudioPCMBuffer(pcmFormat: resampleFormat, frameCapacity: frameCount) else { return }
            var error: NSError?
            let inputBlock: AVAudioConverterInputBlock = { _, outStatus in
                outStatus.pointee = .haveData
                return buffer
            }
            converter.convert(to: resampledBuffer, error: &error, withInputFrom: inputBlock)
            if let error = error { return print("[Audio] Resample error: \(error)") }

            guard let channelData = resampledBuffer.floatChannelData else { return }
            let samples = channelData[0]
            let count = Int(resampledBuffer.frameLength)

            var rms: Float = 0
            var vadActive = false
            if let encoded = self.opusEncoder?.encode(floatSamples: samples, frameCount: count) {
                for i in 0..<count {
                    var s = samples[i]
                    if self.isMuted { s *= 0 }
                    s *= self.micVolume
                    rms += s * s
                }
                rms = sqrt(rms / Float(count))
                vadActive = rms > self.vadThreshold
                if self.shouldTransmit {
                    DispatchQueue.main.async { self.delegate?.audioEngine(self, didCaptureEncodedFrame: encoded) }
                }
            }
            DispatchQueue.main.async { self.delegate?.audioEngine(self, didDetectVoiceActivity: vadActive) }
        }

        audioEngine.prepare()
        try audioEngine.start()
        isRunning = true

        startJitterPlayback()
    }

    func stop() {
        inputNode.removeTap(onBus: 0)
        audioEngine.stop()
        isRunning = false
    }

    func receiveEncodedFrame(_ data: Data) {
        jitterBuffer.push(data)
    }

    func setMuted(_ muted: Bool) { isMuted = muted }
    func setDeafened(_ deafened: Bool) { isDeafened = deafened }
    func setInputMode(_ mode: InputMode) { inputMode = mode }
    func setPtt(_ active: Bool) { pttActive = active }
    func setVadThreshold(_ threshold: Float) { vadThreshold = threshold }

    private func startJitterPlayback() {
        let audioMixerNode = AVAudioMixerNode()
        audioEngine.attach(audioMixerNode)
        audioEngine.connect(audioMixerNode, to: outputNode, format: outputFormat)

        audioMixerNode.installTap(onBus: 0, bufferSize: 960, format: outputFormat) { [weak self] buffer, _ in
            guard let self = self, let channelData = buffer.floatChannelData else { return }
            let count = Int(buffer.frameLength)
            memset(channelData[0], 0, count * MemoryLayout<Float>.size)

            let decoded = self.jitterBuffer.pop()
            if let decoded = decoded, let pcm = self.opusDecoder?.decode(data: decoded, frameCount: self.opusFrameSize) {
                for i in 0..<min(pcm.count, count) {
                    channelData[0][i] = pcm[i] * self.outputVolume
                }
            }
        }
    }

    private func updateAudioPipeline() {
        outputNode.outputVolume = outputVolume
    }

    func cleanup() {
        stop()
        opusEncoder = nil
        opusDecoder = nil
    }
}

struct RingBuffer<T> {
    private let queue = DispatchQueue(label: "com.nevo.ringbuffer", attributes: .concurrent)
    private var buffer: [T] = []
    private let capacity: Int

    init(capacity: Int) { self.capacity = capacity }

    mutating func push(_ item: T) {
        queue.async(flags: .barrier) {
            if self.buffer.count >= self.capacity { self.buffer.removeFirst() }
            self.buffer.append(item)
        }
    }

    func pop() -> T? {
        var result: T?
        queue.sync { if !buffer.isEmpty { result = buffer.removeFirst() } }
        return result
    }
}

class OpusCodec {
    enum Application { case voip; case audio; case lowDelay }

    private let sampleRate: Int32
    private let channels: Int32
    private var encoder: OpaquePointer?
    private var decoder: OpaquePointer?
    private let maxFrameSize: Int

    init(sampleRate: Int32, channels: Int32, application: Application = .voip) {
        self.sampleRate = sampleRate
        self.channels = channels
        self.maxFrameSize = Int(sampleRate) * 120 / 1000

        var encError: Int32 = 0
        encoder = opus_encoder_create(sampleRate, channels, OPUS_APPLICATION_VOIP, &encError)

        var decError: Int32 = 0
        decoder = opus_decoder_create(sampleRate, channels, &decError)
    }

    deinit {
        if let enc = encoder { opus_encoder_destroy(enc) }
        if let dec = decoder { opus_decoder_destroy(dec) }
    }

    func encode(pcmData: Data, frameCount: Int) -> Data? {
        guard let encoder = encoder, frameCount > 0 else { return nil }
        var output = Data(count: 4000)
        let written = output.withUnsafeMutableBytes { buf -> Int32 in
            pcmData.withUnsafeBytes { pcm in
                opus_encode(encoder,
                           pcm.bindMemory(to: opus_int16.self).baseAddress,
                           Int32(frameCount),
                           buf.bindMemory(to: UInt8.self).baseAddress,
                           Int32(output.count))
            }
        }
        guard written > 0 else { return nil }
        return output.prefix(Int(written))
    }

    func encode(floatSamples: UnsafeMutablePointer<Float>, frameCount: Int) -> Data? {
        guard let encoder = encoder, frameCount > 0 else { return nil }
        var pcm16 = [opus_int16](repeating: 0, count: frameCount)
        for i in 0..<frameCount {
            let sample = floatSamples[i] * 32767.0
            pcm16[i] = opus_int16(max(-32768, min(32767, Int32(sample))))
        }
        var output = Data(count: 4000)
        let written = output.withUnsafeMutableBytes { buf -> Int32 in
            opus_encode(encoder, pcm16, Int32(frameCount),
                       buf.bindMemory(to: UInt8.self).baseAddress, Int32(output.count))
        }
        guard written > 0 else { return nil }
        return output.prefix(Int(written))
    }

    func decode(data: Data, frameCount: Int) -> [Float]? {
        guard let decoder = decoder, !data.isEmpty else { return nil }
        var pcm16 = [opus_int16](repeating: 0, count: maxFrameSize * Int(channels))
        let decoded = data.withUnsafeBytes { buf -> Int32 in
            opus_decode(decoder,
                       buf.bindMemory(to: UInt8.self).baseAddress,
                       Int32(data.count),
                       &pcm16,
                       Int32(maxFrameSize),
                       0)
        }
        guard decoded > 0 else { return nil }
        return (0..<Int(decoded)).map { Float(pcm16[$0]) / 32767.0 }
    }
}