#include "../include/logger.hpp"
#include "database.hpp"
#include "result.hpp"
#include "../include/webserver.hpp"
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <string>
#include <string_view>
#include <App.h>


WebServer::WebServer(int port, string_view keyFile, string_view certFile, string_view dbFile)
    : port_(port), dbFile_(dbFile), app_(uWS::SSLApp({.key_file_name = keyFile.data(), .cert_file_name = certFile.data()})) {
    if (!initDB()) {
        throw runtime_error("Failed to initialise database");
    }
    registerRoutes();
    Logger::info("Key file: " + string(keyFile) + " exists=" + (fs::exists(keyFile) ? "yes" : "NO"));
    Logger::info("Cert file: " + string(certFile) + " exists=" + (fs::exists(certFile) ? "yes" : "NO"));
    Logger::info("WebServer constructed");
}

WebServer::~WebServer() {
    if (Database::Get().IsOpen()) {
        Database::Get().Close();
        Logger::info("Database closed");
    }
}

void WebServer::run() {
    app_.listen(port_, [this](auto *socket) {
        if (socket) {
            Logger::log("Server listening on https://localhost:", port_);
        } else {
            Logger::error("Failed to bind to port " + to_string(port_));
        }
   });
    app_.run();
}

bool WebServer::initDB() {
    VoidResult openResult = Database::Get().Open(dbFile_); 
    
    if (!openResult) {
        Logger::error("Database opening failed: " + openResult.error().message);
        return false;
    }

    const char *schema = R"(
        CREATE TABLE IF NOT EXISTS rooms (
            topic        TEXT     PRIMARY KEY,
            clicks       INTEGER  NOT NULL DEFAULT 0,
            last_clicker TEXT,
            created_at   DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    )";

    VoidResult schemaResult = Database::Get().ApplySchema(schema);

    if (!schemaResult) {
        Logger::error("Schema failed: " + schemaResult.error().message);
        return false;
    }

    return true;
}

void WebServer::registerRoutes() {
    app_.post("/room", [this](AppResponse *res, AppRequest *req) {
        handlePost(res, req);
    });

    app_.get("/*", [this](AppResponse *res, AppRequest *req) { 
        handleGet(res, req);
    });

    app_.ws<PerSocketData>("/*", {
        .open = [this](AppWebSocket *ws) { handleSocketOpen(ws); },
        .message = [this](AppWebSocket *ws, string_view msg, uWS::OpCode op) { handleSocketMessage(ws, msg, op); },
        .close = [this](AppWebSocket *ws, int code, string_view message) { handleSocketClosed(ws); }
    });
}

void WebServer::handlePost(AppResponse *res, AppRequest *req) {
    res->onAborted([]() {});

    string buffer;
    Logger::info("[POST] /room received");

    res->onData([res, buffer = move(buffer)](string_view chunk, bool isLast) mutable {
        buffer.append(chunk.data(), chunk.length());

        if (buffer.size() > 4096) {
            res->writeStatus("413 Payload Too Large")->end();
            return;
        }

        if (isLast) {
            try {
                auto data = json::parse(buffer);
                string topic = data.value("topic", "default");
                if (topic.empty() || topic.size() > 64) {    
                    res->writeStatus("422 Unprocessable Entity")->end();
                }
                
                if (!all_of(topic.begin(), topic.end(), [](char c) {
                    return isalnum(c) || c == '-' || c == '_';
                })) {
                    res->writeStatus("422 Unprocessable Entity")->end();
                };

                Logger::log("[POST] /room topic: ", topic);
        
                res->writeHeader("Content-Type", "application/json")
                   ->end(json({ {"status", "OK"}, {"topic", topic} }).dump());
        
            } catch (const exception &e) {
                Logger::warn(string("[POST] JSON parse error: ") + e.what());
                res->writeStatus("400 Bad Request")->end("Invalid JSON");
            }
        }
    });
}

void WebServer::handleGet(AppResponse *res, AppRequest *req) {
    res->onAborted([]() {});
    
    string url = string(req->getUrl());
    string relativePath = (url == "/") ? "index.html" : url.substr(1);
    fs::path filePath = fs::current_path() / "public" / relativePath;
    
    if (fs::exists(filePath) && !fs::is_directory(filePath)) {
        string pathStr = filePath.string();
        res->writeHeader("Content-Type", getMimeType(pathStr))
            ->writeHeader("Cache-Control", "no-cache, no-store, must-revalidate")
            ->writeHeader("Pragma", "no-cache")
            ->writeHeader("Expires", "0")
            ->writeHeader("X-Content-Type-Options", "nosniff")
            ->end(readFile(pathStr));
    } else {
        Logger::log("[GET] 404 – ", filePath.string());
        res->writeStatus("404 Not Found")->end("File not found");
    }
}

// ────────────────────────────────────────────────────────────────
//  WebSocket – open
// ────────────────────────────────────────────────────────────────

void WebServer::handleSocketOpen(AppWebSocket* ws) {
    PerSocketData *sd = ws->getUserData();
    sd->username = makeUsername();

    connections_[sd->username] = ws;
    Logger::log("[WS] Connection upgraded: ", sd->username);
}

void WebServer::handleSocketClosed(AppWebSocket* ws) {
    PerSocketData *sd = ws->getUserData();
    connections_.erase(sd->username);

    Logger::log("[WS] Connection closed: ", sd->username);
}

void WebServer::handleSocketMessage(AppWebSocket *ws, string_view message, uWS::OpCode opCode) {
    PerSocketData *socketData = ws->getUserData();
    
    try {
        json message_json = json::parse(message);
    
        if (!message_json.contains("action")) {
        Logger::warn("[WS] Message missing 'action' field");
        return;
        }
    
        string action = message_json["action"];
    
        if (action == "query") {
            if (socketData->room.empty()) {
                Logger::warn("User tried do query a room while not being in one!");
                return;
            };

            ws->send(json({
                            {"action", "queried"},
                            {"clicks", to_string(getClicks(socketData->room))},
                            {"lastClicker", getLastClicker(socketData->room)}
                          }
                         ).dump(),
                    opCode);
        }
    
        // ── join ───────────────────────────────────────────
        if (action == "join") {
        if (!message_json.contains("topic"))
            return;
        string topic = message_json["topic"];
    
        if (!socketData->room.empty()) {
            Logger::warn("[WS] " + socketData->username + " tried to join whilst already in a room");
            return;
        }
    
        socketData->room = topic;
        ws->subscribe(topic);
        ensureRoom(topic);
    
        Logger::log("[WS] ", socketData->username, " joined room: ", topic);
    
        ws->send(json({{"action", "sync_data"},
                        {"username", socketData->username},
                        {"room", socketData->room}})
                    .dump(),
                uWS::OpCode::TEXT);
        }
    
        else if (action == "click") {
        if (socketData->room.empty()) {
            Logger::warn("[WS] Click from " + socketData->username + " who is not in a room");
            return;
        }
    
        incrementClicks(socketData->room);
        setLastClicker(socketData->room, socketData->username);
        int total = getClicks(socketData->room);
    
        app_.publish(socketData->room,
                    json({{"action", "sync_count"},
                            {"count", total},
                            {"last_clicker", getLastClicker(socketData->room)}
                         }
                        ).dump(),
                    opCode, true);
        }

    } catch (const json::exception &e) {
        Logger::warn(string("[WS] JSON error: ") + e.what());
    } catch (const exception &e) {
        Logger::error(string("[WS] Unexpected error: ") + e.what());
    }
}


void WebServer::ensureRoom(const string &topic) {
    VoidResult result = Database::Get().Exec("INSERT OR IGNORE INTO rooms (topic, clicks) VALUES (?, 0);", {topic});
    if (!result) {
        Error error = result.error();
        Logger::error("DB error in ensureRoom: " + error.message);
    }
}

void WebServer::incrementClicks(const string &topic) {
    VoidResult result = Database::Get().Exec("UPDATE rooms SET clicks = clicks + 1 WHERE topic = ?;", {topic});
    if (!result) {
        Error error = result.error();
        Logger::error("DB error in incrementClicks: " + error.message);
    }
}

void WebServer::setLastClicker(const string &topic, const string &username) {
    VoidResult result = Database::Get().Exec("UPDATE rooms SET last_clicker = ? WHERE topic = ?;", {username, topic});
    if (!result) {
        Error error = result.error();
        Logger::error("DB error in setLastClicker: " + error.message);
    }
}

int WebServer::getClicks(const string& topic) {
    auto result = Database::Get().QueryOne("SELECT clicks FROM rooms WHERE topic = ?;", {topic});

    // 1. Check if the Database operation itself failed
    if (!result) {
        Logger::error("DB error in getClicks: " + result.error().message);
        return -1; // Fallback
    }

    // 2. Access the Optional (did the row actually exist?)
    // 'result.value()' is the optional<DbRow>
    auto& maybe_row = result.value(); 

    if (!maybe_row.has_value()) {
        Logger::warn("Room not found: " + topic);
        return -1; // Fallback if room doesn't exist
    }

    // 3. Get the value safely using the template type
    return maybe_row->GetOr<int>("clicks", 0);
}

string WebServer::getLastClicker(const string &topic) {
    auto result = Database::Get().QueryOne("SELECT last_clicker FROM rooms WHERE topic = ?;", {topic});
    if (!result) {
        Logger::error("DB error in getLastClicker: " + result.error().message);
        return "unknown";
    }

    auto& maybe_row = result.value();

    if (!maybe_row.has_value()) {
        Logger::warn("Room not found: " + topic);
        return "unknown";
    }

    return maybe_row->GetOr<string>("last_clicker", "unknown");
}

string WebServer::readFile(string_view path) {
    ifstream is(path.data(), ios::binary);

    if (!is) {
        return "";
    }

    stringstream buf;
    buf << is.rdbuf();
    return buf.str();
}

string WebServer::getMimeType(const string &path) {
    if (path.ends_with(".html"))   return "text/html";
    if (path.ends_with(".js"))     return "text/javascript";
    if (path.ends_with(".css"))    return "text/css";
    if (path.ends_with(".svg"))    return "image/svg+xml";
    if (path.ends_with(".png"))    return "image/png";
    return "application/octet-stream";
}

string WebServer::makeUsername() {
    return "player_" + to_string(rand() % 100);
}
