#include "webrtc_session.hpp"
#include "dtls_session.hpp"
#include "utils/uuid.hpp"
#include "utils/byte_crypto.hpp"
#include "net/udp/udp_client.hpp"
#include "net/rtprtcp/rtprtcp_pub.hpp"
#include "net/rtprtcp/rtcp_sr.hpp"
#include "net/rtprtcp/rtcp_rr.hpp"
#include "net/rtprtcp/rtp_packet.hpp"
#include "net/rtprtcp/rtcp_pspli.hpp"
#include "net/rtprtcp/rtcpfb_nack.hpp"
#include "utils/timeex.hpp"
#include "config/config.hpp"

namespace cpp_streamer {

WebRtcSession::WebRtcSession(SRtpType type, const std::string& room_id, const std::string& user_id, 
    PacketFromRtcPusherCallbackI* packet2room_cb,
    MediaPushPullEventI* media_push_event_cb,
    uv_loop_t* loop, Logger* logger) : TimerInterface(50),
    direction_type_(type),
    room_id_(room_id),
    user_id_(user_id),
    loop_(loop),
    logger_(logger),
    packet2room_cb_(packet2room_cb),
    media_push_event_cb_(media_push_event_cb)
{
    session_id_ = cpp_streamer::UUID::MakeUUID2();
    ice_server_ = std::make_unique<IceServer>(this, logger_);
    ice_ufrag_ = ice_server_->GetIceUfrag();
    ice_pwd_ = ice_server_->GetIcePwd();

    StartTimer();
    alive_ms_ = now_millisec();

    tcc_server_.reset(new TccServer(this, logger_));

    LogInfof(logger_, "WebRtcSession construct, room_id:%s, user_id:%s, session_id:%s, direction:%s",
        room_id_.c_str(), user_id_.c_str(), session_id_.c_str(),
        (direction_type_ == SRtpType::SRTP_SESSION_TYPE_SEND) ? "SEND" : "RECV");
}

WebRtcSession::~WebRtcSession() {
    StopTimer();
    Close();
    LogInfof(logger_, "WebRtcSession destruct, room_id:%s, user_id:%s, session_id:%s, direction:%s",
        room_id_.c_str(), user_id_.c_str(), session_id_.c_str(),
        (direction_type_ == SRtpType::SRTP_SESSION_TYPE_SEND) ? "SEND" : "RECV");
}

void WebRtcSession::Close() {
    if (closed_) {
        return;
    }
    closed_ = true;
    LogInfof(logger_, "WebRtcSession closed, room_id:%s, user_id:%s, session_id:%s",
        room_id_.c_str(), user_id_.c_str(), session_id_.c_str());
    for (auto& pusher_pair : ssrc2media_pusher_) {
        std::string id = pusher_pair.second->GetPusherId();
        media_push_event_cb_->OnPushClose(id);
    }
    for (auto& puller_pair : ssrc2media_puller_) {
        std::string id = puller_pair.second->GetPullerId();
        media_push_event_cb_->OnPullClose(id);
    }
}

int WebRtcSession::DtlsInit(Role role, const std::string& remote_fingerprint) {
    dtls_session_.reset(new DtlsSession(this, logger_));
    int ret = dtls_session_->InitSession();
    if (ret != 0) {
        LogErrorf(logger_, "WebRtcSession DtlsInit InitSession failed, room_id:%s, user_id:%s, session_id:%s, ret:%d",
            room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), ret);
        return ret;
    }
    dtls_session_->SetRemoteFingerprint(remote_fingerprint);
    dtls_session_->SetRole(role);
    
    auto remote = dtls_session_->GetRemoteFingerprint();

    ret = dtls_session_->Run();
    if (ret != 0) {
        LogErrorf(logger_, "WebRtcSession DtlsInit Run failed, room_id:%s, user_id:%s, session_id:%s, ret:%d",
            room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), ret);
        return ret;
    }
    
    Fingerprint local_fp = DtlsSession::GetLocalFingerprint(remote.algorithm);

    local_finger_print_ = local_fp.ToString(); 
    remote_finger_print_ = remote.ToString();

    LogInfof(logger_, "WebRtcSession DtlsInit, room_id:%s, user_id:%s, session_id:%s, local_fp:%s, remote_fp:%s, role:%d",
        room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), local_finger_print_.c_str(), remote_finger_print_.c_str(), static_cast<int>(role));
    return 0;
}

int WebRtcSession::HandleStunPacket(StunPacket* stun_pkt, UdpTransportI* trans_cb, UdpTuple addr) {
    trans_cb_ = trans_cb;
    remote_addr_ = addr;
    alive_ms_ = now_millisec();
    int ret = ice_server_->HandleStunPacket(stun_pkt, addr);
    if (ret != 0) {
        LogErrorf(logger_, "WebRtcSession HandleStunPacket failed, room_id:%s, user_id:%s, session_id:%s, ret:%d",
            room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), ret);
        return ret;
    }
    dtls_session_->SetRemoteAddress(addr);
    dtls_session_->SetIceConnected(true);
    return 0;
}

int WebRtcSession::HandleNoneStunPacket(const uint8_t* data, size_t len, UdpTransportI* trans_cb, UdpTuple addr) {
    trans_cb_ = trans_cb;
    remote_addr_ = addr;

    alive_ms_ = now_millisec();
    if (IsRtcp(data, len)) {
        LogDebugf(logger_, "Handle RTCP packet, room_id:%s, user_id:%s, session_id:%s, len:%zu",
            room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
        return HandleRtcpPacket(data, len, addr);
    } else if (IsRtp(data, len)) {
        LogDebugf(logger_, "Handle RTP packet, room_id:%s, user_id:%s, session_id:%s, len:%zu",
            room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
        return HandleRtpPacket(data, len, addr);
    } else if (DtlsSession::IsDtlsData(data, len)) {
        LogInfof(logger_, "Handle DTLS packet, room_id:%s, user_id:%s, session_id:%s, len:%zu",
            room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
        dtls_session_->OnHandleDtlsData(data, len, addr);
    } else {
        LogErrorf(logger_, "Unknown none-stun packet, room_id:%s, user_id:%s, session_id:%s, len:%zu",
            room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
    }
    return 0;
}
void WebRtcSession::OnIceWrite(const uint8_t* data, size_t sent_size, cpp_streamer::UdpTuple address) {
    LogDebugf(logger_, "Stun response, room_id:%s, user_id:%s, session_id:%s, len:%lu, address:%s",
        room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), sent_size, address.to_string().c_str());
    trans_cb_->OnWriteUdpData(data, sent_size, address);
}

void WebRtcSession::OnDtlsTransportSendData(const uint8_t* data, size_t sent_size, cpp_streamer::UdpTuple address) {
    LogInfof(logger_, "DTLS send data, room_id:%s, user_id:%s, session_id:%s, len:%lu, address:%s",
        room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), sent_size, address.to_string().c_str());
    trans_cb_->OnWriteUdpData(data, sent_size, address);
}

void WebRtcSession::OnDtlsTransportConnected(
      const DtlsSession* /*dtlsTransport*/,
      SRtpSessionCryptoSuite srtpCryptoSuite,
      uint8_t* srtpLocalKey,
      size_t srtpLocalKeyLen,
      uint8_t* srtpRemoteKey,
      size_t srtpRemoteKeyLen,
      std::string& remoteCert) {
    LogInfof(logger_, "DTLS connected, room_id:%s, user_id:%s, session_id:%s, srtpCryptoSuite:%d, remoteCert:%s, direction:%s",
        room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), static_cast<int>(srtpCryptoSuite), remoteCert.c_str(),
        (direction_type_ == SRtpType::SRTP_SESSION_TYPE_SEND) ? "SEND" : "RECV");
    
    try {
        srtp_send_session_.reset(new SRtpSession(SRtpType::SRTP_SESSION_TYPE_SEND, srtpCryptoSuite,
            srtpLocalKey, srtpLocalKeyLen, logger_));
        srtp_recv_session_.reset(new SRtpSession(SRtpType::SRTP_SESSION_TYPE_RECV, srtpCryptoSuite, 
            srtpRemoteKey, srtpRemoteKeyLen, logger_));
    } catch(const std::exception& e) {
        LogErrorf(logger_, "Create SRtpSession exception:%s, room_id:%s, user_id:%s, session_id:%s",
            e.what(), room_id_.c_str(), user_id_.c_str(), session_id_.c_str());
    }
    // if this session's direction is send and it has video puller,
    // send key frame request to pusher.
    if (direction_type_ == SRtpType::SRTP_SESSION_TYPE_SEND) {
        for (auto& puller_pair : ssrc2media_puller_) {
            auto puller = puller_pair.second;
            if (puller->GetMediaType() == MEDIA_PKT_TYPE::MEDIA_VIDEO_TYPE) {
                uint32_t ssrc = 0;
                for (const auto& ssrc_pair : ssrc2media_puller_) {
                    if (ssrc_pair.second == puller) {
                        ssrc = ssrc_pair.first;
                        break;
                    }
                }
                if (ssrc == 0) {
                    continue;
                }
                media_push_event_cb_->OnKeyFrameRequest(
                    puller->GetPusherId(),
                    puller->GetPulllerUserId(),
                    puller->GetPusherUserId(),
                    ssrc);
            }
        }
    }

    dtls_connected_ = true;
}

int WebRtcSession::HandleRtpPacket(const uint8_t* data, size_t len, UdpTuple addr) {
    if (!srtp_recv_session_) {
        LogErrorf(logger_, "SRTP not established (RTP recv), room_id:%s, user_id:%s, session_id:%s",
            room_id_.c_str(), user_id_.c_str(), session_id_.c_str());
        return -1;
    }

    if (Config::Instance().uplink_discard_percent_ > 0) {
        // simulate packet loss for test
        uint32_t rand_val = ByteCrypto::GetRandomUint(0, 100);
        if (rand_val < Config::Instance().uplink_discard_percent_) {
            LogInfof(logger_, "----------------Discard RTP packet by percent for test, room_id:%s, user_id:%s, session_id:%s, len:%zu, percent:%u%%",
                room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len, Config::Instance().uplink_discard_percent_);
            return 0;
        }
    }
    RtpPacket* rtp_pkt = nullptr;
    
    try {
        bool r = srtp_recv_session_->DecryptRtp(const_cast<uint8_t*>(data), reinterpret_cast<int*>(&len));
        if (!r) {
            LogErrorf(logger_, "Decrypt RTP failed, room_id:%s, user_id:%s, session_id:%s, len:%zu",
                room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
            return -1;
        }

        rtp_pkt = RtpPacket::Parse(const_cast<uint8_t*>(data), len);
        if (!rtp_pkt) {
            LogErrorf(logger_, "Parse RTP failed after decrypt, room_id:%s, user_id:%s, session_id:%s, len:%zu",
                room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
            return -1;
        }
    } catch(const std::exception& e) {
        LogErrorf(logger_, "HandleRtpPacket decrypt/parse exception:%s, room_id:%s, user_id:%s, session_id:%s, len:%zu",
            e.what(), room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
        return -1;
    }

    try {
        uint32_t ssrc = rtp_pkt->GetSsrc();
        if (mid_ext_id_ > 0) {
            rtp_pkt->SetMidExtensionId(static_cast<uint8_t>(mid_ext_id_));
        }
        if (tcc_ext_id_ > 0) {
            rtp_pkt->SetTccExtensionId(static_cast<uint8_t>(tcc_ext_id_));
            tcc_server_->SetTccExtensionId(static_cast<uint8_t>(tcc_ext_id_));
            // get tcc wide seq
            uint16_t wide_seq = 0;
            if (rtp_pkt->ReadWideSeq(wide_seq)) {
                LogDebugf(logger_, "RTP packet with TCC wide seq, room_id:%s, user_id:%s, session_id:%s, ssrc:%u, wide_seq:%u, pt:%d, now_ms:%ld",
                    room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), ssrc, wide_seq, rtp_pkt->GetPayloadType(), rtp_pkt->GetLocalMs());
            }
        }
        tcc_server_->InsertRtpPacket(rtp_pkt);

        auto it = ssrc2media_pusher_.find(ssrc);
        std::shared_ptr<MediaPusher> media_pusher = nullptr;
        if (it == ssrc2media_pusher_.end()) {
            uint8_t mid = 0;
            bool r = rtp_pkt->ReadMid(mid);
            if (!r) {
                LogErrorf(logger_, "RTP packet without mid, room_id:%s, user_id:%s, session_id:%s, ssrc:%u",
                    room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), ssrc);
                delete rtp_pkt;
                return -1;
            }
            auto mid_it = mid2media_pusher_.find(mid);
            if (mid_it == mid2media_pusher_.end()) {
                LogErrorf(logger_, "No MediaPusher for RTP by mid, room_id:%s, user_id:%s, session_id:%s, ssrc:%u, mid:%u",
                    room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), ssrc, mid);
                delete rtp_pkt;
                return -1;
            }
            LogInfof(logger_, "Find MediaPusher for RTP by mid, room_id:%s, user_id:%s, session_id:%s, ssrc:%u, mid:%u",
                room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), ssrc, mid);
            ssrc2media_pusher_[ssrc] = mid_it->second;
            media_pusher = mid_it->second;
            media_pusher->UpdateSSRC(ssrc);
        } else {
            media_pusher = it->second;
        }
        int ret = media_pusher->HandleRtpPacket(rtp_pkt);
        if (ret < 0) {
            LogErrorf(logger_, "MediaPusher HandleRtpPacket failed, room_id:%s, user_id:%s, session_id:%s, ssrc:%u",
                room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), ssrc);
            delete rtp_pkt;
            return ret;
        
        }
        delete rtp_pkt;
        return ret;
    } catch(const std::exception& e) {
        LogErrorf(logger_, "HandleRtpPacket dispatch exception:%s, room_id:%s, user_id:%s, session_id:%s, len:%zu",
            e.what(), room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
    }
    
    return -1;
}

int WebRtcSession::HandleRtcpPacket(const uint8_t* data, size_t len, UdpTuple addr) {
    try {
        if (!srtp_recv_session_) {
            LogErrorf(logger_, "SRTP not established (RTCP recv), room_id:%s, user_id:%s, session_id:%s",
                room_id_.c_str(), user_id_.c_str(), session_id_.c_str());
            return -1;
        }
        bool r = srtp_recv_session_->DecryptRtcp(const_cast<uint8_t*>(data), reinterpret_cast<int*>(&len));
        if (!r) {
            LogErrorf(logger_, "Decrypt RTCP failed, room_id:%s, user_id:%s, session_id:%s, len:%zu",
                room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
            return -1;
        }
    } catch(const std::exception& e) {
        LogErrorf(logger_, "HandleRtcpPacket decrypt exception:%s, room_id:%s, user_id:%s, session_id:%s, len:%zu",
            e.what(), room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
        return -1;
    }
    
    int left_len = static_cast<int>(len);
    uint8_t* p = const_cast<uint8_t*>(data);

    while (left_len > 0) {
        RtcpCommonHeader* rtcp_hdr = reinterpret_cast<RtcpCommonHeader*>(p);
        size_t item_total = GetRtcpLength(rtcp_hdr);

        LogDebugf(logger_, "Handle RTCP packet item, room_id:%s, user_id:%s, session_id:%s, packet_type:%d, item_total:%zu, left_len:%d",
            room_id_.c_str(), user_id_.c_str(), session_id_.c_str(),
            rtcp_hdr->packet_type, item_total, left_len);
        switch (rtcp_hdr->packet_type)
        {
            case RTCP_SR:
            {
                HandleRtcpSrPacket(p, item_total);
                break;
            }
            case RTCP_RR:
            {
                LogDebugf(logger_, "Handle RTCP RR packet, room_id:%s, user_id:%s, session_id:%s, len:%zu",
                    room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), item_total);
                HandleRtcpRrPacket(p, item_total);
                break;
            }
            case RTCP_SDES:
            {
                //rtcp sdes packet needn't to be handled.
                LogDebugf(logger_, "not handle RTCP SDES packet, room_id:%s, user_id:%s, session_id:%s, len:%zu",
                    room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), item_total);
                break;
            }
            case RTCP_BYE:
            {
                LogDebugf(logger_, "not handle RTCP BYE packet, room_id:%s, user_id:%s, session_id:%s, len:%zu",
                    room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), item_total);
                break;
            }
            case RTCP_APP:
            {
                LogDebugf(logger_, "not handle RTCP APP packet, room_id:%s, user_id:%s, session_id:%s, len:%zu",
                    room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), item_total);
                break;
            }
            case RTCP_RTPFB:
            {
                HandleRtcpRtpfbPacket(p, item_total);
                break;
            }
            case RTCP_PSFB:
            {
                HandleRtcpPsfbPacket(p, item_total);
                break;
            }
            case RTCP_XR:
            {
                HandleRtcpXrPacket(p, item_total);
                break;
            }
            default:
            {
                LogErrorf(logger_, "unkown rtcp type:%d", rtcp_hdr->packet_type);
            }
        }
        p        += item_total;
        left_len -= (int)item_total;
    }
    return 0;
}

bool WebRtcSession::IsConnected() {
    return dtls_connected_;
}

void WebRtcSession::OnTransportSendRtp(uint8_t* data, size_t sent_size) {
    if (!dtls_connected_) {
        return;
    }

    if (!srtp_send_session_) {
        LogErrorf(logger_, "SRTP not established (RTP send), room_id:%s, user_id:%s, session_id:%s",
            room_id_.c_str(), user_id_.c_str(), session_id_.c_str());
        return;
    }
    int len = static_cast<int>(sent_size);
    bool r = srtp_send_session_->EncryptRtp(data, &len);
    if (!r) {
        LogErrorf(logger_, "Encrypt RTP failed, room_id:%s, user_id:%s, session_id:%s, len:%zu",
            room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
        return;
    }

    if (Config::Instance().downlink_discard_percent_ > 0) {
        // simulate packet loss for test
        uint32_t rand_val = ByteCrypto::GetRandomUint(0, 100);
        if (rand_val < Config::Instance().downlink_discard_percent_) {
            return;
        }
    }
    trans_cb_->OnWriteUdpData(data, len, remote_addr_);
}

void WebRtcSession::OnTransportSendRtcp(uint8_t* data, size_t sent_size) {
    if (!dtls_connected_) {
        return;
    }
    LogDebugf(logger_, "OnTransportSendRtcp, room_id:%s, user_id:%s, session_id:%s, len:%zu",
        room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), sent_size);
    if (!srtp_send_session_) {
        LogErrorf(logger_, "SRTP not established (RTCP send), room_id:%s, user_id:%s, session_id:%s",
            room_id_.c_str(), user_id_.c_str(), session_id_.c_str());
        return;
    }
    int len = static_cast<int>(sent_size);
    bool r = srtp_send_session_->EncryptRtcp(data, &len);
    if (!r) {
        LogErrorf(logger_, "Encrypt RTCP failed, room_id:%s, user_id:%s, session_id:%s, len:%zu",
            room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
        return;
    }
    trans_cb_->OnWriteUdpData(data, len, remote_addr_);
}

int WebRtcSession::AddPusherRtpSession(const RtpSessionParam& param, std::string& pusher_id) {
    try {
        mid_ext_id_ = param.mid_ext_id_;
        tcc_ext_id_ = param.tcc_ext_id_;
		auto media_pusher = std::make_shared<MediaPusher>(param, room_id_, user_id_, session_id_,
            this, packet2room_cb_, loop_, logger_);
        media_pusher->CreateRtpRecvSession();
        pusher_id = media_pusher->GetPusherId();
        ssrc2media_pusher_[param.ssrc_] = media_pusher;
        mid2media_pusher_[param.mid_] = media_pusher;
        if (param.rtx_ssrc_ != 0) {
            ssrc2media_pusher_[param.rtx_ssrc_] = media_pusher;
        }
    } catch(const std::exception& e) {
        LogErrorf(logger_, "AddPusherRtpSession exception:%s, room_id:%s, user_id:%s",
            e.what(), room_id_.c_str(), user_id_.c_str());
        return -1;
    }

    return 0;
}

int WebRtcSession::AddPullerRtpSession(const RtpSessionParam& param, 
        const std::string& pusher_user_id,
        const std::string& pusher_id, 
        std::string& puller_id) {
    try {
        auto media_puller = std::make_shared<MediaPuller>(param, 
            room_id_, 
            user_id_, 
            pusher_user_id, 
            pusher_id, 
            session_id_,
            this, loop_, logger_);
        media_puller->CreateRtpSendSession();
        uint32_t main_ssrc = param.ssrc_;
        ssrc2media_puller_[main_ssrc] = media_puller;
        if (param.rtx_ssrc_ != 0) {
            uint32_t rtx_ssrc = param.rtx_ssrc_;
            rtxssrc2media_puller_[rtx_ssrc] = media_puller;
        }
    } catch(const std::exception& e) {
        LogErrorf(logger_, "AddPullerRtpSession exception:%s, room_id:%s, user_id:%s",
            e.what(), room_id_.c_str(), user_id_.c_str());
        return -1;
    }

    return 0;
}
int WebRtcSession::HandleRtcpSrPacket(const uint8_t* data, size_t len) {
    LogDebugf(logger_, "Handle RTCP SR packet, room_id:%s, user_id:%s, session_id:%s, len:%zu",
        room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);

    try {
        RtcpSrPacket* sr_pkt = RtcpSrPacket::Parse(const_cast<uint8_t*>(data), len);
        if (!sr_pkt) {
            LogErrorf(logger_, "Parse RTCP SR packet failed, room_id:%s, user_id:%s, session_id:%s, len:%zu",
                room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
            return -1;
        }
        uint32_t ssrc = sr_pkt->GetSsrc();
        auto it = ssrc2media_pusher_.find(ssrc);
        if (it == ssrc2media_pusher_.end()) {
            LogErrorf(logger_, "No MediaPusher for RTCP SR, room_id:%s, user_id:%s, session_id:%s, ssrc:%u",
                room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), ssrc);
            delete sr_pkt;
            return -1;
        }
        int ret = it->second->HandleRtcpSrPacket(sr_pkt);
        if (ret != 0) {
            LogErrorf(logger_, "MediaPusher HandleRtcpSrPacket failed, room_id:%s, user_id:%s, session_id:%s, ssrc:%u",
                room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), ssrc);
            delete sr_pkt;
            return ret;
        }
        delete sr_pkt;
        return 0;
    } catch(const std::exception& e) {
        LogErrorf(logger_, "HandleRtcpSrPacket exception:%s, room_id:%s, user_id:%s, session_id:%s, len:%zu",
            e.what(), room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
        return -1;
    }
    
    return -1;
}

int WebRtcSession::HandleRtcpRrPacket(const uint8_t* data, size_t len) {
    LogDebugf(logger_, "Handle RTCP RR packet, room_id:%s, user_id:%s, session_id:%s, len:%zu",
        room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
    try {
        RtcpRrPacket* rr_pkt = RtcpRrPacket::Parse(const_cast<uint8_t*>(data), len);
        if (!rr_pkt) {
            LogErrorf(logger_, "Parse RTCP RR packet failed, room_id:%s, user_id:%s, session_id:%s, len:%zu",
                room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
            return -1;
        }
        auto rr_blocks = rr_pkt->GetRrBlocks();
        for (auto& rr_block : rr_blocks) {
            uint32_t ssrc = rr_block.GetReporteeSsrc();
            auto it = ssrc2media_puller_.find(ssrc);
            if (it == ssrc2media_puller_.end()) {
                auto rtx_it = rtxssrc2media_puller_.find(ssrc);
                if (rtx_it != rtxssrc2media_puller_.end()) {
                    continue;
                }
                LogErrorf(logger_, "No MediaPuller for RTCP RR, room_id:%s, user_id:%s, session_id:%s, ssrc:%u",
                    room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), ssrc);
                continue;
            }
            int ret = it->second->HandleRtcpRrBlock(rr_block);
            if (ret != 0) {
                LogErrorf(logger_, "MediaPuller HandleRtcpRrBlock failed, room_id:%s, user_id:%s, session_id:%s, ssrc:%u",
                    room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), ssrc);
                continue;
            }
        }
        delete rr_pkt;
    } catch(const std::exception& e) {
        LogErrorf(logger_, "HandleRtcpRrPacket exception:%s, room_id:%s, user_id:%s, session_id:%s, len:%zu",
            e.what(), room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
        return -1;
    }
    return 0;
}
int WebRtcSession::HandleRtcpXrPacket(const uint8_t* data, size_t len) {
    LogDebugf(logger_, "Handle RTCP XR packet, room_id:%s, user_id:%s, session_id:%s, len:%zu",
        room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
    return 0;
}
int WebRtcSession::HandleRtcpRtpfbPacket(const uint8_t* data, size_t len) {
    LogDebugf(logger_, "Handle RTCP RTPFB packet, room_id:%s, user_id:%s, session_id:%s, len:%zu",
        room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);

    try {
        RtcpFbCommonHeader* fb_common_hdr = (RtcpFbCommonHeader*)(const_cast<uint8_t*>(data));
        switch (fb_common_hdr->fmt)
        {
            case FB_RTP_NACK:
            {
                RtcpFbNack* nack_pkt = RtcpFbNack::Parse(const_cast<uint8_t*>(data), len);
                if (!nack_pkt) {
                    LogErrorf(logger_, "Parse RTCP RTPFB NACK packet failed, room_id:%s, user_id:%s, session_id:%s, len:%zu",
                        room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
                    return -1;
                }
                uint32_t ssrc = nack_pkt->GetMediaSsrc();

                auto it = ssrc2media_puller_.find(ssrc);;
                if (it == ssrc2media_puller_.end()) {
                    LogErrorf(logger_, "No MediaPuller for RTCP RTPFB NACK, room_id:%s, user_id:%s, session_id:%s, ssrc:%u",
                        room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), ssrc);
                    delete nack_pkt;
                    return -1;
                }
                int ret = it->second->HandleRtcpFbNack(nack_pkt);
                if (ret < 0) {
                    LogErrorf(logger_, "MediaPuller HandleRtcpFbNack failed, room_id:%s, user_id:%s, session_id:%s, ssrc:%u",
                        room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), ssrc);
                    delete nack_pkt;
                    return ret;
                }
                delete nack_pkt;
                break;
            }
            case FB_RTP_TCC:
            {
                //todo: handle rtcp tcc feedback for simulcast.
                break;
            }
            default:
            {
                LogErrorf(logger_, "Unknown RTCP RTPFB fmt:%d, room_id:%s, user_id:%s, session_id:%s, len:%zu",
                    fb_common_hdr->fmt, room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
            }
        }
    } catch(const std::exception& e) {
        LogErrorf(logger_, "HandleRtcpRtpfbPacket exception:%s, room_id:%s, user_id:%s, \
session_id:%s, len:%zu",
            e.what(), room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
    }
    
    return 0;
}

int WebRtcSession::HandleRtcpPsfbPacket(const uint8_t* data, size_t len) {
    if (len <=sizeof(RtcpFbCommonHeader)) {
        return 0;
    }
    try {
        auto fb_hdr = (RtcpFbCommonHeader*)(const_cast<uint8_t*>(data));
        switch (fb_hdr->fmt)
        {
            case FB_PS_PLI:
            {
                LogInfof(logger_, "Handle RTCP PSFB PLI, room_id:%s, user_id:%s, session_id:%s, len:%zu",
                    room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
                RtcpPsPli* pspli_pkt = RtcpPsPli::Parse(const_cast<uint8_t*>(data), len);
                if (!pspli_pkt) {
                    LogErrorf(logger_, "Parse RTCP PSFB PLI packet failed, room_id:%s, user_id:%s, session_id:%s, len:%zu",
                        room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
                    return -1;
                }
                uint32_t ssrc = pspli_pkt->GetMediaSsrc();
                auto it = ssrc2media_puller_.find(ssrc);
                if (it == ssrc2media_puller_.end()) {
                    LogErrorf(logger_, "No MediaPuller for RTCP PSFB PLI, room_id:%s, user_id:%s, session_id:%s, ssrc:%u",
                        room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), ssrc);
                    delete pspli_pkt;
                    return -1;
                }
                delete pspli_pkt;
                std::string pusher_id = it->second->GetPusherId();
                std::string pusher_user_id = it->second->GetPusherUserId();
                std::string puller_user_id = it->second->GetPulllerUserId();

                media_push_event_cb_->OnKeyFrameRequest(pusher_id, puller_user_id, pusher_user_id, ssrc);
            }
            case FB_PS_AFB:
            {
                LogDebugf(logger_, "Handle RTCP PSFB AFB, room_id:%s, user_id:%s, session_id:%s, len:%zu",
                    room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
                break;
            }
            default:
            {
                LogErrorf(logger_, "Unknown RTCP PSFB fmt:%d, room_id:%s, user_id:%s, session_id:%s, len:%zu",
                    fb_hdr->fmt, room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
            }
        }
    } catch(const std::exception& e) {
        LogErrorf(logger_, "HandleRtcpPsfbPacket exception:%s, room_id:%s, user_id:%s, session_id:%s, len:%zu",
            e.what(), room_id_.c_str(), user_id_.c_str(), session_id_.c_str(), len);
    }
    
    return 0;
}

bool WebRtcSession::OnTimer() {
    if (!dtls_connected_) {
        return timer_running_;;
    }
    if (closed_) {
        return timer_running_;
    }
    int64_t now_ms = now_millisec();

    if (direction_type_ == SRtpType::SRTP_SESSION_TYPE_SEND) {
        for (auto& kv : ssrc2media_puller_) {
            kv.second->OnTimer(now_ms);
        }
    } else if (direction_type_ == SRtpType::SRTP_SESSION_TYPE_RECV) {
        for (auto& kv : ssrc2media_pusher_) {
            kv.second->OnTimer(now_ms);
        }
    } else {
        LogErrorf(logger_, "Unknown direction type:%d, room_id:%s, user_id:%s, session_id:%s",
            static_cast<int>(direction_type_), room_id_.c_str(), user_id_.c_str(), session_id_.c_str());
    }

    tcc_server_->OnTimer(now_ms);
    return timer_running_;
}

bool WebRtcSession::IsAlive() {
    const int64_t kAliveTimeoutMs = 35*1000;
    int64_t now_ms = now_millisec();
    return (now_ms - alive_ms_) <= kAliveTimeoutMs;
}

std::vector<std::shared_ptr<MediaPusher>> WebRtcSession::GetMediaPushers() {
    std::vector<std::shared_ptr<MediaPusher>> pushers;
    for (auto& kv : ssrc2media_pusher_) {
        pushers.push_back(kv.second);
    }
    return pushers;
}

std::vector<std::shared_ptr<MediaPuller>> WebRtcSession::GetMediaPullers() {
    std::vector<std::shared_ptr<MediaPuller>> pullers;
    for (auto& kv : ssrc2media_puller_) {
        pullers.push_back(kv.second);
    }
    return pullers;
}

} // namespace cpp_streamer