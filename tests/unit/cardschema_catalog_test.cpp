#include <doctest/doctest.h>

#include <chrono>
#include <map>
#include <string>
#include <vector>

#include <match/cardschema/catalog.hpp>
#include <match/cardschema/model.hpp>
#include <match/cardschema/resolve.hpp>
#include <match/cardschema/validate.hpp>
#include <match/expr/diagnostic.hpp>

using match::cardschema::CatalogResult;
using match::cardschema::DedupeDiagnostics;
using match::cardschema::ExpandCards;
using match::cardschema::GeneratedCard;
using match::cardschema::KindTemplate;
using match::cardschema::MemberDef;
using match::cardschema::ModSchema;
using match::cardschema::SlotBinding;
using match::cardschema::ValidateCatalog;
using match::expr::Diagnostic;
using match::expr::FormatDiagnostic;

namespace {

using json = match::cardschema::json;

// INFO: the in-memory mod the catalog tests expand. It declares two colors and
// two values, one numbered template with a color and a value slot, and one wild
// template with no slots.
ModSchema MiniUni() {
    ModSchema mod;
    mod.mod_id = "uni";
    mod.directory = "mods/uni";
    mod.attributes = {
        {"uni:color", true, "public", "mods/uni/attributes/color.json"},
        {"uni:value", true, "public", "mods/uni/attributes/value.json"},
    };

    const auto member = [](std::string attribute, std::string identifier,
                           std::vector<std::string> tags, int order) {
        MemberDef def;
        def.attribute = std::move(attribute);
        def.identifier = std::move(identifier);
        def.display_name = "test";
        def.tags = std::move(tags);
        def.order = order;
        def.file = "mods/uni/attributes/test.json";
        return def;
    };
    mod.members = {
        member("uni:color", "uni:red", {"colored", "red"}, 0),
        member("uni:color", "uni:blue", {"colored", "blue"}, 1),
        member("uni:value", "uni:0", {}, 0),
        member("uni:value", "uni:1", {}, 1),
    };

    KindTemplate numbered;
    numbered.identifier = "uni:numbered";
    numbered.identifier_format = "{color}_{value}";
    numbered.tags = {"numbered"};
    numbered.slots = {SlotBinding{"uni:color", "color", "", ""},
                      SlotBinding{"uni:value", "value", "", ""}};
    numbered.slot_order = {"color", "value"};
    numbered.face = json{{"kind", "text"},
                         {"color", "{color}"},
                         {"label", "{value}"},
                         {"art_version", 1}};
    numbered.file = "mods/uni/kinds/numbered.json";

    KindTemplate wild;
    wild.identifier = "uni:wild";
    wild.tags = {"wild", "action", "uni:wild"};
    wild.face = json{{"kind", "text"},
                     {"color", "white"},
                     {"label", "jolly"},
                     {"art_version", 1}};
    wild.file = "mods/uni/kinds/wild.json";

    mod.kinds = {numbered, wild};
    return mod;
}

const GeneratedCard* FindCard(const CatalogResult& result, const std::string& kind_id) {
    for (const GeneratedCard& card : result.cards) {
        if (card.kind_id == kind_id) {
            return &card;
        }
    }
    return nullptr;
}

std::vector<std::string> Ids(const CatalogResult& result) {
    std::vector<std::string> ids;
    ids.reserve(result.cards.size());
    for (const GeneratedCard& card : result.cards) {
        ids.push_back(card.kind_id);
    }
    return ids;
}

// INFO: a mod whose `uni:slot<i>` attributes each hold `member_count` members
// (`uni:s<i>_<j>`), so a three-slot template crosses to a known product size.
ModSchema GridMod(int attribute_count, int member_count) {
    ModSchema mod;
    mod.mod_id = "uni";
    mod.directory = "mods/uni";
    for (int attribute_index = 0; attribute_index < attribute_count; ++attribute_index) {
        const std::string attribute = "uni:slot" + std::to_string(attribute_index);
        mod.attributes.push_back({attribute, true, "public", "mods/uni/attributes/slot.json"});
        for (int member_index = 0; member_index < member_count; ++member_index) {
            MemberDef member;
            member.attribute = attribute;
            member.identifier = "uni:s" + std::to_string(attribute_index) + "_" +
                                std::to_string(member_index);
            member.display_name = "test";
            member.order = member_index;
            member.file = "mods/uni/attributes/slot.json";
            mod.members.push_back(member);
        }
    }
    return mod;
}

// INFO: a mod with one `uni:color` attribute of `member_count` members
// (`uni:m<i>`) and one template with two symmetric slots over it.
ModSchema SymmetricMod(int member_count, const std::string& identifier,
                       const std::string& file) {
    ModSchema mod;
    mod.mod_id = "uni";
    mod.directory = "mods/uni";
    mod.attributes = {{"uni:color", true, "public", "mods/uni/attributes/color.json"}};
    for (int member_index = 0; member_index < member_count; ++member_index) {
        MemberDef member;
        member.attribute = "uni:color";
        member.identifier = "uni:m" + std::to_string(member_index);
        member.display_name = "test";
        member.order = member_index;
        member.file = "mods/uni/attributes/color.json";
        mod.members.push_back(member);
    }

    KindTemplate kind;
    kind.identifier = identifier;
    kind.identifier_format = "{left}_{right}";
    kind.slots = {SlotBinding{"uni:color", "left", "", ""},
                  SlotBinding{"uni:color", "right", "", ""}};
    kind.slot_order = {"left", "right"};
    kind.symmetric_groups = {{"left", "right"}};
    kind.file = file;
    mod.kinds = {kind};
    return mod;
}

}  // namespace

TEST_SUITE("cardschema catalog") {

TEST_CASE("numbered_product") {
    const CatalogResult result = ExpandCards({MiniUni()});

    CHECK(result.diagnostics.empty());
    REQUIRE(result.cards.size() == 5);
    CHECK(Ids(result) == std::vector<std::string>{"uni:blue_0", "uni:blue_1", "uni:red_0",
                                                  "uni:red_1", "uni:wild"});
}

TEST_CASE("face_filled") {
    const CatalogResult result = ExpandCards({MiniUni()});

    const GeneratedCard* card = FindCard(result, "uni:red_1");
    REQUIRE(card != nullptr);
    CHECK(card->face == json{{"kind", "text"},
                             {"color", "red"},
                             {"label", "1"},
                             {"art_version", 1}});
}

TEST_CASE("tags_order_and_dedupe") {
    ModSchema mod = MiniUni();
    mod.kinds[0].tags.push_back("numbered");  // a duplicate inside the template tags
    for (MemberDef& member : mod.members) {
        if (member.identifier == "uni:red") {
            member.tags.push_back("numbered");
        }
    }

    const CatalogResult result = ExpandCards({mod});

    const GeneratedCard* card = FindCard(result, "uni:red_1");
    REQUIRE(card != nullptr);
    CHECK(card->tags == std::vector<std::string>{"numbered", "colored", "red"});
}

TEST_CASE("requires_mods") {
    ModSchema magenta;
    magenta.mod_id = "magenta";
    magenta.directory = "mods/magenta";
    MemberDef member;
    member.attribute = "uni:color";
    member.identifier = "magenta:magenta";
    member.display_name = "test";
    member.order = 2;
    member.file = "mods/magenta/attributes/color/magenta.json";
    magenta.members = {member};

    const CatalogResult result = ExpandCards({MiniUni(), magenta});

    CHECK(result.diagnostics.empty());
    const GeneratedCard* foreign = FindCard(result, "uni:magenta_0");
    REQUIRE(foreign != nullptr);
    CHECK(foreign->host_mod == "uni");
    CHECK(foreign->requires_mods == std::vector<std::string>{"magenta"});

    const GeneratedCard* own = FindCard(result, "uni:red_0");
    REQUIRE(own != nullptr);
    CHECK(own->requires_mods.empty());
}

TEST_CASE("abstract_generates_nothing") {
    ModSchema mod = MiniUni();
    mod.kinds[0].is_abstract = true;

    const CatalogResult result = ExpandCards({mod});

    CHECK(result.diagnostics.empty());
    REQUIRE(result.cards.size() == 1);
    CHECK(result.cards[0].kind_id == "uni:wild");
}

TEST_CASE("unknown_attribute_suggestion") {
    ModSchema mod = MiniUni();
    mod.kinds[0].slots = {SlotBinding{"uni:colour", "color", "", ""},
                          SlotBinding{"uni:value", "value", "", ""}};

    const CatalogResult result = ExpandCards({mod});

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/kinds/numbered.json#/kind/attributes/0/attribute: unknown attribute "
          "\"uni:colour\" (did you mean \"uni:color\")");
}

TEST_CASE("unknown_attribute_uses_the_recorded_pointer") {
    ModSchema mod = MiniUni();
    // INFO: an earlier binding of the file was rejected, so the kept binding sits
    // at position 0 but carries the pointer of its real file index.
    mod.kinds[0].slots = {
        SlotBinding{"uni:colour", "color", "", "", "/kind/attributes/1/attribute"},
        SlotBinding{"uni:value", "value", "", "", "/kind/attributes/2/attribute"}};

    const CatalogResult result = ExpandCards({mod});

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/kinds/numbered.json#/kind/attributes/1/attribute: unknown attribute "
          "\"uni:colour\" (did you mean \"uni:color\")");
}

TEST_CASE("duplicate_kind_id") {
    ModSchema mod;
    mod.mod_id = "uni";
    mod.directory = "mods/uni";
    mod.attributes = {{"uni:x", true, "public", "mods/uni/attributes/x.json"}};
    MemberDef member;
    member.attribute = "uni:x";
    member.identifier = "uni:red";
    member.display_name = "test";
    member.file = "mods/uni/attributes/x/red.json";
    mod.members = {member};

    KindTemplate first;
    first.identifier = "uni:a";
    first.identifier_format = "{x}";
    first.slots = {SlotBinding{"uni:x", "x", "", ""}};
    first.slot_order = {"x"};
    first.file = "mods/uni/kinds/a.json";

    KindTemplate second = first;
    second.identifier = "uni:b";
    second.file = "mods/uni/kinds/b.json";
    mod.kinds = {first, second};

    const CatalogResult result = ExpandCards({mod});

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/kinds/b.json#/kind/identifier_format: card \"uni:red\" is generated by both "
          "\"uni:a\" (x=red) in mods/uni/kinds/a.json and \"uni:b\" (x=red)");
}

TEST_CASE("extends_missing_parent") {
    ModSchema mod = MiniUni();
    mod.kinds[0].extends = "uni:numberd";

    const CatalogResult result = ExpandCards({mod});

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/kinds/numbered.json#/kind/extends: unknown parent \"uni:numberd\" "
          "(did you mean \"uni:numbered\")");
}

TEST_CASE("extends_cycle") {
    ModSchema mod;
    mod.mod_id = "uni";
    mod.directory = "mods/uni";
    KindTemplate first;
    first.identifier = "uni:a";
    first.extends = "uni:b";
    first.file = "mods/uni/kinds/a.json";
    KindTemplate second;
    second.identifier = "uni:b";
    second.extends = "uni:a";
    second.file = "mods/uni/kinds/b.json";
    mod.kinds = {first, second};

    const CatalogResult result = ExpandCards({mod});

    REQUIRE(result.diagnostics.size() == 2);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/kinds/a.json#/kind/extends: extends cycle: uni:a -> uni:b -> uni:a");
    CHECK(FormatDiagnostic(result.diagnostics[1]) ==
          "mods/uni/kinds/b.json#/kind/extends: extends cycle: uni:b -> uni:a -> uni:b");
}

TEST_CASE("same_mod_local_collision_names_both") {
    ModSchema mod = MiniUni();
    MemberDef alias_member;
    alias_member.attribute = "uni:color";
    alias_member.identifier = "uni:crimson";
    alias_member.identifier_alias = "red";
    alias_member.display_name = "test";
    alias_member.order = 2;
    alias_member.file = "mods/uni/attributes/color/crimson.json";
    mod.members.push_back(alias_member);

    const CatalogResult result = ExpandCards({mod});

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/attributes/color/crimson.json#/: member \"uni:crimson\" has the same local "
          "name \"red\" as \"uni:red\" in mods/uni/attributes/test.json");
}

TEST_CASE("cross_mod_local_collision_is_left_to_validate") {
    ModSchema other;
    other.mod_id = "other";
    other.directory = "mods/other";
    MemberDef member;
    member.attribute = "uni:color";
    member.identifier = "other:red";
    member.display_name = "test";
    member.attribute_pointer = "/attribute_member/attribute";
    member.file = "mods/other/attributes/color/red.json";
    other.members = {member};

    const CatalogResult result = ExpandCards({MiniUni(), other});

    CHECK(result.diagnostics.empty());
}

TEST_CASE("cross_mod_collision_dedupes_to_one_line") {
    ModSchema other;
    other.mod_id = "other";
    other.directory = "mods/other";
    MemberDef member;
    member.attribute = "uni:color";
    member.identifier = "other:red";
    member.display_name = "test";
    member.attribute_pointer = "/attribute_member/attribute";
    member.file = "mods/other/attributes/color/red.json";
    other.members = {member};

    std::vector<Diagnostic> diagnostics = ExpandCards({MiniUni(), other}).diagnostics;
    const std::vector<Diagnostic> validated = ValidateCatalog({MiniUni(), other});
    diagnostics.insert(diagnostics.end(), validated.begin(), validated.end());
    DedupeDiagnostics(diagnostics);

    REQUIRE(diagnostics.size() == 1);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "mods/other/attributes/color/red.json#/attribute_member/attribute: members \"uni:red\" "
          "and \"other:red\" share the local name \"red\"; set identifier_alias on one");
}

TEST_CASE("same_mod_collision_dedupes_to_one_line") {
    ModSchema mod = MiniUni();
    MemberDef alias_member;
    alias_member.attribute = "uni:color";
    alias_member.identifier = "uni:crimson";
    alias_member.identifier_alias = "red";
    alias_member.display_name = "test";
    alias_member.order = 2;
    alias_member.file = "mods/uni/attributes/color/crimson.json";
    mod.members.push_back(alias_member);

    std::vector<Diagnostic> diagnostics = ExpandCards({mod}).diagnostics;
    const std::vector<Diagnostic> validated = ValidateCatalog({mod});
    diagnostics.insert(diagnostics.end(), validated.begin(), validated.end());
    DedupeDiagnostics(diagnostics);

    REQUIRE(diagnostics.size() == 1);
    CHECK(FormatDiagnostic(diagnostics[0]) ==
          "mods/uni/attributes/color/crimson.json#/: member \"uni:crimson\" has the same local "
          "name \"red\" as \"uni:red\" in mods/uni/attributes/test.json");
}

TEST_CASE("same_local_in_two_attributes_is_not_a_collision") {
    ModSchema mod;
    mod.mod_id = "uni";
    mod.directory = "mods/uni";
    mod.attributes = {{"uni:color", true, "public", "mods/uni/attributes/color.json"},
                      {"uni:value", true, "public", "mods/uni/attributes/value.json"}};

    MemberDef color;
    color.attribute = "uni:color";
    color.identifier = "uni:zero";
    color.display_name = "test";
    color.file = "mods/uni/attributes/color/zero.json";
    MemberDef value;
    value.attribute = "uni:value";
    value.identifier = "magenta:zero";
    value.display_name = "test";
    value.file = "mods/magenta/attributes/value/zero.json";
    mod.members = {color, value};

    KindTemplate numbered;
    numbered.identifier = "uni:numbered";
    numbered.identifier_format = "{color}_{value}";
    numbered.slots = {SlotBinding{"uni:color", "color", "", ""},
                      SlotBinding{"uni:value", "value", "", ""}};
    numbered.slot_order = {"color", "value"};
    numbered.file = "mods/uni/kinds/numbered.json";
    mod.kinds = {numbered};

    const CatalogResult result = ExpandCards({mod});

    CHECK(result.diagnostics.empty());
    const GeneratedCard* card = FindCard(result, "uni:zero_zero");
    REQUIRE(card != nullptr);
    CHECK(card->slot_members.at("color") == "uni:zero");
    CHECK(card->slot_members.at("value") == "magenta:zero");
    CHECK(card->requires_mods == std::vector<std::string>{"magenta"});
}

TEST_CASE("default_title_splits_member_words") {
    ModSchema mod;
    mod.mod_id = "uni";
    mod.directory = "mods/uni";
    mod.attributes = {{"uni:value", true, "public", "mods/uni/attributes/value.json"}};
    MemberDef member;
    member.attribute = "uni:value";
    member.identifier = "uni:draw_two";
    member.display_name = "test";
    member.file = "mods/uni/attributes/value/draw_two.json";
    mod.members = {member};

    KindTemplate kind;
    kind.identifier = "uni:wild_card";
    kind.slots = {SlotBinding{"uni:value", "value", "", ""}};
    kind.slot_order = {"value"};
    kind.file = "mods/uni/kinds/wild_card.json";
    mod.kinds = {kind};

    const CatalogResult result = ExpandCards({mod});

    REQUIRE(result.diagnostics.empty());
    REQUIRE(result.cards.size() == 1);
    CHECK(result.cards[0].title == "Wild Card Draw Two");
}

TEST_CASE("kind_cap") {
    ModSchema mod = GridMod(3, 20);
    KindTemplate big;
    big.identifier = "uni:big";
    big.identifier_format = "{first}_{second}_{third}";
    big.slots = {SlotBinding{"uni:slot0", "first", "", ""},
                 SlotBinding{"uni:slot1", "second", "", ""},
                 SlotBinding{"uni:slot2", "third", "", ""}};
    big.slot_order = {"first", "second", "third"};
    big.file = "mods/uni/kinds/big.json";
    mod.kinds = {big};

    const CatalogResult result = ExpandCards({mod});

    CHECK(result.cards.empty());
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/kinds/big.json#/kind/identifier_format: template \"uni:big\" expands to "
          "8000 cards; host mod \"uni\" exceeds 4096 kinds");
}

TEST_CASE("kind_cap_symmetric") {
    const CatalogResult collapsed = ExpandCards({SymmetricMod(90, "uni:split",
                                                             "mods/uni/kinds/split.json")});

    CHECK(collapsed.diagnostics.empty());
    CHECK(collapsed.cards.size() == 4095);

    const CatalogResult over = ExpandCards({SymmetricMod(91, "uni:split",
                                                        "mods/uni/kinds/split.json")});

    CHECK(over.cards.empty());
    REQUIRE(over.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(over.diagnostics[0]) ==
          "mods/uni/kinds/split.json#/kind/identifier_format: template \"uni:split\" expands to "
          "4186 cards; host mod \"uni\" exceeds 4096 kinds");
}

TEST_CASE("kind_cap_host_sum") {
    ModSchema mod = SymmetricMod(90, "uni:first", "mods/uni/kinds/first.json");
    mod.kinds.push_back(mod.kinds[0]);
    mod.kinds[1].identifier = "uni:second";
    mod.kinds[1].file = "mods/uni/kinds/second.json";

    const CatalogResult result = ExpandCards({mod});

    CHECK(result.cards.size() == 4095);
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/kinds/second.json#/kind/identifier_format: template \"uni:second\" expands "
          "to 4095 cards; host mod \"uni\" exceeds 4096 kinds");
}

TEST_CASE("member_cap_is_one_diagnostic") {
    ModSchema mod = GridMod(1, 1025);
    KindTemplate kind;
    kind.identifier = "uni:numbered";
    kind.identifier_format = "{first}";
    kind.slots = {SlotBinding{"uni:slot0", "first", "", ""}};
    kind.slot_order = {"first"};
    kind.file = "mods/uni/kinds/numbered.json";
    mod.kinds = {kind};

    const CatalogResult result = ExpandCards({mod});

    CHECK(result.cards.empty());
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/attributes/slot.json#/: attribute \"uni:slot0\" has 1025 members; the maximum "
          "is 1024");
}

TEST_CASE("face_placeholder_reported_once") {
    ModSchema mod = GridMod(2, 25);
    KindTemplate kind;
    kind.identifier = "uni:numbered";
    kind.identifier_format = "{first}_{second}";
    kind.slots = {SlotBinding{"uni:slot0", "first", "", ""},
                  SlotBinding{"uni:slot1", "second", "", ""}};
    kind.slot_order = {"first", "second"};
    kind.face = json{{"kind", "text"}, {"label", "{nope}"}};
    kind.file = "mods/uni/kinds/numbered.json";
    mod.kinds = {kind};

    const CatalogResult result = ExpandCards({mod});

    CHECK(result.cards.empty());
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/kinds/numbered.json#/kind/face/label: unknown placeholder \"{nope}\"");
}

TEST_CASE("face_syntax_error_reported_once") {
    ModSchema mod = GridMod(2, 25);
    KindTemplate kind;
    kind.identifier = "uni:numbered";
    kind.identifier_format = "{first}_{second}";
    kind.slots = {SlotBinding{"uni:slot0", "first", "", ""},
                  SlotBinding{"uni:slot1", "second", "", ""}};
    kind.slot_order = {"first", "second"};
    kind.face = json{{"kind", "text"}, {"label", "{first{second}"}};
    kind.file = "mods/uni/kinds/numbered.json";
    mod.kinds = {kind};

    const CatalogResult result = ExpandCards({mod});

    CHECK(result.cards.empty());
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/kinds/numbered.json#/kind/face/label: nested \"{\" in placeholder");
}

TEST_CASE("invalid_cards_are_not_accepted_past_the_diagnostic_cap") {
    ModSchema mod = GridMod(2, 20);
    KindTemplate kind;
    kind.identifier = "uni:numbered";
    kind.identifier_format = "{first}_{second}";
    kind.slots = {SlotBinding{"uni:slot0", "first", "", ""},
                  SlotBinding{"uni:slot1", "second", "", ""}};
    kind.slot_order = {"first", "second"};
    kind.face = json{{"kind", "text"}, {"label", "{nope}"}};
    kind.file = "mods/uni/kinds/numbered.json";
    mod.kinds = {kind};

    const CatalogResult result = ExpandCards({mod});

    CHECK(result.cards.empty());
    CHECK(result.diagnostics.size() == 1);
}

TEST_CASE("symmetric_collapses") {
    ModSchema mod;
    mod.mod_id = "uni";
    mod.directory = "mods/uni";
    mod.attributes = {{"uni:color", true, "public", "mods/uni/attributes/color.json"}};
    MemberDef first;
    first.attribute = "uni:color";
    first.identifier = "uni:a";
    first.display_name = "test";
    first.order = 0;
    first.file = "mods/uni/attributes/color.json";
    MemberDef second = first;
    second.identifier = "uni:b";
    second.order = 1;
    mod.members = {first, second};

    KindTemplate split;
    split.identifier = "uni:split";
    split.identifier_format = "{left}{right}";
    split.slots = {SlotBinding{"uni:color", "left", "", ""},
                   SlotBinding{"uni:color", "right", "", ""}};
    split.slot_order = {"left", "right"};
    split.symmetric_groups = {{"left", "right"}};
    split.file = "mods/uni/kinds/split.json";
    mod.kinds = {split};

    const CatalogResult result = ExpandCards({mod});

    CHECK(result.diagnostics.empty());
    CHECK(Ids(result) == std::vector<std::string>{"uni:aa", "uni:ab", "uni:bb"});
}

TEST_CASE("legacy_entry_shape") {
    const CatalogResult result = ExpandCards({MiniUni()});

    const GeneratedCard* card = FindCard(result, "uni:red_1");
    REQUIRE(card != nullptr);
    REQUIRE(card->legacy_entry.is_object());
    std::vector<std::string> keys;
    for (auto entry = card->legacy_entry.begin(); entry != card->legacy_entry.end(); ++entry) {
        keys.push_back(entry.key());
    }
    CHECK(keys == std::vector<std::string>{"face", "id", "tags", "title"});
    CHECK(card->legacy_entry["id"] == "red_1");
}

}  // TEST_SUITE("cardschema catalog")

namespace {

namespace combine_mode = match::cardschema::combine_mode;
namespace selector_mode = match::cardschema::selector_mode;

using match::cardschema::DeckEntry;
using match::cardschema::DeckSpec;
using match::cardschema::ExpandedDeck;
using match::cardschema::ExpandDeck;
using match::cardschema::MemberSelector;

// INFO: MiniUni with the value members 2..9, so the shipped classic deck's
// numbered values 0..9 are all present.
ModSchema DeckUni() {
    ModSchema mod = MiniUni();
    for (int value = 2; value <= 9; ++value) {
        MemberDef member;
        member.attribute = "uni:value";
        member.identifier = "uni:" + std::to_string(value);
        member.display_name = "test";
        member.order = value;
        member.file = "mods/uni/attributes/test.json";
        mod.members.push_back(member);
    }
    return mod;
}

MemberDef ColorMember(std::string identifier, std::vector<std::string> tags, int order,
                      std::string file) {
    MemberDef member;
    member.attribute = "uni:color";
    member.identifier = std::move(identifier);
    member.display_name = "test";
    member.tags = std::move(tags);
    member.order = order;
    member.file = std::move(file);
    return member;
}

// INFO: the shipped classic deck as an in-memory spec: colors join through the
// "colored" tag, and the numbered values are spelled out as identifiers.
DeckSpec ClassicDeck() {
    DeckSpec deck;
    deck.identifier = "uni:classic";
    deck.name = "Classic";
    deck.file = "mods/uni/decks/classic.json";

    DeckEntry zeros;
    zeros.kind = "uni:numbered";
    zeros.copies = 1;
    zeros.pointer = "/deck/cards/0";
    MemberSelector colored_zero;
    colored_zero.mode = selector_mode::kTag;
    colored_zero.tag = "colored";
    colored_zero.pointer = "/deck/cards/0/members/color";
    zeros.members["color"] = colored_zero;
    MemberSelector zero;
    zero.mode = selector_mode::kIdentifiers;
    zero.identifiers = {"uni:0"};
    zero.pointer = "/deck/cards/0/members/value";
    zeros.members["value"] = zero;

    DeckEntry numbered;
    numbered.kind = "uni:numbered";
    numbered.copies = "@settings.count_numbered";
    numbered.pointer = "/deck/cards/1";
    MemberSelector colored;
    colored.mode = selector_mode::kTag;
    colored.tag = "colored";
    colored.pointer = "/deck/cards/1/members/color";
    numbered.members["color"] = colored;
    MemberSelector one_to_nine;
    one_to_nine.mode = selector_mode::kIdentifiers;
    for (int value = 1; value <= 9; ++value) {
        one_to_nine.identifiers.push_back("uni:" + std::to_string(value));
    }
    one_to_nine.pointer = "/deck/cards/1/members/value";
    numbered.members["value"] = one_to_nine;

    DeckEntry wild;
    wild.kind = "uni:wild";
    wild.copies = "@settings.count_wild";
    wild.pointer = "/deck/cards/2";

    deck.cards = {zeros, numbered, wild};
    return deck;
}

ModSchema ClassicMod() {
    ModSchema mod = DeckUni();
    mod.decks = {ClassicDeck()};
    return mod;
}

std::map<std::string, int64_t> DefaultSettings() {
    return {{"count_numbered", 2}, {"count_wild", 4}};
}

int CountOf(const ExpandedDeck& deck, const std::string& kind_id) {
    for (const auto& [id, count] : deck.cards) {
        if (id == kind_id) {
            return count;
        }
    }
    return 0;
}

// INFO: a deck with one entry, for the error-path tests that need a specific
// kind, copies value or combine mode.
DeckSpec SingleEntryDeck(const std::string& identifier, const std::string& file,
                         const std::string& kind, const json& copies,
                         const std::string& combine = combine_mode::kProduct) {
    DeckSpec deck;
    deck.identifier = identifier;
    deck.file = file;
    DeckEntry entry;
    entry.kind = kind;
    entry.copies = copies;
    entry.combine = combine;
    entry.pointer = "/deck/cards/0";
    deck.cards = {entry};
    return deck;
}

// INFO: a three-slot template over GridMod's attributes, so a deck entry can
// select a large raw combination count.
KindTemplate GridThreeSlotKind() {
    KindTemplate kind;
    kind.identifier = "uni:grid";
    kind.identifier_format = "{first}_{second}_{third}";
    kind.slots = {SlotBinding{"uni:slot0", "first", "", ""},
                  SlotBinding{"uni:slot1", "second", "", ""},
                  SlotBinding{"uni:slot2", "third", "", ""}};
    kind.slot_order = {"first", "second", "third"};
    kind.file = "mods/uni/kinds/grid.json";
    return kind;
}

// INFO: an eleven-slot template over GridMod(11, 64). The raw product 64^11
// overflows 64 bits, so a deck entry over it must report the oversize selection
// instead of being dropped silently.
KindTemplate OverflowKind() {
    KindTemplate kind;
    kind.identifier = "uni:grid";
    kind.identifier_format = "{s0}_{s1}_{s2}_{s3}_{s4}_{s5}_{s6}_{s7}_{s8}_{s9}_{s10}";
    for (int index = 0; index < 11; ++index) {
        const std::string slot = "s" + std::to_string(index);
        kind.slots.push_back(SlotBinding{"uni:slot" + std::to_string(index), slot, "", ""});
        kind.slot_order.push_back(slot);
    }
    kind.file = "mods/uni/kinds/grid.json";
    return kind;
}

}  // namespace

TEST_SUITE("cardschema deck") {

TEST_CASE("mini_deck_counts") {
    const ExpandedDeck deck =
        ExpandDeck({ClassicMod()}, "uni:classic", {}, DefaultSettings());

    CHECK(deck.diagnostics.empty());
    CHECK(deck.total == 42);
    CHECK(CountOf(deck, "uni:red_0") == 1);
    CHECK(CountOf(deck, "uni:red_5") == 2);
    CHECK(CountOf(deck, "uni:wild") == 4);
    CHECK(deck.cards.size() == 21);
}

TEST_CASE("tag_selector_grows_with_new_color") {
    ModSchema magenta;
    magenta.mod_id = "magenta";
    magenta.directory = "mods/magenta";
    magenta.members = {ColorMember("magenta:magenta", {"colored", "magenta"}, 2,
                                   "mods/magenta/attributes/color/magenta.json")};

    const ExpandedDeck both = ExpandDeck({ClassicMod(), magenta}, "uni:classic",
                                         {"uni", "magenta"}, DefaultSettings());
    CHECK(both.diagnostics.empty());
    CHECK(both.total == 61);
    CHECK(CountOf(both, "uni:magenta_5") == 2);

    const ExpandedDeck only_uni =
        ExpandDeck({ClassicMod(), magenta}, "uni:classic", {"uni"}, DefaultSettings());
    CHECK(only_uni.diagnostics.empty());
    CHECK(only_uni.total == 42);
    CHECK(CountOf(only_uni, "uni:magenta_5") == 0);
}

TEST_CASE("unknown_identifier_suggestion") {
    ModSchema mod = ClassicMod();
    mod.decks[0].cards[0].members["value"].identifiers = {"uni:5x"};

    const ExpandedDeck deck = ExpandDeck({mod}, "uni:classic", {}, DefaultSettings());

    REQUIRE(deck.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(deck.diagnostics[0]) ==
          "mods/uni/decks/classic.json#/deck/cards/0/members/value: unknown member "
          "\"uni:5x\" of attribute \"uni:value\" (did you mean \"uni:5\")");
}

TEST_CASE("zip_length_mismatch") {
    ModSchema mod = ClassicMod();
    DeckSpec deck;
    deck.identifier = "uni:zip";
    deck.file = "mods/uni/decks/zip.json";

    DeckEntry entry;
    entry.kind = "uni:numbered";
    entry.copies = 1;
    entry.combine = combine_mode::kZip;
    entry.pointer = "/deck/cards/0";
    MemberSelector colors;
    colors.mode = selector_mode::kIdentifiers;
    colors.identifiers = {"uni:red", "uni:blue"};
    colors.pointer = "/deck/cards/0/members/color";
    entry.members["color"] = colors;
    MemberSelector value;
    value.mode = selector_mode::kIdentifiers;
    value.identifiers = {"uni:0"};
    value.pointer = "/deck/cards/0/members/value";
    entry.members["value"] = value;
    deck.cards = {entry};
    mod.decks = {deck};

    const ExpandedDeck result = ExpandDeck({mod}, "uni:zip", {}, DefaultSettings());

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/decks/zip.json#/deck/cards/0: zip combine needs equal slot lengths, "
          "got 2 and 1");
}

TEST_CASE("unique_pairs") {
    ModSchema mod = ClassicMod();
    mod.members.push_back(
        ColorMember("uni:green", {"colored", "green"}, 2, "mods/uni/attributes/test.json"));

    KindTemplate split;
    split.identifier = "uni:split";
    split.identifier_format = "{left}_{right}";
    split.slots = {SlotBinding{"uni:color", "left", "", ""},
                   SlotBinding{"uni:color", "right", "", ""}};
    split.slot_order = {"left", "right"};
    split.file = "mods/uni/kinds/split.json";
    mod.kinds.push_back(split);

    DeckSpec deck;
    deck.identifier = "uni:pairs";
    deck.file = "mods/uni/decks/pairs.json";
    DeckEntry entry;
    entry.kind = "uni:split";
    entry.copies = 1;
    entry.combine = combine_mode::kUniquePairs;
    entry.pointer = "/deck/cards/0";
    deck.cards = {entry};
    mod.decks = {deck};

    const ExpandedDeck result = ExpandDeck({mod}, "uni:pairs", {}, DefaultSettings());

    CHECK(result.diagnostics.empty());
    CHECK(result.total == 3);
    CHECK(result.cards.size() == 3);
}

TEST_CASE("copies_negative") {
    ModSchema mod = ClassicMod();
    mod.decks[0].cards[1].copies = "@settings.count_wild - 10";

    const ExpandedDeck result = ExpandDeck({mod}, "uni:classic", {}, DefaultSettings());

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/decks/classic.json#/deck/cards/1/copies: copies expression "
          "\"@settings.count_wild - 10\" is -6; the count must not be negative");
}

TEST_CASE("copies_division_by_zero") {
    ModSchema mod = ClassicMod();
    mod.decks[0].cards[1].copies = "4 / (@settings.count_wild - 4)";

    const ExpandedDeck result = ExpandDeck({mod}, "uni:classic", {}, DefaultSettings());

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/decks/classic.json#/deck/cards/1/copies: copies expression "
          "\"4 / (@settings.count_wild - 4)\" failed: division_by_zero: division by zero");
}

TEST_CASE("deck_cap") {
    ModSchema mod = ClassicMod();
    std::map<std::string, int64_t> settings = DefaultSettings();
    settings["count_numbered"] = 200;

    const auto start = std::chrono::steady_clock::now();
    const ExpandedDeck result = ExpandDeck({mod}, "uni:classic", {}, settings);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(elapsed < std::chrono::seconds(2));
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/decks/classic.json#/deck/cards/1: deck \"uni:classic\" has 3602 cards, "
          "the limit is 1024");
}

TEST_CASE("copies_unknown_setting") {
    ModSchema mod = ClassicMod();
    mod.decks[0].cards[2].copies = "@settings.count_wilds";

    const ExpandedDeck result = ExpandDeck({mod}, "uni:classic", {}, DefaultSettings());

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/decks/classic.json#/deck/cards/2/copies: unknown property "
          "\"count_wilds\" on settings (did you mean \"count_wild\")");
}

TEST_CASE("zero_copies_small_entry") {
    ModSchema mod = GridMod(3, 2);
    mod.kinds = {GridThreeSlotKind()};
    mod.decks = {SingleEntryDeck("uni:zero", "mods/uni/decks/zero.json", "uni:grid", 0)};

    const ExpandedDeck result = ExpandDeck({mod}, "uni:zero", {}, DefaultSettings());

    CHECK(result.diagnostics.empty());
    CHECK(result.cards.empty());
    CHECK(result.total == 0);
}

TEST_CASE("too_many_combinations") {
    ModSchema mod = GridMod(3, 200);
    mod.kinds = {GridThreeSlotKind()};
    mod.decks = {SingleEntryDeck("uni:oversize", "mods/uni/decks/oversize.json", "uni:grid", 0)};

    const auto start = std::chrono::steady_clock::now();
    const ExpandedDeck result = ExpandDeck({mod}, "uni:oversize", {}, DefaultSettings());
    const auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(elapsed < std::chrono::seconds(2));
    CHECK(result.cards.empty());
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/decks/oversize.json#/deck/cards/0: entry selects too many combinations "
          "(8000000; the limit is 65536)");
}

TEST_CASE("combination_product_overflow") {
    ModSchema mod = GridMod(11, 64);
    mod.kinds = {OverflowKind()};
    mod.decks = {SingleEntryDeck("uni:overflow", "mods/uni/decks/overflow.json", "uni:grid", 0)};

    const ExpandedDeck result = ExpandDeck({mod}, "uni:overflow", {}, DefaultSettings());

    CHECK(result.cards.empty());
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/decks/overflow.json#/deck/cards/0: entry selects too many combinations "
          "(18446744073709551615; the limit is 65536)");
}

TEST_CASE("unique_pairs_needs_two_slots") {
    ModSchema mod = GridMod(3, 2);
    mod.kinds = {GridThreeSlotKind()};
    mod.decks = {SingleEntryDeck("uni:pairs3", "mods/uni/decks/pairs3.json", "uni:grid", 1,
                                 combine_mode::kUniquePairs)};

    const ExpandedDeck result = ExpandDeck({mod}, "uni:pairs3", {}, DefaultSettings());

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/decks/pairs3.json#/deck/cards/0: unique_pairs combine needs exactly two "
          "slots, got 3");
}

TEST_CASE("unique_pairs_unequal_slots") {
    ModSchema mod = ClassicMod();
    mod.members.push_back(
        ColorMember("uni:green", {"colored", "green"}, 2, "mods/uni/attributes/test.json"));

    KindTemplate split;
    split.identifier = "uni:split";
    split.identifier_format = "{left}_{right}";
    split.slots = {SlotBinding{"uni:color", "left", "", ""},
                   SlotBinding{"uni:color", "right", "", ""}};
    split.slot_order = {"left", "right"};
    split.file = "mods/uni/kinds/split.json";
    mod.kinds.push_back(split);

    DeckSpec deck = SingleEntryDeck("uni:pairs", "mods/uni/decks/pairs.json", "uni:split", 1,
                                    combine_mode::kUniquePairs);
    MemberSelector left;
    left.mode = selector_mode::kIdentifiers;
    left.identifiers = {"uni:red", "uni:blue"};
    left.pointer = "/deck/cards/0/members/left";
    deck.cards[0].members["left"] = left;
    MemberSelector right;
    right.mode = selector_mode::kIdentifiers;
    right.identifiers = {"uni:red", "uni:blue", "uni:green"};
    right.pointer = "/deck/cards/0/members/right";
    deck.cards[0].members["right"] = right;
    mod.decks = {deck};

    const ExpandedDeck result = ExpandDeck({mod}, "uni:pairs", {}, DefaultSettings());

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/decks/pairs.json#/deck/cards/0/combine: unique_pairs needs both slots to "
          "select the same members");
}

TEST_CASE("identifier_selector_dedupes") {
    ModSchema mod = ClassicMod();
    mod.decks[0].cards[0].members["value"].identifiers = {"uni:0", "uni:0"};

    const ExpandedDeck result = ExpandDeck({mod}, "uni:classic", {}, DefaultSettings());

    CHECK(result.diagnostics.empty());
    CHECK(result.total == 42);
    CHECK(CountOf(result, "uni:red_0") == 1);
}

TEST_CASE("copies_integer_overflow") {
    ModSchema mod = ClassicMod();
    mod.decks[0].cards[1].copies = "9223372036854775807 + 1";

    const ExpandedDeck result = ExpandDeck({mod}, "uni:classic", {}, DefaultSettings());

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/decks/classic.json#/deck/cards/1/copies: copies expression "
          "\"9223372036854775807 + 1\" failed: integer_overflow: integer overflow");
}

TEST_CASE("copies_float_result") {
    ModSchema mod = ClassicMod();
    mod.decks[0].cards[1].copies = "1.5";

    const ExpandedDeck result = ExpandDeck({mod}, "uni:classic", {}, DefaultSettings());

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/decks/classic.json#/deck/cards/1/copies: expected int, got float "
          "(use floor(), ceil() or round())");
}

TEST_CASE("copies_above_maximum") {
    ModSchema mod = ClassicMod();
    mod.decks[0].cards[1].copies = "4097";

    const ExpandedDeck result = ExpandDeck({mod}, "uni:classic", {}, DefaultSettings());

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/decks/classic.json#/deck/cards/1/copies: copies expression \"4097\" is 4097; "
          "the count must not exceed 4096");
}

TEST_CASE("copies_literal_negative") {
    ModSchema mod = ClassicMod();
    mod.decks[0].cards[1].copies = -1;

    const ExpandedDeck result = ExpandDeck({mod}, "uni:classic", {}, DefaultSettings());

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/decks/classic.json#/deck/cards/1/copies: copies is -1; the count must not "
          "be negative");
}

TEST_CASE("unknown_deck") {
    const ExpandedDeck result = ExpandDeck({ClassicMod()}, "uni:nope", {}, DefaultSettings());

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) == "#/: unknown deck \"uni:nope\"");
}

TEST_CASE("unknown_kind_suggestion") {
    ModSchema mod = ClassicMod();
    mod.decks[0].cards[0].kind = "uni:numberd";

    const ExpandedDeck result = ExpandDeck({mod}, "uni:classic", {}, DefaultSettings());

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/decks/classic.json#/deck/cards/0: unknown kind \"uni:numberd\" "
          "(did you mean \"uni:numbered\")");
}

TEST_CASE("abstract_kind_rejected") {
    ModSchema mod = ClassicMod();
    KindTemplate abstract_kind;
    abstract_kind.identifier = "uni:base";
    abstract_kind.is_abstract = true;
    abstract_kind.file = "mods/uni/kinds/base.json";
    mod.kinds.push_back(abstract_kind);
    mod.decks = {SingleEntryDeck("uni:base_deck", "mods/uni/decks/base_deck.json", "uni:base", 1)};

    const ExpandedDeck result = ExpandDeck({mod}, "uni:base_deck", {}, DefaultSettings());

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/decks/base_deck.json#/deck/cards/0: kind \"uni:base\" is abstract");
}

TEST_CASE("max_cards_below_total") {
    ModSchema mod = ClassicMod();
    mod.decks[0].max_cards = 10;

    const ExpandedDeck result = ExpandDeck({mod}, "uni:classic", {}, DefaultSettings());

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/decks/classic.json#/deck/cards/1: deck \"uni:classic\" has 38 cards, "
          "the limit is 10");
}

TEST_CASE("deck_expansion_reuses_the_given_catalog") {
    ModSchema mod = ClassicMod();
    const CatalogResult catalog = ExpandCards({mod});

    // INFO: the given catalog is the id set the deck is checked against, so a
    // pruned catalog must make the wild entry fail even though a fresh
    // expansion over the same mod would generate it.
    CatalogResult pruned;
    for (const GeneratedCard& card : catalog.cards) {
        if (card.kind_id != "uni:wild") {
            pruned.cards.push_back(card);
        }
    }

    const ExpandedDeck result =
        ExpandDeck(pruned, {mod}, "uni:classic", {}, DefaultSettings());

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/decks/classic.json#/deck/cards/2: card \"uni:wild\" is not generated "
          "by the active mods");
}

TEST_CASE("deck_expansion_filters_the_catalog_by_active_mods") {
    ModSchema magenta;
    magenta.mod_id = "magenta";
    magenta.directory = "mods/magenta";
    magenta.members = {ColorMember("magenta:magenta", {"colored", "magenta"}, 2,
                                   "mods/magenta/attributes/color/magenta.json")};
    const CatalogResult catalog = ExpandCards({ClassicMod(), magenta});

    const ExpandedDeck only_uni =
        ExpandDeck(catalog, {ClassicMod(), magenta}, "uni:classic", {"uni"},
                   DefaultSettings());

    CHECK(only_uni.diagnostics.empty());
    CHECK(only_uni.total == 42);
    CHECK(CountOf(only_uni, "uni:magenta_5") == 0);
}

TEST_CASE("deck_cap_constants") {
    CHECK(match::cardschema::kMaximumDecksPerMod == 64);
    CHECK(match::cardschema::kMaximumDecksTotal == 256);
}

TEST_CASE("card_not_in_active_catalog") {
    ModSchema mod = ClassicMod();
    // INFO: a broken `extends` chain makes ExpandCards skip the template, so the
    // active catalog holds no card of it; the deck still expands the template and
    // must report the card the match cannot build.
    mod.kinds[0].extends = "uni:missing";
    mod.decks = {SingleEntryDeck("uni:missing_cards", "mods/uni/decks/missing_cards.json",
                                 "uni:numbered", 1)};
    MemberSelector color;
    color.mode = selector_mode::kIdentifiers;
    color.identifiers = {"uni:red"};
    color.pointer = "/deck/cards/0/members/color";
    mod.decks[0].cards[0].members["color"] = color;
    MemberSelector value;
    value.mode = selector_mode::kIdentifiers;
    value.identifiers = {"uni:0"};
    value.pointer = "/deck/cards/0/members/value";
    mod.decks[0].cards[0].members["value"] = value;

    const ExpandedDeck result = ExpandDeck({mod}, "uni:missing_cards", {}, DefaultSettings());

    REQUIRE(result.diagnostics.size() == 1);
    CHECK(FormatDiagnostic(result.diagnostics[0]) ==
          "mods/uni/decks/missing_cards.json#/deck/cards/0: card \"uni:red_0\" is not generated "
          "by the active mods");
}

}  // TEST_SUITE("cardschema deck")
