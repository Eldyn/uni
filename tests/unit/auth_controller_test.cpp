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

TestHttpResponse SimulateRegister(const std::string& username, const std::string& email, const std::string& password) {
    json req_body = {
        {"username", username},
        {"email", email},
        {"password", password}
    };
    std::string cmd = "curl -s -i -k -X POST -H 'Content-Type: application/json' -d '" +
                      req_body.dump() + "' https://127.0.0.1:" +
                      std::to_string(kAuthTestPort) + "/auth/register";
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

struct TestServerStarter {
    TestServerStarter() {
        setenv("JWT_SECRET", "test-jwt-secret", 1);
        setenv("PASSWORD_PEPPER", "test-password-pepper", 1);
        (void)Database::Get().RunMigrations();

        std::thread([this] {
            uWS::SocketContextOptions options;
            options.key_file_name = "key.pem";
            options.cert_file_name = "cert.pem";

            router = std::make_unique<HttpRouter>();
            auth_ctrl = std::make_unique<AuthController>(*router);
            app = std::make_unique<AppHttp>(options);
            router->Attach(*app);

            app->listen(kAuthTestPort, [](auto* token) {
                if (token) {
                    // listening
                }
            });
            app->run();
        }).detach();

        // Give the background event loop a moment to bind and listen
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::unique_ptr<HttpRouter> router;
    std::unique_ptr<AuthController> auth_ctrl;
    std::unique_ptr<AppHttp> app;
};

}  // namespace

TEST_CASE("Register response includes session cookies") {
    static TestServerStarter server;

    auto response = SimulateRegister("newuser2", "new2@example.com", "password123");
    CHECK(response.status == 200);
    CHECK(response.headers.count("Set-Cookie") > 0);
    CHECK(response.body.contains("username"));
    CHECK(response.body["email_verified"] == false);
}
