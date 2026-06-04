#include "rtp_recv_session.hpp"
#include "net/rtprtcp/rtcpfb_nack.hpp"
#include "net/rtprtcp/rtcp_rr.hpp"
#include <assert.h>

namespace cpp_streamer {

static const int64_t kVideoRtcpRRInterval = 400;
static const int64_t kAudioRtcpRRInterval = 2000;

RtpRecvSession::RtpRecvSession(const RtpSessionParam& param,
    const std::string& room_id,
    const std::string& user_id,
    TransportSendCallbackI* cb,
    uv_loop_t* loop, Logger* logger) :
    RtpSession(param, room_id, user_id, cb, loop, logger),
    TimerInterface(20)
{
    if (param_.use_nack_) {
        nack_generator_.reset(new NackGenerator(loop, logger, this));
    }
    StartTimer();
    LogInfof(logger_, "RtpRecvSession construct, room_id:%s, user_id:%s, ssrc:%u, payload_type:%u, \
media type:%s, nack:%s",
        room_id_.c_str(), user_id_.c_str(), param_.ssrc_, param_.payload_type_,
        avtype_tostring(param_.av_type_).c_str(), BOOL2STRING(param_.use_nack_)
    );
}

RtpRecvSession::~RtpRecvSession() {
    LogInfof(logger_, "RtpRecvSession destruct, room_id:%s, user_id:%s, ssrc:%u, payload_type:%u, \
media type:%s, nack:%s",
        room_id_.c_str(), user_id_.c_str(), param_.ssrc_, param_.payload_type_,
        avtype_tostring(param_.av_type_).c_str(), BOOL2STRING(param_.use_nack_)
    );
}

bool RtpRecvSession::ReceiveRtpPacket(RtpPacket* rtp_pkt) {
    const uint16_t seq = rtp_pkt->GetSeq();

    if (first_pkt_) {
        first_pkt_ = false;
        InitSeq(seq);
        max_packet_ts_ = rtp_pkt->GetTimestamp();
        max_packet_ms_ = rtp_pkt->GetLocalMs();
        LogInfof(logger_, "RtpRecvSession first packet received, room_id:%s, user_id:%s, ssrc:%u, seq:%u",
            room_id_.c_str(), user_id_.c_str(), param_.ssrc_, seq);
        return true;
    }
    if (!UpdateSeq(rtp_pkt)) {
        LogInfof(logger_, "RtpRecvSession packet out of order, room_id:%s, user_id:%s, ssrc:%u, seq:%u",
            room_id_.c_str(), user_id_.c_str(), param_.ssrc_, seq);
        return false;
    }
    
    GenerateJitter(rtp_pkt->GetTimestamp(), rtp_pkt->GetLocalMs());
    last_pkt_ms_ = rtp_pkt->GetLocalMs();
    last_rtp_ts_ = rtp_pkt->GetTimestamp();

    recv_statics_.Update(rtp_pkt->GetDataLength(), rtp_pkt->GetLocalMs());

    if (param_.use_nack_) {
        nack_generator_->UpdateNackList(rtp_pkt);
    }
    
    return true;
}

bool RtpRecvSession::ReceiveRtxPacket(RtpPacket* rtp_pkt, bool& repeat) {
    if (rtp_pkt->GetPayloadLength() == 0) {
        LogDebugf(logger_, "RtpRecvSession receive empty rtx packet, room_id:%s, user_id:%s, packet:\r\n%s",
            room_id_.c_str(), user_id_.c_str(), rtp_pkt->Dump().c_str());
        return true;
    }

    repeat = false;
    try {
        rtp_pkt->RtxDemux(param_.ssrc_, param_.payload_type_);
        // LogWarnf(logger_, "rtx demux seq:%d", rtp_pkt->GetSeq());

        if (nack_generator_) {
            if (!nack_generator_->IsInNackList(rtp_pkt->GetSeq())) {
                LogDebugf(logger_, "RtpRecvSession receive rtx packet seq:%u not in nack list, drop it, room_id:%s, user_id:%s, ssrc:%u",
                    rtp_pkt->GetSeq(), room_id_.c_str(), user_id_.c_str(), param_.ssrc_);
                repeat = true;
                return true; // drop it 
            }
        }
        // pass to normal rtp packet handler
        bool r = ReceiveRtpPacket(rtp_pkt);
        if (!r) {
            LogWarnf(logger_, "RtpRecvSession receive rtx packet failed to process as normal rtp, room_id:%s, user_id:%s, ssrc:%u, seq:%u",
                room_id_.c_str(), user_id_.c_str(), param_.ssrc_, rtp_pkt->GetSeq());
            return false;
        }
    } catch(const std::exception& e) {
        LogErrorf(logger_, "RtpRecvSession receive rtx packet exception:%s, room_id:%s, user_id:%s, ssrc:%u",
            e.what(), room_id_.c_str(), user_id_.c_str(), param_.ssrc_);
        return false;
    }
    
    return true;
}

void RtpRecvSession::GenerateJitter(uint32_t rtp_timestamp, int64_t recv_pkt_ms) {
    if (param_.clock_rate_ <= 0) {
        CSM_THROW_ERROR("clock rate(%d) is invalid, room_id:%s, user_id:%s", param_.clock_rate_, room_id_.c_str(), user_id_.c_str());
    }
    uint32_t clock_rate = param_.clock_rate_;
    if ((last_pkt_ms_ == 0) || (last_rtp_ts_ == 0)) {
        return;
    }
    int64_t receive_diff_ms = recv_pkt_ms - last_pkt_ms_;
    uint32_t receive_diff_rtp = static_cast<uint32_t>((receive_diff_ms * clock_rate) / 1000);
    int64_t time_diff_samples = receive_diff_rtp - (rtp_timestamp - last_rtp_ts_);
    time_diff_samples = std::abs(time_diff_samples);

    // lib_jingle sometimes deliver crazy jumps in TS for the same stream.
    // If this happens, don't update jitter value. Use 5 secs video frequency
    // as the threshold.
    if (time_diff_samples < 450000) {
        // Note we calculate in Q4 to avoid using float.
        int32_t jitter_diff_q4 = (int32_t)(time_diff_samples << 4) - jitter_q4_;
        jitter_q4_ += ((jitter_diff_q4 + 8) >> 4);
        jitter_ = jitter_q4_ >> 4;
    }
    return;
}


void RtpRecvSession::SendRtcpRR() {
    RtcpCommonHeader rtcp_hdr;

    //init rtcp_hdr
    rtcp_hdr.version = 2;
    rtcp_hdr.padding = 0;
    rtcp_hdr.count = 1; //number of rr blocks
    rtcp_hdr.packet_type = RTCP_RR;
    rtcp_hdr.length = htons((sizeof(RtcpCommonHeader) + sizeof(uint32_t) + sizeof(RtcpRrBlock)) / 4 - 1);

    RtcpRrPacket rr_pkt((uint8_t*)&rtcp_hdr, sizeof(RtcpCommonHeader));
    RtcpRrBlockInfo rr_block_info;

    GetLostStatics();
    uint32_t highest_seq = (uint32_t)(max_seq_ + cycles_);

    rr_block_info.SetReporteeSsrc(param_.ssrc_);
    rr_block_info.SetCumulativeLost(static_cast<uint32_t>(total_lost_));
    rr_block_info.SetFracLost(static_cast<uint8_t>(frac_lost_));
    rr_block_info.SetHighestSeq(highest_seq);
    rr_block_info.SetJitter(jitter_);
    rr_block_info.SetLsr(lsr_);
    int64_t dlsr_ms = now_millisec() - last_sr_ms_;
    uint32_t dlsr = static_cast<uint32_t>(((dlsr_ms / 1000) << 16) | ((dlsr_ms % 1000) * 65536 / 1000));
    rr_block_info.SetDlsr(dlsr);

    rr_pkt.AddRrBlock(rr_block_info.GetBlock());

    size_t rr_len = 0;
    uint8_t* rr_data = rr_pkt.GetData(rr_len);

    assert(rr_len == rr_pkt.GetLen());

    transport_cb_->OnTransportSendRtcp(rr_data, rr_len);
}

void RtpRecvSession::GenerateNackList(const std::vector<uint16_t>& seq_vec) {
    RtcpFbNack nack_pkt(0, param_.ssrc_);
    nack_pkt.InsertSeqList(seq_vec);

    transport_cb_->OnTransportSendRtcp(nack_pkt.GetData(), nack_pkt.GetLen());
}

bool RtpRecvSession::OnTimer() {
    int64_t now_ms = now_millisec();

    if (last_send_rtcp_rr_ < 0) {
        last_send_rtcp_rr_ = now_ms;
    } else {
        const int64_t rtcp_rr_interval = (param_.av_type_ == MEDIA_VIDEO_TYPE) ? kVideoRtcpRRInterval : kAudioRtcpRRInterval;
        if (now_ms - last_send_rtcp_rr_ > rtcp_rr_interval) {
            last_send_rtcp_rr_ = now_ms;
            SendRtcpRR();
        }
    }
    return timer_running_;
}

int RtpRecvSession::HandleRtcpSrPacket(RtcpSrPacket* sr_pkt) {
    int64_t now_ms = now_millisec();

    sr_ssrc_       = sr_pkt->GetSsrc();
    ntp_.ntp_sec   = sr_pkt->GetNtpSec();
    ntp_.ntp_frac  = sr_pkt->GetNtpFrac();
    rtp_timestamp_ = (int64_t)sr_pkt->GetRtpTimestamp();
    sr_local_ms_   = now_ms;
    pkt_count_     = sr_pkt->GetPktCount();
    bytes_count_   = sr_pkt->GetBytesCount();

    last_sr_ms_ = now_ms;
    lsr_ = ((ntp_.ntp_sec & 0xffff) << 16) | (ntp_.ntp_frac & 0xffff0000);
    return 0;
}

void RtpRecvSession::GetLostStatics() {
    int64_t expected = GetExpectedPackets();
    int64_t recv_count = (int64_t)recv_statics_.GetCount();

    int64_t expected_interval = expected - expect_recv_;
    expect_recv_ = expected;

    int64_t recv_interval = recv_count - last_recv_;
    if (last_recv_ <= 0) {
        last_recv_ = recv_count;
        return;
    }
    last_recv_ = recv_count;

    if ((expected_interval <= 0) || (recv_interval <= 0)) {
        frac_lost_ = 0;
    } else {
        total_lost_ += expected_interval - recv_interval;
        frac_lost_ = (int64_t)std::round((double)((expected_interval - recv_interval) * 256) / expected_interval);
        lost_percent_ = (double)(expected_interval - recv_interval) / expected_interval;
    }

    return;
}

void RtpRecvSession::UpdateSSRC(uint32_t new_ssrc) {
    param_.ssrc_ = new_ssrc;
}

} // namespace cpp_streamer