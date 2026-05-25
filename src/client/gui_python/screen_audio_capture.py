import threading
import numpy as np

try:
    import sounddevice as sd
    HAS_SD = True
except ImportError:
    HAS_SD = False

AUDIO_NONE = 0
AUDIO_SYSTEM = 1
AUDIO_APPLICATION = 2

SAMPLE_RATE = 48000
CHANNELS = 2
FRAME_SIZE = 960


class ScreenAudioCapture:
    def __init__(self):
        self._stream = None
        self._running = False
        self._audio_source = AUDIO_NONE
        self._on_audio_data = None

    def start(self, audio_source, on_audio_data=None):
        if not HAS_SD or audio_source == AUDIO_NONE:
            return False
        self._audio_source = audio_source
        self._on_audio_data = on_audio_data
        self._running = True

        try:
            device = self._find_loopback_device()
            if device is None:
                self._running = False
                return False

            def callback(indata, frames, time_info, status):
                if self._running and self._on_audio_data:
                    self._on_audio_data(indata.copy())

            self._stream = sd.InputStream(
                device=device,
                channels=CHANNELS,
                samplerate=SAMPLE_RATE,
                blocksize=FRAME_SIZE,
                dtype="float32",
                latency="low",
                callback=callback,
            )
            self._stream.start()
            return True
        except Exception:
            self._running = False
            return False

    def stop(self):
        self._running = False
        if self._stream is not None:
            try:
                self._stream.stop()
                self._stream.close()
            except Exception:
                pass
            self._stream = None

    def _find_loopback_device(self):
        if not HAS_SD:
            return None
        devices = sd.query_devices()
        for i, dev in enumerate(devices):
            name = dev.get("name", "").lower()
            if "loopback" in name and dev.get("max_input_channels", 0) > 0:
                return i
        for i, dev in enumerate(devices):
            name = dev.get("name", "").lower()
            if "stereo mix" in name and dev.get("max_input_channels", 0) > 0:
                return i
        return None

    @property
    def running(self):
        return self._running
