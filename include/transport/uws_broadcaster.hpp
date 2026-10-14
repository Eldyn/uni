#pragma once
#include <transport/ibroadcaster.hpp>

/**
 * @class UwsBroadcaster
 * @brief Production IBroadcaster backed by a live uWS application instance.
 *
 * Wraps AppHttp& and forwards every call to the uWS send/publish/subscribe
 * primitives. The `compress` flag is forwarded to uWS's third argument so
 * permessage-deflate (when negotiated) only runs on frames that request it.
 *
 * Backpressure guard (backend-perf Task 9): `Send` drops the frame when the
 * socket's buffered amount already exceeds `send_skip_bytes_` (env
 * `WS_SEND_SKIP_BYTES`, default 512 KiB) instead of queueing into
 * `closeOnBackpressureLimit`'s silent disconnect at 1 MiB. Snapshots are
 * lossy by design (the next broadcast supersedes a dropped one); the drop
 * is counted and logged so a sustained burst stays visible.
 */
class UwsBroadcaster : public IBroadcaster {
public:
    explicit UwsBroadcaster(AppHttp& app);

    void Send(AppWebSocket* socket, const std::string& msg,
              uWS::OpCode op = uWS::OpCode::TEXT, bool compress = true) override;

    void Publish(const std::string& topic, const std::string& msg,
                 uWS::OpCode op = uWS::OpCode::TEXT, bool compress = true) override;

    void Subscribe(AppWebSocket* socket, const std::string& topic) override;

    void Unsubscribe(AppWebSocket* socket, const std::string& topic) override;

private:
    /** @brief Env-tunable drop threshold, read once at construction. */
    static unsigned SendSkipBytesFromEnv();

    AppHttp& app_;
    unsigned send_skip_bytes_;
};
