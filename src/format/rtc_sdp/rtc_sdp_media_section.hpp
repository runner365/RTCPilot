#ifndef RTC_SDP_MEDIA_SECTION_HPP
#define RTC_SDP_MEDIA_SECTION_HPP
#include "rtc_sdp_pub.hpp"
#include "utils/stringex.hpp"
#include "utils/json.hpp"
#include "utils/av/av.hpp"

#include <string>
#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <map>
#include <memory>

namespace cpp_streamer
{

class RtcSdpMediaCodec
{
public:
    RtcSdpMediaCodec() = default;
    ~RtcSdpMediaCodec() {
    }

public:
    nlohmann::json Dump() {
        nlohmann::json ret_json = nlohmann::json::object();
        ret_json["codec_name"] = codec_name_;
        ret_json["is_rtx"] = is_rtx_;
        ret_json["payload_type"] = payload_type_;
        if (rtx_payload_type_ > 0) {
            ret_json["rtx_payload_type"] = rtx_payload_type_;
        }
        ret_json["rate"] = rate_;
        if (channel_ > 0) {
            ret_json["channel"] = channel_;
        }

        if (!rtcp_features_.empty()) {
            auto json_array = nlohmann::json::array();
            for (auto feature : rtcp_features_) {
                json_array.push_back(feature);
            }
            ret_json["rtcp_features"] = json_array;
        }
        ret_json["fmtp_param"] = fmtp_param_;

        if (h264_fmtp_param_) {
            ret_json["h264_fmtp_param"] = h264_fmtp_param_->Dump();
        }
        if (av1_fmtp_param_) {
            ret_json["av1_fmtp_param"] = av1_fmtp_param_->Dump();
        }
        if (vp9_fmtp_param_) {
            ret_json["vp9_fmtp_param"] = vp9_fmtp_param_->Dump();
        }
        if (opus_fmtp_param_) {
            ret_json["opus_fmtp_param"] = opus_fmtp_param_->Dump();
        }
        return ret_json;
    }
    int GenH264FmtpParam() {
        // level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f
        std::vector<std::string> fmtp_parts_;
        int ret = StringSplit(fmtp_param_, ";", fmtp_parts_);
        if (ret <= 0 || fmtp_parts_.empty()) {
            return -1;
        }
        h264_fmtp_param_ = std::make_shared<H264CodecFmtpParam>();
        for (auto part : fmtp_parts_) {
            std::vector<std::string> key_value;
            ret = StringSplit(part, "=", key_value);
            if (ret != 2) {
                continue;
            }
            if (key_value[0] == "level-asymmetry-allowed") {
                h264_fmtp_param_->level_asymmetry_allowed_ =
                    std::stoi(key_value[1]);
            } else if (key_value[0] == "packetization-mode") {
                h264_fmtp_param_->packetization_mode_ = std::stoi(key_value[1]);
            } else if (key_value[0] == "profile-level-id") {
                h264_fmtp_param_->profile_level_id_ = key_value[1];
            }
        }

        return 0;
    }
    int GenAV1FmtpParam() {
        // level-idx=5;profile=0;tier=0
        std::vector<std::string> fmtp_parts_;
        int ret = StringSplit(fmtp_param_, ";", fmtp_parts_);

        if (ret <= 0 || fmtp_parts_.empty()) {
            return -1;
        }
        av1_fmtp_param_ = std::make_shared<AV1CodecFmtpParam>();
        for (auto part : fmtp_parts_) {
            std::vector<std::string> key_value;
            ret = StringSplit(part, "=", key_value);
            if (ret != 2) {
                continue;
            }
            if (key_value[0] == "level-idx") {
                av1_fmtp_param_->level_idx_ = std::stoi(key_value[1]);
            } else if (key_value[0] == "profile") {
                av1_fmtp_param_->profile_ = std::stoi(key_value[1]);
            } else if (key_value[0] == "tier") {
                av1_fmtp_param_->tier_ = std::stoi(key_value[1]);
            }
        }
        return 0;
    }
    int GenVP9FmtpParam() {
        // profile-id=0
        std::vector<std::string> fmtp_parts_;
        int ret = StringSplit(fmtp_param_, ";", fmtp_parts_);

        if (ret <= 0 || fmtp_parts_.empty()) {
            return -1;
        }

        vp9_fmtp_param_ = std::make_shared<VP9CodecFmtpParam>();

        for (auto part : fmtp_parts_) {
            std::vector<std::string> key_value;
            ret = StringSplit(part, "=", key_value);
            if (ret != 2) {
                continue;
            }
            if (key_value[0] == "profile-id") {
                vp9_fmtp_param_->profile_id_ = std::stoi(key_value[1]);
            }
        }
        return 0;
    }
    int GenOpusFmtpParam() {
        // minptime=10;useinbandfec=1
        std::vector<std::string> fmtp_parts_;
        int ret = StringSplit(fmtp_param_, ";", fmtp_parts_);

        if (ret <= 0 || fmtp_parts_.empty()) {
            return -1;
        }

        opus_fmtp_param_ = std::make_shared<OpusCodecFmtpParam>();

        for (auto part : fmtp_parts_) {
            std::vector<std::string> key_value;
            ret = StringSplit(part, "=", key_value);
            if (ret != 2) {
                continue;
            }
            if (key_value[0] == "minptime") {
                opus_fmtp_param_->minptime_ = std::stoi(key_value[1]);
            } else if (key_value[0] == "useinbandfec") {
                opus_fmtp_param_->useinbandfec_ = std::stoi(key_value[1]);
            } else if (key_value[0] == "maxaveragebitrate") {
                opus_fmtp_param_->maxaveragebitrate_ = std::stoi(key_value[1]);
            } else if (key_value[0] == "stereo") {
                opus_fmtp_param_->stereo_ = std::stoi(key_value[1]);
            } else if (key_value[0] == "sprop-stereo") {
                opus_fmtp_param_->sprop_stereo_ = std::stoi(key_value[1]);
            }
        }
        return 0;
    }
public:
    std::string codec_name_;
    bool is_rtx_ = false;
    int payload_type_ = -1;
    int rtx_payload_type_ = -1;
    int rate_ = 0;
    int channel_ = 0;
    std::vector<std::string> rtcp_features_;
    std::string fmtp_param_;
    bool fmtp_parsed_ = false;

    std::shared_ptr<H264CodecFmtpParam> h264_fmtp_param_ = nullptr;
    std::shared_ptr<AV1CodecFmtpParam> av1_fmtp_param_ = nullptr;
    std::shared_ptr<VP9CodecFmtpParam> vp9_fmtp_param_ = nullptr;
    std::shared_ptr<OpusCodecFmtpParam> opus_fmtp_param_ = nullptr;
};

class RtcSdpMediaSection
{
public:
    RtcSdpMediaSection() = default;
    ~RtcSdpMediaSection() = default;

public:
    nlohmann::json Dump() {
        nlohmann::json ret_json = nlohmann::json::object();

        ret_json["media_type"] = media_type_ == MEDIA_AUDIO_TYPE ? "audio" :
                                media_type_ == MEDIA_VIDEO_TYPE ? "video" : "unknown";
        ret_json["mid"] = mid_;
        ret_json["direction"] = direction_ == DIRECTION_SENDONLY ? "sendonly" :
                               direction_ == DIRECTION_RECVONLY ? "recvonly" :
                               direction_ == DIRECTION_SENDRECV ? "sendrecv" : "unknown";
        if (!media_codecs_.empty()) {
            auto codecs_array = nlohmann::json::array();
            for (const auto& codec_pair : media_codecs_) {
                codecs_array.push_back(codec_pair.second->Dump());
            }
            ret_json["media_codecs"] = codecs_array;
        }

        if (!ssrc_infos_.empty()) {
            auto ssrcs_array = nlohmann::json::array();
            for (const auto& ssrc_pair : ssrc_infos_) {
                nlohmann::json ssrc_json = nlohmann::json::object();
                ssrc_json["ssrc"] = ssrc_pair.second->ssrc_;
                ssrc_json["is_main"] = ssrc_pair.second->is_main_;
                ssrc_json["cname"] = ssrc_pair.second->cname_;
                ssrc_json["stream_id"] = ssrc_pair.second->stream_id_;
                ssrcs_array.push_back(ssrc_json);
            }
            ret_json["ssrc_infos"] = ssrcs_array;
        }

        if (!extensions_.empty()) {
            auto exts_array = nlohmann::json::array();
            for (const auto& ext_pair : extensions_) {
                nlohmann::json ext_json = nlohmann::json::object();
                ext_json["id"] = ext_pair.second->id_;
                ext_json["uri"] =  ext_pair.second->uri_;
                exts_array.push_back(ext_json);
            }
            ret_json["extensions"] = exts_array;
        }

        return ret_json;
    }
public:
    MEDIA_PKT_TYPE media_type_ = MEDIA_UNKNOWN_TYPE;
    int mid_ = -1;
    DirectionType direction_ = DIRECTION_UNKNOWN;
    std::string cname_;
    std::map<int, std::shared_ptr<RtcSdpMediaCodec>> media_codecs_;// key: payload_type, value: codec info
    std::map<uint32_t, std::shared_ptr<SsrcInfo>> ssrc_infos_;// key: ssrc, value: ssrc info
    std::map<int, std::shared_ptr<ExtensionInfo>> extensions_;// key: id, value: extension info
};

}
#endif // RTC_SDP_MEDIA_SECTION_HPP