#include <doctest/doctest.h>

#include <avatar/catalogue.hpp>
#include <database.hpp>
#include <economy/economy_service.hpp>
#include <economy/payout.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/**
 * @file economy_payout_test.cpp
 * @brief The match-end payout: base, win, the once-a-UTC-day bonus and the
 *        gates (creative, bot-only, one human, guests, quits). Every case runs
 *        against the shared in-memory DB (`test_main.cpp`) under usernames this
 *        file owns, so cases never see each other's rows.
 */

using nlohmann::json;
using uni::avatar::Catalogue;
using uni::economy::EconomyService;
using uni::economy::PayMatch;
using uni::economy::PayoutInput;
using uni::economy::PayoutParticipant;

namespace {

/** @brief A minimal valid catalogue; payouts never look an item up. */
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
            }
        ],
        "featured": []
    })");
}

/** @brief A username this test file owns, so cleanup stays scoped. */
std::string Name(const std::string& tag) { return "economy_payout_test_" + tag; }

/** @brief Drop every ledger, ownership and user row this file inserted. */
void CleanupRows() {
    auto ledger = Database::Get().Exec(
        "DELETE FROM cash_ledger WHERE user_id IN "
        "(SELECT id FROM users WHERE username LIKE 'economy_payout_test_%');");
    REQUIRE(ledger.has_value());
    auto owned = Database::Get().Exec(
        "DELETE FROM item_ownership WHERE user_id IN "
        "(SELECT id FROM users WHERE username LIKE 'economy_payout_test_%');");
    REQUIRE(owned.has_value());
    auto users = Database::Get().Exec(
        "DELETE FROM users WHERE username LIKE 'economy_payout_test_%';");
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
struct PayoutFixture {
    PayoutFixture() {
        REQUIRE(Database::Get().RunMigrations().has_value());
        CleanupRows();
    }
    ~PayoutFixture() { CleanupRows(); }
};

/** @brief A registered human participant. */
PayoutParticipant Human(const std::string& username, bool quit = false) {
    return PayoutParticipant{username, false, quit};
}

/** @brief A bot participant. */
PayoutParticipant Bot(const std::string& username) {
    return PayoutParticipant{username, true, false};
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

/** @brief The `ref` of the single `match_daily` row for `username`, if any. */
std::optional<std::string> DailyRef(const std::string& username) {
    auto row = Database::Get().QueryOne(
        "SELECT ref FROM cash_ledger WHERE source = 'match_daily' AND user_id = "
        "(SELECT id FROM users WHERE username = ?);",
        {username});
    if (!row.has_value() || !row->has_value()) return std::nullopt;
    return row.value()->Get<std::string>("ref");
}

}  // namespace

TEST_CASE("economy payout: two humans pay base, win and the first daily bonus") {
    PayoutFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string winner = Name("winner");
    const std::string loser = Name("loser");
    MakeUser(winner);
    MakeUser(loser);

    const std::int64_t now = 1700000000;  // 2023-11-14T22:13:20Z
    PayMatch(economy, PayoutInput{"match-1", false,
                                  {Human(winner), Human(loser)}, winner, now});

    CHECK(economy.Balance(winner) == 100);  // 20 base + 30 win + 50 daily
    CHECK(economy.Balance(loser) == 20);
    CHECK(LedgerCount(winner) == 3);
    CHECK(LedgerCount(loser) == 1);
    CHECK(DailyRef(winner) == "daily:2023-11-14");
    CHECK_FALSE(DailyRef(loser).has_value());
}

TEST_CASE("economy payout: a second win on the same UTC day pays no daily") {
    PayoutFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string winner = Name("day_winner");
    const std::string loser = Name("day_loser");
    MakeUser(winner);
    MakeUser(loser);

    const std::int64_t now = 1700000000;
    PayMatch(economy, PayoutInput{"match-1", false,
                                  {Human(winner), Human(loser)}, winner, now});
    PayMatch(economy, PayoutInput{"match-2", false,
                                  {Human(winner), Human(loser)}, winner,
                                  now + 3600});

    CHECK(economy.Balance(winner) == 150);  // 100 first win + 50 second
    CHECK(economy.Balance(loser) == 40);
    CHECK(DailyRef(winner) == "daily:2023-11-14");
}

TEST_CASE("economy payout: a win on the next UTC day pays the daily again") {
    PayoutFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string winner = Name("next_day_winner");
    const std::string loser = Name("next_day_loser");
    MakeUser(winner);
    MakeUser(loser);

    const std::int64_t now = 1700000000;
    PayMatch(economy, PayoutInput{"match-1", false,
                                  {Human(winner), Human(loser)}, winner, now});
    PayMatch(economy, PayoutInput{"match-2", false,
                                  {Human(winner), Human(loser)}, winner,
                                  now + 86400});

    CHECK(economy.Balance(winner) == 200);
    CHECK(economy.Balance(loser) == 40);
}

TEST_CASE("economy payout: creative matches pay nothing") {
    PayoutFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string winner = Name("creative_winner");
    const std::string loser = Name("creative_loser");
    MakeUser(winner);
    MakeUser(loser);

    PayMatch(economy, PayoutInput{"match-creative", true,
                                  {Human(winner), Human(loser)}, winner,
                                  1700000000});

    CHECK(economy.Balance(winner) == 0);
    CHECK(economy.Balance(loser) == 0);
    CHECK(LedgerCount(winner) == 0);
    CHECK(LedgerCount(loser) == 0);
}

TEST_CASE("economy payout: one human plus bots pays nothing") {
    PayoutFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string human = Name("lone_human");
    MakeUser(human);

    PayMatch(economy, PayoutInput{"match-bots", false,
                                  {Human(human), Bot("bot-1"), Bot("bot-2")},
                                  human, 1700000000});

    CHECK(economy.Balance(human) == 0);
    CHECK(LedgerCount(human) == 0);
}

TEST_CASE("economy payout: a participant who quit earns nothing") {
    PayoutFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string winner = Name("quit_winner");
    const std::string quitter = Name("quitter");
    MakeUser(winner);
    MakeUser(quitter);

    PayMatch(economy, PayoutInput{"match-quit", false,
                                  {Human(winner), Human(quitter, true)},
                                  winner, 1700000000});

    CHECK(economy.Balance(winner) == 100);
    CHECK(economy.Balance(quitter) == 0);
    CHECK(LedgerCount(quitter) == 0);
}

TEST_CASE("economy payout: guests earn nothing and cause no error") {
    PayoutFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string winner = Name("guest_winner");
    const std::string loser = Name("guest_loser");
    const std::string guest = Name("guest");
    MakeUser(winner);
    MakeUser(loser);

    PayMatch(economy, PayoutInput{"match-guest", false,
                                  {Human(winner), Human(loser), Human(guest)},
                                  winner, 1700000000});

    CHECK(economy.Balance(winner) == 100);
    CHECK(economy.Balance(loser) == 20);
    CHECK_FALSE(economy.Balance(guest).has_value());
    CHECK(LedgerCount(guest) == 0);
}

TEST_CASE("economy payout: one human and one guest pays nothing") {
    PayoutFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string human = Name("solo_human");
    const std::string guest = Name("solo_guest");
    MakeUser(human);

    PayMatch(economy, PayoutInput{"match-solo", false,
                                  {Human(human), Human(guest)}, human,
                                  1700000000});

    CHECK(economy.Balance(human) == 0);
    CHECK(LedgerCount(human) == 0);
}

TEST_CASE("economy payout: a repeated PayMatch for one match pays once") {
    PayoutFixture fixture;
    const Catalogue catalogue = Catalogue::Parse(ValidDoc());
    EconomyService economy(Database::Get(), catalogue);
    const std::string winner = Name("replay_winner");
    const std::string loser = Name("replay_loser");
    MakeUser(winner);
    MakeUser(loser);

    const PayoutInput input{"match-replay", false,
                            {Human(winner), Human(loser)}, winner, 1700000000};
    PayMatch(economy, input);
    PayMatch(economy, input);

    CHECK(economy.Balance(winner) == 100);
    CHECK(economy.Balance(loser) == 20);
    CHECK(LedgerCount(winner) == 3);
    CHECK(LedgerCount(loser) == 1);
}
