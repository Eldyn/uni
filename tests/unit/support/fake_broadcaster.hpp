#pragma once
#include <transport/ibroadcaster.hpp>
#include <string>
#include <vector>
#include <set>
#include <utility>

/**
 * @struct SentFrame
 * @brief Records a single Send() call for test assertions.
 */
struct SentFrame {
    AppWebSocket*  to;
    std::string    payload;
    uWS::OpCode    op;
};

/**
 * @class FakeBroadcaster
 * @brief Test double for IBroadcaster.
 *
 * Stores all Send/Publish/Subscribe/Unsubscribe calls in plain vectors
 * so tests can assert on what was sent without touching a live socket.
 * AppWebSocket* values are treated as opaque keys and never dereferenced.
 */
class FakeBroadcaster : public IBroadcaster {
public:
    std::vector<SentFrame>                              sent;
    std::vector<std::pair<std::string, std::string>>    published; // {topic, payload}
    std::set<std::pair<AppWebSocket*, std::string>>     subscriptions;

    bool Send(AppWebSocket* socket, const std::string& msg,
              uWS::OpCode op = uWS::OpCode::TEXT, bool = true,
              bool = false) override {
        sent.push_back({socket, msg, op});
        return true;
    }

    void Publish(const std::string& topic, const std::string& msg,
                 uWS::OpCode = uWS::OpCode::TEXT, bool = true) override {
        published.push_back({topic, msg});
    }

    void Subscribe(AppWebSocket* socket, const std::string& topic) override {
        subscriptions.insert({socket, topic});
    }

    void Unsubscribe(AppWebSocket* socket, const std::string& topic) override {
        subscriptions.erase({socket, topic});
    }

    /** @brief Return all frames sent to a specific socket. */
    std::vector<SentFrame> FramesFor(AppWebSocket* socket) const {
        std::vector<SentFrame> out;
        for (const auto& f : sent)
            if (f.to == socket) out.push_back(f);
        return out;
    }

    void Clear() {
        sent.clear();
        published.clear();
    }
};
