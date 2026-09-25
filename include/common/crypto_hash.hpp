#pragma once
#include <string>
#include <string_view>

/**
 * @file crypto_hash.hpp
 * @brief Shared cryptographic digest helpers.
 */

/**
 * @brief Computes the lowercase hex-encoded SHA-256 digest of `input`.
 *
 * Extracted so verification codes and password-reset tokens share a single
 * hashing implementation. Hex is chosen over base64 because the value is only
 * ever compared, never decoded, and stays greppable in DB inspection without
 * exposing the preimage.
 *
 * @param input Bytes to digest.
 * @return std::string The 64-character lowercase hex digest, or an empty
 * string if OpenSSL's digest call failed (fails closed: callers treat an
 * empty digest as a guaranteed non-match).
 */
std::string Sha256Hex(std::string_view input);
