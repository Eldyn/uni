#pragma once
#include <transport/ibroadcaster.hpp>

/**
 * @class UwsBroadcaster
 * @brief Production IBroadcaster backed by a live uWS application instance.
 *
 * Wraps AppHttp& and forwards every call to the uWS send/publish/subscribe
 * primitives. The `compress` flag is forwarded to uWS's third argument so
 * permessage-deflate (when negotiated) only runs on frames that request it.
 */
class UwsBroadcaster : public IBroadcaster {
public:
    explicit UwsBroadcaster(AppHttp& app) : app_(app) {}

    void Send(AppWebSocket* socket, const std::string& msg,
              uWS::OpCode op = uWS::OpCode::TEXT, bool compress = true) override;

    void Publish(const std::string& topic, const std::string& msg,
                 uWS::OpCode op = uWS::OpCode::TEXT, bool compress = true) override;

    void Subscribe(AppWebSocket* socket, const std::string& topic) override;

    void Unsubscribe(AppWebSocket* socket, const std::string& topic) override;

private:
    AppHttp& app_;
};
