#include "media_puller.hpp"
#include "utils/uuid.hpp"
#include "utils/event_log.hpp"

extern std::unique_ptr<cpp_streamer::EventLog> g_rtc_stream_log;

namespace cpp_streamer {

MediaPuller::MediaPuller(const RtpSessionParam& param, 
    const std::string& room_id, 
    const std::string& puller_user_id, 
    const std::string& pusher_user_id,
    const std::string& pusher_id,
    const std::string& session_id,
    TransportSendCallbackI* cb,
    uv_loop_t* loop, 
    Logger* logger) :
        param_(param),
        loop_(loop),
        logger_(logger),
        room_id_(room_id),
        puller_user_id_(puller_user_id),
        pusher_user_id_(pusher_user_id),
        session_id_(session_id),
        cb_(cb)
{
    puller_id_ = cpp_streamer::UUID::MakeUUID2();
    pusher_id_ = pusher_id;
    
    LogInfof(logger_, "MediaPuller construct, room_id:%s, pusher_id:%s, puller_user_id:%s, pusher_user_id:%s, session_id:%s, puller_id:%s, ssrc:%u, media_type:%s",
        room_id_.c_str(), pusher_id_.c_str(), puller_user_id_.c_str(), pusher_user_id_.c_str(), session_id_.c_str(), puller_id_.c_str(),
        param_.ssrc_, avtype_tostring(param_.av_type_).c_str());
}

MediaPuller::~MediaPuller() 
{
    LogInfof(logger_, "MediaPuller destruct, room_id:%s, puller_user_id:%s, pusher_user_id:%s, session_id:%s, puller_id:%s, ssrc:%u, payload_type:%u, media_type:%s",
        room_id_.c_str(), puller_user_id_.c_str(), pusher_user_id_.c_str(), session_id_.c_str(), puller_id_.c_str(),
        param_.ssrc_, param_.payload_type_, avtype_tostring(param_.av_type_).c_str());
}

void MediaPuller::CreateRtpSendSession() {
    rtp_send_session_ = std::make_unique<RtpSendSession>(param_, 
        room_id_, puller_user_id_, pusher_user_id_, cb_, loop_, logger_);
}

void MediaPuller::OnTransportSendRtp(RtpPacket* in_pkt) {
    if (in_pkt->GetPayloadLength() == 0) {
        return;
    }
    if (cb_) {
        if (!cb_->IsConnected()) {
            return;
        }
    }
    RtpPacket* rtp_pkt = in_pkt;
    if (rtp_pkt->HasExtension()) {
        if (param_.tcc_ext_id_ > 0) {
            auto tcc_seq_extern_id = rtp_pkt->GetTccExtensionId();
            bool r1 = rtp_pkt->UpdateWideSeqExternId(param_.tcc_ext_id_);
            if (!r1) {
                LogErrorf(logger_, "puller update tcc extern id error, new extern_id:%d, old extern_id:%d",
                    param_.tcc_ext_id_, tcc_seq_extern_id);
            }
        }
        if (param_.abs_send_time_ext_id_ > 0) {
            auto old_abs_send_time_ext_id = rtp_pkt->GetAbsTimeExtensionId();
            bool r1 = rtp_pkt->UpdateAbsTimeExternId(param_.abs_send_time_ext_id_);
            if (!r1) {
                LogErrorf(logger_, "puller update abs time extern id error, new extern_id:%d, old extern_id:%d",
                    param_.abs_send_time_ext_id_, old_abs_send_time_ext_id);
            }
        }
    }

    bool r = rtp_send_session_->SendRtpPacket(rtp_pkt);
    if (!r) {
        return;
    }

    int origin_payload_type = rtp_pkt->GetPayloadType();
    uint32_t origin_ssrc = rtp_pkt->GetSsrc();
    // modify payload type to puller's payload type
    rtp_pkt->SetPayloadType(param_.payload_type_);
    rtp_pkt->SetSsrc(param_.ssrc_);
    cb_->OnTransportSendRtp(rtp_pkt->GetData(), rtp_pkt->GetDataLength());
    // restore payload type
    rtp_pkt->SetPayloadType(origin_payload_type);
    rtp_pkt->SetSsrc(origin_ssrc);
}

void MediaPuller::OnTimer(int64_t now_ms) {
    if (last_statics_ms_ < 0) {
        last_statics_ms_ = now_ms;
        return;
    } else {
        if (now_ms - last_statics_ms_ > 5000) {
            auto& send_statics = rtp_send_session_->GetSendStatics();
            size_t pps = 0;
            size_t kbits_per_sec = send_statics.BytesPerSecond(now_ms, pps) * 8 / 1000;

            LogInfof(logger_, "<----media puller SendStatics, room_id:%s, \
puller_user_id:%s, pusher_user_id:%s, \
ssrc:%u, media_type:%s, send_kbits:%zu, send_pps:%zu",
                room_id_.c_str(), puller_user_id_.c_str(), pusher_user_id_.c_str(),
                param_.ssrc_, avtype_tostring(param_.av_type_).c_str(),
                kbits_per_sec, pps);
            // event log
            if (g_rtc_stream_log) {
                json evt_data;
                evt_data["room_id"] = room_id_;
                evt_data["puller_user_id"] = puller_user_id_;
                evt_data["pusher_user_id"] = pusher_user_id_;
                evt_data["ssrc"] = param_.ssrc_;
                evt_data["media_type"] = avtype_tostring(param_.av_type_);
                evt_data["send_kbps"] = kbits_per_sec;
                evt_data["send_pps"] = pps;
                g_rtc_stream_log->Log("puller_send", evt_data);
            }
            last_statics_ms_ = now_ms;
        }
    }

    rtp_send_session_->OnTimer(now_ms);
}

int MediaPuller::HandleRtcpRrBlock(RtcpRrBlockInfo& rr_block) {
    return rtp_send_session_->RecvRtcpRrBlock(rr_block);
}

int MediaPuller::HandleRtcpFbNack(RtcpFbNack* nack_pkt) {
    if (!nack_pkt) {
        return -1;
    }
    //don't delete nack_pkt here, delete in caller function
    int ret = rtp_send_session_->RecvRtcpFbNack(nack_pkt);
    if (ret != 0) {
        LogErrorf(logger_, "MediaPuller HandleRtcpFbNack failed, room_id:%s, puller_user_id:%s, pusher_user_id:%s, session_id:%s, puller_id:%s, ssrc:%u",
            room_id_.c_str(), puller_user_id_.c_str(), pusher_user_id_.c_str(), session_id_.c_str(), puller_id_.c_str(), param_.ssrc_);
        return ret;
    }
    return 0;
}

} // namespace cpp_streamer