#include <jni.h>
#include <android/log.h>
#include <cstring>
#include <memory>
#include <sodium.h>

#define LOG_TAG "NevoJNI_Crypto"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static bool g_sodium_ready = false;

static jbyteArray newByteArray(JNIEnv* env, const uint8_t* data, size_t len) {
    jbyteArray result = env->NewByteArray(static_cast<jsize>(len));
    if (result && len > 0) {
        env->SetByteArrayRegion(result, 0, static_cast<jsize>(len),
                                reinterpret_cast<const jbyte*>(data));
    }
    return result;
}

static jbyteArray copyByteArray(JNIEnv* env, const std::vector<uint8_t>& vec) {
    return newByteArray(env, vec.data(), vec.size());
}

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_nevo_voip_core_crypto_CryptoManager_nativeInit(JNIEnv* env, jclass cls) {
    (void)env;
    (void)cls;
    if (sodium_init() < 0) {
        LOGE("libsodium initialization failed");
        g_sodium_ready = false;
        return JNI_FALSE;
    }
    g_sodium_ready = true;
    LOGD("libsodium initialized successfully");
    return JNI_TRUE;
}

JNIEXPORT jobject JNICALL
Java_com_nevo_voip_core_crypto_CryptoManager_nativeGenerateKeyPair(
    JNIEnv* env, jclass cls) {
    if (!g_sodium_ready) {
        LOGE("generateKeyPair: sodium not initialized");
        return nullptr;
    }

    uint8_t pk[crypto_box_PUBLICKEYBYTES];
    uint8_t sk[crypto_box_SECRETKEYBYTES];

    if (crypto_box_keypair(pk, sk) != 0) {
        LOGE("X25519 keypair generation failed");
        return nullptr;
    }

    jclass pairClass = env->FindClass("kotlin/Pair");
    if (!pairClass) {
        LOGE("Failed to find kotlin.Pair class");
        return nullptr;
    }

    jmethodID ctor = env->GetMethodID(pairClass, "<init>",
        "(Ljava/lang/Object;Ljava/lang/Object;)V");
    if (!ctor) {
        LOGE("Failed to find kotlin.Pair constructor");
        return nullptr;
    }

    jbyteArray pkArray = newByteArray(env, pk, crypto_box_PUBLICKEYBYTES);
    jbyteArray skArray = newByteArray(env, sk, crypto_box_SECRETKEYBYTES);

    sodium_memzero(sk, sizeof(sk));

    jobject pair = env->NewObject(pairClass, ctor, pkArray, skArray);

    env->DeleteLocalRef(pairClass);
    if (pkArray) env->DeleteLocalRef(pkArray);
    if (skArray) env->DeleteLocalRef(skArray);

    return pair;
}

JNIEXPORT jbyteArray JNICALL
Java_com_nevo_voip_core_crypto_CryptoManager_nativeEncryptSealed(
    JNIEnv* env, jclass cls, jbyteArray message, jbyteArray recipientPublicKey) {
    (void)cls;
    if (!g_sodium_ready) {
        LOGE("encryptSealed: sodium not initialized");
        return nullptr;
    }

    if (!message || !recipientPublicKey) {
        LOGE("encryptSealed: null argument");
        return nullptr;
    }

    jsize pkLen = env->GetArrayLength(recipientPublicKey);
    if (pkLen != crypto_box_PUBLICKEYBYTES) {
        LOGE("encryptSealed: invalid public key length %d (expected %d)",
             pkLen, crypto_box_PUBLICKEYBYTES);
        return nullptr;
    }

    jsize msgLen = env->GetArrayLength(message);
    if (msgLen <= 0) {
        LOGE("encryptSealed: empty message");
        return nullptr;
    }

    uint8_t pk[crypto_box_PUBLICKEYBYTES];
    env->GetByteArrayRegion(recipientPublicKey, 0, pkLen,
                            reinterpret_cast<jbyte*>(pk));

    std::vector<uint8_t> plaintext(static_cast<size_t>(msgLen));
    env->GetByteArrayRegion(message, 0, msgLen,
                            reinterpret_cast<jbyte*>(plaintext.data()));

    size_t clen = plaintext.size() + crypto_box_SEALBYTES;
    std::vector<uint8_t> ciphertext(clen, 0);

    int result = crypto_box_seal(
        ciphertext.data(), plaintext.data(), plaintext.size(), pk);

    sodium_memzero(pk, sizeof(pk));
    sodium_memzero(plaintext.data(), plaintext.size());

    if (result != 0) {
        LOGE("crypto_box_seal encryption failed");
        return nullptr;
    }

    LOGD("encryptSealed: %zu bytes -> %zu bytes", plaintext.size(), ciphertext.size());

    jbyteArray output = copyByteArray(env, ciphertext);
    sodium_memzero(ciphertext.data(), ciphertext.size());
    return output;
}

JNIEXPORT jbyteArray JNICALL
Java_com_nevo_voip_core_crypto_CryptoManager_nativeDecryptSealed(
    JNIEnv* env, jclass cls, jbyteArray ciphertext, jbyteArray privateKey) {
    (void)cls;
    if (!g_sodium_ready) {
        LOGE("decryptSealed: sodium not initialized");
        return nullptr;
    }

    if (!ciphertext || !privateKey) {
        LOGE("decryptSealed: null argument");
        return nullptr;
    }

    jsize skLen = env->GetArrayLength(privateKey);
    if (skLen != crypto_box_SECRETKEYBYTES) {
        LOGE("decryptSealed: invalid secret key length %d (expected %d)",
             skLen, crypto_box_SECRETKEYBYTES);
        return nullptr;
    }

    jsize ctLen = env->GetArrayLength(ciphertext);
    if (ctLen <= static_cast<jsize>(crypto_box_SEALBYTES)) {
        LOGE("decryptSealed: ciphertext too short (%d bytes)", ctLen);
        return nullptr;
    }

    uint8_t sk[crypto_box_SECRETKEYBYTES];
    env->GetByteArrayRegion(privateKey, 0, skLen,
                            reinterpret_cast<jbyte*>(sk));

    uint8_t pk[crypto_box_PUBLICKEYBYTES];
    if (crypto_scalarmult_base(pk, sk) != 0) {
        LOGE("decryptSealed: failed to derive public key from secret key");
        sodium_memzero(sk, sizeof(sk));
        return nullptr;
    }

    std::vector<uint8_t> ct(static_cast<size_t>(ctLen));
    env->GetByteArrayRegion(ciphertext, 0, ctLen,
                            reinterpret_cast<jbyte*>(ct.data()));

    size_t ptLen = ct.size() - crypto_box_SEALBYTES;
    std::vector<uint8_t> plaintext(ptLen, 0);

    int result = crypto_box_seal_open(
        plaintext.data(), ct.data(), ct.size(), pk, sk);

    sodium_memzero(sk, sizeof(sk));
    sodium_memzero(pk, sizeof(pk));

    if (result != 0) {
        LOGE("crypto_box_seal_open decryption failed");
        return nullptr;
    }

    LOGD("decryptSealed: %zu bytes -> %zu bytes", ct.size(), plaintext.size());

    jbyteArray output = copyByteArray(env, plaintext);
    sodium_memzero(plaintext.data(), plaintext.size());
    return output;
}

JNIEXPORT jbyteArray JNICALL
Java_com_nevo_voip_core_crypto_CryptoManager_nativeVoiceEncrypt(
    JNIEnv* env, jclass cls,
    jbyteArray key, jbyteArray nonce,
    jbyteArray plaintext, jbyteArray aad) {
    (void)cls;
    if (!g_sodium_ready) {
        LOGE("voiceEncrypt: sodium not initialized");
        return nullptr;
    }

    if (!key || !nonce || !plaintext) {
        LOGE("voiceEncrypt: null required argument");
        return nullptr;
    }

    jsize keyLen = env->GetArrayLength(key);
    if (keyLen != crypto_aead_xchacha20poly1305_ietf_KEYBYTES) {
        LOGE("voiceEncrypt: invalid key length %d (expected %d)",
             keyLen, crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
        return nullptr;
    }

    jsize nonceLen = env->GetArrayLength(nonce);
    if (nonceLen != crypto_aead_xchacha20poly1305_ietf_NPUBBYTES) {
        LOGE("voiceEncrypt: invalid nonce length %d (expected %d)",
             nonceLen, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
        return nullptr;
    }

    jsize ptLen = env->GetArrayLength(plaintext);
    if (ptLen <= 0) {
        LOGW("voiceEncrypt: empty plaintext");
    }

    uint8_t k[crypto_aead_xchacha20poly1305_ietf_KEYBYTES];
    env->GetByteArrayRegion(key, 0, keyLen, reinterpret_cast<jbyte*>(k));

    uint8_t n[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES];
    env->GetByteArrayRegion(nonce, 0, nonceLen, reinterpret_cast<jbyte*>(n));

    std::vector<uint8_t> pt(static_cast<size_t>(ptLen));
    if (ptLen > 0) {
        env->GetByteArrayRegion(plaintext, 0, ptLen,
                                reinterpret_cast<jbyte*>(pt.data()));
    }

    const uint8_t* aadPtr = nullptr;
    size_t aadLen = 0;
    std::vector<uint8_t> aadBuf;
    if (aad) {
        aadLen = static_cast<size_t>(env->GetArrayLength(aad));
        if (aadLen > 0) {
            aadBuf.resize(aadLen);
            env->GetByteArrayRegion(aad, 0, static_cast<jsize>(aadLen),
                                    reinterpret_cast<jbyte*>(aadBuf.data()));
            aadPtr = aadBuf.data();
        }
    }

    size_t clen = pt.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES;
    std::vector<uint8_t> ct(clen, 0);

    unsigned long long actualClen = 0;
    int result = crypto_aead_xchacha20poly1305_ietf_encrypt(
        ct.data(), &actualClen,
        pt.data(), pt.size(),
        aadPtr, aadLen,
        nullptr, n, k);

    sodium_memzero(k, sizeof(k));

    if (result != 0) {
        LOGE("voiceEncrypt: XChaCha20-Poly1305 encryption failed");
        return nullptr;
    }

    ct.resize(actualClen);
    jbyteArray output = copyByteArray(env, ct);
    sodium_memzero(ct.data(), ct.size());
    return output;
}

JNIEXPORT jbyteArray JNICALL
Java_com_nevo_voip_core_crypto_CryptoManager_nativeVoiceDecrypt(
    JNIEnv* env, jclass cls,
    jbyteArray key, jbyteArray nonce,
    jbyteArray ciphertext, jbyteArray aad) {
    (void)cls;
    if (!g_sodium_ready) {
        LOGE("voiceDecrypt: sodium not initialized");
        return nullptr;
    }

    if (!key || !nonce || !ciphertext) {
        LOGE("voiceDecrypt: null required argument");
        return nullptr;
    }

    jsize keyLen = env->GetArrayLength(key);
    if (keyLen != crypto_aead_xchacha20poly1305_ietf_KEYBYTES) {
        LOGE("voiceDecrypt: invalid key length %d (expected %d)",
             keyLen, crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
        return nullptr;
    }

    jsize nonceLen = env->GetArrayLength(nonce);
    if (nonceLen != crypto_aead_xchacha20poly1305_ietf_NPUBBYTES) {
        LOGE("voiceDecrypt: invalid nonce length %d (expected %d)",
             nonceLen, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
        return nullptr;
    }

    jsize ctLen = env->GetArrayLength(ciphertext);
    if (ctLen <= static_cast<jsize>(crypto_aead_xchacha20poly1305_ietf_ABYTES)) {
        LOGE("voiceDecrypt: ciphertext too short (%d bytes)", ctLen);
        return nullptr;
    }

    uint8_t k[crypto_aead_xchacha20poly1305_ietf_KEYBYTES];
    env->GetByteArrayRegion(key, 0, keyLen, reinterpret_cast<jbyte*>(k));

    uint8_t n[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES];
    env->GetByteArrayRegion(nonce, 0, nonceLen, reinterpret_cast<jbyte*>(n));

    std::vector<uint8_t> ct(static_cast<size_t>(ctLen));
    env->GetByteArrayRegion(ciphertext, 0, ctLen,
                            reinterpret_cast<jbyte*>(ct.data()));

    const uint8_t* aadPtr = nullptr;
    size_t aadLen = 0;
    std::vector<uint8_t> aadBuf;
    if (aad) {
        aadLen = static_cast<size_t>(env->GetArrayLength(aad));
        if (aadLen > 0) {
            aadBuf.resize(aadLen);
            env->GetByteArrayRegion(aad, 0, static_cast<jsize>(aadLen),
                                    reinterpret_cast<jbyte*>(aadBuf.data()));
            aadPtr = aadBuf.data();
        }
    }

    size_t ptLen = ct.size() - crypto_aead_xchacha20poly1305_ietf_ABYTES;
    std::vector<uint8_t> pt(ptLen, 0);

    unsigned long long actualPtLen = 0;
    int result = crypto_aead_xchacha20poly1305_ietf_decrypt(
        pt.data(), &actualPtLen,
        nullptr,
        ct.data(), ct.size(),
        aadPtr, aadLen,
        n, k);

    sodium_memzero(k, sizeof(k));

    if (result != 0) {
        LOGW("voiceDecrypt: authentication failed");
        return nullptr;
    }

    pt.resize(actualPtLen);
    jbyteArray output = copyByteArray(env, pt);
    sodium_memzero(pt.data(), pt.size());
    return output;
}

} // extern "C"