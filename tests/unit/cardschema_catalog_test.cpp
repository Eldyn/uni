#include <doctest/doctest.h>

#include <map>
#include <string>
#include <vector>

#include <match/cardschema/catalog.hpp>
#include <match/cardschema/model.hpp>
#include <match/expr/diagnostic.hpp>

using match::cardschema::CatalogResult;
using match::cardschema::ExpandCards;
using match::cardschema::GeneratedCard;
using match::cardschema::KindTemplate;
using match::cardschema::MemberDef;
using match::cardschema::ModSchema;
using match::cardschema::SlotBinding;
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

bool Mentions(const std::vector<Diagnostic>& diagnostics, const std::string& text) {
    for (const Diagnostic& diagnostic : diagnostics) {
        if (FormatDiagnostic(diagnostic).find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
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
          "\"uni:a\" (x=red) and \"uni:b\" (x=red)");
}

TEST_CASE("kind_cap") {
    ModSchema mod;
    mod.mod_id = "uni";
    mod.directory = "mods/uni";
    for (int attribute_index = 0; attribute_index < 3; ++attribute_index) {
        const std::string attribute = "uni:slot" + std::to_string(attribute_index);
        mod.attributes.push_back({attribute, true, "public", "mods/uni/attributes/slot.json"});
        for (int member_index = 0; member_index < 20; ++member_index) {
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

    CHECK(result.cards.size() < 4096);
    CHECK(Mentions(result.diagnostics, "4096"));
    CHECK(Mentions(result.diagnostics, "exceeds 4096 kinds"));
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
