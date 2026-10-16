#include <services/friend_service.hpp>
#include <nlohmann/json.hpp>

namespace {

// Parses the single TEXT cell our json_group_array queries return into a
// string vector. Throws on corrupt input; callers convert that into
// Error::DatabaseFail.
std::vector<std::string> ParseJsonStringArray(const DbRow& row) {
    nlohmann::json arr = nlohmann::json::parse(row.GetOr<std::string>("j", "[]"));
    std::vector<std::string> out;
    out.reserve(arr.size());
    for (const auto& e : arr) {
        out.push_back(e.get<std::string>());
    }
    return out;
}

}  // namespace

FriendService::FriendService(Database& db) : db_(db) {}

VoidResult FriendService::SendRequest(const std::string& from, const std::string& to) {
    if (from == to) {
        return std::unexpected(Error::BadRequest("Cannot send a friend request to yourself"));
    }

    auto existing = db_.QueryOne(
        "SELECT status FROM friends WHERE (user_a = ? AND user_b = ?) "
        "OR (user_a = ? AND user_b = ?);",
        {from, to, to, from});
    if (!existing) {
        return std::unexpected(existing.error());
    }
    if (existing->has_value()) {
        return std::unexpected(Error::Conflict("A request or friendship already exists"));
    }

    auto insert = db_.Exec(
        "INSERT INTO friends (user_a, user_b, status) VALUES (?, ?, 'pending');",
        {from, to});
    if (!insert) {
        return std::unexpected(insert.error());
    }
    return {};
}

VoidResult FriendService::RespondToRequest(const std::string& responder,
                                           const std::string& requester, bool accept) {
    auto existing = db_.QueryOne(
        "SELECT status FROM friends WHERE user_a = ? AND user_b = ? AND status = 'pending';",
        {requester, responder});
    if (!existing) {
        return std::unexpected(existing.error());
    }
    if (!existing->has_value()) {
        return std::unexpected(Error::NotFound("No pending request from that user"));
    }

    if (accept) {
        auto update = db_.Exec(
            "UPDATE friends SET status = 'accepted' WHERE user_a = ? AND user_b = ?;",
            {requester, responder});
        if (!update) {
            return std::unexpected(update.error());
        }
    } else {
        auto remove = db_.Exec(
            "DELETE FROM friends WHERE user_a = ? AND user_b = ?;",
            {requester, responder});
        if (!remove) {
            return std::unexpected(remove.error());
        }
    }
    return {};
}

VoidResult FriendService::RemoveFriend(const std::string& user, const std::string& other) {
    auto existing = db_.QueryOne(
        "SELECT status FROM friends WHERE status = 'accepted' AND "
        "((user_a = ? AND user_b = ?) OR (user_a = ? AND user_b = ?));",
        {user, other, other, user});
    if (!existing) {
        return std::unexpected(existing.error());
    }
    if (!existing->has_value()) {
        return std::unexpected(Error::NotFound("No friendship exists with that user"));
    }

    auto remove = db_.Exec(
        "DELETE FROM friends WHERE status = 'accepted' AND "
        "((user_a = ? AND user_b = ?) OR (user_a = ? AND user_b = ?));",
        {user, other, other, user});
    if (!remove) {
        return std::unexpected(remove.error());
    }
    return {};
}

Result<std::vector<std::string>> FriendService::GetFriends(const std::string& user) {
    auto row = db_.QueryOne(
        "SELECT COALESCE(json_group_array(friend_username), '[]') AS j FROM "
        "(SELECT CASE WHEN user_a = ? THEN user_b ELSE user_a END AS friend_username "
        "FROM friends WHERE status = 'accepted' AND (user_a = ? OR user_b = ?));",
        {user, user, user});
    if (!row) {
        return std::unexpected(row.error());
    }
    if (!row->has_value()) {
        return std::vector<std::string>{};
    }
    try {
        return ParseJsonStringArray(row->value());
    } catch (const std::exception& e) {
        return std::unexpected(Error::DatabaseFail(e.what()));
    }
}

Result<std::vector<std::string>> FriendService::GetIncomingRequests(const std::string& user) {
    auto row = db_.QueryOne(
        "SELECT COALESCE(json_group_array(user_a), '[]') AS j FROM "
        "(SELECT user_a FROM friends WHERE status = 'pending' AND user_b = ?);",
        {user});
    if (!row) {
        return std::unexpected(row.error());
    }
    if (!row->has_value()) {
        return std::vector<std::string>{};
    }
    try {
        return ParseJsonStringArray(row->value());
    } catch (const std::exception& e) {
        return std::unexpected(Error::DatabaseFail(e.what()));
    }
}

Result<std::vector<std::string>> FriendService::GetOutgoingRequests(const std::string& user) {
    auto row = db_.QueryOne(
        "SELECT COALESCE(json_group_array(user_b), '[]') AS j FROM "
        "(SELECT user_b FROM friends WHERE status = 'pending' AND user_a = ?);",
        {user});
    if (!row) {
        return std::unexpected(row.error());
    }
    if (!row->has_value()) {
        return std::vector<std::string>{};
    }
    try {
        return ParseJsonStringArray(row->value());
    } catch (const std::exception& e) {
        return std::unexpected(Error::DatabaseFail(e.what()));
    }
}

Result<FriendLists> FriendService::GetFriendLists(const std::string& user) {
    // INFO: One round trip for all three views: a single `json_object` row
    // with three arrays. Each scalar subquery COALESCEs so an empty view is
    // `[]`, never null.
    auto row = db_.QueryOne(
        "SELECT json_object("
        "'friends', (SELECT COALESCE(json_group_array(friend_username), '[]') FROM "
        "(SELECT CASE WHEN user_a = ? THEN user_b ELSE user_a END AS friend_username "
        "FROM friends WHERE status = 'accepted' AND (user_a = ? OR user_b = ?))),"
        "'incoming', (SELECT COALESCE(json_group_array(user_a), '[]') FROM "
        "(SELECT user_a FROM friends WHERE status = 'pending' AND user_b = ?)),"
        "'outgoing', (SELECT COALESCE(json_group_array(user_b), '[]') FROM "
        "(SELECT user_b FROM friends WHERE status = 'pending' AND user_a = ?))"
        ") AS j;",
        {user, user, user, user, user});
    if (!row) {
        return std::unexpected(row.error());
    }
    if (!row->has_value()) {
        return FriendLists{};
    }
    try {
        nlohmann::json obj =
            nlohmann::json::parse(row->value().GetOr<std::string>("j", "{}"));
        FriendLists lists;
        for (const auto& e : obj.at("friends")) {
            lists.friends.push_back(e.get<std::string>());
        }
        for (const auto& e : obj.at("incoming")) {
            lists.incoming.push_back(e.get<std::string>());
        }
        for (const auto& e : obj.at("outgoing")) {
            lists.outgoing.push_back(e.get<std::string>());
        }
        return lists;
    } catch (const std::exception& e) {
        return std::unexpected(Error::DatabaseFail(e.what()));
    }
}
