#include <../include/Logger.hpp>
#include <../include/WebServer.hpp>
#include <algorithm>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <App.h>

namespace fs = std::filesystem;
using std::ios, std::exception, std::runtime_error, std::to_string;

WebServer::WebServer(int port, string_view keyFile, string_view certFile, string_view dbFile)
    : port_(port), dbFile_(dbFile), app_(uWS::SSLApp({.key_file_name = keyFile.data(), .cert_file_name = certFile.data()})) {
    if (!initDB()) {
        throw runtime_error("Failed to initialise database");
    }
    registerRoutes();
    Logger::info("WebServer constructed");
}

WebServer::~WebServer() {
    if (db_) {
        sqlite3_close(db_);
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
    if (sqlite3_open(dbFile_.c_str(), &db_) != SQLITE_OK) {
        Logger::error("Cannot open DB: " + string(sqlite3_errmsg(db_)));
        sqlite3_close(db_);
        db_ = nullptr;
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

    char *errMsg = nullptr;
    if (sqlite3_exec(db_, schema, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::error("Schema error: " + string(errMsg));
        sqlite3_free(errMsg);
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    Logger::info("Database ready (" + dbFile_ + ")");
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
                    res->writeStatus("422 Unprocessable Entity")->end("Room field is empty or too long.");
                }
                
                if (!std::all_of(topic.begin(), topic.end(), [](char c) {
                    return std::isalnum(c) || c == '-' || c == '_';
                })) {
                    res->writeStatus("422 Unprocessable Entity")->end("Room field includes illegal characters.");
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
  sqlite3_stmt *stmt = nullptr;

  sqlite3_prepare_v2( db_, "INSERT OR IGNORE INTO rooms (topic, clicks) VALUES (?, 0);", -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, topic.c_str(), -1, SQLITE_STATIC);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void WebServer::incrementClicks(const string &topic) {
  sqlite3_stmt *stmt = nullptr;

  sqlite3_prepare_v2(db_, "UPDATE rooms SET clicks = clicks + 1 WHERE topic = ?;", -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, topic.c_str(), -1, SQLITE_STATIC);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void WebServer::setLastClicker(const string &topic, const string &username) {
    sqlite3_stmt *stmt = nullptr;

    sqlite3_prepare_v2(db_, "UPDATE rooms SET last_clicker = ? WHERE topic = ?;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, topic.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

int WebServer::getClicks(const string &topic) {
  sqlite3_stmt *stmt = nullptr;
  int clicks = 0;

  sqlite3_prepare_v2(db_, "SELECT clicks FROM rooms WHERE topic = ?;", -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, topic.c_str(), -1, SQLITE_STATIC);
  if (sqlite3_step(stmt) == SQLITE_ROW)
    clicks = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  return clicks;
}

string WebServer::getLastClicker(const string &topic) {
    sqlite3_stmt *stmt = nullptr;
    string last_clicker;

    sqlite3_prepare_v2(db_, "SELECT last_clicker FROM rooms WHERE topic = ?;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, topic.c_str(), -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* raw = sqlite3_column_text(stmt, 0);
        last_clicker = raw ? reinterpret_cast<const char*>(raw) : "nessuno";
    }
    sqlite3_finalize(stmt);
    return last_clicker;
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
