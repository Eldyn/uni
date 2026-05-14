#pragma once
#include <map>
#include <string>
#include <string_view>
#include <fstream>
#include <sstream>
#include <database.hpp>
#include <nlohmann/json.hpp>
#include <App.h>
#include <unordered_map>
#include <websocket_context.hpp>
#include <action_router.hpp>
#include <http_router.hpp>
#include <websocket_context.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;
using std::unordered_map, std::map, std::string, std::string_view, std::ifstream, std::stringstream, std::ios, std::exception, std::runtime_error, std::to_string, std::move, std::all_of, std::isalnum;

class WebServer {
public:
    explicit WebServer(int port,
                       string_view keyFile  = "key.pem",
                       string_view certFile = "cert.pem",
                       string_view dbFile   = "game.db");
    ~WebServer();

    // Non-copyable — owns a db handle and a uWS app
    WebServer(const WebServer&)            = delete;
    WebServer& operator=(const WebServer&) = delete;

    void Run();

    ActionRouter& GetActionRouter()   { return ws_router_;   }
    HttpRouter&   GetHTTPRouter()     { return http_router_; }

private:
    int    port_;
    string db_file_;

    uWS::SSLApp app_;
    std::map<string, AppWebSocket*> connections_;

    ActionRouter ws_router_;
    HttpRouter   http_router_;

    bool InitDB();
    void RegisterRoutes();

    void HandlePost(AppResponse*, AppRequest*);
    void HandleGet (AppResponse*, AppRequest*);

    void OnSocketOpen   (AppWebSocket*);
    void OnSocketMessage(AppWebSocket*, string_view, uWS::OpCode);
    void OnSocketClosed (AppWebSocket*);

    void   EnsureRoom(const string& topic);

    void   IncrementClicks(const string& topic);
    int    GetClicks      (const string& topic);

    string GetLastClicker (const string& topic);
    void   SetLastClicker (const string& topic, const string& username);


    static string     ReadFile    (string_view path);
    static string     GetMimeType (const string& path);
    static string     MakeUsername();
};
