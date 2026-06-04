#ifndef WEBRTC_SESSION_HPP
#define WEBRTC_SESSION_HPP
#include "utils/timer.hpp"
#include "utils/logger.hpp"
#include "ice_server.hpp"
#include "format/rtc_sdp/rtc_sdp_pub.hpp"
#include "net/stun/stun.hpp"
#include "net/udp/udp_server.hpp"
#include "dtls_session.hpp"
#include "udp_transport.hpp"
#include "srtp_session.hpp"
#include "rtc_info.hpp"
#include "media_pusher.hpp"
#include "media_puller.hpp"
#include "rtc_info.hpp"
#include "tcc_server.hpp"

#include <memory>
#include <map>
#include <vector>
#include <uv.h>

namespace cpp_streamer {

class WebRtcSession : public IceOnDataWriteCallbackI, public DtlsWriteCallbackI, public TransportSendCallbackI, public TimerInterface
{
public:
    WebRtcSession(SRtpType type, const std::string& room_id, const std::string& user_id,
        PacketFromRtcPusherCallbackI* packet2room_cb,
        MediaPushPullEventI* media_push_event_cb,
        uv_loop_t* loop, Logger* logger);
    virtual ~WebRtcSession();

public:
    void Close();
    int DtlsInit(Role role, const std::string& remote_fingerprint);
    int HandleStunPacket(StunPacket* stun_pkt, UdpTransportI* trans_cb, UdpTuple addr);
    int HandleNoneStunPacket(const uint8_t* data, size_t len, UdpTransportI* trans_cb, UdpTuple addr);
    int HandleRtpPacket(const uint8_t* data, size_t len, UdpTuple addr);
    int HandleRtcpPacket(const uint8_t* data, size_t len, UdpTuple addr);
    int AddPusherRtpSession(const RtpSessionParam& param, std::string& pusher_id);
    int AddPullerRtpSession(const RtpSessionParam& param, 
        const std::string& pusher_user_id,
        const std::string& pusher_id, 
        std::string& puller_id);
    bool IsAlive();
    std::string GetRoomId() const { return room_id_; }
    std::string GetUserId() const { return user_id_; }

public:
    std::string GetSessionId() { return session_id_;}
    std::string GetIceUfrag() { return ice_ufrag_;}
    std::string GetIcePwd() { return ice_pwd_;}
    std::string GetLocalFingerPrint() { return local_finger_print_;}
    std::string GetRemoteFingerPrint() { return remote_finger_print_;}
    std::vector<std::shared_ptr<MediaPusher>> GetMediaPushers();
    std::vector<std::shared_ptr<MediaPuller>> GetMediaPullers();

public:
    virtual void OnIceWrite(const uint8_t* data, size_t sent_size, UdpTuple address) override;
public:
	virtual void OnDtlsTransportSendData(const uint8_t* data, size_t sent_size, UdpTuple address) override;
	virtual void OnDtlsTransportConnected(
	  const DtlsSession* /*dtlsTransport*/,
	  SRtpSessionCryptoSuite srtpCryptoSuite,
	  uint8_t* srtpLocalKey,
	  size_t srtpLocalKeyLen,
	  uint8_t* srtpRemoteKey,
	  size_t srtpRemoteKeyLen,
	  std::string& remoteCert) override;
public:
    virtual bool IsConnected() override;
    virtual void OnTransportSendRtp(uint8_t* data, size_t sent_size) override;
    virtual void OnTransportSendRtcp(uint8_t* data, size_t sent_size) override;

protected:
    virtual bool OnTimer() override;

private://handle rtcp packet
    int HandleRtcpSrPacket(const uint8_t* data, size_t len);
    int HandleRtcpRrPacket(const uint8_t* data, size_t len);
    int HandleRtcpXrPacket(const uint8_t* data, size_t len);
    int HandleRtcpRtpfbPacket(const uint8_t* data, size_t len);
    int HandleRtcpPsfbPacket(const uint8_t* data, size_t len);

private:
    SRtpType direction_type_ = SRtpType::SRTP_SESSION_TYPE_INVALID;
    std::string room_id_;
    std::string user_id_;
    uv_loop_t* loop_ = nullptr;
    Logger* logger_ = nullptr;
    std::string session_id_;

private:
    bool closed_ = false;
    int64_t alive_ms_ = -1;
    std::string ice_ufrag_;
    std::string ice_pwd_;
    std::string local_finger_print_;
    std::string remote_finger_print_;
    bool dtls_connected_ = false;

private:
    std::unique_ptr<IceServer> ice_server_;
    std::unique_ptr<DtlsSession> dtls_session_;
    std::unique_ptr<SRtpSession> srtp_send_session_;
    std::unique_ptr<SRtpSession> srtp_recv_session_;
    UdpTuple remote_addr_;

private:
    //main ssrc and rtx ssrc mapping for pusher
    std::map<uint32_t, std::shared_ptr<MediaPusher>> ssrc2media_pusher_;
    std::map<int, std::shared_ptr<MediaPusher>> mid2media_pusher_;

private:
    //main ssrc and rtx ssrc mapping for puller
    std::map<uint32_t, std::shared_ptr<MediaPuller>> ssrc2media_puller_;
    std::map<uint32_t, std::shared_ptr<MediaPuller>> rtxssrc2media_puller_;

private:
    UdpTransportI* trans_cb_ = nullptr;
    PacketFromRtcPusherCallbackI* packet2room_cb_ = nullptr;
    MediaPushPullEventI* media_push_event_cb_ = nullptr;

private:
    int mid_ext_id_ = -1;
    int tcc_ext_id_ = -1;

private:
    std::unique_ptr<TccServer> tcc_server_ = nullptr;
};

} // namespace cpp_streamer

#endif