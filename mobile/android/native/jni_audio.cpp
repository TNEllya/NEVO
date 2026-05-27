#include <jni.h>
#include <android/log.h>
#include <cstring>
#include <vector>
#include <memory>
#include <algorithm>
#include <limits>
#include <opus/opus.h>

#define LOG_TAG "NevoJNI_Audio"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static OpusEncoder* g_opus_encoder = nullptr;
static OpusDecoder* g_opus_decoder = nullptr;
static int g_sample_rate = 48000;
static int g_channels = 1;
static bool g_audio_initialized = false;

struct JitterPacket {
    std::vector<uint8_t> data;
    uint32_t timestamp;
};

static std::vector<JitterPacket> g_jitter_buffer;
static constexpr size_t kMaxJitterPackets = 64;

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_nevo_voip_core_audio_NativeAudioEngine_nativeInitAudio(
    JNIEnv* env, jclass cls, jint sampleRate, jint channels) {
    (void)env;
    (void)cls;

    if (g_audio_initialized) {
        if (g_opus_encoder) { opus_encoder_destroy(g_opus_encoder); }
        if (g_opus_decoder) { opus_decoder_destroy(g_opus_decoder); }
        g_opus_encoder = nullptr;
        g_opus_decoder = nullptr;
        g_audio_initialized = false;
    }

    g_sample_rate = static_cast<int>(sampleRate);
    g_channels = static_cast<int>(channels);

    if (g_sample_rate != 8000 && g_sample_rate != 12000 &&
        g_sample_rate != 16000 && g_sample_rate != 24000 &&
        g_sample_rate != 48000) {
        LOGE("initAudio: unsupported sample rate %d, defaulting to 48000",
             g_sample_rate);
        g_sample_rate = 48000;
    }

    if (g_channels < 1 || g_channels > 2) {
        LOGE("initAudio: invalid channels %d, defaulting to 1", g_channels);
        g_channels = 1;
    }

    int opusErr = 0;

    g_opus_encoder = opus_encoder_create(
        g_sample_rate, g_channels, OPUS_APPLICATION_VOIP, &opusErr);
    if (opusErr != OPUS_OK || !g_opus_encoder) {
        LOGE("opus_encoder_create failed: %d", opusErr);
        if (g_opus_decoder) { opus_decoder_destroy(g_opus_decoder); g_opus_decoder = nullptr; }
        return JNI_FALSE;
    }

    opus_encoder_ctl(g_opus_encoder, OPUS_SET_BITRATE(32000));
    opus_encoder_ctl(g_opus_encoder, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(g_opus_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(g_opus_encoder, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(g_opus_encoder, OPUS_SET_PACKET_LOSS_PERC(10));

    g_opus_decoder = opus_decoder_create(g_sample_rate, g_channels, &opusErr);
    if (opusErr != OPUS_OK || !g_opus_decoder) {
        LOGE("opus_decoder_create failed: %d", opusErr);
        if (g_opus_encoder) { opus_encoder_destroy(g_opus_encoder); g_opus_encoder = nullptr; }
        return JNI_FALSE;
    }

    g_audio_initialized = true;
    g_jitter_buffer.clear();
    LOGD("Audio initialized: %d Hz, %d channels", g_sample_rate, g_channels);
    return JNI_TRUE;
}

JNIEXPORT jbyteArray JNICALL
Java_com_nevo_voip_core_audio_NativeAudioEngine_nativeEncodeOpus(
    JNIEnv* env, jclass cls, jshortArray pcm, jint frameSize) {
    (void)cls;

    if (!g_audio_initialized || !g_opus_encoder) {
        LOGE("encodeOpus: audio not initialized");
        return nullptr;
    }

    if (!pcm) {
        LOGE("encodeOpus: null PCM array");
        return nullptr;
    }

    jsize pcmLen = env->GetArrayLength(pcm);
    int fs = static_cast<int>(frameSize);
    if (fs <= 0) {
        fs = g_sample_rate / 50;
    }

    if (pcmLen < fs * g_channels) {
        LOGE("encodeOpus: PCM too short (%d samples, need %d)", pcmLen, fs * g_channels);
        return nullptr;
    }

    std::vector<opus_int16> pcmData(static_cast<size_t>(fs * g_channels));
    env->GetShortArrayRegion(pcm, 0, static_cast<jsize>(fs * g_channels),
                             pcmData.data());

    constexpr int kMaxOpusFrame = 4000;
    std::vector<uint8_t> opusData(kMaxOpusFrame, 0);

    int encoded = opus_encode(
        g_opus_encoder,
        pcmData.data(),
        fs,
        opusData.data(),
        kMaxOpusFrame);

    if (encoded < 0) {
        LOGE("opus_encode failed: %d", encoded);
        return nullptr;
    }

    jbyteArray result = env->NewByteArray(encoded);
    if (result) {
        env->SetByteArrayRegion(result, 0, encoded,
                                reinterpret_cast<const jbyte*>(opusData.data()));
    }
    return result;
}

JNIEXPORT jshortArray JNICALL
Java_com_nevo_voip_core_audio_NativeAudioEngine_nativeDecodeOpus(
    JNIEnv* env, jclass cls, jbyteArray opus, jint frameSize) {
    (void)cls;

    if (!g_audio_initialized || !g_opus_decoder) {
        LOGE("decodeOpus: audio not initialized");
        return nullptr;
    }

    if (!opus) {
        LOGE("decodeOpus: null Opus array");
        return nullptr;
    }

    if (!g_opus_decoder) {
        LOGE("decodeOpus: decoder was destroyed");
        return nullptr;
    }

    jsize opusLen = env->GetArrayLength(opus);
    if (opusLen <= 0) {
        LOGE("decodeOpus: empty packet");
        return nullptr;
    }
    if (opusLen > 4000) {
        LOGE("decodeOpus: packet too large (%d bytes)", opusLen);
        return nullptr;
    }

    std::vector<uint8_t> opusData(static_cast<size_t>(opusLen));
    env->GetByteArrayRegion(opus, 0, opusLen,
                            reinterpret_cast<jbyte*>(opusData.data()));

    int fs = static_cast<int>(frameSize);
    if (fs <= 0) {
        fs = g_sample_rate / 50;
    }

    constexpr int kMaxFrameSamples = 5760;
    int maxSamples = fs > kMaxFrameSamples ? fs : kMaxFrameSamples;
    maxSamples *= g_channels;

    std::vector<opus_int16> pcmData(static_cast<size_t>(maxSamples), 0);

    int decoded = opus_decode(
        g_opus_decoder,
        opusData.data(),
        static_cast<opus_int32>(opusLen),
        pcmData.data(),
        fs,
        0);

    if (decoded < 0) {
        LOGE("opus_decode failed: %d", decoded);
        return nullptr;
    }

    jshortArray result = env->NewShortArray(decoded * g_channels);
    if (result) {
        env->SetShortArrayRegion(result, 0, decoded * g_channels,
                                 pcmData.data());
    }
    return result;
}

JNIEXPORT jbyteArray JNICALL
Java_com_nevo_voip_core_audio_NativeAudioEngine_nativeMixAudio(
    JNIEnv* env, jclass cls, jobjectArray audioFrames, jfloatArray volumes) {
    (void)cls;

    if (!audioFrames || !volumes) {
        LOGE("mixAudio: null argument");
        return nullptr;
    }

    jsize frameCount = env->GetArrayLength(audioFrames);
    jsize volCount = env->GetArrayLength(volumes);

    if (frameCount == 0) {
        LOGW("mixAudio: no frames to mix");
        return nullptr;
    }

    std::vector<float> volBuf(static_cast<size_t>(volCount));
    env->GetFloatArrayRegion(volumes, 0, volCount, volBuf.data());

    std::vector<std::vector<opus_int16>> pcmFrames(static_cast<size_t>(frameCount));
    jsize maxLen = 0;

    for (jsize i = 0; i < frameCount; i++) {
        jshortArray frame = static_cast<jshortArray>(
            env->GetObjectArrayElement(audioFrames, i));
        if (!frame) {
            LOGE("mixAudio: null frame at index %d", i);
            return nullptr;
        }
        jsize len = env->GetArrayLength(frame);
        pcmFrames[static_cast<size_t>(i)].resize(static_cast<size_t>(len));
        env->GetShortArrayRegion(frame, 0, len,
                                 pcmFrames[static_cast<size_t>(i)].data());
        env->DeleteLocalRef(frame);
        if (len > maxLen) {
            maxLen = len;
        }
    }

    std::vector<opus_int16> mixed(static_cast<size_t>(maxLen), 0);

    for (jsize sampleIdx = 0; sampleIdx < maxLen; sampleIdx++) {
        float sum = 0.0f;
        for (jsize f = 0; f < frameCount; f++) {
            float vol = (f < volCount) ? volBuf[static_cast<size_t>(f)] : 1.0f;
            const auto& frame = pcmFrames[static_cast<size_t>(f)];
            if (sampleIdx < static_cast<jsize>(frame.size())) {
                sum += static_cast<float>(frame[static_cast<size_t>(sampleIdx)]) * vol;
            }
        }
        sum = std::max(-32768.0f, std::min(32767.0f, sum));
        mixed[static_cast<size_t>(sampleIdx)] = static_cast<opus_int16>(static_cast<int>(sum));
    }

    jbyteArray result = env->NewByteArray(
        static_cast<jsize>(mixed.size() * sizeof(opus_int16)));
    if (result) {
        env->SetByteArrayRegion(
            result, 0,
            static_cast<jsize>(mixed.size() * sizeof(opus_int16)),
            reinterpret_cast<const jbyte*>(mixed.data()));
    }
    return result;
}

JNIEXPORT void JNICALL
Java_com_nevo_voip_core_audio_NativeAudioEngine_nativeJitterBufferPush(
    JNIEnv* env, jclass cls, jbyteArray packet, jint timestamp) {
    (void)cls;

    if (!packet) {
        LOGE("jitterBufferPush: null packet");
        return;
    }

    jsize len = env->GetArrayLength(packet);
    if (len <= 0) {
        LOGW("jitterBufferPush: empty packet");
        return;
    }

    if (g_jitter_buffer.size() >= kMaxJitterPackets) {
        LOGD("jitterBufferPush: buffer full, dropping oldest packet");
        g_jitter_buffer.erase(g_jitter_buffer.begin());
    }

    JitterPacket jp;
    jp.data.resize(static_cast<size_t>(len));
    env->GetByteArrayRegion(packet, 0, len,
                            reinterpret_cast<jbyte*>(jp.data.data()));
    jp.timestamp = static_cast<uint32_t>(timestamp);

    auto it = std::lower_bound(
        g_jitter_buffer.begin(), g_jitter_buffer.end(), jp,
        [](const JitterPacket& a, const JitterPacket& b) {
            return a.timestamp < b.timestamp;
        });
    g_jitter_buffer.insert(it, std::move(jp));
}

JNIEXPORT jobject JNICALL
Java_com_nevo_voip_core_audio_NativeAudioEngine_nativeJitterBufferPop(
    JNIEnv* env, jclass cls) {
    if (g_jitter_buffer.empty()) {
        return nullptr;
    }

    JitterPacket jp = std::move(g_jitter_buffer.front());
    g_jitter_buffer.erase(g_jitter_buffer.begin());

    jclass pairClass = env->FindClass("kotlin/Pair");
    if (!pairClass) {
        LOGE("jitterBufferPop: failed to find kotlin.Pair");
        return nullptr;
    }

    jmethodID ctor = env->GetMethodID(pairClass, "<init>",
        "(Ljava/lang/Object;Ljava/lang/Object;)V");
    if (!ctor) {
        LOGE("jitterBufferPop: failed to find constructor");
        return nullptr;
    }

    jbyteArray byteArray = env->NewByteArray(
        static_cast<jsize>(jp.data.size()));
    if (byteArray) {
        env->SetByteArrayRegion(
            byteArray, 0, static_cast<jsize>(jp.data.size()),
            reinterpret_cast<const jbyte*>(jp.data.data()));
    }

    jclass intClass = env->FindClass("java/lang/Integer");
    jmethodID intCtor = env->GetMethodID(intClass, "<init>", "(I)V");
    jobject timestampObj = env->NewObject(intClass, intCtor,
                                          static_cast<jint>(jp.timestamp));

    jobject pair = env->NewObject(pairClass, ctor, byteArray, timestampObj);

    if (byteArray) env->DeleteLocalRef(byteArray);
    if (timestampObj) env->DeleteLocalRef(timestampObj);
    env->DeleteLocalRef(intClass);
    env->DeleteLocalRef(pairClass);

    return pair;
}

} // extern "C"