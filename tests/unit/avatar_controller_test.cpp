#include <doctest/doctest.h>

#include <controllers/avatar_controller.hpp>
#include <avatar/catalogue.hpp>
#include <avatar/loadout.hpp>
#include <http_router.hpp>
#include <services/auth_service.hpp>
#include <websocket_context.hpp>
#include <App.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

/**
 * @file avatar_controller_test.cpp
 * @brief The loadout HTTP API (spec section 5): read the saved or default
 *        look, save a whole loadout atomically, and refuse guests.
 *
 * Follows `auth_controller_test.cpp`: a real TLS uWS server on a local port,
 * driven with curl, because the handlers need a live `AppRequest` /
 * `AppResponse` pair.
 */

using nlohmann::json;
using uni::avatar::Catalogue;
using uni::avatar::StarterOwnership;
using uni::avatar::ValidateLoadout;

namespace {

constexpr int kTestPort = 49161;
constexpr const char* kPath = "/avatar/loadout/me";

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
            }
        ],
        "featured": ["uni:hat_cap"]
    })");
}

/** @brief A loadout with `shape` and `skin` and an empty `slots` object. */
json Loadout(const std::string& shape, const std::string& skin) {
    return json{{"shape", shape}, {"skin", skin}, {"slots", json::object()}};
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

/** @brief A live server exposing the AvatarController routes. */
struct AvatarTestServer {
    /** @brief Thread-safe view of the `on_saved` hook. */
    struct HookState {
        std::mutex mu;
        int calls = 0;
        std::string username;
        json loadout;
    };

    std::shared_ptr<Catalogue> catalogue;
    std::shared_ptr<HookState> hook = std::make_shared<HookState>();
    std::thread server_thread;
    uWS::Loop* loop{nullptr};
    us_listen_socket_t* listen_socket{nullptr};

    AvatarTestServer() {
        setenv("JWT_SECRET", "test-jwt-secret", 1);
        setenv("PASSWORD_PEPPER", "test-password-pepper", 1);
        (void)Database::Get().RunMigrations();

        catalogue = std::make_shared<Catalogue>(Catalogue::Parse(ValidDoc()));

        std::promise<void> bound;
        server_thread = std::thread([this, &bound] {
            loop = uWS::Loop::get();

            uWS::SocketContextOptions options;
            options.key_file_name = "key.pem";
            options.cert_file_name = "cert.pem";

            HttpRouter router;
            AvatarController avatar(router, *catalogue);
            const std::shared_ptr<HookState> hook = this->hook;
            avatar.on_saved = [hook](const std::string& username, const json& loadout) {
                std::lock_guard<std::mutex> lock(hook->mu);
                hook->calls++;
                hook->username = username;
                hook->loadout = loadout;
            };

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

    ~AvatarTestServer() {
        if (listen_socket && loop) {
            loop->defer([this]() { us_listen_socket_close(kAppSSL, listen_socket); });
        }
        if (server_thread.joinable()) server_thread.join();
    }

    AvatarTestServer(const AvatarTestServer&) = delete;
    AvatarTestServer& operator=(const AvatarTestServer&) = delete;
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

}  // namespace

TEST_CASE("avatar controller: GET without a cookie is 401") {
    AvatarTestServer server;

    const auto res = Curl("GET", kPath, "", "");
    CHECK(res.status == 401);
}

TEST_CASE("avatar controller: GET returns the default for a user without a saved loadout") {
    AvatarTestServer server;
    const TestUser user = RegisterUser("avatar_ctrl_default");

    const auto res = Curl("GET", kPath, user.cookies, "");
    REQUIRE(res.status == 200);
    REQUIRE(res.body.contains("loadout"));
    const json& loadout = res.body["loadout"];
    CHECK(loadout["shape"] == "uni:round");
    CHECK(loadout["skin"] == "basic:red");
    CHECK(loadout["slots"]["eyes"]["item"] == "uni:eyes_dot");
    CHECK(loadout["slots"]["mouth"]["item"] == "uni:mouth_smile");
}

TEST_CASE("avatar controller: PUT saves and GET returns it") {
    AvatarTestServer server;
    const TestUser user = RegisterUser("avatar_ctrl_roundtrip");

    json loadout = Loadout("uni:round", "basic:blue");
    loadout["slots"]["eyes"] = json{{"item", "uni:eyes_dot"}, {"colours", json::array()}};

    const auto put = Curl("PUT", kPath, user.cookies, loadout.dump());
    REQUIRE(put.status == 200);
    CHECK(put.body["loadout"] == loadout);

    const auto get = Curl("GET", kPath, user.cookies, "");
    REQUIRE(get.status == 200);
    CHECK(get.body["loadout"] == loadout);
}

TEST_CASE("avatar controller: PUT with an invalid loadout is 400 and saves nothing") {
    AvatarTestServer server;
    const TestUser user = RegisterUser("avatar_ctrl_invalid");

    const auto put = Curl("PUT", kPath, user.cookies,
                          Loadout("uni:nope", "basic:red").dump());
    REQUIRE(put.status == 400);
    CHECK(put.body["error"] == "unknown_shape");
    CHECK_FALSE(put.body["detail"].get<std::string>().empty());

    const auto get = Curl("GET", kPath, user.cookies, "");
    REQUIRE(get.status == 200);
    CHECK(get.body["loadout"]["shape"] == "uni:round");
    CHECK(get.body["loadout"]["skin"] == "basic:red");
}

TEST_CASE("avatar controller: PUT with bad JSON is 400 bad_json") {
    AvatarTestServer server;
    const TestUser user = RegisterUser("avatar_ctrl_badjson");

    const auto put = Curl("PUT", kPath, user.cookies, "{not valid json");
    REQUIRE(put.status == 400);
    CHECK(put.body["error"] == "bad_json");
}

TEST_CASE("avatar controller: PUT as guest is 403 guest") {
    AvatarTestServer server;
    const TestUser guest = MakeGuest();

    const auto put = Curl("PUT", kPath, guest.cookies,
                          Loadout("uni:round", "basic:red").dump());
    REQUIRE(put.status == 403);
    CHECK(put.body["error"] == "guest");
}

TEST_CASE("avatar controller: GET as guest returns a valid guest loadout") {
    AvatarTestServer server;
    const TestUser guest = MakeGuest();

    const auto res = Curl("GET", kPath, guest.cookies, "");
    REQUIRE(res.status == 200);
    REQUIRE(res.body.contains("loadout"));

    const json& loadout = res.body["loadout"];
    const StarterOwnership ownership(*server.catalogue);
    CHECK_FALSE(ValidateLoadout(loadout, *server.catalogue, ownership, guest.username)
                    .has_value());
    CHECK(server.catalogue->IsStarterSwatch(loadout["skin"]));
    CHECK(loadout["shape"] == "uni:round");

    const auto again = Curl("GET", kPath, guest.cookies, "");
    REQUIRE(again.status == 200);
    CHECK(again.body["loadout"] == loadout);
}

TEST_CASE("avatar controller: PUT calls on_saved once with the saved loadout") {
    AvatarTestServer server;
    const TestUser user = RegisterUser("avatar_ctrl_hook");

    const json loadout = Loadout("uni:round", "basic:blue");
    const auto put = Curl("PUT", kPath, user.cookies, loadout.dump());
    REQUIRE(put.status == 200);

    std::lock_guard<std::mutex> lock(server.hook->mu);
    CHECK(server.hook->calls == 1);
    CHECK(server.hook->username == user.username);
    CHECK(server.hook->loadout == loadout);
}

TEST_CASE("avatar controller: PUT with a body over 8 KiB is 400") {
    AvatarTestServer server;
    const TestUser user = RegisterUser("avatar_ctrl_bigbody");

    json loadout = Loadout("uni:round", "basic:red");
    loadout["pad"] = std::string(9000, 'a');

    const auto put = Curl("PUT", kPath, user.cookies, loadout.dump());
    CHECK(put.status == 400);

    const auto get = Curl("GET", kPath, user.cookies, "");
    REQUIRE(get.status == 200);
    CHECK(get.body["loadout"]["shape"] == "uni:round");
}
