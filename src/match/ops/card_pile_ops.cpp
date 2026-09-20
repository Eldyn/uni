#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>
#include <match/resolver.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * @file card_pile_ops.cpp
 * @brief Card and pile op bodies.
 *
 * Every body is a total, bounded function over the store: it reads its
 * declared args through the fail-safe `OpArgs` getters, mutates only through
 * the helpers (`MoveCardToZone` / `DrawTop` / `SetHandOrder`) and the
 * checked component wrappers, and emits event descriptors via `MakeEvent`.
 * A missing/unbound selector, a dead entity, a malformed arg or an
 * out-of-range `IntArg` is a fail-safe `kResolved` no-op, never a crash.
 *
 * Hooks fire through `ctx.event_bus` (veto semantics belong to the bus):
 * `draw_attempt` / `draw` / `shuffle` / `pile_empty` for draws,
 * `card_left_zone` / `card_entered_zone` for single-card relocations, and
 * `visibility_granted` for reveals. `filter` on `draw_cards` is a
 * condition evaluated per
 * candidate card; an unevaluable filter rejects every candidate.
 */

namespace match::ops::detail {
namespace {

using nlohmann::json;

/** @brief Entity handle as a JSON object. */
json EntityJson(ecs::Entity entity) {
    return json{{"index", entity.index}, {"generation", entity.generation}};
}

/** @brief Wire token for a zone kind. */
std::string_view ZoneToken(ecs::ZoneKind kind) {
    switch (kind) {
        case ecs::ZoneKind::kHand:
            return "hand";
        case ecs::ZoneKind::kDrawPile:
            return "draw_pile";
        case ecs::ZoneKind::kDiscardPile:
            return "discard_pile";
        case ecs::ZoneKind::kLimbo:
            return "limbo";
    }
    return "limbo";
}

/** @brief Parse a `kZone` token; false on an unknown token. */
bool ParseZone(const std::string& token, ecs::ZoneKind& out) {
    if (token == "hand") {
        out = ecs::ZoneKind::kHand;
    } else if (token == "draw_pile") {
        out = ecs::ZoneKind::kDrawPile;
    } else if (token == "discard_pile") {
        out = ecs::ZoneKind::kDiscardPile;
    } else if (token == "limbo") {
        out = ecs::ZoneKind::kLimbo;
    } else {
        return false;
    }
    return true;
}

/** @brief Parse a `kPileRef` token; false on an unknown token. */
bool ParsePileKind(const std::string& token, ecs::PileKind& out) {
    if (token == "draw" || token == "@draw_pile") {
        out = ecs::PileKind::kDraw;
    } else if (token == "discard" || token == "@discard_pile") {
        out = ecs::PileKind::kDiscard;
    } else {
        return false;
    }
    return true;
}

/** @brief Pile wire token for hook/event payloads. */
std::string_view PileToken(ecs::PileKind kind) {
    return kind == ecs::PileKind::kDraw ? "draw" : "discard";
}

/** @brief Read a bounded `IntArg`; false when absent or out of range. */
bool BoundedInt(const OpArgs& args, std::string_view name, int64_t lo,
                int64_t hi, int64_t& out) {
    if (!args.GetInt(name, out)) return false;
    return out >= lo && out <= hi;
}

/**
 * @brief True when a frozen kind id matches a local or qualified ref.
 *
 * Mirrors the `has_card_kind` rule: a ref carrying `:` matches exactly; a
 * bare local id matches the tail of a `namespace:id` kind id.
 */
bool KindRefMatches(const std::string& kind_id, const std::string& ref) {
    if (kind_id == ref) return true;
    if (ref.find(':') != std::string::npos) return false;
    if (kind_id.size() <= ref.size()) return false;
    const std::size_t offset = kind_id.size() - ref.size();
    return kind_id[offset - 1] == ':'
        && kind_id.compare(offset, ref.size(), ref) == 0;
}

/**
 * @brief The default condition registry, built once per process.
 *
 * `draw_cards`'s `filter` is a condition; the op seam (`OpContext`) does
 * not carry the Resolver's registry, so the op evaluates filters against the
 * engine defaults registered by `RegisterDefaultConditions`. The Resolver's
 * own registry still governs `branch`/`where`/`veto`.
 */
resolver::ConditionRegistry& DefaultConditions() {
    static resolver::ConditionRegistry registry = [] {
        resolver::ConditionRegistry created;
        RegisterDefaultConditions(created);
        return created;
    }();
    return registry;
}

/**
 * @brief True when `card` passes the draw `filter`.
 *
 * `nullptr`/empty means no filter. The shorthand keys `color`,
 * `kind` and `tag` are evaluated directly against the card; any other object,
 * string or boolean is a condition evaluated with the candidate bound as
 * `@card` and `@target`. A malformed or unknown condition evaluates false, so
 * an unevaluable filter rejects the candidate (fail-safe: never draw an
 * unvetted card).
 */
bool CardMatchesFilter(ecs::EntityStore& store, ecs::Entity card,
                       const json* filter, OpContext& ctx) {
    if (filter == nullptr) return true;
    if (filter->is_boolean()) return filter->get<bool>();
    if (filter->is_string()) {
        return DefaultConditions().Evaluate(store, *filter, ctx);
    }
    if (!filter->is_object()) return false;
    if (filter->empty()) return true;

    if (filter->size() == 1) {
        const auto it = filter->begin();
        if (it.key() == "color" && it.value().is_string()) {
            const ecs::FaceSpec* face = store.Get<ecs::FaceSpec>(card);
            return face != nullptr
                && face->color == it.value().get<std::string>();
        }
        if (it.key() == "kind" && it.value().is_string()) {
            return KindRefMatches(CardKindId(store, card),
                                  it.value().get<std::string>());
        }
        if (it.key() == "tag" && it.value().is_string()) {
            return CardHasTag(CardKindId(store, card),
                              it.value().get<std::string>());
        }
    }

    // INFO: Full condition; bind the candidate so `target: "@card"` (or
    //       `"@target"`) resolves to it, then restore the prior bindings.
    ResolutionFrame& frame = ctx.frame;
    const auto card_it = frame.selectors.find("@card");
    const auto target_it = frame.selectors.find("@target");
    const bool had_card = card_it != frame.selectors.end();
    const bool had_target = target_it != frame.selectors.end();
    const std::vector<ecs::Entity> saved_card =
        had_card ? card_it->second : std::vector<ecs::Entity>();
    const std::vector<ecs::Entity> saved_target =
        had_target ? target_it->second : std::vector<ecs::Entity>();

    frame.BindSelector("@card", {card});
    frame.BindSelector("@target", {card});
    const bool matched = DefaultConditions().Evaluate(store, *filter, ctx);

    if (had_card) {
        frame.BindSelector("@card", saved_card);
    } else {
        frame.selectors.erase("@card");
    }
    if (had_target) {
        frame.BindSelector("@target", saved_target);
    } else {
        frame.selectors.erase("@target");
    }
    return matched;
}

/**
 * @brief Top-most card in `pile` passing `filter`, or nullopt.
 *
 * Piles store the top at the back, so this scans back-to-front; a non-matching
 * card is left in place.
 */
std::optional<ecs::Entity> PickCandidate(ecs::EntityStore& store,
                                         ecs::Entity pile,
                                         const json* filter,
                                         OpContext& ctx) {
    const std::vector<ecs::Entity> cards = PileOf(store, pile);
    for (auto it = cards.rbegin(); it != cards.rend(); ++it) {
        if (CardMatchesFilter(store, *it, filter, ctx)) return *it;
    }
    return std::nullopt;
}

/** @brief Dispatch one before/after hook with `data`. */
void DispatchHook(OpContext& ctx, std::string_view name, ecs::HookPhase phase,
                  const json& data) {
    ecs::HookPayload payload;
    payload.hook = ecs::HookId{std::string(name), phase};
    payload.data = data;
    if (phase == ecs::HookPhase::kBefore) {
        ctx.event_bus.DispatchBefore(payload.hook, payload);
    } else {
        ctx.event_bus.DispatchAfter(payload.hook, payload);
    }
}

/** @brief Zone ref as a payload object. */
json ZoneJson(const ecs::ZoneRef& zone) {
    json owner = zone.kind == ecs::ZoneKind::kHand ? EntityJson(zone.owner)
                                                   : json(nullptr);
    return json{{"kind", std::string(ZoneToken(zone.kind))},
                {"owner", owner}};
}

/** @brief `{card, from, to}` payload for the zone-membership hooks. */
json ZoneTransition(ecs::Entity card, const ecs::ZoneRef& from,
                    const ecs::ZoneRef& to) {
    return json{{"card", EntityJson(card)},
                {"from", ZoneJson(from)},
                {"to", ZoneJson(to)}};
}

/**
 * @brief Move `card` to `to`, firing the zone-membership hooks around it.
 *
 * @return false when the card is dead / lacks `in_zone` or the destination is
 *         rejected; the card then stays put (or lands in limbo per the op layer
 *         helper contract).
 */
bool MoveWithZoneHooks(ecs::EntityStore& store, ecs::Entity card,
                       const ecs::ZoneRef& to, OpContext& ctx) {
    const std::optional<ecs::ZoneRef> from = FindCardZone(store, card);
    if (!from.has_value()) return false;
    const json data = ZoneTransition(card, *from, to);
    DispatchHook(ctx, "card_left_zone", ecs::HookPhase::kBefore, data);
    DispatchHook(ctx, "card_entered_zone", ecs::HookPhase::kBefore, data);
    if (!MoveCardToZone(store, card, to)) return false;
    DispatchHook(ctx, "card_left_zone", ecs::HookPhase::kAfter, data);
    DispatchHook(ctx, "card_entered_zone", ecs::HookPhase::kAfter, data);
    return true;
}

/**
 * @brief Ensure `kind`'s pile has a drawable card, reshuffling when needed.
 *
 * Only the draw pile reshuffles: when empty it moves every discard card back
 * onto the draw pile, unless `before:pile_empty` (or `before:shuffle`, should
 * the bus ever make it veto-capable) vetoes. Emits a `reshuffle` descriptor.
 */
bool EnsureDrawSource(ecs::EntityStore& store, ecs::PileKind kind,
                      OpContext& ctx, std::vector<json>& events) {
    const std::optional<ecs::Entity> pile = FindPile(store, kind);
    if (!pile.has_value()) return false;
    if (!PileOf(store, *pile).empty()) return true;
    if (kind != ecs::PileKind::kDraw) return false;

    ecs::HookPayload empty;
    empty.hook = ecs::HookId{"pile_empty", ecs::HookPhase::kBefore};
    empty.data = json{{"pile", std::string(PileToken(kind))}};
    if (ctx.event_bus.DispatchBefore(empty.hook, empty).vetoed) return false;

    const std::optional<ecs::Entity> discard =
        FindPile(store, ecs::PileKind::kDiscard);
    if (!discard.has_value()) return false;
    const std::vector<ecs::Entity> cards = PileOf(store, *discard);
    if (cards.empty()) return false;

    const std::size_t discard_size = cards.size();
    const json shuffle_data = json{{"draw_size", PileOf(store, *pile).size()},
                                   {"discard_size", discard_size}};
    ecs::HookPayload shuffle;
    shuffle.hook = ecs::HookId{"shuffle", ecs::HookPhase::kBefore};
    shuffle.data = shuffle_data;
    if (ctx.event_bus.DispatchBefore(shuffle.hook, shuffle).vetoed) {
        return false;
    }

    for (ecs::Entity card : cards) {
        MoveCardToZone(store, card,
                       ecs::ZoneRef{ecs::ZoneKind::kDrawPile, ecs::Entity{}});
    }

    const std::size_t draw_size = PileOf(store, *pile).size();
    const json settled = json{{"draw_size", draw_size}, {"discard_size", 0}};
    ecs::HookPayload shuffled;
    shuffled.hook = ecs::HookId{"shuffle", ecs::HookPhase::kAfter};
    shuffled.data = settled;
    ctx.event_bus.DispatchAfter(shuffled.hook, shuffled);
    events.push_back(MakeEvent("reshuffle", settled));
    return !PileOf(store, *pile).empty();
}

/**
 * @brief Open a `choose_card` prompt over `candidates`.
 *
 * Grants each candidate identity visibility to the chooser (the "revealed
 * subset"), emits `visibility_granted`, fills `ctx.input_request` and returns
 * the envelope. The move itself is driven by the response route; ops are
 * single-shot and `from_prompt` binding belongs to the Resolver.
 */
OpResult OpenChooseCard(ecs::EntityStore& store, ecs::Entity from,
                        ecs::Entity chooser,
                        const std::vector<ecs::Entity>& candidates,
                        OpContext& ctx) {
    json options = json::array();
    for (ecs::Entity card : candidates) {
        ecs::VisibilityGrant* grant = store.Get<ecs::VisibilityGrant>(card);
        if (grant == nullptr) {
            store.Add(card, ecs::VisibilityGrant{});
            grant = store.Get<ecs::VisibilityGrant>(card);
        }
        if (grant != nullptr) {
            ecs::VisibilityGrant::Entry entry;
            entry.viewer = chooser;
            entry.aspect_mask =
                static_cast<uint32_t>(ecs::Aspect::kIdentity);
            entry.expires_ms = 0;
            grant->entries.push_back(entry);
        }
        options.push_back(EntityJson(card));
    }

    const json payload = json{{"from", EntityJson(from)}, {"options", options}};
    ctx.input_request = InputRequest{};
    ctx.input_request->kind = "choose_card";
    ctx.input_request->target = chooser;
    ctx.input_request->payload = payload;

    OpResult result = OpResult::NeedsInput(
        json{{"kind", "choose_card"},
             {"payload", payload},
             {"timeout_ms", 0},
             {"default", nullptr}});
    result.events.push_back(MakeEvent(
        "visibility_granted",
        json{{"viewer", EntityJson(chooser)},
             {"target", EntityJson(from)},
             {"aspects", json::array({"identity"})},
             {"count", candidates.size()}}));
    return result;
}

}  // namespace

OpResult OpDrawCards(ecs::EntityStore& store, const OpArgs& args,
                     OpContext& ctx) {
    // INFO: a set selector (`@all_players` / `@others`) draws for every bound
    //       player, in the order the Resolver bound them.
    const std::vector<ecs::Entity> targets = args.EntitiesOrEmpty("target");
    if (targets.empty()) return OpResult::Resolved();

    int64_t n = 0;
    if (!BoundedInt(args, "n", 0, 1000, n)) return OpResult::Resolved();

    std::string from_token = "draw";
    if (args.Has("from") && !args.GetString("from", from_token)) {
        return OpResult::Resolved();
    }
    ecs::PileKind source_kind = ecs::PileKind::kDraw;
    if (!ParsePileKind(from_token, source_kind)) return OpResult::Resolved();

    const json* filter = args.Find("filter");
    if (filter != nullptr && !filter->is_object() && !filter->is_string()
        && !filter->is_boolean()) {
        // WARN: a non-condition filter is unevaluable; fail safe with no draw.
        return OpResult::Resolved();
    }

    const std::optional<ecs::Entity> source = FindPile(store, source_kind);
    if (!source.has_value()) return OpResult::Resolved();

    std::vector<json> events;
    int64_t total_drawn = 0;
    for (ecs::Entity target : targets) {
        if (!store.IsAlive(target) || !store.Has<ecs::Hand>(target)) continue;

        int64_t drawn = 0;
        for (int64_t i = 0; i < n; ++i) {
            if (!EnsureDrawSource(store, source_kind, ctx, events)) break;
            const std::optional<ecs::Entity> candidate =
                PickCandidate(store, *source, filter, ctx);
            if (!candidate.has_value()) break;

            ecs::HookPayload attempt;
            attempt.hook =
                ecs::HookId{"draw_attempt", ecs::HookPhase::kBefore};
            attempt.data =
                json{{"player", EntityJson(target)},
                     {"source", std::string(PileToken(source_kind))}};
            if (ctx.event_bus.DispatchBefore(attempt.hook, attempt).vetoed) {
                break;
            }

            const json draw_data = json{{"card", EntityJson(*candidate)},
                                        {"player", EntityJson(target)}};
            ecs::HookPayload draw;
            draw.hook = ecs::HookId{"draw", ecs::HookPhase::kBefore};
            draw.data = draw_data;
            const bool draw_vetoed =
                ctx.event_bus.DispatchBefore(draw.hook, draw).vetoed;

            if (!draw_vetoed) {
                if (!MoveCardToZone(
                        store, *candidate,
                        ecs::ZoneRef{ecs::ZoneKind::kHand, target})) {
                    break;
                }
                ++drawn;
            }

            DispatchHook(ctx, "draw", ecs::HookPhase::kAfter, draw_data);
            DispatchHook(ctx, "draw_attempt", ecs::HookPhase::kAfter,
                         attempt.data);
            if (draw_vetoed) break;
        }

        events.push_back(MakeEvent(
            "cards_drawn",
            json{{"player", EntityJson(target)}, {"count", drawn},
                 {"source", std::string(PileToken(source_kind))}}));
        total_drawn += drawn;
    }

    OpResult result = OpResult::Resolved(json{{"drawn", total_drawn}});
    result.events = std::move(events);
    return result;
}

OpResult OpMoveCard(ecs::EntityStore& store, const OpArgs& args,
                    OpContext& ctx) {
    const std::optional<ecs::Entity> card = args.FirstEntity("card");
    if (!card.has_value() || !store.IsAlive(*card)) return OpResult::Resolved();
    if (!FindCardZone(store, *card).has_value()) return OpResult::Resolved();

    std::string token;
    if (!args.GetString("to_zone", token)) return OpResult::Resolved();
    ecs::ZoneKind zone_kind = ecs::ZoneKind::kLimbo;
    if (!ParseZone(token, zone_kind)) return OpResult::Resolved();

    ecs::ZoneRef to;
    to.kind = zone_kind;
    if (zone_kind == ecs::ZoneKind::kHand) {
        // INFO: keep a hand reorder on its owner; otherwise relocate to the
        //       acting player (`@self`, else the turn owner).
        const std::optional<ecs::ZoneRef> current = FindCardZone(store, *card);
        if (current.has_value() && current->kind == ecs::ZoneKind::kHand
            && store.IsAlive(current->owner)) {
            to.owner = current->owner;
        } else if (const std::optional<ecs::Entity> self =
                       ctx.frame.FirstSelector("@self");
                   self.has_value()) {
            to.owner = *self;
        } else if (const std::optional<ecs::Entity> current_player =
                       FindCurrentPlayer(store);
                   current_player.has_value()) {
            to.owner = *current_player;
        } else {
            return OpResult::Resolved();
        }
        if (!store.Has<ecs::Hand>(to.owner)) return OpResult::Resolved();
    }

    if (!MoveWithZoneHooks(store, *card, to, ctx)) return OpResult::Resolved();
    return OpResult::Resolved(json{{"card", EntityJson(*card)}});
}

OpResult OpTransferCard(ecs::EntityStore& store, const OpArgs& args,
                        OpContext& ctx) {
    const std::optional<ecs::Entity> from = args.FirstEntity("from_player");
    const std::optional<ecs::Entity> to = args.FirstEntity("to_player");
    if (!from.has_value() || !to.has_value()) return OpResult::Resolved();
    if (!store.IsAlive(*from) || !store.IsAlive(*to)) {
        return OpResult::Resolved();
    }
    if (!store.Has<ecs::Hand>(*from) || !store.Has<ecs::Hand>(*to)) {
        return OpResult::Resolved();
    }

    std::string selector;
    if (!args.GetString("selector", selector)) return OpResult::Resolved();

    const std::vector<ecs::Entity> candidates = HandOf(store, *from);
    if (candidates.empty()) return OpResult::Resolved();

    if (selector == "chosen") {
        return OpenChooseCard(store, *from, *to, candidates, ctx);
    }

    std::vector<ecs::Entity> matches;
    if (selector == "random") {
        // INFO: Draw a uniform index from the same splitmix64 stream the
        //       `roll` op uses, so the steal is deterministic given the same
        //       RngState and varies as the counter advances.
        const std::optional<RngDraw> rng = AdvanceRng(store);
        if (!rng.has_value()) return OpResult::Resolved();
        uint64_t state = rng->state;
        const uint64_t draw = SplitMix64(state);
        matches.push_back(
            candidates[static_cast<std::size_t>(draw % candidates.size())]);
    } else if (selector.rfind("tag:", 0) == 0) {
        const std::string tag = selector.substr(4);
        for (ecs::Entity card : candidates) {
            if (CardHasTag(CardKindId(store, card), tag)) {
                matches.push_back(card);
            }
        }
    } else if (selector.rfind("kind:", 0) == 0) {
        const std::string kind = selector.substr(5);
        for (ecs::Entity card : candidates) {
            if (KindRefMatches(CardKindId(store, card), kind)) {
                matches.push_back(card);
            }
        }
    } else {
        return OpResult::Resolved();
    }
    if (matches.empty()) return OpResult::Resolved();

    const ecs::Entity picked = matches.front();
    if (!MoveWithZoneHooks(store, picked,
                           ecs::ZoneRef{ecs::ZoneKind::kHand, *to}, ctx)) {
        return OpResult::Resolved();
    }
    return OpResult::Resolved(json{{"card", EntityJson(picked)}});
}

OpResult OpPassHands(ecs::EntityStore& store, const OpArgs& args,
                     OpContext& ctx) {
    (void)ctx;
    std::string direction;
    if (!args.GetString("direction", direction)) return OpResult::Resolved();
    int step = 0;
    if (direction == "forward") {
        step = 1;
    } else if (direction == "backward") {
        step = -1;
    } else {
        return OpResult::Resolved();
    }

    std::vector<ecs::Entity> players;
    for (ecs::Entity player : PlayersBySeat(store)) {
        if (store.Has<ecs::Hand>(player)) players.push_back(player);
    }
    if (players.size() < 2) {
        return OpResult::Resolved(json{{"count", players.size()}});
    }

    std::vector<std::vector<ecs::Entity>> hands;
    hands.reserve(players.size());
    for (ecs::Entity player : players) hands.push_back(HandOf(store, player));

    const int count = static_cast<int>(players.size());
    for (int i = 0; i < count; ++i) {
        const int dst = ((i + step) % count + count) % count;
        SetHandOrder(store, players[static_cast<std::size_t>(dst)],
                     hands[static_cast<std::size_t>(i)]);
    }
    return OpResult::Resolved(json{{"count", count}});
}

OpResult OpSwapHands(ecs::EntityStore& store, const OpArgs& args,
                     OpContext& ctx) {
    (void)ctx;
    const std::optional<ecs::Entity> a = args.FirstEntity("a");
    const std::optional<ecs::Entity> b = args.FirstEntity("b");
    if (!a.has_value() || !b.has_value()) return OpResult::Resolved();
    if (!store.IsAlive(*a) || !store.IsAlive(*b)) return OpResult::Resolved();
    if (*a == *b) return OpResult::Resolved();
    if (!store.Has<ecs::Hand>(*a) || !store.Has<ecs::Hand>(*b)) {
        return OpResult::Resolved();
    }

    const std::vector<ecs::Entity> hand_a = HandOf(store, *a);
    const std::vector<ecs::Entity> hand_b = HandOf(store, *b);
    SetHandOrder(store, *a, hand_b);
    SetHandOrder(store, *b, hand_a);
    return OpResult::Resolved(
        json{{"a", EntityJson(*a)}, {"b", EntityJson(*b)}});
}

OpResult OpRedistributeHands(ecs::EntityStore& store, const OpArgs& args,
                             OpContext& ctx) {
    (void)ctx;
    std::string mode;
    if (!args.GetString("mode", mode)) return OpResult::Resolved();
    if (mode != "even") return OpResult::Resolved();

    std::vector<ecs::Entity> players;
    for (ecs::Entity player : PlayersBySeat(store)) {
        if (store.Has<ecs::Hand>(player)) players.push_back(player);
    }
    if (players.empty()) return OpResult::Resolved();

    std::vector<ecs::Entity> pool;
    for (ecs::Entity player : players) {
        const std::vector<ecs::Entity> hand = HandOf(store, player);
        pool.insert(pool.end(), hand.begin(), hand.end());
    }

    const std::size_t total = pool.size();
    const std::size_t count = players.size();
    const std::size_t base = total / count;
    const std::size_t remainder = total % count;
    std::size_t cursor = 0;
    for (std::size_t i = 0; i < count; ++i) {
        // INFO: deterministic deal; the first `remainder` seats (seat order)
        //       take one extra card.
        const std::size_t take = base + (i < remainder ? 1 : 0);
        const std::vector<ecs::Entity> deal(pool.begin() + cursor,
                                            pool.begin() + cursor + take);
        cursor += take;
        SetHandOrder(store, players[i], deal);
    }
    return OpResult::Resolved(
        json{{"total", total}, {"players", count}});
}

OpResult OpMaterializeCard(ecs::EntityStore& store, const OpArgs& args,
                           OpContext& ctx) {
    const std::optional<ecs::Entity> target =
        args.FirstEntity("target_player");
    if (!target.has_value() || !store.IsAlive(*target)) {
        return OpResult::Resolved();
    }
    std::string kind;
    if (!args.GetString("kind", kind) || kind.empty()) {
        return OpResult::Resolved();
    }

    std::string zone_token = "hand";
    if (args.Has("zone") && !args.GetString("zone", zone_token)) {
        return OpResult::Resolved();
    }
    ecs::ZoneKind zone_kind = ecs::ZoneKind::kHand;
    if (!ParseZone(zone_token, zone_kind)) return OpResult::Resolved();
    if (zone_kind == ecs::ZoneKind::kHand && !store.Has<ecs::Hand>(*target)) {
        return OpResult::Resolved();
    }

    const ecs::Entity card = store.Create();
    ecs::CardIdentity identity;
    identity.kind_id = kind;
    store.Add(card, identity);
    store.Add(card, ecs::InZone{
                        ecs::ZoneRef{ecs::ZoneKind::kLimbo, ecs::Entity{}}, 0});
    store.Add(card, ecs::FaceSpec{});

    ecs::ZoneRef to;
    to.kind = zone_kind;
    if (zone_kind == ecs::ZoneKind::kHand) to.owner = *target;
    if (!MoveWithZoneHooks(store, card, to, ctx)) {
        store.Destroy(card);
        return OpResult::Resolved();
    }
    return OpResult::Resolved(json{{"card", EntityJson(card)}});
}

OpResult OpRemoveCard(ecs::EntityStore& store, const OpArgs& args,
                      OpContext& ctx) {
    const std::optional<ecs::Entity> card = args.FirstEntity("card");
    if (!card.has_value() || !store.IsAlive(*card)) return OpResult::Resolved();
    if (!FindCardZone(store, *card).has_value()) return OpResult::Resolved();

    if (!MoveWithZoneHooks(
            store, *card, ecs::ZoneRef{ecs::ZoneKind::kLimbo, ecs::Entity{}},
            ctx)) {
        return OpResult::Resolved();
    }
    return OpResult::Resolved(json{{"card", EntityJson(*card)}});
}

OpResult OpReplaceCard(ecs::EntityStore& store, const OpArgs& args,
                       OpContext& ctx) {
    (void)ctx;
    const std::optional<ecs::Entity> card = args.FirstEntity("card");
    if (!card.has_value() || !store.IsAlive(*card)) return OpResult::Resolved();
    std::string kind;
    if (!args.GetString("kind", kind) || kind.empty()) {
        return OpResult::Resolved();
    }
    ecs::CardIdentity* identity = store.Get<ecs::CardIdentity>(*card);
    if (identity == nullptr) return OpResult::Resolved();
    identity->kind_id = kind;
    return OpResult::Resolved(
        json{{"card", EntityJson(*card)}, {"kind", kind}});
}

OpResult OpPeekPile(ecs::EntityStore& store, const OpArgs& args,
                    OpContext& ctx) {
    (void)ctx;
    const std::optional<ecs::Entity> viewer = args.FirstEntity("viewer");
    if (!viewer.has_value() || !store.IsAlive(*viewer)) {
        return OpResult::Resolved();
    }

    std::string pile_token;
    if (!args.GetString("pile", pile_token)) return OpResult::Resolved();
    ecs::PileKind kind = ecs::PileKind::kDraw;
    if (!ParsePileKind(pile_token, kind)) return OpResult::Resolved();

    int64_t n = 0;
    if (!BoundedInt(args, "n", 0, 1000, n)) return OpResult::Resolved();

    const std::optional<ecs::Entity> pile = FindPile(store, kind);
    if (!pile.has_value()) return OpResult::Resolved();

    const std::vector<ecs::Entity> cards = PileOf(store, *pile);
    const std::size_t reveal =
        std::min(static_cast<std::size_t>(n), cards.size());
    if (reveal == 0) return OpResult::Resolved(json{{"revealed", 0}});

    json revealed = json::array();
    for (std::size_t i = 0; i < reveal; ++i) {
        // INFO: top-n means the back `reveal` entries, top first.
        const ecs::Entity card = cards[cards.size() - 1 - i];
        ecs::VisibilityGrant* grant = store.Get<ecs::VisibilityGrant>(card);
        if (grant == nullptr) {
            store.Add(card, ecs::VisibilityGrant{});
            grant = store.Get<ecs::VisibilityGrant>(card);
        }
        if (grant != nullptr) {
            ecs::VisibilityGrant::Entry entry;
            entry.viewer = *viewer;
            entry.aspect_mask =
                static_cast<uint32_t>(ecs::Aspect::kIdentity);
            entry.expires_ms = 0;
            grant->entries.push_back(entry);
        }
        revealed.push_back(EntityJson(card));
    }

    OpResult result = OpResult::Resolved(json{{"revealed", reveal}});
    result.events.push_back(MakeEvent(
        "visibility_granted",
        json{{"viewer", EntityJson(*viewer)},
             {"target", EntityJson(*pile)},
             {"aspects", json::array({"identity"})},
             {"count", reveal},
             {"cards", revealed}}));
    return result;
}

}  // namespace match::ops::detail
