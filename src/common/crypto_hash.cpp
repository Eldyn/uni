#include <common/crypto_hash.hpp>
#include <logger.hpp>
#include <openssl/evp.h>

std::string Sha256Hex(std::string_view input) {
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int  len = 0;

    // INFO: EVP_Digest returns 1 on success. A SHA-256 digest of a well-formed,
    //       in-memory input essentially never fails in practice, but the return
    //       value is still checked: on failure it logs loudly and return an
    //       empty digest, which callers already treat as a non-match.
    if (EVP_Digest(input.data(), input.size(), out, &len, EVP_sha256(), nullptr) != 1) {
        Logger::Error("[Crypto] EVP_Digest failure while hashing");
        return "";
    }

    static constexpr char kHexDigits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(static_cast<size_t>(len) * 2);
    for (unsigned int i = 0; i < len; ++i) {
        hex.push_back(kHexDigits[(out[i] >> 4) & 0x0F]);
        hex.push_back(kHexDigits[out[i] & 0x0F]);
    }
    return hex;
}
