#include "match/modload/mod_hash.hpp"

#include "common/crypto_hash.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * @file mod_hash.cpp
 * @brief Canonical content hash of a mod folder.
 */

namespace match::modload {

namespace fs = std::filesystem;

namespace {

bool ReadFileBytes(const fs::path& path, std::string& out) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) return false;
    out.assign(std::istreambuf_iterator<char>(input),
               std::istreambuf_iterator<char>());
    return true;
}

bool IsHiddenPath(const fs::path& relative) {
    for (const fs::path& part : relative) {
        if (!part.empty() && part.string().front() == '.') return true;
    }
    return false;
}

}  // namespace

std::string ComputeModContentHash(const std::string& folder_path) {
    const fs::path root(folder_path);
    std::error_code error;
    std::vector<std::pair<std::string, fs::path>> files;
    for (fs::recursive_directory_iterator it(root, error), end;
         !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        const fs::path relative = it->path().lexically_relative(root);
        if (IsHiddenPath(relative)) continue;
        files.emplace_back(relative.generic_string(), it->path());
    }
    if (error) return std::string();
    std::sort(files.begin(), files.end());

    // INFO: each file is hashed alone, so a big asset never sits in memory
    //       next to the others. The listing line has the form
    //       `<relative path>\0<file hash>\n`.
    std::string listing;
    for (const auto& [relative, path] : files) {
        std::string bytes;
        if (!ReadFileBytes(path, bytes)) return std::string();
        listing += relative;
        listing.push_back('\0');
        listing += Sha256Hex(bytes);
        listing.push_back('\n');
    }
    return Sha256Hex(listing);
}

}  // namespace match::modload
