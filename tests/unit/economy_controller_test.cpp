#include <doctest/doctest.h>

#include <avatar/catalogue.hpp>
#include <controllers/economy_controller.hpp>
#include <database.hpp>
#include <economy/economy_service.hpp>
#include <http_router.hpp>
#include <services/auth_service.hpp>
#include <websocket_context.hpp>
#include <App.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

/**
 * @file economy_controller_test.cpp
 * @brief The economy HTTP API (spec sections 5 and 7): read a private balance,
 *        the owned ids and the recent ledger; buy once per request id.
 *
 * Follows `avatar_controller_test.cpp`: a real TLS uWS server on a local port,
 * driven with curl, because the handlers need a live `AppRequest` /
 * `AppResponse` pair.
 */

using nlohmann::json;
using uni::avatar::Catalogue;
using uni::economy::EconomyService;

namespace {

constexpr int kTestPort = 49162;
constexpr const char* kMePath = "/economy/me";
constexpr const char* kBuyPath = "/shop/buy";

/** @brief A tiny catalogue with starters, two buyable items and one hidden. */
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

/** @brief One parsed HTTP response: status, headers and the JSON body. */
struct TestHttpResponse {
    int status{0};
    std::multimap<std::string, std::string> headers;
    json body;
};

/** @brief Run `cmd` through a pipe and return its whole stdout. */
std::string Exec(const char* cmd) {
    std::array<char, 256> buffer;
    std::string result;
    std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) throw std::runtime_error("popen() failed!");
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

/** @brief Drive one request with curl, the way `auth_controller_test` does. */
TestHttpResponse Curl(const std::string& method, const std::string& path,
                      const std::string& cookies, const std::string& body) {
    std::string cmd = "curl -s -i -k -X " + method +
                      " -H 'Content-Type: application/json' ";
    if (!cookies.empty()) cmd += "-H 'Cookie: " + cookies + "' ";
    if (!body.empty()) cmd += "-d '" + body + "' ";
    cmd += "https://127.0.0.1:" + std::to_string(kTestPort) + path;

    const std::string output = Exec(cmd.c_str());
    TestHttpResponse res;
    std::istringstream iss(output);
    std::string line;
    bool headers_done = false;
    std::string body_str;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (res.status == 0 && line.starts_with("HTTP/")) {
            const auto pos = line.find(' ');
            if (pos != std::string::npos) res.status = std::stoi(line.substr(pos + 1, 3));
            continue;
        }
        if (line.empty()) {
            headers_done = true;
            continue;
        }
        if (!headers_done) {
            const auto pos = line.find(": ");
            if (pos != std::string::npos) {
                res.headers.insert({line.substr(0, pos), line.substr(pos + 2)});
            }
        } else {
            body_str += line;
        }
    }
    if (!body_str.empty()) {
        try {
            res.body = json::parse(body_str);
        } catch (...) {}
    }
    return res;
}

/** @brief A registered username this file owns, so cleanup stays scoped. */
std::string Name(const std::string& tag) { return "economy_ctrl_" + tag; }

/** @brief Drop every ledger, ownership and user row this file inserted. */
void CleanupRows() {
    REQUIRE(Database::Get()
                .Exec("DELETE FROM cash_ledger WHERE user_id IN "
                      "(SELECT id FROM users WHERE username LIKE 'economy_ctrl_%');")
                .has_value());
    REQUIRE(Database::Get()
                .Exec("DELETE FROM item_ownership WHERE user_id IN "
                      "(SELECT id FROM users WHERE username LIKE 'economy_ctrl_%');")
                .has_value());
    REQUIRE(Database::Get()
                .Exec("DELETE FROM users WHERE username LIKE 'economy_ctrl_%';")
                .has_value());
}

/** @brief A live server exposing the EconomyController routes. */
struct EconomyTestServer {
    std::shared_ptr<Catalogue> catalogue;
    std::shared_ptr<EconomyService> economy;
    std::thread server_thread;
    uWS::Loop* loop{nullptr};
    us_listen_socket_t* listen_socket{nullptr};

    EconomyTestServer() {
        setenv("JWT_SECRET", "test-jwt-secret", 1);
        setenv("PASSWORD_PEPPER", "test-password-pepper", 1);
        (void)Database::Get().RunMigrations();

        catalogue = std::make_shared<Catalogue>(Catalogue::Parse(ValidDoc()));
        economy = std::make_shared<EconomyService>(Database::Get(), *catalogue);

        std::promise<void> bound;
        server_thread = std::thread([this, &bound] {
            loop = uWS::Loop::get();

            uWS::SocketContextOptions options;
            options.key_file_name = "key.pem";
            options.cert_file_name = "cert.pem";

            HttpRouter router;
            EconomyController economy_controller(router, *economy);

            AppHttp app(options);
            router.Attach(app);

            app.listen(kTestPort, [this, &bound](auto* token) {
                if (token) listen_socket = token;
                bound.set_value();
            });
            app.run();
        });

        bound.get_future().wait();
    }

    ~EconomyTestServer() {
        if (listen_socket && loop) {
            loop->defer([this]() { us_listen_socket_close(kAppSSL, listen_socket); });
        }
        if (server_thread.joinable()) server_thread.join();
    }

    EconomyTestServer(const EconomyTestServer&) = delete;
    EconomyTestServer& operator=(const EconomyTestServer&) = delete;
};

/** @brief A registered account with a valid session cookie pair. */
struct TestUser {
    std::string username;
    std::string cookies;
};

/** @brief Register `username` and mint its session cookie (no HTTP hop). */
TestUser RegisterUser(const std::string& username) {
    AuthService auth;
    REQUIRE(auth.Register(username, username + "@example.com", "password123").has_value());
    auto token = AuthService::IssueToken(username);
    REQUIRE(token.has_value());
    return {username, "auth_token=" + *token + "; ws_token=" + *token};
}

/** @brief A fresh guest session, carrying only a ws_token like the server does. */
TestUser MakeGuest() {
    AuthService auth;
    auto guest = auth.CreateGuestSession();
    REQUIRE(guest.has_value());
    return {guest->username, "ws_token=" + guest->token};
}

/** @brief The buy request body for `item` under `request_id`. */
std::string BuyBody(const std::string& item, const std::string& request_id) {
    return json{{"item", item}, {"request_id", request_id}}.dump();
}

}  // namespace

TEST_CASE("economy controller: GET without a cookie is 401") {
    EconomyTestServer server;

    const auto res = Curl("GET", kMePath, "", "");
    CHECK(res.status == 401);
}

TEST_CASE("economy controller: GET as guest is 403 guest") {
    EconomyTestServer server;
    const TestUser guest = MakeGuest();

    const auto res = Curl("GET", kMePath, guest.cookies, "");
    REQUIRE(res.status == 403);
    CHECK(res.body["error"] == "guest");
}

TEST_CASE("economy controller: GET returns balance, owned and at most 50 ledger rows") {
    EconomyTestServer server;
    const TestUser user = RegisterUser(Name("me"));
    for (int i = 0; i < 60; ++i) {
        REQUIRE(server.economy->Credit(user.username, 10, "grant",
                                       "seed-" + std::to_string(i), 1000 + i));
    }
    REQUIRE(server.economy->Buy(user.username, "uni:hat_cap", "req-me", 2000).status ==
            uni::economy::BuyStatus::kOk);

    const auto res = Curl("GET", kMePath, user.cookies, "");
    REQUIRE(res.status == 200);
    CHECK(res.body["balance"] == 600 - 100);
    REQUIRE(res.body.contains("owned"));
    REQUIRE(res.body.contains("ledger"));

    bool owns_cap = false;
    for (const json& id : res.body["owned"]) {
        if (id == "uni:hat_cap") owns_cap = true;
    }
    CHECK(owns_cap);

    CHECK(res.body["ledger"].size() == 50);
    const json& newest = res.body["ledger"].front();
    CHECK(newest["source"] == "purchase");
    CHECK(newest["ref"] == "req-me");
    CHECK(newest["amount"] == -100);
    CHECK(newest.contains("created_at"));
}

TEST_CASE("economy controller: buy succeeds and returns the new balance") {
    EconomyTestServer server;
    const TestUser user = RegisterUser(Name("buy"));
    REQUIRE(server.economy->Credit(user.username, 250, "grant", "seed", 1000));

    const auto res = Curl("POST", kBuyPath, user.cookies,
                          BuyBody("uni:hat_cap", "req-buy"));
    REQUIRE(res.status == 200);
    CHECK(res.body["item"] == "uni:hat_cap");
    CHECK(res.body["balance"] == 150);
}

TEST_CASE("economy controller: buy with a bad body is 400 bad_json") {
    EconomyTestServer server;
    const TestUser user = RegisterUser(Name("badjson"));

    const auto res = Curl("POST", kBuyPath, user.cookies, "{not valid json");
    REQUIRE(res.status == 400);
    CHECK(res.body["error"] == "bad_json");
}

TEST_CASE("economy controller: buy one cash short is 402 insufficient_cash") {
    EconomyTestServer server;
    const TestUser user = RegisterUser(Name("short"));
    REQUIRE(server.economy->Credit(user.username, 100, "grant", "seed", 1000));

    const auto res = Curl("POST", kBuyPath, user.cookies,
                          BuyBody("uni:mask_visor", "req-short"));
    REQUIRE(res.status == 402);
    CHECK(res.body["error"] == "insufficient_cash");
}

TEST_CASE("economy controller: buy of an unknown item is 404 unknown_item") {
    EconomyTestServer server;
    const TestUser user = RegisterUser(Name("unknown"));
    REQUIRE(server.economy->Credit(user.username, 500, "grant", "seed", 1000));

    const auto res = Curl("POST", kBuyPath, user.cookies,
                          BuyBody("uni:does_not_exist", "req-unknown"));
    REQUIRE(res.status == 404);
    CHECK(res.body["error"] == "unknown_item");
}

TEST_CASE("economy controller: buying an owned item again is 409 already_owned") {
    EconomyTestServer server;
    const TestUser user = RegisterUser(Name("owned"));
    REQUIRE(server.economy->Credit(user.username, 500, "grant", "seed", 1000));

    const auto first = Curl("POST", kBuyPath, user.cookies,
                            BuyBody("uni:hat_cap", "req-owned-1"));
    REQUIRE(first.status == 200);

    const auto second = Curl("POST", kBuyPath, user.cookies,
                             BuyBody("uni:hat_cap", "req-owned-2"));
    REQUIRE(second.status == 409);
    CHECK(second.body["error"] == "already_owned");
}

TEST_CASE("economy controller: buying a hidden item is 410 not_available") {
    EconomyTestServer server;
    const TestUser user = RegisterUser(Name("unavailable"));
    REQUIRE(server.economy->Credit(user.username, 500, "grant", "seed", 1000));

    const auto res = Curl("POST", kBuyPath, user.cookies,
                          BuyBody("uni:hat_hidden", "req-hidden"));
    REQUIRE(res.status == 410);
    CHECK(res.body["error"] == "not_available");
}

TEST_CASE("economy controller: buy as guest is 403 guest") {
    EconomyTestServer server;
    const TestUser guest = MakeGuest();

    const auto res = Curl("POST", kBuyPath, guest.cookies,
                          BuyBody("uni:hat_cap", "req-guest"));
    REQUIRE(res.status == 403);
    CHECK(res.body["error"] == "guest");
}

TEST_CASE("economy controller: buy a paid swatch and list it in owned") {
    EconomyTestServer server;
    const TestUser user = RegisterUser(Name("swatch"));
    REQUIRE(server.economy->Credit(user.username, 250, "grant", "seed", 1000));

    const auto bought = Curl("POST", kBuyPath, user.cookies,
                             BuyBody("uni:neon:hot", "req-swatch"));
    REQUIRE(bought.status == 200);
    CHECK(bought.body["item"] == "uni:neon:hot");
    CHECK(bought.body["balance"] == 0);

    const auto me = Curl("GET", kMePath, user.cookies, "");
    REQUIRE(me.status == 200);
    bool owns_swatch = false;
    for (const json& id : me.body["owned"]) {
        if (id == "uni:neon:hot") owns_swatch = true;
    }
    CHECK(owns_swatch);
}

TEST_CASE("economy controller: buy a hidden swatch is 410 not_available") {
    EconomyTestServer server;
    const TestUser user = RegisterUser(Name("swatch_hidden"));
    REQUIRE(server.economy->Credit(user.username, 600, "grant", "seed", 1000));

    const auto res = Curl("POST", kBuyPath, user.cookies,
                          BuyBody("uni:neon:secret", "req-swatch-hidden"));
    REQUIRE(res.status == 410);
    CHECK(res.body["error"] == "not_available");
}
