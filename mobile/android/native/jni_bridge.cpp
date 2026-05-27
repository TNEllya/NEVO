#include <jni.h>
#include <android/log.h>

#define LOG_TAG "NevoJNI"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_nevo_voip_core_crypto_CryptoManager_nativeInit(JNIEnv*, jclass);

JNIEXPORT jobject JNICALL
Java_com_nevo_voip_core_crypto_CryptoManager_nativeGenerateKeyPair(JNIEnv*, jclass);

JNIEXPORT jbyteArray JNICALL
Java_com_nevo_voip_core_crypto_CryptoManager_nativeEncryptSealed(
    JNIEnv*, jclass, jbyteArray, jbyteArray);

JNIEXPORT jbyteArray JNICALL
Java_com_nevo_voip_core_crypto_CryptoManager_nativeDecryptSealed(
    JNIEnv*, jclass, jbyteArray, jbyteArray);

JNIEXPORT jbyteArray JNICALL
Java_com_nevo_voip_core_crypto_CryptoManager_nativeVoiceEncrypt(
    JNIEnv*, jclass, jbyteArray, jbyteArray, jbyteArray, jbyteArray);

JNIEXPORT jbyteArray JNICALL
Java_com_nevo_voip_core_crypto_CryptoManager_nativeVoiceDecrypt(
    JNIEnv*, jclass, jbyteArray, jbyteArray, jbyteArray, jbyteArray);

JNIEXPORT jboolean JNICALL
Java_com_nevo_voip_core_audio_NativeAudioEngine_nativeInitAudio(
    JNIEnv*, jclass, jint, jint);

JNIEXPORT jbyteArray JNICALL
Java_com_nevo_voip_core_audio_NativeAudioEngine_nativeEncodeOpus(
    JNIEnv*, jclass, jshortArray, jint);

JNIEXPORT jshortArray JNICALL
Java_com_nevo_voip_core_audio_NativeAudioEngine_nativeDecodeOpus(
    JNIEnv*, jclass, jbyteArray, jint);

JNIEXPORT jbyteArray JNICALL
Java_com_nevo_voip_core_audio_NativeAudioEngine_nativeMixAudio(
    JNIEnv*, jclass, jobjectArray, jfloatArray);

JNIEXPORT void JNICALL
Java_com_nevo_voip_core_audio_NativeAudioEngine_nativeJitterBufferPush(
    JNIEnv*, jclass, jbyteArray, jint);

JNIEXPORT jobject JNICALL
Java_com_nevo_voip_core_audio_NativeAudioEngine_nativeJitterBufferPop(
    JNIEnv*, jclass);

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;
    LOGD("NEVO JNI library loaded");
    return JNI_VERSION_1_6;
}

} // extern "C"