#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/view/event_audience.hpp>
#include <match/view/view_builder.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file streak_tags_test.cpp
 * @brief Card kind tags: the base cards carry the skip, reverse, and wild
 *        tags, and `card_played` gives the tags of the played card to every
 *        seat (the card is public when it is played).
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
namespace ops = match::ops;
using namespace match::engine;
using namespace match::modload;
using match::view::ViewBuilder;
using match::view::Viewer;
using nlohmann::json;

namespace {

fs::path ProjectRoot() {
    fs::path path(__FILE__);
    while (!path.empty()) {
        std::error_code error;
        if (fs::is_directory(path / "contract" / "schemas", error)) {
            return path;
        }
        fs::path parent = path.parent_path();
        if (parent == path) break;
        path = parent;
    }
    return {};
}

struct Content {
    std::vector<LoadedMod> mods;
    DeckDef classic;
};

Content LoadContent() {
    Content content;
    LoadResult load = ScanModsDirectory((ProjectRoot() / "mods").string());
    REQUIRE(load.ok());
    content.mods = std::move(load.mods);
    for (const LoadedMod& mod : content.mods) {
        for (const DeckDef& deck : mod.decks) {
            if (deck.deck_id == "uni:classic") content.classic = deck;
        }
    }
    REQUIRE_FALSE(content.classic.deck_id.empty());
    content.classic.mods = {"uni"};
    return content;
}

std::unique_ptr<MatchInstance> MakeEngine(Content& content) {
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 42;
    for (int index = 0; index < 3; ++index) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(index);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, content.classic, options);
    REQUIRE(result.ok());
    return std::make_unique<MatchInstance>(std::move(result.assembly));
}

bool HasTag(const std::vector<std::string>& tags, const std::string& tag) {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

const LoadedMod& BaseMod(const Content& content) {
    for (const LoadedMod& mod : content.mods) {
        if (mod.manifest.id == "uni") return mod;
    }
    FAIL("no uni mod");
    return content.mods.front();
}

std::optional<ecs::Entity> CardOfKind(MatchInstance& engine,
                                      const std::string& kind_id) {
    for (ecs::Entity card : engine.Registries().cards) {
        const ecs::CardIdentity* identity =
            engine.Store().Get<ecs::CardIdentity>(card);
        if (identity != nullptr && identity->kind_id == kind_id) return card;
    }
    return std::nullopt;
}

void GiveHand(MatchInstance& engine, ecs::Entity player, ecs::Entity card) {
    ops::MoveCardToZone(engine.Store(), card,
                        ecs::ZoneRef{ecs::ZoneKind::kHand, player});
}

/** @brief The `card_played` payloads that one viewer receives. */
std::vector<json> CardPlayedFor(MatchInstance& engine, const Content& content,
                                const Viewer& viewer) {
    ViewBuilder builder(engine, content.mods);
    std::vector<json> played;
    for (const json& packet : builder.BuildPackets(viewer)) {
        if (packet.value("id", "") == "card_played") {
            played.push_back(packet["payload"]);
        }
    }
    return played;
}

}  // namespace

TEST_CASE("tags: the base cards carry the skip, reverse, and wild tags") {
    const Content content = LoadContent();
    int skips = 0;
    int reverses = 0;
    int wilds = 0;
    for (const CardDef& card : BaseMod(content).cards) {
        const bool is_skip = card.id.size() > 5
                             && card.id.rfind("_skip") == card.id.size() - 5;
        const bool is_reverse =
            card.id.size() > 8
            && card.id.rfind("_reverse") == card.id.size() - 8;
        const bool is_wild = card.id == "wild" || card.id == "wild_draw4";
        CHECK_MESSAGE(HasTag(card.tags, "uni:skip") == is_skip, card.id);
        CHECK_MESSAGE(HasTag(card.tags, "uni:reverse") == is_reverse, card.id);
        CHECK_MESSAGE(HasTag(card.tags, "uni:wild") == is_wild, card.id);
        skips += is_skip ? 1 : 0;
        reverses += is_reverse ? 1 : 0;
        wilds += is_wild ? 1 : 0;
    }
    CHECK(skips == 4);
    CHECK(reverses == 4);
    CHECK(wilds == 2);
}

TEST_CASE("tags: the audience table makes the card_played tags public") {
    CHECK(match::view::AudienceOf("card_played", "tags")
          == match::view::Audience::kAll);
}

TEST_CASE("tags: card_played gives the tags of the played card to all") {
    Content content = LoadContent();
    std::unique_ptr<MatchInstance> engine = MakeEngine(content);
    const std::string current = engine->GetCurrentPlayerUsername();
    const std::optional<ecs::Entity> player = engine->FindPlayer(current);
    REQUIRE(player.has_value());
    const std::string active = engine->ExportState()["active_type"];
    const std::optional<ecs::Entity> reverse =
        CardOfKind(*engine, "uni:" + active + "_reverse");
    REQUIRE(reverse.has_value());
    GiveHand(*engine, *player, *reverse);
    REQUIRE(engine->PlayCard(current, *reverse));

    const std::vector<std::string> seats = {"player0", "player1", "player2"};
    std::vector<Viewer> viewers = {Viewer::PublicSpectator()};
    for (const std::string& seat : seats) {
        viewers.push_back(Viewer::Player(seat));
    }
    for (const Viewer& viewer : viewers) {
        const std::vector<json> played = CardPlayedFor(*engine, content, viewer);
        REQUIRE(played.size() == 1);
        REQUIRE(played[0].contains("tags"));
        const std::vector<std::string> tags =
            played[0]["tags"].get<std::vector<std::string>>();
        CHECK(HasTag(tags, "uni:reverse"));
        CHECK_FALSE(HasTag(tags, "uni:skip"));
    }
}
