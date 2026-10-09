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

/**
 * @brief One hash-addressed `defs` manifest entry.
 *
 * Shape `{id: "namespace:local", def_hash, ...full def}`: the def's verbatim
 * JSON with its local `id` replaced by the full `namespace:id`. `def_hash` is
 * the existing stable digest over that canonical JSON (minus the hash itself),
 * so the client caches by content and reloads only what changed.
 */
json ManifestEntry(const nlohmann::json& raw, const std::string& full_id) {
    json entry = raw.is_object() ? raw : json::object();
    entry["id"] = full_id;
    entry.erase("def_hash");
    entry["def_hash"] = StableDigest(entry.dump());
    return entry;
}

/** @brief The `defs` manifest array for a `full-id -> def` registry. */
template <typename Registry>
json ManifestArray(const Registry& registry) {
    json out = json::array();
    for (const auto& [full_id, def] : registry) {
        out.push_back(ManifestEntry(def.raw, full_id));
    }
    return out;
}

/**
 * @brief The `defs` manifest entry of an event definition.
 *
 * The client needs the id, the retention, and the field types. The field
 * audiences are server policy and stay on the server.
 */
json EventManifestEntry(const match::modload::EventDef& event) {
    json fields = json::array();
    for (const match::modload::EventFieldDef& field : event.fields) {
        fields.push_back(json{{"name", field.name}, {"type", field.type}});
    }
    json entry = json{{"id", event.event_id},
                      {"retention", event.retention},
                      {"fields", std::move(fields)},
                      {"context", event.context}};
    entry["def_hash"] = StableDigest(entry.dump());
    return entry;
}

/**
 * @brief The `texts` listing of one mod: the locale, the SHA-256 of the
 *        bytes, and the size of each texts file. The client fetches only the
 *        files that it needs and checks each hash before it parses the file.
 */
json TextsListing(const std::vector<match::modload::LoadedMod>& mods,
                  const std::string& mod_id) {
    json listing = json::array();
    for (const match::modload::LoadedMod& loaded : mods) {
        if (loaded.manifest.id != mod_id) continue;
        for (const match::modload::TextsFileDef& texts : loaded.texts) {
            listing.push_back(json{{"locale", texts.locale},
                                   {"hash", texts.hash},
                                   {"size", texts.size}});
        }
    }
    return listing;
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
        std::string content_hash;
        for (const LoadedMod& loaded : mods) {
            if (loaded.manifest.id == mod.id) {
                content_hash = loaded.content_hash;
            }
        }
        json mod_json = json{{"id", mod.id},
                             {"version", mod.version},
                             {"hash", content_hash},
                             {"index", static_cast<int>(i)}};
        json texts = TextsListing(mods, mod.id);
        if (!texts.empty()) mod_json["texts"] = std::move(texts);
        mods_json.push_back(std::move(mod_json));
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
    // INFO: entities/presentations are client-consumed, so unlike
    //       tables they are published here, hash-addressed per def.
    defs["entities"] = ManifestArray(registries.entities);
    // INFO: a presentation binding (file kind `uni:binding`) shares the
    //       `presentations` array with the old kind. The old kind has a
    //       `target`; the client tells them apart by the `uni:binding` key.
    //       A binding is static: it holds no audience and no event value.
    json presentations = ManifestArray(registries.presentations);
    for (const json& binding : ManifestArray(registries.bindings)) {
        presentations.push_back(binding);
    }
    defs["presentations"] = std::move(presentations);
    json events_json = json::array();
    for (const auto& [event_id, event] : registries.events) {
        events_json.push_back(EventManifestEntry(event));
    }
    defs["events"] = std::move(events_json);
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
    // INFO: entities/presentations are client-consumed content, so they
    //       participate in the stale-cache digest alongside mods/kinds.
    basis["entities"] = defs_payload.contains("entities")
                            ? defs_payload["entities"]
                            : nlohmann::json::array();
    basis["presentations"] = defs_payload.contains("presentations")
                                 ? defs_payload["presentations"]
                                 : nlohmann::json::array();
    basis["events"] = defs_payload.contains("events")
                          ? defs_payload["events"]
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
