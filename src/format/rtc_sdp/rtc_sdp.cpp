#include "rtc_sdp.hpp"
#include "utils/stringex.hpp"
#include "utils/json.hpp"
#include "utils/uuid.hpp"
#include "utils/av/av.hpp"

using json = nlohmann::json;

namespace cpp_streamer
{

std::shared_ptr<RtcSdp> RtcSdp::ParseSdp(const std::string& sdp_type, const std::string& sdp_str)
{
    std::vector<std::string> lines;
    std::shared_ptr<RtcSdp> ret_sdp = std::make_shared<RtcSdp>();
    std::shared_ptr<RtcSdpMediaSection> current_media_section = nullptr;

    int ret = StringSplit(sdp_str, "\r\n", lines);
    if (ret <= 0 || lines.empty()) {
        throw std::invalid_argument("SDP string is empty or invalid");
    }

    if (sdp_type == "offer") {
        ret_sdp->type_ = RTC_SDP_OFFER;
        // Implementation of SDP parsing logic goes here
    } else if (sdp_type == "answer") {
        ret_sdp->type_ = RTC_SDP_ANSWER;
        // Implementation of SDP parsing logic goes here
    } else {
        throw std::invalid_argument("Unknown SDP type: " + sdp_type);
    }

    try
    {
        // Parse the SDP lines and populate ret_sdp fields
        ret_sdp->lines_ = lines;

        for (const auto& line : lines) {
            // Example parsing logic (to be expanded as needed)
            if (line.find("v=") == 0) {
                continue;
            }
            if (line.find("o=") == 0) {
                ret_sdp->origin_ = line.substr(2);
                continue;
            }
            if (line.find("s=") == 0) {
                // Session name line
                continue;
            }
            if (line.find("t=") == 0) {
                // Timing line
                continue;
            }
            if (line.find("c=") == 0) {
                // Connection line
                continue;
            }
            // a=msid-semantic: WMS fa22dd6c-0593-4715-b600-a888210d390d
            if (line.find("a=msid-semantic:") == 0) {
                std::string msid_semantic = line.substr(17);
                auto pos = msid_semantic.find("WMS ");
                if (pos != std::string::npos) {
                    ret_sdp->msid_ = msid_semantic.substr(pos + 4);
                }
                continue;
            }
            if (line.find("a=ice-ufrag:") == 0) {
                ret_sdp->ice_ufrag_ = line.substr(12);
                continue;
            }
            if (line.find("a=ice-pwd:") == 0) {
                ret_sdp->ice_pwd_ = line.substr(10);
                continue;
            }
            if (line.find("a=fingerprint:") == 0) {
                ret_sdp->finger_print_ = line.substr(14);
                continue;
            }
            if (line.find("a=candidate:") == 0) {
                throw std::invalid_argument("ICE candidate parsing not implemented");
            }
            if (line.find("a=setup:") == 0) {
                std::string setup_str = line.substr(8);
                if (setup_str == "active") {
                    ret_sdp->setup_ = RTC_SETUP_ACTIVE;
                } else if (setup_str == "passive") {
                    ret_sdp->setup_ = RTC_SETUP_PASSIVE;
                } else if (setup_str == "actpass") {
                    ret_sdp->setup_ = RTC_SETUP_ACTPASS;
                } else {
                    ret_sdp->setup_ = RTC_SETUP_UNKNOWN;
                    ret_sdp = nullptr;
                    throw std::invalid_argument("Unknown setup type: " + setup_str);
                }
                continue;
            }
            if (line.find("a=sendonly") == 0) {
                ret_sdp->direction_ = DIRECTION_SENDONLY;
                if (current_media_section) {
                    current_media_section->direction_ = DIRECTION_SENDONLY;
                }
                continue;
            } else if (line.find("a=recvonly") == 0) {
                ret_sdp->direction_ = DIRECTION_RECVONLY;
                if (current_media_section) {
                    current_media_section->direction_ = DIRECTION_RECVONLY;
                }
                continue;
            } else if (line.find("a=sendrecv") == 0) {
                ret_sdp->direction_ = DIRECTION_SENDRECV;
                if (current_media_section) {
                    current_media_section->direction_ = DIRECTION_SENDRECV;
                }
                continue;
            }
            if (line.find("m=") == 0) {
                // Media description line
                current_media_section = std::make_shared<RtcSdpMediaSection>();

                current_media_section->direction_ = ret_sdp->direction_;
                std::string media_desc = line.substr(2);

                auto pos = media_desc.find("video");;
                if (pos != std::string::npos) {
                    current_media_section->media_type_ = MEDIA_VIDEO_TYPE;
                } else {
                    pos = media_desc.find("audio");
                    if (pos != std::string::npos) {
                        current_media_section->media_type_ = MEDIA_AUDIO_TYPE;
                    } else {
                        current_media_section->media_type_ = MEDIA_UNKNOWN_TYPE;
                    }
                }
                continue;
            }
            if (line.find("a=mid:") == 0 && current_media_section) {
                std::string mid_str = line.substr(6);
                current_media_section->mid_ = std::stoi(mid_str);
                ret_sdp->media_sections_[current_media_section->mid_] = current_media_section;
                continue;
            }
            if (line.find("a=rtpmap:") == 0) {
                std::string rtpmap_str = line.substr(9);
                std::vector<std::string> rtpmap_parts;

                int ret = StringSplit(rtpmap_str, " ", rtpmap_parts);
                if (ret < 2) {
                    throw std::invalid_argument("Invalid rtpmap line: " + line);
                }
                int payload_type = std::stoi(rtpmap_parts[0]);
                std::string codec_info = rtpmap_parts[1];
				std::vector<std::string> codec_parts;

                ret = StringSplit(codec_info, "/", codec_parts);
                if (ret < 2) {
                    throw std::invalid_argument("Invalid codec info in rtpmap line: " + line);
                }
                std::string codec_name = codec_parts[0];
                int clock_rate = std::stoi(codec_parts[1]);
                int channels = -1;
                if (codec_parts.size() == 3) {
                    channels = std::stoi(codec_parts[2]);
                }
                bool is_rtx = false;
                if (codec_name == "rtx") {
                    is_rtx = true;
                }
                std::shared_ptr<RtcSdpMediaCodec> codec_ptr = std::make_shared<RtcSdpMediaCodec>();
                codec_ptr->codec_name_ = codec_name;
                codec_ptr->is_rtx_ = is_rtx;
                codec_ptr->payload_type_ = payload_type;
                codec_ptr->rate_ = clock_rate;
                codec_ptr->channel_ = channels;
                current_media_section->media_codecs_[payload_type] = codec_ptr;
                continue;
            }
            
            if (line.find("a=rtcp-fb:") == 0) {
                std::string rtcp_fb_str = line.substr(10);
                std::vector<std::string> rtcp_fb_parts;

                int ret = StringSplit(rtcp_fb_str, " ", rtcp_fb_parts);
                if (ret < 2) {
                    throw std::invalid_argument("Invalid rtcp-fb line: " + line);
                }
                int payload_type = std::stoi(rtcp_fb_parts[0]);

                auto codec_iter = current_media_section->media_codecs_.find(payload_type);
                std::string feature_str;
                if (codec_iter != current_media_section->media_codecs_.end()) {
                    for (size_t i = 1; i < rtcp_fb_parts.size(); ++i) {
                        feature_str += rtcp_fb_parts[i];
                        if (i + 1 < rtcp_fb_parts.size()) {
                            feature_str += " ";
                        }
                    }
                    codec_iter->second->rtcp_features_.push_back(feature_str);
                }
                continue;
            }
            
            if (line.find("a=fmtp:") == 0) {
                std::string fmtp_str = line.substr(7);
                std::vector<std::string> fmtp_parts;

                int ret = StringSplit(fmtp_str, " ", fmtp_parts);
                if (ret < 2) {
                    throw std::invalid_argument("Invalid fmtp line: " + line);
                }
                int payload_type = std::stoi(fmtp_parts[0]);
                std::string fmtp_param = fmtp_parts[1];

                auto codec_iter = current_media_section->media_codecs_.find(payload_type);
                if (codec_iter != current_media_section->media_codecs_.end()) {
                    codec_iter->second->fmtp_param_ = fmtp_param;
                    if (codec_iter->second->is_rtx_) {
                        // RTX specific fmtp parsing can be added here
                        StringSplit(fmtp_param, ";", fmtp_parts);

                        for (const auto& param : fmtp_parts) {
                            std::vector<std::string> key_value;
                            int ret = StringSplit(param, "=", key_value);
                            if (ret == 2 && key_value[0] == "apt") {
                                int apt_payload_type = std::stoi(key_value[1]);
                                
                                auto apt_codec_iter = current_media_section->media_codecs_.find(apt_payload_type);
                                if (apt_codec_iter != current_media_section->media_codecs_.end()) {
                                    apt_codec_iter->second->rtx_payload_type_ = payload_type;
                                }
                            }
                        }
                    } else {
                        // Non-RTX specific fmtp parsing can be added here
                        // For example, parsing H264 fmtp parameters
                        if (codec_iter->second->codec_name_ == "H264") {
                            // Parse fmtp_param to fill h264_param fields
                            codec_iter->second->GenH264FmtpParam();
                        }
                        if (codec_iter->second->codec_name_ == "AV1") {
                            // Similar parsing for AV1CodecFmtpParam
                            codec_iter->second->GenAV1FmtpParam();
                        }
                        if (codec_iter->second->codec_name_ == "VP9") {
                            // Similar parsing for VP9CodecFmtpParam
                            codec_iter->second->GenVP9FmtpParam();
                        }
                        if (codec_iter->second->codec_name_ == "opus") {
                            // Similar parsing for OpusCodecFmtpParam
                            codec_iter->second->GenOpusFmtpParam();
                        }
                    }
                }
                continue;
            }
        
            if (line.find("a=ssrc:") == 0) {
                /*
                a=ssrc:2373035617 cname:kwFnK0uz8U/3lSqO
                a=ssrc:2373035617 msid:47715022-b1c9-46bf-b91e-1eefc8aa7c36 09faafc2-cf87-439a-8e58-11c09bcad7ad
                a=ssrc:1714831299 cname:kwFnK0uz8U/3lSqO
                a=ssrc:1714831299 msid:47715022-b1c9-46bf-b91e-1eefc8aa7c36 09faafc2-cf87-439a-8e58-11c09bcad7ad
                */
                std::string ssrc_str = line.substr(7);
                std::vector<std::string> ssrc_parts;

                int ret = StringSplit(ssrc_str, " ", ssrc_parts);
                if (ret < 2) {
                    throw std::invalid_argument("Invalid ssrc line: " + line);
                }
                uint32_t ssrc = std::stoul(ssrc_parts[0]);
                std::string attr_str = ssrc_parts[1];

                std::shared_ptr<SsrcInfo> ssrc_info_ptr;
                auto ssrc_iter = current_media_section->ssrc_infos_.find(ssrc);
                if (ssrc_iter == current_media_section->ssrc_infos_.end()) {
                    ssrc_info_ptr = std::make_shared<SsrcInfo>();
                    ssrc_info_ptr->ssrc_ = ssrc;
                    current_media_section->ssrc_infos_[ssrc] = ssrc_info_ptr;
                } else {
                    ssrc_info_ptr = ssrc_iter->second;
                }
                std::vector<std::string> attr_parts;
                ret = StringSplit(attr_str, ":", attr_parts);
                if (ret != 2) {
                    throw std::invalid_argument("Invalid ssrc attribute in line: " + line);
                }
                if (attr_parts[0] == "cname") {
                    ssrc_info_ptr->cname_ = attr_parts[1];
                } else if (attr_parts[0] == "msid") {
                    if (ssrc_parts.size() >= 2) {
                        ssrc_info_ptr->stream_id_ = attr_parts[1];
                    }
                    if (ssrc_parts.size() >= 3) {
                        ssrc_info_ptr->track_id_ = ssrc_parts[2];
                    }
                }

                continue;
            }
        
            if (line.find("a=ssrc-group:") == 0) {
                // SSRC group line
                // a=ssrc-group:FID 3188473065 1684074237
                std::string ssrc_group_str = line.substr(13);
                std::vector<std::string> ssrc_group_parts;
                int ret = StringSplit(ssrc_group_str, " ", ssrc_group_parts);
                if (ret < 2) {
                    throw std::invalid_argument("Invalid ssrc-group line: " + line);
                }
                std::string semantics = ssrc_group_parts[0];

                for (size_t i = 1; i < ssrc_group_parts.size(); ++i) {
                    uint32_t ssrc = std::stoul(ssrc_group_parts[i]);
                    
                    if (i == 1 && semantics == "FID") {
                        auto ssrc_iter = current_media_section->ssrc_infos_.find(ssrc);
                        if (ssrc_iter != current_media_section->ssrc_infos_.end()) {
                            ssrc_iter->second->is_main_ = true;
                        } else {
                            std::shared_ptr<cpp_streamer::SsrcInfo> ssrc_info_ptr = std::make_shared<cpp_streamer::SsrcInfo>();
                            ssrc_info_ptr->ssrc_ = ssrc;
                            ssrc_info_ptr->is_main_ = true;
                            current_media_section->ssrc_infos_[ssrc] = ssrc_info_ptr;
                        }
                    } else if (i > 1 && semantics == "FID") {
                        auto ssrc_iter = current_media_section->ssrc_infos_.find(ssrc);
                        if (ssrc_iter != current_media_section->ssrc_infos_.end()) {
                            ssrc_iter->second->is_main_ = false;
                        } else {
                            std::shared_ptr<cpp_streamer::SsrcInfo> ssrc_info_ptr = std::make_shared<cpp_streamer::SsrcInfo>();
                            ssrc_info_ptr->ssrc_ = ssrc;
                            ssrc_info_ptr->is_main_ = false;
                            current_media_section->ssrc_infos_[ssrc] = ssrc_info_ptr;
                        }
                    }
                }
                continue;
            }
        
            if (line.find("a=extmap:") == 0) {
                // Extension mapping line
                // a=extmap:1 urn:ietf:params:rtp-hdrext:sdes:mid
                std::string extmap_str = line.substr(9);
                std::vector<std::string> extmap_parts;
                int ret = StringSplit(extmap_str, " ", extmap_parts);
                if (ret != 2) {
                    throw std::invalid_argument("Invalid extmap line: " + line);
                }
                int id = std::stoi(extmap_parts[0]);
                std::string uri = extmap_parts[1];

                std::shared_ptr<ExtensionInfo> ext_info_ptr = std::make_shared<ExtensionInfo>();
                ext_info_ptr->id_ = id;
                ext_info_ptr->uri_ = uri;
                current_media_section->extensions_[id] = ext_info_ptr;
                continue;
            }
        }
    }
    catch(const std::exception& e)
    {
        throw e;
    }
    
    return ret_sdp;
}

std::string RtcSdp::DumpSdp() {
    json ret_json = json::object();

    ret_json["type"] = (type_ == RTC_SDP_OFFER) ? "offer" : 
                      (type_ == RTC_SDP_ANSWER) ? "answer" : "unknown";
    ret_json["origin"] = origin_;
    ret_json["ice_ufrag"] = ice_ufrag_;
    ret_json["ice_pwd"] = ice_pwd_;
    ret_json["finger_print"] = finger_print_;
    ret_json["setup"] = (setup_ == RTC_SETUP_ACTIVE) ? "active" :
                       (setup_ == RTC_SETUP_PASSIVE) ? "passive" :
                       (setup_ == RTC_SETUP_ACTPASS) ? "actpass" : "unknown";
    ret_json["direction"] = (direction_ == DIRECTION_SENDONLY) ? "sendonly" :
                            (direction_ == DIRECTION_RECVONLY) ? "recvonly" :
                            (direction_ == DIRECTION_SENDRECV) ? "sendrecv" : "unknown";
    
    if (!ice_candidates_.empty()) {
        ret_json["ice_candidates"] = json::array();
        for (const auto& candidate : ice_candidates_) {
            json candidate_json = json::object();
            candidate_json["ip"] = candidate.ip_;
            candidate_json["port"] = candidate.port_;
            candidate_json["net_type"] = (candidate.net_type_ == RTC_NET_TCP) ? "tcp" :
                                         (candidate.net_type_ == RTC_NET_UDP) ? "udp" : "unknown";
            candidate_json["foundation"] = candidate.foundation_;
            candidate_json["priority"] = candidate.priority_;
            ret_json["ice_candidates"].push_back(candidate_json);
        }
    }
    if (!media_sections_.empty()) {
        auto media_array = json::array();
        for (const auto& media_pair : media_sections_) {
            media_array.push_back(media_pair.second->Dump());
        }
        ret_json["media_sections"] = media_array;
    }
    return ret_json.dump();
}

std::shared_ptr<RtcSdp> RtcSdp::GenAnswerSdp(
    SdpFilter& sdp_filter,
    RtcSetupType setup_type, 
    DirectionType direct_type,
    const std::string& ice_ufrag,
    const std::string& ice_pwd,
    const std::string& finger_print) {
    std::shared_ptr<RtcSdp> answer_sdp = std::make_shared<RtcSdp>();

    answer_sdp->type_ = RTC_SDP_ANSWER;
    answer_sdp->origin_ = origin_;
    answer_sdp->ice_ufrag_ = ice_ufrag;
    answer_sdp->ice_pwd_ = ice_pwd;
    answer_sdp->finger_print_ = finger_print;

    answer_sdp->setup_ = setup_type;
    answer_sdp->direction_ = direct_type;
    if (msid_.empty()) {
        msid_ = cpp_streamer::UUID::MakeUUID2();
    }
    answer_sdp->msid_ = msid_;

    for (auto media_section : media_sections_) {
        int mid = media_section.first;
        auto offer_media = media_section.second;
        
        for (auto offer_codec :offer_media->media_codecs_) {
            bool ret = sdp_filter.IsCodecFilter(offer_media->media_type_, offer_codec.second);
            if (ret) {
                std::shared_ptr<RtcSdpMediaSection> answer_media;
                auto answer_media_iter = answer_sdp->media_sections_.find(mid);
                if (answer_media_iter == answer_sdp->media_sections_.end()) {
                    answer_media = std::make_shared<RtcSdpMediaSection>();
                    answer_media->media_type_ = offer_media->media_type_;
                    answer_media->mid_ = offer_media->mid_;
                    answer_media->direction_ = direct_type;
                    answer_sdp->media_sections_[mid] = answer_media;
                } else {
                    answer_media = answer_media_iter->second;
                }
                answer_media->media_codecs_[offer_codec.second->payload_type_] = offer_codec.second;
                if (answer_media->cname_.empty()) {
                    // Use the first codec's cname for the media section
                    for (const auto& ssrc_pair : offer_media->ssrc_infos_) {
                        answer_media->cname_ = ssrc_pair.second->cname_;
                        break;
                    }
                }
                if (offer_codec.second->rtx_payload_type_ > 0) {
                    auto rtx_codec_iter = offer_media->media_codecs_.find(offer_codec.second->rtx_payload_type_);
                    if (rtx_codec_iter != offer_media->media_codecs_.end()) {
                        answer_media->media_codecs_[rtx_codec_iter->second->payload_type_] = rtx_codec_iter->second;
                    }
                }
            }
        }
        for (auto offer_ext : offer_media->extensions_) {
            bool ret = sdp_filter.IsExtMapFilter(offer_ext.second->uri_);
            if (ret) {
                std::shared_ptr<RtcSdpMediaSection> answer_media;
                auto answer_media_iter = answer_sdp->media_sections_.find(mid);
                if (answer_media_iter == answer_sdp->media_sections_.end()) {
                    answer_media = std::make_shared<RtcSdpMediaSection>();
                    answer_media->media_type_ = offer_media->media_type_;
                    answer_media->mid_ = offer_media->mid_;
                    answer_media->direction_ = direct_type;
                    answer_sdp->media_sections_[mid] = answer_media;
                } else {
                    answer_media = answer_media_iter->second;
                }
                answer_media->extensions_[offer_ext.second->id_] = offer_ext.second;
            }
        }
    }
    for (auto media_section : answer_sdp->media_sections_) {
        int mid = media_section.first;
        auto answer_media = media_section.second;

        auto offer_media_iter = media_sections_.find(mid);
        if (offer_media_iter == media_sections_.end()) {
            throw std::invalid_argument("Media section not found in offer SDP for mid: " + std::to_string(mid));
        }
        auto offer_media = offer_media_iter->second;

        for (auto ssrc_info : offer_media->ssrc_infos_) {
            answer_media->ssrc_infos_[ssrc_info.first] = ssrc_info.second;
        }
    }

    return answer_sdp;
}

std::string RtcSdp::GenAudioSdpString(std::shared_ptr<RtcSdpMediaSection> audio_section_ptr, bool ice_info_session_level) {
    std::string sdp_str;

    sdp_str += "m=audio 9 UDP/TLS/RTP/SAVPF";
    for (const auto& codec_pair : audio_section_ptr->media_codecs_) {
        sdp_str += " " + std::to_string(codec_pair.first);
    }
    sdp_str += "\r\n";
    sdp_str += "c=IN IP4 0.0.0.0\r\n";

    for (const auto& codec_pair : audio_section_ptr->media_codecs_) {
        auto codec = codec_pair.second;
        sdp_str += "a=rtpmap:" + std::to_string(codec->payload_type_) + " " +
                   codec->codec_name_ + "/" + std::to_string(codec->rate_);
        if (codec->channel_ > 0) {
            sdp_str += "/" + std::to_string(codec->channel_);
        }
        sdp_str += "\r\n";

        if (codec->opus_fmtp_param_) {
            //a=fmtp:111 minptime=10;maxaveragebitrate=96000;stereo=1;sprop-stereo=1;useinbandfec=1
            sdp_str += "a=fmtp:" + std::to_string(codec->payload_type_) + " " +
                       "minptime=" + std::to_string(codec->opus_fmtp_param_->minptime_) +
                       ";maxaveragebitrate=" + std::to_string(codec->opus_fmtp_param_->maxaveragebitrate_) +
                       ";stereo=" + std::to_string(codec->opus_fmtp_param_->stereo_) +
                       ";sprop-stereo=" + std::to_string(codec->opus_fmtp_param_->sprop_stereo_) +
                       ";useinbandfec=" + std::to_string(codec->opus_fmtp_param_->useinbandfec_) + "\r\n";
        }
    }
    // sdp_str += "a=rtcp:9 IN IP4 0.0.0.0\r\n";

    for (const auto& ext_pair : audio_section_ptr->extensions_) {
        sdp_str += "a=extmap:" + std::to_string(ext_pair.second->id_) + " " + ext_pair.second->uri_ + "\r\n";
    }
    if (!ice_info_session_level) {
        sdp_str += "a=setup:";
        sdp_str += (setup_ == RTC_SETUP_ACTIVE) ? "active" :
                   (setup_ == RTC_SETUP_PASSIVE) ? "passive" :
                   (setup_ == RTC_SETUP_ACTPASS) ? "actpass" : "unknown";
        sdp_str += "\r\n";
    }
    
    sdp_str += "a=mid:" + std::to_string(audio_section_ptr->mid_) + "\r\n";
    sdp_str += "a=";
    sdp_str += (direction_ == DIRECTION_SENDONLY) ? "sendonly" :
               (direction_ == DIRECTION_RECVONLY) ? "recvonly" :
               (direction_ == DIRECTION_SENDRECV) ? "sendrecv" : "unknown";
    sdp_str += "\r\n";

    if (!ice_info_session_level) {
        sdp_str += "a=ice-lite\r\n";
        sdp_str += "a=ice-ufrag:" + ice_ufrag_ + "\r\n";
        sdp_str += "a=ice-pwd:" + ice_pwd_ + "\r\n";
        sdp_str += "a=fingerprint:" + finger_print_ + "\r\n";

        for (const auto& candidate : ice_candidates_) {
            /* a=candidate:0 1 udp 2130706431 192.168.1.100 8000 typ host*/
            sdp_str += "a=candidate:";
            sdp_str += std::to_string(candidate.foundation_);
            sdp_str += " 1 ";
            sdp_str += ((candidate.net_type_ == RTC_NET_TCP) ? "TCP" :
                        (candidate.net_type_ == RTC_NET_UDP) ? "UDP" : "UNKNOWN");
            sdp_str += " ";
            sdp_str += std::to_string(candidate.priority_);
            sdp_str += " ";
            sdp_str += candidate.ip_;
            sdp_str += " ";
            sdp_str += std::to_string(candidate.port_);
            sdp_str += " typ host\r\n";
        }
    }
    std::string cname;
    if (audio_section_ptr->cname_.empty()) {
        for (const auto& ssrc_pair : audio_section_ptr->ssrc_infos_) {
            cname = ssrc_pair.second->cname_;
            break;
        }
    } else {
        cname = audio_section_ptr->cname_;
    }
    if (cname.empty()) {
        cname = "a_" + cpp_streamer::UUID::MakeNumString(10);
    }
    for (const auto& ssrc_pair : audio_section_ptr->ssrc_infos_) {
        auto ssrc_info = ssrc_pair.second;

        if (ssrc_info->track_id_.empty()) {
            ssrc_info->track_id_ = ssrc_info->stream_id_ + "-audio";
        }
        sdp_str += "a=ssrc:" + std::to_string(ssrc_info->ssrc_) + " cname:" + cname + "\r\n";
        sdp_str += "a=ssrc:" + std::to_string(ssrc_info->ssrc_) + " msid:" 
            + ssrc_info->stream_id_ + " " + ssrc_info->track_id_ + "\r\n";
    }
    sdp_str += "a=rtcp-mux\r\n";
    sdp_str += "a=rtcp-rsize\r\n";
    
    return sdp_str;
}

std::string RtcSdp::GenVideoSdpString(std::shared_ptr<RtcSdpMediaSection> video_section_ptr, bool ice_info_session_level) {
    std::string sdp_str;
    int main_payload = 0;
    int rtx_payload = 0;

    sdp_str += "m=video 9 UDP/TLS/RTP/SAVPF";

    for (const auto& codec_pair : video_section_ptr->media_codecs_) {
        sdp_str += " " + std::to_string(codec_pair.first);
        if (codec_pair.second->codec_name_ == "H264") {
            main_payload = codec_pair.first;
        } else if (codec_pair.second->codec_name_ == "rtx") {
            rtx_payload = codec_pair.first;
        }
    }
    sdp_str += "\r\n";
    sdp_str += "c=IN IP4 0.0.0.0\r\n";

    for (const auto& codec_pair : video_section_ptr->media_codecs_) {
        auto codec = codec_pair.second;
        sdp_str += "a=rtpmap:" + std::to_string(codec->payload_type_) + " " +
                   codec->codec_name_ + "/" + std::to_string(codec->rate_) + "\r\n";
    }
    for (const auto& codec_pair : video_section_ptr->media_codecs_) {
        auto codec = codec_pair.second;
        
        size_t pos = codec->fmtp_param_.find("apt=");
        if (pos == std::string::npos) {
            sdp_str += "a=fmtp:" + std::to_string(codec->payload_type_) + " ";
            sdp_str += codec->fmtp_param_ + "\r\n";
        }
    }
    if (rtx_payload > 0 && main_payload > 0) {
        sdp_str += "a=fmtp:" + std::to_string(rtx_payload) + " apt=" + std::to_string(main_payload) + "\r\n";
    }
    
    for (const auto& codec_pair : video_section_ptr->media_codecs_) {
        auto codec = codec_pair.second;
        for (const auto& rtcp_fb : codec->rtcp_features_) {
            sdp_str += "a=rtcp-fb:" + std::to_string(codec->payload_type_) + " " + rtcp_fb + "\r\n";
        }
    }
    // sdp_str += "a=rtcp:9 IN IP4 0.0.0.0\r\n";

    for (const auto& ext_pair : video_section_ptr->extensions_) {
        sdp_str += "a=extmap:" + std::to_string(ext_pair.second->id_) + " " + ext_pair.second->uri_ + "\r\n";
    }
    if (!ice_info_session_level) {
        sdp_str += "a=setup:";
        sdp_str += (setup_ == RTC_SETUP_ACTIVE) ? "active" :
                   (setup_ == RTC_SETUP_PASSIVE) ? "passive" :
                   (setup_ == RTC_SETUP_ACTPASS) ? "actpass" : "unknown";
        sdp_str += "\r\n";
    }
    sdp_str += "a=mid:" + std::to_string(video_section_ptr->mid_) + "\r\n";
    sdp_str += "a=";
    sdp_str += (direction_ == DIRECTION_SENDONLY) ? "sendonly" :
               (direction_ == DIRECTION_RECVONLY) ? "recvonly" :
               (direction_ == DIRECTION_SENDRECV) ? "sendrecv" : "unknown";
    sdp_str += "\r\n";

    if (!ice_info_session_level) {
        sdp_str += "a=ice-lite\r\n";
        sdp_str += "a=ice-ufrag:" + ice_ufrag_ + "\r\n";
        sdp_str += "a=ice-pwd:" + ice_pwd_ + "\r\n";
        sdp_str += "a=fingerprint:" + finger_print_ + "\r\n";

        for (const auto& candidate : ice_candidates_) {
            sdp_str += "a=candidate:" + std::to_string(candidate.foundation_) + " 1 " +
                       ((candidate.net_type_ == RTC_NET_TCP) ? "TCP" :
                        (candidate.net_type_ == RTC_NET_UDP) ? "UDP" : "UNKNOWN") + " " +
                       std::to_string(candidate.priority_) + " " +
                       candidate.ip_ + " " +
                       std::to_string(candidate.port_) + " typ host\r\n";
        }
    }

    uint32_t main_ssrc = 0;
    uint32_t backup_ssrc = 0;
    for (const auto& ssrc_pair : video_section_ptr->ssrc_infos_) {
        auto ssrc_info = ssrc_pair.second;
        if (ssrc_info->is_main_) {
            main_ssrc = ssrc_info->ssrc_;
        } else {
            backup_ssrc = ssrc_info->ssrc_;
        }
    }
    if (backup_ssrc > 0) {
        sdp_str += "a=ssrc-group:FID " + std::to_string(main_ssrc) + " " + std::to_string(backup_ssrc) + "\r\n";
    }
    
    std::string cname;

    if (video_section_ptr->cname_.empty()) {
        for (const auto& ssrc_pair : video_section_ptr->ssrc_infos_) {
            if (ssrc_pair.second->is_main_ == false) {
                continue;
            }
            cname = ssrc_pair.second->cname_;
            break;
        }
    } else {
        cname = video_section_ptr->cname_;
    }
    if (cname.empty()) {
        cname = "v_" + std::to_string(main_ssrc);
    }
    //set main first
    for (const auto& ssrc_pair : video_section_ptr->ssrc_infos_) {
        auto ssrc_info = ssrc_pair.second;

        if (ssrc_info->is_main_ == false) {
            continue;
        }
        if (ssrc_info->track_id_.empty()) {
            ssrc_info->track_id_ = ssrc_info->stream_id_ + "-video";
        }
        sdp_str += "a=ssrc:" + std::to_string(ssrc_info->ssrc_) + " cname:" + cname + "\r\n";
        sdp_str += "a=ssrc:" + std::to_string(ssrc_info->ssrc_) + " msid:" 
            + ssrc_info->stream_id_ + " " + ssrc_info->track_id_ + "\r\n";
    }

    //then backup
    for (const auto& ssrc_pair : video_section_ptr->ssrc_infos_) {
        auto ssrc_info = ssrc_pair.second;

        if (ssrc_info->is_main_ == true) {
            continue;
        }
        if (ssrc_info->track_id_.empty()) {
            ssrc_info->track_id_ = ssrc_info->stream_id_ + "-video";
        }
        sdp_str += "a=ssrc:" + std::to_string(ssrc_info->ssrc_) + " cname:" + cname + "\r\n";
        sdp_str += "a=ssrc:" + std::to_string(ssrc_info->ssrc_) + " msid:" 
            + ssrc_info->stream_id_ + " " + ssrc_info->track_id_ + "\r\n";
    }
    /*
    for (const auto& ssrc_pair : video_section_ptr->ssrc_infos_) {
        auto ssrc_info = ssrc_pair.second;

        if (ssrc_info->stream_id_.empty()) {
            ssrc_info->stream_id_ = "v_" + std::to_string(ssrc_info->ssrc_);
        }
        sdp_str += "a=ssrc:" + std::to_string(ssrc_info->ssrc_) + " cname:" + cname + "\r\n";
        sdp_str += "a=ssrc:" + std::to_string(ssrc_info->ssrc_) + " msid:" 
            + this->msid_ + " " + ssrc_info->stream_id_ + "\r\n";
    }
    */
    sdp_str += "a=rtcp-mux\r\n";
    sdp_str += "a=rtcp-rsize\r\n";
    return sdp_str;
}

std::string RtcSdp::GenSdpString(bool ice_info_session_level) {
    std::string sdp_str;
    std::string group_str = "a=group:BUNDLE";

    for (const auto& media_pair : media_sections_) {
        group_str += " " + std::to_string(media_pair.second->mid_);
    }
    sdp_str += "v=0\r\n";
    sdp_str += "o=" + origin_ + "\r\n";
    sdp_str += "s=-\r\n";
    sdp_str += "t=0 0\r\n";
	sdp_str += "a=extmap-allow-mixed\r\n";
	sdp_str += "a=msid-semantic:WMS " + msid_ + "\r\n";
	sdp_str += group_str + "\r\n";
    
    sdp_str += "a=ice-options:ice2,trickle\r\n";
    
    if (ice_info_session_level) {
        sdp_str += "a=ice-lite\r\n";
        sdp_str += "a=ice-ufrag:" + ice_ufrag_ + "\r\n";
        sdp_str += "a=ice-pwd:" + ice_pwd_ + "\r\n";
        sdp_str += "a=fingerprint:" + finger_print_ + "\r\n";

        for (const auto& candidate : ice_candidates_) {
            sdp_str += "a=candidate:" + std::to_string(candidate.foundation_) + " 1 " +
                       ((candidate.net_type_ == RTC_NET_TCP) ? "TCP" :
                        (candidate.net_type_ == RTC_NET_UDP) ? "UDP" : "UNKNOWN") + " " +
                       std::to_string(candidate.priority_) + " " +
                       candidate.ip_ + " " +
                       std::to_string(candidate.port_) + " typ host\r\n";
        }
        sdp_str += "a=setup:";
        sdp_str += (setup_ == RTC_SETUP_ACTIVE) ? "active" :
                   (setup_ == RTC_SETUP_PASSIVE) ? "passive" :
                   (setup_ == RTC_SETUP_ACTPASS) ? "actpass" : "unknown";
        sdp_str += "\r\n";
    }

    for (const auto& media_pair : media_sections_) {
        auto media_section = media_pair.second;
        if (media_section->media_type_ == MEDIA_AUDIO_TYPE) {
            sdp_str += GenAudioSdpString(media_section, ice_info_session_level);
        } else if (media_section->media_type_ == MEDIA_VIDEO_TYPE) {
            sdp_str += GenVideoSdpString(media_section, ice_info_session_level);
        } else {
            throw std::invalid_argument("Unsupported media type in SDP generation");
        }
    }
    return sdp_str;
} 
}// namespace cpp_streamer