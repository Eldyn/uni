#include <doctest/doctest.h>

#include <avatar/catalogue.hpp>
#include <avatar/loadout.hpp>
#include <database.hpp>
#include <economy/economy_service.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <ctime>
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
using uni::economy::PopularItem;

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
                        "rarity": "rare"
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
                "rarity": "rare",
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
    auto wishlist = Database::Get().Exec(
        "DELETE FROM wishlist WHERE user_id IN "
        "(SELECT id FROM users WHERE username LIKE 'economy_test_%');");
    REQUIRE(wishlist.has_value());
    auto cosmetics = Database::Get().Exec(
        "DELETE FROM user_cosmetics WHERE user_id IN "
        "(SELECT id FROM users WHERE username LIKE 'economy_test_%');");
    REQUIRE(cosmetics.has_value());
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

/** @brief Insert one `item_ownership` row for `username` at `acquired_at`. */
void AddOwnership(const std::string& username, const std::string& item_id,
                  const std::string& source, std::int64_t acquired_at) {
    auto res = Database::Get().Exec(
        "INSERT INTO item_ownership (user_id, item_id, source, acquired_at) "
        "VALUES ((SELECT id FROM users WHERE username = ?), ?, ?, ?);",
        {username, item_id, source, static_cast<int>(acquired_at)});
    REQUIRE(res.has_value());
}

/** @brief Insert one `wishlist` row for `username` at `created_at`. */
void AddWishlistRow(const std::string& username, const std::string& item_id,
                    std::int64_t created_at) {
    auto res = Database::Get().Exec(
        "INSERT INTO wishlist (user_id, item_id, created_at) "
        "VALUES ((SELECT id FROM users WHERE username = ?), ?, ?);",
        {username, item_id, static_cast<int>(created_at)});
    REQUIRE(res.has_value());
}

/** @brief The ranking rows whose item id starts with `prefix`. */
std::vector<PopularItem> WithPrefix(const std::vector<PopularItem>& ranking,
                                    const std::string& prefix) {
    std::vector<PopularItem> picked;
    for (const PopularItem& entry : ranking) {
        if (entry.item_id.rfind(prefix, 0) == 0) picked.push_back(entry);
    }
    return picked;
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

TEST_CASE("economy: wishlist add and remove are idempotent") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string user = Name("wishlist");
    MakeUser(user);

    CHECK(economy.Wishlist(user).empty());
    CHECK(economy.AddWishlist(user, "uni:hat_cap"));
    CHECK(economy.AddWishlist(user, "uni:hat_cap"));
    CHECK(economy.Wishlist(user) == std::vector<std::string>{"uni:hat_cap"});

    CHECK(economy.AddWishlist(user, "uni:mask_visor"));
    CHECK(economy.Wishlist(user)
          == std::vector<std::string>{"uni:hat_cap", "uni:mask_visor"});

    CHECK(economy.RemoveWishlist(user, "uni:hat_cap"));
    CHECK(economy.RemoveWishlist(user, "uni:hat_cap"));
    CHECK(economy.Wishlist(user) == std::vector<std::string>{"uni:mask_visor"});
}

TEST_CASE("economy: wishlist rejects unknown users and non-catalogue items") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string user = Name("wishlist_bad");
    MakeUser(user);

    CHECK_FALSE(economy.AddWishlist(Name("ghost"), "uni:hat_cap"));
    CHECK_FALSE(economy.RemoveWishlist(Name("ghost"), "uni:hat_cap"));
    CHECK(economy.Wishlist(Name("ghost")).empty());

    CHECK_FALSE(economy.AddWishlist(user, "uni:nope"));
    CHECK_FALSE(economy.RemoveWishlist(user, "uni:nope"));
    CHECK(economy.Wishlist(user).empty());
}

TEST_CASE("economy: Popular ranks purchases in the window and counts wishlists") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string buyer_a = Name("popular_a");
    const std::string buyer_b = Name("popular_b");
    const std::string buyer_c = Name("popular_c");
    const std::string buyer_d = Name("popular_d");
    MakeUser(buyer_a);
    MakeUser(buyer_b);
    MakeUser(buyer_c);
    MakeUser(buyer_d);

    const std::int64_t now = std::time(nullptr);
    AddOwnership(buyer_a, "uni:popular_alpha", "purchase", now - 100);
    AddOwnership(buyer_b, "uni:popular_alpha", "purchase", now - 200);
    AddOwnership(buyer_c, "uni:popular_beta", "purchase", now - 300);
    AddOwnership(buyer_d, "uni:popular_beta", "grant", now - 100);
    AddOwnership(buyer_d, "uni:popular_gamma", "purchase", now - 2 * 86400);

    AddWishlistRow(buyer_a, "uni:popular_alpha", now - 100);
    AddWishlistRow(buyer_b, "uni:popular_alpha", now - 100);
    AddWishlistRow(buyer_c, "uni:popular_beta", now - 100);

    const std::vector<PopularItem> week =
        WithPrefix(economy.Popular(7, 50), "uni:popular_");
    REQUIRE(week.size() == 3);
    CHECK(week[0].item_id == "uni:popular_alpha");
    CHECK(week[0].purchases == 2);
    CHECK(week[0].wishlists == 2);
    CHECK(week[1].item_id == "uni:popular_beta");
    CHECK(week[1].purchases == 1);
    CHECK(week[1].wishlists == 1);
    CHECK(week[2].item_id == "uni:popular_gamma");
    CHECK(week[2].purchases == 1);
    CHECK(week[2].wishlists == 0);

    const std::vector<PopularItem> day =
        WithPrefix(economy.Popular(1, 50), "uni:popular_");
    REQUIRE(day.size() == 2);
    CHECK(day[0].item_id == "uni:popular_alpha");
    CHECK(day[1].item_id == "uni:popular_beta");
    CHECK(day[1].purchases == 1);
}

TEST_CASE("economy: Popular clamps days to 1..90 and limit to 1..50") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string user = Name("popular_clamp");
    MakeUser(user);

    const std::int64_t now = std::time(nullptr);
    AddOwnership(user, "uni:clamp_old", "purchase", now - 2 * 86400);
    AddOwnership(user, "uni:clamp_recent", "purchase", now - 100);

    const std::vector<PopularItem> one_day =
        WithPrefix(economy.Popular(0, 10), "uni:clamp_");
    REQUIRE(one_day.size() == 1);
    CHECK(one_day[0].item_id == "uni:clamp_recent");

    const std::vector<PopularItem> wide =
        WithPrefix(economy.Popular(500, 100), "uni:clamp_");
    REQUIRE(wide.size() == 2);
    CHECK(wide[0].item_id == "uni:clamp_old");
    CHECK(wide[1].item_id == "uni:clamp_recent");

    CHECK(economy.Popular(7, 0).size() == 1);
}

TEST_CASE("economy: Cosmetic returns the equipped slot or nothing") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string user = Name("cosmetic_read");
    MakeUser(user);

    CHECK_FALSE(economy.Cosmetic(user, "card_face").has_value());
    CHECK_FALSE(economy.Cosmetic(user, "card_back").has_value());
    CHECK_FALSE(economy.Cosmetic(Name("ghost"), "card_face").has_value());

    auto inserted = Database::Get().Exec(
        "INSERT INTO user_cosmetics (user_id, slot, item_id, updated_at) "
        "VALUES ((SELECT id FROM users WHERE username = ?), 'card_face', ?, 0);",
        {user, std::string("uni:hat_cap")});
    REQUIRE(inserted.has_value());

    const auto equipped = economy.Cosmetic(user, "card_face");
    REQUIRE(equipped.has_value());
    CHECK(*equipped == "uni:hat_cap");
    CHECK_FALSE(economy.Cosmetic(user, "card_back").has_value());
}

TEST_CASE("economy: SetCosmetic rejects bad slots, mismatched items and ghost users") {
    EconomyFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string user = Name("cosmetic_set");
    MakeUser(user);

    CHECK_FALSE(economy.SetCosmetic(user, "hat", "uni:hat_cap"));
    CHECK_FALSE(economy.SetCosmetic(user, "card_face", "uni:nope"));
    CHECK_FALSE(economy.SetCosmetic(user, "card_face", "uni:hat_cap"));
    CHECK_FALSE(economy.SetCosmetic(Name("ghost"), "card_face", "uni:hat_cap"));
    CHECK_FALSE(economy.Cosmetic(user, "card_face").has_value());
}
