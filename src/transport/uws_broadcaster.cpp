#include <transport/uws_broadcaster.hpp>
#include <common/env.hpp>
#include <logger.hpp>

#include <atomic>
#include <string>

namespace {

// Dropped-frame counter (see Send): logged on first drop and every 256th
// after, so a sustained burst stays visible without logging every frame.
std::atomic<unsigned long long> g_send_drops{0};

}  // namespace

unsigned UwsBroadcaster::SendSkipBytesFromEnv() {
    const std::string raw = Env::Get("WS_SEND_SKIP_BYTES", "524288");
    try {
        const unsigned long parsed = std::stoul(raw);
        return parsed > 0 ? static_cast<unsigned>(parsed) : 524288u;
    } catch (...) {
        return 524288u;
    }
}

UwsBroadcaster::UwsBroadcaster(AppHttp& app)
    : app_(app), send_skip_bytes_(SendSkipBytesFromEnv()) {}

void UwsBroadcaster::Send(AppWebSocket* socket, const std::string& msg,
                           uWS::OpCode op, bool compress) {
    if (socket == nullptr) return;
    // INFO: backpressure guard — a socket already buffering past the
    //       threshold is not keeping up; dropping this (superseded-by-next)
    //       frame keeps the connection alive instead of tripping uWS's
    //       closeOnBackpressureLimit disconnect. Critical one-shot frames
    //       (match_over) share this path; the counter + periodic warn make a
    //       burst visible so the threshold can be tuned, not silent.
    if (send_skip_bytes_ > 0 &&
        socket->getBufferedAmount() > send_skip_bytes_) {
        const unsigned long long drops = ++g_send_drops;
        if (drops == 1 || drops % 256 == 0) {
            Logger::Warn("[WS] send skipped: backpressure guard dropped " +
                         std::to_string(drops) + " frame(s)");
        }
        return;
    }
    socket->send(msg, op, compress);
}

void UwsBroadcaster::Publish(const std::string& topic, const std::string& msg,
                              uWS::OpCode op, bool compress) {
    app_.publish(topic, msg, op, compress);
}

void UwsBroadcaster::Subscribe(AppWebSocket* socket, const std::string& topic) {
    socket->subscribe(topic);
}

void UwsBroadcaster::Unsubscribe(AppWebSocket* socket, const std::string& topic) {
    socket->unsubscribe(topic);
}
