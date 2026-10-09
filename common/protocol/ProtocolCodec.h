#pragma once

#include "common/net/Buffer.h"
#include "common/protocol/Packet.h"

#include <cstddef>
#include <string>
#include <vector>

namespace tinyimx {

enum class DecodeStatus {
    kOk = 0,
    kNeedMoreData,
    kInvalidMagic,
    kUnsupportedVersion,
    kBodyTooLarge,
    kInvalidMessageType
};

struct DecodeResult {
    DecodeStatus status{DecodeStatus::kNeedMoreData};
    std::vector<Packet> packets;
    std::string error_message;
};

class ProtocolCodec {
public:
    explicit ProtocolCodec(std::size_t max_body_size = kDefaultMaxBodySize);

    bool Encode(const Packet& packet,
                Buffer* output,
                std::string* error_message = nullptr) const;

    DecodeResult Decode(Buffer* input) const;

    std::size_t MaxBodySize() const;
    void SetMaxBodySize(std::size_t max_body_size);

private:
    bool ValidatePacketForEncode(const Packet& packet,
                                 std::string* error_message) const;

private:
    std::size_t max_body_size_{kDefaultMaxBodySize};
};

std::string DecodeStatusToString(DecodeStatus status);

}  // namespace tinyimx