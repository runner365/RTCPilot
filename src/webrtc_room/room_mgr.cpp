#include "room_mgr.hpp"
#include "room.hpp"
#include "rtc_user.hpp"
#include "ws_message/ws_protoo_info.hpp"
#include "webrtc_server.hpp"
#include "utils/timeex.hpp"
#include "config/config.hpp"

namespace cpp_streamer {

RoomMgr::RoomMgr(uv_loop_t* loop, Logger* logger) : TimerInterface(1000),
    loop_(loop),
    logger_(logger)
{
    StartTimer();
}

RoomMgr::~RoomMgr() {
    StopTimer();
}

void RoomMgr::OnProtooRequest(const int id, const std::string& method, json& j, ProtooResponseI* resp_cb) {
    int ret = 0;
    if (method != "heartbeat") {
        LogInfof(logger_, "OnProtooRequest, id:%d, method:%s, json:%s", id, method.c_str(), j.dump().c_str());
    }
    
    // Handle Protoo request
    if (method == "join") {
        json& data = j["data"];
        // Handle join request
        ret = HandleJoinRequest(id, data, resp_cb);
        if (ret != 0) {
            LogErrorf(logger_, "HandleJoinRequest failed, id:%d", id);
        }
    } else if (method == "push") {
        json& data = j["data"];
        // Handle push request
        ret = HandlePushRequest(id, data, resp_cb);
        if (ret != 0) {
            LogErrorf(logger_, "HandlePushRequest failed, id:%d", id);
        }
    } else if (method == "pull") {
        json& data = j["data"];
        ret = HandlePullRequest(id, data, resp_cb);
        if (ret != 0) {
            LogErrorf(logger_, "HandlePullRequest failed, id:%d", id);
        }
    } else if (method == "heartbeat") {
        // Handle heartbeat request
        json& data = j["data"];
        ret = HandleHeartbeatRequest(id, data, resp_cb);
        if (ret != 0) {
            LogErrorf(logger_, "HandleHeartbeatRequest failed, id:%d", id);
        }
    } else {
        LogErrorf(logger_, "Unknown Protoo request method:%s, id:%d", method.c_str(), id);
    }
}

void RoomMgr::OnProtooNotification(const std::string& method, json& j) {
    int ret = 0;
    if (method == "textMessage") {
        json& data = j["data"];
        ret = HandleTextMessageNotification(data);
        if (ret != 0) {
            LogErrorf(logger_, "HandleTextMessageNotification failed, method:%s", method.c_str());
        }
    }
}

int RoomMgr::HandleTextMessageNotification(nlohmann::json& data_json) {
    try {
        std::string room_id = data_json["roomId"];
        auto room_ptr = GetOrCreateRoom(room_id);
        std::string user_id = data_json["userId"];
        std::string user_name = data_json["userName"];
        std::string message = data_json["message"];
        LogInfof(logger_, "RoomMgr received text message notification, room_id:%s, from_user_id:%s, from_user_name:%s, message:%s",
            room_id.c_str(), user_id.c_str(), user_name.c_str(), message.c_str());
        room_ptr->NotifyTextMessage2LocalUsers(user_id, user_name, message);
        room_ptr->NotifyTextMessage2PilotCenter(user_id, user_name, message);
    } catch (const std::exception& e) {
        LogWarnf(logger_, "Invalid text message notification data, exception:%s", e.what());
        return -1;
    }
    return 0;
}
void RoomMgr::OnProtooResponse(const int id, int code, const std::string& err_msg, json& j) {
    // Handle Protoo response
}

void RoomMgr::OnWsSessionClose(const std::string& room_id, const std::string& user_id) {
	LogInfof(logger_, "OnWsSessionClose, room_id:%s, user_id:%s", room_id.c_str(), user_id.c_str());
    auto room_ptr = GetOrCreateRoom(room_id);
    room_ptr->DisconnectUser(user_id);
}

bool RoomMgr::OnTimer() {
    // Handle timer event
    if (pilot_client_) {
        if (last_pilot_heartbeat_ts_ <= 0) {
            last_pilot_heartbeat_ts_ = now_millisec();
        } else {
            int64_t now_ts = now_millisec();
            if (now_ts - last_pilot_heartbeat_ts_ < 5000) {
                return timer_running_;
            }
            last_pilot_heartbeat_ts_ = now_ts;
            json echo_data = json::object();
            echo_data["ts"] = now_millisec();
            echo_data["index"] = pilot_heartbeat_index_++;
            echo_data["ws_url"] = Config::Instance().my_ws_url_;
            int id = pilot_client_->AsyncRequest("echo", echo_data, this);
            if (id > 0) {
                id2ts_[id] = now_ts;
            }
        }
    }

    // Check room alive status  
    std::vector<std::string> closed_rooms;
    for (auto& room_kv : rooms_) {
        auto& room_ptr = room_kv.second;
        if (!room_ptr->IsAlive()) {
            closed_rooms.push_back(room_kv.first);
        }
    }
    for (const auto& room_id : closed_rooms) {
        LogInfof(logger_, "RoomMgr closing inactive room, room_id:%s", room_id.c_str());
        rooms_.erase(room_id);
    }
    return timer_running_;
}

void RoomMgr::OnAsyncRequestResponse(int id, const std::string& method, json& resp_json) {
    if (id > 0) {
        auto it = id2ts_.find(id);
        if (it != id2ts_.end()) {
            int64_t req_ts = it->second;
            int64_t now_ts = now_millisec();
            LogDebugf(logger_, "PilotClient in RoomMgr %s response time cost:%lld ms", method.c_str(), now_ts - req_ts);
            id2ts_.erase(it);
        } else {
            LogWarnf(logger_, "Cannot find request ts for id:%d", id);
            return;
        }
    } else {
        LogWarnf(logger_, "Invalid request id:%d in OnAsyncRequestResponse", id);
    }
    return;
}

void RoomMgr::OnAsyncNotification(const std::string& method, json& data_json) {
    LogInfof(logger_, "RoomMgr received async notification from PilotClient, method:%s, data:%s",
        method.c_str(), data_json.dump().c_str());
    std::string room_id;
    std::shared_ptr<Room> room_ptr;

    try {
        room_id = data_json["roomId"];
        room_ptr = GetOrCreateRoom(room_id);
    } catch (const std::exception& e) {
        LogWarnf(logger_, "Invalid async notification data, missing roomId, exception:%s", e.what());
        return;
    }
    
    // Handle async notification
    if (method == "newUser") {
        room_ptr->HandleNewUserNotificationFromCenter(data_json);
    } else if (method == "newPusher") {
        room_ptr->HandleNewPusherNotificationFromCenter(data_json);
    } else if (method == "pullRemoteStream") {
        room_ptr->HandlePullRemoteStreamNotificationFromCenter(data_json);
    } else if (method == "userDisconnect") {
        room_ptr->HandleUserDisconnectNotificationFromCenter(data_json);
    } else if (method == "userLeave") {
        room_ptr->HandleUserLeaveNotificationFromCenter(data_json);
    } else if (method == "textMessage") {
        room_ptr->HandleNotifyTextMessageFromCenter(data_json);
    } else {
        LogWarnf(logger_, "Unknown async notification method:%s", method.c_str());
    }
}

std::shared_ptr<Room> RoomMgr::GetOrCreateRoom(const std::string& room_id) {
    auto it = rooms_.find(room_id);
    if (it != rooms_.end()) {
        return it->second;
    }

    std::shared_ptr<Room> new_room = std::make_shared<Room>(room_id, 
        pilot_client_,
        loop_, logger_);
    rooms_[room_id] = new_room;
    return new_room;
}

std::shared_ptr<Room> RoomMgr::GetRoom(const std::string& room_id) {
    auto it = rooms_.find(room_id);
    if (it != rooms_.end()) {
        return it->second;
    }
    return nullptr;
}
void RoomMgr::RemoveRoom(const std::string& room_id) {
    auto it = rooms_.find(room_id);
    if (it != rooms_.end()) {
        it->second->Close();
        rooms_.erase(it);
        LogInfof(logger_, "RoomMgr removed room, room_id:%s", room_id.c_str());
    } else {
        LogWarnf(logger_, "RoomMgr cannot find room to remove, room_id:%s", room_id.c_str());
    }
}
int RoomMgr::HandleJoinRequest(int id, json& j, ProtooResponseI* resp_cb) {
    std::vector<std::shared_ptr<RtcUser>> users;
    try {
        std::string roomId = j["roomId"];
        std::string userId = j["userId"];
        std::string userName = j["userName"];
        std::string roomToken;
        if (j.contains("roomToken")) {
            roomToken = j["roomToken"];
        }
        bool audience = false;
        if (j.contains("audience")) {
            audience = j["audience"];
        }

        LogInfof(logger_, "join request roomId:%s, userId:%s, userName:%s, audience:%s, roomToken:%s",
            roomId.c_str(), userId.c_str(), userName.c_str(), BOOL2STRING(audience), roomToken.c_str());
		resp_cb->SetUserInfo(roomId, userId);
        auto room_ptr = GetOrCreateRoom(roomId);
        int ret = room_ptr->UserJoin(userId, userName, audience, id, resp_cb, roomToken);
        if (ret < 0) {
            json resp_json = json::object();
            resp_json["message"] = "user join failed";
            resp_json["code"] = ret;
            ProtooResponse resp(id, -1, "user join failed", resp_json);
            resp_cb->OnProtooResponse(resp);
            return ret;
        }
    } catch(const std::exception& e) {
        json resp_json = json::object();
        resp_json["message"] = "invalid join request:" + std::string(e.what());
        resp_json["code"] = -1;
        
        ProtooResponse resp(id, -1, "invalid join request", resp_json);

        resp_cb->OnProtooResponse(resp);
        return -1;
    }
    
    return 0;
}

int RoomMgr::HandlePushRequest(int id, json& j, ProtooResponseI* resp_cb) {
    std::string answer_sdp_str;
    int ret = -1;
    try {
        std::string userId = j["userId"];
        std::string roomId = j["roomId"];
        json sdp_json = j["sdp"];
        std::string sdp_type = sdp_json["type"];
        std::string sdp_str = sdp_json["sdp"];

        LogInfof(logger_, "handle push request, userId:%s, roomId:%s, type:%s, sdp:%s",
            userId.c_str(), roomId.c_str(), sdp_type.c_str(), sdp_str.c_str());

        auto room_ptr = GetOrCreateRoom(roomId);
        ret = room_ptr->HandlePushSdp(userId, sdp_type, sdp_str, id, resp_cb);
        if (ret < 0) {
            json resp_json = json::object();
            resp_json["message"] = "handle push sdp failed";
            resp_json["code"] = ret;
            ProtooResponse resp(id, -1, "handle push sdp failed", resp_json);
            resp_cb->OnProtooResponse(resp);
            return -1;
        }
    } catch(const std::exception& e) {
        json resp_json = json::object();
        resp_json["message"] = "invalid push request:" + std::string(e.what());
        resp_json["code"] = -1;
        
        ProtooResponse resp(id, -1, "invalid push request", resp_json);

        resp_cb->OnProtooResponse(resp);
        return -1;
    }

    return 0;
}

int RoomMgr::HandlePullRequest(int id, json& j, ProtooResponseI* resp_cb) {
    try {
        PullRequestInfo pull_info;
        std::string roomId = j["roomId"];
        std::string userId = j["userId"];
        std::string target_user_id = j["targetUserId"];
        auto pushs = j["specs"];
        auto sdp_json = j["sdp"];
        std::string sdp_str = sdp_json["sdp"];
        std::string sdp_type = sdp_json["type"];
        
        pull_info.room_id_ = roomId;
        pull_info.src_user_id_ = userId;
        pull_info.target_user_id_ = target_user_id;

        for (auto push_item : pushs) {
            PushInfo push_info;
            push_info.pusher_id_ = push_item["pusher_id"];
            std::string media_type_str = push_item["type"];
            if (media_type_str == "audio") {
                push_info.param_.av_type_ = MEDIA_AUDIO_TYPE;
            } else if (media_type_str == "video") {
                push_info.param_.av_type_ = MEDIA_VIDEO_TYPE;
            } else {
                push_info.param_.av_type_ = MEDIA_UNKNOWN_TYPE;
            }
            pull_info.pushers_.push_back(push_info);
        }

        std::string answer_sdp;
        auto room_ptr = GetOrCreateRoom(roomId);
        int ret = 0;
        auto user_type = room_ptr->GetUserType(target_user_id);

        if (user_type == REMOTE_RTC_USER) {
            ret = room_ptr->HandleRemotePullSdp(target_user_id, pull_info, sdp_type, sdp_str, id, resp_cb);
        } else if (user_type == LOCAL_RTC_USER) {
            ret = room_ptr->HandlePullSdp(pull_info, sdp_type, sdp_str, id, resp_cb);
        } else {
            LogErrorf(logger_, "unknown target user type for pull request, target_user_id:%s", target_user_id.c_str());
            ret = -1;
        }
        if (ret < 0) {
            json resp_json = json::object();
            resp_json["message"] = "handle pull sdp failed";
            resp_json["code"] = ret;
            ProtooResponse resp(id, -1, "handle pull sdp failed", resp_json);
            resp_cb->OnProtooResponse(resp);
            return -1;
        }
    } catch(const std::exception& e) {
        LogErrorf(logger_, "HandlePullRequest exception:%s", e.what());
        json resp_json = json::object();
        resp_json["message"] = "invalid pull request:" + std::string(e.what());
        resp_json["code"] = -1;
        
        ProtooResponse resp(id, -1, "invalid pull request", resp_json);

        resp_cb->OnProtooResponse(resp);
        return -1;
    }
    return 0;
}

int RoomMgr::HandleHeartbeatRequest(int id, json& data, ProtooResponseI* resp_cb) {
    try {
        LogInfof(logger_, "HandleHeartbeatRequest, id:%d, json:%s", id, data.dump().c_str());
        //{"roomId":"qvrwn9bs","time":1766130710245,"userId":"8385","userName":"User_8385"}
        std::string roomId = data["roomId"];
        std::string userId = data["userId"];
        LogDebugf(logger_, "heartbeat request roomId:%s, userId:%s",
            roomId.c_str(), userId.c_str());

        auto room_ptr = GetOrCreateRoom(roomId);
        int ret = room_ptr->HandleWsHeartbeat(userId);
        if (ret < 0) {
            json resp_json = json::object();
            resp_json["message"] = "handle heartbeat failed";
            resp_json["code"] = ret;
            ProtooResponse resp(id, -1, "handle heartbeat failed", resp_json);
            resp_cb->OnProtooResponse(resp);
            return ret;
        }
        json resp_json = json::object();
        resp_json["message"] = "ok";
        resp_json["code"] = 0;
        resp_json["roomId"] = roomId;
        resp_json["userId"] = userId;
        
        ProtooResponse resp(id, 0, "ok", resp_json);

        resp_cb->OnProtooResponse(resp);
    } catch(const std::exception& e) {
        json resp_json = json::object();
        resp_json["message"] = "invalid heartbeat request:" + std::string(e.what());
        resp_json["code"] = -1;
        
        ProtooResponse resp(id, -1, "invalid heartbeat request", resp_json);

        resp_cb->OnProtooResponse(resp);
        return -1;
    }
    
    return 0;
}

} // namespace cpp_streamer