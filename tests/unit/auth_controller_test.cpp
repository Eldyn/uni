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
#include <common/email_queue.hpp>
#include <common/email_sender.hpp>

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

static std::map<std::string, std::string> g_last_issued_codes;

class CapturingEmailSender : public IEmailSender {
public:
    VoidResult Send(const OutboundEmail& mail) override {
        // extract 6 digit code from text_body
        for (size_t i = 0; i + 5 < mail.text_body.size(); ++i) {
            bool all_digits = true;
            for (size_t j = 0; j < 6; ++j) {
                if (!std::isdigit(mail.text_body[i+j])) {
                    all_digits = false;
                    break;
                }
            }
            if (all_digits && (i == 0 || !std::isdigit(mail.text_body[i-1])) && (i + 6 == mail.text_body.size() || !std::isdigit(mail.text_body[i+6]))) {
                g_last_issued_codes[mail.to_address] = mail.text_body.substr(i, 6);
                break;
            }
        }
        return {};
    }
};

std::string GetLastIssuedCodeForTest(const std::string& username) {
    auto rows = Database::Get().Query("SELECT email FROM users WHERE username = ?;", {username});
    if (rows && !rows->empty()) {
        std::string email = rows->at(0).Get<std::string>("email");
        return g_last_issued_codes[email];
    }
    return "";
}

void ForceExpireCodeForTest(const std::string& username) {
    (void)Database::Get().Exec("UPDATE email_verification_codes SET expires_at = 0 WHERE user_id = (SELECT id FROM users WHERE username = ?);", {username});
}

struct ScopedTestServer {
    std::thread server_thread;
    uWS::Loop* loop{nullptr};
    us_listen_socket_t* listen_socket{nullptr};
    int port{kAuthTestPort};

    ScopedTestServer() {
        setenv("JWT_SECRET", "test-jwt-secret", 1);
        setenv("PASSWORD_PEPPER", "test-password-pepper", 1);
        setenv("EMAIL_MAX_SENDS_PER_DAY", "5", 1);
        setenv("RATE_VERIFY_BURST", "3", 1);
        setenv("RATE_VERIFY_RPS", "1.0", 1); // faster rate for testing
        (void)Database::Get().RunMigrations();

        std::promise<void> bound;
        server_thread = std::thread([this, &bound] {
            loop = uWS::Loop::get();

            uWS::SocketContextOptions options;
            options.key_file_name = "key.pem";
            options.cert_file_name = "cert.pem";

            HttpRouter router;
            EmailQueue email_queue(std::make_unique<CapturingEmailSender>());
            AuthController auth_ctrl(router, email_queue);
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

struct TestSession {
    std::string cookies;
};

TestHttpResponse SimulatePost(const std::string& path, const std::string& body, const std::string& cookies = "") {
    std::string cmd = "curl -s -i -k -X POST -H 'Content-Type: application/json' ";
    if (!cookies.empty()) {
        cmd += "-H 'Cookie: " + cookies + "' ";
    }
    cmd += "-d '" + body + "' https://127.0.0.1:" + std::to_string(kAuthTestPort) + path;
    
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
        try { res.body = json::parse(body_str); } catch (...) {}
    }
    return res;
}

TestSession SimulateLoginSession(const std::string& username, const std::string& email, const std::string& password) {
    auto res = SimulateRegister(kAuthTestPort, username, email, password);
    std::string cookies;
    auto range = res.headers.equal_range("Set-Cookie");
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second.starts_with("auth_token=")) {
            cookies += it->second.substr(0, it->second.find(';')) + "; ";
        } else if (it->second.starts_with("ws_token=")) {
            cookies += it->second.substr(0, it->second.find(';')) + "; ";
        }
    }
    return {cookies};
}

TestSession SimulateVerifiedSession(const std::string& username) {
    auto session = SimulateLoginSession(username, username + "@example.com", "password123");
    (void)Database::Get().Exec("UPDATE users SET email_verified = 1 WHERE username = ?", {username});
    return session;
}

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

TEST_CASE("request-code queues an email for an unverified session") {
    ScopedTestServer server;
    auto session = SimulateLoginSession("requser", "req@example.com", "password123");
    auto response = SimulatePost("/auth/verify/request-code", "{}", session.cookies);
    CHECK(response.status == 202);
}

TEST_CASE("request-code returns 409 when already verified") {
    ScopedTestServer server;
    auto session = SimulateVerifiedSession("verifieduser");
    auto response = SimulatePost("/auth/verify/request-code", "{}", session.cookies);
    CHECK(response.status == 409);
}

TEST_CASE("6th request in 24h returns 429 and does not queue an email") {
    ScopedTestServer server;
    auto session = SimulateLoginSession("cappeduser", "cap@example.com", "password123");
    for (int i = 0; i < 5; ++i) {
        auto res = SimulatePost("/auth/verify/request-code", "{}", session.cookies);
        CHECK(res.status == 202);
        std::this_thread::sleep_for(std::chrono::milliseconds(1100)); // clear token bucket
    }
    auto response = SimulatePost("/auth/verify/request-code", "{}", session.cookies);
    CHECK(response.status == 429);
    auto count_res = Database::Get().QueryOne(
        "SELECT COUNT(*) as c FROM email_send_log WHERE user_id = (SELECT id FROM users WHERE username='cappeduser');");
    CHECK(count_res.has_value());
    CHECK(count_res->has_value());
    CHECK(count_res->value().Get<int>("c") == 5);
}

TEST_CASE("request-code handles empty and malformed body safely") {
    ScopedTestServer server;
    auto session = SimulateLoginSession("emptybodyuser", "empty@example.com", "password123");
    
    // Empty body should succeed with default locale
    auto empty_res = SimulatePost("/auth/verify/request-code", "", session.cookies);
    CHECK(empty_res.status == 202);

    // Clear rate limit bucket
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    // Malformed JSON should return 400 Bad Request
    auto bad_res = SimulatePost("/auth/verify/request-code", "{not-valid-json", session.cookies);
    CHECK(bad_res.status == 400);
}


TEST_CASE("confirm-code with correct code verifies the account") {
    ScopedTestServer server;
    auto session = SimulateLoginSession("confuser", "conf@example.com", "password123");
    SimulatePost("/auth/verify/request-code", "{}", session.cookies);
    auto code = GetLastIssuedCodeForTest("confuser");
    auto response = SimulatePost("/auth/verify/confirm-code", R"({"code":")" + code + "\"}", session.cookies);
    CHECK(response.status == 200);
    std::string token = session.cookies.substr(session.cookies.find("auth_token=") + 11);
    token = token.substr(0, token.find(';'));
    auto me = SimulateMe(server.port, token);
    CHECK(me.body["email_verified"] == true);
}

TEST_CASE("confirm-code with expired code returns generic 401") {
    ScopedTestServer server;
    auto session = SimulateLoginSession("expuser", "exp@example.com", "password123");
    SimulatePost("/auth/verify/request-code", "{}", session.cookies);
    ForceExpireCodeForTest("expuser");
    auto response = SimulatePost("/auth/verify/confirm-code", R"({"code":"000000"})", session.cookies);
    CHECK(response.status == 401);
}

TEST_CASE("5 wrong then correct returns 429, correct code no longer works") {
    ScopedTestServer server;
    auto session = SimulateLoginSession("brutuser", "brut@example.com", "password123");
    SimulatePost("/auth/verify/request-code", "{}", session.cookies);
    auto code = GetLastIssuedCodeForTest("brutuser");
    for (int i = 0; i < 5; ++i)
        SimulatePost("/auth/verify/confirm-code", R"({"code":"999999"})", session.cookies);
    auto response = SimulatePost("/auth/verify/confirm-code", R"({"code":")" + code + "\"}", session.cookies);
    CHECK(response.status == 429);
}

TEST_CASE("magic-link confirm from a clean cookie jar succeeds") {
    ScopedTestServer server;
    auto session = SimulateLoginSession("linkuser", "link@example.com", "password123");
    SimulatePost("/auth/verify/request-code", "{}", session.cookies);
    auto code = GetLastIssuedCodeForTest("linkuser");
    auto response = SimulatePost("/auth/verify/confirm-code",
        R"({"email":"link@example.com","code":")" + code + "\"}", /*no cookies*/ "");
    CHECK(response.status == 200);
}

TEST_CASE("confirm-code repeated unknown email attempts lock out by IP") {
    ScopedTestServer server;
    for (int i = 0; i < 5; ++i) {
        auto res = SimulatePost("/auth/verify/confirm-code",
            R"({"email":"unknown_does_not_exist@example.com","code":"123456"})", "");
        CHECK(res.status == 401);
    }
    // 6th attempt should be locked out with 429
    auto locked_res = SimulatePost("/auth/verify/confirm-code",
        R"({"email":"unknown_does_not_exist@example.com","code":"123456"})", "");
    CHECK(locked_res.status == 429);
}

