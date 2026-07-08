#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>

namespace Base64 {

inline constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
inline constexpr uint8_t kInvalid = 0xFF;

inline constexpr std::array<uint8_t, 256> kDecodeTable = []() {
    std::array<uint8_t, 256> table{};
    table.fill(kInvalid);

    for (uint8_t i = 0; i < 64; ++i) {
        table[static_cast<uint8_t>(kAlphabet[i])] = i;
    }

    return table;
}();

//  Takes arbitrary binary data and returns its base64 representation.
//  Every 3 input bytes → 4 output characters; '=' padding fills the tail.
//
//  Bit arithmetic:
//    Given bytes [A, B, C] packed into a 24-bit integer:
//      bits 23-18 → index 0
//      bits 17-12 → index 1
//      bits 11- 6 → index 2
//      bits  5- 0 → index 3
//
// @param data   Raw bytes to encode.
// @returns      Base64-encoded ASCII string.
inline std::string Encode(const std::vector<uint8_t>& data) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t b = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size()) b |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < data.size()) b |= static_cast<uint32_t>(data[i + 2]);

        out += kAlphabet[(b >> 18) & 0x3F];
        out += kAlphabet[(b >> 12) & 0x3F];

        out += (i + 1 < data.size()) ? kAlphabet[(b >> 6) & 0x3F] : '=';
        out += (i + 2 < data.size()) ? kAlphabet[ b       & 0x3F] : '=';
    }
    return out;
}

//  Inverse of Encode. Validates every character against kDecodeTable and
//  throws std::invalid_argument on malformed input.
//
// @param encoded   Base64 string (may include '=' padding).
// @returns         Decoded raw bytes.
// @throws          std::invalid_argument if `encoded` contains illegal chars
//                  or has a length that is not a multiple of 4.
inline std::vector<uint8_t> Decode(const std::string& encoded) {
    if (encoded.size() % 4 != 0) {
        throw std::invalid_argument("base64::Decode: length must be a multiple of 4");
    }

    std::vector<uint8_t> out;
    out.reserve((encoded.size() / 4) * 3);

    for (size_t i = 0; i < encoded.size(); i += 4) {
        auto idx = [&](size_t pos) -> uint32_t {
            uint8_t c = static_cast<uint8_t>(encoded[pos]);
            uint8_t v = kDecodeTable[c];
            if (v == kInvalid && encoded[pos] != '=') {
                throw std::invalid_argument(
                    std::string("base64::Decode: invalid character '") + encoded[pos] + "'");
            }
            return (encoded[pos] == '=') ? 0u : static_cast<uint32_t>(v);
        };

        // Pack four 6-bit values back into a 24-bit integer.
        uint32_t b = (idx(i) << 18) | (idx(i+1) << 12) | (idx(i+2) << 6) | idx(i+3);

        out.push_back(static_cast<uint8_t>((b >> 16) & 0xFF));
        if (encoded[i + 2] != '=') out.push_back(static_cast<uint8_t>((b >> 8) & 0xFF));
        if (encoded[i + 3] != '=') out.push_back(static_cast<uint8_t>( b       & 0xFF));
    }
    return out;
}

}
