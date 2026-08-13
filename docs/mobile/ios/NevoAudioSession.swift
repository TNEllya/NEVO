/**
 * NevoAudioSession.swift
 * NEVO VoIP - iOS Audio Session Configuration
 *
 * This file manages the AVAudioSession configuration for the NEVO VoIP app,
 * ensuring proper audio routing, Bluetooth support, and sample rate handling.
 *
 * Key responsibilities:
 *   1. Configure AVAudioSession for simultaneous recording and playback
 *   2. Set voice chat mode for optimized audio processing
 *   3. Handle Bluetooth audio route changes
 *   4. Notify the native audio engine of sample rate changes
 *
 * Audio Session Configuration:
 *   - Category: .playAndRecord (simultaneous input and output)
 *   - Mode: .voiceChat (optimized for VoIP: echo cancellation, noise suppression)
 *   - Options: .allowBluetooth, .allowBluetoothA2DP (Bluetooth headset support)
 *   - Preferred sample rate: 48000 Hz (Opus native rate)
 *   - Preferred IO buffer duration: 0.02s (20ms, matching Opus frame size)
 *
 * Bluetooth Route Changes:
 *   When a Bluetooth device is connected or disconnected, the audio route
 *   changes. This can affect the sample rate (e.g., Bluetooth HFP uses
 *   16kHz, A2DP uses 44.1kHz). The AudioEngineBridge is notified so the
 *   native C++ AudioEngine can reconfigure its Resampler.
 *
 * Integration with Native Code:
 *   AudioEngineBridge is a Swift-accessible bridge that calls into the
 *   C++ AudioEngine::onDeviceSampleRateChanged() method. This allows
 *   the iOS audio session to notify the native audio pipeline of
 *   hardware configuration changes.
 */

import Foundation
import AVFoundation

/**
 * Bridge protocol for communicating audio session changes to the native C++ layer.
 *
 * The native C++ AudioEngine implements this protocol (via Objective-C++ wrapper)
 * to receive notifications when the audio hardware configuration changes,
 * particularly sample rate changes caused by Bluetooth route switches.
 *
 * Example scenario:
 *   1. User connects Bluetooth headset (HFP, 16kHz)
 *   2. AVAudioSession changes sample rate to 16000
 *   3. NevoAudioSession detects this via route change notification
 *   4. Calls AudioEngineBridge.onSampleRateChanged(newRate: 16000)
 *   5. Native AudioEngine reconfigures Resampler: 16kHz -> 48kHz
 */
@objc protocol AudioEngineBridge: AnyObject {
    /**
     * Called when the audio hardware sample rate changes.
     *
     * The native AudioEngine should reconfigure its Resampler to
     * convert between the new device sample rate and Opus's 48kHz.
     *
     * @param newSampleRate The new hardware sample rate in Hz
     */
    func onSampleRateChanged(_ newSampleRate: Double)
}

/**
 * Manages the AVAudioSession configuration for NEVO VoIP.
 *
 * Configures the audio session for VoIP operation, handles Bluetooth
 * route changes, and notifies the native audio engine of hardware
 * configuration changes.
 *
 * Usage:
 *   let audioSession = NevoAudioSession()
 *   audioSession.configure()
 *   audioSession.setBridge(nativeEngineBridge)
 *
 * The audio session remains active for the lifetime of the app.
 * Bluetooth route changes are automatically handled via AVAudioSession
 * notifications.
 */
class NevoAudioSession {

    // ================================================================
    // Constants
    // ================================================================

    /// Preferred sample rate for VoIP (Opus native rate: 48kHz)
    private let preferredSampleRate: Double = 48000.0

    /// Preferred IO buffer duration (20ms, matching Opus frame size)
    private let preferredIOBufferDuration: TimeInterval = 0.02

    // ================================================================
    // Properties
    // ================================================================

    /// Reference to the native audio engine bridge for sample rate change notifications
    private weak var bridge: AudioEngineBridge?

    /// The shared AVAudioSession instance
    private let audioSession = AVAudioSession.sharedInstance()

    // ================================================================
    // Initialization
    // ================================================================

    init() {
        // Register for audio route change notifications
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(handleRouteChange(_:)),
            name: AVAudioSession.routeChangeNotification,
            object: audioSession
        )

        // Register for media services reset notification
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(handleMediaServicesReset),
            name: AVAudioSession.mediaServicesWereResetNotification,
            object: audioSession
        )
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
    }

    // ================================================================
    // Public Methods
    // ================================================================

    /**
     * Configure the AVAudioSession for VoIP operation.
     *
     * Sets up the audio session with:
     *   - .playAndRecord category (simultaneous mic + speaker)
     *   - .voiceChat mode (echo cancellation, noise suppression)
     *   - Bluetooth support (.allowBluetooth, .allowBluetoothA2DP)
     *   - Preferred sample rate of 48kHz
     *   - Preferred IO buffer duration of 20ms
     *
     * This method should be called once during app startup,
     * before any audio I/O begins.
     *
     * @throws AVAudioSession configuration errors
     */
    func configure() throws {
        do {
            // Set category: playAndRecord for simultaneous input/output
            try audioSession.setCategory(
                .playAndRecord,
                mode: .voiceChat,
                options: [.allowBluetooth, .allowBluetoothA2DP]
            )

            // Set preferred sample rate to match Opus (48kHz)
            try audioSession.setPreferredSampleRate(preferredSampleRate)

            // Set preferred IO buffer duration (20ms = one Opus frame)
            try audioSession.setPreferredIOBufferDuration(preferredIOBufferDuration)

            // Activate the audio session
            try audioSession.setActive(true)

            print("[NevoAudioSession] Configured successfully. " +
                  "Sample rate: \(audioSession.sampleRate) Hz, " +
                  "IO buffer duration: \(audioSession.ioBufferDuration) s, " +
                  "Current route: \(audioSession.currentRoute)")

            // Notify bridge of the current sample rate
            notifySampleRateChange()

        } catch let error as AVAudioSession.Error {
            print("[NevoAudioSession] Configuration error: \(error.localizedDescription)")
            throw error
        }
    }

    /**
     * Set the native audio engine bridge for sample rate change notifications.
     *
     * The bridge receives callbacks when the audio hardware sample rate
     * changes, allowing the native AudioEngine to reconfigure its Resampler.
     *
     * @param bridge The native audio engine bridge implementation
     */
    func setBridge(_ bridge: AudioEngineBridge) {
        self.bridge = bridge
    }

    /**
     * Deactivate the audio session when the VoIP call ends.
     *
     * This releases the audio hardware resources and allows
     * other apps to use the audio session.
     *
     * @param notifyOthers If true, other apps are notified that the
     *                     audio session has been deactivated
     */
    func deactivate(notifyOthers: Bool = true) {
        do {
            let options: AVAudioSession.SetActiveOptions =
                notifyOthers ? .notifyOthersOnDeactivation : []
            try audioSession.setActive(false, options: options)
            print("[NevoAudioSession] Deactivated")
        } catch {
            print("[NevoAudioSession] Deactivation error: \(error.localizedDescription)")
        }
    }

    // ================================================================
    // Notification Handlers
    // ================================================================

    /**
     * Handle audio route change notifications.
     *
     * Route changes occur when:
     *   - Bluetooth headset is connected or disconnected
     *   - Wired headset is plugged in or unplugged
     *   - User switches audio output via Control Center
     *
     * When a route change occurs, the hardware sample rate may change
     * (e.g., Bluetooth HFP uses 16kHz). We detect this and notify
     * the native AudioEngine via the bridge.
     *
     * @param notification The route change notification
     */
    @objc private func handleRouteChange(_ notification: Notification) {
        guard let userInfo = notification.userInfo,
              let reasonValue = userInfo[AVAudioSessionRouteChangeReasonKey] as? UInt,
              let reason = AVAudioSession.RouteChangeReason(rawValue: reasonValue) else {
            return
        }

        let currentSampleRate = audioSession.sampleRate
        let currentRoute = audioSession.currentRoute

        switch reason {
        case .newDeviceAvailable:
            print("[NevoAudioSession] New audio device available. " +
                  "Sample rate: \(currentSampleRate) Hz, Route: \(currentRoute)")

        case .oldDeviceUnavailable:
            print("[NevoAudioSession] Audio device removed. " +
                  "Sample rate: \(currentSampleRate) Hz, Route: \(currentRoute)")

        case .routeConfigurationChange:
            print("[NevoAudioSession] Route configuration changed. " +
                  "Sample rate: \(currentSampleRate) Hz")

        case .override:
            print("[NevoAudioSession] Route override. " +
                  "Sample rate: \(currentSampleRate) Hz")

        default:
            print("[NevoAudioSession] Route change reason: \(reason). " +
                  "Sample rate: \(currentSampleRate) Hz")
        }

        // Check if sample rate changed and notify bridge
        notifySampleRateChange()
    }

    /**
     * Handle media services reset notification.
     *
     * When media services are reset (rare, but can happen),
     * reconfigure the audio session.
     */
    @objc private func handleMediaServicesReset() {
        print("[NevoAudioSession] Media services were reset. Reconfiguring...")

        do {
            try configure()
        } catch {
            print("[NevoAudioSession] Reconfiguration failed after media services reset: " +
                  "\(error.localizedDescription)")
        }
    }

    // ================================================================
    // Private Helpers
    // ================================================================

    /**
     * Notify the native audio engine bridge of the current sample rate.
     *
     * Called after configuration and whenever a route change may have
     * caused the sample rate to change.
     */
    private func notifySampleRateChange() {
        let currentSampleRate = audioSession.sampleRate

        // Only notify if the sample rate is different from what we prefer
        // (the bridge already knows about 48kHz from initial configuration)
        bridge?.onSampleRateChanged(currentSampleRate)

        if currentSampleRate != preferredSampleRate {
            print("[NevoAudioSession] Sample rate differs from preferred: " +
                  "current=\(currentSampleRate) Hz, preferred=\(preferredSampleRate) Hz. " +
                  "Native Resampler will handle conversion.")
        }
    }
}
