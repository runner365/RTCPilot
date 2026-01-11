#include "config.hpp"
#include "yaml-cpp/yaml.h"
#include <iostream>

Config* Config::instance_ = nullptr;

Config& Config::Instance() {
    if (instance_ == nullptr) {
        instance_ = new Config();
    }
    return *instance_;
}

int Config::LoadConfig(const std::string& config_file) {
    int ret = -1;

    try {
        YAML::Node config = YAML::LoadFile(config_file);
        if (!config) {
            std::cerr << "Failed to load config file: " << config_file << '\n';
            return ret;
		}
        if (config["log"]) {
            auto log_cfg = config["log"];
            if (log_cfg["log_path"]) {
                log_path_ = log_cfg["log_path"].as<std::string>();
            }
            if (log_cfg["log_level"]) {
                log_level_ = log_cfg["log_level"].as<std::string>();
            }
            if (log_cfg["log_console"]) {
                log_console_ = log_cfg["log_console"].as<bool>();
            }
        }

        if (config["websocket_server"]) {
            auto ws_cfg = config["websocket_server"];
            if (ws_cfg["listen_ip"]) {
                ws_listen_ip_ = ws_cfg["listen_ip"].as<std::string>();
            }
            if (ws_cfg["port"]) {
                ws_listen_port_ = ws_cfg["port"].as<uint16_t>();
            }
        }
        
        if (config["cert_path"]) {
            cert_path_ = config["cert_path"].as<std::string>();
		}
        if (config["key_path"]) {
            key_path_ = config["key_path"].as<std::string>();
        }
        if (config["downlink_discard_percent"]) {
            downlink_discard_percent_ = config["downlink_discard_percent"].as<uint32_t>();
        }
        if (config["uplink_discard_percent"]) {
            uplink_discard_percent_ = config["uplink_discard_percent"].as<uint32_t>();
        }

		auto candidates_node = config["candidates"];
        if (candidates_node && candidates_node.IsSequence()) {
            for (const auto& candidate_node : candidates_node) {
                std::string nettype_str = candidate_node["nettype"].as<std::string>();
                RtcNetType net_type;
                if (nettype_str == "tcp") {
                    net_type = RTC_NET_TCP;
                } else if (nettype_str == "udp") {
                    net_type = RTC_NET_UDP;
                } else {
                    throw std::invalid_argument("Unknown nettype: " + nettype_str);
                }
                std::string candidate_ip = candidate_node["candidate_ip"].as<std::string>();
                std::string listen_ip = candidate_node["listen_ip"].as<std::string>();
                uint16_t port = candidate_node["port"].as<uint16_t>();
                RtcCandidate candidate(net_type, candidate_ip, listen_ip, port);
                rtc_candidates_.push_back(candidate);
            }
        }

        auto pilot_center_node = config["pilot_center"];
        if (pilot_center_node) {
            if (pilot_center_node["enable"]) {
                pilot_center_cfg_.enable_ = pilot_center_node["enable"].as<bool>();
			}
            if (pilot_center_node["host"]) {
                pilot_center_cfg_.host_ = pilot_center_node["host"].as<std::string>();
            }
            if (pilot_center_node["port"]) {
                pilot_center_cfg_.port_ = pilot_center_node["port"].as<uint16_t>();
            }
            if (pilot_center_node["subpath"]) {
                pilot_center_cfg_.subpath_ = pilot_center_node["subpath"].as<std::string>();
            }
        }
        auto rtc_relay_node = config["rtc_relay"];
        if (rtc_relay_node) {
            if (rtc_relay_node["relay_server_ip"]) {
                relay_cfg_.relay_server_ip_ = rtc_relay_node["relay_server_ip"].as<std::string>();
            }
            if (rtc_relay_node["relay_udp_start"]) {
                relay_cfg_.relay_udp_start_ = rtc_relay_node["relay_udp_start"].as<uint16_t>();
            }
            if (rtc_relay_node["relay_udp_end"]) {
                relay_cfg_.relay_udp_end_ = rtc_relay_node["relay_udp_end"].as<uint16_t>();
            }
            if (rtc_relay_node["send_discard_percent"]) {
                relay_cfg_.send_discard_percent_ = rtc_relay_node["send_discard_percent"].as<uint32_t>();
            }
            if (rtc_relay_node["recv_discard_percent"]) {
                relay_cfg_.recv_discard_percent_ = rtc_relay_node["recv_discard_percent"].as<uint32_t>();
            }
        }

        // RTMP server configuration
        auto rtmp_node = config["rtmp_server"];
        if (rtmp_node) {
            if (rtmp_node["enable"]) {
                rtmp_cfg_.enable_ = rtmp_node["enable"].as<bool>();
            }
            if (rtmp_node["listen_ip"]) {
                rtmp_cfg_.listen_ip_ = rtmp_node["listen_ip"].as<std::string>();
            }
            if (rtmp_node["port"]) {
                rtmp_cfg_.port_ = rtmp_node["port"].as<uint16_t>();
            }
        }

        // HTTP-FLV server configuration
        auto httpflv_node = config["httpflv_server"];
        if (httpflv_node) {
            if (httpflv_node["enable"]) {
                httpflv_cfg_.enable_ = httpflv_node["enable"].as<bool>();
            }
            if (httpflv_node["listen_ip"]) {
                httpflv_cfg_.listen_ip_ = httpflv_node["listen_ip"].as<std::string>();
            }
            if (httpflv_node["port"]) {
                httpflv_cfg_.port_ = httpflv_node["port"].as<uint16_t>();
            }
        }

        // WebSocket stream server configuration
        auto ws_stream_node = config["ws_stream_server"];
        if (ws_stream_node) {
            if (ws_stream_node["enable"]) {
                ws_stream_cfg_.enable_ = ws_stream_node["enable"].as<bool>();
            }
            if (ws_stream_node["listen_ip"]) {
                ws_stream_cfg_.listen_ip_ = ws_stream_node["listen_ip"].as<std::string>();
            }
            if (ws_stream_node["port"]) {
                ws_stream_cfg_.port_ = ws_stream_node["port"].as<uint16_t>();
            }
        }

		ret = 0;
    } catch(const std::exception& e) {
        std::cerr << e.what() << '\n';
    }

    return ret;
}

std::string Config::Dump() {
    std::string dump_str;
    dump_str += "log_path: " + log_path_ + "\n";
    dump_str += "log_level: " + log_level_ + "\n";
    dump_str += "log_console: " + std::string(log_console_ ? "true" : "false") + "\n";
    
    dump_str += "cert_path: " + cert_path_ + "\n";
    dump_str += "key_path: " + key_path_ + "\n";
    dump_str += "candidates:\n";
    for (const auto& candidate : rtc_candidates_) {
        dump_str += "  - nettype: ";
        dump_str += (candidate.net_type_ == RTC_NET_TCP) ? "tcp" : 
             (candidate.net_type_ == RTC_NET_UDP) ? "udp" : "unknown";
        dump_str += "\n";
        dump_str += "    candidate ip: " + candidate.candidate_ip_ + "\n";
        dump_str += "    listen ip: " + candidate.listen_ip_ + "\n";
        dump_str += "    port: " + std::to_string(candidate.port_) + "\n";
    }
    dump_str += "downlink_discard_percent: " + std::to_string(downlink_discard_percent_) + "\n";
    dump_str += "uplink_discard_percent: " + std::to_string(uplink_discard_percent_) + "\n";

    if (pilot_center_cfg_.host_.empty() || pilot_center_cfg_.port_ == 0 || pilot_center_cfg_.subpath_.empty()) {
        dump_str += "pilot_center: null\n";
        return dump_str;
    } else {
        dump_str += "pilot_center:\n";
		dump_str += "  enable: " + std::string(pilot_center_cfg_.enable_ ? "true" : "false") + "\n";
        dump_str += "  host: " + pilot_center_cfg_.host_ + "\n";
        dump_str += "  port: " + std::to_string(pilot_center_cfg_.port_) + "\n";
        dump_str += "  subpath: " + pilot_center_cfg_.subpath_ + "\n";
    }

    if (relay_cfg_.relay_server_ip_.empty() || 
        relay_cfg_.relay_udp_start_ == 0 || 
        relay_cfg_.relay_udp_end_ == 0) {
        dump_str += "rtc_relay: null\n";
    } else {
        dump_str += "rtc_relay:\n";
        dump_str += "  relay_server_ip: " + relay_cfg_.relay_server_ip_ + "\n";
        dump_str += "  relay_udp_start: " + std::to_string(relay_cfg_.relay_udp_start_) + "\n";
        dump_str += "  relay_udp_end: " + std::to_string(relay_cfg_.relay_udp_end_) + "\n";
        dump_str += "  send_discard_percent: " + std::to_string(relay_cfg_.send_discard_percent_) + "\n";
        dump_str += "  recv_discard_percent: " + std::to_string(relay_cfg_.recv_discard_percent_) + "\n";
    }

    // RTMP server configuration
    dump_str += "rtmp_server:\n";
    dump_str += "  enable: " + std::string(rtmp_cfg_.enable_ ? "true" : "false") + "\n";
    dump_str += "  listen_ip: " + rtmp_cfg_.listen_ip_ + "\n";
    dump_str += "  port: " + std::to_string(rtmp_cfg_.port_) + "\n";

    // HTTP-FLV server configuration
    dump_str += "httpflv_server:\n";
    dump_str += "  enable: " + std::string(httpflv_cfg_.enable_ ? "true" : "false") + "\n";
    dump_str += "  listen_ip: " + httpflv_cfg_.listen_ip_ + "\n";
    dump_str += "  port: " + std::to_string(httpflv_cfg_.port_) + "\n";

    // WebSocket stream server configuration
    dump_str += "ws_stream_server:\n";
    dump_str += "  enable: " + std::string(ws_stream_cfg_.enable_ ? "true" : "false") + "\n";
    dump_str += "  listen_ip: " + ws_stream_cfg_.listen_ip_ + "\n";
    dump_str += "  port: " + std::to_string(ws_stream_cfg_.port_) + "\n";

    return dump_str;
}