# -*- coding: utf-8 -*-
"""
协议统一性测试（Python 侧金样）

运行时唯一线格式：自定义小端 TLV（见 docs/protocol-wire-format.md）——
C++ 服务端、Python 客户端、Android 客户端共用同一格式。

金样字节与 C++ 侧 tests/core_tests/TestPacketCodec.cpp 中的
PacketCodecInteropTest 一一对应：任何一端格式漂移，任一侧测试即失败。

运行：python -m unittest tests.test_wire_format（在 gui_python 目录下）
"""
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import nevo_wire as w  # noqa: E402

# 与 C++ 测试共享的金样常量（由 Python 生成、C++ 断言解码/编码一致）
LOGIN_REQUEST_GOLDEN_HEX = (
    "010000006800000011000000696e7465726f705f746573745f75736572"
    "0900000070617373773072642101000000160000005832353531392b63"
    "727970746f5f626f785f7365616c20000000000102030405060708090a"
    "0b0c0d0e0f101112131415161718191a1b1c1d1e1f6f5f705f"
)
LOGIN_RESPONSE_GOLDEN_HEX = (
    "02000000bc000000000000001b0000002a0000000000000005000000616c6963"
    "65010000000001020000000b000000616263313233746f6b656e100000001011"
    "12131415161718191a1b1c1d1e1f160000005832353531392b63727970746f5f"
    "626f785f7365616c50000000aabbaabbaabbaabbaabbaabbaabbaabbaabbaabb"
    "aabbaabbaabbaabbaabbaabbaabbaabbaabbaabbaabbaabbaabbaabbaabbaabb"
    "aabbaabbaabbaabbaabbaabbaabbaabbaabbaabbaabbaabbaabbaabb01000000"
    "6f5f705f"
)


def _make_login_request():
    return w.LoginRequest(
        username="interop_test_user",
        auth_credential=b"passw0rd!",
        key_exchange_methods=["X25519+crypto_box_seal"],
        client_public_key=bytes(range(32)),
        client_udp_port=24431,
        client_video_udp_port=24432,
    )


def _make_login_response():
    return w.LoginResponse(
        result=0,
        user_info=w.UserInfo(id=42, username="alice", status=1,
                             muted=False, deafened=True, group_id=2),
        session_token="abc123token",
        server_public_key=bytes(range(16, 32)),
        key_exchange_method="X25519+crypto_box_seal",
        encrypted_session_key=bytes([0xAA, 0xBB] * 40),
        owner_exists=1,
        server_udp_port=24431,
        server_video_udp_port=24432,
    )


class WireFormatGoldenTest(unittest.TestCase):
    """金样测试：锁定 C++ ↔ Python ↔ Android 三端共用的线格式。"""

    def test_login_request_matches_golden(self):
        """Python 编码的 LoginRequest 必须与金样字节一致（C++ 侧依赖此字节流）。"""
        req = _make_login_request()
        outer = w.serialize_control_message(1, req)
        self.assertEqual(outer.hex(), LOGIN_REQUEST_GOLDEN_HEX)

    def test_login_response_matches_golden(self):
        """Python 编码的 LoginResponse 必须与金样字节一致。"""
        resp = _make_login_response()
        outer = w.serialize_control_message(2, resp)
        self.assertEqual(outer.hex(), LOGIN_RESPONSE_GOLDEN_HEX)

    def test_login_request_golden_roundtrip(self):
        """解码金样字节 → 字段逐项一致（等价于 C++ 服务端解码该字节流的视角）。"""
        data = bytes.fromhex(LOGIN_REQUEST_GOLDEN_HEX)
        msg_type, req = w.deserialize_control_message(data)
        self.assertEqual(msg_type, 1)
        self.assertIsInstance(req, w.LoginRequest)
        self.assertEqual(req.username, "interop_test_user")
        self.assertEqual(req.auth_credential, b"passw0rd!")
        self.assertEqual(req.key_exchange_methods, ["X25519+crypto_box_seal"])
        self.assertEqual(req.client_public_key, bytes(range(32)))
        self.assertEqual(req.client_udp_port, 24431)
        self.assertEqual(req.client_video_udp_port, 24432)

    def test_login_response_golden_roundtrip(self):
        """解码 C++ 侧同样会编码出的字节流 → 字段逐项一致。"""
        data = bytes.fromhex(LOGIN_RESPONSE_GOLDEN_HEX)
        msg_type, resp = w.deserialize_control_message(data)
        self.assertEqual(msg_type, 2)
        self.assertIsInstance(resp, w.LoginResponse)
        self.assertEqual(resp.result, 0)
        self.assertEqual(resp.user_info.id, 42)
        self.assertEqual(resp.user_info.username, "alice")
        self.assertEqual(resp.user_info.deafened, True)
        self.assertEqual(resp.session_token, "abc123token")
        self.assertEqual(resp.server_public_key, bytes(range(16, 32)))
        self.assertEqual(resp.key_exchange_method, "X25519+crypto_box_seal")
        self.assertEqual(resp.encrypted_session_key, bytes([0xAA, 0xBB] * 40))
        self.assertEqual(resp.owner_exists, 1)
        self.assertEqual(resp.server_udp_port, 24431)
        self.assertEqual(resp.server_video_udp_port, 24432)

    def test_outer_frame_layout(self):
        """外层包装固定为 [u32 LE case][u32 LE 内层长度][内层载荷]。"""
        req = _make_login_request()
        outer = w.serialize_control_message(1, req)
        case, inner_len = struct.unpack("<II", outer[:8])
        self.assertEqual(case, 1)
        self.assertEqual(inner_len, len(outer) - 8)


class WireFormatRobustnessTest(unittest.TestCase):
    """边界与健壮性：损坏/截断的包必须安全失败，不得静默错位解析。"""

    def test_truncated_outer_frame(self):
        data = bytes.fromhex(LOGIN_REQUEST_GOLDEN_HEX)[:6]  # 缺内层
        msg_type, msg = w.deserialize_control_message(data)
        self.assertIsNone(msg_type)
        self.assertIsNone(msg)

    def test_unknown_case_value(self):
        # case=999 未注册：应安全返回 None 而非抛出/错位
        payload = struct.pack("<II", 999, 4) + b"abcd"
        msg_type, msg = w.deserialize_control_message(payload)
        self.assertIsNone(msg_type)
        self.assertIsNone(msg)

    def test_rename_channel_response_type_35_registration(self):
        """回归：类型 35 必须注册为 RENAME_CHANNEL_RESPONSE 编解码器（曾错注册为 request）。"""
        resp = w.RenameChannelResponse(result=0, message="ok")
        payload = w.serialize_control_message(35, resp)
        msg_type, decoded = w.deserialize_control_message(payload)
        self.assertEqual(msg_type, 35)
        self.assertIsInstance(decoded, w.RenameChannelResponse)
        self.assertEqual(decoded.message, "ok")


if __name__ == "__main__":
    unittest.main()
