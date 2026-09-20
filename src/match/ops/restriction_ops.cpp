#include <match/modload/restriction.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * @file restriction_ops.cpp
 * @brief Type and restriction op bodies.
 *
 * `set_active_type` / `clear_active_type` maintain the `ActiveTypeReq`
 * component on the match entity. `set_active_type` takes either a literal
 * `type` or a `from_prompt` node id whose bound prompt value is
 * read from `ctx.frame`.
 *
 * `add_restriction` / `remove_restriction` maintain the ordered the store
 * `PlayRestriction` pipeline: entries are data components on the
 * match entity and vanilla ships its entries as removable data. `add` rejects a
 * duplicate id with a `kError` (fail-safe) so the pipeline never carries two
 * entries with the same reason id; a malformed `entry_def` is a `kResolved`
 * no-op. Neither op emits an event: a pipeline denial emits `play_rejected`
 * from the play-legality path, not from the pipeline edit.
 *
 * Every fail-safe path — an unbound selector, a dead entity, a missing match,
 * a missing / malformed arg, a malformed prompt value — is a `kResolved` no-op
 * (or the documented duplicate-id error), never a crash.
 */

namespace match::ops::detail {
namespace {

using nlohmann::json;

/** @brief Entity handle as a JSON object. */
json EntityJson(ecs::Entity entity) {
    return json{{"index", entity.index}, {"generation", entity.generation}};
}

/**
 * @brief Extract a required type token from a bound prompt value.
 *
 * Accepts a bare string or an object with a string `type` field; any other
 * shape yields false (fail-safe).
 */
bool PromptTypeValue(const json& value, std::string& out) {
    if (value.is_string()) {
        out = value.get<std::string>();
        return !out.empty();
    }
    if (value.is_object()) {
        const auto it = value.find("type");
        if (it == value.end() || !it->is_string()) return false;
        out = it->get<std::string>();
        return !out.empty();
    }
    return false;
}

/** @brief The mutable `active_type_req` on `match`, adding one if absent. */
ecs::ActiveTypeReq* EnsureActiveTypeReq(ecs::EntityStore& store,
                                        ecs::Entity match) {
    ecs::ActiveTypeReq* req = store.Get<ecs::ActiveTypeReq>(match);
    if (req != nullptr) return req;
    store.Add(match, ecs::ActiveTypeReq{});
    return store.Get<ecs::ActiveTypeReq>(match);
}

/** @brief The mutable `play_restriction` on `match`, adding one if absent. */
ecs::PlayRestriction* EnsurePlayRestriction(ecs::EntityStore& store,
                                            ecs::Entity match) {
    ecs::PlayRestriction* pipeline = store.Get<ecs::PlayRestriction>(match);
    if (pipeline != nullptr) return pipeline;
    store.Add(match, ecs::PlayRestriction{});
    return store.Get<ecs::PlayRestriction>(match);
}

/** @brief Map a parsed `allow|deny` token to the pipeline enum. */
ecs::RestrictionPhase ToPhase(const std::string& token) {
    return token == "allow" ? ecs::RestrictionPhase::kAllow
                            : ecs::RestrictionPhase::kDeny;
}

}  // namespace

OpResult OpSetActiveType(ecs::EntityStore& store, const OpArgs& args,
                         OpContext& ctx) {
    std::string type;
    const bool has_literal =
        args.GetString("type", type) && !type.empty();

    // INFO: `from_prompt` names the prompt node whose bound result is the
    //       chosen type; the Resolver binds it before this op runs.
    if (!has_literal) {
        std::string node_id;
        if (!args.GetString("from_prompt", node_id) || node_id.empty()) {
            return OpResult::Resolved();
        }
        const json* value = ctx.frame.FindPromptValue(node_id);
        if (value == nullptr || !PromptTypeValue(*value, type)) {
            return OpResult::Resolved();
        }
    }

    const std::optional<ecs::Entity> match = FindMatch(store);
    if (!match.has_value()) return OpResult::Resolved();
    ecs::ActiveTypeReq* req = EnsureActiveTypeReq(store, *match);
    if (req == nullptr) return OpResult::Resolved();
    req->type = type;

    return OpResult::Resolved(json{{"type", type}});
}

OpResult OpClearActiveType(ecs::EntityStore& store, const OpArgs& args,
                           OpContext& ctx) {
    (void)args;
    (void)ctx;
    const std::optional<ecs::Entity> match = FindMatch(store);
    if (!match.has_value()) return OpResult::Resolved();
    ecs::ActiveTypeReq* req = store.Get<ecs::ActiveTypeReq>(*match);
    if (req == nullptr || !req->type.has_value()) {
        return OpResult::Resolved();
    }
    req->type.reset();
    return OpResult::Resolved(json{{"type", nullptr}});
}

OpResult OpAddRestriction(ecs::EntityStore& store, const OpArgs& args,
                          OpContext& ctx) {
    (void)ctx;
    const json* entry_def = args.GetObject("entry_def");
    if (entry_def == nullptr) return OpResult::Resolved();

    modload::RestrictionEntry parsed;
    std::string error;
    if (!modload::ParseRestrictionEntry(*entry_def, parsed, error)) {
        // INFO: content is validated at load; a malformed runtime entry
        //       is a fail-safe no-op rather than a crash.
        return OpResult::Resolved();
    }

    const std::optional<ecs::Entity> match = FindMatch(store);
    if (!match.has_value()) return OpResult::Resolved();
    ecs::PlayRestriction* pipeline = EnsurePlayRestriction(store, *match);
    if (pipeline == nullptr) return OpResult::Resolved();

    for (const ecs::RestrictionEntry& entry : pipeline->entries) {
        if (entry.id == parsed.id) {
            return OpResult::Error(
                "add_restriction: duplicate entry id '" + parsed.id + "'");
        }
    }

    ecs::RestrictionEntry entry;
    entry.id = parsed.id;
    entry.phase = ToPhase(parsed.phase);
    entry.condition = parsed.condition;
    pipeline->entries.push_back(std::move(entry));

    return OpResult::Resolved(
        json{{"id", parsed.id}, {"phase", parsed.phase}});
}

OpResult OpRemoveRestriction(ecs::EntityStore& store, const OpArgs& args,
                             OpContext& ctx) {
    (void)ctx;
    std::string entry_id;
    if (!args.GetString("entry_id", entry_id) || entry_id.empty()) {
        return OpResult::Resolved();
    }

    const std::optional<ecs::Entity> match = FindMatch(store);
    if (!match.has_value()) return OpResult::Resolved();
    ecs::PlayRestriction* pipeline = store.Get<ecs::PlayRestriction>(*match);
    if (pipeline == nullptr) return OpResult::Resolved();

    std::vector<ecs::RestrictionEntry> kept;
    kept.reserve(pipeline->entries.size());
    std::size_t removed = 0;
    for (ecs::RestrictionEntry& entry : pipeline->entries) {
        if (entry.id == entry_id) {
            ++removed;
            continue;
        }
        kept.push_back(std::move(entry));
    }
    if (removed == 0) return OpResult::Resolved();

    pipeline->entries = std::move(kept);
    return OpResult::Resolved(
        json{{"id", entry_id}, {"removed", removed}});
}

}  // namespace match::ops::detail
