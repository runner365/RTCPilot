#ifndef RTC_USER_HPP
#define RTC_USER_HPP
#include "utils/logger.hpp"
#include "utils/av/av.hpp"
#include "rtc_info.hpp"

#include <map>

namespace cpp_streamer {

class ProtooResponseI;
class RtcUser
{
public:
    RtcUser(const std::string& roomId,
        const std::string& user_id,
        const std::string& user_name,
        bool audience,
        ProtooResponseI* resp_cb,
        Logger* logger,
        const std::string& room_token = "");
    ~RtcUser();

public:
    std::string GetRoomId() { return room_id_; }
    std::string GetUserId() { return user_id_; }
    std::string GetUserName() { return user_name_; }
    std::string GetRoomToken() { return room_token_; }
    bool IsRemote() { return is_remote_; }
    void SetRemote(bool is_remote) { is_remote_ = is_remote; }
    bool IsWhip() { return is_whip_; }
    void SetWhip(bool is_whip) { is_whip_ = is_whip; }
    bool IsAudience() { return audience_; }
    void SetAudience(bool audience) { audience_ = audience; }
public:
    void UpdateHeartbeat(int64_t now_ms = 0);
    bool IsAlive();
    void AddPusher(const std::string& pusher_id, PushInfo& push_info);
    std::map<std::string, PushInfo>& GetPushers();
    bool GetPusher(const std::string& pusher_id, PushInfo&);
    ProtooResponseI* GetRespCb();
    void SetRespCb(ProtooResponseI* resp_cb);

private:
    std::string room_id_;
    std::string user_id_;
    std::string user_name_;
    std::string room_token_;
    bool audience_ = false;
    bool is_remote_ = false;
    bool is_whip_ = false;
    ProtooResponseI* resp_cb_ = nullptr;
    Logger* logger_ = nullptr;
    
private:
    uint64_t last_heartbeat_ms_ = 0;

private:
    std::map<std::string, PushInfo> pushers_;// pusher_id -> PushInfo
};

} // namespace cpp_streamer

#endif // RTC_USER_HPP