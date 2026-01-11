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
    std::string ws_listen_ip_;
    uint16_t    ws_listen_port_ = 0;
    
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

private:
    Config() {}
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

private:
    static Config* instance_;


};
#endif // CONFIG_HPP