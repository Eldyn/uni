#include <doctest/doctest.h>
#include <string>
#include <string_view>
#include "webserver.hpp"

TEST_SUITE("inbound") {

TEST_CASE("inbound extracts action without alloc") {
    std::string msg = R"({"action":"chat_send","request_id":"r1","body":"hi"})";
    auto in = ParseInbound(std::string_view(msg));
    CHECK(in.action == "chat_send");
    CHECK(in.request_id == "r1");
    CHECK(msg.data() <= in.action.data()); // view into original buffer
    CHECK(in.action.data() + in.action.size() <= msg.data() + msg.size());
    CHECK(msg.data() <= in.request_id.data());
    CHECK(in.request_id.data() + in.request_id.size() <= msg.data() + msg.size());
}

TEST_CASE("inbound tolerates whitespace and field order") {
    std::string msg = R"({ "request_id" : "abc-123" , "action" : "ping" })";
    auto in = ParseInbound(std::string_view(msg));
    CHECK(in.action == "ping");
    CHECK(in.request_id == "abc-123");
}

TEST_CASE("inbound empty action on malformed frames") {
    for (const char* raw :
         {"", "not json", "5", "[]", "{}", R"({"action":123})",
          R"({"action":null})", R"({"request_id":"r1"})", R"({"action":""})",
          R"({"action":"unterminated)"}) {
        std::string msg(raw);
        auto in = ParseInbound(std::string_view(msg));
        CHECK(in.action.empty());
    }
}

TEST_CASE("inbound missing request_id yields empty view, action intact") {
    std::string msg = R"({"action":"ping"})";
    auto in = ParseInbound(std::string_view(msg));
    CHECK(in.action == "ping");
    CHECK(in.request_id.empty());
}

TEST_CASE("inbound ignores key-like text inside string values") {
    std::string msg = R"({"body":"say \"action\": loudly","action":"chat_send"})";
    auto in = ParseInbound(std::string_view(msg));
    CHECK(in.action == "chat_send");
}

TEST_CASE("ActionHash is FNV-1a over the action bytes") {
    CHECK(Fnv1a("") == 2166136261u);
    CHECK(ActionHash("chat_send") == Fnv1a("chat_send"));
    CHECK(ActionHash("chat_send") == Fnv1a(std::string_view("chat_send")));
    CHECK(ActionHash("ping") != ActionHash("pong"));
}

}  // TEST_SUITE
