#include "match/engine/match_assembler.hpp"

#include "match/content_wiring.hpp"
#include "match/engine/mutation_compiler.hpp"
#include "match/ops/op_helpers.hpp"
#include "match/rng.hpp"

#include <logger.hpp>

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

/**
 * @file match_assembler.cpp
 * @brief Assembly implementation.
 */

namespace match::engine {
namespace {

using nlohmann::json;

/** @brief Copy a modload face kind into the `ecs::FaceKind` enum. */
ecs::FaceKind ToEcsFaceKind(modload::FaceKind kind) {
    switch (kind) {
        case modload::FaceKind::kText:
            return ecs::FaceKind::kText;
        case modload::FaceKind::kImage:
            return ecs::FaceKind::kImage;
        case modload::FaceKind::kEmoji:
            return ecs::FaceKind::kEmoji;
        case modload::FaceKind::kBlank:
            return ecs::FaceKind::kBlank;
    }
    return ecs::FaceKind::kBlank;
}

/** @brief Convert a modload face spec into the component. */
ecs::FaceSpec ToEcsFace(const modload::FaceSpec& face) {
    ecs::FaceSpec out;
    out.kind = ToEcsFaceKind(face.kind);
    out.color = face.color.value_or("");
    out.label = face.label.value_or("");
    out.art = face.art.value_or("");
    out.art_mode = face.art_mode;
    out.art_fit = face.art_fit;
    out.keep = face.keep;
    out.art_version = face.art_version;
    return out;
}

/** @brief Convert a modload window declaration into the component. */
ecs::WindowSpec ToEcsWindow(const modload::WindowSpec& window) {
    ecs::WindowSpec out;
    out.responders = window.responders;
    out.respond_with = window.respond_with.value_or(json::object());
    out.duration = window.duration;
    out.on_response = window.on_response;
    out.default_route = window.default_route;
    out.raw = window.raw;
    return out;
}

/**
 * @brief Convert a modload `auto_trigger` declaration into the component.
 *
 * The graph is stored as the object the engine's `AutoPlayCard` expects: a
 * graph carrying `nodes` is kept verbatim, otherwise its node list is wrapped
 * in `{"nodes": ...}` (mirrors `ToEcsBehavior`).
 */
ecs::AutoTrigger ToEcsAutoTrigger(const modload::AutoTriggerDef& def) {
    ecs::AutoTrigger out;
    out.condition = def.condition;
    if (def.graph.is_object() && def.graph.contains("nodes")) {
        out.graph = def.graph;
    } else {
        out.graph = json{{"nodes", def.graph}};
    }
    out.must_apply = def.must_apply;
    return out;
}

/**
 * @brief Build the `card_behavior` trigger map from a card's defs.
 *
 * The resolver consumes `BehaviorGraph`, so the verbatim graph object is
 * stored; a graph with no raw object is wrapped from its node list. When the
 * card kind is a mutation target, each trigger graph is compiled first, so
 * the component carries the effective graph.
 */
ecs::CardBehavior ToEcsBehavior(
    const modload::CardDef& card,
    const std::map<std::string, std::vector<const modload::MutationDef*>>&
        mutations_by_target,
    const MutationCompileOptions& compile_options) {
    ecs::CardBehavior out;
    const auto target = mutations_by_target.find(card.kind_id);
    const bool mutated = target != mutations_by_target.end();
    for (const modload::BehaviorEntry& entry : card.behaviors) {
        if (mutated) {
            const modload::BehaviorGraph compiled = CompileMutations(
                entry.graph, target->second, compile_options);
            out.triggers[entry.hook] = compiled.raw;
        } else if (entry.graph.raw.is_object()) {
            out.triggers[entry.hook] = entry.graph.raw;
        } else {
            out.triggers[entry.hook] = json{{"nodes", entry.graph.nodes}};
        }
    }
    return out;
}

/**
 * @brief Insert every `add_restriction` entry id declared in `graph`.
 *
 * Mirrors the semantic validator's `CollectGraphDecls`: a mutation
 * `target` that names one of these entries and no behavior kind/rule is a
 * restriction-only mutation and must be WARN-no-opped
 * rather than folded into a colliding graph. Callers scan the same sources the
 * validator scans: card behaviors, rule hooks and mutation replacements.
 */
void CollectRestrictionEntryIds(
    const modload::BehaviorGraph& graph,
    std::set<std::string, std::less<>>& out) {
    for (const json& node : graph.nodes) {
        if (!node.is_object()) continue;
        if (node.value("op", std::string()) != "add_restriction") continue;
        const auto args = node.find("args");
        if (args == node.end() || !args->is_object()) continue;
        const auto entry = args->find("entry_def");
        if (entry == args->end() || !entry->is_object()) continue;
        const auto id = entry->find("id");
        if (id != entry->end() && id->is_string()) {
            out.insert(id->get<std::string>());
        }
    }
}

/** @brief Parse a `{index, generation}` entity handle from JSON. */
std::optional<ecs::Entity> EntityFromJson(const json& value) {
    if (value.is_number_unsigned()) {
        ecs::Entity entity;
        entity.index = value.get<uint32_t>();
        return entity;
    }
    if (!value.is_object()) return std::nullopt;
    const auto index = value.find("index");
    if (index == value.end() || !index->is_number_unsigned()) {
        return std::nullopt;
    }
    ecs::Entity entity;
    entity.index = index->get<uint32_t>();
    const auto generation = value.find("generation");
    if (generation != value.end() && generation->is_number_unsigned()) {
        entity.generation = generation->get<uint32_t>();
    }
    return entity;
}

/** @brief Entity handle bound to `key` in an event payload, or nullopt. */
std::optional<ecs::Entity> PayloadEntity(const json& data,
                                         const char* key) {
    if (!data.is_object()) return std::nullopt;
    const auto it = data.find(key);
    if (it == data.end()) return std::nullopt;
    return EntityFromJson(*it);
}

/** @brief Stable `Entity` map key (index in the high 32 bits). */
uint64_t EntityKey(ecs::Entity entity) {
    return (static_cast<uint64_t>(entity.index) << 32) | entity.generation;
}

/** @brief True when the card kind declares the (legacy numeric) `numbered`. */
bool IsNumbered(const modload::CardDef& card) {
    return std::find(card.tags.begin(), card.tags.end(), "numbered")
        != card.tags.end();
}

/**
 * @brief Resolve the selector context an event payload implies.
 *
 * The engine extends this as more hooks learn their payload entity keys; the
 * keys used here are the payload essentials.
 */
resolver::SelectorContext ContextFromPayload(const ecs::HookPayload& payload) {
    resolver::SelectorContext context;
    context.self = PayloadEntity(payload.data, "player");
    context.target = PayloadEntity(payload.data, "target");
    context.responder = PayloadEntity(payload.data, "responder");
    context.card = PayloadEntity(payload.data, "card");
    context.in_card_context = context.card.has_value();
    context.in_window = context.responder.has_value();
    return context;
}

/** @brief Bind the selectors a `where` condition may read before evaluation. */
void BindWhereSelectors(ecs::EntityStore& store,
                        const resolver::SelectorContext& context,
                        ops::ResolutionFrame& frame) {
    if (context.self.has_value()) {
        frame.BindSelector("@self", {*context.self});
    }
    if (context.target.has_value()) {
        frame.BindSelector("@target", {*context.target});
    }
    if (context.responder.has_value()) {
        frame.BindSelector("@responder", {*context.responder});
    }
    if (context.card.has_value()) {
        frame.BindSelector("@card", {*context.card});
    }
    if (const std::optional<ecs::Entity> match = ops::FindMatch(store);
        match.has_value()) {
        frame.BindSelector("@match", {*match});
    }
    if (const std::optional<ecs::Entity> draw =
            ops::FindPile(store, ecs::PileKind::kDraw);
        draw.has_value()) {
        frame.BindSelector("@draw_pile", {*draw});
    }
    if (const std::optional<ecs::Entity> discard =
            ops::FindPile(store, ecs::PileKind::kDiscard);
        discard.has_value()) {
        frame.BindSelector("@discard_pile", {*discard});
    }
    const std::vector<ecs::Entity> players = ops::PlayersBySeat(store);
    frame.BindSelector("@all_players", players);
    std::vector<ecs::Entity> others;
    others.reserve(players.size());
    for (ecs::Entity player : players) {
        if (!context.self.has_value() || !(player == *context.self)) {
            others.push_back(player);
        }
    }
    frame.BindSelector("@others", std::move(others));
    if (const std::optional<ecs::Entity> current =
            ops::FindCurrentPlayer(store);
        current.has_value()) {
        frame.BindSelector("@current_player", {*current});
    }
}

/** @brief Resolve a hook token (`on_play`, `after:play`, `play`) to a hook. */
std::optional<ecs::HookId> ResolveEntryHook(const modload::BehaviorEntry& e) {
    std::string token = e.hook;
    if (e.phase.has_value() && !e.phase->empty()
        && token.find(':') == std::string::npos) {
        token = *e.phase + ":" + token;
    }
    return ecs::ResolveHookId(token);
}

/** @brief Fisher-Yates the draw pile over the match RNG and renumber it. */
void ShufflePile(ecs::EntityStore& store, ecs::Entity pile, Rng& rng) {
    ecs::PileContents* contents = store.Get<ecs::PileContents>(pile);
    if (contents == nullptr) return;
    std::vector<ecs::Entity>& cards = contents->cards;
    for (std::size_t i = cards.size(); i > 1; --i) {
        const std::size_t j =
            static_cast<std::size_t>(rng.NextDraw() % i);
        std::swap(cards[i - 1], cards[j]);
    }
    for (std::size_t i = 0; i < cards.size(); ++i) {
        if (ecs::InZone* in = store.Get<ecs::InZone>(cards[i])) {
            in->zone = ecs::ZoneRef{ecs::ZoneKind::kDrawPile, ecs::Entity{}};
            in->ordinal = static_cast<uint32_t>(i);
        }
    }
}

/** @brief Draw and deal `n` cards from `draw_pile` into `player`'s hand. */
void DealCards(ecs::EntityStore& store, ecs::Entity draw_pile,
               ecs::Entity player, int n) {
    for (int i = 0; i < n; ++i) {
        const std::optional<ecs::Entity> card = ops::DrawTop(store, draw_pile);
        if (!card.has_value()) return;
        ops::MoveCardToZone(
            store, *card,
            ecs::ZoneRef{ecs::ZoneKind::kHand, player});
    }
}

/**
 * @brief Open the discard with a numeric starter and set the active type.
 *
 * Mirrors legacy `MatchInstance::Start`: take
 * the top-most numeric card from the draw pile, else the top card; the match
 * active type becomes that card's face colour.
 *
 * @return The starter card's kind id, or empty when the draw pile is empty.
 */
std::string OpenDiscard(ecs::EntityStore& store, ecs::Entity draw_pile,
                        ecs::Entity discard_pile,
                        const std::map<std::string, bool>& numbered) {
    ecs::PileContents* draw = store.Get<ecs::PileContents>(draw_pile);
    if (draw == nullptr || draw->cards.empty()) return "";

    std::optional<ecs::Entity> starter;
    for (std::size_t i = draw->cards.size(); i > 0; --i) {
        const ecs::Entity card = draw->cards[i - 1];
        const ecs::CardIdentity* identity = store.Get<ecs::CardIdentity>(card);
        if (identity == nullptr) continue;
        const auto it = numbered.find(identity->kind_id);
        if (it != numbered.end() && it->second) {
            starter = card;
            break;
        }
    }
    if (!starter.has_value()) {
        starter = ops::DrawTop(store, draw_pile);
    } else {
        // INFO: MoveCardToZone detaches from the draw pile itself, so no
        //       separate pop is needed for the chosen starter.
    }
    if (!starter.has_value()) return "";

    ops::MoveCardToZone(
        store, *starter,
        ecs::ZoneRef{ecs::ZoneKind::kDiscardPile, ecs::Entity{}});
    const ecs::CardIdentity* identity = store.Get<ecs::CardIdentity>(*starter);
    return identity == nullptr ? std::string() : identity->kind_id;
}

}  // namespace

// --- MatchRegistries -------------------------------------------------------

std::optional<KindIndex> MatchRegistries::FindKind(
    const std::string& kind_id) const {
    const auto it = kind_index.find(kind_id);
    if (it == kind_index.end()) return std::nullopt;
    return it->second;
}

std::optional<ecs::CompactCardV2> MatchRegistries::CardId(
    ecs::Entity card) const {
    const auto it = card_by_entity.find(EntityKey(card));
    if (it == card_by_entity.end()) return std::nullopt;
    return card_ids[it->second];
}

std::optional<ecs::Entity> MatchRegistries::CardEntity(
    ecs::CompactCardV2 id) const {
    const auto it = entity_by_card.find(id.bits);
    if (it == entity_by_card.end()) return std::nullopt;
    return it->second;
}

void MatchRegistries::AddCard(ecs::Entity card, ecs::CompactCardV2 id) {
    card_by_entity[EntityKey(card)] = static_cast<uint32_t>(card_ids.size());
    entity_by_card[id.bits] = card;
    card_ids.push_back(id);
}

// --- MatchAssembly ---------------------------------------------------------

void MatchAssembly::RunSystem(std::size_t index, ecs::HookPayload& payload) {
    if (index >= systems.size()) return;
    const ModSystem& system = systems[index];
    if (bus.IsDisarmed(system.mod_id)) return;

    // INFO: card behaviors only run for the card they belong to; the played
    //       card rides the `card` payload key.
    if (system.card_kind.has_value()) {
        const std::optional<ecs::Entity> card =
            PayloadEntity(payload.data, "card");
        if (!card.has_value()) return;
        const ecs::CardIdentity* identity = store.Get<ecs::CardIdentity>(*card);
        if (identity == nullptr || identity->kind_id != *system.card_kind) {
            return;
        }
    }

    const resolver::SelectorContext context = ContextFromPayload(payload);

    // INFO: A `draw` dispatch carries the just-drawn card under the
    //       `card` key; bind it as `@drawn_card` so a rule graph (or its
    //       `where`) can test playability and force-play it. The engine's
    //       `play_card` effect routing is what actually plays it.
    std::optional<ecs::Entity> drawn_card;
    if (payload.hook.name == "draw" || payload.hook.name == "draw_attempt") {
        drawn_card = PayloadEntity(payload.data, "card");
    }

    // INFO: a rule hook's `where` filter decides whether its graph runs
    // Selectors the condition may address are bound first.
    if (system.where.has_value() && !system.where->is_null()) {
        ops::ResolutionFrame where_frame;
        ops::OpContext where_ctx(bus, budget, where_frame);
        where_ctx.registries = &registries;
        BindWhereSelectors(store, context, where_frame);
        if (drawn_card.has_value()) {
            where_frame.BindSelector("@drawn_card", {*drawn_card});
        }
        if (!conditions.Evaluate(store, *system.where, where_ctx)) return;
    }

    if (resolver == nullptr) return;

    ops::ResolutionFrame frame;
    if (drawn_card.has_value()) {
        frame.BindSelector("@drawn_card", {*drawn_card});
    }
    const resolver::ResolveResult result =
        resolver->Resolve(system.graph, system.mod_id, context, frame);

    HookRun run;
    run.mod_id = system.mod_id;
    run.source_id = system.source_id;
    run.hook = system.hook;
    run.status = result.status;
    run.events = result.events;
    run.effects = result.effects;
    run.error = result.error;

    // INFO: Additive pause continuation captured for the engine.
    run.system_index = index;
    run.context = context;
    run.input_request = result.input_request;
    run.window = result.window;
    run.schedule = result.schedule;
    run.resume = result.resume;

    runs.push_back(std::move(run));
}

// --- MatchAssembler --------------------------------------------------------

AssemblyResult MatchAssembler::Assemble(
    const std::vector<modload::LoadedMod>& mods,
    const modload::DeckDef& deck,
    const MatchAssemblyOptions& options) {
    auto fail = [](std::string check, std::string message) {
        AssemblyResult result;
        result.error = AssemblyError{std::move(check), std::move(message)};
        return result;
    };

    if (deck.mods.empty()) {
        return fail("assembly.no_mods", "deck declares no mods");
    }
    if (static_cast<uint32_t>(deck.mods.size()) > ecs::kMaxMods) {
        return fail("assembly.mod_count",
                    "active mod count " + std::to_string(deck.mods.size())
                        + " exceeds " + std::to_string(ecs::kMaxMods));
    }
    if (options.players.empty()) {
        return fail("assembly.no_players", "no players seated");
    }
    if (options.starting_cards < 0) {
        return fail("assembly.starting_cards",
                    "starting_cards must be non-negative");
    }

    // INFO: freeze the mod list in the deck's declared order (priority).
    MatchRegistries registries;
    std::vector<const modload::LoadedMod*> active;
    active.reserve(deck.mods.size());
    std::set<std::string> seen_mods;
    for (const std::string& id : deck.mods) {
        if (!seen_mods.insert(id).second) {
            return fail("assembly.duplicate_mod",
                        "mod '" + id + "' listed more than once");
        }
        const modload::LoadedMod* found = nullptr;
        for (const modload::LoadedMod& mod : mods) {
            if (mod.manifest.id == id) {
                found = &mod;
                break;
            }
        }
        if (found == nullptr) {
            return fail("assembly.unknown_mod", "loaded mod '" + id
                                                    + "' is not available");
        }
        active.push_back(found);
        registries.mods.push_back(
            ecs::ModRef{found->manifest.id, found->manifest.version});
    }

    // INFO: per-mod kind table, sorted for a deterministic kind_index. The
    //       list covers every declared kind, deck-referenced or not.
    for (const modload::LoadedMod* mod : active) {
        std::vector<std::string> kinds;
        kinds.reserve(mod->cards.size());
        for (const modload::CardDef& card : mod->cards) {
            kinds.push_back(card.kind_id);
        }
        std::sort(kinds.begin(), kinds.end());
        if (static_cast<uint32_t>(kinds.size()) > ecs::kMaxKindsPerMod) {
            return fail("assembly.kind_count",
                        "mod '" + mod->manifest.id + "' declares "
                            + std::to_string(kinds.size()) + " kinds, exceeds "
                            + std::to_string(ecs::kMaxKindsPerMod));
        }
        const uint32_t mod_index =
            static_cast<uint32_t>(registries.kinds_by_mod.size());
        for (uint32_t ki = 0; ki < kinds.size(); ++ki) {
            registries.kind_index[kinds[ki]] = KindIndex{mod_index, ki};
        }
        registries.kinds_by_mod.push_back(std::move(kinds));
    }

    // INFO: validate the deck multiset against the frozen kind table and the
    //       per-kind instance bound.
    std::map<std::string, int> counts;
    for (const std::pair<std::string, int>& entry : deck.cards) {
        if (entry.second < 0) {
            return fail("assembly.copy_count",
                        "negative count for kind '" + entry.first + "'");
        }
        if (static_cast<uint32_t>(entry.second)
            > ecs::kMaxInstancesPerKind) {
            return fail("assembly.copy_count",
                        "kind '" + entry.first + "' count "
                            + std::to_string(entry.second) + " exceeds "
                            + std::to_string(ecs::kMaxInstancesPerKind));
        }
        if (registries.kind_index.find(entry.first)
            == registries.kind_index.end()) {
            return fail("assembly.unknown_kind",
                        "kind '" + entry.first
                            + "' is not in the active mod set");
        }
        counts[entry.first] = entry.second;
    }

    // INFO: Content-static facts for the lookup and the tag table.
    std::vector<modload::CardDef> active_cards;
    std::map<std::string, modload::PlayCardFacts> card_facts;
    std::map<std::string, bool> numbered;
    for (const modload::LoadedMod* mod : active) {
        for (const modload::CardDef& card : mod->cards) {
            modload::PlayCardFacts facts;
            facts.color = card.face.color.value_or("");
            facts.value = card.face.label.value_or("");
            facts.tags = card.tags;
            card_facts[card.kind_id] = std::move(facts);
            numbered[card.kind_id] = IsNumbered(card);
            active_cards.push_back(card);
        }
    }

    // INFO: mutations are applied at assembly, not walk time.
    //       Collect them per target in the frozen mod-list order (priority).
    std::map<std::string, std::vector<const modload::MutationDef*>>
        mutations_by_target;
    for (const modload::LoadedMod* mod : active) {
        for (const modload::MutationDef& mutation : mod->mutations) {
            mutations_by_target[mutation.target].push_back(&mutation);
        }
    }

    // INFO: classify targets. A target naming a card kind or a rule hook is a
    //       graph mutation and is compiled; one naming only a restriction entry
    //       is a WARN-no-op. Restriction entries are declared in
    //       card behaviors, rule hooks and mutation replacements (the same
    //       sources the validator scans); a kind/rule id always wins so a
    //       colliding target is never mis-filed as a restriction.
    std::set<std::string> kind_ids;
    std::set<std::string> rule_ids;
    std::set<std::string, std::less<>> declared_restrictions;
    for (const modload::LoadedMod* mod : active) {
        for (const modload::CardDef& card : mod->cards) {
            kind_ids.insert(card.kind_id);
            for (const modload::BehaviorEntry& entry : card.behaviors) {
                CollectRestrictionEntryIds(entry.graph, declared_restrictions);
            }
        }
        for (const modload::RuleDef& rule : mod->rules) {
            rule_ids.insert(rule.rule_id);
            for (const modload::BehaviorEntry& entry : rule.hooks) {
                CollectRestrictionEntryIds(entry.graph, declared_restrictions);
            }
        }
        for (const modload::MutationDef& mutation : mod->mutations) {
            CollectRestrictionEntryIds(mutation.replacement,
                                       declared_restrictions);
        }
    }
    MutationCompileOptions compile_options;
    for (const std::string& entry : declared_restrictions) {
        if (kind_ids.count(entry) != 0) continue;
        if (rule_ids.count(entry) != 0) continue;
        compile_options.restriction_targets.insert(entry);
    }
    // INFO: a restriction-only target has no graph to compile, so the compiler
    //       never sees it; warn here so the mutation is not silently dropped.
    for (const modload::LoadedMod* mod : active) {
        for (const modload::MutationDef& mutation : mod->mutations) {
            if (compile_options.restriction_targets.count(mutation.target)
                == 0) {
                continue;
            }
            Logger::Warn("[MutationCompiler] mutation '", mutation.mutation_id,
                         "' targets restriction entry '", mutation.target,
                         "'; inert");
        }
    }

    auto assembly = std::make_unique<MatchAssembly>();
    assembly->registries = registries;
    assembly->card_facts = card_facts;
    // INFO: retain the deck identity + settings so the view layer can build
    //       `match_start` and `match_end` without the caller re-supplying them
    //       (resolves the former match_instance.cpp TODO).
    assembly->deck.deck_id = deck.deck_id;
    assembly->deck.name = deck.name;
    assembly->deck.settings = deck.settings;

    ecs::EntityStore& store = assembly->store;
    MatchRegistries& reg = assembly->registries;

    // --- players (seat order; seat 0 takes the first turn) -----------------
    for (std::size_t i = 0; i < options.players.size(); ++i) {
        const MatchPlayerSpec& spec = options.players[i];
        const ecs::Entity entity = store.Create();
        ecs::PlayerInfo info;
        info.username = spec.username;
        info.seat = static_cast<uint32_t>(i);
        info.connected = spec.connected;
        info.is_bot = spec.is_bot;
        info.ready = spec.ready;
        store.Add(entity, std::move(info));
        store.Add(entity, ecs::Hand{});
        ecs::TurnState turn;
        turn.is_current = (i == 0);
        store.Add(entity, turn);
        reg.players.push_back(entity);
    }

    // --- piles -------------------------------------------------------------
    reg.draw_pile = store.Create();
    ecs::PileContents draw_contents;
    draw_contents.kind = ecs::PileKind::kDraw;
    store.Add(reg.draw_pile, std::move(draw_contents));

    reg.discard_pile = store.Create();
    ecs::PileContents discard_contents;
    discard_contents.kind = ecs::PileKind::kDiscard;
    store.Add(reg.discard_pile, std::move(discard_contents));

    // --- match entity ------------------------------------------------------
    reg.match = store.Create();
    ecs::MatchMeta meta;
    meta.mods = reg.mods;
    meta.direction = ecs::Direction::kForward;
    meta.round = 0;
    store.Add(reg.match, std::move(meta));
    store.Add(reg.match, ecs::Placements{});
    ecs::RngState rng_state;
    store.Add(reg.match, rng_state);
    store.Add(reg.match, ecs::PlayRestriction{});
    store.Add(reg.match, ecs::ActiveTypeReq{});
    store.Add(reg.match, ecs::PendingSchedule{});

    // --- cards (kind-sorted, per-kind instance counter) --------------------
    for (uint32_t mi = 0; mi < reg.kinds_by_mod.size(); ++mi) {
        const modload::LoadedMod* mod = active[mi];
        for (uint32_t ki = 0; ki < reg.kinds_by_mod[mi].size(); ++ki) {
            const std::string& kind_id = reg.kinds_by_mod[mi][ki];
            const auto count_it = counts.find(kind_id);
            const int count = count_it == counts.end() ? 0 : count_it->second;
            const modload::CardDef* def = nullptr;
            for (const modload::CardDef& card : mod->cards) {
                if (card.kind_id == kind_id) {
                    def = &card;
                    break;
                }
            }
            for (int instance = 0; instance < count; ++instance) {
                const std::optional<ecs::CompactCardV2> compact =
                    ecs::MakeCompactCard(mi, ki,
                                         static_cast<uint32_t>(instance));
                if (!compact.has_value()) {
                    return fail("assembly.index_overflow",
                                "compact card index out of range for '"
                                    + kind_id + "'");
                }
                const ecs::Entity entity = store.Create();
                ecs::CardIdentity identity;
                identity.mod_index = mi;
                identity.kind_index = ki;
                identity.kind_id = kind_id;
                store.Add(entity, std::move(identity));

                ecs::InZone zone;
                zone.zone = ecs::ZoneRef{ecs::ZoneKind::kDrawPile,
                                         ecs::Entity{}};
                zone.ordinal = static_cast<uint32_t>(reg.cards.size());
                store.Add(entity, zone);

                if (def != nullptr) {
                    store.Add(entity, ToEcsFace(def->face));
                    store.Add(entity, ToEcsBehavior(*def, mutations_by_target,
                                                    compile_options));
                    if (def->window.has_value()) {
                        store.Add(entity, ToEcsWindow(*def->window));
                    }
                    if (def->auto_trigger.has_value()) {
                        store.Add(entity,
                                  ToEcsAutoTrigger(*def->auto_trigger));
                    }
                }

                reg.AddCard(entity, *compact);
                reg.cards.push_back(entity);

                ecs::PileContents* draw = store.Get<ecs::PileContents>(
                    reg.draw_pile);
                if (draw != nullptr) draw->cards.push_back(entity);
            }
        }
    }

    // --- seed, shuffle, deal, open discard
    const std::optional<uint64_t> seed =
        ::match::InitializeRngSeed(store, reg.match, options.seed);
    if (seed.has_value()) {
        ecs::RngState* state = store.Get<ecs::RngState>(reg.match);
        if (state != nullptr) {
            match::Rng rng(*state);
            ShufflePile(store, reg.draw_pile, rng);
        }
    }
    for (ecs::Entity player : reg.players) {
        DealCards(store, reg.draw_pile, player, options.starting_cards);
    }
    const std::string starter =
        OpenDiscard(store, reg.draw_pile, reg.discard_pile, numbered);
    if (!starter.empty()) {
        const auto facts = card_facts.find(starter);
        if (facts != card_facts.end()) {
            if (ecs::ActiveTypeReq* req =
                    store.Get<ecs::ActiveTypeReq>(reg.match);
                req != nullptr) {
                req->type = facts->second.color;
            }
        }
    }

    // --- the validator matcher and the op layer tag table
    // ------------------------------------
    assembly->play_matcher = std::make_unique<modload::PlayConditionMatcher>(
        [facts = card_facts](const std::string& kind_id,
                             modload::PlayCardFacts& out) {
            const auto it = facts.find(kind_id);
            if (it == facts.end()) return false;
            out = it->second;
            return true;
        });
    // INFO: The tag table is per-match (`registries.card_tags`),
    //       never a process global, so concurrent assemblies cannot clobber
    //       each other's `has_card_tag` / `draw_penalty` / `tag:` semantics.
    wiring::LoadCardTags(active_cards, assembly->registries.card_tags);

    // --- systems: rules then card behaviors, frozen order
    for (const modload::LoadedMod* mod : active) {
        uint32_t registration = 0;
        for (const modload::RuleDef& rule : mod->rules) {
            for (const modload::BehaviorEntry& entry : rule.hooks) {
                const std::optional<ecs::HookId> hook =
                    ResolveEntryHook(entry);
                if (!hook.has_value()) continue;
                ModSystem system;
                system.mod_id = mod->manifest.id;
                system.registration_index = registration++;
                system.hook = *hook;
                system.source_id = rule.rule_id;
                system.where = entry.where;
                system.graph = entry.graph;
                if (const auto mutated = mutations_by_target.find(rule.rule_id);
                    mutated != mutations_by_target.end()) {
                    // INFO: compiled here so the Resolver walks the effective
                    //       graph; `call_original` never reaches it.
                    system.graph = CompileMutations(entry.graph, mutated->second,
                                                    compile_options);
                }
                assembly->systems.push_back(std::move(system));
            }
        }
        for (const modload::CardDef& card : mod->cards) {
            for (const modload::BehaviorEntry& entry : card.behaviors) {
                const std::optional<ecs::HookId> hook =
                    ResolveEntryHook(entry);
                if (!hook.has_value()) continue;
                ModSystem system;
                system.mod_id = mod->manifest.id;
                system.registration_index = registration++;
                system.hook = *hook;
                system.source_id = card.kind_id;
                system.card_kind = card.kind_id;
                system.graph = entry.graph;
                if (const auto mutated = mutations_by_target.find(card.kind_id);
                    mutated != mutations_by_target.end()) {
                    // INFO: the subscribed card behavior graph is what the bus
                    //       walks; compile it exactly like the component above.
                    system.graph = CompileMutations(entry.graph, mutated->second,
                                                    compile_options);
                }
                assembly->systems.push_back(std::move(system));
            }
        }
    }

    // --- bus wiring, resolver, match_start (installs restrictions) ---------
    std::vector<std::string> mod_order;
    mod_order.reserve(reg.mods.size());
    for (const ecs::ModRef& mod : reg.mods) mod_order.push_back(mod.id);
    assembly->bus.SetModOrder(mod_order);

    ops::RegisterDefaultOps(assembly->runtime);
    wiring::InstallDefaultConditions(assembly->conditions);
    assembly->resolver = std::make_unique<resolver::Resolver>(
        assembly->store, assembly->runtime, assembly->bus, assembly->budget,
        assembly->conditions, resolver::ResolverConfig::FromEnv());
    // INFO: The assembly hands its frozen card index map to the
    //       resolver, which attaches it to every op context so card-prompt
    //       ops emit wire `CompactCardV2.bits`.
    assembly->resolver->SetRegistries(&assembly->registries);

    for (std::size_t i = 0; i < assembly->systems.size(); ++i) {
        MatchAssembly* self = assembly.get();
        assembly->bus.Subscribe(
            assembly->systems[i].mod_id,
            assembly->systems[i].registration_index,
            assembly->systems[i].hook,
            [self, i](ecs::HookPayload& payload) {
                self->RunSystem(i, payload);
            });
    }

    // INFO: `match_start` fires once assembly is done. Dispatching it
    //       here is what installs the vanilla restriction entries shipped as
    //       `after:match_start` data; the engine does not re-dispatch it.
    const json settings = deck.settings;
    ecs::HookPayload before;
    before.hook = ecs::HookId{"match_start", ecs::HookPhase::kBefore};
    before.data = json{{"settings", settings}};
    assembly->bus.DispatchBefore(before.hook, before);
    ecs::HookPayload after;
    after.hook = ecs::HookId{"match_start", ecs::HookPhase::kAfter};
    after.data = before.data;
    assembly->bus.DispatchAfter(after.hook, after);

    // INFO: mirror the assembly-time budget into the match entity so the
    //       snapshot agrees with the resolver's ledger.
    if (ecs::MatchMeta* meta = store.Get<ecs::MatchMeta>(reg.match);
        meta != nullptr) {
        meta->budgets = assembly->budget;
    }

    AssemblyResult result;
    result.assembly = std::move(assembly);
    return result;
}

}  // namespace match::engine
