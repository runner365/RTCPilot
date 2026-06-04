#include "rtp_packet.hpp"
#include "rtprtcp_pub.hpp"
#include "logger.hpp"
#include "timeex.hpp"
#include "byte_stream.hpp"
#ifdef _WIN64
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif
#include <sstream>
#include <cstring>
#include <assert.h>

namespace cpp_streamer
{

RtpPacket* RtpPacket::Parse(uint8_t* data, size_t len) {
    RtpCommonHeader* header = (RtpCommonHeader*)data;
    HeaderExtension* ext = nullptr;
    uint8_t* p = (uint8_t*)(header + 1);

    if (len > RTP_PACKET_MAX_SIZE) {
        CSM_THROW_ERROR("rtp len(%zu) is to large", len);
    }

    if (header->csrc_count > 0) {
        p += 4 * header->csrc_count;
    }

    if (header->extension) {
        if (len < (size_t)(p - data + 4)) {
            CSM_THROW_ERROR("rtp len(%zu) is to small", len);
        }
        ext = (HeaderExtension*)p;
        size_t extension_byte = (size_t)(ntohs(ext->length) * 4);
        if (len < (size_t)(p - data + 4 + extension_byte)) {
            CSM_THROW_ERROR("rtp len(%zu) is to small", len);
        }
        p += 4 + extension_byte;//4bytes(externsion header) + extersion bytes
    }

    if (len <= (size_t)(p - data)) {
        CSM_THROW_ERROR("rtp len(%zu) is to small, has no payload", len);
    }
    uint8_t* payload = p;
    size_t payload_len = len - (size_t)(p - data);
    uint8_t pad_len = 0;

    if (header->padding) {
        pad_len = data[len - 1];
        if (pad_len > 0) {
            if (payload_len < pad_len) {
                CSM_THROW_ERROR("rtp payload length(%zu), pad length(%d) error",
                        payload_len, pad_len);
            }
            payload_len -= pad_len;
        }
    }

    RtpPacket* pkt = new RtpPacket(header, ext, payload, payload_len, pad_len, len);

    return pkt;
}

RtpPacket::RtpPacket(RtpCommonHeader* header, HeaderExtension* ext,
                uint8_t* payload, size_t payload_len,
                uint8_t pad_len, size_t data_len) {
    if (data_len > RTP_PACKET_MAX_SIZE) {
        CSM_THROW_ERROR("rtp len(%zu) is to large", data_len);
    }
    this->header      = header;
    this->ext         = ext;
    this->payload     = payload;
    this->payload_len = payload_len;
    this->pad_len     = pad_len;

    this->data_len    = data_len;

    this->local_ms    = (int64_t)now_millisec();

    this->ParseExt();
    this->need_delete = false;
}

RtpPacket::~RtpPacket() {
    uint8_t* data = (uint8_t*)this->header;
    if (this->need_delete && data) {
        delete[] data;
    }
}

RtpPacket* RtpPacket::Clone(uint8_t* buffer) {
    uint8_t* new_data = nullptr;
    
    if (buffer) {
        new_data = buffer;
    } else {
        new_data = new uint8_t[RTP_PACKET_MAX_SIZE];
    }
    
    assert(this->GetDataLength() < RTP_PACKET_MAX_SIZE);
    memcpy(new_data, this->GetData(), this->GetDataLength());

    RtpPacket* new_pkt = RtpPacket::Parse(new_data, this->GetDataLength());

    if (buffer) {
        new_pkt->need_delete = false;
    } else {
        new_pkt->need_delete = true;
    }
    new_pkt->logger_ = this->logger_;
    new_pkt->local_ms = this->local_ms;
    new_pkt->mid_extension_id_ = this->mid_extension_id_;
    new_pkt->abs_time_extension_id_ = this->abs_time_extension_id_;
    new_pkt->tcc_extension_id_ = this->tcc_extension_id_;

    return new_pkt;
}

std::string RtpPacket::Dump() {
    std::stringstream ss;
    char desc[128];

    snprintf(desc, sizeof(desc), "%p", this->GetData());

    ss << "rtp packet data:" << desc << ", data length:" << this->data_len << "\r\n";
    ss << "  version:" << (int)this->Version() << ", padding:" << this->HasPadding();
    ss << ", extension:" << this->HasExtension() << ", csrc count:" << (int)this->CsrcCount() << "\r\n";
    ss << "  marker:" << (int)this->GetMarker() << ", payload type:" << (int)this->GetPayloadType() << "\r\n";
    ss << "  sequence:" << (int)this->GetSeq() << ", timestamp:" << this->GetTimestamp();
    ss << ", ssrc:" << this->GetSsrc() << "\r\n";

    snprintf(desc, sizeof(desc), "%p", this->GetPayload());
    ss << "  payload:" << desc << ", payload length:" << this->GetPayloadLength() << "\r\n";

    if (this->HasPadding()) {
        uint8_t* media_data = this->GetData();
        ss << "  padding len:" << (int)(media_data[this->data_len - 1]) << "\r\n";
    }

    if (this->HasExtension()) {
        if (!this->onebyte_ext_map_.empty()) {
            ss <<  "  rtp onebyte extension:" << "\r\n";
            for (auto item : this->onebyte_ext_map_) {
                OnebyteExtension* item_ext = item.second;
                ss << "    id:" << (int)item.first << ", length:" << (int)(item_ext->len) << "\r\n";
                if (item.first == mid_extension_id_) {
                    std::string mid_str((char*)(item_ext->value), (int)item_ext->len + 1);
                    ss << "      mid:" << mid_str << "\r\n";
                } else if (item.first == abs_time_extension_id_) {
                    uint32_t abs_time_24bits = ByteStream::Read3Bytes(item_ext->value);
                    double send_ms = abs_time_to_ms(abs_time_24bits);
                    ss << "      abs time:" << send_ms << "\r\n";
                }
            }
        }

        if (!this->twobytes_ext_map_.empty()) {
            ss << "  rtp twobytes extension:" << "\r\n";
            for ( auto item : this->twobytes_ext_map_) {
                TwobytesExtension* item_ext = item.second;
                ss << "    id:" << (int)item.first << ", length:" << (int)item_ext->len << "\r\n";
                if (item.first == mid_extension_id_) {
                    std::string mid_str((char*)(item_ext->value), (int)item_ext->len + 1);
                    ss << "      mid:" << mid_str << "\r\n";
                }
            }
        }
    }
    return ss.str();
}

uint16_t RtpPacket::GetExtId(HeaderExtension* rtp_ext) {
    if (rtp_ext == nullptr) {
        return 0;
    }
    
    return ntohs(rtp_ext->id);
}

uint16_t RtpPacket::GetExtLength(HeaderExtension* rtp_ext) {
    if (rtp_ext == nullptr) {
        return 0;
    }
    
    return ntohs(rtp_ext->length) * 4;
}

uint8_t* RtpPacket::GetExtValue(HeaderExtension* rtp_ext) {
    if (rtp_ext == nullptr) {
        return 0;
    }
    
    return rtp_ext->value;
}

void RtpPacket::ParseExt() {
    if ((this->header->extension == 0) || (this->ext == nullptr)) {
        return;
    }

    //base on rfc5285
    if (HasOnebyteExt(this->ext)) {
        ParseOnebyteExt();
    } else if (HasTwobytesExt(this->ext)) {
        ParseTwobytesExt();
    } else {
        CSM_THROW_ERROR("the rtp extension id(%02x) error", this->ext->id);
    }
}

void RtpPacket::ParseOnebyteExt() {
    onebyte_ext_map_.clear();

    uint8_t* ext_start = (uint8_t*)(this->ext) + 4;//skip id(16bits) + length(16bits)
    uint8_t* ext_end   = ext_start + GetExtLength(this->ext);
    uint8_t* p = ext_start;

/*
       0                   1                   2                   3
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |       0xBE    |    0xDE       |           length=3            |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |  ID   | L=0   |     data      |  ID   |  L=1  |   data...
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            ...data   |    0 (pad)    |    0 (pad)    |  ID   | L=3   |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                          data                                 |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
    while (p < ext_end) {
        uint8_t id = (*p & 0xF0) >> 4;
        size_t len = (size_t)(*p & 0x0F) + 1;

        if (id == 0x0f)
            break;

        if (id != 0) {
            if (p + 1 + len > ext_end) {
                CSM_THROW_ERROR("rtp extension length(%d) is not enough in one byte extension mode.",
                    GetExtLength(this->ext));
                break;
            }

            onebyte_ext_map_[id] = (OnebyteExtension*)p;
            p += (1 + len);
        }
        else {
            p++;
        }

        while ((p < ext_end) && (*p == 0))
        {
            p++;
        }
    }
}

void RtpPacket::ParseTwobytesExt() {
    twobytes_ext_map_.clear();

    uint8_t* ext_start = (uint8_t*)(this->ext) + 4;//skip id(16bits) + length(16bits)
    uint8_t* ext_end   = ext_start + GetExtLength(this->ext);
    uint8_t* p         = ext_start;

/*
       0                   1                   2                   3
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |       0x10    |    0x00       |           length=3            |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |      ID       |     L=0       |     ID        |     L=1       |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |       data    |    0 (pad)    |       ID      |      L=4      |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                          data                                 |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    The 8-bit length field is the length of extension data in bytes not
    including the ID and length fields.  The value zero indicates there
    is no data following.
*/
    while (p + 1 < ext_end) {
        uint8_t id  = *p;
        uint8_t len = *(p + 1);

        if (id != 0) {
            if (p + 2 + len > ext_end) {
                CSM_THROW_ERROR("rtp extension length(%d) is not enough in two bytes extension mode.",
                    GetExtLength(this->ext));
                break;
            }

            // Store the Two-Bytes extension element in the map.
            twobytes_ext_map_[id] = (TwobytesExtension*)p;

            p += (2 + len);
        } else {
            ++p;
        }

        while ((p < ext_end) && (*p == 0)) {
            ++p;
        }
    }
}

bool RtpPacket::HasOnebyteExt(HeaderExtension* rtp_ext) {
    return GetExtId(rtp_ext) == 0xBEDE;
}

bool RtpPacket::HasTwobytesExt(HeaderExtension* rtp_ext) {
/*
       0                   1
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |         0x100         |appbits|
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
    return (GetExtId(rtp_ext) & 0xfff0) == 0x1000;
}

uint8_t* RtpPacket::GetExtension(uint8_t id, uint8_t& len) {
    if (HasOnebyteExt(this->ext)) {
        auto iter = onebyte_ext_map_.find(id);
        if (iter == onebyte_ext_map_.end()) {
            return nullptr;
        }

        OnebyteExtension* ext_data = iter->second;
        len = ext_data->len + 1;
        return ext_data->value;
    } else if (HasTwobytesExt(this->ext)) {
        auto iter = twobytes_ext_map_.find(id);
        if (iter == twobytes_ext_map_.end()) {
            return nullptr;
        }
        TwobytesExtension* ext_data = iter->second;
        len = ext_data->len;
        if (len == 0) {
            return nullptr;
        }
        return ext_data->value;
    } else {
        // LogErrorf(logger_, "get extension bytes type is wrong.");
        return nullptr;
    }
}

bool RtpPacket::UpdateMid(uint8_t mid) {
    uint8_t extern_len = 0;
    uint8_t* extern_value = GetExtension(this->mid_extension_id_, extern_len);

    if (extern_value == nullptr) {
        LogErrorf(logger_, "update mid, The rtp packet has not extern mid:%d", this->mid_extension_id_);
        return false;
    }

    std::string mid_str = std::to_string(mid);

    memcpy(extern_value, mid_str.c_str(), mid_str.length());

    //update extension length
    return UpdateExtensionLength(mid_extension_id_, (uint8_t)mid_str.length());
}

bool RtpPacket::UpdateMid(uint8_t new_mid_extern_id, uint8_t mid) {
    uint8_t len;
    uint8_t* extern_value = nullptr;

    // update the mid extern_id and the value
    if (HasOnebyteExt(this->ext)) {
        auto iter = onebyte_ext_map_.find(mid_extension_id_);
        if (iter == onebyte_ext_map_.end()) {
            LogDebugf(logger_, "fail to find the mid extern_id:%d in onebytes", mid_extension_id_);
            return false;
        }

        OnebyteExtension* ext_data = iter->second;
        len = ext_data->len + 1;
        extern_value = ext_data->value;
        ext_data->id = new_mid_extern_id;

        onebyte_ext_map_.erase(mid_extension_id_);
        mid_extension_id_ = new_mid_extern_id;
        onebyte_ext_map_[mid_extension_id_] = ext_data;
    } else if (HasTwobytesExt(this->ext)) {
        auto iter = twobytes_ext_map_.find(mid_extension_id_);
        if (iter == twobytes_ext_map_.end()) {
            LogDebugf(logger_, "fail to find the mid extern_id:%d in onebytes", mid_extension_id_);
            return false;
        }
        TwobytesExtension* ext_data = iter->second;
        len = ext_data->len;
        if (len == 0) {
            LogErrorf(logger_, "two bytes ext len is zero");
            return false;
        }
        extern_value = ext_data->value;
        ext_data->id = new_mid_extern_id;
        twobytes_ext_map_.erase(mid_extension_id_);
        mid_extension_id_ = new_mid_extern_id;
        twobytes_ext_map_[mid_extension_id_] = ext_data;
    } else {
        LogErrorf(logger_, "update mid, the extension bytes type is wrong, mid_extern_id:%d, mid:%d", mid_extension_id_, mid);
        return false;
    }
    std::string mid_str = std::to_string(mid);

    memcpy(extern_value, mid_str.c_str(), mid_str.length());

    return true;
}

bool RtpPacket::ReadMid(uint8_t& mid) {
    uint8_t extern_len = 0;
    uint8_t* extern_value = GetExtension(this->mid_extension_id_, extern_len);

    if (extern_value == nullptr) {
        //LogErrorf(logger_, "read mid, The rtp packet has not extern mid:%d", this->mid_extension_id_);
        return false;
    }
    std::string mid_str((char*)extern_value, extern_len);
    mid = (uint8_t)atoi(mid_str.c_str());

    return true;
}

bool RtpPacket::ClearMidExtension(uint8_t mid_value, uint8_t& old_ext_id) {
    std::string mid_str = std::to_string(mid_value);

    //mid_extension_id_ is the extern_id of mid extension，clear the mid extension id
    mid_extension_id_ = 0;

    if (HasOnebyteExt(this->ext)) {
        uint8_t* ext_start = (uint8_t*)(this->ext) + 4;
        uint8_t* ext_end = ext_start + GetExtLength(this->ext);

        for (auto& item : onebyte_ext_map_) {
            OnebyteExtension* ext_data = item.second;
            std::string val((char*)ext_data->value, ext_data->len + 1);
            if (val == mid_str) {
                old_ext_id = item.first;
                // One-byte element: header(1 byte) + value(len+1 bytes)
                size_t total_len = (size_t)ext_data->len + 2;
                uint8_t* ptr = (uint8_t*)ext_data;
                if (ptr + total_len > ext_end) {
                    return false;
                }
                memset(ptr, 0, total_len);
                onebyte_ext_map_.erase(item.first);
                return true;
            }
        }
    } else if (HasTwobytesExt(this->ext)) {
        uint8_t* ext_start = (uint8_t*)(this->ext) + 4;
        uint8_t* ext_end = ext_start + GetExtLength(this->ext);

        for (auto& item : twobytes_ext_map_) {
            TwobytesExtension* ext_data = item.second;
            std::string val((char*)ext_data->value, ext_data->len);
            if (val == mid_str) {
                old_ext_id = item.first;
                // Two-byte element: id(1 byte) + len(1 byte) + value(len bytes)
                size_t total_len = (size_t)ext_data->len + 2;
                uint8_t* ptr = (uint8_t*)ext_data;
                if (ptr + total_len > ext_end) {
                    return false;
                }
                memset(ptr, 0, total_len);
                twobytes_ext_map_.erase(item.first);
                return true;
            }
        }
    }
    return false;
}

bool RtpPacket::ReadAbsTime(uint32_t& abs_time_24bits) {
    uint8_t extern_len = 0;
    uint8_t* extern_value = GetExtension(this->abs_time_extension_id_, extern_len);

    if (extern_value == nullptr) {
        //LogErrorf(logger_, "The rtp packet has not extern abs time id:%d", this->abs_time_extension_id_);
        return false;
    }

    if (extern_len != 3) {
        LogWarnf(logger_, "read abs time length is not 3, extern_len:%d", extern_len);
    }
    abs_time_24bits = ByteStream::Read3Bytes(extern_value);

    return true;
}

bool RtpPacket::UpdateAbsTimeExternId(uint8_t new_abs_time_extern_id) {
    uint8_t len = 0;

    if (HasOnebyteExt(this->ext)) {
        auto iter = onebyte_ext_map_.find(abs_time_extension_id_);
        if (iter == onebyte_ext_map_.end()) {
            LogErrorf(logger_, "fail to find abs time extern_id:%d in one byte map", abs_time_extension_id_);
            return false;
        }

        OnebyteExtension* ext_data = iter->second;
        len = ext_data->len + 1;
        onebyte_ext_map_.erase(abs_time_extension_id_);
        ext_data->id = new_abs_time_extern_id;
        abs_time_extension_id_ = new_abs_time_extern_id;
        onebyte_ext_map_[new_abs_time_extern_id] = ext_data;
    } else if (HasTwobytesExt(this->ext)) {
        auto iter = twobytes_ext_map_.find(abs_time_extension_id_);
        if (iter == twobytes_ext_map_.end()) {
            LogErrorf(logger_, "fail to find abs time extern_id:%d in two byte map", abs_time_extension_id_);
            return false;
        }
        TwobytesExtension* ext_data = iter->second;
        len = ext_data->len;
        if (len == 0) {
            LogErrorf(logger_, "two bytes extension get len is zero");
            return false;
        }
        twobytes_ext_map_.erase(abs_time_extension_id_);
        ext_data->id = new_abs_time_extern_id;
        abs_time_extension_id_ = new_abs_time_extern_id;
        twobytes_ext_map_[new_abs_time_extern_id] = ext_data;
    } else {
        LogErrorf(logger_, "update abs time extern_id, the extension bytes type is wrong.");
        return false;
    }

    return true;
}

bool RtpPacket::UpdateAbsTime(uint32_t abs_time_24bits) {
    uint8_t extern_len = 0;
    uint8_t* extern_value = GetExtension(this->abs_time_extension_id_, extern_len);

    if (extern_value == nullptr) {
        LogErrorf(logger_, "The rtp packet has not extern abs time id:%d", this->abs_time_extension_id_);
        return false;
    }

    if (extern_len != 3) {
        LogWarnf(logger_, "update abs time length is not 3, extern_len:%d", extern_len);
    }
    ByteStream::Write3Bytes(extern_value, abs_time_24bits);

    //update extension length
    return UpdateExtensionLength(abs_time_extension_id_, 3);
}

bool RtpPacket::ReadWideSeq(uint16_t& wide_seq) {
    uint8_t extern_len = 0;
    uint8_t* extern_value = GetExtension(this->tcc_extension_id_, extern_len);

    if (extern_value == nullptr) {
        // extension not found
        return false;
    }

    if (extern_len != 2) {
        LogWarnf(logger_, "read wide seq length is not 2, extern_len:%d", extern_len);
    }
    wide_seq = ByteStream::Read2Bytes(extern_value);
    return true;
}

bool RtpPacket::UpdateWideSeqExternId(uint8_t new_wide_seq_extern_id) {
    uint8_t id = tcc_extension_id_;
    uint8_t len = 0;

    if (HasOnebyteExt(this->ext)) {
        auto iter = onebyte_ext_map_.find(id);
        if (iter == onebyte_ext_map_.end()) {
            return false;
        }

        OnebyteExtension* ext_data = iter->second;
        onebyte_ext_map_.erase(id);
        ext_data->id = new_wide_seq_extern_id;
        tcc_extension_id_ = new_wide_seq_extern_id;
        onebyte_ext_map_[new_wide_seq_extern_id] = ext_data;
    } else if (HasTwobytesExt(this->ext)) {
        auto iter = twobytes_ext_map_.find(id);
        if (iter == twobytes_ext_map_.end()) {
            return false;
        }
        TwobytesExtension* ext_data = iter->second;
        len = ext_data->len;
        if (len == 0) {
            return false;
        }
        twobytes_ext_map_.erase(id);
        ext_data->id = new_wide_seq_extern_id;
        tcc_extension_id_ = new_wide_seq_extern_id;
        twobytes_ext_map_[new_wide_seq_extern_id] = ext_data;
    } else {
        LogErrorf(logger_, "update wide seq extern_id, the extension bytes type is wrong.");
        return false;
    }
    return true;
}

bool RtpPacket::UpdateWideSeq(uint16_t wide_seq) {
    uint8_t extern_len = 0;
    uint8_t* extern_value = GetExtension(this->tcc_extension_id_, extern_len);

    if (extern_value == nullptr) {
        LogErrorf(logger_, "The rtp packet has not extern tcc id:%d", this->tcc_extension_id_);
        return false;
    }

    if (extern_len != 2) {
        LogWarnf(logger_, "update wide seq length is not 2, extern_len:%d", extern_len);
    }
    ByteStream::Write2Bytes(extern_value, wide_seq);
    return UpdateExtensionLength(tcc_extension_id_, 2);
}

bool RtpPacket::UpdateExtensionLength(uint8_t id, uint8_t len) {
    if (len == 0) {
        LogErrorf(logger_, "update extension length error: len must not be zero.");
        return false;
    }
    if (HasOnebyteExt(this->ext)) {
        auto iter = this->onebyte_ext_map_.find(id);
        if (iter == this->onebyte_ext_map_.end()) {
            LogErrorf(logger_, "fail to get id:%d from the onebyte ext map.", id);
            return false;
        }
        auto* extension = iter->second;
        uint8_t current_len = extension->len + 1;
        if (len < current_len) {
            memset(extension->value + len, 0, current_len - len);
        }
        extension->len = len - 1;
    } else if (HasTwobytesExt(this->ext)) {
        auto iter = this->twobytes_ext_map_.find(id);
        if (iter == this->twobytes_ext_map_.end()) {
            LogErrorf(logger_, "fail to get id:%d from the twobytes ext map.", id);
            return false;
        }
        auto* extension = iter->second;
        uint8_t current_len = extension->len;
        if (len < current_len) {
            memset(extension->value + len, 0, current_len - len);
        }
        extension->len = len;
    } else {
        LogErrorf(logger_, "update extension length, id:%d, the extension bytes type is wrong.", id);
        return false;
    }
    return true;
}

void RtpPacket::RtxDemux(uint32_t ssrc, uint8_t payloadtype) {
    if (this->payload_len < 2) {
        CSM_THROW_ERROR("rtx payload len(%zu) is less than 2", this->payload_len);
    }

    uint16_t replace_seq = ByteStream::Read2Bytes(this->payload);
    SetPayloadType(payloadtype);
    SetSeq(replace_seq);
    SetSsrc(ssrc);

    std::memmove(this->payload, this->payload + 2, this->payload_len - 2);
    this->payload_len -= 2;
    this->data_len    -= 2;

    if (this->HasPadding()) {
        SetPadding(false);
        this->data_len -= this->pad_len;
        this->pad_len   = 0;
    }
}

void RtpPacket::RtxMux(uint8_t payload_type, uint32_t ssrc, uint16_t seq) {
    SetPayloadType(payload_type);
    SetSsrc(ssrc);
    
    std::memmove(this->payload + 2, this->payload, this->payload_len);
    ByteStream::Write2Bytes(this->payload, GetSeq());

    SetSeq(seq);

    this->payload_len += 2u;
    this->data_len += 2u;

    //remove padding 
    if (this->HasPadding())
    {
        SetPadding(false);

        this->data_len -= this->pad_len;
        this->pad_len = 0;
    }
}
}

