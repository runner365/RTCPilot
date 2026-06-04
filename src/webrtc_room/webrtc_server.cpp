#include "webrtc_server.hpp"
#include "webrtc_session.hpp"
#include "net/stun/stun.hpp"
#include <vector>
#include <arpa/inet.h>

namespace cpp_streamer {

std::unordered_map<std::string, std::shared_ptr<WebRtcSession>> WebRtcServer::username2sessions_;//ice_username ->session
std::unordered_map<uint64_t, std::shared_ptr<WebRtcSession>> WebRtcServer::addr2sessions_;//address ->session

WebRtcServer::WebRtcServer(uv_loop_t* loop, Logger* logger, const RtcCandidate& candidate) :
    TimerInterface(1000),
    loop_(loop),
    logger_(logger),
    rtc_candidate_(candidate)
{
    LogInfof(logger_, "WebRtcServer construct candidate_ip:%s, listen_ip:%s, port:%u", 
        rtc_candidate_.candidate_ip_.c_str(), rtc_candidate_.listen_ip_.c_str(), 
        rtc_candidate_.port_);
    udp_server_ = std::make_unique<UdpServer>(loop_, 
        rtc_candidate_.listen_ip_, 
        rtc_candidate_.port_, 
        this, 
        logger_);
    StartTimer();
}

WebRtcServer::~WebRtcServer() {
    LogInfof(logger_, "WebRtcServer destruct");
    StopTimer();
}

void WebRtcServer::OnWrite(size_t sent_size, UdpTuple address) {
    // Handle UDP write completion if needed
}

void WebRtcServer::OnRead(const char* data, size_t data_size, UdpTuple address) {
    // Handle incoming UDP data

    if (StunPacket::IsStun((const uint8_t*)data, data_size)) {
        HandleStunPacket((const uint8_t*)data, data_size, address);
    } else {
        HandleNoneStunPacket((const uint8_t*)data, data_size, address);
    }
}

bool WebRtcServer::OnTimer() {
    std::vector<uint64_t> addr_to_remove;
    std::vector<std::string> ufrag_remove;

    for (auto& kv : WebRtcServer::addr2sessions_) {
        auto& session = kv.second;
        if (!session->IsAlive()) {
            LogInfof(logger_, "WebRtcServer remove inactive session:%s",
                session->GetSessionId().c_str());
            ufrag_remove.push_back(session->GetIceUfrag());
            addr_to_remove.push_back(kv.first);
        }
    }

    for (const auto& addr : addr_to_remove) {
        WebRtcServer::addr2sessions_.erase(addr);
    }
    for (const auto& ufrag : ufrag_remove) {
        WebRtcServer::username2sessions_.erase(ufrag);
    }
    return timer_running_;
}

void WebRtcServer::RemoveSessionByRoomId(const std::string& room_id) {
    std::vector<uint64_t> addr_to_remove;
    std::vector<std::string> ufrag_remove;

    for (auto& kv : WebRtcServer::addr2sessions_) {
        auto& session = kv.second;
        if (session->GetRoomId() == room_id) {
            ufrag_remove.push_back(session->GetIceUfrag());
            addr_to_remove.push_back(kv.first);
        }
    }
    for (const auto& addr : addr_to_remove) {
        WebRtcServer::addr2sessions_.erase(addr);
    }
    for (const auto& ufrag : ufrag_remove) {
        WebRtcServer::username2sessions_.erase(ufrag);
    }
}

void WebRtcServer::HandleStunPacket(const uint8_t* data, size_t data_size, UdpTuple address) {
    std::vector<uint8_t> stun_data(data_size);
    uint8_t* p = &stun_data[0];
    memcpy(p, data, data_size);
    try {
        auto stun_pkt = StunPacket::Parse(p, data_size);
        if (!stun_pkt) {
            LogErrorf(logger_, "stun packet parse error");
            return;
        }
        std::string key = GetKeyByUsername(stun_pkt->username_);
        LogDebugf(logger_, "stun packet key:%s", key.c_str());
        auto it = WebRtcServer::username2sessions_.find(key);
        if (it != WebRtcServer::username2sessions_.end()) {
            WebRtcServer::SetAddr2Session(address.to_u64(), it->second);
            it->second->HandleStunPacket(stun_pkt, this, address);
        } else {
            LogErrorf(logger_, "no stun session found by username key:%s", key.c_str());
        }
        delete stun_pkt;
    }
    catch (std::exception& e) {
        LogErrorf(logger_, "handle stun packet exception:%s", e.what());
    }
}

void WebRtcServer::HandleNoneStunPacket(const uint8_t* data, size_t data_size, UdpTuple address) {
    auto addr_key = address.to_u64();
    auto it = WebRtcServer::addr2sessions_.find(addr_key);
    if (it == WebRtcServer::addr2sessions_.end()) {
        LogErrorf(logger_, "no non-stun session found by addr:%llu", addr_key);
        return;
    }

    it->second->HandleNoneStunPacket(data, data_size, this, address);
}

void WebRtcServer::SetUserName2Session(const std::string& username, std::shared_ptr<WebRtcSession> session) {
    WebRtcServer::username2sessions_[username] = session;
}

void WebRtcServer::SetAddr2Session(uint64_t addr_u64, std::shared_ptr<WebRtcSession> session) {
    WebRtcServer::addr2sessions_[addr_u64] = session;
}

std::string WebRtcServer::GetKeyByUsername(const std::string& username) {
    size_t pos = username.find(':');
    if (pos == std::string::npos) {
        return username;
    }
    return username.substr(0, pos);
}

void WebRtcServer::OnWriteUdpData(const uint8_t* data, size_t sent_size, UdpTuple address) {
    udp_server_->Write((const char*)data, sent_size, address);
}

} // namespace cpp_streamer