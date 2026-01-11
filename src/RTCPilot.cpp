#ifdef _WIN64
#define WIN32_LEAN_AND_MEAN
#endif
#include "net/rtmp/rtmp_server.hpp"
#include "net/httpflv/httpflv_server.hpp"
#include "format/rtc_sdp/rtc_sdp_filter.hpp"
#include "ws_stream/ws_stream_server.hpp"
#include "ws_message/ws_message_server.hpp"
#include "webrtc_room/dtls_session.hpp"
#include "webrtc_room/srtp_session.hpp"
#include "webrtc_room/webrtc_server.hpp"
#include "webrtc_room/room_mgr.hpp"
#include "webrtc_room/pilot_message_client.hpp"
#include "webrtc_room/port_generator.hpp"
#include "config/config.hpp"
#include "utils/logger.hpp"
#include "utils/av/media_stream_manager.hpp"
#include "utils/timeex.hpp"
#include "utils/byte_crypto.hpp"

#include <thread>
#include <vector>
#ifdef _WIN64
#include <stdlib.h>
#include <crtdbg.h>
#endif

using namespace cpp_streamer;

enum LOGGER_LEVEL GetLogLevelFromString(const std::string& level_str) {
    if (level_str == "debug") {
        return LOGGER_DEBUG_LEVEL;
    } else if (level_str == "info") {
        return LOGGER_INFO_LEVEL;
    } else if (level_str == "warn") {
        return LOGGER_WARN_LEVEL;
    } else if (level_str == "error") {
        return LOGGER_ERROR_LEVEL;
    }
    return LOGGER_INFO_LEVEL;
}

int main(int argc, char* argv[]) {
#ifdef _WIN64
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
    if (argc < 2) {
        std::cout << "Usage: RTCPilot <config_file>" << std::endl;
        return -1;
    }
    std::string config_file = argv[1];

    // Create an event loop
    uv_loop_t* loop = uv_default_loop();

    InitSdpFilter();
    ByteCrypto::Init();
    StreamerTimerInitialize(loop, 5);

    try {
        Config::Instance().LoadConfig(config_file);
    } catch(const std::exception& e) {
        std::cerr << e.what() << '\n';
        return -1;
    }
	std::string log_file = Config::Instance().log_path_;
    log_file += ".";
    log_file += get_now_str_for_filename();
    auto log_level = GetLogLevelFromString(Config::Instance().log_level_);
    // Initialize logger
	std::unique_ptr<Logger> logger(new Logger(log_file, log_level));
    
    LogInfof(logger.get(), "Loaded config:\n%s", Config::Instance().Dump().c_str());
    MediaStreamManager::SetLogger(logger.get());

    try {
        int ret = DtlsSession::Init(Config::Instance().cert_path_, Config::Instance().key_path_);
        if (ret != 0) {
            LogErrorf(logger.get(), "Failed to initialize DtlsSession");
            return -1;
        }
        LogInfof(logger.get(), "DtlsSession initialized successfully");
        ret = SRtpSession::GlobalInit();
        if (ret != 0) {
            LogErrorf(logger.get(), "Failed to initialize SRtpSession");
            return -1;
        }
        LogInfof(logger.get(), "SRtpSession initialized successfully");
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return -1;
    }
    std::unique_ptr<PilotMessageClient> pilot_client;
    PilotCenterConfig& pilot_cfg =  Config::Instance().pilot_center_cfg_;
    if (pilot_cfg.enable_ && !pilot_cfg.host_.empty() && pilot_cfg.port_ != 0 && !pilot_cfg.subpath_.empty()) {
        pilot_client.reset(new PilotMessageClient(pilot_cfg, loop, logger.get()));
    }
    else {
        LogInfof(logger.get(), "pilot center is disable.");
    }
    // Create and run the RTMP server
    if (Config::Instance().rtmp_cfg_.enable_) {
        RtmpServer rtmp_server(loop, Config::Instance().rtmp_cfg_.listen_ip_, Config::Instance().rtmp_cfg_.port_, logger.get());
        LogInfof(logger.get(), "Starting RTMP server on %s:%d", 
            Config::Instance().rtmp_cfg_.listen_ip_.c_str(), 
            Config::Instance().rtmp_cfg_.port_);
    } else {
        LogInfof(logger.get(), "RTMP server is disabled");
    }

    // Create and run the HTTP-FLV server
    if (Config::Instance().httpflv_cfg_.enable_) {
        HttpFlvServer httpflv_server(loop, Config::Instance().httpflv_cfg_.listen_ip_, Config::Instance().httpflv_cfg_.port_, logger.get());
        LogInfof(logger.get(), "Starting httpflv server on %s:%d", 
            Config::Instance().httpflv_cfg_.listen_ip_.c_str(), 
            Config::Instance().httpflv_cfg_.port_);
    } else {
        LogInfof(logger.get(), "HTTP-FLV server is disabled");
    }

    // Create and run the WebSocket stream server
    if (Config::Instance().ws_stream_cfg_.enable_) {
        WsStreamServer ws_stream_server(Config::Instance().ws_stream_cfg_.listen_ip_, 
            Config::Instance().ws_stream_cfg_.port_, 
            loop, 
            logger.get());
        LogInfof(logger.get(), "Starting ws_stream(flv) server on %s:%d", 
            Config::Instance().ws_stream_cfg_.listen_ip_.c_str(), 
            Config::Instance().ws_stream_cfg_.port_);
    } else {
        LogInfof(logger.get(), "WebSocket stream server is disabled");
    }

    WsMessageServer ws_message_server(Config::Instance().ws_listen_ip_, 
                            Config::Instance().ws_listen_port_, 
                            loop, 
                            logger.get());
    LogInfof(logger.get(), "Starting ws_message server on %s:%d for webrtc signaling",
        Config::Instance().ws_listen_ip_.c_str(),
        Config::Instance().ws_listen_port_);
    std::vector<std::shared_ptr<WebRtcServer>> webrtc_servers;

    for (auto& candidate : Config::Instance().rtc_candidates_) {
        LogInfof(logger.get(), "Configured RTC candidate, nettype:%s, candidate_ip:%s, listen_ip:%s, port:%d",
            (candidate.net_type_ == RTC_NET_TCP) ? "tcp" : 
             (candidate.net_type_ == RTC_NET_UDP) ? "udp" : "unknown",
            candidate.candidate_ip_.c_str(),
            candidate.listen_ip_.c_str(),
            candidate.port_);
        auto webrtc_server_ptr = std::make_shared<WebRtcServer>(loop, logger.get(), candidate);
        webrtc_servers.push_back(webrtc_server_ptr); 
    }
    
    if (Config::Instance().pilot_center_cfg_.enable_ && pilot_client) {
        RoomMgr::Instance(loop, logger.get()).SetPilotClient(pilot_client.get());

        PortGenerator::Instance()->Initialize(
            Config::Instance().relay_cfg_.relay_udp_start_,
            Config::Instance().relay_cfg_.relay_udp_end_, logger.get());
        pilot_client->SetAsyncNotificationCallbackI(&RoomMgr::Instance(loop, logger.get()));
	}

    try {
        std::cout << "server is running..." << std::endl;
        uv_run(loop, UV_RUN_DEFAULT);
    } catch (const std::exception& e) {
        LogErrorf(logger.get(), "Exception in event loop: %s", e.what());
    }
	LogInfof(logger.get(), "live server instance exiting...");

    ByteCrypto::DeInit();
    DtlsSession::CleanupGlobal();
#ifdef _WIN64
    _CrtDumpMemoryLeaks();
#endif
    return 0;
}