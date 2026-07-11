#pragma once
#include <transport/ipresence_store.hpp>
#include <unordered_map>
#include <string>
#include <vector>

/**
 * @class FakePresenceStore
 * @brief Test double for IPresenceStore.
 *
 * Sockets are registered/removed explicitly by the test so controllers under
 * test can look up "who's online" without a live PresenceRegistry.
 */
class FakePresenceStore : public IPresenceStore {
public:
    std::unordered_map<std::string, AppWebSocket*> online;

    bool IsOnline(const std::string& username) const override {
        return online.find(username) != online.end();
    }

    AppWebSocket* GetSocket(const std::string& username) const override {
        auto it = online.find(username);
        return it == online.end() ? nullptr : it->second;
    }

    std::vector<std::string> OnlineUsernames() const override {
        std::vector<std::string> out;
        out.reserve(online.size());
        for (const auto& [username, socket] : online) out.push_back(username);
        return out;
    }
};
