import numpy as np
from fractions import Fraction as _Fraction

try:
    import av
    HAS_AV = True
except ImportError:
    HAS_AV = False


SCREEN_SHARE_BITRATE_720P = 1500000
SCREEN_SHARE_BITRATE_1080P = 3000000
SCREEN_SHARE_FPS = 15
SCREEN_SHARE_MAX_WIDTH = 1920
SCREEN_SHARE_MAX_HEIGHT = 1080


def _align_dimension(val, alignment=16):
    return (val // alignment) * alignment


class VideoEncoder:
    def __init__(self):
        self._codec_context = None
        self._width = 0
        self._height = 0
        self._fps = SCREEN_SHARE_FPS

    @staticmethod
    def is_available():
        return HAS_AV

    def init(self, width, height, fps=SCREEN_SHARE_FPS, bitrate=None):
        if not HAS_AV:
            return False
        try:
            width = _align_dimension(min(width, SCREEN_SHARE_MAX_WIDTH))
            height = _align_dimension(min(height, SCREEN_SHARE_MAX_HEIGHT))
            if width < 16 or height < 16:
                return False
            self._width = width
            self._height = height
            self._fps = fps

            if bitrate is None:
                if height >= 1080:
                    bitrate = SCREEN_SHARE_BITRATE_1080P
                else:
                    bitrate = SCREEN_SHARE_BITRATE_720P

            self._codec_context = av.codec.CodecContext.create("h264", "w")
            self._codec_context.width = width
            self._codec_context.height = height
            self._codec_context.pix_fmt = "yuv420p"
            self._codec_context.bit_rate = bitrate
            self._codec_context.framerate = _Fraction(fps, 1)
            self._codec_context.options = {
                "preset": "ultrafast",
                "tune": "zerolatency",
                "profile": "high",
            }
            self._codec_context.open()
            return True
        except Exception:
            import traceback
            print(f"[ENCODER] init FAILED: {traceback.format_exc()}")
            self._codec_context = None
            return False

    def encode(self, frame_bgr):
        if not self._codec_context:
            return []
        try:
            av_frame = av.VideoFrame.from_ndarray(frame_bgr, format="bgr24")
            packets = self._codec_context.encode(av_frame)
            result = []
            for pkt in packets:
                result.append(bytes(pkt))
            return result
        except Exception:
            return []

    def flush(self):
        if not self._codec_context:
            return []
        try:
            packets = self._codec_context.encode()
            result = []
            for pkt in packets:
                result.append(bytes(pkt))
            return result
        except Exception:
            return []

    def close(self):
        self._codec_context = None

    @property
    def width(self):
        return self._width

    @property
    def height(self):
        return self._height


class VideoDecoder:
    def __init__(self):
        self._codec_context = None
        self._width = 0
        self._height = 0

    def init(self, width=0, height=0):
        if not HAS_AV:
            return False
        try:
            self._codec_context = av.codec.CodecContext.create("h264", "r")
            self._width = width
            self._height = height
            return True
        except Exception:
            self._codec_context = None
            return False

    def decode(self, nal_data):
        if not self._codec_context:
            return None
        try:
            packet = av.Packet(nal_data)
            frames = self._codec_context.decode(packet)
            for frame in frames:
                return frame.to_ndarray(format="bgr24")
        except Exception:
            pass
        return None

    def close(self):
        self._codec_context = None
