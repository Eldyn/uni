#include "action_router.hpp"
#include "common/http.hpp"
#include "controllers/auth_controller.hpp"
#include "http_router.hpp"
#include "websocket_context.hpp"
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <string>
#include <string_view>
#include <App.h>
#include <webserver.hpp>
#include <database.hpp>
#include <logger.hpp>

WebServer::WebServer(int port, string_view key_file, string_view cert_file, string_view db_file)
    : port_(port), db_file_(db_file), app_(uWS::SSLApp({.key_file_name = key_file.data(), .cert_file_name = cert_file.data()})) {
    if (!InitDB()) {
        throw runtime_error("Failed to initialise database");
    }
    RegisterRoutes();
    Logger::Info("Key file: " + string(key_file) + " exists=" + (fs::exists(key_file) ? "yes" : "NO"));
    Logger::Info("Cert file: " + string(cert_file) + " exists=" + (fs::exists(cert_file) ? "yes" : "NO"));
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
            Logger::Log("Server listening on https://localhost:", port_);
        } else {
            Logger::Error("Failed to bind to port " + to_string(port_));
        }
    });
    app_.run();
}

bool WebServer::InitDB() {
    VoidResult openResult = Database::Get().Open(db_file_); 
    
    if (!openResult) {
        Logger::Error("Database opening failed: " + openResult.error().message);
        return false;
    }

    const char *schema = R"(
        CREATE TABLE IF NOT EXISTS rooms (
            topic        TEXT     PRIMARY KEY,
            clicks       INTEGER  NOT NULL DEFAULT 0,
            last_clicker TEXT,
            created_at   DATETIME DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS users (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            username   TEXT    NOT NULL UNIQUE,
            pass_hash  TEXT    NOT NULL,
            salt       TEXT    NOT NULL,
            email      TEXT    UNIQUE
        );
    )";

    VoidResult schemaResult = Database::Get().ApplySchema(schema);

    if (!schemaResult) {
        Logger::Error("Schema failed: " + schemaResult.error().message);
        return false;
    }

    return true;
}

void WebServer::RegisterRoutes() {
    ws_router_.On("query", [this](WsContext context, const json& msg) {
        if (context.socket_data->room.empty()) {
            Logger::Warn("User tried do query a room while not being in one!");
            return false;
        };
        context.socket->send(json({
            {"action", "queried"},
            {"clicks", to_string(GetClicks(context.socket_data->room))},
            {"lastClicker", GetLastClicker(context.socket_data->room)}
        }).dump(), context.op_code);
        return true;
    });

    ws_router_.On("join", [this](WsContext context, const json& msg) {
        if (!msg.contains("topic")) {
            return false;
        }
    
        if (!context.socket_data->room.empty()) {
            Logger::Warn("[WS] " + context.socket_data->username + " tried to join whilst already in a room");
            return false;
        }
    
        string topic = msg["topic"];
        context.socket_data->room = topic;
        context.socket->subscribe(topic);
        EnsureRoom(topic);

        context.socket->send(json({
            {"action", "sync_data"},
            {"username", context.socket_data->username},
            {"room", context.socket_data->room}
        }).dump(), uWS::OpCode::TEXT);
        return true;
    });

    ws_router_.On("click", [this](WsContext context, const json& msg) {
        if (context.socket_data->room.empty()) {
            Logger::Warn("[WS] Click from " + context.socket_data->username + " who is not in a room");
            return false;
        }
    
        IncrementClicks(context.socket_data->room);
        SetLastClicker(context.socket_data->room, context.socket_data->username);
        int total = GetClicks(context.socket_data->room);
    
        app_.publish(context.socket_data->room, json({
            {"action", "sync_count"},
            {"count", total},
            {"last_clicker", GetLastClicker(context.socket_data->room)}
        }).dump(), context.op_code, true);
        return true;
    });

    http_router_.Post("/room", [this](auto *response, auto *request) {
        HandlePost(response, request);
    });

    app_.get("/*", [this](AppResponse *res, AppRequest *req) { 
        HandleGet(res, req);
    });

    app_.ws<PerSocketData>("/*", {
        .upgrade = [this](AppResponse* res, AppRequest*  req, us_socket_context_t* ctx) {
            string token = string(req->getQuery("token"));
            auto payload = AuthController::VerifyToken(token);

            if (!payload) {
                Logger::Warn("[WS] Rejected upgrade — invalid token");
                res->writeStatus("401 Unauthorized")->end();
                return;
            }

            PerSocketData sd;
            sd.username = payload->username;

            res->upgrade(std::move(sd),
                req->getHeader("sec-websocket-key"),
                req->getHeader("sec-websocket-protocol"),
                req->getHeader("sec-websocket-extensions"),
                ctx);
        },
        .open = [this](AppWebSocket *ws) {
            OnSocketOpen(ws);
        },
        .message = [this](AppWebSocket *ws, string_view msg, uWS::OpCode op) {
            OnSocketMessage(ws, msg, op);
        },
        .close = [this](AppWebSocket *ws, int code, string_view message) {
            OnSocketClosed(ws);
        }
    });
}

void WebServer::HandlePost(AppResponse *response, AppRequest *request) {
    http::ReadBody(response, 4096, [response](const std::string body) {
            json data;
            try {
                data = json::parse(body);
            } catch (...) {
                response->writeStatus("400 Bad Request")
                        ->writeHeader("Content-Type", "application/json")
                        ->end(json({{"error", "Invalid JSON"}}).dump());
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

void WebServer::HandleGet(AppResponse *res, AppRequest *req) {
    auto is_alive = std::make_shared<bool>(true);
    res->onAborted([is_alive]() {*is_alive = false;});

    if (!*is_alive) return;
    
    string url = string(req->getUrl());
    string relativePath = (url == "/") ? "index.html" : url.substr(1);
    fs::path filePath = fs::current_path() / "public" / relativePath;
    
    if (fs::exists(filePath) && !fs::is_directory(filePath)) {
        string pathStr = filePath.string();
        res->writeHeader("Content-Type", GetMimeType(pathStr))
            ->writeHeader("Cache-Control", "no-cache, no-store, must-revalidate")
            ->writeHeader("Pragma", "no-cache")
            ->writeHeader("Expires", "0")
            ->writeHeader("X-Content-Type-Options", "nosniff")
            ->end(ReadFile(pathStr));
    } else {
        Logger::Log("[GET] 404 – ", filePath.string());
        res->writeStatus("404 Not Found")->end("File not found");
    }
}

// ────────────────────────────────────────────────────────────────
//  WebSocket – open
// ────────────────────────────────────────────────────────────────

void WebServer::OnSocketOpen(AppWebSocket* ws) {
    PerSocketData *sd = ws->getUserData();
    sd->username = MakeUsername();

    connections_[sd->username] = ws;
    Logger::Log("[WS] Connection upgraded: ", sd->username);
}

void WebServer::OnSocketClosed(AppWebSocket* ws) {
    PerSocketData *sd = ws->getUserData();
    connections_.erase(sd->username);

    Logger::Log("[WS] Connection closed: ", sd->username);
}

void WebServer::OnSocketMessage(AppWebSocket *socket, string_view message, uWS::OpCode op_code) {
    json message_json = json::parse(message);
    
    if (!message_json.contains("action")) {
        Logger::Warn("[WS] Message missing 'action' field");
        return;
    }

    WsContext context = { .socket = socket, .socket_data = socket->getUserData(), .op_code = op_code};
    
    if (!ws_router_.Dispatch(context, message_json)) {
        Logger::Warn("No handler found for action: " + message_json.value("action", "UNKNOWN"));
    }
}

void WebServer::EnsureRoom(const string &topic) {
    VoidResult result = Database::Get().Exec("INSERT OR IGNORE INTO rooms (topic, clicks) VALUES (?, 0);", {topic});
    if (!result) {
        Error error = result.error();
        Logger::Error("DB error in ensureRoom: " + error.message);
    }
}

void WebServer::IncrementClicks(const string &topic) {
    VoidResult result = Database::Get().Exec("UPDATE rooms SET clicks = clicks + 1 WHERE topic = ?;", {topic});
    if (!result) {
        Error error = result.error();
        Logger::Error("DB error in incrementClicks: " + error.message);
    }
}

void WebServer::SetLastClicker(const string &topic, const string &username) {
    VoidResult result = Database::Get().Exec("UPDATE rooms SET last_clicker = ? WHERE topic = ?;", {username, topic});
    if (!result) {
        Error error = result.error();
        Logger::Error("DB error in setLastClicker: " + error.message);
    }
}

int WebServer::GetClicks(const string& topic) {
    auto result = Database::Get().QueryOne("SELECT clicks FROM rooms WHERE topic = ?;", {topic});

    // 1. Check if the Database operation itself failed
    if (!result) {
        Logger::Error("DB error in getClicks: " + result.error().message);
        return -1; // Fallback
    }

    // 2. Access the Optional (did the row actually exist?)
    // 'result.value()' is the optional<DbRow>
    auto& maybe_row = result.value(); 

    if (!maybe_row.has_value()) {
        Logger::Warn("Room not found: " + topic);
        return -1; // Fallback if room doesn't exist
    }

    // 3. Get the value safely using the template type
    return maybe_row->GetOr<int>("clicks", 0);
}

string WebServer::GetLastClicker(const string &topic) {
    auto result = Database::Get().QueryOne("SELECT last_clicker FROM rooms WHERE topic = ?;", {topic});
    if (!result) {
        Logger::Error("DB error in getLastClicker: " + result.error().message);
        return "unknown";
    }

    auto& maybe_row = result.value();

    if (!maybe_row.has_value()) {
        Logger::Warn("Room not found: " + topic);
        return "unknown";
    }

    return maybe_row->GetOr<string>("last_clicker", "unknown");
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
    return "application/octet-stream";
}

string WebServer::MakeUsername() {
    return "player_" + to_string(rand() % 100);
}
