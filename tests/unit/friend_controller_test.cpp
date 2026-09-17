#include <doctest/doctest.h>
#include <controllers/friend_controller.hpp>
#include <services/auth_service.hpp>
#include <database.hpp>
#include <common/payloads.hpp>
#include <common/ws.hpp>
#include <action_router.hpp>
#include "support/fake_broadcaster.hpp"
#include "support/fake_presence_store.hpp"

using json = nlohmann::json;

namespace {
struct FriendFixture {
    FriendFixture() {
        REQUIRE(Database::Get().RunMigrations().has_value());
        Database::Get().Exec("DELETE FROM users WHERE username LIKE 'friend_test_%';");
    }
    ~FriendFixture() {
        Database::Get().Exec("DELETE FROM users WHERE username LIKE 'friend_test_%';");
    }
};
}

TEST_SUITE("FriendController") {
    TEST_CASE("Unverified account cannot send friend requests") {
        FriendFixture f;
        AuthService auth;
        auth.Register("friend_test_unver", "fu@example.com", "password123");
        
        ActionRouter router;
        FakeBroadcaster broadcast;
        FakePresenceStore presence;
        FriendController controller(router, broadcast, presence);
        
        WsContext ctx;
        ctx.socket = nullptr;
        ctx.op_code = uWS::OpCode::TEXT;
        ctx.socket_data = new PerSocketData{"friend_test_unver"};
        
        json msg = {
            {"action", ws::ClientAction::kFriendRequest},
            {"payload", {{"username", "somebody"}}}
        };
        
        router.Dispatch(ctx, msg);
        
        REQUIRE(!broadcast.sent.empty());
        auto resp = json::parse(broadcast.sent.front().payload);
        CHECK(resp["code"] == contract::kErrorCodeStr.at(contract::ErrorCode::kFriendRequestInvalid));
        
        delete ctx.socket_data;
    }
}
