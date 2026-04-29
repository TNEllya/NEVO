# Mic Monitor (Local Loopback) Feature

## Problem

The "Test Input" feature in `AudioSettingsWidget` only drives a visual level meter (QProgressBar). There is **no code** that routes captured microphone audio back to the speaker output. The user cannot hear their mic input during testing.

## Audio Pipeline Analysis

```
Mic -> maInputCallback -> input_fifo_ -> processEncodeCycle -> Opus -> Network
                                                                    (never looped back)

Remote Users -> queueAudioData -> JitterBuffer -> processMixCycle -> output_fifo_ -> maOutputCallback -> Speaker

Test Tone -> playTestTone -> output_fifo_ -> Speaker (works!)
```

`output_fifo_` is only written by `processMixCycle()` and `playTestTone()`. No local mic audio ever reaches it.

## Solution: Monitor FIFO in AudioEngine

Add a dedicated `monitor_fifo_` (SPSC queue) that bridges the input callback to the output callback. When monitoring is enabled, captured audio is copied to both `input_fifo_` and `monitor_fifo_`. The output callback mixes monitor audio into the speaker output.

### Architecture

```
Mic -> maInputCallback -> input_fifo_   -> processEncodeCycle -> Network
                       -> monitor_fifo_ -> maOutputCallback   -> Speaker (when enabled)
```

### Key Design Decisions

1. **Separate monitor FIFO**: Cannot reuse `output_fifo_` (SPSC invariant — single producer). `monitor_fifo_` has its own producer (input callback) and consumer (output callback).

2. **Monitor mixing in output callback**: Lowest possible latency. The output callback pops from both `output_fifo_` and `monitor_fifo_`, mixes them with respective volumes, and writes to the speaker.

3. **Atomic flags for real-time safety**: `monitor_enabled_` (atomic bool) and `monitor_volume_` (atomic float) are read by the real-time callbacks without locks.

4. **Monitor volume**: Default 0.8 to avoid loud feedback. Controlled via `setMonitorVolume()`.

5. **Resampling**: If input and output devices have different sample rates, the monitor audio needs resampling. The input callback captures at the input device rate, but the output callback expects the output device rate. For simplicity, the captured frame is pushed at the input rate, and resampling is done in the output callback if rates differ. However, in the common case (both 48kHz), no resampling is needed.

   **Simplified approach**: Since both input and output typically run at the same rate on most systems, and for monitoring purposes a slight rate mismatch is tolerable, we push the gain-applied PCM directly into `monitor_fifo_` and mix it in the output callback without additional resampling. If the rates differ, the monitor audio may have pitch/speed artifacts, but this is acceptable for a test feature.

## Affected Files

| File | Modification Type | Details |
|---|---|---|
| `src/core/include/nevo/core/audio/AudioEngine.h` | Add members | `monitor_fifo_`, `monitor_enabled_`, `monitor_volume_`; Add public methods: `setMonitorEnabled()`, `setMonitorVolume()` |
| `src/core/src/audio/AudioEngine.cpp` | Modify | `maInputCallback`: push to `monitor_fifo_` when enabled; `maOutputCallback`: mix monitor audio; `shutdown()`: drain `monitor_fifo_` |
| `src/client/include/nevo/client/ClientCore.h` | Add methods | `setMonitorEnabled(bool)`, `setMonitorVolume(float)` forwarding to AudioEngine |
| `src/client/src/ClientCore.cpp` | Add method impls | Forward calls to `audio_engine_` |
| `src/ui/src/MainWindow.cpp` | Modify | In `testInputToggled` handler, enable monitoring when active, disable when inactive |

## Implementation Details

### AudioEngine.h Additions

```cpp
// Public methods
void setMonitorEnabled(bool enabled);
void setMonitorVolume(float volume);

// Private data members
std::atomic<bool> monitor_enabled_{false};
std::atomic<float> monitor_volume_{0.8f};

#ifdef NEVO_HAS_BOOST
boost::lockfree::spsc_queue<AudioFrame, boost::lockfree::capacity<kFifoCapacity>> monitor_fifo_;
#else
std::mutex monitor_fifo_mutex_;
std::deque<AudioFrame> monitor_fifo_;
#endif
```

### maInputCallback Modification

After pushing to `input_fifo_`, conditionally push to `monitor_fifo_`:

```cpp
// Step 3 (existing): push to input_fifo_
if (!self->input_fifo_.push(frame)) { /* drop */ }

// Step 4 (new): push to monitor_fifo_ when monitoring is enabled
if (self->monitor_enabled_.load(std::memory_order_relaxed)) {
    self->monitor_fifo_.push(frame);  // drop if full, acceptable
}
```

### maOutputCallback Modification

After getting the mixed frame from `output_fifo_`, mix in monitor audio:

```cpp
// Existing: get frame from output_fifo_
AudioFrame frame{};
const bool has_frame = self->output_fifo_.pop(frame);

// New: get monitor frame
AudioFrame monitor_frame{};
const bool has_monitor = self->monitor_fifo_.pop(monitor_frame);

if (has_frame || has_monitor) {
    const float volume = self->output_volume_.load(std::memory_order_relaxed);
    const float mon_vol = self->monitor_volume_.load(std::memory_order_relaxed);

    for (uint32_t i = 0; i < samples_to_write; ++i) {
        float sample = 0.0f;
        if (has_frame) sample += frame[i] * volume;
        if (has_monitor) sample += monitor_frame[i] * mon_vol;
        pcm_output[i] = std::clamp(sample, -1.0f, 1.0f);
    }
} else {
    std::memset(pcm_output, 0, frame_count * sizeof(float));
}
```

### ClientCore Forwarding

```cpp
void ClientCore::setMonitorEnabled(bool enabled) {
    if (audio_engine_) {
        audio_engine_->setMonitorEnabled(enabled);
    }
}

void ClientCore::setMonitorVolume(float volume) {
    if (audio_engine_) {
        audio_engine_->setMonitorVolume(volume);
    }
}
```

### MainWindow testInputToggled Handler

```cpp
connect(settings, &AudioSettingsWidget::testInputToggled,
        this, [this, settings](bool active) {
    if (!client_core_) return;
    if (active) {
        client_core_->setInputLevelCallback([settings](float level) {
            QMetaObject::invokeMethod(settings, [settings, level]() {
                settings->inputLevelBar()->setValue(
                    static_cast<int>(level * 100.0f));
            }, Qt::QueuedConnection);
        });
        client_core_->setMonitorEnabled(true);
    } else {
        client_core_->setInputLevelCallback(nullptr);
        client_core_->setMonitorEnabled(false);
    }
});
```

## Boundary Conditions

- **Feedback loop**: If the mic picks up speaker output during monitoring, audio feedback can occur. This is inherent to monitoring and acceptable for a test feature. Users should use headphones.
- **FIFO overflow**: If `monitor_fifo_` is full (output callback is slow), monitor frames are silently dropped. Acceptable — glitch is better than latency buildup.
- **FIFO underflow**: If no monitor frame is available, only the mixed remote audio is played. No issue.
- **Rate mismatch**: If input/output sample rates differ, monitor audio may have pitch artifacts. Acceptable for a test feature.
- **Thread safety**: All monitor state is accessed via atomics or lock-free FIFOs. No mutex in real-time callbacks.

## Expected Outcome

When the user clicks "Test Input" in AudioSettingsWidget, they will hear their microphone audio through the speaker/headphones in addition to seeing the level meter. When they click "Stop Test", monitoring stops immediately.
