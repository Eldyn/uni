#include "match/modload/asset_index.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

/**
 * @file asset_index.cpp
 * @brief AssetIndex build + resolve.
 */

namespace match::modload {

namespace {

bool ReadBytes(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    out.assign(std::istreambuf_iterator<char>(in),
               std::istreambuf_iterator<char>());
    return true;
}

}  // namespace

std::string ShortContentHash(const std::string& bytes) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), digest, &length, EVP_sha256(),
                   nullptr)
        != 1) {
        return std::string();
    }
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(16);
    for (unsigned int i = 0; i < length && out.size() < 16; ++i) {
        out.push_back(kHex[digest[i] >> 4]);
        out.push_back(kHex[digest[i] & 0x0F]);
    }
    return out;
}

std::string AssetContentType(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (ext == ".png") return "image/png";
    if (ext == ".webp") return "image/webp";
    if (ext == ".jpg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".json") return "application/json";
    return std::string();
}

AssetIndex AssetIndex::Build(const LoadResult& loaded) {
    return BuildFromMods(loaded.mods);
}

AssetIndex AssetIndex::BuildFromMods(const std::vector<LoadedMod>& mods) {
    AssetIndex index;
    for (const LoadedMod& mod : mods) {
        for (const AssetBundleDef& bundle : mod.assets) {
            for (const AssetSlot& slot : bundle.slots) {
                for (const AssetVariant& variant : slot.variants) {
                    if (!variant.file.has_value()) continue;
                    std::error_code ec;
                    const fs::path path = fs::weakly_canonical(
                        fs::path(bundle.folder) / *variant.file, ec);
                    if (ec) continue;
                    std::string bytes;
                    if (!ReadBytes(path.string(), bytes)) continue;
                    AssetEntry entry;
                    entry.mod_id = mod.manifest.id;
                    entry.bundle_id = bundle.id;
                    entry.slot = slot.name;
                    entry.tier = variant.tier;
                    entry.hash = ShortContentHash(bytes);
                    entry.content_type = AssetContentType(path.string());
                    entry.path = path.string();
                    entry.size = bytes.size();
                    index.entries_.push_back(std::move(entry));
                }
            }
        }
    }
    return index;
}

const AssetEntry* AssetIndex::Resolve(const std::string& mod_id,
                                      const std::string& bundle_id,
                                      const std::string& slot,
                                      const std::string& tier,
                                      const std::string& hash) const {
    for (const AssetEntry& entry : entries_) {
        if (entry.mod_id == mod_id && entry.bundle_id == bundle_id
            && entry.slot == slot && ToString(entry.tier) == tier
            && entry.hash == hash) {
            return &entry;
        }
    }
    return nullptr;
}

const AssetEntry* AssetIndex::Find(const std::string& mod_id,
                                   const std::string& bundle_id,
                                   const std::string& slot,
                                   AssetTier tier) const {
    for (const AssetEntry& entry : entries_) {
        if (entry.mod_id == mod_id && entry.bundle_id == bundle_id
            && entry.slot == slot && entry.tier == tier) {
            return &entry;
        }
    }
    return nullptr;
}

std::vector<const AssetEntry*> AssetIndex::Variants(
    const std::string& mod_id,
    const std::string& bundle_id,
    const std::string& slot) const {
    std::vector<const AssetEntry*> out;
    for (const AssetEntry& entry : entries_) {
        if (entry.mod_id == mod_id && entry.bundle_id == bundle_id
            && entry.slot == slot) {
            out.push_back(&entry);
        }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const AssetEntry* a, const AssetEntry* b) {
                         return static_cast<int>(a->tier)
                             < static_cast<int>(b->tier);
                     });
    return out;
}

std::string AssetIndex::Url(const std::string& mod_id,
                            const std::string& bundle_id,
                            const std::string& slot,
                            AssetTier tier,
                            const std::string& hash) {
    return "/assets/" + mod_id + "/" + bundle_id + "/" + slot + "/"
        + ToString(tier) + "/" + hash;
}

}  // namespace match::modload
