#include <doctest/doctest.h>

#include <match/engine/match_assembler.hpp>
#include <match/ecs/compact_card.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

/**
 * @file engine_assembler_test.cpp
 * @brief `MatchAssembler` acceptance tests.
 *
 * Builds matches from the real `mods/` tree and the vanilla classic deck, so
 * the frozen registries, entity layout, deterministic instance ids, bound
 * enforcement and RNG seeding are all exercised against shipped content.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
using namespace match::engine;
using namespace match::ecs;
using namespace match::modload;

namespace {

std::string AssemblyMessage(const AssemblyResult& result) {
    return result.error.has_value() ? result.error->message
                                    : std::string("assembly failed");
}

/* INFO: locate the project root from this file, mirroring
 *       vanilla_content_test.cpp, so the test is cwd-independent. */
fs::path ProjectRoot() {
    fs::path p(__FILE__);
    while (!p.empty()) {
        std::error_code ec;
        if (fs::is_directory(p / "contract" / "schemas", ec)) return p;
        fs::path parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    return {};
}

struct Content {
    std::vector<LoadedMod> mods;
    DeckDef classic;
};

bool LoadContent(Content& out) {
    const fs::path root = ProjectRoot();
    if (root.empty()) return false;
    LoadResult load = ScanModsDirectory((root / "mods").string());
    if (!load.ok()) return false;
    out.mods = std::move(load.mods);
    for (const LoadedMod& mod : out.mods) {
        for (const DeckDef& deck : mod.decks) {
            if (deck.deck_id == "vanilla:classic") out.classic = deck;
        }
    }
    return !out.classic.deck_id.empty();
}

MatchAssemblyOptions Players(int count, int starting_cards,
                             uint64_t seed) {
    MatchAssemblyOptions options;
    options.starting_cards = starting_cards;
    options.seed = seed;
    for (int i = 0; i < count; ++i) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(i);
        options.players.push_back(spec);
    }
    return options;
}

std::vector<std::string> HandKinds(const MatchAssembly& assembly,
                                   ecs::Entity player) {
    std::vector<std::string> kinds;
    const ecs::Hand* hand = assembly.store.Get<ecs::Hand>(player);
    if (hand == nullptr) return kinds;
    for (ecs::Entity card : hand->cards) {
        const ecs::CardIdentity* identity =
            assembly.store.Get<ecs::CardIdentity>(card);
        kinds.push_back(identity == nullptr ? "" : identity->kind_id);
    }
    return kinds;
}

std::size_t PileSize(const MatchAssembly& assembly, ecs::PileKind kind) {
    for (ecs::Entity pile :
         assembly.store.EntitiesWith<ecs::PileContents>()) {
        const ecs::PileContents* contents =
            assembly.store.Get<ecs::PileContents>(pile);
        if (contents != nullptr && contents->kind == kind) {
            return contents->cards.size();
        }
    }
    return 0;
}

ModManifest SynthManifest(const std::string& id) {
    ModManifest manifest;
    manifest.id = id;
    manifest.name = id;
    manifest.version = "1.0.0";
    manifest.api = "1";
    return manifest;
}

CardDef SynthCard(const std::string& ns, std::vector<std::string> tags) {
    CardDef card;
    card.id = "card";
    card.namespace_id = ns;
    card.kind_id = ns + ":card";
    card.title = ns;
    card.face.kind = match::modload::FaceKind::kText;
    card.face.color = std::string("red");
    card.face.label = ns;
    card.tags = std::move(tags);
    return card;
}

DeckDef SynthDeck(const std::string& mod_id) {
    DeckDef deck;
    deck.id = mod_id;
    deck.namespace_id = mod_id;
    deck.deck_id = mod_id + ":deck";
    deck.name = mod_id;
    deck.mods = {mod_id};
    deck.cards = {{mod_id + ":card", 10}};
    return deck;
}

}  // namespace

TEST_CASE("engine assembler: classic deck builds entities and hands") {
    Content content;
    REQUIRE(LoadContent(content));

    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, content.classic,
                                 Players(/*count=*/4, /*cards=*/7,
                                         /*seed=*/42));
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    MatchAssembly& assembly = *result.assembly;

    CHECK(assembly.store.LiveCount() == 108 + 4 + 2 + 1);
    CHECK(assembly.registries.cards.size() == 108);
    CHECK(assembly.registries.players.size() == 4);
    CHECK(assembly.registries.mods.size() == 1);
    CHECK(assembly.registries.mods.front().id == "vanilla");

    for (ecs::Entity player : assembly.registries.players) {
        CHECK(HandKinds(assembly, player).size() == 7);
    }
    CHECK(PileSize(assembly, ecs::PileKind::kDraw) == 108 - 28 - 1);
    CHECK(PileSize(assembly, ecs::PileKind::kDiscard) == 1);

    const ecs::TurnState* first =
        assembly.store.Get<ecs::TurnState>(assembly.registries.players[0]);
    REQUIRE(first != nullptr);
    CHECK(first->is_current);

    const ecs::ActiveTypeReq* active =
        assembly.store.Get<ecs::ActiveTypeReq>(assembly.registries.match);
    REQUIRE(active != nullptr);
    CHECK(active->type.has_value());
}

TEST_CASE("engine assembler: kind table and CompactCardV2 round-trip") {
    Content content;
    REQUIRE(LoadContent(content));
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, content.classic,
                                 Players(2, 7, 7));
    REQUIRE(result.ok());
    MatchAssembly& assembly = *result.assembly;
    const MatchRegistries& reg = assembly.registries;

    REQUIRE(reg.kinds_by_mod.size() == 1);
    CHECK(reg.kinds_by_mod.front().size() == 54);
    CHECK(std::is_sorted(reg.kinds_by_mod.front().begin(),
                         reg.kinds_by_mod.front().end()));

    std::map<std::string, uint32_t> instances;
    for (std::size_t i = 0; i < reg.cards.size(); ++i) {
        const ecs::Entity card = reg.cards[i];
        const ecs::CardIdentity* identity =
            assembly.store.Get<ecs::CardIdentity>(card);
        REQUIRE(identity != nullptr);

        const ecs::CompactCardV2 packed = reg.card_ids[i];
        CHECK(packed.ModIndex() == 0);
        CHECK(packed.KindIndex() == identity->kind_index);
        CHECK(identity->kind_id == reg.kinds_by_mod[0][packed.KindIndex()]);

        const std::optional<ecs::CompactCardV2> looked_up = reg.CardId(card);
        REQUIRE(looked_up.has_value());
        CHECK(looked_up->bits == packed.bits);
        const std::optional<ecs::Entity> back = reg.CardEntity(packed);
        REQUIRE(back.has_value());
        CHECK(*back == card);

        const uint32_t instance = packed.InstanceId();
        const uint32_t expected = instances[identity->kind_id]++;
        CHECK(instance == expected);
    }

    const std::optional<KindIndex> red5 = reg.FindKind("vanilla:red_5");
    REQUIRE(red5.has_value());
    CHECK(red5->mod_index == 0);
    CHECK_FALSE(reg.FindKind("ghost:nope").has_value());
}

TEST_CASE("engine assembler: same seed assembles identically") {
    Content content;
    REQUIRE(LoadContent(content));
    AssemblyResult a =
        MatchAssembler::Assemble(content.mods, content.classic,
                                 Players(4, 7, 123456789));
    AssemblyResult b =
        MatchAssembler::Assemble(content.mods, content.classic,
                                 Players(4, 7, 123456789));
    REQUIRE(a.ok());
    REQUIRE(b.ok());

    for (std::size_t p = 0; p < a.assembly->registries.players.size(); ++p) {
        CHECK(HandKinds(*a.assembly, a.assembly->registries.players[p])
              == HandKinds(*b.assembly, b.assembly->registries.players[p]));
    }
    CHECK(PileSize(*a.assembly, ecs::PileKind::kDraw)
          == PileSize(*b.assembly, ecs::PileKind::kDraw));
    CHECK(PileSize(*a.assembly, ecs::PileKind::kDiscard)
          == PileSize(*b.assembly, ecs::PileKind::kDiscard));
    for (std::size_t i = 0; i < a.assembly->registries.card_ids.size(); ++i) {
        CHECK(a.assembly->registries.card_ids[i].bits
              == b.assembly->registries.card_ids[i].bits);
    }
}

TEST_CASE("engine assembler: RNG seeding is explicit and deterministic") {
    Content content;
    REQUIRE(LoadContent(content));

    AssemblyResult first =
        MatchAssembler::Assemble(content.mods, content.classic,
                                 Players(2, 7, 987654321));
    REQUIRE(first.ok());
    const ecs::RngState* state = first.assembly->store.Get<ecs::RngState>(
        first.assembly->registries.match);
    REQUIRE(state != nullptr);
    CHECK(state->seed == 987654321u);

    const ecs::MatchMeta* meta = first.assembly->store.Get<ecs::MatchMeta>(
        first.assembly->registries.match);
    REQUIRE(meta != nullptr);
    CHECK(meta->rng_seed == 987654321u);

    AssemblyResult other =
        MatchAssembler::Assemble(content.mods, content.classic,
                                 Players(2, 7, 111));
    REQUIRE(other.ok());
    const ecs::RngState* other_state = other.assembly->store.Get<ecs::RngState>(
        other.assembly->registries.match);
    REQUIRE(other_state != nullptr);
    CHECK(other_state->seed == 111u);
    CHECK(other_state->seed != state->seed);
}

TEST_CASE("engine assembler: installs vanilla restriction entries") {
    Content content;
    REQUIRE(LoadContent(content));
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, content.classic,
                                 Players(4, 7, 5));
    REQUIRE(result.ok());

    const ecs::PlayRestriction* pipeline =
        result.assembly->store.Get<ecs::PlayRestriction>(
            result.assembly->registries.match);
    REQUIRE(pipeline != nullptr);
    REQUIRE(pipeline->entries.size() == 3);
    CHECK(pipeline->entries[0].id == "vanilla:turn_order");
    CHECK(pipeline->entries[1].id == "vanilla:match_type_or_value");
    CHECK(pipeline->entries[2].id == "vanilla:must_own_card");
    CHECK(pipeline->entries[1].phase == ecs::RestrictionPhase::kDeny);

    REQUIRE(result.assembly->play_matcher != nullptr);
    PlayAttempt out_of_turn;
    out_of_turn.player = "player1";
    out_of_turn.in_turn = false;
    CHECK(result.assembly->play_matcher->Matches(
        nlohmann::json{{"plays_out_of_turn", nlohmann::json::object()}},
        out_of_turn));
    out_of_turn.in_turn = true;
    CHECK_FALSE(result.assembly->play_matcher->Matches(
        nlohmann::json{{"plays_out_of_turn", nlohmann::json::object()}},
        out_of_turn));

    // INFO: The tag table is per-assembly, read through the
    //       assembly's own registries, not a process-global static.
    CHECK(match::ops::CardHasTag(&result.assembly->registries, "vanilla:red_5",
                                 "red"));
    CHECK(match::ops::CardHasTag(&result.assembly->registries,
                                 "vanilla:wild_draw4", "stackable"));
    CHECK_FALSE(match::ops::CardHasTag(&result.assembly->registries,
                                       "vanilla:red_5", "stackable"));
}

TEST_CASE("engine assembler: per-match card tags are isolated") {
    // INFO: Regression - assembling a second match used to REPLACE a
    //       process-global tag table, changing the first match's `has_card_tag`
    //       / `draw_penalty` / `tag:` results. With the table on each match's
    //       registries the two assemblies stay isolated.
    LoadedMod mod_a;
    mod_a.manifest = SynthManifest("mod_a");
    mod_a.cards.push_back(SynthCard("mod_a", {"alpha"}));
    LoadedMod mod_b;
    mod_b.manifest = SynthManifest("mod_b");
    mod_b.cards.push_back(SynthCard("mod_b", {"beta"}));

    AssemblyResult a = MatchAssembler::Assemble({mod_a}, SynthDeck("mod_a"),
                                                Players(2, 3, 1));
    REQUIRE_MESSAGE(a.ok(), AssemblyMessage(a));
    AssemblyResult b = MatchAssembler::Assemble({mod_b}, SynthDeck("mod_b"),
                                                Players(2, 3, 2));
    REQUIRE_MESSAGE(b.ok(), AssemblyMessage(b));

    CHECK(match::ops::CardHasTag(&a.assembly->registries, "mod_a:card",
                                 "alpha"));
    CHECK_FALSE(match::ops::CardHasTag(&a.assembly->registries, "mod_b:card",
                                       "beta"));
    CHECK(match::ops::CardHasTag(&b.assembly->registries, "mod_b:card",
                                 "beta"));
    CHECK_FALSE(match::ops::CardHasTag(&b.assembly->registries, "mod_a:card",
                                       "alpha"));
}

TEST_CASE("engine assembler: all active mods subscribe restrictions") {
    Content content;
    REQUIRE(LoadContent(content));
    DeckDef deck = content.classic;
    deck.mods = {"vanilla", "jump_in", "no_bluffing"};

    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, deck, Players(4, 7, 3));
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    const ecs::PlayRestriction* pipeline =
        result.assembly->store.Get<ecs::PlayRestriction>(
            result.assembly->registries.match);
    REQUIRE(pipeline != nullptr);
    REQUIRE(pipeline->entries.size() == 5);
    CHECK(pipeline->entries[3].id == "jump_in:identical_out_of_turn");
    CHECK(pipeline->entries[3].phase == ecs::RestrictionPhase::kAllow);
    CHECK(pipeline->entries[4].id == "no_bluffing:drawn_four");

    bool has_match_start_system = false;
    for (const ModSystem& system : result.assembly->Systems()) {
        if (system.hook.name == "match_start") has_match_start_system = true;
    }
    CHECK(has_match_start_system);
}

TEST_CASE("engine assembler: enforces index-space bounds") {
    Content content;
    REQUIRE(LoadContent(content));

    SUBCASE("copy count over 4096") {
        DeckDef deck = content.classic;
        deck.cards = {{"vanilla:red_5", 4097}};
        AssemblyResult result =
            MatchAssembler::Assemble(content.mods, deck, Players(2, 7, 1));
        CHECK_FALSE(result.ok());
        REQUIRE(result.error.has_value());
        CHECK(result.error->check == "assembly.copy_count");
    }
    SUBCASE("unknown kind") {
        DeckDef deck = content.classic;
        deck.cards = {{"vanilla:not_a_card", 1}};
        AssemblyResult result =
            MatchAssembler::Assemble(content.mods, deck, Players(2, 7, 1));
        CHECK_FALSE(result.ok());
        REQUIRE(result.error.has_value());
        CHECK(result.error->check == "assembly.unknown_kind");
    }
    SUBCASE("unknown mod") {
        DeckDef deck = content.classic;
        deck.mods = {"ghost"};
        AssemblyResult result =
            MatchAssembler::Assemble(content.mods, deck, Players(2, 7, 1));
        CHECK_FALSE(result.ok());
        REQUIRE(result.error.has_value());
        CHECK(result.error->check == "assembly.unknown_mod");
    }
    SUBCASE("no players") {
        AssemblyResult result =
            MatchAssembler::Assemble(content.mods, content.classic,
                                     MatchAssemblyOptions{});
        CHECK_FALSE(result.ok());
        REQUIRE(result.error.has_value());
        CHECK(result.error->check == "assembly.no_players");
    }
}
