#include <common/env.hpp>
#include <common/http.hpp>
#include <common/ws.hpp>
#include <common/email_queue.hpp>
#include <common/email_sender.hpp>
#include <controllers/auth_controller.hpp>
#include <controllers/chat_controller.hpp>
#include <controllers/friend_controller.hpp>
#include <controllers/lobby_controller.hpp>
#include <controllers/match_controller.hpp>
#include <controllers/stats_controller.hpp>
#include <logger.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <transport/presence_registry.hpp>
#include <webserver.hpp>

// Mirrors the UNI_HAS_LIBCURL switch in src/common/email_sender.cpp: only
// uni_server links libcurl (BrevoEmailSender), so curl_global_init/cleanup
// are only relevant in that binary.
#ifndef UNI_HAS_LIBCURL
#define UNI_HAS_LIBCURL 0
#endif
#if UNI_HAS_LIBCURL
#include <curl/curl.h>
#endif

using json = nlohmann::json;

int main() {
#if UNI_HAS_LIBCURL
    // curl_global_init/cleanup are not thread-safe against concurrent calls
    // and must run exactly once per process, before any thread may call into
    // libcurl (BrevoEmailSender::Send runs on an EmailQueue worker thread).
    // Doing this here, once, at startup — rather than lazily inside Send()
    // itself — avoids re-introducing that exact race on every call.
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
    try {
        Env::Load(".env");

        const int         port          = std::stoi(Env::Get("PORT", "9999"));
        const std::string db_path       = Env::Get("DB_PATH", "uni.sqlite");
        const std::string frontend_path = Env::Get("FRONTEND_PATH", "public");
        const std::string ssl_cert      = Env::Get("SSL_CERT_PATH", "cert.pem");
        const std::string ssl_key       = Env::Get("SSL_KEY_PATH", "key.pem");

        EmailQueue       email_queue(MakeEmailSender());
        WebServer server(port, ssl_key, ssl_cert, db_path, frontend_path, &email_queue);
        AuthController   auth(server.GetHTTPRouter(), email_queue);
        PresenceRegistry presence;
        LobbyController  lobby(server.GetActionRouter(), server.GetBroadcaster(),
                               server.GetTimerService(), presence,
                               &server.GetHTTPRouter());
        ChatController   chat(server.GetActionRouter(), server.GetBroadcaster(), presence, -1,
                              ChatController::kUnsetHistoryLimit, &lobby);

        server.OnConnectionOpen([&lobby, &presence, &chat](AppWebSocket* ws, PerSocketData* sd) {
            Logger::Info("[WS] connection opened: user=", sd->username, " ip=", sd->ip);
            presence.OnOpen(ws, sd);
            lobby.OnOpen(ws, sd);
            chat.OnOpen(ws, sd);
        });
        server.OnConnectionClose([&lobby, &presence](AppWebSocket* ws, PerSocketData* sd) {
            Logger::Info("[WS] connection closed: user=", sd->username, " ip=", sd->ip);
            presence.OnClose(ws, sd);
            lobby.OnClose(ws, sd);
        });
        server.SetActiveMatchProvider([&lobby] { return lobby.ActiveMatchCount(); });

        StatsController  stats(server.GetHTTPRouter());
        MatchController  game(server.GetActionRouter(), server.GetBroadcaster(),
                              server.GetTimerService(), lobby);
        FriendController friends(server.GetActionRouter(), server.GetBroadcaster(), presence);

        // INFO: Logging Middleware (Debug-level: enable with LOG_LEVEL=debug)
        const bool trust_proxy = (Env::Get("TRUST_PROXY", "0") != "0");
        server.GetHTTPRouter().OnAny([trust_proxy](AppResponse* response, AppRequest* request) {
            Logger::HTTP("route received: ", std::string(request->getFullUrl()),
                         " ip=", http::GetClientIp(response, request, trust_proxy));
            return true;
        });

        server.GetActionRouter().OnAny([](WsContext ctx, const json& msg) -> bool {
            Logger::WS("request received: ", ctx.socket_data->username, ".",
                       ws::GetOr<std::string>(msg, "action", "?"), "(",
                       ws::GetOr<std::string>(msg, "request_id", "?"), ") ip=",
                       ctx.socket_data->ip);
            return true;
        });

        server.Run();
#if UNI_HAS_LIBCURL
        curl_global_cleanup();
#endif
    } catch (const std::exception& e) {
        Logger::Error(std::string("Fatal: "), e.what());
#if UNI_HAS_LIBCURL
        curl_global_cleanup();
#endif
        return 1;
    }
}
