#include <doctest/doctest.h>

#include <avatar/catalogue.hpp>
#include <avatar/loadout.hpp>
#include <database.hpp>
#include <economy/economy_service.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

/**
 * @file economy_service_test.cpp
 * @brief The ledger service: credit idempotency, the buy transaction and the
 *        ownership reads. Every case runs against the shared in-memory DB
 *        (`test_main.cpp`) under usernames this file owns, so cases never see
 *        each other's rows.
 */

using nlohmann::json;
using uni::avatar::Catalogue;
using uni::avatar::ValidateLoadout;
using uni::economy::BuyStatus;
using uni::economy::EconomyService;
using uni::economy::LedgerRow;

namespace {

/** @brief A tiny catalogue with three starters, three buyable items, one hidden. */
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
                    {
                        "id": "uni:neon:hot",
                        "hex": "#ff2d95",
                        "price": 250,
                        "rarity": "uncommon"
                    },
                    {
                        "id": "uni:neon:secret",
                        "hex": "#00e5ff",
                        "price": 600,
                        "rarity": "rare",
                        "hidden": true
                    }
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
                "asset": { "kind": "strip", "path": "head_top/uni_hat_cap.png" },
                "regions": 1,
                "rarity": "common",
                "price": 100
            },
            {
                "id": "uni:mask_visor",
                "slot": "mask",
                "name": "Visor",
                "asset": { "kind": "strip", "path": "mask/uni_mask_visor.png" },
                "regions": 1,
                "rarity": "uncommon",
                "price": 250
            },
            {
                "id": "uni:body_suit",
                "slot": "bodywear",
                "name": "Suit",
                "asset": { "kind": "strip", "path": "bodywear/uni_body_suit.png" },
                "regions": 0,
                "rarity": "rare",
                "price": 600
            },
            {
                "id": "uni:hat_hidden",
                "slot": "head_top",
                "name": "Secret cap",
                "asset": { "kind": "strip", "path": "head_top/uni_hat_hidden.png" },
                "regions": 1,
                "rarity": "common",
                "price": 100,
                "hidden": true
            }
        ],
        "featured": ["uni:hat_cap"]
    })");
}

/** @brief A username this test file owns, so cleanup stays scoped. */
std::string Name(const std::string& tag) { return "economy_test_" + tag; }

/** @brief Drop every ledger, ownership and user row this file inserted. */
void CleanupRows() {
    auto ledger = Database::Get().Exec(
        "DELETE FROM cash_ledger WHERE user_id IN "
        "(SELECT id FROM users WHERE username LIKE 'economy_test_%');");
    REQUIRE(ledger.has_value());
    auto owned = Database::Get().Exec(
        "DELETE FROM item_ownership WHERE user_id IN "
        "(SELECT id FROM users WHERE username LIKE 'economy_test_%');");
    REQUIRE(owned.has_value());
    auto users = Database::Get().Exec(
        "DELETE FROM users WHERE username LIKE 'economy_test_%';");
    REQUIRE(users.has_value());
}

/** @brief Register one account with no cash. */
void MakeUser(const std::string& username) {
    auto res = Database::Get().Exec(
        "INSERT INTO users (username, pass_hash, salt, email, email_verified, "
        "created_at) VALUES (?, 'hash', 'salt', ?, 1, 0);",
        {username, username + "@example.test"});
    REQUIRE(res.has_value());
}

/** @brief Migrate, clear the file's rows, and clear again on teardown. */
struct EconomyFixture {
    EconomyFixture() {
        REQUIRE(Database::Get().RunMigrations().has_value());
        CleanupRows();
    }
    ~EconomyFixture() { CleanupRows(); }
};

/** @brief The ledger sum for `username`, the value the cache must match. */
std::int64_t LedgerSum(const std::string& username) {
    auto row = Database::Get().QueryOne(
        "SELECT COALESCE(SUM(amount), 0) AS total FROM cash_ledger "
        "WHERE user_id = (SELECT id FROM users WHERE username = ?);",
        {username});
    REQUIRE(row.has_value());
    REQUIRE(row->has_value());
    return row.value()->Get<int>("total");
}

/** @brief The number of ledger rows for `username`. */
int LedgerCount(const std::string& username) {
    auto row = Database::Get().QueryOne(
        "SELECT COUNT(*) AS count FROM cash_ledger "
        "WHERE user_id = (SELECT id FROM users WHERE username = ?);",
        {username});
    REQUIRE(row.has_value());
    REQUIRE(row->has_value());
    return row.value()->Get<int>("count");
}

/** @brief The number of ownership rows for `username`. */
int OwnedCount(const std::string& username) {
    auto row = Database::Get().QueryOne(
        "SELECT COUNT(*) AS count FROM item_ownership "
        "WHERE user_id = (SELECT id FROM users WHERE username = ?);",
        {username});
    REQUIRE(row.has_value());
    REQUIRE(row->has_value());
    return row.value()->Get<int>("count");
}

}  // namespace

TEST_CASE("economy: Credit adds the amount and one ledger row") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string user = Name("credit");
    MakeUser(user);

    CHECK(economy.Credit(user, 20, "match_base", "match-1", 1000));
    CHECK(economy.Balance(user) == 20);
    CHECK(LedgerCount(user) == 1);

    CHECK(economy.Credit(user, 30, "match_win", "match-1", 1000));
    CHECK(economy.Balance(user) == 50);
    CHECK(LedgerCount(user) == 2);
}

TEST_CASE("economy: a repeated Credit ref returns false and leaves the balance") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string user = Name("credit_repeat");
    MakeUser(user);

    CHECK(economy.Credit(user, 20, "match_base", "match-1", 1000));
    CHECK_FALSE(economy.Credit(user, 20, "match_base", "match-1", 1000));
    CHECK(economy.Balance(user) == 20);
    CHECK(LedgerCount(user) == 1);
}

TEST_CASE("economy: Credit and Balance fail for a user without a row") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);

    CHECK_FALSE(economy.Credit(Name("ghost"), 20, "match_base", "m", 1000));
    CHECK_FALSE(economy.Balance(Name("ghost")).has_value());
}

TEST_CASE("economy: Buy exactly at price leaves balance 0") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string user = Name("exact");
    MakeUser(user);
    REQUIRE(economy.Credit(user, 100, "grant", "seed", 1000));

    const auto result = economy.Buy(user, "uni:hat_cap", "req-exact", 1000);
    CHECK(result.status == BuyStatus::kOk);
    CHECK(result.balance == 0);
    CHECK(economy.Balance(user) == 0);
    CHECK(economy.OwnsItem(user, "uni:hat_cap"));
}

TEST_CASE("economy: Buy one cash short returns kInsufficient and writes nothing") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string user = Name("short");
    MakeUser(user);
    REQUIRE(economy.Credit(user, 99, "grant", "seed", 1000));

    const auto result = economy.Buy(user, "uni:hat_cap", "req-short", 1000);
    CHECK(result.status == BuyStatus::kInsufficient);
    CHECK(economy.Balance(user) == 99);
    CHECK(LedgerCount(user) == 1);
    CHECK(OwnedCount(user) == 0);
}

TEST_CASE("economy: the same request id buys once and replays with the balance") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string user = Name("replay");
    MakeUser(user);
    REQUIRE(economy.Credit(user, 300, "grant", "seed", 1000));

    const auto first = economy.Buy(user, "uni:hat_cap", "req-1", 1000);
    CHECK(first.status == BuyStatus::kOk);
    CHECK(first.balance == 200);

    const auto second = economy.Buy(user, "uni:hat_cap", "req-1", 1000);
    CHECK(second.status == BuyStatus::kOk);
    CHECK(second.balance == 200);

    CHECK(economy.Balance(user) == 200);
    CHECK(OwnedCount(user) == 1);
    auto purchases = Database::Get().QueryOne(
        "SELECT COUNT(*) AS count FROM cash_ledger "
        "WHERE source = 'purchase' AND user_id = "
        "(SELECT id FROM users WHERE username = ?);",
        {user});
    REQUIRE(purchases.has_value());
    REQUIRE(purchases->has_value());
    CHECK(purchases.value()->Get<int>("count") == 1);
}

TEST_CASE("economy: Buy of a starter or an owned item returns kAlreadyOwned") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string user = Name("owned");
    MakeUser(user);
    REQUIRE(economy.Credit(user, 500, "grant", "seed", 1000));

    const auto starter = economy.Buy(user, "uni:eyes_dot", "req-starter", 1000);
    CHECK(starter.status == BuyStatus::kAlreadyOwned);

    REQUIRE(economy.Buy(user, "uni:hat_cap", "req-first", 1000).status
            == BuyStatus::kOk);
    const auto again = economy.Buy(user, "uni:hat_cap", "req-second", 1000);
    CHECK(again.status == BuyStatus::kAlreadyOwned);
    CHECK(economy.Balance(user) == 400);
}

TEST_CASE("economy: Buy of a hidden item returns kNotAvailable") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string user = Name("hidden");
    MakeUser(user);
    REQUIRE(economy.Credit(user, 500, "grant", "seed", 1000));

    const auto result = economy.Buy(user, "uni:hat_hidden", "req-hidden", 1000);
    CHECK(result.status == BuyStatus::kNotAvailable);
    CHECK(OwnedCount(user) == 0);
}

TEST_CASE("economy: Buy of an unknown id returns kUnknownItem") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string user = Name("unknown");
    MakeUser(user);

    const auto result = economy.Buy(user, "uni:nope", "req-nope", 1000);
    CHECK(result.status == BuyStatus::kUnknownItem);
}

TEST_CASE("economy: Buy for a user without a row returns kNoAccount") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);

    const auto result = economy.Buy(Name("ghost"), "uni:hat_cap", "r", 1000);
    CHECK(result.status == BuyStatus::kNoAccount);
}

TEST_CASE("economy: GrantItem owns once with no ledger row") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string user = Name("grant");
    MakeUser(user);

    CHECK(economy.GrantItem(user, "uni:hat_cap", "achievement", 1000));
    CHECK_FALSE(economy.GrantItem(user, "uni:hat_cap", "achievement", 1000));
    CHECK(economy.OwnsItem(user, "uni:hat_cap"));
    CHECK(OwnedCount(user) == 1);
    CHECK(LedgerCount(user) == 0);
}

TEST_CASE("economy: OwnsItem is true for starters and owned items only") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string user = Name("owns");
    MakeUser(user);

    CHECK(economy.OwnsItem(user, "uni:eyes_dot"));
    CHECK_FALSE(economy.OwnsItem(user, "uni:hat_cap"));
    CHECK_FALSE(economy.OwnsItem(user, "uni:nope"));
    CHECK_FALSE(economy.OwnsItem(Name("ghost"), "uni:hat_cap"));

    REQUIRE(economy.GrantItem(user, "uni:hat_cap", "reason", 1000));
    CHECK(economy.OwnsItem(user, "uni:hat_cap"));
    CHECK_FALSE(economy.OwnsItem(user, "uni:mask_visor"));
}

TEST_CASE("economy: Buy of a paid swatch owns the swatch id") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string user = Name("swatch_buy");
    MakeUser(user);
    REQUIRE(economy.Credit(user, 250, "grant", "seed", 1000));

    const auto result = economy.Buy(user, "uni:neon:hot", "req-swatch", 1000);
    CHECK(result.status == BuyStatus::kOk);
    CHECK(result.balance == 0);
    CHECK(economy.OwnsSwatch(user, "uni:neon:hot"));

    auto row = Database::Get().QueryOne(
        "SELECT item_id FROM item_ownership WHERE user_id = "
        "(SELECT id FROM users WHERE username = ?);",
        {user});
    REQUIRE(row.has_value());
    REQUIRE(row->has_value());
    CHECK(row.value()->Get<std::string>("item_id") == "uni:neon:hot");
}

TEST_CASE("economy: OwnsSwatch is true for starters and bought swatches") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string user = Name("swatch_owns");
    MakeUser(user);

    CHECK(economy.OwnsSwatch(user, "basic:red"));
    CHECK_FALSE(economy.OwnsSwatch(user, "uni:neon:hot"));
    REQUIRE(economy.Credit(user, 250, "grant", "seed", 1000));
    REQUIRE(economy.Buy(user, "uni:neon:hot", "req-owns", 1000).status
            == BuyStatus::kOk);
    CHECK(economy.OwnsSwatch(user, "uni:neon:hot"));
}

TEST_CASE("economy: Buy of a hidden swatch returns kNotAvailable") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string user = Name("swatch_hidden");
    MakeUser(user);
    REQUIRE(economy.Credit(user, 600, "grant", "seed", 1000));

    const auto result = economy.Buy(user, "uni:neon:secret", "req-secret", 1000);
    CHECK(result.status == BuyStatus::kNotAvailable);
    CHECK(economy.Balance(user) == 600);
}

TEST_CASE("economy: a bought swatch passes loadout validation") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string user = Name("swatch_loadout");
    MakeUser(user);
    REQUIRE(economy.Credit(user, 250, "grant", "seed", 1000));

    const json loadout = json{{"shape", "uni:round"}, {"skin", "uni:neon:hot"}};
    const auto before = ValidateLoadout(loadout, catalogue, economy, user);
    REQUIRE(before.has_value());
    CHECK(before->code == "swatch_not_owned");

    REQUIRE(economy.Buy(user, "uni:neon:hot", "req-loadout", 1000).status
            == BuyStatus::kOk);
    CHECK_FALSE(ValidateLoadout(loadout, catalogue, economy, user).has_value());
}

TEST_CASE("economy: random credits and buys keep the cache equal to the ledger") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string user = Name("property");
    MakeUser(user);

    const std::vector<std::string> buyable = {
        "uni:hat_cap", "uni:mask_visor", "uni:body_suit"};

    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> coin(1, 250);
    std::uniform_int_distribution<int> choice(0, 3);

    for (int i = 0; i < 200; ++i) {
        const int pick = choice(rng);
        if (pick == 0) {
            economy.Credit(user, coin(rng), "grant", "g-" + std::to_string(i), 1000 + i);
        } else {
            const std::string& item = buyable[coin(rng) % buyable.size()];
            const BuyStatus expected = economy.OwnsItem(user, item)
                                           ? BuyStatus::kAlreadyOwned
                                           : (economy.Balance(user).value_or(0)
                                                          < (item == "uni:hat_cap"
                                                                 ? 100
                                                                 : (item == "uni:mask_visor"
                                                                        ? 250
                                                                        : 600))
                                                          ? BuyStatus::kInsufficient
                                                          : BuyStatus::kOk);
            const auto result =
                economy.Buy(user, item, "b-" + std::to_string(i), 1000 + i);
            CHECK(result.status == expected);
        }

        const std::int64_t balance = economy.Balance(user).value_or(-1);
        CHECK(balance >= 0);
        CHECK(balance == LedgerSum(user));
    }
}
