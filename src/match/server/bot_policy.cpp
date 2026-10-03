#include <match/server/bot_policy.hpp>

#include <match/ecs/compact_card.hpp>
#include <match/engine/match_instance.hpp>
#include <match/server/match_session.hpp>
#include <match/view/event_sink.hpp>
#include <match/view/view_builder.hpp>
#include <match/view/view_util.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

/**
 * @file bot_policy.cpp
 * @brief Heuristic bot decisions + the `MatchSession` driver glue.
 */

namespace match::server {
namespace {

using nlohmann::json;

/** @brief Colours the phase-1 `choose_color` enum is expected to carry. */
constexpr char kFallbackColor[] = "red";

/** @brief FNV-1a over a username, for the deterministic window roll. */
uint64_t HashName(const std::string& name) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (char c : name) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= 0x100000001B3ull;
    }
    return hash;
}

/** @brief splitmix64 finalizer: a cheap deterministic mix. */
uint64_t Mix64(uint64_t value) {
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31);
}

/**
 * @brief True when the bot rolls its 20% window response.
 *
 * Derived from `(seed, window_id, username)` so the roll is idempotent: a
 * repeated decision for the same window never flips. Deterministic across
 * replays.
 */
bool WindowResponds(uint64_t seed, uint64_t window_id,
                    const std::string& username) {
    const uint64_t mixed =
        Mix64(seed ^ Mix64(window_id) ^ HashName(username));
    return (mixed % 100ull) < 20ull;
}

/** @brief True when the schema declares `value` inside its `enum`. */
bool EnumContains(const json& schema, const std::string& value) {
    if (!schema.is_object() || !schema.contains("enum")) return false;
    const json& options = schema["enum"];
    if (!options.is_array()) return false;
    for (const json& option : options) {
        if (option.is_string() && option.get<std::string>() == value) {
            return true;
        }
    }
    return false;
}

/** @brief First `enum` member, or empty when the schema has no enum. */
std::string FirstEnumMember(const json& schema) {
    if (!schema.is_object() || !schema.contains("enum")) return {};
    const json& options = schema["enum"];
    if (!options.is_array() || options.empty()) return {};
    if (options.front().is_string()) return options.front().get<std::string>();
    return {};
}

/**
 * @brief The most common non-wild colour in `hand`.
 *
 * Wild (`white`) cards carry no chosen colour and are skipped. When `schema`
 * declares a colour enum, only enum colours are considered; an empty result
 * falls back to the schema's first member, else `red`. Ties keep the
 * first-seen colour (hand order).
 */
std::string PickMostCommonColor(const std::vector<BotHandCard>& hand,
                                const json& schema) {
    const bool has_enum =
        schema.is_object() && schema.contains("enum")
        && schema["enum"].is_array() && !schema["enum"].empty();

    std::vector<std::string> order;
    std::map<std::string, int> counts;
    for (const BotHandCard& card : hand) {
        if (card.color.empty() || card.color == "white") continue;
        if (has_enum && !EnumContains(schema, card.color)) continue;
        if (counts.find(card.color) == counts.end()) {
            order.push_back(card.color);
        }
        counts[card.color] += 1;
    }

    std::string best;
    int best_count = 0;
    for (const std::string& color : order) {
        if (counts[color] > best_count) {
            best = color;
            best_count = counts[color];
        }
    }
    if (!best.empty()) return best;

    const std::string fallback = FirstEnumMember(schema);
    return fallback.empty() ? std::string(kFallbackColor) : fallback;
}

/**
 * @brief The other seat holding the fewest cards.
 *
 * The bot never targets itself; when it is the only seat it returns its own
 * username. Ties keep the earliest seat (registry order).
 */
std::string PickFewestCardsPlayer(const std::vector<BotPlayerRow>& players,
                                  const std::string& self,
                                  const json& payload) {
    std::vector<std::string> offered;
    if (payload.is_object() && payload.contains("options")
        && payload["options"].is_array()) {
        for (const json& option : payload["options"]) {
            if (option.is_string()) offered.push_back(option.get<std::string>());
        }
    }
    const bool restrict_to_offered = !offered.empty();

    std::string best = self;
    int best_count = 0;
    bool found = false;
    for (const BotPlayerRow& row : players) {
        if (row.username.empty() || row.username == self) continue;
        if (restrict_to_offered
            && std::find(offered.begin(), offered.end(), row.username)
                   == offered.end()) {
            continue;
        }
        if (!found || row.card_count < best_count) {
            best = row.username;
            best_count = row.card_count;
            found = true;
        }
    }
    return best;
}

/** @brief First element of the prompt payload's `options` array, else null. */
json FirstOption(const json& payload) {
    if (!payload.is_object() || !payload.contains("options")) return nullptr;
    const json& options = payload["options"];
    if (!options.is_array() || options.empty()) return nullptr;
    return options.front();
}

/**
 * @brief Read a numeric bound from the payload, its `default`, or the schema.
 *
 * Payload keys are `min` / `max`; schema keys are `minimum` / `maximum`.
 * Returns false when no numeric bound is present.
 */
bool FindBound(const json& payload, const json& schema, const char* key,
               const char* schema_key, double& out) {
    if (payload.is_object() && payload.contains(key)
        && payload[key].is_number()) {
        out = payload[key].get<double>();
        return true;
    }
    if (payload.is_object() && payload.contains("default")
        && payload["default"].is_object()
        && payload["default"].contains(key)
        && payload["default"][key].is_number()) {
        out = payload["default"][key].get<double>();
        return true;
    }
    if (schema.is_object() && schema.contains(schema_key)
        && schema[schema_key].is_number()) {
        out = schema[schema_key].get<double>();
        return true;
    }
    return false;
}

/** @brief The middle of the declared range. */
json MiddleValue(const json& payload, const json& schema) {
    double low = 0.0;
    double high = 0.0;
    const bool has_low = FindBound(payload, schema, "min", "minimum", low);
    const bool has_high = FindBound(payload, schema, "max", "maximum", high);
    if (has_low && has_high) {
        const double middle = (low + high) / 2.0;
        if (middle == static_cast<double>(static_cast<int64_t>(middle))) {
            return static_cast<int64_t>(middle);
        }
        return middle;
    }
    if (has_low) return low;
    if (has_high) return high;
    return 0;
}

/** @brief The prompt payload's `default` when present and non-null. */
json PayloadDefault(const json& payload) {
    if (!payload.is_object() || !payload.contains("default")) return nullptr;
    if (payload["default"].is_null()) return nullptr;
    return payload["default"];
}

/**
 * @brief First value the schema accepts.
 *
 * An absent/unknown schema is permissive and yields JSON null (any value is
 * accepted by `MatchSession`). Supports the same subset the session validates:
 * `enum`, `type` in string/boolean/integer/number/object/array, and numeric
 * `minimum`.
 */
json FirstSchemaValidValue(const json& schema) {
    if (!schema.is_object()) return nullptr;
    if (schema.contains("enum") && schema["enum"].is_array()
        && !schema["enum"].empty()) {
        return schema["enum"].front();
    }
    const std::string type = schema.value("type", std::string());
    if (type == "boolean") return false;
    if (type == "string") return std::string();
    if (type == "integer" || type == "number") {
        if (schema.contains("minimum") && schema["minimum"].is_number()) {
            return schema["minimum"];
        }
        return type == "integer" ? json(0) : json(0.0);
    }
    if (type == "array") return json::array();
    if (type == "object") return json::object();
    return nullptr;
}

/** @brief The `yes_no` heuristic: accept unless the payload says otherwise. */
json YesNoAnswer(const json& payload) {
    if (payload.is_object() && payload.contains("default")
        && payload["default"].is_boolean()) {
        return payload["default"];
    }
    return true;
}

/**
 * @brief Preference score for a window response candidate.
 *
 * Draw penalties punish hardest, then skip/reverse, then everything else.
 * `wild_draw4` is the strongest stack; `draw2` next.
 */
int WindowCandidateScore(const BotHandCard& card) {
    if (card.value == "jolly_draw4") return 4;
    if (card.value == "draw2") return 3;
    if (card.value == "skip" || card.value == "reverse") return 2;
    return 1;
}

}  // namespace

HeuristicBotPolicy::HeuristicBotPolicy(
    uint64_t seed, std::map<std::string, nlohmann::json> extra_schemas)
    : seed_(seed), schemas_(std::move(extra_schemas)) {
    // INFO: phase-1 built-ins, mirroring MatchSession's own table. `emplace`
    //       keeps an injected mod schema for the same kind (it wins).
    schemas_.emplace(
        "choose_color",
        json{{"type", "string"}, {"enum", {"red", "blue", "green", "yellow"}}});
    schemas_.emplace("choose_yes_no", json{{"type", "boolean"}});
}

const json* HeuristicBotPolicy::SchemaFor(const std::string& kind) const {
    const auto it = schemas_.find(kind);
    return it == schemas_.end() ? nullptr : &it->second;
}

std::optional<uint32_t> HeuristicBotPolicy::ChoosePlay(const BotView& view) {
    // INFO: prefer a non-wild legal card so the turn resolves without a colour
    //       prompt; fall back to any legal card (a wild is always playable).
    std::optional<uint32_t> wild;
    for (const BotHandCard& card : view.hand) {
        if (!card.can_play) continue;
        if (card.color != "white") return card.bits;
        if (!wild.has_value()) wild = card.bits;
    }
    return wild;
}

json HeuristicBotPolicy::ChoosePrompt(const BotView& view) {
    const std::string& kind = view.prompt_kind;
    const json* schema = SchemaFor(kind);
    const json permissive = json::object();

    if (kind == "choose_color") {
        return PickMostCommonColor(view.hand,
                                   schema == nullptr ? permissive : *schema);
    }
    if (kind == "choose_player") {
        return PickFewestCardsPlayer(view.players, view.username,
                                     view.prompt_payload);
    }
    if (kind == "choose_card") {
        return FirstOption(view.prompt_payload);
    }
    if (kind == "choose_value") {
        return MiddleValue(view.prompt_payload,
                           schema == nullptr ? permissive : *schema);
    }
    if (kind == "yes_no" || kind == "choose_yes_no") {
        return YesNoAnswer(view.prompt_payload);
    }

    // INFO: unknown kind - the prompt default when the payload carries one,
    //       else the first value the resolved schema accepts.
    const json fallback = PayloadDefault(view.prompt_payload);
    if (!fallback.is_null()) return fallback;
    if (schema == nullptr) return nullptr;
    return FirstSchemaValidValue(*schema);
}

std::vector<uint32_t> HeuristicBotPolicy::ChooseWindowResponses(
    const BotView& view) {
    if (!view.window_open || !view.is_responder || view.window_responded) {
        return {};
    }
    if (!WindowResponds(seed_, view.window_id, view.username)) return {};

    std::vector<const BotHandCard*> ordered;
    ordered.reserve(view.hand.size());
    for (const BotHandCard& card : view.hand) {
        if (card.bits != 0) ordered.push_back(&card);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const BotHandCard* a, const BotHandCard* b) {
                         return WindowCandidateScore(*a)
                                > WindowCandidateScore(*b);
                     });

    std::vector<uint32_t> candidates;
    candidates.reserve(ordered.size());
    for (const BotHandCard* card : ordered) {
        candidates.push_back(card->bits);
    }
    return candidates;
}

BotView BuildBotView(const MatchSession& session, const std::string& username) {
    BotView view;
    view.username = username;

    match::view::EventSink sink;
    const json snapshot = session.View().BuildSnapshot(
        match::view::Viewer::Player(username), sink);
    const json state =
        snapshot.value("match_state", json::object());
    view.current_player = state.value("current_player", std::string());

    for (const json& row : state.value("players", json::array())) {
        BotPlayerRow player;
        player.username = row.value("username", std::string());
        player.card_count = row.value("card_count", 0);
        view.players.push_back(player);
        if (player.username != username) continue;
        for (const json& entry : row.value("hand", json::array())) {
            BotHandCard card;
            card.bits = entry.value("card", 0u);
            card.kind = entry.value("kind", std::string());
            card.color = entry.value("color", std::string());
            card.value = entry.value("value", std::string());
            card.can_play = entry.value("can_play", false);
            view.hand.push_back(card);
        }
    }

    const std::optional<json> pending = session.Engine().PendingInput();
    if (pending.has_value() && pending->is_object()) {
        const std::string target = match::view::ResolvePlayer(
            session.Engine(), pending->value("target", json()));
        if (target == username) {
            view.prompt_pending = true;
            view.prompt_kind = pending->value("kind", std::string());
            view.prompt_payload =
                pending->value("payload", json::object());
        }
    }

    if (session.Engine().WindowOpen()) {
        view.window_open = true;
        const json window = session.Engine().ExportWindow();
        view.window_id = window.value("id", 0u);
        for (const json& responder :
             window.value("responders", json::array())) {
            if (responder.is_string()
                && responder.get<std::string>() == username) {
                view.is_responder = true;
            }
        }
        for (const json& response : window.value("responses", json::array())) {
            if (response.value("player", std::string()) == username) {
                view.window_responded = true;
            }
        }
    }

    // INFO: a playable voluntary draw parks a play/keep choice on its owner;
    //       expose the drawn card only to that owner so the bot resolves the
    //       hold instead of leaving the turn parked.
    const std::optional<match::engine::PendingPlayDrawn>& drawn_choice =
        session.Engine().PendingPlayDrawnState();
    if (drawn_choice.has_value()) {
        const std::optional<ecs::Entity> owner =
            session.Engine().FindPlayer(username);
        if (owner.has_value() && *owner == drawn_choice->player) {
            const std::optional<ecs::CompactCardV2> id =
                session.Engine().Registries().CardId(drawn_choice->card);
            if (id.has_value()) view.drawn_card = id->bits;
        }
    }
    return view;
}

bool BotStep(MatchSession& session, IBotPolicy& policy,
             const std::string& username) {
    match::engine::MatchInstance& engine = session.Engine();
    if (engine.IsMatchOver()) return false;

    const BotView view = BuildBotView(session, username);

    if (view.prompt_pending) {
        const json value = policy.ChoosePrompt(view);
        return session.SubmitInput(username, view.prompt_kind, value);
    }

    if (view.window_open && view.is_responder && !view.window_responded) {
        const std::vector<uint32_t> candidates =
            policy.ChooseWindowResponses(view);
        for (uint32_t bits : candidates) {
            if (session.RespondWindow(username, bits)) return true;
        }
        return session.PassWindow(username);
    }

    // INFO: a held voluntary draw owns the turn. Decide from a view whose hand
    //       is only the drawn card, so a policy can only accept or decline it;
    //       declining (or a refused play) keeps the card and passes the turn.
    if (view.drawn_card.has_value()) {
        BotView drawn_view = view;
        drawn_view.hand.clear();
        BotHandCard drawn;
        drawn.bits = *view.drawn_card;
        drawn.can_play = true;
        drawn_view.hand.push_back(drawn);
        const std::optional<uint32_t> play = policy.ChoosePlay(drawn_view);
        if (play.has_value() && session.PlayCard(username, *play)) return true;
        return session.KeepDrawn(username);
    }

    if (view.current_player == username) {
        const std::optional<uint32_t> play = policy.ChoosePlay(view);
        if (play.has_value() && session.PlayCard(username, *play)) {
            return true;
        }
        return session.DrawCard(username);
    }
    return false;
}

std::map<std::string, nlohmann::json> PromptSchemasFor(
    const std::vector<match::modload::LoadedMod>& mods) {
    std::map<std::string, json> schemas;
    for (const match::modload::LoadedMod& mod : mods) {
        if (!mod.manifest.prompts.is_array()) continue;
        for (const json& decl : mod.manifest.prompts) {
            if (!decl.is_object()) continue;
            const std::string kind = decl.value("kind", std::string());
            if (kind.empty() || !decl.contains("response_schema")) continue;
            if (!decl["response_schema"].is_object()) continue;
            // INFO: Built-ins are engine-owned; ignore a mod
            //       declaration for one so the bot answers with the same
            //       schema `MatchSession::SubmitInput` validates against.
            if (IsBuiltinPromptKind(kind)) continue;
            schemas[kind] = decl["response_schema"];
        }
    }
    return schemas;
}

}  // namespace match::server
