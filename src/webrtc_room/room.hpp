#ifndef ROOM_HPP
#define ROOM_HPP
#include "utils/logger.hpp"
#include "utils/timer.hpp"
#include "utils/timeex.hpp"
#include "net/http/http_common.hpp"
#include "webrtc_session.hpp"
#include "udp_transport.hpp"
#include "rtc_info.hpp"
#include "voice_agent/voice_agent_pub.hpp"
#include "voice_agent/va_fake_session.hpp"
#include <map>
#include <memory>
#include <vector>
#include <mutex>
#include <uv.h>

namespace cpp_streamer {

class RtcUser;
class WebRtcServer;
class ProtooResponseI;
class RtcRecvRelay;
class RtcSendRelay;

class Room : public TimerInterface, 
    public PacketFromRtcPusherCallbackI, 
    public MediaPushPullEventI,
    public AsyncRequestCallbackI,
    public VoiceAgentCallbackI
{
public:
    Room(const std::string& room_id, 
        PilotClientI* pilot_client,
        uv_loop_t* loop, 
        Logger* logger);
    virtual ~Room();

public:
    RTC_USER_TYPE GetUserType(const std::string& user_id);
    std::string GetRoomId() { return room_id_;}
    void Close();
    int UserJoin(const std::string& user_id,
        const std::string& user_name,
        bool audience,
        int id,
        ProtooResponseI* resp_cb,
        const std::string& room_token = "");
    int WhipUserJoin(const std::string& user_id, 
        const std::string& user_name);
	int UserLeave(const std::string& user_id);
    int DisconnectUser(const std::string& user_id);

    int HandlePushSdp(const std::string& user_id, 
        const std::string& sdp_type, 
        const std::string& sdp_str, 
        int id,
        ProtooResponseI* resp_cb);
    int HandleWhipPushSdp(const std::string& user_id, 
        const std::string& sdp_type, 
        const std::string& sdp_str, 
        std::string& anwser_sdp);
    int HandlePullSdp(const PullRequestInfo& pull_info, 
        const std::string& sdp_type, 
        const std::string& sdp_str, 
        int id,
        ProtooResponseI* resp_cb);
    int HandleRemotePullSdp(const std::string& pusher_user_id,
        const PullRequestInfo& pull_info, 
        const std::string& sdp_type, 
        const std::string& sdp_str, 
        int id,
        ProtooResponseI* resp_cb);
    int HandleWsHeartbeat(const std::string& user_id);
    bool IsAlive();
    
public:
    void NotifyTextMessage2LocalUsers(const std::string& from_user_id, const std::string& from_user_name, const std::string& message);
    void NotifyTextMessage2PilotCenter(const std::string& from_user_id, const std::string& from_user_name, const std::string& message);
    
public:
    void HandleNewUserNotificationFromCenter(json& data_json);
    void HandleNewPusherNotificationFromCenter(json& data_json);
    void HandlePullRemoteStreamNotificationFromCenter(json& data_json);
    void HandleUserDisconnectNotificationFromCenter(json& data_json);
    void HandleUserLeaveNotificationFromCenter(json& data_json);
    void HandleNotifyTextMessageFromCenter(json& data_json);

public://implement PacketFromRtcPusherCallbackI
    virtual void OnRtpPacketFromRtcPusher(const std::string& user_id, const std::string& session_id,
        const std::string& pusher_id, RtpPacket* rtp_packet) override;
    virtual void OnRtpPacketFromRemoteRtcPusher(const std::string& pusher_user_id,
        const std::string& pusher_id, 
        RtpPacket* rtp_packet) override;
public:
    virtual void OnPushClose(const std::string& pusher_id) override;
    virtual void OnPullClose(const std::string& puller_id) override;
    virtual void OnKeyFrameRequest(const std::string& pusher_id, 
        const std::string& puller_user_id, 
        const std::string& pusher_user_id,
        uint32_t ssrc) override;

public://implement AsyncRequestCallbackI
    virtual void OnAsyncRequestResponse(int id, const std::string& method, json& resp_json) override;
    
protected://implement VoiceAgentCallbackI
    virtual void OnVoiceAgentRecognizedText(const std::string& room_id,
        const std::string& user_id,
        const std::string& text,
        int64_t ts) override;
    virtual void OnVoiceAgentResponseText(const std::string& room_id,
        const std::string& user_id,
        const std::string& text,
        int64_t ts) override;
    virtual void OnVoiceAgentAiOpusData(const std::vector<uint8_t>& opus_data, 
        int sample_rate, int channels, int64_t pts, int current_index) override;
    virtual void OnVoiceAgentConversationStart(const std::string& room_id,
        const std::string& conversation_id,
        int64_t ts) override;
    virtual void OnVoiceAgentConversationEnd(const std::string& room_id,
        const std::string& conversation_id,
        int64_t ts) override;

protected:
    virtual bool OnTimer() override;

private:
    int UpdateRtcSdpByPullers(std::vector<std::shared_ptr<MediaPuller>>& media_pullers, std::shared_ptr<RtcSdp> answer_sdp);
    void NotifyNewUser(const std::string& user_id, const std::string& user_name);
    void NotifyNewPusher(const std::string& pusher_user_id, 
        const std::string& pusher_user_name,
        const std::vector<PushInfo>& push_infos);
    int PullRemotePusher(const std::string& pusher_user_id, const PushInfo& push_info);
    int SendPullRequestToPilotCenter(const std::string& pusher_user_id, 
        const PushInfo& push_info,
        std::shared_ptr<RtcRecvRelay> relay_ptr);
    
private:
    int ReConnect(std::shared_ptr<RtcUser> new_user, int id, ProtooResponseI* resp_cb);

private:
    void Join2PilotCenter(std::shared_ptr<RtcUser> user_ptr);
    void JoinResponseFromPilotCenter(const std::string& method, json& data_json);
    void NewPusher2PilotCenter(const std::string& pusher_user_id, const std::vector<PushInfo>& push_infos);
    void LeaveFromPilotCenter(std::shared_ptr<RtcUser> user_ptr);
    void UserDisconnect2PilotCenter(const std::string& user_id);
    void UserLeave2PilotCenter(const std::string& user_id);

private:
    std::shared_ptr<RtcRecvRelay> CreateOrGetRecvRtcRelay(const std::string& pusher_user_id, const PushInfo& push_info);
    void ReleaseUserResources(const std::string& user_id);

private: // for voice agent
    void VoiceAgentAiJoin();
    int VoiceAgentPushVoice();
    RtpPacket* GenRtpPacketFromOpusData(uint8_t* opus_data, size_t opus_data_len);
    void OnSendVoiceAgentRtpPacket(int64_t now_ms);
    void ClearVoiceAgentRtpPacketsNoLock();

private:
    std::string room_id_;
    PilotClientI* pilot_client_ = nullptr;
	uv_loop_t* loop_ = nullptr;
    Logger* logger_ = nullptr;
    int64_t last_alive_ms_ = -1;

private:
    bool closed_ = false;
    std::map<std::string, std::shared_ptr<RtcUser>> users_;
    std::map<std::string, std::shared_ptr<MediaPusher>> pusherId2pusher_;
    std::map<std::string, std::map<std::string, std::shared_ptr<MediaPuller>>> pusher2pullers_;// pusher_id -> (puller_id -> MediaPuller)
    // pusher_id -> RtcRecvRelay
    std::map<std::string, std::shared_ptr<RtcRecvRelay>> pusherId2recvRelay_;
    // user_id -> RtcRecvRelay
    std::map<std::string, std::shared_ptr<RtcRecvRelay>> pusher_user_id2recvRelay_;
    // pusher_user_id -> RtcSendRelay
    std::map<std::string, std::shared_ptr<RtcSendRelay>> pusher_user_id2sendRelay_;

private:// for voice agent
    std::string voice_agent_ai_id_;
    bool voice_agent_ai_joined_ = false;
    bool voice_agent_ai_pushed_ = false;
    std::unique_ptr<VaFakeSession> va_fake_session_ptr_;
    std::shared_ptr<MediaPusher> voice_agent_pusher_ptr_;
    uint16_t va_rtp_seq_ = 0;
    const uint32_t va_rtp_ssrc_ = 1000;
    uint32_t va_rtp_timestamp_ = 0;
    std::mutex va_rtp_packets_mutex_;
    std::map<int, std::queue<RtpPacket*>> va_rtp_packets_;//current_index -> rtp packets
    int64_t last_send_va_rtp_ms_ = -1;
    int64_t last_send_va_sys_ms_ = -1;
    int current_index_ = -1;
};

} // namespace cpp_streamer

#endif // ROOM_HPP