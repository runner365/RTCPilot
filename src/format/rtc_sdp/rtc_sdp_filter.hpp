#ifndef RTC_SDP_FILTER_HPP
#define RTC_SDP_FILTER_HPP
#include "rtc_sdp_pub.hpp"
#include "rtc_sdp_media_section.hpp"
#include "utils/stringex.hpp"
#include "utils/json.hpp"
#include "utils/av/av.hpp"

#include <string>
#include <stdint.h>
#include <stddef.h>
#include <vector>

namespace cpp_streamer
{

class CodecFilter
{
public:
    CodecFilter() = default;
    ~CodecFilter() = default;

public:
    MEDIA_PKT_TYPE media_type_ = MEDIA_UNKNOWN_TYPE;
    RtcSdpMediaCodec codec_;
};

class SdpFilter
{
public:
    SdpFilter() = default;
    ~SdpFilter() = default;

public:
    bool IsExtMapFilter(const std::string& input_ext) {
        for (const auto& ext : exts_) {
            if (ext == input_ext) {
                return true;
            }
        }
        return false;
    }

    bool IsCodecFilter(MEDIA_PKT_TYPE media_type, std::shared_ptr<RtcSdpMediaCodec> input_codec) {
        bool found = false;
        for (const auto& codec_filter : codecs_) {
            if (codec_filter.media_type_ != media_type) {
                continue;
            }
            
            if (codec_filter.codec_.codec_name_ != input_codec->codec_name_) {
                continue;
            }
            if (input_codec->h264_fmtp_param_ != nullptr &&
                codec_filter.codec_.h264_fmtp_param_ != nullptr) {
                if (input_codec->h264_fmtp_param_->profile_level_id_ !=
                    codec_filter.codec_.h264_fmtp_param_->profile_level_id_) {
                    continue;
                }
                if (input_codec->h264_fmtp_param_->packetization_mode_ !=
                    codec_filter.codec_.h264_fmtp_param_->packetization_mode_) {
                    continue;
                }
                if (input_codec->h264_fmtp_param_->level_asymmetry_allowed_ != 
                    codec_filter.codec_.h264_fmtp_param_->level_asymmetry_allowed_) {
                    continue;
                }
                found = true;
                break;
            }
            if (input_codec->vp9_fmtp_param_ != nullptr &&
                codec_filter.codec_.vp9_fmtp_param_ != nullptr) {
                if (input_codec->vp9_fmtp_param_->profile_id_ !=
                    codec_filter.codec_.vp9_fmtp_param_->profile_id_) {
                    continue;
                }
                found = true;
                break;
            }
            if (input_codec->av1_fmtp_param_ != nullptr &&
                codec_filter.codec_.av1_fmtp_param_ != nullptr) {
                if (input_codec->av1_fmtp_param_->profile_ !=
                    codec_filter.codec_.av1_fmtp_param_->profile_) {
                    continue;
                }
                if (input_codec->av1_fmtp_param_->level_idx_ !=
                    codec_filter.codec_.av1_fmtp_param_->level_idx_) {
                    continue;
                }
                if (input_codec->av1_fmtp_param_->tier_ !=
                    codec_filter.codec_.av1_fmtp_param_->tier_) {
                    continue;
                }
                found = true;
                break;
            }
            if (input_codec->opus_fmtp_param_ != nullptr &&
                codec_filter.codec_.opus_fmtp_param_ != nullptr) {
                if (input_codec->opus_fmtp_param_->minptime_ !=
                    codec_filter.codec_.opus_fmtp_param_->minptime_) {
                    //continue;
                }
                if (input_codec->opus_fmtp_param_->useinbandfec_ !=
                    codec_filter.codec_.opus_fmtp_param_->useinbandfec_) {
                    continue;
                }
                if (input_codec->opus_fmtp_param_->maxaveragebitrate_ > 0 && 
                    input_codec->opus_fmtp_param_->maxaveragebitrate_ !=
                    codec_filter.codec_.opus_fmtp_param_->maxaveragebitrate_) {
                    continue;
                }
                if (input_codec->opus_fmtp_param_->stereo_ > 0 && input_codec->opus_fmtp_param_->stereo_ !=
                    codec_filter.codec_.opus_fmtp_param_->stereo_) {
                    continue;
                }
                if (input_codec->opus_fmtp_param_->sprop_stereo_ > 0 &&
                    input_codec->opus_fmtp_param_->sprop_stereo_ !=
                    codec_filter.codec_.opus_fmtp_param_->sprop_stereo_) {
                    continue;
                }
                found = true;
                break;
            }
        }
        return found;
    }
public:
    std::vector<std::string> exts_;
    std::vector<CodecFilter> codecs_;
};

extern SdpFilter g_sdp_answer_filter;

void InitSdpFilter();

}
#endif