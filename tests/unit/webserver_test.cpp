#include <doctest/doctest.h>
#include <common/http_utils.hpp>
#include <filesystem>
#include <fstream>
#include <webserver.hpp>
#include <common/email_queue.hpp>
#include <common/email_sender.hpp>
#include <database.hpp>
#include <thread>
#include <chrono>
#include <future>
#include <sstream>
#include <array>
#include <map>
#include <nlohmann/json.hpp>
#include <atomic>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Fixture: unique scratch directory under the system temp dir, cleaned up
// after each test case that touches the filesystem.
// ---------------------------------------------------------------------------
struct ScratchDir {
    fs::path root;

    ScratchDir() {
        root = fs::temp_directory_path() /
               ("webserver_test_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        fs::remove_all(root);
        fs::create_directories(root);
    }

    ~ScratchDir() { fs::remove_all(root); }

    fs::path WriteFile(const std::string& relative, const std::string& contents = "x") const {
        fs::path full = root / relative;
        fs::create_directories(full.parent_path());
        std::ofstream(full) << contents;
        return full;
    }
};

TEST_SUITE("http_utils::GetMimeType") {

TEST_CASE("maps known extensions to their exact MIME strings") {
    CHECK(http::GetMimeType("index.html") == "text/html");
    CHECK(http::GetMimeType("bundle.js") == "text/javascript");
    CHECK(http::GetMimeType("style.css") == "text/css");
    CHECK(http::GetMimeType("icon.svg") == "image/svg+xml");
    CHECK(http::GetMimeType("logo.png") == "image/png");
    CHECK(http::GetMimeType("font.woff2") == "font/woff2");
    CHECK(http::GetMimeType("font.woff") == "font/woff");
    CHECK(http::GetMimeType("manifest.xml") == "application/xml");
    CHECK(http::GetMimeType("draco_decoder.wasm") == "application/wasm");
    CHECK(http::GetMimeType("data.json") == "application/json");
    CHECK(http::GetMimeType("app.webmanifest") == "application/manifest+json");
}

TEST_CASE("falls back to application/octet-stream for unknown extensions") {
    CHECK(http::GetMimeType("archive.zip") == "application/octet-stream");
}

TEST_CASE("falls back to application/octet-stream when there is no extension") {
    CHECK(http::GetMimeType("README") == "application/octet-stream");
}

} // TEST_SUITE

TEST_SUITE("http_utils::ResolveSafePath") {

TEST_CASE("resolves a normal in-root relative path") {
    ScratchDir dir;
    dir.WriteFile("index.html");

    auto resolved = http::ResolveSafePath(dir.root, "index.html");
    REQUIRE(resolved.has_value());
    CHECK(*resolved == fs::weakly_canonical(dir.root / "index.html"));
}

TEST_CASE("resolves a nested in-root relative path") {
    ScratchDir dir;
    dir.WriteFile("assets/foo.js");

    auto resolved = http::ResolveSafePath(dir.root, "assets/foo.js");
    REQUIRE(resolved.has_value());
    CHECK(*resolved == fs::weakly_canonical(dir.root / "assets/foo.js"));
}

TEST_CASE("rejects a straightforward traversal attempt") {
    ScratchDir dir;
    CHECK(http::ResolveSafePath(dir.root, "../../etc/passwd") == std::nullopt);
    CHECK(http::ResolveSafePath(dir.root, "../outside.txt") == std::nullopt);
}

TEST_CASE("rejects traversal mixed with legitimate leading segments") {
    ScratchDir dir;
    CHECK(http::ResolveSafePath(dir.root, "assets/../../secret") == std::nullopt);
}

} // TEST_SUITE

TEST_SUITE("http_utils::CacheControlFor") {

TEST_CASE("html paths get no-cache") {
    CHECK(http::CacheControlFor("index.html") == "no-cache");
}

TEST_CASE("assets/* paths get a year-long immutable policy") {
    CHECK(http::CacheControlFor("assets/index-8FzcvdAa.js") ==
          "public, max-age=31536000, immutable");
}

TEST_CASE("fonts/* paths get a 30-day policy") {
    CHECK(http::CacheControlFor("fonts/JetBrainsMono.woff2") == "public, max-age=2592000");
}

TEST_CASE("everything else falls back to the 1-day default") {
    CHECK(http::CacheControlFor("favicon.ico") == "public, max-age=86400");
}

} // TEST_SUITE

TEST_SUITE("http_utils::AcceptsGzip") {

TEST_CASE("accepts the encodings browsers actually send") {
    CHECK(http::AcceptsGzip("gzip"));
    CHECK(http::AcceptsGzip("gzip, deflate, br"));
    CHECK(http::AcceptsGzip("br, gzip"));
    CHECK(http::AcceptsGzip("deflate, gzip;q=1.0, *;q=0.5"));
}

TEST_CASE("refuses when gzip is absent") {
    CHECK(!http::AcceptsGzip(""));
    CHECK(!http::AcceptsGzip("deflate"));
    CHECK(!http::AcceptsGzip("br, deflate"));
}

TEST_CASE("honours an explicit zero qvalue as a refusal") {
    CHECK(!http::AcceptsGzip("gzip;q=0"));
    CHECK(!http::AcceptsGzip("gzip;q=0.0"));
    CHECK(!http::AcceptsGzip("gzip;q=0.000, deflate"));
}

TEST_CASE("treats any non-zero qvalue as acceptance") {
    CHECK(http::AcceptsGzip("gzip;q=0.1"));
    CHECK(http::AcceptsGzip("gzip;q=0.5, deflate"));
}

} // TEST_SUITE

TEST_SUITE("http_utils::PrecompressedVariant") {

TEST_CASE("finds the sidecar sitting next to the asset") {
    ScratchDir dir;
    fs::path asset = dir.WriteFile("assets/bundle.js", "console.log(1)");
    dir.WriteFile("assets/bundle.js.gz", "compressed");

    auto sidecar = http::PrecompressedVariant(asset);
    REQUIRE(sidecar.has_value());
    CHECK(sidecar->filename() == "bundle.js.gz");
}

TEST_CASE("returns nothing when no sidecar was built") {
    ScratchDir dir;
    fs::path asset = dir.WriteFile("assets/bundle.js", "console.log(1)");
    CHECK(http::PrecompressedVariant(asset) == std::nullopt);
}

TEST_CASE("never wraps a .gz request in another layer of gzip") {
    ScratchDir dir;
    fs::path sidecar = dir.WriteFile("assets/bundle.js.gz", "compressed");
    dir.WriteFile("assets/bundle.js.gz.gz", "double");

    CHECK(http::PrecompressedVariant(sidecar) == std::nullopt);
}

} // TEST_SUITE

TEST_SUITE("http_utils::SelectPrecompressed") {

TEST_CASE("prefers brotli over gzip when both are accepted and built") {
    ScratchDir dir;
    fs::path asset = dir.WriteFile("assets/bundle.js", "console.log(1)");
    dir.WriteFile("assets/bundle.js.gz", "gzipped");
    dir.WriteFile("assets/bundle.js.br", "brotli");

    auto body = http::SelectPrecompressed(asset, "gzip, deflate, br");
    REQUIRE(body.has_value());
    CHECK(body->path.filename() == "bundle.js.br");
    CHECK(body->encoding == "br");
}

TEST_CASE("falls back to gzip when the client does not accept brotli") {
    ScratchDir dir;
    fs::path asset = dir.WriteFile("assets/bundle.js", "console.log(1)");
    dir.WriteFile("assets/bundle.js.gz", "gzipped");
    dir.WriteFile("assets/bundle.js.br", "brotli");

    auto body = http::SelectPrecompressed(asset, "gzip, deflate");
    REQUIRE(body.has_value());
    CHECK(body->encoding == "gzip");
}

TEST_CASE("falls back to gzip when no brotli sidecar was built") {
    ScratchDir dir;
    fs::path asset = dir.WriteFile("assets/bundle.js", "console.log(1)");
    dir.WriteFile("assets/bundle.js.gz", "gzipped");

    auto body = http::SelectPrecompressed(asset, "br, gzip");
    REQUIRE(body.has_value());
    CHECK(body->encoding == "gzip");
}

TEST_CASE("honours an explicit refusal of brotli") {
    ScratchDir dir;
    fs::path asset = dir.WriteFile("assets/bundle.js", "console.log(1)");
    dir.WriteFile("assets/bundle.js.br", "brotli");

    CHECK(http::SelectPrecompressed(asset, "br;q=0, gzip") == std::nullopt);
}

TEST_CASE("serves the identity body when nothing compressed is accepted") {
    ScratchDir dir;
    fs::path asset = dir.WriteFile("assets/bundle.js", "console.log(1)");
    dir.WriteFile("assets/bundle.js.br", "brotli");

    CHECK(http::SelectPrecompressed(asset, "") == std::nullopt);
}

TEST_CASE("never wraps a .br request in another layer of compression") {
    ScratchDir dir;
    fs::path sidecar = dir.WriteFile("assets/bundle.js.br", "brotli");
    dir.WriteFile("assets/bundle.js.br.br", "double");

    CHECK(http::SelectPrecompressed(sidecar, "br") == std::nullopt);
}

} // TEST_SUITE

TEST_SUITE("http_utils::MakeETag") {

TEST_CASE("produces a non-empty weak validator for an existing file") {
    ScratchDir dir;
    fs::path file = dir.WriteFile("index.html", "hello");

    std::string etag = http::MakeETag(file);
    CHECK(!etag.empty());
    CHECK(etag.starts_with("W/\""));
    CHECK(etag.ends_with("\""));
}

TEST_CASE("is deterministic for an unchanged file") {
    ScratchDir dir;
    fs::path file = dir.WriteFile("index.html", "hello");

    std::string first  = http::MakeETag(file);
    std::string second = http::MakeETag(file);
    CHECK(first == second);
}

TEST_CASE("returns an empty string when the file does not exist") {
    ScratchDir dir;
    CHECK(http::MakeETag(dir.root / "missing.html") == "");
}

} // TEST_SUITE

namespace {

constexpr int kBlastTestPort = 49155;

struct TestHttpResponse {
    int status{0};
    std::multimap<std::string, std::string> headers;
    nlohmann::json body;
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

class CapturingEmailSender : public IEmailSender {
public:
    std::vector<OutboundEmail> sent;
    std::mutex mtx;

    VoidResult Send(const OutboundEmail& mail) override {
        std::lock_guard<std::mutex> lock(mtx);
        sent.push_back(mail);
        return {};
    }
};

struct BlastTestServerFixture {
    static BlastTestServerFixture& Instance() {
        static BlastTestServerFixture instance;
        return instance;
    }

    std::thread server_thread;
    std::atomic<WebServer*> server_ptr{nullptr};

    BlastTestServerFixture() {
        setenv("DEPLOY_STATUS_TOKEN", "test-deploy-token-123", 1);
        setenv("BLAST_MAX_RECIPIENTS", "50", 1);
        setenv("JWT_SECRET", "test-jwt-secret", 1);
        setenv("PASSWORD_PEPPER", "test-password-pepper", 1);

        std::promise<void> bound;
        server_thread = std::thread([this, &bound] {
            auto email_queue = std::make_unique<EmailQueue>(std::make_unique<CapturingEmailSender>());
            auto server = std::make_unique<WebServer>(
                kBlastTestPort, "key.pem", "cert.pem", ":memory:", "public", email_queue.get());

            server_ptr = server.get();

            server->Run([&bound](bool ok) {
                bound.set_value();
            });

            server_ptr.store(nullptr);
            server.reset();
            email_queue.reset();
        });
        bound.get_future().wait();
    }

    ~BlastTestServerFixture() {
        WebServer* s = server_ptr.load();
        if (s) {
            s->Stop();
        }
        if (server_thread.joinable()) {
            server_thread.join();
        }
    }
};

TestHttpResponse SimulatePost(const std::string& path, const std::string& body,
                             const std::map<std::string, std::string>& extra_headers = {}) {
    BlastTestServerFixture::Instance();
    std::string proto = kAppSSL ? "https" : "http";
    std::string cmd = "curl -s -i -k -X POST -H 'Content-Type: application/json' ";
    for (const auto& [k, v] : extra_headers) {
        cmd += "-H '" + k + ": " + v + "' ";
    }
    cmd += "-d '" + body + "' " + proto + "://127.0.0.1:" +
           std::to_string(kBlastTestPort) + path;

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
            res.body = nlohmann::json::parse(body_str);
        } catch (...) {}
    }
    return res;
}

TestHttpResponse SimulatePostWithDeployToken(const std::string& path, const std::string& body) {
    return SimulatePost(path, body, {{"X-Deploy-Token", "test-deploy-token-123"}});
}

int InsertTestUser(const std::string& username, int email_verified, std::int64_t created_at) {
    auto& db = Database::Get();
    (void)db.RunMigrations();
    auto res = db.Exec("INSERT INTO users (username, pass_hash, salt, email, email_verified, created_at) VALUES (?, 'hash', 'salt', ?, ?, ?);",
        {username, username + "@example.com", email_verified, static_cast<int>(created_at)});
    if (!res) throw std::runtime_error("InsertTestUser failed: " + res.error().message);
    
    auto row = db.QueryOne("SELECT id FROM users WHERE username = ?;", {username});
    if (!row || !row.value()) throw std::runtime_error("InsertTestUser couldn't find user");
    return row.value()->Get<int>("id");
}

void ResetTestUsers() {
    auto& db = Database::Get();
    (void)db.RunMigrations();
    (void)db.Exec("DELETE FROM email_send_log;");
    (void)db.Exec("DELETE FROM email_verification_codes;");
    (void)db.Exec("DELETE FROM users WHERE email_verified = 0 OR username LIKE 'blast%';");
}

} // namespace

TEST_SUITE("WebServer::VerifyBlast") {

TEST_CASE("verify-blast without token returns 404") {
    ResetTestUsers();
    auto response = SimulatePost("/internal/verify-blast", "{}", {});
    CHECK(response.status == 404);

    auto wrong_token = SimulatePost("/internal/verify-blast", "{}", {{"X-Deploy-Token", "wrong-token"}});
    CHECK(wrong_token.status == 404);
}

TEST_CASE("verify-blast with valid token queues codes for all unverified users") {
    ResetTestUsers();
    InsertTestUser("blast1", 0, std::time(nullptr));
    InsertTestUser("blast2", 0, std::time(nullptr));
    InsertTestUser("blast_verified", 1, std::time(nullptr));
    auto response = SimulatePostWithDeployToken("/internal/verify-blast", "{}");
    CHECK(response.status == 200);
    CHECK(response.body["queued"] == 2);
    CHECK(response.body["skipped"] == 0);

    auto codes = Database::Get().Query("SELECT user_id FROM email_verification_codes;");
    CHECK(codes.has_value());
    CHECK(codes->size() == 2);

    auto sends = Database::Get().Query("SELECT user_id FROM email_send_log;");
    CHECK(sends.has_value());
    CHECK(sends->size() == 2);

    auto ver_user = Database::Get().QueryOne("SELECT id FROM users WHERE username = 'blast_verified';");
    CHECK(ver_user.has_value());
    CHECK(ver_user->has_value());
    int ver_id = ver_user->value().Get<int>("id");
    auto ver_code = Database::Get().Query("SELECT user_id FROM email_verification_codes WHERE user_id = ?;", {ver_id});
    CHECK(ver_code.has_value());
    CHECK(ver_code->empty());

    // Re-running re-issues codes and re-sends (intentional retry mechanism)
    auto retry_response = SimulatePostWithDeployToken("/internal/verify-blast", "{}");
    CHECK(retry_response.status == 200);
    CHECK(retry_response.body["queued"] == 2);
    CHECK(retry_response.body["skipped"] == 0);
}

TEST_CASE("verify-blast refuses when recipient count exceeds cap") {
    ResetTestUsers();
    for (int i = 0; i < 60; ++i)
        InsertTestUser("blastmany" + std::to_string(i), 0, std::time(nullptr));
    auto response = SimulatePostWithDeployToken("/internal/verify-blast", "{}");
    CHECK(response.status == 409);
}

} // TEST_SUITE

