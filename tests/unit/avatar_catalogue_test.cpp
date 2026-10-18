#include <doctest/doctest.h>

#include <avatar/catalogue.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
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
 *       `avatar/catalogue.json` loads cwd-independently (mirrors
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
                "body_centre": [48, 62]
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

TEST_CASE("avatar catalogue: rejects regions above 3") {
    json doc = ValidDoc();
    ItemAt(doc, "uni:eyes_dot")["regions"] = 4;
    CHECK_THROWS_AS(Catalogue::Parse(doc), std::runtime_error);
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
        doc["shape_sockets"]["uni:round"].erase("body_centre");
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

TEST_CASE("avatar catalogue: the real avatar/catalogue.json loads") {
    const fs::path root = ProjectRoot();
    REQUIRE_FALSE(root.empty());

    const Catalogue catalogue =
        Catalogue::LoadFile((root / "avatar" / "catalogue.json").string());

    REQUIRE(catalogue.FindItem("uni:eyes_dot") != nullptr);
    CHECK(catalogue.IsStarter("uni:eyes_dot"));
    CHECK_FALSE(catalogue.IsStarter("uni:hat_crown"));
    CHECK(catalogue.IsStarterSwatch("basic:red"));
    CHECK(catalogue.HasSwatch("uni:neon:hot"));
    REQUIRE(catalogue.SlotDefault("shape") != nullptr);
    CHECK(catalogue.SlotDefault("shape")->id == "uni:round");
    REQUIRE(catalogue.SlotDefault("mouth") != nullptr);
    CHECK(catalogue.SlotDefault("mouth")->id == "uni:mouth_smile");

    const Item* patches = catalogue.FindItem("uni:motif_patches");
    REQUIRE(patches != nullptr);
    CHECK(patches->regions == 3);
}
