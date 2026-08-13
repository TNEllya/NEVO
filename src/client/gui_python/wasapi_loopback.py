"""Windows WASAPI loopback audio capture (no virtual cable needed).

Uses Windows Core Audio API via ctypes to capture system audio output
in shared mode with AUDCLNT_STREAMFLAGS_LOOPBACK. Falls back gracefully
on non-Windows platforms or when COM/WASAPI is unavailable.
"""
from __future__ import annotations

import ctypes
import logging
import threading
import time
from typing import Callable, Optional

import numpy as np

log = logging.getLogger("wasapi_loopback")

# ---------------------------------------------------------------------------
# Windows-only: import succeeds but capture only works on Windows
# ---------------------------------------------------------------------------
if ctypes.windll is None:
    _IS_WINDOWS = False
else:
    _IS_WINDOWS = True


class _GUID(ctypes.Structure):
    _fields_ = [
        ("Data1", ctypes.c_uint32),
        ("Data2", ctypes.c_uint16),
        ("Data3", ctypes.c_uint16),
        ("Data4", ctypes.c_ubyte * 8),
    ]

    def __init__(self, guid_string: Optional[str] = None):
        super().__init__()
        if guid_string:
            import uuid

            u = uuid.UUID(guid_string)
            self.Data1 = u.time_low
            self.Data2 = u.time_mid
            self.Data3 = u.time_hi_version
            for i in range(8):
                self.Data4[i] = u.bytes_le[8 + i]


_CLSID_MMDeviceEnumerator = _GUID("{BCDE0395-E52F-467C-8E3D-C4579291692E}")
_IID_IMMDeviceEnumerator = _GUID("{A95664D2-9614-4F35-A746-DE8DB63617E6}")
_IID_IAudioClient = _GUID("{1CB9AD4C-DBFA-4C32-B178-C2F568A703B2}")
_IID_IAudioCaptureClient = _GUID("{C8ADBD64-E71E-48A0-A4DE-185C395CD317}")

_eRender = 0
_eConsole = 0
_AUDCLNT_SHAREMODE_SHARED = 0
_AUDCLNT_STREAMFLAGS_LOOPBACK = 0x00020000


class _WAVEFORMATEX(ctypes.Structure):
    _fields_ = [
        ("wFormatTag", ctypes.c_uint16),
        ("nChannels", ctypes.c_uint16),
        ("nSamplesPerSec", ctypes.c_uint32),
        ("nAvgBytesPerSec", ctypes.c_uint32),
        ("nBlockAlign", ctypes.c_uint16),
        ("wBitsPerSample", ctypes.c_uint16),
        ("cbSize", ctypes.c_uint16),
    ]


class _WAVEFORMATEXTENSIBLE(ctypes.Structure):
    class _Samples(ctypes.Union):
        _fields_ = [
            ("wValidBitsPerSample", ctypes.c_uint16),
            ("wSamplesPerBlock", ctypes.c_uint16),
            ("wReserved", ctypes.c_uint16),
        ]

    _fields_ = [
        ("Format", _WAVEFORMATEX),
        ("Samples", _Samples),
        ("dwChannelMask", ctypes.c_uint32),
        ("SubFormat", _GUID),
    ]


def _check_hr(hr: int) -> None:
    if hr < 0:
        raise ctypes.WinError(hr)


def _com_method(iface: ctypes.c_void_p, index: int, restype, *argtypes):
    """Fetch a COM interface method from its vtable."""
    vtable_ptr = ctypes.cast(iface, ctypes.POINTER(ctypes.c_void_p)).contents
    vtbl = ctypes.cast(vtable_ptr, ctypes.POINTER(ctypes.c_void_p))
    func_ptr = vtbl[index]
    return ctypes.CFUNCTYPE(restype, ctypes.c_void_p, *argtypes)(func_ptr)


class WasapiLoopbackCapture:
    """Capture system audio output on Windows using WASAPI loopback."""

    def __init__(self):
        self._audio_client: ctypes.c_void_p = ctypes.c_void_p()
        self._capture_client: ctypes.c_void_p = ctypes.c_void_p()
        self._mix_format: Optional[_WAVEFORMATEXTENSIBLE] = None
        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._on_data: Optional[Callable[[np.ndarray], None]] = None

    @staticmethod
    def is_available() -> bool:
        return _IS_WINDOWS

    def start(self, on_data: Callable[[np.ndarray], None]) -> bool:
        if not _IS_WINDOWS:
            return False
        try:
            self._start_impl(on_data)
            return True
        except Exception as e:
            log.warning(f"WASAPI loopback start failed: {e}")
            self._cleanup()
            return False

    def _start_impl(self, on_data: Callable[[np.ndarray], None]) -> None:
        self._on_data = on_data
        ole32 = ctypes.windll.ole32
        ole32.CoInitializeEx(None, 0)

        # MMDeviceEnumerator
        pEnumerator = ctypes.c_void_p()
        hr = ole32.CoCreateInstance(
            ctypes.byref(_CLSID_MMDeviceEnumerator),
            None,
            1,  # CLSCTX_INPROC_SERVER
            ctypes.byref(_IID_IMMDeviceEnumerator),
            ctypes.byref(pEnumerator),
        )
        _check_hr(hr)

        try:
            # Default render (output) endpoint
            pDevice = ctypes.c_void_p()
            get_default = _com_method(
                pEnumerator, 4, ctypes.HRESULT,
                ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_void_p)
            )
            hr = get_default(pEnumerator, _eRender, _eConsole, ctypes.byref(pDevice))
            _check_hr(hr)
        finally:
            release_enum = _com_method(pEnumerator, 2, ctypes.c_ulong)
            release_enum(pEnumerator)

        try:
            # Activate IAudioClient
            pAudioClient = ctypes.c_void_p()
            activate = _com_method(
                pDevice, 3, ctypes.HRESULT,
                ctypes.POINTER(_GUID), ctypes.c_void_p, ctypes.c_uint32,
                ctypes.POINTER(ctypes.c_void_p)
            )
            hr = activate(
                pDevice, ctypes.byref(_IID_IAudioClient), None, 0,
                ctypes.byref(pAudioClient)
            )
            _check_hr(hr)
        finally:
            release_dev = _com_method(pDevice, 2, ctypes.c_ulong)
            release_dev(pDevice)

        try:
            # Get mix format
            pMixFormat = ctypes.POINTER(_WAVEFORMATEXTENSIBLE)()
            get_mix_format = _com_method(
                pAudioClient, 8, ctypes.HRESULT,
                ctypes.POINTER(ctypes.POINTER(_WAVEFORMATEXTENSIBLE))
            )
            hr = get_mix_format(pAudioClient, ctypes.byref(pMixFormat))
            _check_hr(hr)
            self._mix_format = pMixFormat.contents
            log.info(
                f"WASAPI mix format: {self._mix_format.Format.nChannels}ch, "
                f"{self._mix_format.Format.nSamplesPerSec}Hz, "
                f"{self._mix_format.Format.wBitsPerSample}bit"
            )

            # Initialize shared-mode stream with loopback flag
            hns_buffer_duration = ctypes.c_int64(int(10 * 10000000))  # 10ms
            initialize = _com_method(
                pAudioClient, 3, ctypes.HRESULT,
                ctypes.c_int, ctypes.c_uint32, ctypes.c_int64, ctypes.c_int64,
                ctypes.POINTER(_WAVEFORMATEXTENSIBLE), ctypes.c_void_p
            )
            hr = initialize(
                pAudioClient,
                _AUDCLNT_SHAREMODE_SHARED,
                _AUDCLNT_STREAMFLAGS_LOOPBACK,
                hns_buffer_duration,
                0,
                pMixFormat,
                None,
            )
            _check_hr(hr)

            # Get capture client
            pCaptureClient = ctypes.c_void_p()
            get_service = _com_method(
                pAudioClient, 14, ctypes.HRESULT,
                ctypes.POINTER(_GUID), ctypes.POINTER(ctypes.c_void_p)
            )
            hr = get_service(
                pAudioClient, ctypes.byref(_IID_IAudioCaptureClient),
                ctypes.byref(pCaptureClient)
            )
            _check_hr(hr)

            # Start
            start = _com_method(pAudioClient, 10, ctypes.HRESULT)
            hr = start(pAudioClient)
            _check_hr(hr)

            self._audio_client = pAudioClient
            self._capture_client = pCaptureClient
        except Exception:
            release = _com_method(pAudioClient, 2, ctypes.c_ulong)
            release(pAudioClient)
            raise

        self._running = True
        self._thread = threading.Thread(target=self._capture_loop, daemon=True)
        self._thread.start()

    def _capture_loop(self) -> None:
        if self._mix_format is None:
            return

        sample_rate = self._mix_format.Format.nSamplesPerSec
        channels = self._mix_format.Format.nChannels
        bits = self._mix_format.Format.wBitsPerSample
        bytes_per_sample = bits // 8

        get_next_packet_size = _com_method(
            self._capture_client, 5, ctypes.HRESULT,
            ctypes.POINTER(ctypes.c_uint32)
        )
        get_buffer = _com_method(
            self._capture_client, 3, ctypes.HRESULT,
            ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_uint32),
            ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_uint64)
        )
        release_buffer = _com_method(
            self._capture_client, 4, ctypes.HRESULT, ctypes.c_uint32
        )

        log.info("WASAPI loopback capture loop started")
        while self._running:
            try:
                packet_length = ctypes.c_uint32()
                hr = get_next_packet_size(
                    self._capture_client, ctypes.byref(packet_length)
                )
                _check_hr(hr)

                if packet_length.value == 0:
                    time.sleep(0.001)
                    continue

                data_ptr = ctypes.c_void_p()
                frames_available = ctypes.c_uint32()
                flags = ctypes.c_uint32()
                hr = get_buffer(
                    self._capture_client,
                    ctypes.byref(data_ptr),
                    ctypes.byref(frames_available),
                    ctypes.byref(flags),
                    None,
                    None,
                )
                _check_hr(hr)

                if frames_available.value > 0 and data_ptr.value:
                    frame_count = frames_available.value
                    buffer_size = frame_count * channels * bytes_per_sample
                    try:
                        if bits == 32:
                            arr = np.frombuffer(
                                (ctypes.c_char * buffer_size).from_address(
                                    data_ptr.value
                                ),
                                dtype=np.float32,
                            ).copy()
                            arr = arr.reshape(-1, channels)
                        elif bits == 16:
                            arr = np.frombuffer(
                                (ctypes.c_char * buffer_size).from_address(
                                    data_ptr.value
                                ),
                                dtype=np.int16,
                            ).astype(np.float32) / 32768.0
                            arr = arr.reshape(-1, channels)
                        else:
                            arr = None

                        if arr is not None and self._on_data:
                            self._on_data(arr)
                    except Exception as e:
                        log.warning(f"WASAPI buffer processing error: {e}")

                hr = release_buffer(
                    self._capture_client, frames_available.value
                )
                _check_hr(hr)
            except Exception as e:
                if self._running:
                    log.warning(f"WASAPI capture loop error: {e}")
                break

    def stop(self) -> None:
        self._running = False
        if self._thread:
            self._thread.join(timeout=2)
            self._thread = None
        self._cleanup()

    def _cleanup(self) -> None:
        if self._audio_client:
            try:
                stop = _com_method(self._audio_client, 11, ctypes.HRESULT)
                stop(self._audio_client)
            except Exception:
                pass
            try:
                release = _com_method(self._audio_client, 2, ctypes.c_ulong)
                release(self._audio_client)
            except Exception:
                pass
            self._audio_client = ctypes.c_void_p()
        if self._capture_client:
            try:
                release = _com_method(self._capture_client, 2, ctypes.c_ulong)
                release(self._capture_client)
            except Exception:
                pass
            self._capture_client = ctypes.c_void_p()
        try:
            ctypes.windll.ole32.CoUninitialize()
        except Exception:
            pass
