#include <doctest/doctest.h>
#include <controllers/password_reset_controller.hpp>
#include <http_router.hpp>
#include <websocket_context.hpp>
#include <database.hpp>
#include <services/auth_service.hpp>
#include <App.h>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <memory>
#include <array>
#include <map>
#include <mutex>
#include <future>
#include <cstring>
#include <nlohmann/json.hpp>
#include <common/email_queue.hpp>
#include <common/email_sender.hpp>

using json = nlohmann::json;

namespace {

constexpr int kResetTestPort = 49154;

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

TestHttpResponse ParseResponse(const std::string& output) {
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
                res.headers.insert({line.substr(0, pos), line.substr(pos + 2)});
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

TestHttpResponse SimulatePost(int port, const std::string& path, const std::string& body) {
    std::string cmd = "curl -s -i -k -X POST -H 'Content-Type: application/json' -d '" +
                      body + "' https://127.0.0.1:" + std::to_string(port) + path;
    return ParseResponse(exec(cmd.c_str()));
}

// Captures the plaintext reset token out of the rendered email's text body.
std::mutex g_reset_mutex;
std::map<std::string, std::string> g_reset_tokens;
int g_reset_mail_count = 0;

class ResetCapturingSender : public IEmailSender {
public:
    VoidResult Send(const OutboundEmail& mail) override {
        const std::string& text = mail.text_body;
        const std::string marker = "/reset-password/";
        auto pos = text.find(marker);
        if (pos != std::string::npos) {
            size_t start = pos + marker.size();
            size_t end = text.find_first_of(" \n\r\t", start);
            std::string token = text.substr(start, end - start);
            std::lock_guard<std::mutex> lock(g_reset_mutex);
            g_reset_tokens[mail.to_address] = token;
            ++g_reset_mail_count;
        }
        return {};
    }
};

std::string WaitForResetToken(const std::string& email) {
    for (int i = 0; i < 100; ++i) {
        {
            std::lock_guard<std::mutex> lock(g_reset_mutex);
            auto it = g_reset_tokens.find(email);
            if (it != g_reset_tokens.end() && !it->second.empty()) return it->second;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return "";
}

int ResetMailCount() {
    std::lock_guard<std::mutex> lock(g_reset_mutex);
    return g_reset_mail_count;
}

std::string RegisterUser(const std::string& username) {
    AuthService auth;
    auto result = auth.Register(username, username + "@example.com", "password123");
    if (!result) throw std::runtime_error("RegisterUser failed: " + result.error().message);
    return username + "@example.com";
}

bool PasswordMatches(const std::string& username, const std::string& candidate) {
    auto row = Database::Get().QueryOne(
        "SELECT pass_hash, salt FROM users WHERE username = ?;", {username});
    REQUIRE(row.has_value());
    REQUIRE(row->has_value());
    std::string stored = row->value().Get<std::string>("salt") + ":" +
                         row->value().Get<std::string>("pass_hash");
    return AuthService::VerifyPassword(candidate, stored);
}

struct ScopedResetServer {
    std::thread server_thread;
    uWS::Loop* loop{nullptr};
    us_listen_socket_t* listen_socket{nullptr};
    int port{kResetTestPort};

    ScopedResetServer() {
        setenv("JWT_SECRET", "test-jwt-secret", 1);
        setenv("PASSWORD_PEPPER", "test-password-pepper", 1);
        setenv("EMAIL_MAX_SENDS_PER_DAY", "5", 1);
        (void)Database::Get().RunMigrations();

        std::promise<void> bound;
        server_thread = std::thread([this, &bound] {
            loop = uWS::Loop::get();

            uWS::SocketContextOptions options;
            options.key_file_name = "key.pem";
            options.cert_file_name = "cert.pem";

            HttpRouter router;
            EmailQueue email_queue(std::make_unique<ResetCapturingSender>());
            PasswordResetController reset_ctrl(router, email_queue);
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

    ~ScopedResetServer() {
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

TEST_CASE("reset request returns 202 for existing and unknown emails alike") {
    ScopedResetServer server;
    RegisterUser("resetctrl_existing");

    auto existing = SimulatePost(server.port, "/auth/reset/request",
                                 R"({"email":"resetctrl_existing@example.com"})");
    CHECK(existing.status == 202);
    CHECK(existing.body["status"] == "queued");

    auto unknown = SimulatePost(server.port, "/auth/reset/request",
                                R"({"email":"resetctrl_nobody@example.com"})");
    CHECK(unknown.status == 202);
    CHECK(unknown.body["status"] == "queued");
}

TEST_CASE("reset confirm sets both cookies and succeeds") {
    ScopedResetServer server;
    const std::string username = "resetctrl_confirm";
    const std::string email    = RegisterUser(username);

    auto req = SimulatePost(server.port, "/auth/reset/request",
                            "{\"email\":\"" + email + "\"}");
    REQUIRE(req.status == 202);
    const std::string token = WaitForResetToken(email);
    REQUIRE_FALSE(token.empty());

    auto res = SimulatePost(server.port, "/auth/reset/confirm",
                            "{\"token\":\"" + token + "\",\"password\":\"brandnewpass1\"}");
    CHECK(res.status == 200);
    CHECK(res.body["status"] == "ok");

    auto range = res.headers.equal_range("Set-Cookie");
    bool has_auth = false;
    bool has_ws = false;
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second.starts_with("auth_token=")) has_auth = true;
        if (it->second.starts_with("ws_token=")) has_ws = true;
    }
    CHECK(has_auth);
    CHECK(has_ws);

    CHECK(PasswordMatches(username, "brandnewpass1"));
    CHECK_FALSE(PasswordMatches(username, "password123"));
}

TEST_CASE("reset confirm rejects a bad token with generic 401") {
    ScopedResetServer server;
    auto res = SimulatePost(server.port, "/auth/reset/confirm",
                            R"({"token":"not-a-real-token","password":"brandnewpass1"})");
    CHECK(res.status == 401);
}

TEST_CASE("reset confirm rejects an expired token") {
    ScopedResetServer server;
    const std::string username = "resetctrl_expired";
    const std::string email    = RegisterUser(username);

    (void)SimulatePost(server.port, "/auth/reset/request", "{\"email\":\"" + email + "\"}");
    const std::string token = WaitForResetToken(email);
    REQUIRE_FALSE(token.empty());

    (void)Database::Get().Exec("UPDATE password_reset_tokens SET expires_at = 0;");

    auto res = SimulatePost(server.port, "/auth/reset/confirm",
                            "{\"token\":\"" + token + "\",\"password\":\"brandnewpass1\"}");
    CHECK(res.status == 401);
}

TEST_CASE("reset confirm token is single-use") {
    ScopedResetServer server;
    const std::string username = "resetctrl_reuse";
    const std::string email    = RegisterUser(username);

    (void)SimulatePost(server.port, "/auth/reset/request", "{\"email\":\"" + email + "\"}");
    const std::string token = WaitForResetToken(email);
    REQUIRE_FALSE(token.empty());

    auto first = SimulatePost(server.port, "/auth/reset/confirm",
                              "{\"token\":\"" + token + "\",\"password\":\"brandnewpass1\"}");
    REQUIRE(first.status == 200);

    auto second = SimulatePost(server.port, "/auth/reset/confirm",
                               "{\"token\":\"" + token + "\",\"password\":\"anotherpass1\"}");
    CHECK(second.status == 401);
}

TEST_CASE("reset confirm rejects a short password with 400") {
    ScopedResetServer server;
    const std::string username = "resetctrl_short";
    const std::string email    = RegisterUser(username);

    (void)SimulatePost(server.port, "/auth/reset/request", "{\"email\":\"" + email + "\"}");
    const std::string token = WaitForResetToken(email);
    REQUIRE_FALSE(token.empty());

    auto res = SimulatePost(server.port, "/auth/reset/confirm",
                            "{\"token\":\"" + token + "\",\"password\":\"short\"}");
    CHECK(res.status == 400);
}

TEST_CASE("reset request silently caps at EMAIL_MAX_SENDS_PER_DAY") {
    ScopedResetServer server;
    const std::string username = "resetctrl_capped";
    const std::string email    = RegisterUser(username);

    const int before = ResetMailCount();
    setenv("EMAIL_MAX_SENDS_PER_DAY", "1", 1);

    auto first = SimulatePost(server.port, "/auth/reset/request",
                              "{\"email\":\"" + email + "\"}");
    CHECK(first.status == 202);
    const std::string token = WaitForResetToken(email);
    REQUIRE_FALSE(token.empty());

    auto second = SimulatePost(server.port, "/auth/reset/request",
                               "{\"email\":\"" + email + "\"}");
    CHECK(second.status == 202);

    CHECK(ResetMailCount() == before + 1);

    setenv("EMAIL_MAX_SENDS_PER_DAY", "5", 1);
}
