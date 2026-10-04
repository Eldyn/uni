#include "action_router.hpp"
#include "common/http.hpp"
#include "common/ws.hpp"
#include "common/env.hpp"
#include <fstream>
#include "services/auth_service.hpp"
#include "http_router.hpp"
#include "websocket_context.hpp"
#include <WebSocketProtocol.h>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <App.h>
#include <webserver.hpp>
#include <database.hpp>
#include <logger.hpp>
#include "services/account_reaper.hpp"
#include "common/email_queue.hpp"
#include "common/email_templates.hpp"
#include "services/verification_service.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

WebServer::WebServer(int port, std::string_view key_file, std::string_view cert_file,
                     std::string_view db_file, std::string_view frontend_path,
                     EmailQueue* email_queue)
    : port_(port), db_file_(db_file), frontend_path_(frontend_path),
      trust_proxy_(Env::Get("TRUST_PROXY", "0") != "0"),
      static_cache_enabled_(Env::Get("STATIC_CACHE", "1") != "0"),
      email_queue_(email_queue),
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
        throw std::runtime_error("Failed to initialise database");
    }

    auto grace_days = std::stoull(Env::Get("UNVERIFIED_GRACE_DAYS", "7"));
    auto interval_sec = std::stoull(Env::Get("REAPER_INTERVAL_SEC", "3600"));
    reaper_ = std::make_unique<AccountReaper>(Database::Get(),
                                              std::chrono::seconds(interval_sec),
                                              std::chrono::hours(24 * grace_days));
    reaper_->Start();

    RegisterRoutes();
    if (static_cache_enabled_) {
        LoadStaticFileCache();
    } else {
        Logger::Info("Static file cache: disabled (STATIC_CACHE=0), serving " + frontend_path_ +
                     " straight off disk");
    }
    if constexpr (kAppSSL) {
        Logger::Info("Key file: " + std::string(key_file) + " exists=" +
                     (fs::exists(key_file) ? "yes" : "NO"));
        Logger::Info("Cert file: " + std::string(cert_file) + " exists=" +
                     (fs::exists(cert_file) ? "yes" : "NO"));
    } else {
        Logger::Info("TLS disabled (plain HTTP), UNI_ENABLE_SSL=0");
    }
    Logger::Info("WebServer constructed");
}

WebServer::~WebServer() {
    Stop();
    if (db_file_ != ":memory:" && Database::Get().IsOpen()) {
        Database::Get().Close();
        Logger::Info("Database closed");
    }
}

void WebServer::Run(std::function<void(bool)> on_listen) {
    http_router_.Attach(app_);
    loop_ = uWS::Loop::get();

    app_.listen(port_, [this, on_listen = std::move(on_listen)](auto *socket) {
        listen_socket_ = socket;
        if (socket) {
            Logger::Log("Server listening on ", (kAppSSL ? "https" : "http"),
                       "://localhost:", port_);
        } else {
            Logger::Error("Failed to bind to port " + std::to_string(port_));
        }
        if (on_listen) {
            on_listen(socket != nullptr);
        }
    });
    app_.run();
}

void WebServer::Stop() {
    if (reaper_) {
        reaper_->Stop();
    }
    if (listen_socket_ && loop_) {
        loop_->defer([this]() {
            if (listen_socket_) {
                us_listen_socket_close(kAppSSL, listen_socket_);
                listen_socket_ = nullptr;
            }
        });
    }
}

bool WebServer::InitDB() {
    if (Database::Get().IsOpen() && db_file_ == ":memory:") {
        return Database::Get().RunMigrations().has_value();
    }

    VoidResult open_result = Database::Get().Open(db_file_);

    if (!open_result) {
        Logger::Error("Database opening failed: " + open_result.error().message);
        return false;
    }

    VoidResult migration_result = Database::Get().RunMigrations();

    if (!migration_result) {
        Logger::Error("Migration failed: " + migration_result.error().message);
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
        try {
            const std::string token = Env::Get("DEPLOY_STATUS_TOKEN", "");
            if (token.empty() || req->getHeader("x-deploy-token") != token) {
                res->writeStatus("404 Not Found")->end("File not found");
                return;
            }
            const std::size_t count = active_match_provider_ ? active_match_provider_() : 0;
            res->writeHeader("Content-Type", "application/json")
               ->end(json({{"active_matches", count}}).dump());
        } catch (const std::exception& e) {
            Logger::Error("[HTTP] internal active-games: ", e.what());
            res->writeStatus("500 Internal Server Error")->end();
        }
    });

    // INFO: One-time migration email blast endpoint for unverified users.
    //       Re-running re-issues codes and re-sends — intentional (retry mechanism)
    //       but burns Brevo quota, hence token gate and cap.
    app_.post("/internal/verify-blast", [this](AppResponse *res, AppRequest *req) {
        try {
            const std::string token = Env::Get("DEPLOY_STATUS_TOKEN", "");
            if (token.empty() || req->getHeader("x-deploy-token") != token) {
                res->writeStatus("404 Not Found")->end("File not found");
                return;
            }

            http::ReadBody(res, 4096, [this, res](const std::string& /*body*/) {
                try {
                    const int cap = Env::GetInt("BLAST_MAX_RECIPIENTS", 50);
                    auto rows = Database::Get().Query(
                        "SELECT id, username, email, locale FROM users WHERE email_verified = 0;");
                    if (!rows) {
                        Logger::Error("[VerifyBlast] Query failed: " + rows.error().message);
                        res->writeStatus("500 Internal Server Error")->end();
                        return;
                    }

                    if (static_cast<int>(rows->size()) > cap) {
                        res->writeStatus("409 Conflict")
                           ->writeHeader("Content-Type", "application/json")
                           ->end(json({{"error", "too many recipients"}}).dump());
                        return;
                    }

                    VerificationService verifier(Database::Get());
                    int queued = 0;
                    int skipped = 0;

                    for (const auto& row : *rows) {
                        const int user_id = row.Get<int>("id");
                        const std::string username = row.Get<std::string>("username");
                        const std::string email = row.Get<std::string>("email");
                        const std::string locale = row.Get<std::string>("locale");

                        if (!email_queue_) {
                            Logger::Error("[VerifyBlast] No email queue configured; skipping " +
                                          username);
                            ++skipped;
                            continue;
                        }

                        auto issue_res = verifier.IssueCodeForEmail(email);
                        if (!issue_res) {
                            Logger::Error("[VerifyBlast] Failed to issue code for " + username +
                                          " (" + email + "): " + issue_res.error().message);
                            ++skipped;
                            continue;
                        }

                        try {
                            auto mail = RenderMigrationEmail(VerifyEmailData{
                                .username = username,
                                .code = issue_res->plaintext_code,
                                .magic_link = BuildVerifyMagicLink(issue_res->plaintext_code),
                                .locale = locale
                            });
                            mail.to_address = email;

                            email_queue_->Enqueue(std::move(mail));
                            verifier.RecordSend(user_id);
                            ++queued;
                        } catch (const std::exception& e) {
                            Logger::Error("[VerifyBlast] Failed to send email to " + username +
                                          ": " + e.what());
                            ++skipped;
                        }
                    }

                    res->writeHeader("Content-Type", "application/json")
                       ->end(json({{"queued", queued}, {"skipped", skipped}}).dump());
                } catch (const std::exception& e) {
                    Logger::Error("[HTTP] internal verify-blast: ", e.what());
                    res->writeStatus("500 Internal Server Error")->end();
                }
            });
        } catch (const std::exception& e) {
            Logger::Error("[HTTP] internal verify-blast: ", e.what());
            res->writeStatus("500 Internal Server Error")->end();
        }
    });

    app_.head("/*", [this](AppResponse *res, AppRequest *req) {
        HandleHead(res, req);
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
            ws::SendError(ctx.socket, uWS::OpCode::TEXT, contract::ErrorCode::kRateLimited,
                          request_id);
            return false;
        }
        return true;
    });

    // INFO: WebSocket transport limits (env-overridable). Capping the frame
    //       size, idle time and server-side backpressure stops oversized
    //       frames, slow-loris sockets and unbounded outbound buffering from
    //       exhausting memory.
    const uint32_t ws_max_payload =
        static_cast<uint32_t>(std::stoul(Env::Get("WS_MAX_PAYLOAD", "16384")));  // 16 KB
    const uint32_t ws_max_backpressure =
        static_cast<uint32_t>(std::stoul(Env::Get("WS_MAX_BACKPRESSURE", "1048576")));  // 1 MB
    const uint16_t ws_idle_timeout =
        static_cast<uint16_t>(std::stoi(Env::Get("WS_IDLE_TIMEOUT", "120")));  // seconds

    // INFO: SHARED_COMPRESSOR negotiates permessage-deflate using one shared
    //       compressor (no per-socket memory). Gated by WS_COMPRESSION so it
    //       can be disabled without a rebuild if profiling shows CPU pressure.
    const bool ws_compression = (Env::Get("WS_COMPRESSION", "1") != "0");
    const auto compression_mode = ws_compression
        ? uWS::SHARED_COMPRESSOR
        : uWS::DISABLED;

    app_.ws<PerSocketData>("/*", {
        .compression = compression_mode,
        .maxPayloadLength = ws_max_payload,
        .idleTimeout = ws_idle_timeout,
        .maxBackpressure = ws_max_backpressure,
        .closeOnBackpressureLimit = true,
        .sendPingsAutomatically = true,
        .upgrade = [this](AppResponse* res, AppRequest*  req, us_socket_context_t* ctx) {
            std::string ip;
            bool conn_counted = false;
            try {
                std::string_view cookies = req->getHeader("cookie");
                // INFO: ws_token (SameSite=None) is accepted for cross-origin embeds
                //       (e.g. itch.io); auth_token (SameSite=Strict) covers direct use.
                auto token = http::GetCookieValue(cookies, "ws_token");
                if (!token || token->empty()) token = http::GetCookieValue(cookies, "auth_token");

                // INFO: Capture the IP before upgrade() invalidates the request
                //       object.
                ip = http::GetClientIp(res, req, trust_proxy_);

                // INFO: CSWSH defence for the SameSite=None ws_token cookie: a
                //       browser-supplied Origin must be same-origin (empty
                //       allowlist) or explicitly allowed; non-browser clients
                //       send no Origin and cannot be CSRF'd.
                const std::string origin = std::string(req->getHeader("origin"));
                const std::string host   = std::string(req->getHeader("host"));
                if (!http::IsAllowedWsOrigin(origin, host, Env::Get("WS_ALLOWED_ORIGINS", ""))) {
                    Logger::Warn("[WS] Rejected upgrade, disallowed origin");
                    res->writeStatus("403 Forbidden")->end();
                    return;
                }

                if (!token || token->empty()) {
                    Logger::Warn("[WS] Rejected upgrade, missing token ip=" + ip);
                    res->writeStatus("401 Unauthorized")->end();
                    return;
                }

                auto payload = AuthService::VerifyToken(*token);

                if (!payload) {
                    Logger::Warn("[WS] Rejected upgrade, invalid token ip=" + ip);
                    res->writeStatus("401 Unauthorized")->end();
                    return;
                }

                // INFO: Cap concurrent connections per IP so one host cannot
                //       exhaust the server's sockets. The counter is incremented
                //       here and decremented in OnSocketClosed.
                if (max_conn_per_ip_ > 0 && conn_per_ip_[ip] >= max_conn_per_ip_) {
                    Logger::Warn("[WS] Rejected upgrade, connection cap reached ip=" + ip);
                    res->writeStatus("429 Too Many Requests")->end();
                    return;
                }

                PerSocketData socket_data;
                socket_data.username = payload->username;
                socket_data.ip = ip;
                ++conn_per_ip_[ip];
                conn_counted = true;

                res->upgrade(std::move(socket_data),
                    req->getHeader("sec-websocket-key"),
                    req->getHeader("sec-websocket-protocol"),
                    req->getHeader("sec-websocket-extensions"),
                    ctx);
                conn_counted = false;
            } catch (const std::exception& e) {
                // INFO: Roll back the per-IP slot if the upgrade threw before a
                //       socket existed: OnSocketClosed never runs for a failed
                //       upgrade, so the count would otherwise leak and lock the
                //       IP out once it reaches the cap.
                if (conn_counted) {
                    if (auto it = conn_per_ip_.find(ip); it != conn_per_ip_.end()) {
                        if (--it->second <= 0) {
                            conn_per_ip_.erase(it);
                        }
                    }
                }
                Logger::Error("[WS] upgrade handler: ", e.what());
                res->writeStatus("500 Internal Server Error")->end();
            }
        },
        .open = [this](AppWebSocket *ws) {
            try {
                OnSocketOpen(ws);
            } catch (const std::exception& e) {
                Logger::Error("[WS] open handler: ", e.what());
                ws->close();
            }
        },
        .message = [this](AppWebSocket *ws, std::string_view message, uWS::OpCode op) {
            try {
                OnSocketMessage(ws, message, op);
            } catch (const std::exception& e) {
                Logger::Error("[WS] message handler: ", e.what());
            }
        },
        .close = [this](AppWebSocket *ws, int code, std::string_view message) {
            try {
                OnSocketClosed(ws);
            } catch (const std::exception& e) {
                Logger::Error("[WS] close handler: ", e.what());
            }
        }
    });
}

void WebServer::HandlePost(AppResponse *response, AppRequest *request) {
    // INFO: req is invalid once ReadBody's async callback runs, so capture the
    //       cookies it needs by value now.
    const std::string cookies = std::string(request->getHeader("cookie"));
    try {
        http::ReadBody(response, 4096, [response, cookies](const std::string body) {
            try {
                json data;
                try {
                    data = json::parse(body);
                } catch (...) {
                    response->writeStatus("400 Bad Request")
                            ->writeHeader("Content-Type", "application/json")
                            ->end(json({{"error", "Invalid JSON"}}).dump());
                    return;
                }

                std::string_view cookie_view = cookies;
                auto token = http::GetCookieValue(cookie_view, "auth_token");
                auto payload = AuthService::VerifyToken(*token);

                if (!payload) {
                    response->writeStatus("401 Unauthorized")->end();
                    return;
                }

                std::string topic = data.value("topic", "default");
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
            } catch (const std::exception& e) {
                Logger::Error("[HTTP] post handler: ", e.what());
                response->writeStatus("500 Internal Server Error")->end();
            }
        });
    } catch (const std::exception& e) {
        Logger::Error("[HTTP] post handler: ", e.what());
        response->writeStatus("500 Internal Server Error")->end();
    }
}

void WebServer::HandleHead(AppResponse *res, AppRequest *req) {
    auto is_alive = std::make_shared<bool>(true);
    res->onAborted([is_alive]() { *is_alive = false; });

    try {
        if (!*is_alive) return;

        std::string url = std::string(req->getUrl());
        std::string relativePath = (url == "/") ? "index.html" : url.substr(1);

        // INFO: Refuse dot-leading path segments (.env, .git/config) so a
        //       stray file in the served root cannot be fetched. The literal
        //       .well-known/ prefix stays reachable for discovery endpoints.
        if (http::HasForbiddenDotSegment(relativePath)) {
            res->writeStatus("404 Not Found")->end();
            return;
        }

        std::string if_none_match = std::string(req->getHeader("if-none-match"));
        std::string accept_encoding = std::string(req->getHeader("accept-encoding"));

        auto resolved = http::ResolveSafePath(fs::path(frontend_path_), relativePath);

        if (resolved && fs::exists(*resolved) && !fs::is_directory(*resolved)) {
            const fs::path& filePath = *resolved;
            std::string pathStr = filePath.string();

            // A HEAD must describe exactly the response a GET would produce, so it
            // has to pick the same encoding and report that variant's ETag/length.
            fs::path bodyPath = filePath;
            std::string_view contentEncoding;
            if (auto precompressed = http::SelectPrecompressed(filePath, accept_encoding)) {
                bodyPath        = precompressed->path;
                contentEncoding = precompressed->encoding;
            }

            std::string etag = http::MakeETag(bodyPath);

            if (!etag.empty() && if_none_match == etag) {
                res->writeStatus("304 Not Modified")
                    ->writeHeader("Cache-Control", http::CacheControlFor(relativePath))
                    ->writeHeader("Vary", "Accept-Encoding")
                    ->writeHeader("ETag", etag)
                    ->end();
                return;
            }

            res->writeHeader("Content-Type", http::GetMimeType(pathStr))
                ->writeHeader("Cache-Control", http::CacheControlFor(relativePath))
                ->writeHeader("Vary", "Accept-Encoding")
                ->writeHeader("X-Content-Type-Options", "nosniff");
            if (!contentEncoding.empty()) {
                res->writeHeader("Content-Encoding", contentEncoding);
            }
            if (!etag.empty()) {
                res->writeHeader("ETag", etag);
            }
            res->end();
        } else if (http::IsClientRoute(relativePath)) {
            // Mirrors HandleGet's client-route fallback: a HEAD must describe
            // exactly what the equivalent GET would produce, and that GET serves
            // the app shell for a client-side route rather than 404ing.
            auto index_resolved = http::ResolveSafePath(fs::path(frontend_path_), "index.html");
            if (index_resolved && fs::exists(*index_resolved)) {
                res->writeHeader("Content-Type", http::GetMimeType("index.html"))
                    ->writeHeader("Cache-Control", http::CacheControlFor("index.html"))
                    ->writeHeader("X-Content-Type-Options", "nosniff")
                    ->end();
            } else {
                res->writeStatus("404 Not Found")->end();
            }
        } else {
            res->writeStatus("404 Not Found")->end();
        }
    } catch (const std::exception& e) {
        Logger::Error("[HTTP] head handler: ", e.what());
        res->writeStatus("500 Internal Server Error")->end();
    }
}

void WebServer::HandleGet(AppResponse *res, AppRequest *req) {
    auto is_alive = std::make_shared<bool>(true);
    res->onAborted([is_alive]() {*is_alive = false;});

    try {
        if (!*is_alive) return;

        std::string url = std::string(req->getUrl());
        std::string relativePath = (url == "/") ? "index.html" : url.substr(1);

        // INFO: Refuse dot-leading path segments (.env, .git/config) so a
        //       stray file in the served root cannot be fetched. The literal
        //       .well-known/ prefix stays reachable for discovery endpoints.
        if (http::HasForbiddenDotSegment(relativePath)) {
            res->writeStatus("404 Not Found")->end();
            return;
        }

        // INFO: Capture the conditional-request header now: uWebSockets recycles
        //       the request object as soon as the response is written, so it
        //       cannot be read afterwards.
        std::string if_none_match = std::string(req->getHeader("if-none-match"));
        std::string accept_encoding = std::string(req->getHeader("accept-encoding"));

        // INFO: Resolve both the served root and the requested file to canonical
        //       form so that "../" segments and symlinks are collapsed, then
        //       confirm the result stays inside the root. Without this, a raw
        //       request such as "GET /../../etc/passwd" would escape
        //       frontend_path_ and disclose host files.
        auto resolved = http::ResolveSafePath(fs::path(frontend_path_), relativePath);

        if (resolved && fs::exists(*resolved) && !fs::is_directory(*resolved)) {
            const fs::path& filePath = *resolved;
            std::string pathStr = filePath.string();

            // INFO: Prefer the build-time .br/.gz sidecar whenever the client accepts
            //       it. The bundle carrying three.js/Threlte is why this matters,
            //       it is by far the heaviest asset served, and shipping a
            //       precompressed body costs no per-request CPU. Content-Type still
            //       comes from the *uncompressed* name: gzip is transport encoding,
            //       not the media type.
            fs::path bodyPath = filePath;
            std::string_view contentEncoding;
            if (auto precompressed = http::SelectPrecompressed(filePath, accept_encoding)) {
                bodyPath        = precompressed->path;
                contentEncoding = precompressed->encoding;
            }

            // The two encodings are distinct representations, so they must not
            // share a validator, MakeETag stats whichever one is actually sent.
            std::string etag = http::MakeETag(bodyPath);

            // INFO: Conditional request: the client already holds this exact
            //       version, so skip resending the body. This is what makes
            //       revalidation of the large unhashed font cheap once its
            //       max-age lapses, an empty 304 instead of ~1 MB on the wire.
            if (!etag.empty() && if_none_match == etag) {
                res->writeStatus("304 Not Modified")
                    ->writeHeader("Cache-Control", http::CacheControlFor(relativePath))
                    ->writeHeader("Vary", "Accept-Encoding")
                    ->writeHeader("ETag", etag)
                    ->end();
                return;
            }

            res->writeHeader("Content-Type", http::GetMimeType(pathStr))
                ->writeHeader("Cache-Control", http::CacheControlFor(relativePath))
                ->writeHeader("Vary", "Accept-Encoding")
                ->writeHeader("X-Content-Type-Options", "nosniff");
            if (!contentEncoding.empty()) {
                res->writeHeader("Content-Encoding", contentEncoding);
            }
            if (!etag.empty()) {
                res->writeHeader("ETag", etag);
            }
            res->end(ReadFile(bodyPath.string()));
        } else if (http::IsClientRoute(relativePath)) {
            // Not a real file, but shaped like a client-side route (no dot in its
            // final segment) rather than a missing asset — serve the app shell so
            // client-side routing can take over. A genuinely missing asset
            // (favicon.ico, a mistyped .js path) still falls through to 404 below.
            auto index_resolved = http::ResolveSafePath(fs::path(frontend_path_), "index.html");
            if (index_resolved && fs::exists(*index_resolved)) {
                res->writeHeader("Content-Type", http::GetMimeType("index.html"))
                    ->writeHeader("Cache-Control", http::CacheControlFor("index.html"))
                    ->writeHeader("X-Content-Type-Options", "nosniff")
                    ->end(ReadFile(index_resolved->string()));
            } else {
                res->writeStatus("404 Not Found")->end("File not found");
            }
        } else {
            std::error_code ec;
            fs::path logged_path =
                fs::weakly_canonical(fs::path(frontend_path_) / relativePath, ec);
            Logger::Log("[GET] 404 – ", logged_path.string());
            res->writeStatus("404 Not Found")->end("File not found");
        }
    } catch (const std::exception& e) {
        Logger::Error("[HTTP] get handler: ", e.what());
        res->writeStatus("500 Internal Server Error")->end();
    }
}

void WebServer::OnSocketOpen(AppWebSocket* socket) {
    PerSocketData *socket_data = socket->getUserData();

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

    // INFO: Release this IP's connection slot; drop the entry once it
    //       reaches zero.
    if (auto it = conn_per_ip_.find(socket_data->ip); it != conn_per_ip_.end()) {
        if (--it->second <= 0) {
            conn_per_ip_.erase(it);
        }
    }

    Logger::Log("[WS] Connection closed: ", socket_data->username);
}

void WebServer::OnSocketMessage(AppWebSocket *socket, std::string_view message,
                                uWS::OpCode op_code) {
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
    if (!message_json.is_object() || !message_json.contains("action") ||
        !message_json["action"].is_string()) {
        Logger::Warn("[WS] Message missing or invalid 'action' field");
        return;
    }

    WsContext context = { .socket = socket, .socket_data = socket->getUserData(),
                          .op_code = op_code};

    try {
        if (!ws_router_.Dispatch(context, message_json)) {
            Logger::Warn("No handler found for action: " +
                         message_json["action"].get<std::string>());
        }
    } catch (const std::exception& e) {
        Logger::Error("[WS] Uncaught exception in OnSocketMessage: ", e.what());
    }
}

void WebServer::LoadStaticFileCache() {
    std::error_code ec;
    fs::path root = fs::weakly_canonical(frontend_path_, ec);

    if (ec || !fs::exists(root)) {
        Logger::Warn("Static file cache: root does not exist: " + frontend_path_);
        return;
    }

    for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
        if (ec) break;
        // INFO: Never cache symlinks. is_regular_file() follows links, so a
        //       symlink pointing at a host file (.env, /etc/passwd) would
        //       otherwise be admitted into the cache and served.
        if (entry.is_symlink()) continue;
        if (!entry.is_regular_file()) continue;

        std::ifstream is(entry.path(), std::ios::binary);
        if (!is) continue;

        std::stringstream buf;
        buf << is.rdbuf();
        static_file_cache_.emplace(entry.path().string(), buf.str());
    }

    Logger::Info("Static file cache: loaded " + std::to_string(static_file_cache_.size()) +
                 " file(s) from " + root.string());
}

std::string WebServer::ReadFile(std::string_view path) const {
    // INFO: Hot path, the cache is populated once at startup by
    //       LoadStaticFileCache(), so a busy server serving the same asset
    //       repeatedly avoids the disk read + stringstream churn per request.
    if (auto it = static_file_cache_.find(std::string(path)); it != static_file_cache_.end()) {
        return it->second;
    }

    // INFO: Fallback for a file that exists on disk but was not present in
    //       the cache at startup (e.g. added after the server was launched).
    //       Kept so behaviour matches the pre-cache implementation instead of
    //       silently 200-ing with an empty body.
    std::ifstream is(path.data(), std::ios::binary);

    if (!is) {
        return "";
    }

    std::stringstream buf;
    buf << is.rdbuf();
    return buf.str();
}

void WebServer::OnConnectionOpen(ConnectionHandler handler) {
    on_open_hooks_.push_back(std::move(handler));
}

void WebServer::OnConnectionClose(ConnectionHandler handler) {
    on_close_hooks_.push_back(std::move(handler));
}
