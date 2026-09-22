#pragma once

#include "match/modload/artifacts.hpp"

#include <string>
#include <vector>

/**
 * @file mod_loader.hpp
 * @brief Scans a `mods/` root into the in-memory artifact model.
 *
 * Content store: one folder per mod, each with a `mod.json`
 * manifest and optional `cards.json` / `rules.json` / `mutations.json` /
 * `decks/*.json`. The loader enforces structural rules only (well-formed
 * JSON, required manifest fields, id syntax, api gate, duplicate ids, missing
 * referenced files). Semantic validation (references, arity, graph structure,
 * mutation conflicts) is the validator's job and runs over the model this
 * loader emits.
 */

namespace match::modload {

/**
 * @brief Scans `root` for mod folders and parses every one it finds.
 *
 * Folders are visited in deterministic (sorted) order so the emitted
 * `LoadResult::mods` is stable across filesystems, and finally sorted by
 * manifest id (see `LoadModsFromDirectory`).
 *
 * @param root  Directory holding one folder per mod.
 * @return LoadResult  Parsed mods on success; structured errors otherwise.
 */
LoadResult ScanModsDirectory(const std::string& root);

/**
 * @brief Reads the `UNI_MODS_DIR` env var (default `mods/`) and scans it.
 */
LoadResult LoadModsFromEnv();

/**
 * @brief Parses a single mod folder; errors are tagged with this folder.
 *
 * @param folder_path Path of the mod folder.
 * @param folder_name Folder name (suggested id; manifest id wins).
 * @param out         Appended to on success.
 * @return true when the folder loaded without errors.
 */
bool ParseModFolder(const std::string& folder_path,
                    const std::string& folder_name,
                    LoadedMod& out,
                    std::vector<LoadError>& errors);

/** @brief Engine mod-content api version this build understands. */
int EngineModApiVersion();

/** @brief True when `id` matches `[a-z0-9_]+` in 1..32 chars. */
bool IsValidLocalId(const std::string& id);

/** @brief True when `id` matches `namespace:local` with both parts valid. */
bool IsValidKindId(const std::string& id);

/** @brief Parses an integer from a JSON scalar (int or numeric string). */
bool ParseApiVersion(const nlohmann::json& value, int& out);

}  // namespace match::modload
