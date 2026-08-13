import os
import struct
import threading
import time

try:
    from nacl.bindings import (
        crypto_aead_xchacha20poly1305_ietf_encrypt,
        crypto_aead_xchacha20poly1305_ietf_decrypt,
    )
    HAS_SODIUM = True
except ImportError:
    HAS_SODIUM = False

CRYPTO_KEY_SIZE = 32
XCHACHA_NONCE_SIZE = 24
POLY1305_TAG_SIZE = 16
KEY_OVERLAP_SECONDS = 20
# 每实例随机 nonce 前缀长度：nonce = 前缀(16B) + 计数器(8B)
# 保证同一把会话密钥被多个实例共享时（同账号多设备）nonce 不碰撞（~2^-128）
NONCE_PREFIX_SIZE = 16


class VoiceCrypto:
    def __init__(self):
        self._key = bytes(CRYPTO_KEY_SIZE)
        self._old_key = None
        self._old_key_expiry = 0
        self._nonce_counter = 0
        self._nonce_prefix = os.urandom(NONCE_PREFIX_SIZE)
        self._lock = threading.Lock()

    def set_session_key(self, key):
        with self._lock:
            self._key = bytes(key)
            self._old_key = None
            self._old_key_expiry = 0
            self._nonce_counter = 0
            # 重新生成随机前缀：即使多个实例共享同一密钥，nonce 空间互不重叠
            self._nonce_prefix = os.urandom(NONCE_PREFIX_SIZE)

    def rotate_key(self, new_key):
        with self._lock:
            self._old_key = self._key
            self._old_key_expiry = int(time.time()) + KEY_OVERLAP_SECONDS
            self._key = bytes(new_key)

    def encrypt(self, plaintext, header_aad=None):
        if not HAS_SODIUM:
            return b""
        with self._lock:
            key = self._key
            counter = self._nonce_counter
            self._nonce_counter += 1

        nonce = self._generate_nonce(counter)
        aad = header_aad if header_aad else b""
        try:
            ct = crypto_aead_xchacha20poly1305_ietf_encrypt(
                plaintext, aad, nonce, key
            )
        except Exception:
            return b""

        output = bytearray()
        output.extend(nonce)
        output.extend(ct)
        return bytes(output)

    def decrypt(self, data, nonce=None, header_aad=None):
        if not HAS_SODIUM:
            return None
        if nonce is None:
            if len(data) < XCHACHA_NONCE_SIZE + POLY1305_TAG_SIZE:
                return None
            nonce = data[:XCHACHA_NONCE_SIZE]
            ciphertext = data[XCHACHA_NONCE_SIZE:]
        else:
            ciphertext = data

        if len(ciphertext) < POLY1305_TAG_SIZE:
            return None

        aad = header_aad if header_aad else b""

        with self._lock:
            keys_to_try = [self._key]
            if self._old_key is not None and time.time() < self._old_key_expiry:
                keys_to_try.append(self._old_key)

        for key in keys_to_try:
            try:
                pt = crypto_aead_xchacha20poly1305_ietf_decrypt(
                    ciphertext, aad, nonce, key
                )
                return pt
            except Exception:
                continue

        return None

    def decrypt_simple(self, ciphertext: bytes, header_aad: bytes = None):
        """Decrypt a video packet using the internally set session key."""
        if not HAS_SODIUM:
            return None
        if len(ciphertext) < XCHACHA_NONCE_SIZE + POLY1305_TAG_SIZE:
            return None

        nonce = ciphertext[:XCHACHA_NONCE_SIZE]
        ct_with_tag = ciphertext[XCHACHA_NONCE_SIZE:]
        aad = header_aad if header_aad else b""

        with self._lock:
            keys_to_try = [self._key]
            if self._old_key is not None and time.time() < self._old_key_expiry:
                keys_to_try.append(self._old_key)

        for key in keys_to_try:
            try:
                return crypto_aead_xchacha20poly1305_ietf_decrypt(ct_with_tag, aad, nonce, key)
            except Exception:
                continue

        return None

    def purge_expired_old_key(self):
        with self._lock:
            if self._old_key is not None and time.time() >= self._old_key_expiry:
                self._old_key = None

    def _generate_nonce(self, counter):
        # 24 字节 nonce = 随机前缀(16B) + 计数器(8B LE)
        # 前缀保证跨实例唯一（多设备共享密钥时不会密钥流重用），
        # 计数器保证实例内严格递增
        nonce = bytearray(XCHACHA_NONCE_SIZE)
        nonce[:NONCE_PREFIX_SIZE] = self._nonce_prefix
        struct.pack_into("<Q", nonce, NONCE_PREFIX_SIZE, counter)
        return bytes(nonce)

    @staticmethod
    def encrypted_size(plaintext_len):
        return XCHACHA_NONCE_SIZE + plaintext_len + POLY1305_TAG_SIZE
