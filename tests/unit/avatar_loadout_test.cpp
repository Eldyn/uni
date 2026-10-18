#include <doctest/doctest.h>

#include <avatar/catalogue.hpp>
#include <avatar/loadout.hpp>
#include <avatar/loadout_repository.hpp>
#include <database.hpp>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

/**
 * @file avatar_loadout_test.cpp
 * @brief The server-side loadout rules (spec section 5): an atomic save that
 *        rejects unknown, misplaced, unowned or over-tinted choices, plus the
 *        stable guest default look.
 */

using nlohmann::json;
using uni::avatar::Catalogue;
using uni::avatar::GuestLoadout;
using uni::avatar::IOwnership;
using uni::avatar::LoadoutError;
using uni::avatar::LoadoutRepository;
using uni::avatar::StarterOwnership;
using uni::avatar::ValidateLoadout;

namespace {

/** @brief A small catalogue that passes every rule, as a JSON document. */
json ValidDoc() {
    return json::parse(R"({
        "version": 1,
        "shape_sockets": {
            "uni:round": {
                "eyes": [36, 26],
                "mouth": [42, 40],
                "head_top": [32, 4],
                "body_centre": [48, 62]
            },
            "uni:tall": {
                "eyes": [36, 20],
                "mouth": [42, 34],
                "head_top": [32, -2],
                "body_centre": [48, 60]
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
                "id": "uni:tall",
                "slot": "shape",
                "name": "Tall",
                "asset": { "kind": "strip", "path": "shapes/uni_tall.png" },
                "regions": 1,
                "rarity": "uncommon",
                "price": 120
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
                "id": "uni:eyes_wide",
                "slot": "eyes",
                "name": "Wide eyes",
                "asset": { "kind": "strip", "path": "eyes/uni_eyes_wide.png" },
                "regions": 1,
                "rarity": "common",
                "price": 50,
                "shapes": ["uni:tall"]
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
            },
            {
                "id": "uni:motif_patches",
                "slot": "motif",
                "name": "Patches",
                "asset": { "kind": "strip", "path": "motif/uni_motif_patches.png" },
                "regions": 3,
                "rarity": "epic",
                "price": 500
            }
        ],
        "featured": ["uni:hat_cap"]
    })");
}

/** @brief An ownership fake that owns every item and swatch. */
class OwnsEverything : public IOwnership {
  public:
    bool OwnsItem(const std::string&, const std::string&) const override {
        return true;
    }
    bool OwnsSwatch(const std::string&, const std::string&) const override {
        return true;
    }
};

/** @brief A loadout with `shape` and `skin` and an empty `slots` object. */
json Loadout(const std::string& shape, const std::string& skin) {
    return json{{"shape", shape}, {"skin", skin}, {"slots", json::object()}};
}

/** @brief A slot choice with no colours. */
json Choice(const std::string& item) {
    return json{{"item", item}, {"colours", json::array()}};
}

/** @brief Assert that validation rejected the loadout with `code`. */
void CheckRejected(const std::optional<LoadoutError>& error,
                   const std::string& code) {
    REQUIRE(error.has_value());
    CHECK(error->code == code);
    CHECK_FALSE(error->detail.empty());
}

/** @brief The username the repository cases insert and read back. */
const char* kRepoUser = "avatar_loadout_test_alice";

/** @brief Drop every row the repository cases own, everywhere they run. */
void CleanupRepoRows() {
    auto result = Database::Get().Exec(
        "DELETE FROM users WHERE username LIKE 'avatar_loadout_test_%';");
    REQUIRE(result.has_value());
}

/** @brief A migrated DB with one registered user and no saved loadout. */
struct RepoFixture {
    RepoFixture() {
        REQUIRE(Database::Get().RunMigrations().has_value());
        CleanupRepoRows();
        auto res = Database::Get().Exec(
            "INSERT INTO users (username, pass_hash, salt, email, "
            "email_verified, created_at) VALUES (?, 'hash', 'salt', ?, 1, 0);",
            {std::string(kRepoUser), "avatar_loadout_test_alice@example.com"});
        REQUIRE(res.has_value());
    }
    ~RepoFixture() { CleanupRepoRows(); }
};

}  // namespace

TEST_CASE("avatar loadout: accepts a starter-only loadout") {
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    const StarterOwnership ownership(catalogue);

    json loadout = Loadout("uni:round", "basic:red");
    loadout["slots"]["eyes"] = Choice("uni:eyes_dot");
    loadout["slots"]["mouth"] = Choice("uni:mouth_smile");

    CHECK_FALSE(
        ValidateLoadout(loadout, catalogue, ownership, "alice").has_value());
}

TEST_CASE("avatar loadout: accepts an empty slots object") {
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    const StarterOwnership ownership(catalogue);

    const json loadout = Loadout("uni:round", "basic:blue");

    CHECK_FALSE(
        ValidateLoadout(loadout, catalogue, ownership, "alice").has_value());
}

TEST_CASE("avatar loadout: accepts owned items and swatches") {
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    const OwnsEverything ownership;

    json loadout = Loadout("uni:round", "basic:red");
    loadout["slots"]["head_top"] = Choice("uni:hat_cap");
    loadout["slots"]["motif"] = Choice("uni:motif_patches");
    loadout["slots"]["motif"]["colours"] =
        json::array({"basic:blue", "uni:neon:hot", "basic:red"});

    CHECK_FALSE(
        ValidateLoadout(loadout, catalogue, ownership, "alice").has_value());
}

TEST_CASE("avatar loadout: rejects a non-object loadout as bad_json") {
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    const StarterOwnership ownership(catalogue);

    CheckRejected(
        ValidateLoadout(json::array(), catalogue, ownership, "alice"),
        "bad_json");
}

TEST_CASE("avatar loadout: rejects unknown_shape") {
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    const StarterOwnership ownership(catalogue);

    SUBCASE("an id that is not in the catalogue") {
        CheckRejected(
            ValidateLoadout(Loadout("uni:nope", "basic:red"), catalogue,
                            ownership, "alice"),
            "unknown_shape");
    }
    SUBCASE("a missing shape") {
        json loadout = Loadout("uni:round", "basic:red");
        loadout.erase("shape");
        CheckRejected(ValidateLoadout(loadout, catalogue, ownership, "alice"),
                      "unknown_shape");
    }
    SUBCASE("an item that is not in the shape slot") {
        CheckRejected(
            ValidateLoadout(Loadout("uni:eyes_dot", "basic:red"), catalogue,
                            ownership, "alice"),
            "unknown_shape");
    }
}

TEST_CASE("avatar loadout: rejects missing_skin") {
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    const StarterOwnership ownership(catalogue);

    SUBCASE("an unknown swatch") {
        CheckRejected(
            ValidateLoadout(Loadout("uni:round", "uni:nope"), catalogue,
                            ownership, "alice"),
            "missing_skin");
    }
    SUBCASE("a missing skin") {
        json loadout = Loadout("uni:round", "basic:red");
        loadout.erase("skin");
        CheckRejected(ValidateLoadout(loadout, catalogue, ownership, "alice"),
                      "missing_skin");
    }
}

TEST_CASE("avatar loadout: rejects unknown_item") {
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    const StarterOwnership ownership(catalogue);

    json loadout = Loadout("uni:round", "basic:red");
    loadout["slots"]["eyes"] = Choice("uni:nope");

    CheckRejected(ValidateLoadout(loadout, catalogue, ownership, "alice"),
                  "unknown_item");
}

TEST_CASE("avatar loadout: rejects wrong_slot") {
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    const StarterOwnership ownership(catalogue);

    SUBCASE("a hat in the eyes slot") {
        json loadout = Loadout("uni:round", "basic:red");
        loadout["slots"]["eyes"] = Choice("uni:hat_cap");
        CheckRejected(ValidateLoadout(loadout, catalogue, ownership, "alice"),
                      "wrong_slot");
    }
    SUBCASE("a slot key that is not a loadout slot") {
        json loadout = Loadout("uni:round", "basic:red");
        loadout["slots"]["ears"] = Choice("uni:eyes_dot");
        CheckRejected(ValidateLoadout(loadout, catalogue, ownership, "alice"),
                      "wrong_slot");
    }
}

TEST_CASE("avatar loadout: rejects shape_unsupported") {
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    const StarterOwnership ownership(catalogue);

    json loadout = Loadout("uni:round", "basic:red");
    loadout["slots"]["eyes"] = Choice("uni:eyes_wide");

    CheckRejected(ValidateLoadout(loadout, catalogue, ownership, "alice"),
                  "shape_unsupported");
}

TEST_CASE("avatar loadout: rejects not_owned") {
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    const StarterOwnership ownership(catalogue);

    json loadout = Loadout("uni:round", "basic:red");
    loadout["slots"]["head_top"] = Choice("uni:hat_cap");

    CheckRejected(ValidateLoadout(loadout, catalogue, ownership, "alice"),
                  "not_owned");
}

TEST_CASE("avatar loadout: rejects unknown_swatch") {
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    const StarterOwnership ownership(catalogue);

    json loadout = Loadout("uni:round", "basic:red");
    loadout["slots"]["mouth"] = Choice("uni:mouth_smile");
    loadout["slots"]["mouth"]["colours"] = json::array({"uni:nope"});

    CheckRejected(ValidateLoadout(loadout, catalogue, ownership, "alice"),
                  "unknown_swatch");
}

TEST_CASE("avatar loadout: rejects swatch_not_owned") {
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    const StarterOwnership ownership(catalogue);

    SUBCASE("a paid colour on an item") {
        json loadout = Loadout("uni:round", "basic:red");
        loadout["slots"]["mouth"] = Choice("uni:mouth_smile");
        loadout["slots"]["mouth"]["colours"] = json::array({"uni:neon:hot"});
        CheckRejected(ValidateLoadout(loadout, catalogue, ownership, "alice"),
                      "swatch_not_owned");
    }
    SUBCASE("an unowned skin") {
        CheckRejected(
            ValidateLoadout(Loadout("uni:round", "uni:neon:hot"), catalogue,
                            ownership, "alice"),
            "swatch_not_owned");
    }
}

TEST_CASE("avatar loadout: rejects too_many_colours") {
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    const OwnsEverything ownership;

    json loadout = Loadout("uni:round", "basic:red");
    loadout["slots"]["motif"] = Choice("uni:motif_patches");
    loadout["slots"]["motif"]["colours"] =
        json::array({"basic:red", "basic:blue", "uni:neon:hot", "basic:red"});

    CheckRejected(ValidateLoadout(loadout, catalogue, ownership, "alice"),
                  "too_many_colours");
}

TEST_CASE("avatar loadout: GuestLoadout always validates and uses a basic swatch") {
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    const StarterOwnership ownership(catalogue);

    const json guest = GuestLoadout(catalogue, 0);

    CHECK(catalogue.IsStarterSwatch(guest["skin"]));
    CHECK(guest["shape"] == "uni:round");
    CHECK(guest["slots"]["eyes"]["item"] == "uni:eyes_dot");
    CHECK(guest["slots"]["mouth"]["item"] == "uni:mouth_smile");
    CHECK_FALSE(
        ValidateLoadout(guest, catalogue, ownership, "guest").has_value());
}

TEST_CASE("avatar loadout: GuestLoadout is stable for one seed and varies over seeds") {
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());

    CHECK(GuestLoadout(catalogue, 7) == GuestLoadout(catalogue, 7));
    CHECK(GuestLoadout(catalogue, 0)["skin"] != GuestLoadout(catalogue, 1)["skin"]);
    CHECK(GuestLoadout(catalogue, 4)["skin"] == GuestLoadout(catalogue, 0)["skin"]);
}

TEST_CASE("avatar loadout repository: Get is nullopt for a new user") {
    RepoFixture fixture;
    LoadoutRepository repo;

    CHECK_FALSE(repo.Get(kRepoUser).has_value());
}

TEST_CASE("avatar loadout repository: Save then Get round-trips") {
    RepoFixture fixture;
    LoadoutRepository repo;

    const json loadout = Loadout("uni:round", "basic:red");
    CHECK(repo.Save(kRepoUser, loadout));
    CHECK(repo.Get(kRepoUser) == loadout);
}

TEST_CASE("avatar loadout repository: Save returns false for an unknown user") {
    RepoFixture fixture;
    LoadoutRepository repo;

    CHECK_FALSE(repo.Save("avatar_loadout_test_nobody",
                          Loadout("uni:round", "basic:red")));
}

TEST_CASE("avatar loadout repository: Get is nullopt for corrupt JSON text") {
    RepoFixture fixture;
    LoadoutRepository repo;

    auto res = Database::Get().Exec(
        "UPDATE users SET avatar_loadout = ? WHERE username = ?;",
        {std::string("{not json"), std::string(kRepoUser)});
    REQUIRE(res.has_value());

    CHECK_FALSE(repo.Get(kRepoUser).has_value());
}

TEST_CASE("avatar loadout repository: Save overwrites the previous loadout") {
    RepoFixture fixture;
    LoadoutRepository repo;

    REQUIRE(repo.Save(kRepoUser, Loadout("uni:round", "basic:red")));

    json second = Loadout("uni:tall", "basic:blue");
    second["slots"]["mouth"] = Choice("uni:mouth_smile");
    REQUIRE(repo.Save(kRepoUser, second));

    CHECK(repo.Get(kRepoUser) == second);
}
