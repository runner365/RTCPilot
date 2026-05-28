#include "pilot_message_client.hpp"
#include "utils/uuid.hpp"
#include "utils/timeex.hpp"

#include <atomic>

namespace cpp_streamer 
{

// request_id_ is a member of PilotMessageClient now; no global counter.

PilotMessageClient::PilotMessageClient(const PilotCenterConfig& cfg, uv_loop_t* loop, Logger* logger)
	: cfg_(cfg), 
    loop_(loop),
    logger_(logger)
{
	// Create WsProtooClient but do not initiate network here (caller calls AsyncConnect())
}

PilotMessageClient::~PilotMessageClient()
{
	ws_protoo_client_ptr_.reset();
}

void PilotMessageClient::AsyncConnect()
{
    if (is_connected_) {
        LogInfof(logger_, "PilotMessageClient: Already connected, AsyncConnect skipped");
        return;
    }
    if (last_connecting_ms_ > 0) {
        int64_t now_ms = now_millisec();
        if (now_ms - last_connecting_ms_ < 10*1000) {
            // Avoid frequent reconnect attempts within 60 seconds
            return;
        }
    }
    last_connecting_ms_ = now_millisec();
    if (ws_protoo_client_ptr_) {
        old_clients_.insert(std::make_pair(last_connecting_ms_, ws_protoo_client_ptr_));
    }
    try {
	    ws_protoo_client_ptr_ = std::make_shared<WsProtooClient>(loop_,
														   cfg_.host_,
														   cfg_.port_,
														   cfg_.subpath_,
														   true, /* ssl_enable */
														   logger_,
														   this);
        LogInfof(logger_, "PilotMessageClient::AsyncConnect: host=%s, port=%d, subpath=%s", cfg_.host_.c_str(), cfg_.port_, cfg_.subpath_.c_str());
        ws_protoo_client_ptr_->AsyncConnect();
    } catch(const std::exception& e) {
        LogErrorf(logger_, "PilotMessageClient::AsyncConnect exception: %s", e.what());
    }
}

int PilotMessageClient::AsyncRequest(const std::string& method, json& data_json, AsyncRequestCallbackI* cb)
{
    if (is_connected_ == false) {
        LogWarnf(logger_, "PilotMessageClient: AsyncRequest called but not connected");
        AsyncConnect();
        return -1;
    }
    if (!ws_protoo_client_ptr_) {
        LogWarnf(logger_, "PilotMessageClient: AsyncRequest called but ws_protoo_client_ptr_ is null");
        return -1;
    }
	int id = request_id_++;
	try {
		std::string payload = data_json.dump();
		ws_protoo_client_ptr_->SendRequest(id, method, payload);
		LogInfof(logger_, "PilotMessageClient: Sent request id=%d method=%s", id, method.c_str());
	} catch (const std::exception& e) {
        is_connected_ = false;
		LogErrorf(logger_, "PilotMessageClient::AsyncRequest exception: %s", e.what());
        return -1;
	}
    if (cb != nullptr) {
        async_request_cbs_.insert(std::make_pair(id, PilotCallbackInfo(id, method, cb)));
    }
    return id;
}

void PilotMessageClient::AsyncNotification(const std::string& method, json& data_json)
{
    if (is_connected_ == false) {
        LogWarnf(logger_, "PilotMessageClient: AsyncNotification called but not connected");
        AsyncConnect();
        return;
    }
	if (!ws_protoo_client_ptr_) {
		LogWarnf(logger_, "PilotMessageClient: AsyncNotification called but ws_protoo_client_ptr_ is null");
		return;
	}
	try {
		std::string payload = data_json.dump();
		ws_protoo_client_ptr_->SendNotification(method, payload);
		LogInfof(logger_, "PilotMessageClient: Sent notification method=%s, data:%s", method.c_str(), payload.c_str());
	} catch (const std::exception& e) {
        is_connected_ = false;
		LogErrorf(logger_, "PilotMessageClient::AsyncNotification exception: %s", e.what());
	}
}

// WsProtooClient callbacks
void PilotMessageClient::OnConnected()
{
    is_connected_ = true;
	LogInfof(logger_, "PilotMessageClient connected to %s:%d%s", cfg_.host_.c_str(), (int)cfg_.port_, cfg_.subpath_.c_str());
}

void PilotMessageClient::OnResponse(const std::string& text)
{
	try {
		json j = json::parse(text);
		LogInfof(logger_, "PilotMessageClient received response: %s", text.c_str());
        int id = j.value("id", -1);
        auto it = async_request_cbs_.find(id);
        if (it != async_request_cbs_.end()) {
            PilotCallbackInfo cb_info = it->second;
            int64_t now_ms = now_millisec();
            async_request_cbs_.erase(it);
            if (cb_info.callback && now_ms - cb_info.created_ms_ < 8000) {
                std::string method = cb_info.method_;
                json data = j["data"];

                cb_info.callback->OnAsyncRequestResponse(id, method, data);
            }
        } else {
            LogWarnf(logger_, "PilotMessageClient received response with unknown id: %d", id);
        }
		// Optionally handle response payload here
	} catch (const std::exception& e) {
		LogWarnf(logger_, "PilotMessageClient failed to parse response JSON: %s", e.what());
	}
}

void PilotMessageClient::OnNotification(const std::string& text)
{
	try {
		json j = json::parse(text);
		LogInfof(logger_, "PilotMessageClient received notification: %s", text.c_str());
        /*
        {"notification": true, "method": "newUser", "data": {"roomId": "10pyp92u", "userId": "6512", "userName": "User_6512"}}
        */
        std::string method = j["method"];
        json data = j["data"];

        if (async_notification_cb_) {
            async_notification_cb_->OnAsyncNotification(method, data);
        }
		// Optionally handle notification here
	} catch (const std::exception& e) {
		LogWarnf(logger_, "PilotMessageClient failed to parse notification JSON: %s", e.what());
	}
}

void PilotMessageClient::OnClosed(int code, const std::string& reason)
{
    is_connected_ = false;
	LogInfof(logger_, "PilotMessageClient connection closed: code=%d reason=%s", code, reason.c_str());
}

}