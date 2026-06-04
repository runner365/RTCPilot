#ifndef RTP_PACKET_HPP
#define RTP_PACKET_HPP
#include "rtprtcp_pub.hpp"
#include <stdint.h>
#include <stddef.h>
#include <string>
#ifdef _WIN64
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif
#include <map>

namespace cpp_streamer
{
class Logger;

#define RTP_SEQ_MOD (1<<16)

typedef struct HeaderExtensionS
{
    uint16_t id;
    uint16_t length;
    uint8_t  value[1];
} HeaderExtension;

typedef struct OnebyteExtensionS {
    uint8_t len : 4;
    uint8_t id  : 4;
    uint8_t value[1];
} OnebyteExtension;

typedef struct TwobytesExtensionS {
    uint8_t id  : 8;
    uint8_t len : 8;
    uint8_t value[1];
} TwobytesExtension;

/**
    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |V=2|P|X|  CC   |M|     PT      |       sequence number         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           timestamp                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |           synchronization source (SSRC) identifier            |
   +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
   |            contributing source (CSRC) identifiers             |
   |                             ....                              |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |      defined by profile       |           length              |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                        header extension                       |
   |                             ....                              |
 */

class RtpPacket
{
public:
    RtpPacket(RtpCommonHeader* header, HeaderExtension* ext,
            uint8_t* payload, size_t payload_len,
            uint8_t pad_len, size_t data_len);
    ~RtpPacket();

public:
    uint8_t Version() {return this->header->version;}
    bool HasPadding() {return (this->header->padding == 1) ? true : false;}
    void SetPadding(bool flag) {this->header->padding = flag ? 1 : 0;}
    bool HasExtension() {return (this->header->extension == 1) ? true : false;}
    uint8_t CsrcCount() {return this->header->csrc_count;}
    uint8_t GetPayloadType() {return this->header->payload_type;}
    void SetPayloadType(uint8_t type) {this->header->payload_type = type;}
    uint8_t GetMPayloadType() {
        uint8_t marker = this->header->marker;
        return (marker << 7) | this->header->payload_type;
    }
    uint8_t GetMarker() {return this->header->marker;}
    void SetMarker(uint8_t marker) { this->header->marker = marker; }
    uint16_t GetSeq() {return ntohs(this->header->sequence);}
    void SetSeq(uint16_t seq) {this->header->sequence = htons(seq);}
    uint32_t GetTimestamp() {return ntohl(this->header->timestamp);}
    void SetTimestamp(uint32_t ts) { this->header->timestamp = (uint32_t)htonl(ts); }
    uint32_t GetSsrc() {return ntohl(this->header->ssrc);}
    void SetSsrc(uint32_t ssrc) {this->header->ssrc = (uint32_t)htonl(ssrc);}

    uint8_t* GetData() {return (uint8_t*)this->header;}
    size_t GetDataLength() {return data_len;}

    uint8_t* GetPayload() {return this->payload;}
    size_t GetPayloadLength() {return this->payload_len;}
    void SetPayloadLength(size_t len) { this->payload_len = len; }

    void SetMidExtensionId(uint8_t id) { mid_extension_id_ = id; }
    uint8_t GetMidExtensionId() { return mid_extension_id_; }

    void SetAbsTimeExtensionId(uint8_t id) { abs_time_extension_id_ = id; }
    uint8_t GetAbsTimeExtensionId() { return abs_time_extension_id_; }

    void SetTccExtensionId(uint8_t id) { tcc_extension_id_ = id; }
    uint8_t GetTccExtensionId() { return tcc_extension_id_; }

    bool UpdateMid(uint8_t mid);
    bool UpdateMid(uint8_t new_mid_extern_id, uint8_t mid);
    bool ReadMid(uint8_t& mid);
    bool ClearMidExtension(uint8_t mid_value, uint8_t& old_ext_id);

    bool ReadAbsTime(uint32_t& abs_time_24bits);
    bool UpdateAbsTime(uint32_t abs_time_24bits);
    bool UpdateAbsTimeExternId(uint8_t abs_time_extern_id);

    // Transport-wide sequence number (TCC, RFC8888 like) extension accessors
    // 根据 tcc_extension_id_ 读取/写入扩展中的 16bit 序号
    bool ReadWideSeq(uint16_t& wide_seq);
    bool UpdateWideSeq(uint16_t wide_seq);
    bool UpdateWideSeqExternId(uint8_t wide_seq_extern_id);

    void SetNeedDelete(bool flag) { this->need_delete = flag; }
    bool GetNeedDelete() { return this->need_delete; }
    void EnableDebug() { debug_enable = true; }
    void DisableDebug() { debug_enable = false; }
    bool IsDebug() { return debug_enable; }
    
    int64_t GetLocalMs() {return this->local_ms;}

    void RtxDemux(uint32_t ssrc, uint8_t payloadtype);
    void RtxMux(uint8_t payload_type, uint32_t ssrc, uint16_t seq);

    std::string Dump();
    void SetLogger(Logger* logger) { logger_ = logger; }

public:
    static RtpPacket* Parse(uint8_t* data, size_t len);
    RtpPacket* Clone(uint8_t* buffer = nullptr);

private:
    void ParseExt();
    void ParseOnebyteExt();
    void ParseTwobytesExt();
    uint16_t GetExtId(HeaderExtension* rtp_ext);
    uint16_t GetExtLength(HeaderExtension* rtp_ext);
    uint8_t* GetExtValue(HeaderExtension* rtp_ext);
    bool HasOnebyteExt(HeaderExtension* rtp_ext);
    bool HasTwobytesExt(HeaderExtension* rtp_ext);

    uint8_t* GetExtension(uint8_t id, uint8_t& len);

    bool UpdateExtensionLength(uint8_t id, uint8_t len);

private:
    RtpCommonHeader* header = nullptr;
    HeaderExtension* ext     = nullptr;
    uint8_t* payload          = nullptr;
    size_t payload_len        = 0;
    uint8_t pad_len           = 0;
    size_t data_len           = 0;
    int64_t local_ms          = 0;
    bool need_delete          = false;
    bool debug_enable         = false;

private:
    uint8_t mid_extension_id_      = 0;
    uint8_t abs_time_extension_id_ = 0;
    uint8_t tcc_extension_id_      = 0;

private:
    std::map<uint8_t, OnebyteExtension*>  onebyte_ext_map_;
    std::map<uint8_t, TwobytesExtension*> twobytes_ext_map_;

private:
    Logger* logger_ = nullptr;
};

typedef enum 
{
    NORMAL_SEQ = 0,
    REPEAT_SEQ = 1,
    JUMP_LARGE_SEQ = 2,
    REVERSE_SEQ = 3,
    DISCORD_SEQ = 4,
    LITTLE_JUMP_SEQ = 5,
} SEQ_COMPARE_RESULT;

inline SEQ_COMPARE_RESULT CompareSeq(uint16_t last_seq, uint16_t current_seq) {
    if (current_seq == last_seq) {
        return REPEAT_SEQ;
    } else if (current_seq == last_seq + 1) {
        return NORMAL_SEQ;
    }
    
    const uint16_t MAX_GAP = 3000;
    if (current_seq < last_seq) {
        if (last_seq - current_seq > MAX_GAP) {
            return REVERSE_SEQ;
        }
        return NORMAL_SEQ;
    } else {
        if (current_seq > last_seq + MAX_GAP) {
            return JUMP_LARGE_SEQ;
        } else {
            return LITTLE_JUMP_SEQ;
        }
    }
    return DISCORD_SEQ;
}

}

#endif

