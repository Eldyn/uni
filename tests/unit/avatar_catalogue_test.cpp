#include <doctest/doctest.h>

#include <avatar/catalogue.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>

/**
 * @file avatar_catalogue_test.cpp
 * @brief The C++ side of the avatar catalogue: the same rules the TS schema
 *        (`frontend/src/lib/avatar/catalogueSchema.ts`) enforces, plus the
 *        lookups the server needs (slot defaults, starters, swatches).
 */

namespace fs = std::filesystem;
using nlohmann::json;
using uni::avatar::Catalogue;
using uni::avatar::Item;

namespace {

/* INFO: locate the repo root from this test file so the shipped
 *       `avatar/catalogue` folder loads cwd-independently (mirrors
 *       mod_hash_test / lobby_test). */
fs::path ProjectRoot() {
    fs::path path(__FILE__);
    while (!path.empty()) {
        std::error_code error;
        if (fs::is_directory(path / "contract" / "schemas", error)) return path;
        const fs::path parent = path.parent_path();
        if (parent == path) break;
        path = parent;
    }
    return {};
}

/** @brief A minimal catalogue that passes every rule, as a JSON document. */
json ValidDoc() {
    return json::parse(R"({
        "version": 1,
        "shape_sockets": {
            "uni:round": {
                "eyes": [36, 26],
                "mouth": [42, 40],
                "head_top": [32, 4],
                "body_center": [48, 62]
            }
        },
        "palettes": [
            {
                "id": "basic",
                "name": "Basic",
                "price": 0,
                "starter": true,
                "swatches": [
                    { "id": "basic:red", "hex": "#e84a4a" },
                    { "id": "basic:blue", "hex": "#4a6ae8" }
                ]
            },
            {
                "id": "uni:neon",
                "name": "Neon",
                "price": 250,
                "swatches": [
                    { "id": "uni:neon:hot", "hex": "#ff2d95" }
                ]
            }
        ],
        "items": [
            {
                "id": "uni:round",
                "slot": "shape",
                "name": "Round",
                "asset": { "kind": "strip", "path": "shapes/uni_round.png" },
                "regions": 1,
                "rarity": "common",
                "price": 0,
                "starter": true
            },
            {
                "id": "uni:eyes_dot",
                "slot": "eyes",
                "name": "Dot eyes",
                "asset": { "kind": "strip", "path": "eyes/uni_eyes_dot.png" },
                "regions": 1,
                "rarity": "common",
                "price": 0,
                "starter": true
            },
            {
                "id": "uni:mouth_smile",
                "slot": "mouth",
                "name": "Smile",
                "asset": { "kind": "strip", "path": "mouth/uni_mouth_smile.png" },
                "regions": 1,
                "rarity": "common",
                "price": 0,
                "starter": true
            },
            {
                "id": "uni:hat_cap",
                "slot": "head_top",
                "name": "Cap",
                "asset": {
                    "kind": "still",
                    "path": "head_top/uni_hat_cap.png",
                    "bob": [[0, 0], [0, -1], [0, -2], [0, -1], [0, 0]]
                },
                "regions": 1,
                "rarity": "common",
                "price": 80,
                "socket": "head_top"
            }
        ],
        "featured": ["uni:hat_cap"]
    })");
}

/** @brief Writes `files` (relative path to text) under a fresh temp folder. */
fs::path WriteTree(const std::string& name,
                   const std::map<std::string, std::string>& files) {
    const fs::path root = fs::temp_directory_path() / ("uni_catalogue_" + name);
    fs::remove_all(root);
    for (const auto& [relative, text] : files) {
        fs::create_directories((root / relative).parent_path());
        std::ofstream(root / relative, std::ios::binary) << text;
    }
    return root;
}

/** @brief A tree that passes every rule; tests edit one entry of it. */
std::map<std::string, std::string> SmallTree() {
    return {
        {"meta.json", R"({"version":1,"featured":[]})"},
        {"sockets/uni_round.json",
         R"({"shape":"uni:round","eyes":[1,2],"mouth":[3,4],"head_top":[5,6],"body_center":[7,8]})"},
        {"palettes/basic.json",
         R"({"id":"basic","order":10,"name":"Basic","price":0,"starter":true,
             "swatches":[{"id":"basic:red","hex":"#ff0000"}]})"},
        {"items/shape/uni_round.json",
         R"({"id":"uni:round","order":10,"slot":"shape","name":"Round",
             "asset":{"kind":"strip","path":"shapes/uni_round.png"},
             "regions":1,"rarity":"common","price":0,"starter":true})"},
        {"items/eyes/uni_eyes_b.json",
         R"({"id":"uni:eyes_b","order":20,"slot":"eyes","name":"B",
             "asset":{"kind":"strip","path":"eyes/b.png"},
             "regions":0,"rarity":"common","price":0,"starter":true})"},
        {"items/eyes/uni_eyes_a.json",
         R"({"id":"uni:eyes_a","order":10,"slot":"eyes","name":"A",
             "asset":{"kind":"strip","path":"eyes/a.png"},
             "regions":0,"rarity":"common","price":0,"starter":true})"},
        {"items/mouth/uni_mouth.json",
         R"({"id":"uni:mouth","order":10,"slot":"mouth","name":"M",
             "asset":{"kind":"strip","path":"mouth/m.png"},
             "regions":0,"rarity":"common","price":0,"starter":true})"},
    };
}

/** @brief The item with `id` in a catalogue document, for targeted edits. */
json& ItemAt(json& doc, const std::string& id) {
    for (json& item : doc["items"]) {
        if (item["id"] == id) return item;
    }
    throw std::runtime_error("no such test item: " + id);
}

}  // namespace

TEST_CASE("avatar catalogue: parses a valid catalogue") {
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());

    const Item* eyes = catalogue.FindItem("uni:eyes_dot");
    REQUIRE(eyes != nullptr);
    CHECK(eyes->slot == "eyes");
    CHECK(eyes->regions == 1);
    CHECK(eyes->starter);

    const Item* hat = catalogue.FindItem("uni:hat_cap");
    REQUIRE(hat != nullptr);
    CHECK(hat->slot == "head_top");
    CHECK_FALSE(hat->starter);

    REQUIRE(catalogue.SlotDefault("shape") != nullptr);
    CHECK(catalogue.SlotDefault("shape")->id == "uni:round");
}

TEST_CASE("avatar catalogue: rejects duplicate ids") {
    json doc = ValidDoc();
    doc["items"].push_back(ItemAt(doc, "uni:eyes_dot"));
    CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
}

TEST_CASE("avatar catalogue: rejects an id with no namespace") {
    json doc = ValidDoc();
    ItemAt(doc, "uni:eyes_dot")["id"] = "eyes_dot";
    CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
}

TEST_CASE("avatar catalogue: rejects regions above 6") {
    json doc = ValidDoc();
    ItemAt(doc, "uni:eyes_dot")["regions"] = 7;
    CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
}

TEST_CASE("avatar catalogue: regions above 3 need an extra mask file") {
    SUBCASE("accepts 6 regions with one extra mask") {
        json doc = ValidDoc();
        json& item = ItemAt(doc, "uni:eyes_dot");
        item["asset"]["masks"] = json::array({"eyes/uni_eyes_dot_b.png"});
        item["regions"] = 6;
        CHECK_NOTHROW(Catalogue::Parse(doc));
    }
    SUBCASE("rejects 4 regions with no masks") {
        json doc = ValidDoc();
        ItemAt(doc, "uni:eyes_dot")["regions"] = 4;
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("rejects 4 regions with an empty masks list") {
        json doc = ValidDoc();
        json& item = ItemAt(doc, "uni:eyes_dot");
        item["asset"]["masks"] = json::array();
        item["regions"] = 4;
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("rejects 7 regions even with a mask") {
        json doc = ValidDoc();
        json& item = ItemAt(doc, "uni:eyes_dot");
        item["asset"]["masks"] = json::array({"eyes/uni_eyes_dot_b.png"});
        item["regions"] = 7;
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("rejects masks that is not an array") {
        json doc = ValidDoc();
        ItemAt(doc, "uni:eyes_dot")["asset"]["masks"] = "eyes/b.png";
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("rejects a masks entry that is not a string") {
        json doc = ValidDoc();
        ItemAt(doc, "uni:eyes_dot")["asset"]["masks"] = json::array({7});
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("rejects more than one extra mask") {
        json doc = ValidDoc();
        json& item = ItemAt(doc, "uni:eyes_dot");
        item["asset"]["masks"] = json::array({"a.png", "b.png"});
        item["regions"] = 3;
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
}

TEST_CASE("avatar catalogue: rejects a missing shape starter") {
    json doc = ValidDoc();
    ItemAt(doc, "uni:round")["starter"] = false;
    CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
}

TEST_CASE("avatar catalogue: rejects a bad swatch hex") {
    json doc = ValidDoc();
    doc["palettes"][0]["swatches"][0]["hex"] = "red";
    CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
}

TEST_CASE("avatar catalogue: rejects the rest of the schema rules") {
    SUBCASE("an unknown slot") {
        json doc = ValidDoc();
        ItemAt(doc, "uni:eyes_dot")["slot"] = "ears";
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("a negative price") {
        json doc = ValidDoc();
        ItemAt(doc, "uni:eyes_dot")["price"] = -1;
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("a still bob that is not 5 frames") {
        json doc = ValidDoc();
        ItemAt(doc, "uni:hat_cap")["asset"]["bob"] = json::array(
            {json::array({0, 0}), json::array({0, -1})});
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("a still part with no socket") {
        json doc = ValidDoc();
        ItemAt(doc, "uni:hat_cap").erase("socket");
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("a featured id that is not an item") {
        json doc = ValidDoc();
        doc["featured"] = json::array({"uni:nope"});
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("a shape_sockets entry missing a socket") {
        json doc = ValidDoc();
        doc["shape_sockets"]["uni:round"].erase("body_center");
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("available_until before available_from") {
        json doc = ValidDoc();
        ItemAt(doc, "uni:eyes_dot")["available_from"] = "2026-05-01T00:00:00Z";
        ItemAt(doc, "uni:eyes_dot")["available_until"] = "2026-04-01T00:00:00Z";
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("a swatch id duplicated across palettes") {
        json doc = ValidDoc();
        doc["palettes"][1]["swatches"][0]["id"] = "basic:red";
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("a wrong version") {
        json doc = ValidDoc();
        doc["version"] = 2;
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
}

TEST_CASE("avatar catalogue: FindItem returns null for unknown ids") {
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());

    CHECK(catalogue.FindItem("uni:nope") == nullptr);
    CHECK(catalogue.FindItem("") == nullptr);
    CHECK(catalogue.SlotDefault("motif") == nullptr);
}

TEST_CASE("avatar catalogue: IsStarter is true only for starter items") {
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());

    CHECK(catalogue.IsStarter("uni:eyes_dot"));
    CHECK_FALSE(catalogue.IsStarter("uni:hat_cap"));
    CHECK_FALSE(catalogue.IsStarter("uni:nope"));
}

TEST_CASE("avatar catalogue: IsStarterSwatch follows the palette starter flag") {
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());

    CHECK(catalogue.HasSwatch("basic:red"));
    CHECK(catalogue.IsStarterSwatch("basic:red"));
    CHECK(catalogue.HasSwatch("uni:neon:hot"));
    CHECK_FALSE(catalogue.IsStarterSwatch("uni:neon:hot"));
    CHECK_FALSE(catalogue.HasSwatch("uni:nope"));
    CHECK_FALSE(catalogue.IsStarterSwatch("uni:nope"));
}

TEST_CASE("avatar catalogue: PriceOf returns catalogue prices") {
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());

    CHECK(catalogue.PriceOf("uni:hat_cap") == 80);
    CHECK(catalogue.PriceOf("uni:neon") == 250);
    CHECK_FALSE(catalogue.PriceOf("uni:nope").has_value());
    CHECK_FALSE(catalogue.PriceOf("").has_value());
}

TEST_CASE("avatar catalogue: IsBuyable follows starter, flags and the window") {
    /* 2026-05-01T00:00:00Z and 2026-06-01T00:00:00Z, the window used below. */
    constexpr std::int64_t kWindowStart = 1777593600;
    constexpr std::int64_t kWindowEnd = 1780272000;

    SUBCASE("a starter is not buyable") {
        const Catalogue catalogue = Catalogue::Parse(ValidDoc());
        CHECK_FALSE(catalogue.IsBuyable("uni:eyes_dot", kWindowStart));
    }
    SUBCASE("a hidden item is not buyable") {
        json doc = ValidDoc();
        ItemAt(doc, "uni:hat_cap")["hidden"] = true;
        CHECK_FALSE(Catalogue::Parse(doc).IsBuyable("uni:hat_cap", kWindowStart));
    }
    SUBCASE("an unlisted item is not buyable") {
        json doc = ValidDoc();
        ItemAt(doc, "uni:hat_cap")["unlisted"] = true;
        CHECK_FALSE(Catalogue::Parse(doc).IsBuyable("uni:hat_cap", kWindowStart));
    }
    SUBCASE("before available_from it is not buyable") {
        json doc = ValidDoc();
        ItemAt(doc, "uni:hat_cap")["available_from"] = "2026-05-01T00:00:00Z";
        CHECK_FALSE(
            Catalogue::Parse(doc).IsBuyable("uni:hat_cap", kWindowStart - 1));
    }
    SUBCASE("after available_until it is not buyable") {
        json doc = ValidDoc();
        ItemAt(doc, "uni:hat_cap")["available_until"] = "2026-06-01T00:00:00Z";
        CHECK_FALSE(
            Catalogue::Parse(doc).IsBuyable("uni:hat_cap", kWindowEnd + 1));
    }
    SUBCASE("inside the window it is buyable") {
        json doc = ValidDoc();
        ItemAt(doc, "uni:hat_cap")["available_from"] = "2026-05-01T00:00:00Z";
        ItemAt(doc, "uni:hat_cap")["available_until"] = "2026-06-01T00:00:00Z";
        const Catalogue catalogue = Catalogue::Parse(doc);
        CHECK(catalogue.IsBuyable("uni:hat_cap", kWindowStart));
        CHECK(catalogue.IsBuyable("uni:hat_cap", kWindowEnd));
    }
    SUBCASE("an unknown id is not buyable") {
        const Catalogue catalogue = Catalogue::Parse(ValidDoc());
        CHECK_FALSE(catalogue.IsBuyable("uni:nope", kWindowStart));
    }
}

TEST_CASE("avatar catalogue: the real avatar/catalogue folder loads") {
    const fs::path root = ProjectRoot();
    REQUIRE_FALSE(root.empty());

    const Catalogue catalogue =
        Catalogue::LoadDirectory((root / "avatar" / "catalogue").string());

    REQUIRE(catalogue.FindItem("uni:eyes_dot") != nullptr);
    CHECK(catalogue.FindItem("uni:eyes_dot")->rarity == "basic");
    REQUIRE(catalogue.FindItem("uni:body_scarf") != nullptr);
    CHECK(catalogue.FindItem("uni:body_scarf")->rarity == "rare");
    REQUIRE(catalogue.FindItem("uni:hat_crown") != nullptr);
    CHECK(catalogue.FindItem("uni:hat_crown")->rarity == "epic");
    CHECK(catalogue.IsStarter("uni:eyes_dot"));
    CHECK_FALSE(catalogue.IsStarter("uni:hat_crown"));
    CHECK(catalogue.IsStarterSwatch("basic:red"));
    CHECK(catalogue.HasSwatch("uni:neon:hot"));
    REQUIRE(catalogue.SlotDefault("shape") != nullptr);
    CHECK(catalogue.SlotDefault("shape")->id == "uni:classic");
    CHECK(catalogue.SlotDefault("shape")->regions == 1);
    CHECK(catalogue.SlotDefault("shape")->tint == "multiply");
    REQUIRE(catalogue.SlotDefault("mouth") != nullptr);
    CHECK(catalogue.SlotDefault("mouth")->id == "uni:mouth_smile");

    const Item* patches = catalogue.FindItem("uni:motif_patches");
    REQUIRE(patches != nullptr);
    CHECK(patches->regions == 3);
    CHECK(patches->default_colors.size() == 2);
}

TEST_CASE("avatar catalogue folder: merge rules") {
    SUBCASE("a valid tree loads and order picks the slot default") {
        const Catalogue catalogue = Catalogue::LoadDirectory(
            WriteTree("valid", SmallTree()).string());
        REQUIRE(catalogue.SlotDefault("eyes") != nullptr);
        CHECK(catalogue.SlotDefault("eyes")->id == "uni:eyes_a");
    }
    SUBCASE("the merged document has no order field") {
        const json doc = Catalogue::MergeDirectory(WriteTree("noorder", SmallTree()).string());
        CHECK_FALSE(doc["items"][0].contains("order"));
        CHECK_FALSE(doc["palettes"][0].contains("order"));
        CHECK_FALSE(doc["shape_sockets"]["uni:round"].contains("shape"));
        CHECK(doc["items"][0]["id"] == "uni:round");
    }
    SUBCASE("a stray file is rejected") {
        auto files = SmallTree();
        files["notes/todo.json"] = "{}";
        CHECK_THROWS_WITH_AS(Catalogue::MergeDirectory(WriteTree("stray", files).string()),
                             doctest::Contains("unexpected file 'notes/todo.json'"),
                             std::runtime_error);
    }
    SUBCASE("a stem that does not match the id is rejected") {
        auto files = SmallTree();
        files["items/eyes/uni_eyes_a.json"] =
            R"({"id":"uni:eyes_z","order":10,"slot":"eyes"})";
        CHECK_THROWS_WITH_AS(Catalogue::MergeDirectory(WriteTree("stem", files).string()),
                             doctest::Contains("stem 'uni_eyes_a'"), std::runtime_error);
    }
    SUBCASE("a folder that differs from the slot is rejected") {
        auto files = SmallTree();
        files["items/eyes/uni_eyes_a.json"] =
            R"({"id":"uni:eyes_a","order":10,"slot":"mouth"})";
        CHECK_THROWS_WITH_AS(Catalogue::MergeDirectory(WriteTree("slot", files).string()),
                             doctest::Contains("folder 'eyes'"), std::runtime_error);
    }
    SUBCASE("two items with the same order in one slot are rejected") {
        auto files = SmallTree();
        files["items/eyes/uni_eyes_b.json"] =
            R"({"id":"uni:eyes_b","order":10,"slot":"eyes"})";
        CHECK_THROWS_WITH_AS(Catalogue::MergeDirectory(WriteTree("order", files).string()),
                             doctest::Contains("duplicate order 10 in slot eyes"),
                             std::runtime_error);
    }
    SUBCASE("a missing meta.json is rejected") {
        auto files = SmallTree();
        files.erase("meta.json");
        CHECK_THROWS_WITH_AS(Catalogue::MergeDirectory(WriteTree("meta", files).string()),
                             doctest::Contains("missing 'meta.json'"), std::runtime_error);
    }
    SUBCASE("a folder that does not exist is rejected") {
        CHECK_THROWS_AS(Catalogue::MergeDirectory("/no/such/uni/catalogue"),
                        std::runtime_error);
    }
}

TEST_CASE("avatar catalogue: tint mode") {
    SUBCASE("a missing tint is mask") {
        const Catalogue catalogue = Catalogue::Parse(ValidDoc());
        REQUIRE(catalogue.FindItem("uni:round") != nullptr);
        CHECK(catalogue.FindItem("uni:round")->tint == "mask");
    }
    SUBCASE("accepts multiply on a one-region item") {
        json doc = ValidDoc();
        ItemAt(doc, "uni:round")["tint"] = "multiply";
        const Catalogue catalogue = Catalogue::Parse(doc);
        CHECK(catalogue.FindItem("uni:round")->tint == "multiply");
    }
    SUBCASE("rejects an unknown tint") {
        json doc = ValidDoc();
        ItemAt(doc, "uni:round")["tint"] = "paint";
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("rejects a tint that is not a string") {
        json doc = ValidDoc();
        ItemAt(doc, "uni:round")["tint"] = 3;
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("rejects multiply with no region") {
        json doc = ValidDoc();
        json& item = ItemAt(doc, "uni:round");
        item["tint"] = "multiply";
        item["regions"] = 0;
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("rejects multiply with two regions") {
        json doc = ValidDoc();
        json& item = ItemAt(doc, "uni:round");
        item["tint"] = "multiply";
        item["regions"] = 2;
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("rejects multiply with an extra mask file") {
        json doc = ValidDoc();
        json& item = ItemAt(doc, "uni:round");
        item["tint"] = "multiply";
        item["asset"]["masks"] = json::array({"shapes/b.png"});
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
}

TEST_CASE("avatar catalogue: default colors") {
    SUBCASE("accepts a swatch id and a hex entry") {
        json doc = ValidDoc();
        json& item = ItemAt(doc, "uni:eyes_dot");
        item["regions"] = 2;
        item["default_colors"] = json::array({"basic:red", "#202020"});
        const Catalogue catalogue = Catalogue::Parse(doc);
        CHECK(catalogue.FindItem("uni:eyes_dot")->default_colors.size() == 2);
    }
    SUBCASE("an item with no regions is valid without default colors") {
        json doc = ValidDoc();
        doc["items"].push_back(json::parse(R"({
            "id": "uni:set_suit", "slot": "set", "name": "Suit",
            "asset": { "kind": "strip", "path": "set/uni_set_suit.png" },
            "regions": 0, "rarity": "rare", "price": 900
        })"));
        CHECK_NOTHROW(Catalogue::Parse(doc));
    }
    SUBCASE("rejects a swatch id no palette holds") {
        json doc = ValidDoc();
        ItemAt(doc, "uni:eyes_dot")["default_colors"] = json::array({"basic:nope"});
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("rejects a bad hex entry") {
        json doc = ValidDoc();
        ItemAt(doc, "uni:eyes_dot")["default_colors"] = json::array({"#12"});
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("rejects an entry that is not a string") {
        json doc = ValidDoc();
        ItemAt(doc, "uni:eyes_dot")["default_colors"] = json::array({7});
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("rejects a value that is not an array") {
        json doc = ValidDoc();
        ItemAt(doc, "uni:eyes_dot")["default_colors"] = "basic:red";
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("rejects more entries than regions") {
        json doc = ValidDoc();
        ItemAt(doc, "uni:eyes_dot")["default_colors"] =
            json::array({"basic:red", "basic:red"});
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("rejects a non-empty list on an item with no regions") {
        json doc = ValidDoc();
        json& item = ItemAt(doc, "uni:eyes_dot");
        item["regions"] = 0;
        item["default_colors"] = json::array({"basic:red"});
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
    SUBCASE("rejects default colors on a shape") {
        json doc = ValidDoc();
        ItemAt(doc, "uni:round")["default_colors"] = json::array({"basic:red"});
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
}

TEST_CASE("avatar catalogue: paid swatch metadata") {
    SUBCASE("a swatch takes a price, rarity, hidden and unlisted") {
        json doc = ValidDoc();
        json& swatch = doc["palettes"][1]["swatches"][0];
        swatch["price"] = 600;
        swatch["rarity"] = "rare";
        swatch["hidden"] = true;
        swatch["unlisted"] = true;

        const Catalogue catalogue = Catalogue::Parse(doc);
        CHECK(catalogue.PriceOf("uni:neon:hot").value() == 600);
        CHECK_FALSE(catalogue.IsBuyable("uni:neon:hot", 1000));
    }

    SUBCASE("a priced swatch is buyable") {
        json doc = ValidDoc();
        doc["palettes"][1]["swatches"][0]["price"] = 600;

        const Catalogue catalogue = Catalogue::Parse(doc);
        CHECK(catalogue.IsBuyable("uni:neon:hot", 1000));
        CHECK(catalogue.PriceOf("uni:neon:hot").value() == 600);
    }

    SUBCASE("a swatch with no price is not buyable") {
        const Catalogue catalogue = Catalogue::Parse(ValidDoc());
        CHECK_FALSE(catalogue.IsBuyable("uni:neon:hot", 1000));
        CHECK_FALSE(catalogue.PriceOf("uni:neon:hot").has_value());
    }

    SUBCASE("a starter swatch is not buyable") {
        json doc = ValidDoc();
        doc["palettes"][0]["swatches"][0]["price"] = 100;

        const Catalogue catalogue = Catalogue::Parse(doc);
        CHECK_FALSE(catalogue.IsBuyable("basic:red", 1000));
    }

    SUBCASE("an unknown rarity is rejected") {
        json doc = ValidDoc();
        doc["palettes"][1]["swatches"][0]["rarity"] = "mythic";
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }

    SUBCASE("a negative or fractional price is rejected") {
        json negative = ValidDoc();
        negative["palettes"][1]["swatches"][0]["price"] = -1;
        CHECK_THROWS_AS(Catalogue::Parse(negative), std::runtime_error);

        json fractional = ValidDoc();
        fractional["palettes"][1]["swatches"][0]["price"] = 1.5;
        CHECK_THROWS_AS(Catalogue::Parse(fractional), std::runtime_error);
    }

    SUBCASE("a swatch id may not equal an item id") {
        json doc = ValidDoc();
        doc["palettes"][1]["swatches"][0]["id"] = "uni:hat_cap";
        CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
    }
}

TEST_CASE("avatar catalogue: the five rarities") {
    SUBCASE("an item takes basic, common, rare, epic and special") {
        for (const char* rarity : {"basic", "common", "rare", "epic", "special"}) {
            json doc = ValidDoc();
            ItemAt(doc, "uni:hat_cap")["rarity"] = rarity;
            CHECK(Catalogue::Parse(doc).FindItem("uni:hat_cap")->rarity == rarity);
        }
    }
    SUBCASE("a swatch takes basic and special") {
        json doc = ValidDoc();
        doc["palettes"][0]["swatches"][0]["rarity"] = "basic";
        doc["palettes"][1]["swatches"][0]["rarity"] = "special";
        CHECK_NOTHROW(Catalogue::Parse(doc));
    }
    SUBCASE("a listed special item is buyable, a hidden or unlisted one is not") {
        json listed = ValidDoc();
        ItemAt(listed, "uni:hat_cap")["rarity"] = "special";
        CHECK(Catalogue::Parse(listed).IsBuyable("uni:hat_cap", 1000));

        json hidden = listed;
        ItemAt(hidden, "uni:hat_cap")["hidden"] = true;
        CHECK_FALSE(Catalogue::Parse(hidden).IsBuyable("uni:hat_cap", 1000));

        json unlisted = listed;
        ItemAt(unlisted, "uni:hat_cap")["unlisted"] = true;
        CHECK_FALSE(Catalogue::Parse(unlisted).IsBuyable("uni:hat_cap", 1000));
    }
    SUBCASE("the old uncommon rarity is rejected") {
        json item = ValidDoc();
        ItemAt(item, "uni:hat_cap")["rarity"] = "uncommon";
        CHECK_THROWS_AS(Catalogue::Parse(item), std::runtime_error);
        json swatch = ValidDoc();
        swatch["palettes"][1]["swatches"][0]["rarity"] = "uncommon";
        CHECK_THROWS_AS(Catalogue::Parse(swatch), std::runtime_error);
    }
}
