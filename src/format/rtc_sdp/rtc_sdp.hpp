#ifndef RTC_SDP_HPP
#define RTC_SDP_HPP
#include "rtc_sdp_pub.hpp"
#include "rtc_sdp_media_section.hpp"
#include "rtc_sdp_filter.hpp"
#include <string>
#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <map>
#include <memory>

namespace cpp_streamer
{

class IceCandidate
{
public:
    IceCandidate() = default;
    ~IceCandidate() = default;
public:
    std::string ip_;
    uint16_t port_ = 0;
    RtcNetType net_type_;
    uint64_t foundation_ = 0;
    uint64_t priority_ = 0;
};

class RtcSdp
{
public:
    RtcSdpType type_ = RTC_SDP_UNKNOWN;
    std::string origin_;
    std::string ice_ufrag_;
    std::string ice_pwd_;
    std::string finger_print_;
    std::string msid_;
    RtcSetupType setup_ = RTC_SETUP_UNKNOWN;
    DirectionType direction_ = DIRECTION_UNKNOWN;
    std::vector<IceCandidate> ice_candidates_;
    std::map<int, std::shared_ptr<RtcSdpMediaSection>> media_sections_;

public:
    std::vector<std::string> lines_;

public:
    static std::shared_ptr<RtcSdp> ParseSdp(const std::string& sdp_type, const std::string& sdp_str);

public:
    RtcSdp() = default;
    ~RtcSdp() = default;

public:
    std::string DumpSdp();
    std::shared_ptr<RtcSdp> GenAnswerSdp(
        SdpFilter& sdp_filter, 
        RtcSetupType setup_type,
        DirectionType direct_type,
        const std::string& ice_ufrag,
        const std::string& ice_pwd,
        const std::string& finger_print);
    std::string GenSdpString(bool ice_info_session_level = false);
    std::string GenAudioSdpString(std::shared_ptr<RtcSdpMediaSection> audio_section_ptr, bool ice_info_session_level = false);
    std::string GenVideoSdpString(std::shared_ptr<RtcSdpMediaSection> video_section_ptr, bool ice_info_session_level = false);

private:
    static void ParseFmtpLine(std::shared_ptr<RtcSdpMediaSection> current_media_section, 
        std::shared_ptr<RtcSdpMediaCodec> codec_ptr, int payload_type);
};
}
#endif // RTC_SDP_HPP