#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <result.hpp>
#include <common/base64.hpp>
#include <common/env.hpp>

/**
 * @file crypto.hpp
 * @brief AES-256-GCM helper for encrypting data at rest (DM ciphertext).
 */

namespace Crypto {

inline constexpr size_t kKeyBytes   = 32;  // AES-256
inline constexpr size_t kNonceBytes = 12;  // Standard GCM nonce size
inline constexpr size_t kTagBytes   = 16;  // GCM authentication tag size

/**
 * @struct EncryptedBlob
 * @brief A ciphertext with the nonce needed to decrypt it, both base64-encoded
 * for storage as TEXT columns.
 * @tag CMN-CRYPTO-STR-001
 */
struct EncryptedBlob {
    std::string nonce_b64;
    std::string ciphertext_b64;  // Ciphertext with the GCM tag appended.
};

/**
 * @brief Reads the 32-byte AES-256 key from the `CHAT_DM_KEY` environment
 * variable (base64-encoded). Fails fast at startup rather than deep in a
 * request handler if the key is missing or the wrong size.
 * @tag CMN-CRYPTO-MTH-001
 */
inline std::vector<uint8_t> LoadKey() {
    auto key = Base64::Decode(Env::Require("CHAT_DM_KEY"));
    if (key.size() != kKeyBytes) {
        throw std::runtime_error("[Crypto] CHAT_DM_KEY must decode to 32 bytes");
    }
    return key;
}

/**
 * @brief Encrypts `plaintext` with AES-256-GCM under `key`, using a fresh
 * random nonce per call.
 * @tag CMN-CRYPTO-MTH-002
 */
inline Result<EncryptedBlob> Encrypt(const std::string& plaintext,
                                     const std::vector<uint8_t>& key) {
    std::vector<uint8_t> nonce(kNonceBytes);
    if (RAND_bytes(nonce.data(), static_cast<int>(kNonceBytes)) != 1) {
        return std::unexpected(Error::Internal("[Crypto] CSPRNG failure: RAND_bytes returned 0"));
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return std::unexpected(Error::Internal("[Crypto] Failed to allocate cipher context"));
    }

    std::vector<uint8_t> ciphertext(plaintext.size());
    int len = 0;
    int ciphertext_len = 0;
    std::vector<uint8_t> tag(kTagBytes);

    bool ok =
        EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kNonceBytes),
                            nullptr) == 1 &&
        EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) == 1 &&
        EVP_EncryptUpdate(ctx, ciphertext.data(), &len,
                          reinterpret_cast<const uint8_t*>(plaintext.data()),
                          static_cast<int>(plaintext.size())) == 1;
    ciphertext_len = len;

    ok = ok && EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) == 1;
    ciphertext_len += len;

    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, static_cast<int>(kTagBytes),
                                   tag.data()) == 1;

    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        return std::unexpected(Error::Internal("[Crypto] AES-256-GCM encryption failed"));
    }

    ciphertext.resize(ciphertext_len);
    ciphertext.insert(ciphertext.end(), tag.begin(), tag.end());

    return EncryptedBlob{Base64::Encode(nonce), Base64::Encode(ciphertext)};
}

/**
 * @brief Decrypts a blob produced by Encrypt(), verifying the GCM tag.
 * @return Result<std::string> The plaintext, or an Error if the key/nonce is
 * wrong or the ciphertext was tampered with (tag mismatch).
 * @tag CMN-CRYPTO-MTH-003
 */
inline Result<std::string> Decrypt(const EncryptedBlob& blob, const std::vector<uint8_t>& key) {
    std::vector<uint8_t> nonce = Base64::Decode(blob.nonce_b64);
    std::vector<uint8_t> data  = Base64::Decode(blob.ciphertext_b64);
    if (nonce.size() != kNonceBytes || data.size() < kTagBytes) {
        return std::unexpected(Error::Internal("[Crypto] Malformed encrypted blob"));
    }

    std::vector<uint8_t> ciphertext(data.begin(), data.end() - kTagBytes);
    std::vector<uint8_t> tag(data.end() - kTagBytes, data.end());

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return std::unexpected(Error::Internal("[Crypto] Failed to allocate cipher context"));
    }

    std::vector<uint8_t> plaintext(ciphertext.size());
    int len = 0;
    int plaintext_len = 0;

    bool ok =
        EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kNonceBytes),
                            nullptr) == 1 &&
        EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) == 1 &&
        EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(),
                          static_cast<int>(ciphertext.size())) == 1;
    plaintext_len = len;

    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(kTagBytes),
                                   tag.data()) == 1;
    ok = ok && EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) == 1;
    plaintext_len += len;

    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        return std::unexpected(Error::Unauthorised("[Crypto] Decryption failed: bad key or "
                                                    "tampered ciphertext"));
    }

    plaintext.resize(plaintext_len);
    return std::string(plaintext.begin(), plaintext.end());
}

}  // namespace Crypto
