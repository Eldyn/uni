#pragma once
#include <string>
#include <vector>
#include <result.hpp>
#include <database.hpp>

/**
 * @file friend_service.hpp
 * @brief Domain layer for friend requests and friendships.
 */

/**
 * @class FriendService
 * @brief Owns the friend-request/accept/remove rules over the `friends` table,
 * independent of the WebSocket wire layer.
 * @tag SVC-FRIEND-CLS-001
 */
class FriendService {
public:
    /**
     * @param db Database to read/write friend rows from.
     * @tag SVC-FRIEND-MTH-001
     */
    explicit FriendService(Database& db = Database::Get());

    /**
     * @brief Sends a friend request from one user to another.
     * Fails with kBadRequest if `from == to`, kConflict if a pending request
     * or friendship already exists between the pair in either direction.
     * @tag SVC-FRIEND-MTH-002
     */
    VoidResult SendRequest(const std::string& from, const std::string& to);

    /**
     * @brief Accepts or declines a pending request addressed to `responder`.
     * Accepting flips the row to status "accepted"; declining deletes it.
     * Fails with kNotFound if no pending request from `requester` to
     * `responder` exists.
     * @tag SVC-FRIEND-MTH-003
     */
    VoidResult RespondToRequest(const std::string& responder, const std::string& requester,
                                bool accept);

    /**
     * @brief Removes an accepted friendship between the two users.
     * Fails with kNotFound if no accepted friendship exists between the pair.
     * @tag SVC-FRIEND-MTH-004
     */
    VoidResult RemoveFriend(const std::string& user, const std::string& other);

    /**
     * @brief Lists the usernames of every accepted friend of `user`.
     * @tag SVC-FRIEND-MTH-005
     */
    Result<std::vector<std::string>> GetFriends(const std::string& user);

    /**
     * @brief Lists the usernames of users who sent `user` a pending request.
     * @tag SVC-FRIEND-MTH-006
     */
    Result<std::vector<std::string>> GetIncomingRequests(const std::string& user);

    /**
     * @brief Lists the usernames `user` has sent a still-pending request to.
     * @tag SVC-FRIEND-MTH-007
     */
    Result<std::vector<std::string>> GetOutgoingRequests(const std::string& user);

private:
    Database& db_;
};
