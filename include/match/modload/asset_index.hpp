#pragma once

#include "match/modload/artifacts.hpp"

#include <cstdint>
#include <string>
#include <vector>

/**
 * @file asset_index.hpp
 * @brief In-memory asset index for serving mod bundles by id + hash.
 *
 * Built from a `LoadResult`, it maps the route's `(mod, bundle, slot, tier,
 * hash)` tuple to a real file. The client only ever sees ids and content
 * hashes; no filesystem path crosses the wire, and the route cannot express a
 * path, so traversal is structurally impossible.
 */

namespace match::modload {

/**
 * @brief Short content hash: the first 16 hex chars of SHA-256.
 */
std::string ShortContentHash(const std::string& bytes);

/**
 * @brief Content type for an allowlisted asset extension, or "" if unknown.
 */
std::string AssetContentType(const std::string& path);

/**
 * @struct AssetEntry
 * @brief One servable file variant.
 */
struct AssetEntry {
    std::string mod_id;
    std::string bundle_id;
    std::string slot;
    AssetTier tier = AssetTier::kLow;
    std::string hash;          /**< Short content hash (URL + ETag). */
    std::string content_type;  /**< Allowlist-derived MIME type. */
    std::string path;          /**< Server-side path (never sent). */
    std::uintmax_t size = 0;
};

/**
 * @class AssetIndex
 * @brief Immutable id -> file map built from one scan.
 */
class AssetIndex {
   public:
    /**
     * @brief Hash every file variant of every loaded mod's bundles.
     *
     * Unreadable or missing files are skipped (the loader already rejected
     * them as errors; a mod carrying one would not be in `loaded.mods`).
     */
    static AssetIndex Build(const LoadResult& loaded);

    /** @brief Same as `Build`, for callers holding only the mod list. */
    static AssetIndex BuildFromMods(const std::vector<LoadedMod>& mods);

    /**
     * @brief Resolve a route tuple to an entry, or nullptr.
     *
     * `tier` is the wire token (`high`/`medium`/`low`); `hash` must match the
     * entry's content hash, so a stale or forged hash never resolves.
     */
    const AssetEntry* Resolve(const std::string& mod_id,
                              const std::string& bundle_id,
                              const std::string& slot,
                              const std::string& tier,
                              const std::string& hash) const;

    /** @brief First entry for a `(mod, bundle, slot, tier)`, or nullptr. */
    const AssetEntry* Find(const std::string& mod_id,
                           const std::string& bundle_id,
                           const std::string& slot,
                           AssetTier tier) const;

    /**
     * @brief Entries for one `(mod, bundle, slot)`, richest tier first.
     */
    std::vector<const AssetEntry*> Variants(const std::string& mod_id,
                                            const std::string& bundle_id,
                                            const std::string& slot) const;

    /**
     * @brief Client-facing URL for a variant: ids + hash only.
     */
    static std::string Url(const std::string& mod_id,
                           const std::string& bundle_id,
                           const std::string& slot,
                           AssetTier tier,
                           const std::string& hash);

    bool empty() const { return entries_.empty(); }
    const std::vector<AssetEntry>& entries() const { return entries_; }

   private:
    std::vector<AssetEntry> entries_;
};

}  // namespace match::modload
