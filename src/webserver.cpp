#include "action_router.hpp"
#include "common/http.hpp"
#include "common/ws.hpp"
#include "common/env.hpp"
#include <fstream>
#include "controllers/auth_controller.hpp"
#include "http_router.hpp"
#include "websocket_context.hpp"
#include <WebSocketProtocol.h>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <string>
#include <string_view>
#include <App.h>
#include <webserver.hpp>
#include <database.hpp>
#include <logger.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;
using std::string, std::string_view, std::ifstream, std::stringstream, std::ios, std::runtime_error, std::to_string, std::isalnum;


WebServer::WebServer(int port, string_view key_file, string_view cert_file, string_view db_file, string_view frontend_path)
    : port_(port), db_file_(db_file), frontend_path_(frontend_path),
      trust_proxy_(Env::Get("TRUST_PROXY", "0") != "0"),
      app_(AppHttp({.key_file_name = key_file.data(), .cert_file_name = cert_file.data()})),
      http_limiter_(std::stod(Env::Get("RATE_HTTP_BURST", "120")),
                    std::stod(Env::Get("RATE_HTTP_RPS",   "50"))),
      auth_limiter_(std::stod(Env::Get("RATE_AUTH_BURST", "10")),
                    std::stod(Env::Get("RATE_AUTH_RPS",   "0.5"))),
      ws_limiter_(std::stod(Env::Get("RATE_WS_BURST", "30")),
                  std::stod(Env::Get("RATE_WS_RPS",   "15"))),
      last_evict_(RateLimiter::Clock::now()),
      max_conn_per_ip_(std::stoi(Env::Get("MAX_CONN_PER_IP", "10"))) {
    if (!InitDB()) {
        throw runtime_error("Failed to initialise database");
    }
    RegisterRoutes();
    if constexpr (kAppSSL) {
        Logger::Info("Key file: " + string(key_file) + " exists=" + (fs::exists(key_file) ? "yes" : "NO"));
        Logger::Info("Cert file: " + string(cert_file) + " exists=" + (fs::exists(cert_file) ? "yes" : "NO"));
    } else {
        Logger::Info("TLS disabled (plain HTTP) — UNI_ENABLE_SSL=0");
    }
    Logger::Info("WebServer constructed");
}

WebServer::~WebServer() {
    if (Database::Get().IsOpen()) {
        Database::Get().Close();
        Logger::Info("Database closed");
    }
}

void WebServer::Run() {
    http_router_.Attach(app_);

    app_.listen(port_, [this](auto *socket) {
        if (socket) {
            Logger::Log("Server listening on ", (kAppSSL ? "https" : "http"), "://localhost:", port_);
        } else {
            Logger::Error("Failed to bind to port " + to_string(port_));
        }
    });
    app_.run();
}

bool WebServer::InitDB() {
    VoidResult open_result = Database::Get().Open(db_file_); 
    
    if (!open_result) {
        Logger::Error("Database opening failed: " + open_result.error().message);
        return false;
    }

    const char *schema = R"(
        CREATE TABLE IF NOT EXISTS users (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            username   TEXT    NOT NULL UNIQUE,
            pass_hash  TEXT    NOT NULL,
            salt       TEXT    NOT NULL,
            email      TEXT    NOT NULL UNIQUE
        );

        CREATE TABLE IF NOT EXISTS matches (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            winner_username TEXT NOT NULL,
            ended_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS match_participants (
            match_id INTEGER NOT NULL,
            username TEXT NOT NULL,
            FOREIGN KEY(match_id) REFERENCES matches(id) ON DELETE CASCADE,
            PRIMARY KEY(match_id, username)
        );

        CREATE TABLE IF NOT EXISTS player_stats (
            username TEXT PRIMARY KEY,
            total_wins INTEGER DEFAULT 0,
            total_losses INTEGER DEFAULT 0,
            
            cards_played_red INTEGER DEFAULT 0,
            cards_played_blue INTEGER DEFAULT 0,
            cards_played_green INTEGER DEFAULT 0,
            cards_played_yellow INTEGER DEFAULT 0,
            cards_played_wild INTEGER DEFAULT 0, 
            
            cards_played_0 INTEGER DEFAULT 0,
            cards_played_1 INTEGER DEFAULT 0,
            cards_played_2 INTEGER DEFAULT 0,
            cards_played_3 INTEGER DEFAULT 0,
            cards_played_4 INTEGER DEFAULT 0,
            cards_played_5 INTEGER DEFAULT 0,
            cards_played_6 INTEGER DEFAULT 0,
            cards_played_7 INTEGER DEFAULT 0,
            cards_played_8 INTEGER DEFAULT 0,
            cards_played_9 INTEGER DEFAULT 0,
            cards_played_skip INTEGER DEFAULT 0,
            cards_played_reverse INTEGER DEFAULT 0,
            cards_played_draw2 INTEGER DEFAULT 0,
            cards_played_draw4 INTEGER DEFAULT 0,
            cards_played_colorswitch INTEGER DEFAULT 0
        );

        CREATE TABLE IF NOT EXISTS saved_matches (
            id TEXT PRIMARY KEY,
            state_json TEXT NOT NULL,
            saved_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            expires_at DATETIME DEFAULT (datetime('now', '+1 day'))
        );

        CREATE TABLE IF NOT EXISTS saved_match_participants (
            match_id TEXT NOT NULL,
            username TEXT NOT NULL,
            FOREIGN KEY(match_id) REFERENCES saved_matches(id) ON DELETE CASCADE,
            PRIMARY KEY(match_id, username)
        );
    )";

    VoidResult schema_result = Database::Get().ApplySchema(schema);

    if (!schema_result) {
        Logger::Error("Schema failed: " + schema_result.error().message);
        return false;
    }

    return true;
}

void WebServer::MaybeEvict() {
    const auto now = RateLimiter::Clock::now();
    if (now - last_evict_ < std::chrono::seconds(60)) return;
    last_evict_ = now;
    http_limiter_.Evict();
    auth_limiter_.Evict();
    ws_limiter_.Evict();
}

void WebServer::RegisterRoutes() {
    // INFO: Per-IP HTTP rate limiting (runs before every router-handled
    //       route). Auth endpoints get a much tighter bucket: they are the
    //       brute-force and PBKDF2 CPU-amplification surface. The static
    //       file catch-all is registered straight on the uWS app (below)
    //       and is intentionally left to the edge.
    http_router_.OnAny([this](AppResponse* res, AppRequest* req) -> bool {
        MaybeEvict();
        const std::string ip = http::GetClientIp(res, req, trust_proxy_);
        const bool is_auth = req->getUrl().starts_with("/auth/");
        RateLimiter& limiter = is_auth ? auth_limiter_ : http_limiter_;

        if (!limiter.Allow(ip)) {
            Logger::Warn("[HTTP] 429 rate limited: " + ip);
            res->writeStatus("429 Too Many Requests")
               ->writeHeader("Retry-After", "1")
               ->end();
            return false;  // chain interrupted; response already sent
        }
        return true;
    });

    http_router_.Post("/room", [this](auto *response, auto *request) {
        HandlePost(response, request);
    });

    // INFO: Internal operational endpoint: reports the number of in-progress
    //       matches so the deploy tooling can hold a redeploy until games
    //       drain (state is RAM-only). Gated by a shared secret
    //       (X-Deploy-Token == DEPLOY_STATUS_TOKEN); a missing or wrong token
    //       returns 404 so the endpoint isn't even discoverable, and Traefik
    //       additionally blocks the /internal prefix on the public router.
    app_.get("/internal/active-games", [this](AppResponse *res, AppRequest *req) {
        const std::string token = Env::Get("DEPLOY_STATUS_TOKEN", "");
        if (token.empty() || req->getHeader("x-deploy-token") != token) {
            res->writeStatus("404 Not Found")->end("File not found");
            return;
        }
        const std::size_t count = active_match_provider_ ? active_match_provider_() : 0;
        res->writeHeader("Content-Type", "application/json")
           ->end(json({{"active_matches", count}}).dump());
    });

    app_.get("/*", [this](AppResponse *res, AppRequest *req) {
        HandleGet(res, req);
    });

    // INFO: Per-connection WebSocket action rate limiting (runs before
    //       dispatch). Keyed by client IP, falling back to username, so an
    //       authenticated client cannot flood the action router. Returning
    //       false aborts the dispatch chain.
    ws_router_.OnAny([this](WsContext ctx, const json& msg) {
        MaybeEvict();
        const std::string& key = !ctx.socket_data->ip.empty()
                                     ? ctx.socket_data->ip
                                     : ctx.socket_data->username;
        if (!ws_limiter_.Allow(key)) {
            Logger::Warn("[WS] rate limited: " + ctx.socket_data->username);
            // INFO: Standard error envelope (contract: {action:"error",
            //       reason}), echoing the request_id so the client can tie
            //       it back to the throttled action.
            const std::string request_id = ws::GetOr<std::string>(msg, "request_id", "");
            ws::SendError(ctx.socket, uWS::OpCode::TEXT, contract::ErrorCode::kRateLimited, request_id);
            return false;
        }
        return true;
    });

    // INFO: WebSocket transport limits (env-overridable). Capping the frame
    //       size, idle time and server-side backpressure stops oversized
    //       frames, slow-loris sockets and unbounded outbound buffering from
    //       exhausting memory.
    const unsigned int   ws_max_payload      = static_cast<unsigned int>(std::stoul(Env::Get("WS_MAX_PAYLOAD", "16384")));        // 16 KB
    const unsigned int   ws_max_backpressure = static_cast<unsigned int>(std::stoul(Env::Get("WS_MAX_BACKPRESSURE", "1048576"))); // 1 MB
    const unsigned short ws_idle_timeout     = static_cast<unsigned short>(std::stoi(Env::Get("WS_IDLE_TIMEOUT", "120")));         // seconds

    app_.ws<PerSocketData>("/*", {
        .maxPayloadLength = ws_max_payload,
        .idleTimeout = ws_idle_timeout,
        .maxBackpressure = ws_max_backpressure,
        .closeOnBackpressureLimit = true,
        .sendPingsAutomatically = true,
        .upgrade = [this](AppResponse* res, AppRequest*  req, us_socket_context_t* ctx) {
            std::string_view cookies = req->getHeader("cookie");
            auto token = http::GetCookieValue(cookies, "auth_token");

            auto payload = AuthController::VerifyToken(*token);

            if (!payload) {
                Logger::Warn("[WS] Rejected upgrade — invalid token");
                res->writeStatus("401 Unauthorized")->end();
                return;
            }

            // INFO: Capture the IP before upgrade() invalidates the request
            //       object.
            const std::string ip = http::GetClientIp(res, req, trust_proxy_);

            // INFO: Cap concurrent connections per IP so one host cannot
            //       exhaust the server's sockets. The counter is incremented
            //       here and decremented in OnSocketClosed.
            if (max_conn_per_ip_ > 0 && conn_per_ip_[ip] >= max_conn_per_ip_) {
                Logger::Warn("[WS] Rejected upgrade — connection cap reached for IP " + ip);
                res->writeStatus("429 Too Many Requests")->end();
                return;
            }

            PerSocketData socket_data;
            socket_data.username = payload->username;
            socket_data.ip = ip;
            ++conn_per_ip_[ip];

            res->upgrade(std::move(socket_data),
                req->getHeader("sec-websocket-key"),
                req->getHeader("sec-websocket-protocol"),
                req->getHeader("sec-websocket-extensions"),
                ctx);
        },
        .open = [this](AppWebSocket *ws) {
            OnSocketOpen(ws);
        },
        .message = [this](AppWebSocket *ws, string_view message, uWS::OpCode op) {
            OnSocketMessage(ws, message, op);
        },
        .close = [this](AppWebSocket *ws, int code, string_view message) {
            OnSocketClosed(ws);
        }
    });
}

void WebServer::HandlePost(AppResponse *response, AppRequest *request) {
    http::ReadBody(response, 4096, [response, request](const std::string body) {
            json data;
            try {
                data = json::parse(body);
            } catch (...) {
                response->writeStatus("400 Bad Request")
                        ->writeHeader("Content-Type", "application/json")
                        ->end(json({{"error", "Invalid JSON"}}).dump());
                return;
            }

            std::string_view cookies = request->getHeader("cookie");
            auto token = http::GetCookieValue(cookies, "auth_token");
            auto payload = AuthController::VerifyToken(*token);

            if (!payload) {
                response->writeStatus("401 Unauthorized")->end();
                return;
            }

            string topic = data.value("topic", "default");
            if (topic.empty() || topic.size() > 64) {    
                response->writeStatus("422 Unprocessable Entity")->end();
                return;
            }

            const bool valid = std::ranges::all_of(topic, [](unsigned char c) { 
                return std::isalnum(c) || c == '-' || c == '_'; 
            });

            if (!valid) {
                response->writeStatus("422 Unprocessable Entity")->end();
                return;
            }
        
            response->writeHeader("Content-Type", "application/json")
                    ->end(json({ {"status", "OK"}, {"topic", topic} }).dump());
    });
}

namespace {

// INFO: Cache policy for a static asset, keyed on its path within the
//       served root.
//
//   *.html              -> "no-cache": always revalidate. index.html is the
//                          entry point that names the content-hashed bundles,
//                          so a returning client must re-check it or a redeploy
//                          would stay invisible. "no-cache" still allows storing
//                          the copy; paired with the ETag below the recheck is a
//                          cheap 304 when nothing changed.
//   assets/*            -> immutable for a year. Vite content-hashes these
//                          (e.g. assets/index-8FzcvdAa.js); a new build yields a
//                          new name, so the old URL can be trusted forever.
//   fonts/*             -> 30 days. Stable filenames whose bytes effectively
//                          never change; long TTL avoids re-fetching the ~1 MB
//                          JetBrainsMono on every visit, ETag covers the rare edit.
//   everything else     -> 1 day (favicon, icons, root images).
std::string CacheControlFor(string_view relative_path) {
    if (relative_path.ends_with(".html"))   return "no-cache";
    if (relative_path.starts_with("assets/")) return "public, max-age=31536000, immutable";
    if (relative_path.starts_with("fonts/"))  return "public, max-age=2592000";
    return "public, max-age=86400";
}

// INFO: Weak ETag derived from the file's size and last-write time. It is
//       an opaque validator (RFC 7232) that only has to change when the
//       file does — size+mtime captures that without hashing the contents.
//       Empty return means the file could not be stat'd, in which case the
//       caller simply omits the header.
std::string MakeETag(const fs::path& file) {
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec) return "";
    const auto mtime = fs::last_write_time(file, ec);
    if (ec) return "";
    return "W/\"" + to_string(size) + "-" + to_string(mtime.time_since_epoch().count()) + "\"";
}

}  // namespace

void WebServer::HandleGet(AppResponse *res, AppRequest *req) {
    auto is_alive = std::make_shared<bool>(true);
    res->onAborted([is_alive]() {*is_alive = false;});

    if (!*is_alive) return;

    string url = string(req->getUrl());
    string relativePath = (url == "/") ? "index.html" : url.substr(1);

    // INFO: Capture the conditional-request header now: uWebSockets recycles
    //       the request object as soon as the response is written, so it
    //       cannot be read afterwards.
    string if_none_match = string(req->getHeader("if-none-match"));

    // INFO: Resolve both the served root and the requested file to canonical
    //       form so that "../" segments and symlinks are collapsed, then
    //       confirm the result stays inside the root. Without this, a raw
    //       request such as "GET /../../etc/passwd" would escape
    //       frontend_path_ and disclose host files.
    std::error_code ec;
    fs::path root     = fs::weakly_canonical(fs::path(frontend_path_), ec);
    fs::path filePath = fs::weakly_canonical(root / relativePath, ec);
    fs::path rel      = filePath.lexically_relative(root);
    const bool within_root = !ec && !rel.empty() && *rel.begin() != "..";

    if (within_root && fs::exists(filePath) && !fs::is_directory(filePath)) {
        string pathStr = filePath.string();
        string etag = MakeETag(filePath);

        // INFO: Conditional request: the client already holds this exact
        //       version, so skip resending the body. This is what makes
        //       revalidation of the large unhashed font cheap once its
        //       max-age lapses — an empty 304 instead of ~1 MB on the wire.
        if (!etag.empty() && if_none_match == etag) {
            res->writeStatus("304 Not Modified")
                ->writeHeader("Cache-Control", CacheControlFor(relativePath))
                ->writeHeader("ETag", etag)
                ->end();
            return;
        }

        res->writeHeader("Content-Type", GetMimeType(pathStr))
            ->writeHeader("Cache-Control", CacheControlFor(relativePath))
            ->writeHeader("X-Content-Type-Options", "nosniff");
        if (!etag.empty()) {
            res->writeHeader("ETag", etag);
        }
        res->end(ReadFile(pathStr));
    } else {
        Logger::Log("[GET] 404 – ", filePath.string());
        res->writeStatus("404 Not Found")->end("File not found");
    }
}

void WebServer::OnSocketOpen(AppWebSocket* socket) {
    PerSocketData *socket_data = socket->getUserData();
    connections_[socket_data->username] = socket;

    for (auto handler : on_open_hooks_) {
        handler(socket, socket_data);
    }

    Logger::Log("[WS] Connection upgraded: ", socket_data->username);
}

void WebServer::OnSocketClosed(AppWebSocket* socket) {
    PerSocketData *socket_data = socket->getUserData();

    for (auto handler : on_close_hooks_) {
        handler(socket, socket_data);
    }

    connections_.erase(socket_data->username);

    // INFO: Release this IP's connection slot; drop the entry once it
    //       reaches zero.
    if (auto it = conn_per_ip_.find(socket_data->ip); it != conn_per_ip_.end()) {
        if (--it->second <= 0) {
            conn_per_ip_.erase(it);
        }
    }

    Logger::Log("[WS] Connection closed: ", socket_data->username);
}

void WebServer::OnSocketMessage(AppWebSocket *socket, string_view message, uWS::OpCode op_code) {
    // INFO: A malformed frame must never escape this callback: an exception
    //       thrown through uWebSockets' C event loop terminates the whole
    //       process, so a single junk frame from any connected client would
    //       be a remote DoS.
    json message_json = json::parse(message, nullptr, /*allow_exceptions=*/false);

    if (message_json.is_discarded()) {
        Logger::Warn("[WS] Dropping malformed JSON frame");
        return;
    }

    // INFO: Downstream helpers call json::value(), which throws on
    //       non-objects, so a well-formed but non-object payload (e.g. "5"
    //       or "[]") must be rejected too.
    if (!message_json.is_object() || !message_json.contains("action")) {
        Logger::Warn("[WS] Message missing 'action' field");
        return;
    }

    WsContext context = { .socket = socket, .socket_data = socket->getUserData(), .op_code = op_code};
    
    if (!ws_router_.Dispatch(context, message_json)) {
        Logger::Warn("No handler found for action: " + message_json.value("action", "UNKNOWN"));
    }
}

string WebServer::ReadFile(string_view path) {
    ifstream is(path.data(), ios::binary);

    if (!is) {
        return "";
    }

    stringstream buf;
    buf << is.rdbuf();
    return buf.str();
}

string WebServer::GetMimeType(const string &path) {
    if (path.ends_with(".html"))   return "text/html";
    if (path.ends_with(".js"))     return "text/javascript";
    if (path.ends_with(".css"))    return "text/css";
    if (path.ends_with(".svg"))    return "image/svg+xml";
    if (path.ends_with(".png"))    return "image/png";
    if (path.ends_with(".woff2"))  return "font/woff2";
    if (path.ends_with(".woff"))   return "font/woff";
    return "application/octet-stream";
}

void WebServer::OnConnectionOpen(ConnectionHandler handler) {
    on_open_hooks_.push_back(std::move(handler));
}

void WebServer::OnConnectionClose(ConnectionHandler handler) {
    on_close_hooks_.push_back(std::move(handler));
}
