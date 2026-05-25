import json
import os
import threading
import numpy as np
import sounddevice as sd

_SETTINGS_DIR = os.path.join(os.path.expanduser("~"), ".nevo")
_SETTINGS_FILE = os.path.join(_SETTINGS_DIR, "audio_settings.json")

_DEFAULT_SETTINGS = {
    "input_device": None,
    "output_device": None,
    "noise_suppression": False,
    "input_gain": 1.0,
    "output_volume": 1.0,
    "input_mode": "continuous",
    "vad_threshold": 0.02,
    "ptt_key": "Ctrl+Space",
}


class InputMode:
    CONTINUOUS = "continuous"
    PTT = "ptt"
    VAD = "vad"

    ALL = [CONTINUOUS, PTT, VAD]

    @staticmethod
    def display_name(mode: str) -> str:
        names = {
            InputMode.CONTINUOUS: "Continuous",
            InputMode.PTT: "Push-to-Talk (PTT)",
            InputMode.VAD: "Voice Activity Detection (VAD)",
        }
        return names.get(mode, mode)


class AudioManager:
    def __init__(self):
        self._settings = dict(_DEFAULT_SETTINGS)
        self._load_settings()
        self._test_input_stream = None
        self._test_output_stream = None
        self._input_level = 0.0
        self._input_level_callback = None
        self._ns_state = None
        self._ns_enabled = False

        self._ptt_active = False
        self._vad_speaking = False
        self._vad_stream = None
        self._vad_running = False
        self._on_vad_changed = None
        self._vad_lock = threading.Lock()

    @property
    def settings(self) -> dict:
        return dict(self._settings)

    @property
    def input_mode(self) -> str:
        return self._settings.get("input_mode", InputMode.CONTINUOUS)

    def set_input_mode(self, mode: str):
        if mode not in InputMode.ALL:
            return
        old_mode = self._settings.get("input_mode", InputMode.CONTINUOUS)
        self._settings["input_mode"] = mode
        self._save_settings()
        if old_mode == InputMode.VAD and mode != InputMode.VAD:
            self.stop_vad()
        if mode == InputMode.VAD and old_mode != InputMode.VAD:
            self.start_vad()

    @property
    def vad_threshold(self) -> float:
        return self._settings.get("vad_threshold", 0.02)

    def set_vad_threshold(self, threshold: float):
        self._settings["vad_threshold"] = max(0.001, min(0.5, threshold))
        self._save_settings()

    @property
    def ptt_key(self) -> str:
        return self._settings.get("ptt_key", "Ctrl+Space")

    def set_ptt_key(self, key: str):
        self._settings["ptt_key"] = key
        self._save_settings()

    @property
    def ptt_active(self) -> bool:
        return self._ptt_active

    def set_ptt_active(self, active: bool):
        self._ptt_active = active

    @property
    def vad_speaking(self) -> bool:
        return self._vad_speaking

    @property
    def on_vad_changed(self):
        return self._on_vad_changed

    @on_vad_changed.setter
    def on_vad_changed(self, callback):
        self._on_vad_changed = callback

    def should_transmit(self) -> bool:
        mode = self.input_mode
        if mode == InputMode.CONTINUOUS:
            return True
        elif mode == InputMode.PTT:
            return self._ptt_active
        elif mode == InputMode.VAD:
            return self._vad_speaking
        return True

    def start_vad(self):
        with self._vad_lock:
            if self._vad_running:
                return
            self._vad_running = True
        try:
            self._vad_stream = sd.InputStream(
                device=self.get_current_input_device(),
                channels=1,
                samplerate=48000,
                blocksize=960,
                dtype="float32",
                callback=self._vad_callback,
            )
            self._vad_stream.start()
        except Exception:
            with self._vad_lock:
                self._vad_running = False

    def stop_vad(self):
        with self._vad_lock:
            self._vad_running = False
        if self._vad_stream is not None:
            try:
                self._vad_stream.stop()
                self._vad_stream.close()
            except Exception:
                pass
            self._vad_stream = None
        self._vad_speaking = False

    def _vad_callback(self, indata, frames, time_info, status):
        with self._vad_lock:
            if not self._vad_running:
                return
        rms = float(np.sqrt(np.mean(indata ** 2)))
        gain = self._settings.get("input_gain", 1.0)
        rms *= gain
        was_speaking = self._vad_speaking
        threshold = self._settings.get("vad_threshold", 0.02)
        self._vad_speaking = rms > threshold
        if self._on_vad_changed and was_speaking != self._vad_speaking:
            try:
                self._on_vad_changed(self._vad_speaking)
            except Exception:
                pass

    @staticmethod
    def _deduplicate_devices(devices: list[dict]) -> list[dict]:
        seen = {}
        for dev in devices:
            name = dev["name"]
            if name not in seen:
                seen[name] = dev
            else:
                existing = seen[name]
                if dev["is_default"] and not existing["is_default"]:
                    seen[name] = dev
        return list(seen.values())

    def enumerate_input_devices(self) -> list[dict]:
        devices = sd.query_devices()
        result = []
        default_input = sd.default.device[0]
        for i, dev in enumerate(devices):
            if dev["max_input_channels"] > 0:
                result.append({
                    "index": i,
                    "name": dev["name"],
                    "sample_rate": int(dev["default_samplerate"]),
                    "channels": dev["max_input_channels"],
                    "is_default": (i == default_input),
                })
        return self._deduplicate_devices(result)

    def enumerate_output_devices(self) -> list[dict]:
        devices = sd.query_devices()
        result = []
        default_output = sd.default.device[1]
        for i, dev in enumerate(devices):
            if dev["max_output_channels"] > 0:
                result.append({
                    "index": i,
                    "name": dev["name"],
                    "sample_rate": int(dev["default_samplerate"]),
                    "channels": dev["max_output_channels"],
                    "is_default": (i == default_output),
                })
        return self._deduplicate_devices(result)

    def set_input_device(self, device_index: int):
        self._settings["input_device"] = device_index
        sd.default.device[0] = device_index
        self._save_settings()

    def set_output_device(self, device_index: int):
        self._settings["output_device"] = device_index
        sd.default.device[1] = device_index
        self._save_settings()

    def get_current_input_device(self) -> int:
        return sd.default.device[0]

    def get_current_output_device(self) -> int:
        return sd.default.device[1]

    def set_noise_suppression(self, enabled: bool):
        self._settings["noise_suppression"] = enabled
        self._ns_enabled = enabled
        if enabled and self._ns_state is None:
            try:
                from noisereduce import generate_noise_profile
                self._ns_state = "ready"
            except ImportError:
                self._ns_state = None
                self._ns_enabled = False
                self._settings["noise_suppression"] = False
        self._save_settings()

    @property
    def noise_suppression_enabled(self) -> bool:
        return self._ns_enabled

    def set_input_gain(self, gain: float):
        self._settings["input_gain"] = max(0.0, min(2.0, gain))
        self._save_settings()

    def set_output_volume(self, volume: float):
        self._settings["output_volume"] = max(0.0, min(1.0, volume))
        self._save_settings()

    def apply_noise_suppression(self, audio_data: np.ndarray, sample_rate: int) -> np.ndarray:
        if not self._ns_enabled:
            return audio_data
        try:
            from noisereduce import reduce_noise
            return reduce_noise(y=audio_data, sr=sample_rate, prop_decrease=0.8)
        except Exception:
            return audio_data

    def start_input_test(self, level_callback=None):
        self._input_level_callback = level_callback
        try:
            self._test_input_stream = sd.InputStream(
                device=self.get_current_input_device(),
                channels=1,
                samplerate=48000,
                blocksize=960,
                dtype="float32",
                callback=self._input_test_callback,
            )
            self._test_input_stream.start()
        except Exception:
            self._test_input_stream = None

    def stop_input_test(self):
        if self._test_input_stream is not None:
            try:
                self._test_input_stream.stop()
                self._test_input_stream.close()
            except Exception:
                pass
            self._test_input_stream = None
        self._input_level = 0.0

    def _input_test_callback(self, indata, frames, time_info, status):
        peak = float(np.max(np.abs(indata)))
        self._input_level = peak
        if self._input_level_callback:
            self._input_level_callback(peak)

    def get_input_level(self) -> float:
        return self._input_level

    def start_output_test(self):
        try:
            duration = 2.0
            sample_rate = 48000
            t = np.linspace(0, duration, int(sample_rate * duration), endpoint=False)
            tone = (0.3 * np.sin(2 * np.pi * 440.0 * t)).astype(np.float32)
            fade_samples = int(0.05 * sample_rate)
            tone[:fade_samples] *= np.linspace(0, 1, fade_samples, dtype=np.float32)
            tone[-fade_samples:] *= np.linspace(1, 0, fade_samples, dtype=np.float32)
            self._test_output_stream = sd.OutputStream(
                device=self.get_current_output_device(),
                channels=1,
                samplerate=sample_rate,
                dtype="float32",
            )
            self._test_output_stream.start()
            self._test_output_stream.write(tone.reshape(-1, 1))
        except Exception:
            pass

    def stop_output_test(self):
        if self._test_output_stream is not None:
            try:
                self._test_output_stream.stop()
                self._test_output_stream.close()
            except Exception:
                pass
            self._test_output_stream = None

    def _load_settings(self):
        try:
            if os.path.exists(_SETTINGS_FILE):
                with open(_SETTINGS_FILE, "r", encoding="utf-8") as f:
                    saved = json.load(f)
                for key in _DEFAULT_SETTINGS:
                    if key in saved:
                        self._settings[key] = saved[key]
                if self._settings["input_device"] is not None:
                    sd.default.device[0] = self._settings["input_device"]
                if self._settings["output_device"] is not None:
                    sd.default.device[1] = self._settings["output_device"]
                self._ns_enabled = self._settings.get("noise_suppression", False)
        except Exception:
            pass

    def _save_settings(self):
        try:
            os.makedirs(_SETTINGS_DIR, exist_ok=True)
            with open(_SETTINGS_FILE, "w", encoding="utf-8") as f:
                json.dump(self._settings, f, indent=2)
        except Exception:
            pass
