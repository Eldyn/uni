#include <match/view/defs_builder.hpp>

#include <match/engine/match_assembler.hpp>
#include <match/modload/asset_index.hpp>
#include <match/view/event_sink.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @file defs_builder.cpp
 * @brief `defs` / `match_start` builder.
 */

namespace match::view {

namespace {

using nlohmann::json;

/** @brief Serialize a declarative face for the defs table. */
json FaceJson(const match::modload::FaceSpec& face) {
    json out = json::object();
    out["kind"] = match::modload::ToString(face.kind);
    if (face.color.has_value()) out["color"] = *face.color;
    if (face.label.has_value()) out["label"] = *face.label;
    if (face.url.has_value()) out["url"] = *face.url;
    out["art_version"] = face.art_version;
    return out;
}

/** @brief Resolve one card's `face.slots` from its asset bundle. */
json SlotsJson(const match::modload::LoadedMod& mod,
               const std::string& bundle_id,
               const match::modload::AssetIndex& index) {
    const match::modload::AssetBundleDef* bundle = nullptr;
    for (const auto& candidate : mod.assets) {
        if (candidate.id == bundle_id) {
            bundle = &candidate;
            break;
        }
    }
    if (bundle == nullptr) return json::object();

    json slots = json::object();
    for (const match::modload::AssetSlot& slot : bundle->slots) {
        json variants = json::array();
        for (const match::modload::AssetVariant& variant : slot.variants) {
            if (variant.value.has_value()) {
                variants.push_back(
                    {{"tier", match::modload::ToString(variant.tier)},
                     {"value", *variant.value}});
                continue;
            }
            const match::modload::AssetEntry* entry = index.Find(
                mod.manifest.id, bundle_id, slot.name, variant.tier);
            if (entry == nullptr) continue;
            variants.push_back(
                {{"tier", match::modload::ToString(entry->tier)},
                 {"url", match::modload::AssetIndex::Url(
                             entry->mod_id, entry->bundle_id, entry->slot,
                             entry->tier, entry->hash)},
                 {"hash", entry->hash}});
        }
        if (!variants.empty()) slots[slot.name] = std::move(variants);
    }
    return slots;
}

/** @brief A face plus the resolved slots of its asset bundle, if any. */
json FaceJsonWithSlots(const match::modload::FaceSpec& face,
                       const match::modload::LoadedMod* mod,
                       const match::modload::AssetIndex& index) {
    json out = FaceJson(face);
    if (face.art.has_value()) out["art"] = *face.art;
    out["art_mode"] = face.art_mode;
    out["art_fit"] = face.art_fit;
    if (!face.keep.empty()) out["keep"] = face.keep;
    if (mod != nullptr && face.art.has_value()) {
        json slots = SlotsJson(*mod, *face.art, index);
        if (!slots.empty()) out["slots"] = std::move(slots);
    }
    return out;
}

}  // namespace

nlohmann::json DefsBuilder::Build(
    const match::engine::MatchRegistries& registries,
    const std::vector<match::modload::LoadedMod>& mods) {
    using match::modload::CardDef;
    using match::modload::LoadedMod;

    // INFO: faces and tags are not carried on the assembly, so the loaded mod
    //       content is the source; index it by the full kind id. The owning mod
    //       is kept so the face's asset slots can be resolved.
    struct KindSource {
        const LoadedMod* mod = nullptr;
        const CardDef* card = nullptr;
    };
    const match::modload::AssetIndex asset_index =
        match::modload::AssetIndex::BuildFromMods(mods);
    std::unordered_map<std::string, KindSource> by_kind;
    for (const LoadedMod& mod : mods) {
        for (const CardDef& card : mod.cards) {
            by_kind[card.kind_id] = KindSource{&mod, &card};
        }
    }

    json mods_json = json::array();
    for (std::size_t i = 0; i < registries.mods.size(); ++i) {
        const match::ecs::ModRef& mod = registries.mods[i];
        mods_json.push_back(json{{"id", mod.id},
                                 {"version", mod.version},
                                 {"index", static_cast<int>(i)}});
    }

    json kinds_json = json::array();
    for (std::size_t mod_index = 0;
         mod_index < registries.kinds_by_mod.size(); ++mod_index) {
        const std::vector<std::string>& kinds =
            registries.kinds_by_mod[mod_index];
        for (std::size_t kind_index = 0; kind_index < kinds.size();
             ++kind_index) {
            const std::string& kind_id = kinds[kind_index];
            json face = json::object();
            json tags = json::array();
            const auto it = by_kind.find(kind_id);
            if (it != by_kind.end() && it->second.card != nullptr) {
                face = FaceJsonWithSlots(it->second.card->face, it->second.mod,
                                         asset_index);
                tags = it->second.card->tags;
            } else {
                // WARN: a frozen kind with no loaded def is a content
                //       inconsistency; keep the index slot with a blank face
                //       so kind_index alignment survives.
                face["kind"] = match::modload::ToString(
                    match::modload::FaceKind::kBlank);
                face["art_version"] = 1;
            }
            kinds_json.push_back(
                json{{"index", static_cast<int>(kind_index)},
                     {"string_id", kind_id},
                     {"face", face},
                     {"tags", tags}});
        }
    }

    json defs = json::object();
    defs["defs_digest"] = std::string();
    defs["mods"] = mods_json;
    defs["kinds"] = kinds_json;
    defs["defs_digest"] = Digest(defs);
    return defs;
}

std::string DefsBuilder::Digest(const nlohmann::json& defs_payload) {
    if (!defs_payload.is_object()) return std::string();
    json basis = json::object();
    basis["mods"] = defs_payload.contains("mods")
                        ? defs_payload["mods"]
                        : nlohmann::json::array();
    basis["kinds"] = defs_payload.contains("kinds")
                         ? defs_payload["kinds"]
                         : nlohmann::json::array();
    return StableDigest(basis.dump());
}

nlohmann::json DefsBuilder::BuildMatchStart(
    const match::engine::MatchRegistries& registries,
    const std::vector<match::modload::LoadedMod>& mods,
    const std::string& deck_id, const std::string& deck_name,
    const nlohmann::json& settings) {
    const json defs = Build(registries, mods);
    json out = json::object();
    out["defs_digest"] = defs["defs_digest"];
    out["mods"] = defs["mods"];
    out["deck_id"] = deck_id;
    out["deck_name"] = deck_name;
    out["settings"] =
        settings.is_object() ? settings : nlohmann::json::object();
    return out;
}

}  // namespace match::view
