#include <doctest/doctest.h>
#include <match/modload/restriction.hpp>
#include <match/modload/semantic_validator.hpp>
#include <match/modload/vocabulary.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace match::modload;
using nlohmann::json;

namespace {

/* INFO: locate contract/schemas by walking up from this test file so the
 *       suite works regardless of the ctest working directory. */
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

bool HasCheck(const std::vector<LoadError>& errors, const std::string& check) {
    for (const auto& e : errors) {
        if (e.check == check) return true;
    }
    return false;
}

json Op(const std::string& id, const std::string& name, json args,
        const std::string& next = "") {
    json node = {{"id", id}, {"op", name}, {"args", std::move(args)}};
    if (!next.empty()) node["next"] = next;
    return node;
}

json GraphJson(json nodes) {
    return json{{"nodes", std::move(nodes)}};
}

json HookRaw(const std::string& on, const std::string& phase, json where,
             json nodes) {
    json hook = {{"on", on}, {"nodes", std::move(nodes)}};
    if (!phase.empty()) hook["phase"] = phase;
    if (!where.is_null()) hook["where"] = std::move(where);
    return hook;
}

BehaviorGraph MakeGraph(json nodes) {
    BehaviorGraph g;
    if (!nodes.is_array()) nodes = json::array();
    g.nodes = nodes;
    g.raw = GraphJson(nodes);
    return g;
}

/* INFO: one valid mod with a manifest and a single playable card. Tests
 *       extend or mutate a copy; every artifact carries schema-valid raw. */
LoadedMod ValidMod(const std::string& id = "alpha") {
    LoadedMod mod;
    mod.folder = id;
    mod.path = "/tmp/uni_validator/" + id;
    mod.manifest.id = id;
    mod.manifest.name = "Alpha";
    mod.manifest.version = "1.0.0";
    mod.manifest.api = "1";
    mod.manifest.raw = {{"id", id},
                        {"name", "Alpha"},
                        {"version", "1.0.0"},
                        {"api", "1"}};

    CardDef card;
    card.id = "c1";
    card.namespace_id = id;
    card.kind_id = id + ":c1";
    card.title = "C1";
    card.face.kind = FaceKind::kText;
    card.raw = {{"id", "c1"},
                {"title", "C1"},
                {"face",
                 {{"kind", "text"}, {"color", "red"}, {"label", "1"}}},
                {"tags", json::array({"stackable"})}};
    card.tags = {"stackable"};

    BehaviorEntry be;
    be.hook = "on_play";
    be.phase = "after";
    be.graph = MakeGraph({Op("n1", "draw_cards",
                             {{"target", "@next_player"}, {"n", 4}}, "n2"),
                          Op("n2", "advance_turn", json::object())});
    card.behaviors.push_back(be);
    card.raw["behavior"] = json{{"on_play", be.graph.raw}};
    mod.cards.push_back(card);
    return mod;
}

/* INFO: attach a rule with the given hook graph to a mod. */
void AddRule(LoadedMod& mod,
             const std::string& id,
             const std::string& on,
             const std::string& phase,
             json where,
             json nodes) {
    RuleDef rule;
    rule.id = id;
    rule.namespace_id = mod.manifest.id;
    rule.rule_id = mod.manifest.id + ":" + id;
    rule.title = id;
    BehaviorEntry hook;
    hook.hook = on;
    if (!phase.empty()) hook.phase = phase;
    if (!where.is_null()) hook.where = where;
    hook.graph = MakeGraph(nodes);
    rule.hooks.push_back(hook);
    rule.raw = {{"id", id},
                {"title", id},
                {"hooks", json::array({HookRaw(on, phase, where,
                                               hook.graph.nodes)})}};
    mod.rules.push_back(std::move(rule));
}

void AddStatus(LoadedMod& mod,
               const std::string& id,
               const std::string& stack_policy) {
    StatusDef status;
    status.id = id;
    status.namespace_id = mod.manifest.id;
    status.status_id = mod.manifest.id + ":" + id;
    status.title = id;
    status.stack_policy = stack_policy;
    status.raw = {{"id", id}, {"stack_policy", stack_policy}};
    mod.statuses.push_back(std::move(status));
}

MutationDef MakeMutation(const std::string& ns,
                         const std::string& id,
                         const std::string& target,
                         const std::string& mode,
                         std::initializer_list<json> nodes) {
    MutationDef mut;
    mut.id = id;
    mut.namespace_id = ns;
    mut.mutation_id = ns + ":" + id;
    mut.target = target;
    mut.mode = mode;
    mut.replacement = MakeGraph(nodes);
    mut.raw = {{"id", id},
               {"target", target},
               {"mode", mode},
               {"replacement", mut.replacement.raw}};
    return mut;
}

DeckDef MakeDeck(const std::string& ns,
                 const std::string& id,
                 std::vector<std::string> mods,
                 std::vector<std::pair<std::string, int>> cards,
                 json settings) {
    DeckDef deck;
    deck.id = id;
    deck.namespace_id = ns;
    deck.deck_id = ns + ":" + id;
    deck.name = id;
    deck.mods = std::move(mods);
    deck.cards = std::move(cards);
    deck.settings = std::move(settings);
    json raw_cards = json::object();
    for (const auto& [kind, count] : deck.cards) raw_cards[kind] = count;
    deck.raw = {{"id", id},
                {"name", id},
                {"mods", deck.mods},
                {"cards", raw_cards}};
    if (!deck.settings.empty()) deck.raw["settings"] = deck.settings;
    return deck;
}

}  // namespace

// --- check 1: JSON Schema conformance --------------------------------------

TEST_CASE("validator: schema subset accepts a valid mod") {
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(ValidMod());
    CHECK_FALSE(HasCheck(errors, "schema.invalid"));
    CHECK_FALSE(HasCheck(errors, "schema.unsupported"));
    CHECK_FALSE(HasCheck(errors, "schema.missing"));
    CHECK(errors.empty());
}

TEST_CASE("validator: schema.invalid for a card missing required id") {
    LoadedMod mod = ValidMod();
    mod.cards[0].raw.erase("id");
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "schema.invalid"));
}

TEST_CASE("validator: schema.missing when the schema directory is empty") {
    SemanticValidator v("");
    auto errors = v.ValidateMod(ValidMod());
    CHECK(HasCheck(errors, "schema.missing"));
}

TEST_CASE("schema subset: supported and unsupported keywords") {
    JsonSchemaSubsetValidator validator;
    std::string error;

    json schema = {{"type", "object"},
                   {"required", json::array({"id"})},
                   {"properties", {{"id", {{"type", "string"},
                                           {"pattern", "^[a-z]+$"}}}}}};
    REQUIRE(JsonSchemaSubsetValidator::Compile(schema, validator, error));
    CHECK(validator.Validate({{"id", "abc"}}).empty());
    CHECK_FALSE(validator.Validate({{"id", "ABC"}}).empty());
    CHECK_FALSE(validator.Validate(json::object()).empty());

    JsonSchemaSubsetValidator bad;
    json unsupported = {{"type", "string"}, {"format", "email"}};
    CHECK_FALSE(JsonSchemaSubsetValidator::Compile(unsupported, bad, error));
    CHECK(error.find("format") != std::string::npos);
}

TEST_CASE("schema subset: oneOf, $ref, enum, const, propertyNames") {
    JsonSchemaSubsetValidator validator;
    std::string error;
    json schema = {
        {"oneOf",
         json::array({{{"type", "array"},
                       {"items", {{"$ref", "#/$defs/entry"}}}},
                      {{"type", "object"},
                       {"propertyNames", {{"pattern", "^[a-z]+$"}}}}})},
        {"$defs",
         {{"entry",
           {{"type", "object"},
            {"required", json::array({"kind"})},
            {"properties",
             {{"kind", {{"enum", json::array({"a", "b"})}}},
              {"fixed", {{"const", 7}}}}}}}}}};
    REQUIRE(JsonSchemaSubsetValidator::Compile(schema, validator, error));

    CHECK(validator.Validate(json::array({json{{"kind", "a"}}})).empty());
    CHECK_FALSE(
        validator.Validate(json::array({json{{"kind", "c"}}})).empty());
    CHECK(validator.Validate(json{{"ok", 1}}).empty());
    CHECK_FALSE(validator.Validate(json{{"Bad", 1}}).empty());
}

TEST_CASE("schema subset: bounds, additionalProperties, min/maxLength") {
    JsonSchemaSubsetValidator validator;
    std::string error;
    json schema = {{"type", "object"},
                   {"additionalProperties", {{"type", "integer"},
                                             {"minimum", 0},
                                             {"maximum", 10}}}};
    REQUIRE(JsonSchemaSubsetValidator::Compile(schema, validator, error));
    CHECK(validator.Validate(json{{"n", 5}}).empty());
    CHECK_FALSE(validator.Validate(json{{"n", 11}}).empty());
    CHECK_FALSE(validator.Validate(json{{"n", -1}}).empty());

    JsonSchemaSubsetValidator s2;
    json len = {{"type", "string"}, {"minLength", 2}, {"maxLength", 3}};
    REQUIRE(JsonSchemaSubsetValidator::Compile(len, s2, error));
    CHECK(s2.Validate("ab").empty());
    CHECK_FALSE(s2.Validate("a").empty());
    CHECK_FALSE(s2.Validate("abcd").empty());
}

// --- check 2: id syntax + uniqueness ---------------------------------------

TEST_CASE("validator: id.syntax for an invalid card id") {
    LoadedMod mod = ValidMod();
    mod.cards[0].id = "Bad-Id";
    mod.cards[0].kind_id = "alpha:Bad-Id";
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "id.syntax"));
}

TEST_CASE("validator: id.duplicate for repeated kinds in one namespace") {
    LoadedMod mod = ValidMod();
    mod.cards.push_back(mod.cards[0]);
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "id.duplicate"));
}

TEST_CASE("validator: id.syntax for a bad restriction entry id") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph = MakeGraph(
        {Op("n1", "add_restriction",
            {{"entry_def",
              {{"id", "no_namespace"},
               {"phase", "deny"},
               {"condition", json{{"always", json::object()}}}}}})});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "id.syntax"));
}

// --- check 3: reference resolution -----------------------------------------

TEST_CASE("validator: unknown mutation target kind is ref.kind") {
    LoadedMod mod = ValidMod();
    mod.mutations.push_back(MakeMutation(
        "alpha", "m1", "alpha:missing", "replace",
        {Op("n1", "advance_turn", json::object())}));
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "ref.kind"));
}

TEST_CASE("validator: known mutation target kind passes") {
    LoadedMod mod = ValidMod();
    mod.mutations.push_back(MakeMutation(
        "alpha", "m1", "alpha:c1", "replace",
        {Op("n1", "advance_turn", json::object())}));
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(errors.empty());
}

TEST_CASE("validator: unknown status ref is ref.status") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph = MakeGraph(
        {Op("n1", "apply_status",
            {{"target", "@self"}, {"status_kind", "alpha:ghost"}})});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "ref.status"));
}

TEST_CASE("validator: known status ref passes") {
    LoadedMod mod = ValidMod();
    AddStatus(mod, "shielded", "replace");
    mod.cards[0].behaviors[0].graph = MakeGraph(
        {Op("n1", "apply_status",
            {{"target", "@self"}, {"status_kind", "alpha:shielded"}})});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(errors.empty());
}

TEST_CASE("validator: dangling graph route is ref.graph") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph =
        MakeGraph({Op("n1", "advance_turn", json::object(), "n9")});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "ref.graph"));
}

TEST_CASE("validator: from_prompt node ref must resolve") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph = MakeGraph(
        {Op("n1", "set_active_type", {{"from_prompt", "nope"}})});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "ref.graph"));
}

TEST_CASE("validator: unknown restriction ref is ref.restriction") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph = MakeGraph(
        {Op("n1", "remove_restriction", {{"entry_id", "alpha:ghost"}})});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "ref.restriction"));
}

TEST_CASE("validator: declared restriction ref resolves") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph = MakeGraph(
        {Op("n1", "add_restriction",
            {{"entry_def",
              {{"id", "alpha:jump"},
               {"phase", "allow"},
               {"condition", json{{"always", json::object()}}}}}},
            "n2"),
         Op("n2", "remove_restriction", {{"entry_id", "alpha:jump"}})});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(errors.empty());
}

TEST_CASE("validator: unresolved tag ref is ref.tag in a match set") {
    LoadedMod mod = ValidMod();
    AddRule(mod, "r1", "before:play", "before",
            json{{"has_card_tag", {{"target", "@self"}, {"tag", "ghost"}}}},
            {Op("n1", "advance_turn", json::object())});
    std::vector<LoadedMod> mods = {mod};
    DeckDef deck = MakeDeck("alpha", "d", {"alpha"}, {{"alpha:c1", 1}}, json::object());
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMatchSet(mods, deck);
    CHECK(HasCheck(errors, "ref.tag"));
}

TEST_CASE("validator: declared tag ref resolves in a match set") {
    LoadedMod mod = ValidMod();
    AddRule(mod, "r1", "before:play", "before",
            json{{"has_card_tag", {{"target", "@self"}, {"tag", "stackable"}}}},
            {Op("n1", "advance_turn", json::object())});
    std::vector<LoadedMod> mods = {mod};
    DeckDef deck = MakeDeck("alpha", "d", {"alpha"}, {{"alpha:c1", 1}}, json::object());
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMatchSet(mods, deck);
    CHECK(errors.empty());
}

// --- check 4: op arity / type ----------------------------------------------

TEST_CASE("validator: unknown op is op.unknown") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph =
        MakeGraph({Op("n1", "teleport", json::object())});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "op.unknown"));
}

TEST_CASE("validator: missing required arg is op.arity") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph =
        MakeGraph({Op("n1", "draw_cards", {{"n", 4}})});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "op.arity"));
}

TEST_CASE("validator: unknown extra arg is op.arity") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph = MakeGraph(
        {Op("n1", "advance_turn", {{"bonus", 1}})});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "op.arity"));
}

TEST_CASE("validator: wrong arg type is op.type") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph =
        MakeGraph({Op("n1", "draw_cards", {{"target", 5}, {"n", 4}})});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "op.type"));
}

TEST_CASE("validator: out-of-bounds int is op.bounds") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph = MakeGraph(
        {Op("n1", "draw_cards", {{"target", "@self"}, {"n", 5000}})});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "op.bounds"));
}

TEST_CASE("validator: node without an id is graph.node") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph = MakeGraph(
        {json{{"op", "advance_turn"}, {"args", json::object()}}});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "graph.node"));
}

TEST_CASE("validator: unknown hook name is hook.name") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].hook = "on_banana";
    mod.cards[0].raw["behavior"] = json{
        {"on_banana", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "hook.name"));
}

TEST_CASE("validator: condition with unknown keyword is op.unknown") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].where =
        json{{"mystery", {{"target", "@self"}}}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "op.unknown"));
}

// --- check 5: graph structure ----------------------------------------------

TEST_CASE("validator: cyclic edges are graph.cycle") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph =
        MakeGraph({Op("n1", "advance_turn", json::object(), "n2"),
                   Op("n2", "advance_turn", json::object(), "n1")});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "graph.cycle"));
}

TEST_CASE("validator: unreachable node is graph.unreachable") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph =
        MakeGraph({Op("n1", "advance_turn", json::object(), "n2"),
                   Op("n2", "advance_turn", json::object()),
                   Op("dead", "advance_turn", json::object())});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "graph.unreachable"));
}

TEST_CASE("validator: window node without a default route") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph = MakeGraph(
        {json{{"id", "w1"},
              {"window", json::object()},
              {"on_response", {{"x", "n2"}}}},
         Op("n2", "advance_turn", json::object())});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "window.default"));
}

TEST_CASE("validator: call_original outside a wrap mutation") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph =
        MakeGraph({Op("n1", "call_original", json::object())});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "graph.call_original"));
}

TEST_CASE("validator: call_original inside a wrap mutation is allowed") {
    LoadedMod mod = ValidMod();
    mod.mutations.push_back(MakeMutation(
        "alpha", "m1", "alpha:c1", "wrap",
        {Op("n1", "call_original", json::object())}));
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(errors.empty());
}

// --- check 6: mutation conflicts -------------------------------------------

TEST_CASE("validator: replace plus replace on one target conflicts") {
    LoadedMod alpha = ValidMod("alpha");
    LoadedMod beta = ValidMod("beta");
    alpha.mutations.push_back(MakeMutation(
        "alpha", "m1", "alpha:c1", "replace",
        {Op("n1", "advance_turn", json::object())}));
    beta.mutations.push_back(MakeMutation(
        "beta", "m2", "alpha:c1", "replace",
        {Op("n1", "advance_turn", json::object())}));
    std::vector<LoadedMod> mods = {alpha, beta};
    DeckDef deck = MakeDeck("alpha", "d", {"alpha", "beta"},
                            {{"alpha:c1", 1}, {"beta:c1", 1}}, json::object());
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMatchSet(mods, deck);
    REQUIRE(HasCheck(errors, "mutation.conflict"));
    std::string message;
    for (const auto& e : errors) {
        if (e.check == "mutation.conflict") message = e.message;
    }
    CHECK(message.find("alpha") != std::string::npos);
    CHECK(message.find("beta") != std::string::npos);
    CHECK(message.find("alpha:c1") != std::string::npos);
}

TEST_CASE("validator: replace plus wrap on one target conflicts") {
    LoadedMod alpha = ValidMod("alpha");
    LoadedMod beta = ValidMod("beta");
    alpha.mutations.push_back(MakeMutation(
        "alpha", "m1", "alpha:c1", "replace",
        {Op("n1", "advance_turn", json::object())}));
    beta.mutations.push_back(MakeMutation(
        "beta", "m2", "alpha:c1", "wrap",
        {Op("n1", "call_original", json::object())}));
    std::vector<LoadedMod> mods = {alpha, beta};
    DeckDef deck = MakeDeck("alpha", "d", {"alpha", "beta"},
                            {{"alpha:c1", 1}, {"beta:c1", 1}}, json::object());
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMatchSet(mods, deck);
    CHECK(HasCheck(errors, "mutation.conflict"));
}

TEST_CASE("validator: wrap plus wrap composes") {
    LoadedMod alpha = ValidMod("alpha");
    LoadedMod beta = ValidMod("beta");
    alpha.mutations.push_back(MakeMutation(
        "alpha", "m1", "alpha:c1", "wrap",
        {Op("n1", "call_original", json::object())}));
    beta.mutations.push_back(MakeMutation(
        "beta", "m2", "alpha:c1", "wrap",
        {Op("n1", "call_original", json::object())}));
    std::vector<LoadedMod> mods = {alpha, beta};
    DeckDef deck = MakeDeck("alpha", "d", {"alpha", "beta"},
                            {{"alpha:c1", 1}, {"beta:c1", 1}}, json::object());
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMatchSet(mods, deck);
    CHECK_FALSE(HasCheck(errors, "mutation.conflict"));
    CHECK(errors.empty());
}

TEST_CASE("validator: filter plus filter composes") {
    LoadedMod alpha = ValidMod("alpha");
    LoadedMod beta = ValidMod("beta");
    alpha.mutations.push_back(MakeMutation(
        "alpha", "m1", "alpha:c1", "filter",
        {Op("n1", "advance_turn", json::object())}));
    beta.mutations.push_back(MakeMutation(
        "beta", "m2", "alpha:c1", "filter",
        {Op("n1", "advance_turn", json::object())}));
    std::vector<LoadedMod> mods = {alpha, beta};
    DeckDef deck = MakeDeck("alpha", "d", {"alpha", "beta"},
                            {{"alpha:c1", 1}, {"beta:c1", 1}}, json::object());
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMatchSet(mods, deck);
    CHECK_FALSE(HasCheck(errors, "mutation.conflict"));
    CHECK(errors.empty());
}

TEST_CASE("validator: veto coexists with replace") {
    LoadedMod alpha = ValidMod("alpha");
    LoadedMod beta = ValidMod("beta");
    alpha.mutations.push_back(MakeMutation(
        "alpha", "m1", "alpha:c1", "replace",
        {Op("n1", "advance_turn", json::object())}));
    MutationDef veto = MakeMutation(
        "beta", "m2", "alpha:c1", "veto",
        {Op("n1", "advance_turn", json::object())});
    veto.where = json{{"always", json::object()}};
    veto.raw["where"] = *veto.where;
    beta.mutations.push_back(veto);
    std::vector<LoadedMod> mods = {alpha, beta};
    DeckDef deck = MakeDeck("alpha", "d", {"alpha", "beta"},
                            {{"alpha:c1", 1}, {"beta:c1", 1}}, json::object());
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMatchSet(mods, deck);
    CHECK_FALSE(HasCheck(errors, "mutation.conflict"));
    CHECK(errors.empty());
}

TEST_CASE("validator: mutation targeting a restriction another mod removes") {
    LoadedMod alpha = ValidMod("alpha");
    LoadedMod beta = ValidMod("beta");
    /* INFO: beta declares the entry and mutates it; alpha removes it, so the
     *       mutation targets an entry another mod removes. */
    alpha.cards[0].behaviors[0].graph = MakeGraph(
        {Op("n1", "remove_restriction", {{"entry_id", "beta:turn_order"}})});
    alpha.cards[0].raw["behavior"] = json{
        {"on_play", alpha.cards[0].behaviors[0].graph.raw}};
    beta.cards[0].behaviors[0].graph = MakeGraph(
        {Op("n1", "add_restriction",
            {{"entry_def",
              {{"id", "beta:turn_order"},
               {"phase", "deny"},
               {"condition", json{{"always", json::object()}}}}}})});
    beta.cards[0].raw["behavior"] = json{
        {"on_play", beta.cards[0].behaviors[0].graph.raw}};
    beta.mutations.push_back(MakeMutation(
        "beta", "m2", "beta:turn_order", "replace",
        {Op("n1", "advance_turn", json::object())}));
    std::vector<LoadedMod> mods = {alpha, beta};
    DeckDef deck = MakeDeck("alpha", "d", {"alpha", "beta"},
                            {{"alpha:c1", 1}, {"beta:c1", 1}}, json::object());
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMatchSet(mods, deck);
    CHECK(HasCheck(errors, "mutation.conflict"));
}

// --- check 7: deck validation ----------------------------------------------

TEST_CASE("validator: unknown deck kind is deck.kind") {
    LoadedMod alpha = ValidMod("alpha");
    std::vector<LoadedMod> mods = {alpha};
    DeckDef deck = MakeDeck("alpha", "d", {"alpha"}, {{"alpha:ghost", 1}},
                            json::object());
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMatchSet(mods, deck);
    CHECK(HasCheck(errors, "deck.kind"));
}

TEST_CASE("validator: deck count above index-space bound is deck.count") {
    LoadedMod alpha = ValidMod("alpha");
    std::vector<LoadedMod> mods = {alpha};
    DeckDef deck = MakeDeck("alpha", "d", {"alpha"}, {{"alpha:c1", 5000}},
                            json::object());
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMatchSet(mods, deck);
    CHECK(HasCheck(errors, "deck.count"));
}

TEST_CASE("validator: deck referencing an absent mod is deck.mod") {
    LoadedMod alpha = ValidMod("alpha");
    std::vector<LoadedMod> mods = {alpha};
    DeckDef deck = MakeDeck("alpha", "d", {"alpha", "ghost"},
                            {{"alpha:c1", 1}}, json::object());
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMatchSet(mods, deck);
    CHECK(HasCheck(errors, "deck.mod"));
}

TEST_CASE("validator: unknown deck setting is deck.setting") {
    LoadedMod alpha = ValidMod("alpha");
    std::vector<LoadedMod> mods = {alpha};
    DeckDef deck = MakeDeck("alpha", "d", {"alpha"}, {{"alpha:c1", 1}},
                            json{{"mystery", 1}});
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMatchSet(mods, deck);
    CHECK(HasCheck(errors, "deck.setting"));
}

TEST_CASE("validator: deck setting type mismatch is deck.setting") {
    LoadedMod alpha = ValidMod("alpha");
    SettingDecl decl;
    decl.id = "score_limit";
    decl.type = "int";
    decl.default_value = 500;
    decl.raw = {{"id", "score_limit"}, {"type", "int"}, {"default", 500}};
    alpha.manifest.settings.push_back(decl);
    alpha.manifest.raw["settings"] = json::array({decl.raw});
    std::vector<LoadedMod> mods = {alpha};
    DeckDef deck = MakeDeck("alpha", "d", {"alpha"}, {{"alpha:c1", 1}},
                            json{{"score_limit", "lots"}});
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMatchSet(mods, deck);
    CHECK(HasCheck(errors, "deck.setting"));
}

TEST_CASE("validator: deck setting range breach is deck.setting") {
    LoadedMod alpha = ValidMod("alpha");
    SettingDecl decl;
    decl.id = "score_limit";
    decl.type = "int";
    decl.default_value = 500;
    decl.range = json{{"min", 0}, {"max", 1000}};
    decl.raw = {{"id", "score_limit"},
                {"type", "int"},
                {"default", 500},
                {"range", {{"min", 0}, {"max", 1000}}}};
    alpha.manifest.settings.push_back(decl);
    alpha.manifest.raw["settings"] = json::array({decl.raw});
    std::vector<LoadedMod> mods = {alpha};
    DeckDef deck = MakeDeck("alpha", "d", {"alpha"}, {{"alpha:c1", 1}},
                            json{{"score_limit", 5000}});
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMatchSet(mods, deck);
    CHECK(HasCheck(errors, "deck.setting"));
}

TEST_CASE("validator: valid deck with a declared setting passes") {
    LoadedMod alpha = ValidMod("alpha");
    SettingDecl decl;
    decl.id = "score_limit";
    decl.type = "int";
    decl.default_value = 500;
    decl.range = json{{"min", 0}, {"max", 1000}};
    decl.raw = {{"id", "score_limit"},
                {"type", "int"},
                {"default", 500},
                {"range", {{"min", 0}, {"max", 1000}}}};
    alpha.manifest.settings.push_back(decl);
    alpha.manifest.raw["settings"] = json::array({decl.raw});
    std::vector<LoadedMod> mods = {alpha};
    DeckDef deck = MakeDeck("alpha", "d", {"alpha"}, {{"alpha:c1", 1}},
                            json{{"score_limit", 500}});
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMatchSet(mods, deck);
    CHECK(errors.empty());
}

// --- check 8: selector sanity ----------------------------------------------

TEST_CASE("validator: unknown selector is selector.unknown") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph = MakeGraph(
        {Op("n1", "draw_cards", {{"target", "@nobody"}, {"n", 1}})});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "selector.unknown"));
}

TEST_CASE("validator: @responder outside a window route is selector.scope") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph = MakeGraph(
        {Op("n1", "skip_turn", {{"target", "@responder"}})});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "selector.scope"));
}

TEST_CASE("validator: @responder inside a window route is allowed") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph = MakeGraph(
        {json{{"id", "w1"},
              {"window", json::object()},
              {"default", "n2"},
              {"on_response", {{"any", "n3"}}}},
         Op("n2", "advance_turn", json::object()),
         Op("n3", "skip_turn", {{"target", "@responder"}})});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK_FALSE(HasCheck(errors, "selector.scope"));
    CHECK(errors.empty());
}

TEST_CASE("validator: window responders must be a known selector") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph = MakeGraph(
        {json{{"id", "w1"},
              {"window",
               {{"responders", "@nobody"}, {"duration", "env"}}},
              {"default", "n2"}},
         Op("n2", "advance_turn", json::object())});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "selector.unknown"));
}

TEST_CASE("validator: window duration must be env or a duration unit") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph = MakeGraph(
        {json{{"id", "w1"},
              {"window",
               {{"responders", "@others"}, {"duration", "fortnight"}}},
              {"default", "n2"}},
         Op("n2", "advance_turn", json::object())});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "op.type"));
}

TEST_CASE("validator: window with declared fields validates clean") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph = MakeGraph(
        {json{{"id", "w1"},
              {"window",
               {{"responders", "@others"},
                {"respond_with", {{"any_tag", json::array({"stackable"})}}},
                {"duration", "env"}}},
              {"default", "n2"},
              {"on_response", {{"stackable", "n3"}}}},
         Op("n2", "advance_turn", json::object()),
         Op("n3", "skip_turn", {{"target", "@responder"}})});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(errors.empty());
}

TEST_CASE("validator: window on_response inside the window object resolves") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph = MakeGraph(
        {json{{"id", "w1"},
              {"window",
               {{"responders", "@others"},
                {"duration", "env"},
                {"on_response", {{"stackable", "ghost"}}}}},
              {"default", "n2"}},
         Op("n2", "advance_turn", json::object())});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "ref.graph"));
}

TEST_CASE("validator: @card in a rule graph is selector.scope") {
    LoadedMod mod = ValidMod();
    AddRule(mod, "r1", "before:play", "before", json(),
            {Op("n1", "move_card",
                {{"card", "@card"}, {"to_zone", "discard_pile"}})});
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "selector.scope"));
}

TEST_CASE("validator: @card in card behavior is allowed") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph = MakeGraph(
        {Op("n1", "move_card",
            {{"card", "@card"}, {"to_zone", "discard_pile"}})});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(errors.empty());
}

TEST_CASE("validator: drawn-card vocabulary validates clean") {
    LoadedMod mod = ValidMod();
    AddRule(mod, "force", "after:draw", "after", json(),
            {json{{"id", "n1"},
                  {"cases",
                   json::array(
                       {json{{"when",
                              json{{"drawn_card_playable", json::object()}}},
                             {"next", "n2"}},
                        json{{"when",
                              json{{"player_count",
                                    {{"cmp", "eq"}, {"n", 2}}}}},
                             {"next", "n3"}}})},
                  {"else", "n3"}},
             json{{"id", "n2"},
                  {"op", "play_card"},
                  {"args",
                   json{{"card", "@drawn_card"}, {"player", "@self"}}}},
             json{{"id", "n3"}, {"op", "advance_turn"}}});
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(errors.empty());
}

TEST_CASE("validator: player_count n is bounds-checked") {
    LoadedMod mod = ValidMod();
    AddRule(mod, "count", "after:draw", "after", json(),
            {json{{"id", "n1"},
                  {"cases",
                   json::array({json{
                       {"when",
                        json{{"player_count",
                              {{"cmp", "eq"}, {"n", 1001}}}}},
                       {"next", "n2"}}})},
                  {"else", "n2"}},
             json{{"id", "n2"}, {"op", "advance_turn"}}});
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "op.bounds"));
}

// --- restriction pipeline

namespace {
ConditionMatcher MatchAll() {
    return [](const json&, const PlayAttempt&) { return true; };
}
ConditionMatcher MatchNone() {
    return [](const json&, const PlayAttempt&) { return false; };
}
}  // namespace

TEST_CASE("restriction: deny then allow rescues") {
    std::vector<RestrictionEntry> entries = {
        {"vanilla:turn_order", "deny", json{{"always", json::object()}}},
        {"mod:jump_in", "allow", json{{"always", json::object()}}},
    };
    PlayAttempt attempt;
    auto decision = EvaluatePlayRestrictions(entries, attempt, MatchAll());
    CHECK(decision.allowed);
    CHECK(decision.reason_id.empty());
}

TEST_CASE("restriction: first matching deny supplies the reason") {
    std::vector<RestrictionEntry> entries = {
        {"vanilla:turn_order", "deny", json{{"always", json::object()}}},
        {"vanilla:match_type", "deny", json{{"always", json::object()}}},
    };
    PlayAttempt attempt;
    auto decision = EvaluatePlayRestrictions(entries, attempt, MatchAll());
    CHECK_FALSE(decision.allowed);
    CHECK(decision.reason_id == "vanilla:turn_order");
}

TEST_CASE("restriction: no matching entries allows") {
    std::vector<RestrictionEntry> entries = {
        {"vanilla:turn_order", "deny", json{{"always", json::object()}}},
    };
    PlayAttempt attempt;
    auto decision = EvaluatePlayRestrictions(entries, attempt, MatchNone());
    CHECK(decision.allowed);
}

TEST_CASE("restriction: null condition matches unconditionally") {
    std::vector<RestrictionEntry> entries = {
        {"vanilla:must_own_card", "deny", json()},
    };
    PlayAttempt attempt;
    auto decision = EvaluatePlayRestrictions(entries, attempt, MatchNone());
    CHECK_FALSE(decision.allowed);
    CHECK(decision.reason_id == "vanilla:must_own_card");
}

TEST_CASE("restriction: parse entry validates shape") {
    RestrictionEntry entry;
    std::string error;
    REQUIRE(ParseRestrictionEntry(
        json{{"id", "alpha:jump"},
             {"phase", "allow"},
             {"condition", json{{"always", json::object()}}}},
        entry, error));
    CHECK(entry.id == "alpha:jump");
    CHECK(entry.phase == "allow");

    CHECK_FALSE(ParseRestrictionEntry(
        json{{"id", "alpha:jump"}, {"phase", "maybe"}}, entry, error));
    CHECK_FALSE(ParseRestrictionEntry(
        json{{"id", "bad"}, {"phase", "allow"}}, entry, error));
}

// --- the validator review fixes
// ------------------------------------------------------

TEST_CASE("validator: no engine-embedded restriction ids (undeclared vanilla)") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph = MakeGraph(
        {Op("n1", "remove_restriction", {{"entry_id", "vanilla:turn_order"}})});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    std::vector<LoadedMod> mods = {mod};
    DeckDef deck = MakeDeck("alpha", "d", {"alpha"}, {{"alpha:c1", 1}},
                            json::object());
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMatchSet(mods, deck);
    CHECK(HasCheck(errors, "ref.restriction"));
}

TEST_CASE("validator: mutation target cannot be an engine-embedded restriction") {
    LoadedMod mod = ValidMod();
    mod.mutations.push_back(MakeMutation(
        "alpha", "m1", "vanilla:turn_order", "replace",
        {Op("n1", "advance_turn", json::object())}));
    std::vector<LoadedMod> mods = {mod};
    DeckDef deck = MakeDeck("alpha", "d", {"alpha"}, {{"alpha:c1", 1}},
                            json::object());
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMatchSet(mods, deck);
    CHECK(HasCheck(errors, "ref.kind"));
}

TEST_CASE("validator: mod-declared restriction entry still resolves") {
    LoadedMod mod = ValidMod();
    mod.cards[0].behaviors[0].graph = MakeGraph(
        {Op("n1", "add_restriction",
            {{"entry_def",
              {{"id", "alpha:own_entry"},
               {"phase", "deny"},
               {"condition", json{{"always", json::object()}}}}}},
            "n2"),
         Op("n2", "remove_restriction", {{"entry_id", "alpha:own_entry"}})});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    std::vector<LoadedMod> mods = {mod};
    DeckDef deck = MakeDeck("alpha", "d", {"alpha"}, {{"alpha:c1", 1}},
                            json::object());
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMatchSet(mods, deck);
    CHECK(errors.empty());
}

TEST_CASE("validator: local-form remove_restriction fires the conflict gate") {
    LoadedMod alpha = ValidMod("alpha");
    LoadedMod beta = ValidMod("beta");
    /* INFO: beta declares `beta:turn_order` and removes it via the local form
     *       `"turn_order"`; alpha mutates the entry, so the gate must fire. */
    beta.cards[0].behaviors[0].graph = MakeGraph(
        {Op("n1", "add_restriction",
            {{"entry_def",
              {{"id", "beta:turn_order"},
               {"phase", "deny"},
               {"condition", json{{"always", json::object()}}}}}},
            "n2"),
         Op("n2", "remove_restriction", {{"entry_id", "turn_order"}})});
    beta.cards[0].raw["behavior"] = json{
        {"on_play", beta.cards[0].behaviors[0].graph.raw}};
    alpha.mutations.push_back(MakeMutation(
        "alpha", "m1", "beta:turn_order", "replace",
        {Op("n1", "advance_turn", json::object())}));
    std::vector<LoadedMod> mods = {alpha, beta};
    DeckDef deck = MakeDeck("alpha", "d", {"alpha", "beta"},
                            {{"alpha:c1", 1}, {"beta:c1", 1}}, json::object());
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMatchSet(mods, deck);
    CHECK(HasCheck(errors, "mutation.conflict"));
}

TEST_CASE("validator: same-mod replace plus replace conflicts") {
    LoadedMod mod = ValidMod();
    mod.mutations.push_back(MakeMutation(
        "alpha", "m1", "alpha:c1", "replace",
        {Op("n1", "advance_turn", json::object())}));
    mod.mutations.push_back(MakeMutation(
        "alpha", "m2", "alpha:c1", "replace",
        {Op("n1", "advance_turn", json::object())}));
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    REQUIRE(HasCheck(errors, "mutation.conflict"));
    std::string message;
    for (const auto& e : errors) {
        if (e.check == "mutation.conflict") message = e.message;
    }
    CHECK(message.find("alpha:m1") != std::string::npos);
    CHECK(message.find("alpha:m2") != std::string::npos);
}

TEST_CASE("validator: invalid stack policy is op.type") {
    LoadedMod mod = ValidMod();
    AddStatus(mod, "shielded", "replace");
    mod.cards[0].behaviors[0].graph = MakeGraph(
        {Op("n1", "apply_status",
            {{"target", "@self"},
             {"status_kind", "alpha:shielded"},
             {"stack_policy", "banana"}})});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "op.type"));
}

TEST_CASE("validator: valid cap:N stack policy passes") {
    LoadedMod mod = ValidMod();
    AddStatus(mod, "shielded", "replace");
    mod.cards[0].behaviors[0].graph = MakeGraph(
        {Op("n1", "apply_status",
            {{"target", "@self"},
             {"status_kind", "alpha:shielded"},
             {"stack_policy", "cap:3"}})});
    mod.cards[0].raw["behavior"] = json{
        {"on_play", mod.cards[0].behaviors[0].graph.raw}};
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(errors.empty());
}

TEST_CASE("validator: card window routes resolve against on_play nodes") {
    LoadedMod mod = ValidMod();
    WindowSpec window;
    window.responders = "@others";
    window.respond_with = json::object();
    window.duration = "env";
    window.on_response = "n2";
    window.default_route = "n1";
    window.raw = json::object();
    mod.cards[0].window = window;
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(errors.empty());
}

TEST_CASE("validator: dangling card window on_response route is ref.graph") {
    LoadedMod mod = ValidMod();
    WindowSpec window;
    window.responders = "@others";
    window.respond_with = json::object();
    window.duration = "env";
    window.on_response = "ghost";
    window.default_route = "n1";
    window.raw = json::object();
    mod.cards[0].window = window;
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "ref.graph"));
}

TEST_CASE("validator: dangling card window default route is ref.graph") {
    LoadedMod mod = ValidMod();
    WindowSpec window;
    window.responders = "@others";
    window.respond_with = json::object();
    window.duration = "env";
    window.on_response = "n2";
    window.default_route = "ghost";
    window.raw = json::object();
    mod.cards[0].window = window;
    SemanticValidator v(SchemaDir());
    auto errors = v.ValidateMod(mod);
    CHECK(HasCheck(errors, "ref.graph"));
}
