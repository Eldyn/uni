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
 * @brief False in production builds (UNI_PROD_BUILD or UNI_ENV=production):
 *        dev-only mods and freestyle lobbies must not be reachable.
 */
bool IsDevContentAllowed();

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

/**
 * @brief The namespace rule: a mod defines ids only in its own namespace, and
 *        only the `uni` mod defines `uni:` ids.
 *
 * References to ids of another namespace are not definitions and are not
 * checked here.
 *
 * @param mod_id  The mod that defines the id.
 * @param full_id The defined id, expected as `namespace:name`.
 * @return An empty string when the id is allowed; else the reason.
 */
std::string NamespaceRuleViolation(const std::string& mod_id,
                                   const std::string& full_id);

/** @brief Parses an integer from a JSON scalar (int or numeric string). */
bool ParseApiVersion(const nlohmann::json& value, int& out);

/**
 * @brief The mod a cardschema diagnostic belongs to, read from its
 *        `mods/<id>/` file label; empty when the label is not mod-prefixed.
 *
 * @param file The diagnostic file label.
 */
std::string CatalogDiagnosticModId(const std::string& file);

/**
 * @brief The mods a cardschema diagnostic is charged to, read from its
 *        `mods/<id>/` file label.
 *
 * A catalog diagnostic carries the label of the file that caused it, so the
 * loader charges it to the mod the label names and drops that mod. A label
 * that does not name a mod folder is not attributable; the returned list then
 * holds every loaded mod id, which makes the caller fail closed: the
 * diagnostic goes to the flat report and every mod that contributed to the
 * catalog is dropped.
 *
 * @param file           The diagnostic file label.
 * @param loaded_mod_ids The mods that contributed to the catalog.
 * @return The owners; empty only when no mod contributed.
 */
std::vector<std::string> CatalogDiagnosticOwners(
    const std::string& file, const std::vector<std::string>& loaded_mod_ids);

}  // namespace match::modload
