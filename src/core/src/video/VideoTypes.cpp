/**
 * @file VideoTypes.cpp
 * @brief 视频通话共享类型的 Protobuf 转换实现
 */

#include "nevo/core/video/VideoTypes.h"

#include "control.pb.h"

namespace nevo::video {

CodecCapability codecCapabilityFromProto(const control::VideoCodecCapability& proto) {
    return CodecCapability{
        .codec = static_cast<VideoCodec>(proto.codec()),
        .max_width = proto.max_width(),
        .max_height = proto.max_height(),
        .max_fps = proto.max_fps(),
        .hardware_accelerated = proto.hardware_accelerated(),
    };
}

void codecCapabilityToProto(const CodecCapability& cap, control::VideoCodecCapability* proto) {
    proto->set_codec(static_cast<uint32_t>(cap.codec));
    proto->set_max_width(cap.max_width);
    proto->set_max_height(cap.max_height);
    proto->set_max_fps(cap.max_fps);
    proto->set_hardware_accelerated(cap.hardware_accelerated);
}

VideoProfile videoProfileFromProto(const control::VideoProfile& proto) {
    return VideoProfile{
        .codec = static_cast<VideoCodec>(proto.codec()),
        .width = proto.width(),
        .height = proto.height(),
        .fps = proto.fps(),
        .target_bitrate_kbps = proto.target_bitrate_kbps(),
    };
}

void videoProfileToProto(const VideoProfile& profile, control::VideoProfile* proto) {
    proto->set_codec(static_cast<uint32_t>(profile.codec));
    proto->set_width(profile.width);
    proto->set_height(profile.height);
    proto->set_fps(profile.fps);
    proto->set_target_bitrate_kbps(profile.target_bitrate_kbps);
}

VideoCallEndReason videoCallEndReasonFromProto(uint32_t reason) {
    switch (reason) {
        case 1: return VideoCallEndReason::LocalHangup;
        case 2: return VideoCallEndReason::RemoteHangup;
        case 3: return VideoCallEndReason::RemoteRejected;
        case 4: return VideoCallEndReason::RemoteBusy;
        case 5: return VideoCallEndReason::Timeout;
        case 6: return VideoCallEndReason::NetworkError;
        case 7: return VideoCallEndReason::PeerDisconnected;
        default: return VideoCallEndReason::Unknown;
    }
}

} // namespace nevo::video
