#include <doctest/doctest.h>

#include <match/modload/play_conditions.hpp>
#include <match/modload/semantic_validator.hpp>
#include <match/modload/vocabulary.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

using match::modload::CardDef;
using match::modload::ConditionCatalog;
using match::modload::EvaluatePlayCondition;
using match::modload::EvaluatePlayRestrictions;
using match::modload::LoadedMod;
using match::modload::PlayAttempt;
using match::modload::PlayCardFacts;
using match::modload::PlayCardLookup;
using match::modload::PlayConditionMatcher;
using match::modload::RestrictionEntry;
using match::modload::SemanticValidator;

using nlohmann::json;

namespace {

json Cond(const std::string& keyword, json args = json::object()) {
    return json{{keyword, std::move(args)}};
}

/** Synthetic content table for the predicates under test. */
PlayCardLookup Lookup() {
    return [](const std::string& kind, PlayCardFacts& out) {
        if (kind == "vanilla:red_5") {
            out = {"red", "5", {"colored", "red"}};
            return true;
        }
        if (kind == "vanilla:red_7") {
            out = {"red", "7", {"colored", "red", "seven"}};
            return true;
        }
        if (kind == "vanilla:blue_5") {
            out = {"blue", "5", {"colored", "blue"}};
            return true;
        }
        if (kind == "vanilla:blue_7") {
            out = {"blue", "7", {"colored", "blue"}};
            return true;
        }
        if (kind == "vanilla:green_7") {
            out = {"green", "7", {"colored", "green"}};
            return true;
        }
        if (kind == "vanilla:white_wild4") {
            out = {"white", "jolly_draw4", {"wild", "wild_draw4"}};
            return true;
        }
        return false;
    };
}

PlayAttempt Attempt(const std::string& kind,
                    bool in_turn,
                    const std::string& active = "",
                    const std::string& top = "",
                    std::vector<std::string> hand = {}) {
    PlayAttempt attempt;
    attempt.player = "p1";
    attempt.card_kind = kind;
    attempt.in_turn = in_turn;
    attempt.context = json::object();
    if (!active.empty()) attempt.context["active_type"] = active;
    if (!top.empty()) attempt.context["top_kind"] = top;
    if (!hand.empty()) attempt.context["hand"] = std::move(hand);
    return attempt;
}

// --- validator harness (mirrors semantic_validator_test's minimal mod) ------

std::string SchemaDir() {
    fs::path p(__FILE__);
    while (!p.empty()) {
        std::error_code ec;
        fs::path cand = p / "contract" / "schemas";
        if (fs::is_directory(cand, ec)) return cand.string();
        fs::path parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    return {};
}

bool HasCheck(const std::vector<match::modload::LoadError>& errors,
              const std::string& check) {
    for (const auto& e : errors) {
        if (e.check == check) return true;
    }
    return false;
}

/** @brief A schema-valid one-card mod whose play hook carries `condition`. */
LoadedMod PlayMod(const json& condition) {
    LoadedMod mod;
    mod.folder = "alpha";
    mod.path = "/tmp/uni_play_conditions/alpha";
    mod.manifest.id = "alpha";
    mod.manifest.name = "Alpha";
    mod.manifest.version = "1.0.0";
    mod.manifest.api = "1";
    mod.manifest.raw = {{"id", "alpha"},
                        {"name", "Alpha"},
                        {"version", "1.0.0"},
                        {"api", "1"}};
    CardDef card;
    card.id = "c1";
    card.namespace_id = "alpha";
    card.kind_id = "alpha:c1";
    card.title = "C1";
    card.raw = {{"id", "c1"},
                {"title", "C1"},
                {"face",
                 {{"kind", "text"}, {"color", "red"}, {"label", "1"}}},
                {"tags", json::array({"stackable"})}};
    card.tags = {"stackable"};
    match::modload::BehaviorEntry be;
    be.hook = "on_play";
    be.phase = "after";
    be.where = condition;
    const json node = {
        {"id", "n1"}, {"op", "advance_turn"}, {"args", json::object()}};
    be.graph.raw = json{{"nodes", json::array({node})}};
    be.graph.nodes = be.graph.raw["nodes"];
    card.behaviors.push_back(be);
    card.raw["behavior"] = json{{"on_play", be.graph.raw}};
    mod.cards.push_back(card);
    return mod;
}

}  // namespace

// --- turn context ----------------------------------------------------------

TEST_CASE("play_conditions: in_turn and plays_out_of_turn") {
    PlayConditionMatcher matcher(Lookup());
    const PlayAttempt in = Attempt("vanilla:red_5", true);
    const PlayAttempt out = Attempt("vanilla:red_5", false);
    CHECK(matcher(Cond("in_turn"), in));
    CHECK_FALSE(matcher(Cond("in_turn"), out));
    CHECK(matcher(Cond("plays_out_of_turn"), out));
    CHECK_FALSE(matcher(Cond("plays_out_of_turn"), in));
}

// --- match predicates ------------------------------------------------------

TEST_CASE("play_conditions: plays_matches_active") {
    PlayConditionMatcher matcher(Lookup());
    CHECK(matcher(Cond("plays_matches_active"),
                  Attempt("vanilla:red_5", true, "red")));
    CHECK_FALSE(matcher(Cond("plays_matches_active"),
                        Attempt("vanilla:blue_5", true, "red")));
    CHECK_FALSE(matcher(Cond("plays_matches_active"),
                        Attempt("vanilla:red_5", true)));
    // INFO: wild (white) never matches a colour.
    CHECK_FALSE(matcher(Cond("plays_matches_active"),
                        Attempt("vanilla:white_wild4", true, "red")));
}

TEST_CASE("play_conditions: plays_matches_top") {
    PlayConditionMatcher matcher(Lookup());
    CHECK(matcher(Cond("plays_matches_top"),
                  Attempt("vanilla:red_5", true, "blue", "vanilla:red_5")));
    // INFO: same value, different colour still matches the top by value.
    CHECK(matcher(Cond("plays_matches_top"),
                  Attempt("vanilla:blue_5", true, "red", "vanilla:red_5")));
    CHECK_FALSE(matcher(
        Cond("plays_matches_top"),
        Attempt("vanilla:green_7", true, "red", "vanilla:red_5")));
    CHECK_FALSE(matcher(Cond("plays_matches_top"),
                        Attempt("vanilla:red_5", true, "red")));
}

TEST_CASE("play_conditions: plays_mismatch mirrors StandardRule") {
    PlayConditionMatcher matcher(Lookup());
    // matches active colour -> legal.
    CHECK_FALSE(matcher(Cond("plays_mismatch"),
                        Attempt("vanilla:red_5", true, "red",
                                "vanilla:blue_7")));
    // matches top value -> legal.
    CHECK_FALSE(matcher(Cond("plays_mismatch"),
                        Attempt("vanilla:red_5", true, "blue",
                                "vanilla:red_5")));
    // neither -> mismatch.
    CHECK(matcher(Cond("plays_mismatch"),
                  Attempt("vanilla:blue_5", true, "red",
                          "vanilla:green_7")));
    // wild is always legal.
    CHECK_FALSE(matcher(Cond("plays_mismatch"),
                        Attempt("vanilla:white_wild4", true, "red",
                                "vanilla:green_7")));
    // empty discard + non-wild, non-active -> mismatch.
    CHECK(matcher(Cond("plays_mismatch"), Attempt("vanilla:blue_5", true)));
    CHECK_FALSE(matcher(Cond("plays_mismatch"),
                        Attempt("vanilla:blue_5", true, "blue")));
}

TEST_CASE("play_conditions: plays_identical_to_top mirrors jump_in") {
    PlayConditionMatcher matcher(Lookup());
    CHECK(matcher(Cond("plays_identical_to_top"),
                  Attempt("vanilla:red_7", false, "red", "vanilla:red_7")));
    // same value, different colour -> not identical.
    CHECK_FALSE(matcher(Cond("plays_identical_to_top"),
                        Attempt("vanilla:blue_7", false, "", "vanilla:red_7")));
    // same colour, different value -> not identical.
    CHECK_FALSE(matcher(Cond("plays_identical_to_top"),
                        Attempt("vanilla:red_5", false, "", "vanilla:red_7")));
    // empty discard.
    CHECK_FALSE(matcher(Cond("plays_identical_to_top"),
                        Attempt("vanilla:red_7", false)));
}

TEST_CASE("play_conditions: plays_identical_out_of_turn is the jump-in form") {
    PlayConditionMatcher matcher(Lookup());
    CHECK(matcher(Cond("plays_identical_out_of_turn"),
                  Attempt("vanilla:red_7", false, "red", "vanilla:red_7")));
    // INFO: an in-turn identical play is ordinary play, not a jump-in.
    CHECK_FALSE(matcher(Cond("plays_identical_out_of_turn"),
                        Attempt("vanilla:red_7", true, "red", "vanilla:red_7")));
    CHECK_FALSE(matcher(Cond("plays_identical_out_of_turn"),
                        Attempt("vanilla:red_5", false, "", "vanilla:red_7")));
}

// --- attempted-card identity -----------------------------------------------

TEST_CASE("play_conditions: plays_kind, tag, color and value") {
    PlayConditionMatcher matcher(Lookup());
    const PlayAttempt red5 = Attempt("vanilla:red_5", true);

    CHECK(matcher(Cond("plays_kind", {{"kind", "vanilla:red_5"}}), red5));
    CHECK(matcher(Cond("plays_kind", {{"kind", "red_5"}}), red5));
    CHECK_FALSE(matcher(Cond("plays_kind", {{"kind", "red_7"}}), red5));
    CHECK_FALSE(matcher(Cond("plays_kind", json::object()), red5));

    CHECK(matcher(Cond("plays_tag", {{"tag", "colored"}}), red5));
    CHECK_FALSE(matcher(Cond("plays_tag", {{"tag", "wild"}}), red5));
    CHECK_FALSE(matcher(Cond("plays_tag", json::object()), red5));

    CHECK(matcher(Cond("plays_color", {{"color", "red"}}), red5));
    CHECK_FALSE(matcher(Cond("plays_color", {{"color", "blue"}}), red5));
    CHECK_FALSE(matcher(Cond("plays_color", json::object()), red5));

    CHECK(matcher(Cond("plays_value", {{"value", "5"}}), red5));
    CHECK_FALSE(matcher(Cond("plays_value", {{"value", "7"}}), red5));
    CHECK_FALSE(matcher(Cond("plays_value", json::object()), red5));
}

// --- ownership -------------------------------------------------------------

TEST_CASE("play_conditions: owns_card and plays_unowned") {
    PlayConditionMatcher matcher(Lookup());
    const PlayAttempt owns =
        Attempt("vanilla:red_5", true, "", "",
                {"vanilla:red_5", "vanilla:blue_5"});
    const PlayAttempt foreign =
        Attempt("vanilla:red_5", true, "", "", {"vanilla:blue_5"});
    const PlayAttempt unknown_hand = Attempt("vanilla:red_5", true);
    CHECK(matcher(Cond("owns_card"), owns));
    CHECK_FALSE(matcher(Cond("owns_card"), foreign));
    CHECK_FALSE(matcher(Cond("owns_card"), unknown_hand));
    CHECK_FALSE(matcher(Cond("plays_unowned"), owns));
    CHECK(matcher(Cond("plays_unowned"), foreign));
    // INFO: an unknown hand never denies (must_own_card fail-safe).
    CHECK_FALSE(matcher(Cond("plays_unowned"), unknown_hand));
}

// --- no_bluffing compound --------------------------------------------------

TEST_CASE("play_conditions: plays_bluffing mirrors no_bluffing") {
    PlayConditionMatcher matcher(Lookup());
    const json condition = Cond("plays_bluffing", {{"value", "jolly_draw4"}});
    // +4 while holding a card matching the active colour -> a bluff.
    CHECK(matcher(condition,
                  Attempt("vanilla:white_wild4", true, "red", "",
                          {"vanilla:red_5"})));
    // +4 while holding nothing of the active colour -> legal.
    CHECK_FALSE(matcher(condition,
                        Attempt("vanilla:white_wild4", true, "red", "",
                                {"vanilla:blue_5"})));
    // not the +4 at all.
    CHECK_FALSE(matcher(condition,
                        Attempt("vanilla:red_5", true, "red", "",
                                {"vanilla:red_7"})));
    // no active type / no hand -> false.
    CHECK_FALSE(matcher(condition,
                        Attempt("vanilla:white_wild4", true, "", "",
                                {"vanilla:red_5"})));
    CHECK_FALSE(matcher(condition,
                        Attempt("vanilla:white_wild4", true, "red")));
    // missing arg.
    CHECK_FALSE(matcher(Cond("plays_bluffing"),
                        Attempt("vanilla:white_wild4", true, "red", "",
                                {"vanilla:red_5"})));
    // legacy no_bluffing.cpp:10 returns early for out-of-turn attempts, so an
    // otherwise-bluffing +4 is not flagged when it is a jump-in response.
    CHECK_FALSE(matcher(condition,
                        Attempt("vanilla:white_wild4", false, "red", "",
                                {"vanilla:red_5"})));
}

// --- fail-safe -------------------------------------------------------------

TEST_CASE("play_conditions: fail-safe when unbound or unresolved") {
    const PlayCardLookup lookup = Lookup();
    const PlayAttempt attempt =
        Attempt("vanilla:red_5", true, "red", "vanilla:red_7",
                {"vanilla:red_5"});
    // INFO: no PlayAttempt bound -> every play predicate is false.
    for (const auto& signature : ConditionCatalog()) {
        if (!signature.play_context) continue;
        CHECK_FALSE(EvaluatePlayCondition(Cond(signature.keyword), nullptr,
                                          lookup));
    }
    // Unknown kind -> facts predicates false.
    const PlayAttempt ghost =
        Attempt("vanilla:ghost", true, "red", "vanilla:red_7",
                {"vanilla:red_5"});
    CHECK_FALSE(EvaluatePlayCondition(Cond("plays_value", {{"value", "5"}}),
                                      &ghost, lookup));
    CHECK_FALSE(EvaluatePlayCondition(Cond("plays_mismatch"), &ghost, lookup));
    // Empty lookup -> facts predicates false; turn and ownership still work.
    const PlayCardLookup none;
    CHECK(EvaluatePlayCondition(Cond("in_turn"), &attempt, none));
    CHECK(EvaluatePlayCondition(Cond("owns_card"), &attempt, none));
    CHECK_FALSE(EvaluatePlayCondition(Cond("plays_value", {{"value", "5"}}),
                                      &attempt, none));
    CHECK_FALSE(EvaluatePlayCondition(Cond("plays_matches_top"), &attempt,
                                      none));
    // Malformed conditions and unknown keywords -> false.
    CHECK_FALSE(EvaluatePlayCondition(json(5), &attempt, lookup));
    CHECK_FALSE(EvaluatePlayCondition(json::object(), &attempt, lookup));
    CHECK_FALSE(EvaluatePlayCondition(
        json{{"plays_value", {{"value", "5"}}}, {"other", 1}}, &attempt,
        lookup));
    CHECK_FALSE(EvaluatePlayCondition(Cond("not_a_keyword"), &attempt, lookup));
}

// --- pipeline integration --------------------------------------------------

TEST_CASE("play_conditions: matcher drives the restriction pipeline") {
    PlayConditionMatcher matcher(Lookup());
    const std::vector<RestrictionEntry> entries = {
        {"vanilla:turn_order", "deny", Cond("plays_out_of_turn")},
        {"vanilla:match_type_or_value", "deny", Cond("plays_mismatch")},
        {"vanilla:must_own_card", "deny", Cond("plays_unowned")},
    };
    // Legal in-turn play of an owned, colour-matching card.
    CHECK(EvaluatePlayRestrictions(
              entries,
              Attempt("vanilla:red_5", true, "red", "vanilla:green_7",
                      {"vanilla:red_5"}),
              matcher)
              .allowed);
    // Out-of-turn play is denied by turn_order.
    const auto out = EvaluatePlayRestrictions(
        entries,
        Attempt("vanilla:red_5", false, "red", "vanilla:green_7",
                {"vanilla:red_5"}),
        matcher);
    CHECK_FALSE(out.allowed);
    CHECK(out.reason_id == "vanilla:turn_order");
    // A card matching neither colour nor value is denied by type_or_value.
    const auto mismatch = EvaluatePlayRestrictions(
        entries,
        Attempt("vanilla:blue_5", true, "red", "vanilla:green_7",
                {"vanilla:blue_5"}),
        matcher);
    CHECK_FALSE(mismatch.allowed);
    CHECK(mismatch.reason_id == "vanilla:match_type_or_value");
    // A card the player does not hold is denied by must_own_card.
    const auto foreign = EvaluatePlayRestrictions(
        entries,
        Attempt("vanilla:red_5", true, "red", "vanilla:green_7",
                {"vanilla:blue_5"}),
        matcher);
    CHECK_FALSE(foreign.allowed);
    CHECK(foreign.reason_id == "vanilla:must_own_card");
    // jump_in: deny out-of-turn, allow identical-to-top (rescues).
    const std::vector<RestrictionEntry> jump_in = {
        {"vanilla:turn_order", "deny", Cond("plays_out_of_turn")},
        {"jump_in:allow_identical", "allow",
         Cond("plays_identical_out_of_turn")},
    };
    CHECK(EvaluatePlayRestrictions(
              jump_in,
              Attempt("vanilla:red_7", false, "", "vanilla:red_7",
                      {"vanilla:red_7"}),
              matcher)
              .allowed);
    // no_bluffing: deny a bluffing +4.
    const std::vector<RestrictionEntry> no_bluff = {
        {"no_bluffing:no_bluff", "deny",
         Cond("plays_bluffing", {{"value", "jolly_draw4"}})},
    };
    const auto bluff = EvaluatePlayRestrictions(
        no_bluff,
        Attempt("vanilla:white_wild4", true, "red", "", {"vanilla:red_5"}),
        matcher);
    CHECK_FALSE(bluff.allowed);
    CHECK(bluff.reason_id == "no_bluffing:no_bluff");
    // jump_in + no_bluffing: an in-turn +4 on a +4 is not a jump-in, so the
    // jump-in allow must not rescue the bluff.
    std::vector<RestrictionEntry> both = no_bluff;
    both.insert(both.end(), jump_in.begin(), jump_in.end());
    const auto bluff_on_four = EvaluatePlayRestrictions(
        both,
        Attempt("vanilla:white_wild4", true, "red", "vanilla:white_wild4",
                {"vanilla:white_wild4", "vanilla:red_5"}),
        matcher);
    CHECK_FALSE(bluff_on_four.allowed);
    CHECK(bluff_on_four.reason_id == "no_bluffing:no_bluff");
}

// --- validator accept / reject ---------------------------------------------

TEST_CASE("validator: accepts every new play-context keyword") {
    const std::vector<json> accepted = {
        Cond("in_turn"),
        Cond("plays_out_of_turn"),
        Cond("plays_matches_active"),
        Cond("plays_matches_top"),
        Cond("plays_mismatch"),
        Cond("plays_identical_to_top"),
        Cond("plays_identical_out_of_turn"),
        Cond("plays_kind", {{"kind", "alpha:c1"}}),
        Cond("plays_tag", {{"tag", "stackable"}}),
        Cond("plays_color", {{"color", "red"}}),
        Cond("plays_value", {{"value", "5"}}),
        Cond("owns_card"),
        Cond("plays_unowned"),
        Cond("plays_bluffing", {{"value", "jolly_draw4"}}),
        Cond("plays_stack_response", {{"tag", "stackable"}}),
    };
    SemanticValidator validator(SchemaDir());
    for (const json& condition : accepted) {
        const auto errors = validator.ValidateMod(PlayMod(condition));
        CHECK_MESSAGE(errors.empty(), condition.dump());
    }
}

TEST_CASE("validator: rejects malformed play-context conditions") {
    SemanticValidator validator(SchemaDir());
    // Unknown keyword.
    CHECK(HasCheck(validator.ValidateMod(PlayMod(Cond("plays_mystery"))),
                   "op.unknown"));
    // No-arg predicate with arguments.
    CHECK(HasCheck(
        validator.ValidateMod(
            PlayMod(json{{"plays_mismatch", {{"bogus", 1}}}})),
        "op.arity"));
    // Required arg missing.
    CHECK(HasCheck(
        validator.ValidateMod(PlayMod(json{{"plays_value", json::object()}})),
        "op.arity"));
    // Wrong arg type.
    CHECK(HasCheck(
        validator.ValidateMod(PlayMod(json{{"plays_value", {{"value", 5}}}})),
        "op.type"));
    // Unknown extra arg.
    CHECK(HasCheck(
        validator.ValidateMod(PlayMod(
            json{{"plays_value", {{"value", "5"}, {"extra", 1}}}})),
        "op.arity"));
}
