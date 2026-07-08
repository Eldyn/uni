#include <doctest/doctest.h>
#include <action_router.hpp>
#include <websocket_context.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// AppWebSocket* as an opaque test key — never dereferenced by ActionRouter.
static AppWebSocket* fake_sock(PerSocketData& sd) {
    return reinterpret_cast<AppWebSocket*>(&sd);
}

static WsContext make_ctx(AppWebSocket* sock, PerSocketData* sd) {
    return WsContext{sock, sd, uWS::OpCode::TEXT};
}

struct RouterFixture {
    ActionRouter router;
    PerSocketData sd;
    AppWebSocket* sock;

    RouterFixture() : sock(fake_sock(sd)) {}

    WsContext ctx() { return make_ctx(sock, &sd); }
};

TEST_SUITE("ActionRouter") {

TEST_CASE("On: registered handler is invoked with its own ctx and msg") {
    RouterFixture f;
    bool called = false;
    WsContext seen_ctx{};
    json seen_msg;

    f.router.On("ping", [&](WsContext ctx, const json& msg) {
        called    = true;
        seen_ctx  = ctx;
        seen_msg  = msg;
        return true;
    });

    json msg = {{"action", "ping"}, {"value", 42}};
    f.router.Dispatch(f.ctx(), msg);

    CHECK(called);
    CHECK(seen_ctx.socket == f.sock);
    CHECK(seen_ctx.socket_data == &f.sd);
    CHECK(seen_msg.value("value", 0) == 42);
}

TEST_CASE("Dispatch: returns true when handler exists, regardless of handler's own return value") {
    RouterFixture f;
    f.router.On("returns_true", [](WsContext, const json&) { return true; });
    f.router.On("returns_false", [](WsContext, const json&) { return false; });

    CHECK(f.router.Dispatch(f.ctx(), {{"action", "returns_true"}}) == true);
    // Dispatch discards the specific handler's own return value and reports
    // true simply because a handler was found and invoked.
    CHECK(f.router.Dispatch(f.ctx(), {{"action", "returns_false"}}) == true);
}

TEST_CASE("Dispatch: unknown action returns false without invoking any handler") {
    RouterFixture f;
    bool called = false;
    f.router.On("known", [&](WsContext, const json&) {
        called = true;
        return true;
    });

    bool result = f.router.Dispatch(f.ctx(), {{"action", "unknown"}});

    CHECK_FALSE(result);
    CHECK_FALSE(called);
}

TEST_CASE("Dispatch: missing action field is treated as empty-string action and returns false") {
    RouterFixture f;
    bool result = f.router.Dispatch(f.ctx(), json::object());
    CHECK_FALSE(result);
}

TEST_CASE("OnAny: wildcard runs before the specific handler") {
    RouterFixture f;
    std::vector<std::string> order;

    f.router.OnAny([&](WsContext, const json&) {
        order.push_back("wildcard");
        return true;
    });
    f.router.On("ping", [&](WsContext, const json&) {
        order.push_back("specific");
        return true;
    });

    f.router.Dispatch(f.ctx(), {{"action", "ping"}});

    REQUIRE(order.size() == 2);
    CHECK(order[0] == "wildcard");
    CHECK(order[1] == "specific");
}

TEST_CASE("OnAny: multiple wildcards run in registration order") {
    RouterFixture f;
    std::vector<int> order;

    f.router.OnAny([&](WsContext, const json&) {
        order.push_back(1);
        return true;
    });
    f.router.OnAny([&](WsContext, const json&) {
        order.push_back(2);
        return true;
    });
    f.router.OnAny([&](WsContext, const json&) {
        order.push_back(3);
        return true;
    });

    f.router.Dispatch(f.ctx(), {{"action", "ping"}});

    REQUIRE(order.size() == 3);
    CHECK(order[0] == 1);
    CHECK(order[1] == 2);
    CHECK(order[2] == 3);
}

TEST_CASE("OnAny: a wildcard returning false aborts the chain before the specific handler") {
    RouterFixture f;
    bool specific_called = false;

    f.router.OnAny([](WsContext, const json&) { return false; });
    f.router.On("ping", [&](WsContext, const json&) {
        specific_called = true;
        return true;
    });

    bool result = f.router.Dispatch(f.ctx(), {{"action", "ping"}});

    CHECK_FALSE(result);
    CHECK_FALSE(specific_called);
}

TEST_CASE("OnAny: a later wildcard is never reached once an earlier one returns false") {
    RouterFixture f;
    bool second_wildcard_called = false;

    f.router.OnAny([](WsContext, const json&) { return false; });
    f.router.OnAny([&](WsContext, const json&) {
        second_wildcard_called = true;
        return true;
    });

    f.router.Dispatch(f.ctx(), {{"action", "ping"}});

    CHECK_FALSE(second_wildcard_called);
}

TEST_CASE("On: registering a second handler for the same action silently replaces the first") {
    RouterFixture f;
    bool first_called  = false;
    bool second_called = false;

    f.router.On("ping", [&](WsContext, const json&) {
        first_called = true;
        return true;
    });
    f.router.On("ping", [&](WsContext, const json&) {
        second_called = true;
        return true;
    });

    f.router.Dispatch(f.ctx(), {{"action", "ping"}});

    CHECK_FALSE(first_called);
    CHECK(second_called);
}

TEST_CASE("On/OnAny: return *this so calls can be chained") {
    RouterFixture f;
    ActionRouter& r1 = f.router.On("a", [](WsContext, const json&) { return true; });
    ActionRouter& r2 = f.router.OnAny([](WsContext, const json&) { return true; });

    CHECK(&r1 == &f.router);
    CHECK(&r2 == &f.router);
}

} // TEST_SUITE
