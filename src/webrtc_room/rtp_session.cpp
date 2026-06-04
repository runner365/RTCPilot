#include "rtp_session.hpp"
#include "utils/timeex.hpp"

#include <uv.h>

namespace cpp_streamer {

std::vector<RtpSessionParam> GetRtpSessionParamsFromSdp(const RtcSdp& sdp) {
    std::vector<RtpSessionParam> params;

    for (const auto& media_section : sdp.media_sections_) {
        RtpSessionParam param;
        param.mid_ = media_section.first;
        if (media_section.second->media_type_ == MEDIA_PKT_TYPE::MEDIA_AUDIO_TYPE) {
            param.av_type_ = MEDIA_AUDIO_TYPE;
        } else if (media_section.second->media_type_ == MEDIA_PKT_TYPE::MEDIA_VIDEO_TYPE) {
            param.av_type_ = MEDIA_VIDEO_TYPE;
        } else {
            continue;
        }
        for (auto ext_item : media_section.second->extensions_) {
            if (ext_item.second->uri_ == "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01") {
                param.tcc_ext_id_ = ext_item.second->id_;
            } else if (ext_item.second->uri_ == "urn:ietf:params:rtp-hdrext:sdes:mid") {
                param.mid_ext_id_ = ext_item.second->id_;
            } else if (ext_item.second->uri_ == "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time") {
                param.abs_send_time_ext_id_ = ext_item.second->id_;
            }
        }
        //read format/rtc_sdp/rtc_sdp.hpp for details and set parameters
        for (const auto& ssrc_info_pair : media_section.second->ssrc_infos_) {
            const auto& ssrc_info = ssrc_info_pair.second;
            if (ssrc_info->is_main_) {
                param.ssrc_ = ssrc_info->ssrc_;
            } else {
                param.rtx_ssrc_ = ssrc_info->ssrc_;
            }
        }
        for (const auto& codec_pair : media_section.second->media_codecs_) {
            const auto& codec = codec_pair.second;
            if (!codec->is_rtx_) {
                param.payload_type_ = static_cast<uint8_t>(codec->payload_type_);
                param.clock_rate_ = static_cast<uint32_t>(codec->rate_);
            } else {
                param.rtx_payload_type_ = static_cast<uint8_t>(codec->payload_type_);
                continue;
            }
            param.codec_name_ = codec->codec_name_;
            param.channel_ = codec->channel_;
            param.fmtp_param_ = codec->fmtp_param_;
            param.rtcp_features_ = codec->rtcp_features_;
            for (const std::string& rtcp_fb_str : codec->rtcp_features_) {
                if (rtcp_fb_str == "nack") {
					param.use_nack_ = true;
                }
				size_t pos = rtcp_fb_str.find("pli");
                if (pos != std::string::npos) {
					param.key_request_ = true;
                }
				pos = rtcp_fb_str.find("fir");
                if (pos != std::string::npos) {
                    param.key_request_ = true;
                }
            }
        }

        params.push_back(param);
    }
    return params;
}

RtpSession::RtpSession(const RtpSessionParam& param,
    const std::string& room_id,
    const std::string& user_id,
    TransportSendCallbackI* cb,
    uv_loop_t* loop, Logger* logger):param_(param),
    logger_(logger),
    room_id_(room_id),
    user_id_(user_id),
    transport_cb_(cb)
{
    LogInfof(logger_, "RtpSession construct, room_id:%s, user_id:%s, ssrc:%u, payload_type:%u",
        room_id_.c_str(), user_id_.c_str(), param_.ssrc_, param_.payload_type_);
}

RtpSession::~RtpSession() {
    LogInfof(logger_, "RtpSession destruct, room_id:%s, user_id:%s, ssrc:%u",
        room_id_.c_str(), user_id_.c_str(), param_.ssrc_);
}

void RtpSession::InitSeq(uint16_t seq) {
    base_seq_ = seq;
    max_seq_  = seq;
    bad_seq_  = RTP_SEQ_MOD + 1;   /* so seq == bad_seq is false */
    cycles_   = 0;
}

bool RtpSession::UpdateSeq(RtpPacket* rtp_pkt) {
    const int MAX_DROPOUT    = 3000;
    const int MAX_MISORDER   = 1500;
    
    const uint16_t seq = rtp_pkt->GetSeq();

    uint16_t udelta = seq - max_seq_;

    if (udelta < MAX_DROPOUT) {
        /* in order, with permissible gap */
        if (seq < max_seq_) {
            /*
             * Sequence number wrapped - count another 64K cycle.
             */
            cycles_ += RTP_SEQ_MOD;
        }
        max_seq_ = seq;
    } else if (udelta <= RTP_SEQ_MOD - MAX_MISORDER) {
           /* the sequence number made a very large jump */
           if (seq == bad_seq_) {
               /*
                * Two sequential packets -- assume that the other side
                * restarted without telling us so just re-sync
                * (i.e., pretend this was the first packet).
                */
               InitSeq(seq);
           }
           else {
               bad_seq_= (seq + 1) & (RTP_SEQ_MOD-1);
               discard_count_++;
               return false;
           }
    } else {
        /* duplicate or reordered packet */
    }
    return true;
}

int64_t RtpSession::GetExpectedPackets() const {
    return cycles_ + max_seq_ - bad_seq_ + 1;
}

} // namespace cpp_streamer