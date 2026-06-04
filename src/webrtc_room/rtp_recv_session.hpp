#ifndef RTP_RECV_SESSION_HPP
#define RTP_RECV_SESSION_HPP
#include "rtp_session.hpp"
#include "net/rtprtcp/rtcp_sr.hpp"
#include "utils/timeex.hpp"
#include "utils/stream_statics.hpp"
#include "nack_generator.hpp"

namespace cpp_streamer {

class RtpRecvSession : public RtpSession, public TimerInterface, public NackGeneratorCallbackI
{

public:
    RtpRecvSession(const RtpSessionParam& param,
        const std::string& room_id,
        const std::string& user_id,
        TransportSendCallbackI* cb,
        uv_loop_t* loop, Logger* logger);
    virtual ~RtpRecvSession();

public:
    bool ReceiveRtpPacket(RtpPacket* rtp_pkt);
    bool ReceiveRtxPacket(RtpPacket* rtp_pkt, bool& repeat);

public:
    int HandleRtcpSrPacket(RtcpSrPacket* sr_pkt);
    void UpdateSSRC(uint32_t new_ssrc);

public:
    StreamStatics& GetRecvStatics() { return recv_statics_; }
    
protected:
    virtual bool OnTimer() override;

protected:
    virtual void GenerateNackList(const std::vector<uint16_t>& seq_vec) override;

private:
    void GetLostStatics();
    void GenerateJitter(uint32_t rtp_timestamp, int64_t recv_pkt_ms);
    void SendRtcpRR();

private:
    std::unique_ptr<NackGenerator> nack_generator_;
private:
    int64_t last_send_rtcp_rr_ = -1;
    uint32_t sr_ssrc_ = 0;
    NTP_TIMESTAMP ntp_;
    int64_t rtp_timestamp_ = 0;
    int64_t sr_local_ms_ = 0;
    uint32_t pkt_count_ = 0;
    uint32_t bytes_count_ = 0;
    int64_t last_sr_ms_ = 0;
    uint32_t lsr_ = 0;
    int64_t expect_recv_   = 0;
    int64_t last_recv_ = 0;
    int64_t total_lost_ = 0;
    int64_t frac_lost_ = 0;
    double lost_percent_ = 0.0;
    StreamStatics recv_statics_;
};

} // namespace cpp_streamer

#endif // RTP_RECV_SESSION_HPP