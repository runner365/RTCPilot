#include "media_pusher.hpp"
#include "utils/uuid.hpp"
#include "utils/event_log.hpp"
#include "net/rtprtcp/rtcp_pspli.hpp"
#include "config/config.hpp"
#include <assert.h>

extern std::unique_ptr<cpp_streamer::EventLog> g_rtc_stream_log;

namespace cpp_streamer {

MediaPusher::MediaPusher(const RtpSessionParam& param, 
    const std::string& room_id, 
    const std::string& user_id, 
    const std::string& session_id,
    TransportSendCallbackI* cb,
    PacketFromRtcPusherCallbackI* packet2room_cb,
    uv_loop_t* loop, 
    Logger* logger) :
        param_(param),
        loop_(loop),
        logger_(logger),
        room_id_(room_id),
        user_id_(user_id),
        session_id_(session_id),
        cb_(cb),
        packet2room_cb_(packet2room_cb)
{
    pusher_id_ = cpp_streamer::UUID::MakeUUID2();
    media_type_ = param_.av_type_;

    LogInfof(logger_, "MediaPusher construct, room_id:%s, user_id:%s, session_id:%s, pusher_id:%s, \
ssrc:%u, payload_type:%u, media_type:%s",
        room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), pusher_id_.c_str(),
        param_.ssrc_, param_.payload_type_, avtype_tostring(media_type_).c_str());
}

MediaPusher::~MediaPusher() 
{
    LogInfof(logger_, "MediaPusher destruct, room_id:%s, user_id:%s, session_id:%s, \
pusher_id:%s, ssrc:%u, payload_type:%u, media_type:%s",
        room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), pusher_id_.c_str(),
        param_.ssrc_, param_.payload_type_, avtype_tostring(media_type_).c_str());
}

void MediaPusher::CreateRtpRecvSession() {
    // transport callback and loop are not available here, pass nullptr if not used
    auto rtp_recv_session = std::make_shared<RtpRecvSession>(param_, room_id_, user_id_, this, loop_, logger_);
    ssrc2sessions_[param_.ssrc_] = rtp_recv_session;
    if (param_.rtx_ssrc_ != 0) {
        rtxssrc2sessions_[param_.rtx_ssrc_] = rtp_recv_session;
    }
}

int MediaPusher::HandleRtpPacket(RtpPacket* rtp_pkt) {
    rtp_pkt->SetLogger(logger_);

    //clear mid extension in packet (zero out ext id, making it padding)
    if (param_.mid_ext_id_ > 0) {
        rtp_pkt->SetMidExtensionId(static_cast<uint8_t>(param_.mid_ext_id_));
    }
    if (param_.tcc_ext_id_ > 0) {
        rtp_pkt->SetTccExtensionId(param_.tcc_ext_id_);
    }
    if (param_.abs_send_time_ext_id_ > 0) {
        rtp_pkt->SetAbsTimeExtensionId(param_.abs_send_time_ext_id_);
    }
    uint32_t ssrc = rtp_pkt->GetSsrc();
    bool is_rtx = false;
    auto session = GetRtpRecvSession(rtp_pkt, is_rtx);
    if (!session) {
        LogErrorf(logger_, "MediaPusher HandleRtpPacket, no session for ssrc:%u, room_id:%s, user_id:%s",
            rtp_pkt->GetSsrc(), room_id_.c_str(), user_id_.c_str());
        return -1;
    }

    if (!is_rtx) {
        bool result = session->ReceiveRtpPacket(rtp_pkt);
        if (!result) {
            LogErrorf(logger_, "MediaPusher Handle RtpPacket failed for ssrc:%u, room_id:%s, user_id:%s",
                rtp_pkt->GetSsrc(), room_id_.c_str(), user_id_.c_str());
            return -1;
        }
        if (Config::Instance().voice_agent_cfg_.enable_ && param_.av_type_ == MEDIA_AUDIO_TYPE) {
            // forward to voice agent
            try {
                if (!voice_agent_ptr_) {
                    voice_agent_ptr_.reset(new VoiceAgent(room_id_, user_id_, param_.clock_rate_, voice_agent_cb_, loop_, logger_));
                }
                RtpPacket* rtp_pkt_copy = rtp_pkt->Clone();
                voice_agent_ptr_->PushRtpPacket(rtp_pkt_copy);
            } catch (const std::exception& e) {
                LogErrorf(logger_, "MediaPusher Handle RtpPacket forward to VoiceAgent exception:%s, \
room_id:%s, user_id:%s", e.what(), room_id_.c_str(), user_id_.c_str());
            }
        }
        
        uint8_t old_ext_id = 0;
        rtp_pkt->ClearMidExtension((uint8_t)param_.mid_, old_ext_id);
        LogDebugf(logger_, "MediaPusher clear mid extension, old_ext_id:%d, mid:%d",
            old_ext_id, param_.mid_);
        
        packet2room_cb_->OnRtpPacketFromRtcPusher(user_id_, session_id_, pusher_id_, rtp_pkt);
        return 0;
    }
    

    bool repeat = false;
    bool result = session->ReceiveRtxPacket(rtp_pkt, repeat);
    if (!result) {
        LogErrorf(logger_, "MediaPusher Handle RtpPacket failed for rtx ssrc:%u, room_id:%s, user_id:%s",
            rtp_pkt->GetSsrc(), room_id_.c_str(), user_id_.c_str());
        return -1;
    }
    if (repeat) {
        return 0;
    }
    if (rtp_pkt->GetSsrc() == ssrc) {
        return 0;
    }
    if (rtp_pkt->GetPayloadLength() == 0) {
        return 0;
    }
    
    uint8_t old_ext_id = 0;
    rtp_pkt->ClearMidExtension((uint8_t)param_.mid_, old_ext_id);
    LogDebugf(logger_, "MediaPusher clear mid extension, old_ext_id:%d, mid:%d",
        old_ext_id, param_.mid_);
    
    packet2room_cb_->OnRtpPacketFromRtcPusher(user_id_,session_id_, pusher_id_, rtp_pkt);
    return 0;
}

std::shared_ptr<RtpRecvSession> MediaPusher::GetRtpRecvSession(RtpPacket* rtp_pkt, bool& is_rtx) {
    uint32_t ssrc = rtp_pkt->GetSsrc();
    int payload_type = rtp_pkt->GetPayloadType();
    is_rtx = false;
    
    auto it = ssrc2sessions_.find(ssrc);
    if (it != ssrc2sessions_.end()) {
        is_rtx = false;
        return it->second;

    }
    auto rtx_it = rtxssrc2sessions_.find(ssrc);
    if (rtx_it != rtxssrc2sessions_.end()) {
        is_rtx = true;
        return rtx_it->second;
    }
    
    std::shared_ptr<RtpRecvSession> session = nullptr;
    for (auto& item : ssrc2sessions_) {
        if (item.second->GetRtpSessionParam().payload_type_ == payload_type) {
            session = item.second;
            is_rtx = false;
            break;
        }
    }
    if (session) {
        session->UpdateSSRC(ssrc);
        ssrc2sessions_[ssrc] = session;
        is_rtx = false;
        return session;
    }

    std::shared_ptr<RtpRecvSession> rtx_session = nullptr;
    for (auto& item : rtxssrc2sessions_) {
        if (item.second->GetRtpSessionParam().payload_type_ == payload_type) {
            rtx_session = item.second;
            is_rtx = true;
            break;
        }
    }
    if (rtx_session) {
        rtx_session->UpdateSSRC(ssrc);
        rtxssrc2sessions_[ssrc] = rtx_session;
        is_rtx = true;
        return rtx_session;
    }
    return nullptr;
}

int MediaPusher::HandleRtcpSrPacket(RtcpSrPacket* sr_pkt) {
    uint32_t ssrc = sr_pkt->GetSsrc();
    auto it = ssrc2sessions_.find(ssrc);
    if (it != ssrc2sessions_.end()) {
        return it->second->HandleRtcpSrPacket(sr_pkt);
    }
    LogErrorf(logger_, "MediaPusher HandleRtcpSrPacket, unknown ssrc:%u, room_id:%s, user_id:%s",
        ssrc, room_id_.c_str(), user_id_.c_str());
    return -1;
}

bool MediaPusher::IsConnected() {
    if (!cb_) {
        return false;
    }
    return cb_->IsConnected();
}

//implement TransportSendCallbackI
void MediaPusher::OnTransportSendRtp(uint8_t* data, size_t sent_size) {
    cb_->OnTransportSendRtp(data, sent_size);
}

void MediaPusher::OnTransportSendRtcp(uint8_t* data, size_t sent_size) {
    cb_->OnTransportSendRtcp(data, sent_size);
}

void MediaPusher::OnTimer(int64_t now_ms) {
    if (last_statics_ms_ == -1) {
        last_statics_ms_ = now_ms;
    } else {
        if (now_ms - last_statics_ms_ < 5000) {
            return;
        }
        last_statics_ms_ = now_ms;
        for (auto& it : ssrc2sessions_) {
            StreamStatics& stats = it.second->GetRecvStatics();
            size_t pps = 0;
            size_t bps = stats.BytesPerSecond(now_ms, pps);
            size_t kbps = bps * 8 / 1000;
            LogDebugf(logger_, "++++>media pusher RecvStatics, room_id:%s, user_id:%s, \
session_id:%s, pusher_id:%s, ssrc:%u, media_type:%s, recv_kbits:%zu, recv_pkt_count:%zu",
                room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), pusher_id_.c_str(),
                it.first, avtype_tostring(media_type_).c_str(),
                kbps, pps);
            //log to event log
            if (g_rtc_stream_log) {
                json evt_data;
                evt_data["room_id"] = room_id_;
                evt_data["user_id"] = user_id_;
                evt_data["session_id"] = session_id_;
                evt_data["pusher_id"] = pusher_id_;
                evt_data["ssrc"] = it.first;
                evt_data["media_type"] = avtype_tostring(media_type_);
                evt_data["recv_bps"] = kbps;
                evt_data["recv_pps"] = pps;
                g_rtc_stream_log->Log("pusher_recv", evt_data);
            }
        }
    }
 
    // request key frame every 3 seconds
    if (param_.av_type_ == MEDIA_PKT_TYPE::MEDIA_VIDEO_TYPE) {
        if (last_keyframe_request_ms_ < 0) {
            last_keyframe_request_ms_ = now_ms;
        } else {
            if (now_ms - last_keyframe_request_ms_ >= 8000) {
                RequestKeyFrame(param_.ssrc_);
            }
        }
    }
}


void MediaPusher::RequestKeyFrame(uint32_t ssrc) {
    if(ssrc != param_.ssrc_) {
        LogErrorf(logger_, "MediaPusher RequestKeyFrame ssrc mismatch, request ssrc:%u, session ssrc:%u, room_id:%s, user_id:%s",
            ssrc, param_.ssrc_, room_id_.c_str(), user_id_.c_str());
        ssrc = param_.ssrc_;
    }

    std::unique_ptr<RtcpPsPli> pspli_pkt = std::make_unique<RtcpPsPli>();

    last_keyframe_request_ms_ = now_millisec();
    pspli_pkt->SetSenderSsrc(0); //0 means server
    pspli_pkt->SetMediaSsrc(ssrc);
    
    LogInfof(logger_, "MediaPusher RequestKeyFrame, room_id:%s, user_id:%s, session_id:%s, pusher_id:%s, ssrc:%u",
        room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), pusher_id_.c_str(), ssrc);
    cb_->OnTransportSendRtcp(pspli_pkt->GetData(), pspli_pkt->GetDataLen());
}

void MediaPusher::UpdateSSRC(uint32_t new_ssrc) {
    param_.ssrc_ = new_ssrc;
}

} // namespace cpp_streamer