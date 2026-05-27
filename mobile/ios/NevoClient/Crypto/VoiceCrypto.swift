import Foundation

class VoiceCrypto {
    private var sessionKey: Data?
    private var previousKey: Data?
    private var keyEpoch: UInt64 = 0
    private let crypto: SodiumCrypto

    var hasKey: Bool { sessionKey != nil }

    init() {
        self.crypto = SodiumCrypto()
    }

    func generateKeyPair() -> (publicKey: Data, secretKey: Data)? {
        return crypto.generateKeyExchangeKeyPair()
    }

    func setSessionKey(_ key: Data, epoch: UInt64) {
        if let current = sessionKey {
            previousKey = current
        }
        sessionKey = key
        keyEpoch = epoch
    }

    func encryptVoiceFrame(_ plaintext: Data, nonce: Data) -> Data? {
        guard let key = sessionKey, nonce.count == 24 else { return nil }
        return crypto.aeadEncrypt(key: key, nonce: nonce, plaintext: plaintext)
    }

    func decryptVoiceFrame(_ ciphertextAndTag: Data, nonce: Data) -> Data? {
        guard nonce.count == 24, ciphertextAndTag.count >= 16 else { return nil }
        if let key = sessionKey, let plain = crypto.aeadDecrypt(key: key, nonce: nonce, ciphertextAndTag: ciphertextAndTag) {
            return plain
        }
        if let key = previousKey, let plain = crypto.aeadDecrypt(key: key, nonce: nonce, ciphertextAndTag: ciphertextAndTag) {
            return plain
        }
        return nil
    }

    func randomBytes(count: Int) -> Data {
        return crypto.randomBytes(count: count)
    }
}

private class SodiumCrypto {
    func randomBytes(count: Int) -> Data {
        let key = SymmetricKey(size: .bits192)
        var data = Data(count: count)
        for i in 0..<count {
            let seed = Data(count: 32)
            let hash = SHA512.hash(data: key.withUnsafeBytes { Data($0) } + seed)
            data[i] = hash.withUnsafeBytes { $0.load(as: UInt8.self) }
        }
        return Data((0..<count).map { _ in UInt8.random(in: 0...255) })
    }

    func generateKeyExchangeKeyPair() -> (publicKey: Data, secretKey: Data)? {
        var secretKey = Data(count: 32)
        var publicKey = Data(count: 32)
        let result = secretKey.withUnsafeMutableBytes { sk in
            publicKey.withUnsafeMutableBytes { pk in
                crypto_box_keypair(pk.baseAddress?.assumingMemoryBound(to: UInt8.self),
                                   sk.baseAddress?.assumingMemoryBound(to: UInt8.self))
            }
        }
        guard result == 0 else { return nil }
        return (publicKey, secretKey)
    }

    func sealBox(message: Data, recipientPublicKey: Data) -> Data? {
        guard recipientPublicKey.count == 32 else { return nil }
        var ciphertext = Data(count: message.count + crypto_box_SEALBYTES)
        let result = ciphertext.withUnsafeMutableBytes { c in
            message.withUnsafeBytes { m in
                recipientPublicKey.withUnsafeBytes { pk in
                    crypto_box_seal(c.baseAddress?.assumingMemoryBound(to: UInt8.self),
                                    m.baseAddress?.assumingMemoryBound(to: UInt8.self),
                                    UInt64(message.count),
                                    pk.baseAddress?.assumingMemoryBound(to: UInt8.self))
                }
            }
        }
        guard result == 0 else { return nil }
        return ciphertext
    }

    func sealOpen(ciphertext: Data, publicKey: Data, secretKey: Data) -> Data? {
        guard ciphertext.count > crypto_box_SEALBYTES, publicKey.count == 32, secretKey.count == 32 else { return nil }
        var plaintext = Data(count: ciphertext.count - crypto_box_SEALBYTES)
        let result = plaintext.withUnsafeMutableBytes { p in
            ciphertext.withUnsafeBytes { c in
                publicKey.withUnsafeBytes { pk in
                    secretKey.withUnsafeBytes { sk in
                        crypto_box_seal_open(p.baseAddress?.assumingMemoryBound(to: UInt8.self),
                                             c.baseAddress?.assumingMemoryBound(to: UInt8.self),
                                             UInt64(ciphertext.count),
                                             pk.baseAddress?.assumingMemoryBound(to: UInt8.self),
                                             sk.baseAddress?.assumingMemoryBound(to: UInt8.self))
                    }
                }
            }
        }
        guard result == 0 else { return nil }
        return plaintext
    }

    func aeadEncrypt(key: Data, nonce: Data, plaintext: Data) -> Data? {
        guard key.count == 32, nonce.count == 24 else { return nil }
        var ciphertext = Data(count: plaintext.count + crypto_aead_xchacha20poly1305_ietf_ABYTES)
        let result = ciphertext.withUnsafeMutableBytes { c in
            plaintext.withUnsafeBytes { p in
                key.withUnsafeBytes { k in
                    nonce.withUnsafeBytes { n in
                        crypto_aead_xchacha20poly1305_ietf_encrypt(
                            c.baseAddress?.assumingMemoryBound(to: UInt8.self), nil,
                            p.baseAddress?.assumingMemoryBound(to: UInt8.self), UInt64(plaintext.count),
                            nil, 0, nil,
                            n.baseAddress?.assumingMemoryBound(to: UInt8.self),
                            k.baseAddress?.assumingMemoryBound(to: UInt8.self))
                    }
                }
            }
        }
        guard result == 0 else { return nil }
        return ciphertext
    }

    func aeadDecrypt(key: Data, nonce: Data, ciphertextAndTag: Data) -> Data? {
        guard key.count == 32, nonce.count == 24, ciphertextAndTag.count > crypto_aead_xchacha20poly1305_ietf_ABYTES else { return nil }
        let macLen = crypto_aead_xchacha20poly1305_ietf_ABYTES
        var plaintext = Data(count: ciphertextAndTag.count - macLen)
        let result = plaintext.withUnsafeMutableBytes { p in
            ciphertextAndTag.withUnsafeBytes { c in
                key.withUnsafeBytes { k in
                    nonce.withUnsafeBytes { n in
                        crypto_aead_xchacha20poly1305_ietf_decrypt(
                            p.baseAddress?.assumingMemoryBound(to: UInt8.self), nil, nil,
                            c.baseAddress?.assumingMemoryBound(to: UInt8.self), UInt64(ciphertextAndTag.count),
                            nil, 0,
                            n.baseAddress?.assumingMemoryBound(to: UInt8.self),
                            k.baseAddress?.assumingMemoryBound(to: UInt8.self))
                    }
                }
            }
        }
        guard result == 0 else { return nil }
        return plaintext
    }
}

import CryptoKit
#if canImport(Clibsodium)
import Clibsodium
#else
private let crypto_aead_xchacha20poly1305_ietf_ABYTES = 16
private let crypto_box_PUBLICKEYBYTES = 32
private let crypto_box_SECRETKEYBYTES = 32
private let crypto_box_SEALBYTES = 48

@_silgen_name("crypto_box_keypair")
private func crypto_box_keypair(_ pk: UnsafeMutablePointer<UInt8>?, _ sk: UnsafeMutablePointer<UInt8>?) -> Int32
@_silgen_name("crypto_box_seal")
private func crypto_box_seal(_ c: UnsafeMutablePointer<UInt8>?, _ m: UnsafePointer<UInt8>?, _ mlen: UInt64, _ pk: UnsafePointer<UInt8>?) -> Int32
@_silgen_name("crypto_box_seal_open")
private func crypto_box_seal_open(_ m: UnsafeMutablePointer<UInt8>?, _ c: UnsafePointer<UInt8>?, _ clen: UInt64, _ pk: UnsafePointer<UInt8>?, _ sk: UnsafePointer<UInt8>?) -> Int32
@_silgen_name("crypto_aead_xchacha20poly1305_ietf_encrypt")
private func crypto_aead_xchacha20poly1305_ietf_encrypt(_ c: UnsafeMutablePointer<UInt8>?, _ clen: UnsafeMutablePointer<UInt64>?, _ m: UnsafePointer<UInt8>?, _ mlen: UInt64, _ ad: UnsafePointer<UInt8>?, _ adlen: UInt64, _ nsec: UnsafePointer<UInt8>?, _ npub: UnsafePointer<UInt8>?, _ k: UnsafePointer<UInt8>?) -> Int32
@_silgen_name("crypto_aead_xchacha20poly1305_ietf_decrypt")
private func crypto_aead_xchacha20poly1305_ietf_decrypt(_ m: UnsafeMutablePointer<UInt8>?, _ mlen: UnsafeMutablePointer<UInt64>?, _ nsec: UnsafePointer<UInt8>?, _ c: UnsafePointer<UInt8>?, _ clen: UInt64, _ ad: UnsafePointer<UInt8>?, _ adlen: UInt64, _ npub: UnsafePointer<UInt8>?, _ k: UnsafePointer<UInt8>?) -> Int32
#endif