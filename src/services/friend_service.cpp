#include <services/friend_service.hpp>

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
    auto rows = db_.Query(
        "SELECT CASE WHEN user_a = ? THEN user_b ELSE user_a END AS friend_username "
        "FROM friends WHERE status = 'accepted' AND (user_a = ? OR user_b = ?);",
        {user, user, user});
    if (!rows) {
        return std::unexpected(rows.error());
    }

    std::vector<std::string> friends;
    friends.reserve(rows->size());
    for (const auto& row : rows.value()) {
        friends.push_back(row.Get<std::string>("friend_username"));
    }
    return friends;
}

Result<std::vector<std::string>> FriendService::GetIncomingRequests(const std::string& user) {
    auto rows = db_.Query(
        "SELECT user_a FROM friends WHERE status = 'pending' AND user_b = ?;",
        {user});
    if (!rows) {
        return std::unexpected(rows.error());
    }

    std::vector<std::string> requesters;
    requesters.reserve(rows->size());
    for (const auto& row : rows.value()) {
        requesters.push_back(row.Get<std::string>("user_a"));
    }
    return requesters;
}

Result<std::vector<std::string>> FriendService::GetOutgoingRequests(const std::string& user) {
    auto rows = db_.Query(
        "SELECT user_b FROM friends WHERE status = 'pending' AND user_a = ?;",
        {user});
    if (!rows) {
        return std::unexpected(rows.error());
    }

    std::vector<std::string> targets;
    targets.reserve(rows->size());
    for (const auto& row : rows.value()) {
        targets.push_back(row.Get<std::string>("user_b"));
    }
    return targets;
}
