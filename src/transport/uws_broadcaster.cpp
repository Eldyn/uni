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

bool UwsBroadcaster::Send(AppWebSocket* socket, const std::string& msg,
                           uWS::OpCode op, bool compress, bool droppable) {
    if (socket == nullptr) return false;
    // INFO: backpressure guard — applies to droppable (superseded-by-next
    //       snapshot) frames only. A socket already buffering past the
    //       threshold is not keeping up; dropping this snapshot keeps the
    //       connection alive instead of tripping uWS's
    //       closeOnBackpressureLimit disconnect, and the next broadcast
    //       supersedes it. Non-droppable frames (events, barrier, defs,
    //       match_start, abort, DM, errors, match_over) always queue with
    //       pre-Task-9 semantics: they must never be lost to a silent gap.
    //       The false return lets one-shot callers (match_over) retry
    //       instead of losing the frame; the counter + periodic warn make a
    //       burst visible so the threshold can be tuned, not silent.
    if (droppable && send_skip_bytes_ > 0 &&
        socket->getBufferedAmount() > send_skip_bytes_) {
        const unsigned long long drops = ++g_send_drops;
        if (drops == 1 || drops % 256 == 0) {
            Logger::Warn("[WS] send skipped: backpressure guard dropped " +
                         std::to_string(drops) + " frame(s)");
        }
        return false;
    }
    socket->send(msg, op, compress);
    return true;
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
