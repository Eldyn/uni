#include <controllers/match_controller.hpp>
#include <controllers/lobby_controller.hpp>
#include <controllers/auth_controller.hpp>
#include <controllers/stats_controller.hpp>
#include <controllers/friend_controller.hpp>
#include <transport/presence_registry.hpp>
#include <common/env.hpp>
#include <logger.hpp>
#include <common/ws.hpp>
#include <nlohmann/json_fwd.hpp>
#include <webserver.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

int main() {
    try {
        Env::Load(".env");

        const int         port          = std::stoi(Env::Get("PORT", "9999"));
        const std::string db_path       = Env::Get("DB_PATH",       "uni.sqlite");
        const std::string frontend_path = Env::Get("FRONTEND_PATH", "public");
        const std::string ssl_cert      = Env::Get("SSL_CERT_PATH", "cert.pem");
        const std::string ssl_key       = Env::Get("SSL_KEY_PATH",  "key.pem");

        WebServer server(port, ssl_key, ssl_cert, db_path, frontend_path);

        AuthController   auth(server.GetHTTPRouter());
        PresenceRegistry presence;
        LobbyController  lobby(server.GetActionRouter(), server.GetBroadcaster(),
                               server.GetTimerService(), presence);

        server.OnConnectionOpen([&lobby, &presence](AppWebSocket* ws, PerSocketData* sd) {
            presence.OnOpen(ws, sd);
            lobby.OnOpen(ws, sd);
        });
        server.OnConnectionClose([&lobby, &presence](AppWebSocket* ws, PerSocketData* sd) {
            presence.OnClose(ws, sd);
            lobby.OnClose(ws, sd);
        });
        server.SetActiveMatchProvider([&lobby] { return lobby.ActiveMatchCount(); });

        StatsController stats(server.GetHTTPRouter());
        MatchController game(server.GetActionRouter(), server.GetBroadcaster(),
                             server.GetTimerService(), lobby);
        FriendController friends(server.GetActionRouter(), server.GetBroadcaster(), presence);

        // INFO: Logging Middleware
        server.GetHTTPRouter().OnAny([](AppResponse *response, AppRequest *request) {
            Logger::Log("[HTTP] route received: ", std::string(request->getFullUrl()));
            return true;
        });

        server.GetActionRouter().OnAny([](WsContext ctx, const json& msg) -> bool {
            Logger::Log(
                "[WS] request received: ", ctx.socket_data->username, ".",
                ws::GetOr<std::string>(msg, "action", "?"),
                "(", ws::GetOr<std::string>(msg, "request_id", "?"), ")");
            return true;
        });

        server.Run();
    } catch (const std::exception& e) {
        Logger::Error(std::string("Fatal: "), e.what());
        return 1;
    }
}
