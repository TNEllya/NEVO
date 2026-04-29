# Mic Monitor (Local Loopback) Summary

## Problem
The "Test Input" feature only showed a visual level meter (QProgressBar) but did not play back captured microphone audio to the speaker. Users could not hear their mic input during testing.

## Root Cause
No code path existed to route captured microphone audio to the speaker output. The audio pipeline was strictly one-way:
```
Mic -> input_fifo_ -> Opus encode -> Network send (never looped back)
```

## Solution
Added a `monitor_fifo_` SPSC queue that bridges the input callback to the output callback. When monitoring is enabled, captured audio is copied to both `input_fifo_` (for encoding/network) and `monitor_fifo_` (for local playback). The output callback mixes monitor audio into the speaker output.

```
Mic -> maInputCallback -> input_fifo_   -> processEncodeCycle -> Network
                       -> monitor_fifo_ -> maOutputCallback   -> Speaker (when enabled)
```

## Files Modified

| File | Changes |
|---|---|
| `src/core/include/nevo/core/audio/AudioEngine.h` | Added `monitor_fifo_`, `monitor_enabled_`, `monitor_volume_` members; Added `setMonitorEnabled()`, `setMonitorVolume()` public methods |
| `src/core/src/audio/AudioEngine.cpp` | `maInputCallback`: push to `monitor_fifo_` when enabled; `maOutputCallback`: mix monitor audio with output; `setMonitorEnabled/setMonitorVolume` implementations; `shutdown()`: drain `monitor_fifo_` |
| `src/client/include/nevo/client/ClientCore.h` | Added `setMonitorEnabled(bool)`, `setMonitorVolume(float)` declarations |
| `src/client/src/ClientCore.cpp` | Forwarding implementations to `audio_engine_` |
| `src/ui/src/MainWindow.cpp` | `testInputToggled` handler: enable monitoring on activate, disable on deactivate |

## Key Design Decisions
- **Separate monitor FIFO**: Cannot reuse `output_fifo_` due to SPSC single-producer constraint
- **Real-time safe**: All monitor state uses atomics + lock-free FIFO, no mutex in audio callbacks
- **Default monitor volume 0.8**: Avoids excessively loud feedback
- **Drain on disable**: `setMonitorEnabled(false)` clears stale frames from `monitor_fifo_`
- **Clamped mixing**: Output is clamped to [-1.0, 1.0] to prevent clipping artifacts
