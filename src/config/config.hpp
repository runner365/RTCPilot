#ifndef CONFIG_HPP
#define CONFIG_HPP
#include "format/rtc_sdp/rtc_sdp.hpp"
#include <string>
#include <stdint.h>
#include <stddef.h>
#include <vector>

using namespace cpp_streamer;

class RtcCandidate
{
public:
    RtcCandidate(RtcNetType net_type, 
        const std::string& candidate_ip, 
        const std::string& listen_ip, uint16_t port) :
        net_type_(net_type),
        candidate_ip_(candidate_ip),
        listen_ip_(listen_ip),
        port_(port)
    {}
    ~RtcCandidate() {}
public:
    RtcNetType net_type_;
    std::string candidate_ip_;
    std::string listen_ip_;
    uint16_t port_ = 0;
};

class PilotCenterConfig
{
public:
    PilotCenterConfig() = default;
    ~PilotCenterConfig() = default;
public:
	bool 	    enable_ = false;
    std::string host_;
    uint16_t    port_ = 0;
    std::string subpath_;
};

class RelayConfig
{
public:
    RelayConfig() = default;
    ~RelayConfig() = default;

public:
    std::string relay_server_ip_;
    uint16_t    relay_udp_start_ = 0;
    uint16_t    relay_udp_end_ = 0;
    uint32_t send_discard_percent_ = 0;
    uint32_t recv_discard_percent_ = 0;
};

class RtmpConfig
{
public:
    RtmpConfig() = default;
    ~RtmpConfig() = default;

public:
    bool        enable_ = true;
    std::string listen_ip_ = "0.0.0.0";
    uint16_t    port_ = 1935;
};

class HttpFlvConfig
{
public:
    HttpFlvConfig() = default;
    ~HttpFlvConfig() = default;

public:
    bool        enable_ = true;
    std::string listen_ip_ = "0.0.0.0";
    uint16_t    port_ = 8080;
};

class WsStreamConfig
{
public:
    WsStreamConfig() = default;
    ~WsStreamConfig() = default;

public:
    bool        enable_ = true;
    std::string listen_ip_ = "0.0.0.0";
    uint16_t    port_ = 8443;
};

class WSSignalConfig
{
public:
    WSSignalConfig() = default;
    ~WSSignalConfig() = default;

public:
    bool ssl_enable_ = false;
    std::string cert_path_;
    std::string key_path_;
    std::string listen_ip_ = "0.0.0.0";
    uint16_t    port_ = 8443;
};

/*
whip_server:
  enable: true
  ssl_enable: false
  cert_path: "certificate.crt"
  key_path: "private.key"
  listen_ip: "0.0.0.0"
  port: 6443
*/
class WhipServerConfig
{
public:
    WhipServerConfig() = default;
    ~WhipServerConfig() = default;
public:
    bool        enable_ = false;
    bool        ssl_enable_ = false;
    std::string cert_path_;
    std::string key_path_;
    std::string listen_ip_ = "0.0.0.0";
    uint16_t    port_ = 6443;
};

class EventLogConfig
{
public:
    EventLogConfig() = default;
    ~EventLogConfig() = default;

public:
    std::string rtc_log_path_;
    std::string rtc_stream_log_path_;
};

/*
how to download models:
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/matcha-icefall-zh-baker.tar.bz2
tar xvf matcha-icefall-zh-baker.tar.bz2
rm matcha-icefall-zh-baker.tar.bz2
*/
class TtsConfig
{
public:
    TtsConfig() = default;
    ~TtsConfig() = default;
public:
    bool        tts_enable_ = false;
    std::string acoustic_model_;
    std::string vocoder_;
    std::string lexicon_;
    std::string tokens_;
    std::string dict_dir_;
    int32_t num_threads_ = 1;
};

class VoiceAgentConfig
{
public:
    VoiceAgentConfig() = default;
    ~VoiceAgentConfig() = default;

public:
    bool        enable_ = false;
    std::string agent_ip_;
    uint16_t    agent_port_ = 0;
    std::string agent_subpath_;

public:
    TtsConfig   tts_config_;
};

class Config
{
public:
    ~Config() {}
    static Config& Instance();
    int LoadConfig(const std::string& config_file);
    std::string Dump();

public:
    std::string log_path_;
    std::string log_level_;
    bool        log_console_ = false;
    
public:
    EventLogConfig event_log_cfg_;
    
public:
    WSSignalConfig ws_signal_cfg_;

public:
    WhipServerConfig whip_server_cfg_;

public:
    std::vector<RtcCandidate> rtc_candidates_;
    std::string cert_path_;
    std::string key_path_;

public:
    RtmpConfig     rtmp_cfg_;
    HttpFlvConfig  httpflv_cfg_;
    WsStreamConfig ws_stream_cfg_;

public:
    PilotCenterConfig pilot_center_cfg_;

public:
    RelayConfig relay_cfg_;
    
public:
    uint32_t downlink_discard_percent_ = 0;
    uint32_t uplink_discard_percent_ = 0;

public:
    // 向外暴露自己webrtc sfu服务的websocket链接方式
    std::string my_ws_url_;

public:
    VoiceAgentConfig voice_agent_cfg_;

private:
    Config() {}
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

private:
    static Config* instance_;


};
#endif // CONFIG_HPP