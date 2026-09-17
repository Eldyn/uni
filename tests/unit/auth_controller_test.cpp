#include <doctest/doctest.h>
#include <controllers/auth_controller.hpp>
#include <http_router.hpp>
#include <websocket_context.hpp>
#include <database.hpp>
#include <App.h>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <memory>
#include <array>
#include <map>
#include <future>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

constexpr int kAuthTestPort = 49153;

struct TestHttpResponse {
    int status{0};
    std::multimap<std::string, std::string> headers;
    json body;
};

std::string exec(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, int(*)(FILE*)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

TestHttpResponse SimulateRegister(int port, const std::string& username, const std::string& email, const std::string& password) {
    json req_body = {
        {"username", username},
        {"email", email},
        {"password", password}
    };
    std::string cmd = "curl -s -i -k -X POST -H 'Content-Type: application/json' -d '" +
                      req_body.dump() + "' https://127.0.0.1:" +
                      std::to_string(port) + "/auth/register";
    std::string output = exec(cmd.c_str());

    TestHttpResponse res;
    std::istringstream iss(output);
    std::string line;
    bool headers_done = false;
    std::string body_str;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (res.status == 0 && line.starts_with("HTTP/")) {
            auto pos = line.find(' ');
            if (pos != std::string::npos) {
                res.status = std::stoi(line.substr(pos + 1, 3));
            }
            continue;
        }
        if (line.empty()) {
            headers_done = true;
            continue;
        }
        if (!headers_done) {
            auto pos = line.find(": ");
            if (pos != std::string::npos) {
                std::string key = line.substr(0, pos);
                std::string val = line.substr(pos + 2);
                res.headers.insert({key, val});
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

TestHttpResponse SimulateMe(int port, const std::string& auth_token) {
    std::string cmd = "curl -s -i -k -H 'Cookie: auth_token=" + auth_token + "' " +
                      "https://127.0.0.1:" + std::to_string(port) + "/auth/me";
    std::string output = exec(cmd.c_str());

    TestHttpResponse res;
    std::istringstream iss(output);
    std::string line;
    bool headers_done = false;
    std::string body_str;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (res.status == 0 && line.starts_with("HTTP/")) {
            auto pos = line.find(' ');
            if (pos != std::string::npos) {
                res.status = std::stoi(line.substr(pos + 1, 3));
            }
            continue;
        }
        if (line.empty()) {
            headers_done = true;
            continue;
        }
        if (!headers_done) {
            auto pos = line.find(": ");
            if (pos != std::string::npos) {
                std::string key = line.substr(0, pos);
                std::string val = line.substr(pos + 2);
                res.headers.insert({key, val});
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

struct ScopedTestServer {
    std::thread server_thread;
    uWS::Loop* loop{nullptr};
    us_listen_socket_t* listen_socket{nullptr};
    int port{kAuthTestPort};

    ScopedTestServer() {
        setenv("JWT_SECRET", "test-jwt-secret", 1);
        setenv("PASSWORD_PEPPER", "test-password-pepper", 1);
        (void)Database::Get().RunMigrations();

        std::promise<void> bound;
        server_thread = std::thread([this, &bound] {
            loop = uWS::Loop::get();

            uWS::SocketContextOptions options;
            options.key_file_name = "key.pem";
            options.cert_file_name = "cert.pem";

            HttpRouter router;
            AuthController auth_ctrl(router);
            AppHttp app(options);
            router.Attach(app);

            app.listen(port, [this, &bound](auto* token) {
                if (token) {
                    listen_socket = token;
                }
                bound.set_value();
            });
            app.run();
        });

        bound.get_future().wait();
    }

    ~ScopedTestServer() {
        if (listen_socket && loop) {
            loop->defer([this]() {
                us_listen_socket_close(kAppSSL, listen_socket);
            });
        }
        if (server_thread.joinable()) {
            server_thread.join();
        }
    }
};

}  // namespace

TEST_CASE("Register response includes session cookies") {
    ScopedTestServer server;

    auto response = SimulateRegister(server.port, "newuser2", "new2@example.com", "password123");
    CHECK(response.status == 200);
    CHECK(response.headers.count("Set-Cookie") > 0);
    CHECK(response.body.contains("username"));
    CHECK(response.body["email_verified"] == false);
}

TEST_CASE("/auth/me returns email_verified") {
    ScopedTestServer server;

    auto reg_res = SimulateRegister(server.port, "meuser", "me@example.com", "password123");
    REQUIRE(reg_res.status == 200);
    
    // Extract token
    std::string token;
    auto range = reg_res.headers.equal_range("Set-Cookie");
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second.starts_with("auth_token=")) {
            token = it->second.substr(11, it->second.find(';') - 11);
            break;
        }
    }
    REQUIRE_FALSE(token.empty());

    auto me_res = SimulateMe(server.port, token);
    CHECK(me_res.status == 200);
    CHECK(me_res.body["username"] == "meuser");
    CHECK(me_res.body["email"] == "me@example.com");
    CHECK(me_res.body["email_verified"] == false);
}
