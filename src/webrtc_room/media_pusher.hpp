#ifndef MEDIA_PUSHER_HPP
#define MEDIA_PUSHER_HPP
#include "utils/logger.hpp"
#include "utils/av/av.hpp"
#include "net/rtprtcp/rtp_packet.hpp"
#include "net/rtprtcp/rtprtcp_pub.hpp"
#include "net/rtprtcp/rtcp_sr.hpp"
#include "rtc_info.hpp"
#include "rtp_recv_session.hpp"
#include "voice_agent/voice_agent.hpp"
#include "voice_agent/voice_agent_pub.hpp"
#include <map>
#include <memory>
#include <uv.h>

namespace cpp_streamer {

/*MediaPusher is for rtc media track pusher
    * It supports pushing audio/video media to remote WebRTC peer connections.
    * RtpSessionParam is used to configure the RTP session for the media track.
*/
class MediaPusher : public TransportSendCallbackI
{
public:
    MediaPusher(const RtpSessionParam& param, 
        const std::string& room_id, 
        const std::string& user_id, 
        const std::string& session_id,
        TransportSendCallbackI* cb,
        PacketFromRtcPusherCallbackI* packet2room_cb,
        uv_loop_t* loop,
        Logger* logger);
    virtual ~MediaPusher();

public:
    std::string GetPusherId() { return pusher_id_; }
    std::string GetRoomId() { return room_id_; }
    std::string GetUserId() { return user_id_; }
    void CreateRtpRecvSession();
    int HandleRtpPacket(RtpPacket* rtp_pkt);
    MEDIA_PKT_TYPE GetMediaType() { return media_type_; }
    const RtpSessionParam& GetRtpSessionParam() { return param_; }
    void SetVoiceAgentCallback(VoiceAgentCallbackI* cb) { voice_agent_cb_ = cb; }
    void UpdateSSRC(uint32_t new_ssrc);

public:
    int HandleRtcpSrPacket(RtcpSrPacket* sr_pkt);
    void RequestKeyFrame(uint32_t ssrc);
    
public://implement TransportSendCallbackI
    virtual bool IsConnected() override;
    virtual void OnTransportSendRtp(uint8_t* data, size_t sent_size) override;
    virtual void OnTransportSendRtcp(uint8_t* data, size_t sent_size) override;

public:
    void OnTimer(int64_t now_ms);

private:
    std::shared_ptr<RtpRecvSession> GetRtpRecvSession(RtpPacket* rtp_pkt, bool& is_rtx);

private:
    RtpSessionParam param_;
    uv_loop_t* loop_ = nullptr;
    Logger* logger_ = nullptr;
    std::string room_id_;
    std::string user_id_;
    std::string session_id_;
    std::string pusher_id_;
    TransportSendCallbackI* cb_ = nullptr;
    PacketFromRtcPusherCallbackI* packet2room_cb_ = nullptr;
    VoiceAgentCallbackI* voice_agent_cb_ = nullptr;

private:
    std::map<uint32_t, std::shared_ptr<RtpRecvSession>> ssrc2sessions_;
    std::map<uint32_t, std::shared_ptr<RtpRecvSession>> rtxssrc2sessions_;

private:
    MEDIA_PKT_TYPE media_type_ = MEDIA_UNKNOWN_TYPE;

private:
    int64_t last_statics_ms_ = -1;
    int64_t last_keyframe_request_ms_ = -1;

private:
    std::unique_ptr<VoiceAgent> voice_agent_ptr_;
};

} // namespace cpp_streamer

#endif // MEDIA_PUSHER_HPP