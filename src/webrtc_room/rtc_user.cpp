#include "rtc_user.hpp"
#include "utils/json.hpp"

using json = nlohmann::json;
namespace cpp_streamer {

RtcUser::RtcUser(const std::string& roomId,
    const std::string& user_id,
    const std::string& user_name,
    bool audience,
    ProtooResponseI* resp_cb,
    Logger* logger,
    const std::string& room_token)
    : room_id_(roomId),
      user_id_(user_id),
      user_name_(user_name),
      room_token_(room_token),
      resp_cb_(resp_cb),
      logger_(logger)
{
    audience_ = audience;
    last_heartbeat_ms_ = now_millisec();
    LogInfof(logger_, "RtcUser construct, roomId:%s, userId:%s, userName:%s",
        room_id_.c_str(), user_id_.c_str(), user_name_.c_str());
}

RtcUser::~RtcUser()
{
	resp_cb_ = nullptr;
    LogInfof(logger_, "RtcUser destruct, roomId:%s, userId:%s, userName:%s",
        room_id_.c_str(), user_id_.c_str(), user_name_.c_str());
}

void RtcUser::UpdateHeartbeat(int64_t now_ms) {
    if (now_ms == 0) {
        now_ms = now_millisec();
    }
    last_heartbeat_ms_ = now_ms;
}
bool RtcUser::IsAlive() {
    const int64_t HEARTBEAT_TIMEOUT_MS = 45*1000;
    const int64_t DISCONNECT_TIMEOUT_MS = 20*1000;
    int64_t timeout_ms = HEARTBEAT_TIMEOUT_MS;

    if (resp_cb_ == nullptr) {
        timeout_ms += DISCONNECT_TIMEOUT_MS;
    }
    int64_t now_ms = now_millisec();
    return (now_ms - (int64_t)last_heartbeat_ms_) <= timeout_ms;
}

void RtcUser::AddPusher(const std::string& pusher_id, PushInfo& push_info) {
    json dump_json = json::object();
    push_info.DumpJson(dump_json);
    LogInfof(logger_, "RtcUser::AddPusher called, roomId:%s, userId:%s, pusherId:%s, pushInfo:%s",
        room_id_.c_str(), user_id_.c_str(), pusher_id.c_str(), dump_json.dump().c_str());
    pushers_[pusher_id] = push_info;
}

bool RtcUser::GetPusher(const std::string& pusher_id, PushInfo& push_info) {
    auto it = pushers_.find(pusher_id);
    if (it == pushers_.end()) {
        return false;
    }
    push_info = it->second;
    return true;
}

std::map<std::string, PushInfo>& RtcUser::GetPushers() {
    return pushers_;
}

ProtooResponseI* RtcUser::GetRespCb() {
    return resp_cb_;
}
void RtcUser::SetRespCb(ProtooResponseI* resp_cb) {
    resp_cb_ = resp_cb;
}

} // namespace cpp_streamer