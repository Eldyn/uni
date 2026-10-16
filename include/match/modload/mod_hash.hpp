#pragma once

#include <string>

/**
 * @file mod_hash.hpp
 * @brief The content hash of one mod folder.
 *
 * The `defs` packet lists the hash of each mod beside its version. The client
 * keys its cache on both, so a changed file invalidates the cache.
 */

namespace match::modload {

/**
 * @brief SHA-256 (lowercase hex) of every regular file in `folder_path`.
 *
 * The files are taken in the canonical order of their relative paths (byte
 * order of the path with `/` separators). Each file contributes its relative
 * path and the SHA-256 of its bytes, so a rename, a new file, a removed file,
 * and a changed byte all change the hash. The folder path itself does not
 * change the hash. Files whose name starts with `.` are skipped.
 *
 * @param folder_path Path of the mod folder.
 * @return 64 hex characters, or an empty string when the folder cannot be
 *         read (the caller treats that as no hash).
 */
std::string ComputeModContentHash(const std::string& folder_path);

}  // namespace match::modload
