# -*- coding: utf-8 -*-
"""
VoiceCrypto（Python 语音加密）单元测试

覆盖：往返加密/解密、篡改检测、AAD 绑定、密钥轮换重叠期、
以及跨实例 nonce 唯一性安全回归（同账号多设备共享密钥场景）。

运行：python -m unittest tests.test_voice_crypto（在 gui_python 目录下）
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from voice_crypto import (  # noqa: E402
    CRYPTO_KEY_SIZE,
    POLY1305_TAG_SIZE,
    XCHACHA_NONCE_SIZE,
    VoiceCrypto,
)


def make_key(seed: int) -> bytes:
    return bytes((seed * 7 + i * 13 + 37) & 0xFF for i in range(CRYPTO_KEY_SIZE))


class VoiceCryptoRoundtripTest(unittest.TestCase):
    def setUp(self):
        self.crypto = VoiceCrypto()
        self.key = make_key(0xAB)
        self.crypto.set_session_key(self.key)

    def test_roundtrip_with_aad(self):
        plaintext = b"NEVO voice frame payload"
        aad = b"protobuf-header-bytes"
        encrypted = self.crypto.encrypt(plaintext, header_aad=aad)
        self.assertEqual(len(encrypted),
                         XCHACHA_NONCE_SIZE + len(plaintext) + POLY1305_TAG_SIZE)
        decrypted = self.crypto.decrypt(encrypted, header_aad=aad)
        self.assertEqual(decrypted, plaintext)

    def test_tampered_ciphertext_fails(self):
        encrypted = self.crypto.encrypt(b"secret audio", header_aad=b"hdr")
        tampered = bytearray(encrypted)
        tampered[XCHACHA_NONCE_SIZE + 2] ^= 0xFF  # 篡改密文
        self.assertIsNone(self.crypto.decrypt(bytes(tampered), header_aad=b"hdr"))

    def test_wrong_aad_fails(self):
        encrypted = self.crypto.encrypt(b"secret audio", header_aad=b"hdr-a")
        self.assertIsNone(self.crypto.decrypt(encrypted, header_aad=b"hdr-b"))

    def test_wrong_key_fails(self):
        other = VoiceCrypto()
        other.set_session_key(make_key(0xCD))
        encrypted = self.crypto.encrypt(b"secret audio", header_aad=b"hdr")
        self.assertIsNone(other.decrypt(encrypted, header_aad=b"hdr"))

    def test_key_rotation_overlap(self):
        old_key = self.key
        new_key = make_key(0xEF)
        encrypted_old = self.crypto.encrypt(b"before rotation", header_aad=b"h")
        self.crypto.rotate_key(new_key)
        # 旧包仍可用旧密钥解密（重叠期）
        self.assertEqual(self.crypto.decrypt(encrypted_old, header_aad=b"h"),
                         b"before rotation")
        # 新包用新密钥
        encrypted_new = self.crypto.encrypt(b"after rotation", header_aad=b"h")
        self.assertEqual(self.crypto.decrypt(encrypted_new, header_aad=b"h"),
                         b"after rotation")


class VoiceCryptoNonceSecurityTest(unittest.TestCase):
    """安全回归：同一把密钥被多个实例共享时，nonce 必须跨实例唯一。"""

    def test_cross_instance_nonce_uniqueness(self):
        shared_key = make_key(7)
        instances = [VoiceCrypto() for _ in range(4)]
        for c in instances:
            c.set_session_key(shared_key)

        seen = set()
        for c in instances:
            for _ in range(250):
                encrypted = c.encrypt(b"x" * 64)
                nonce = encrypted[:XCHACHA_NONCE_SIZE]
                self.assertNotIn(nonce, seen,
                                 "nonce 跨实例重用（密钥流重用风险）！")
                seen.add(nonce)
        self.assertEqual(len(seen), 4 * 250)

    def test_set_session_key_regenerates_prefix(self):
        """同一实例反复设置同一密钥，nonce 前缀也必须更新（实例级唯一性）。"""
        key = make_key(9)
        c = VoiceCrypto()
        c.set_session_key(key)
        n1 = c.encrypt(b"x")[:XCHACHA_NONCE_SIZE]
        c.set_session_key(key)
        n2 = c.encrypt(b"x")[:XCHACHA_NONCE_SIZE]
        self.assertNotEqual(n1[:16], n2[:16])


if __name__ == "__main__":
    unittest.main()
