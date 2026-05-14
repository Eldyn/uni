#include "../include/webserver.hpp"
#include "../include/logger.hpp"

int main() {
    try {
        WebServer server(9999);

        server.GetHTTPRouter().OnAny([](AppResponse *response, AppRequest *request) {
            Logger::Log("[HTTP]: route received: ", std::string(request->getFullUrl()));
            return true;
        });

        server.GetActionRouter().OnAny([](WsContext ctx, const json& msg) -> bool {
            Logger::Log("[WS]: action received from: ", ctx.socket_data->username, " → action=", msg.value("action", "?"));
            return true;
        });
 
        server.Run();
    } catch (const std::exception& e) {
        Logger::Error(string("Fatal: ") + e.what());
        return 1;
    }

    return 0;
}
