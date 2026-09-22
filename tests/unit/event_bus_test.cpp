#include <doctest/doctest.h>

#include <match/ecs/event_bus.hpp>
#include <match/ecs/hooks.hpp>

#include <common/env.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

using match::ecs::EventBus;
using match::ecs::HookDispatchResult;
using match::ecs::HookId;
using match::ecs::HookPayload;
using match::ecs::HookPhase;

namespace {

/**
 * INFO: scoped environment override; the bus reads its guards at construction
 *       so restoring at scope exit keeps the suite order-independent.
 */
struct EnvGuard {
    std::string key;
    std::string old;
    bool had = false;

    EnvGuard(std::string k, std::string value) : key(std::move(k)) {
        const char* current = std::getenv(key.c_str());
        if (current != nullptr) {
            had = true;
            old = current;
        }
        Env::SetEnv(key, value);
    }

    ~EnvGuard() {
        if (had) {
            Env::SetEnv(key, old);
        } else {
            Env::SetEnv(key, "");
        }
    }
};

}  // namespace

TEST_CASE("hooks: HookId resolves through the catalog") {
    auto after = match::ecs::ResolveHookId("after:play");
    REQUIRE(after.has_value());
    CHECK(after->name == "play");
    CHECK(after->phase == HookPhase::kAfter);

    auto on_play = match::ecs::ResolveHookId("on_play");
    REQUIRE(on_play.has_value());
    CHECK(on_play->name == "play");
    CHECK(on_play->phase == HookPhase::kAfter);

    auto bare = match::ecs::ResolveHookId("draw_attempt");
    REQUIRE(bare.has_value());
    CHECK(bare->name == "draw_attempt");
    CHECK(bare->phase == HookPhase::kBefore);

    CHECK_FALSE(match::ecs::ResolveHookId("not_a_hook").has_value());
    CHECK_FALSE(match::ecs::ResolveHookId("between:play").has_value());

    CHECK(match::ecs::IsKnownHook(HookId{"play", HookPhase::kBefore}));
    CHECK(match::ecs::IsKnownHook(HookId{"play", HookPhase::kAfter}));
    CHECK_FALSE(match::ecs::IsKnownHook(HookId{"nope", HookPhase::kBefore}));
}

TEST_CASE("event_bus: order is mod-list order then registration order") {
    EventBus bus({"alpha", "beta", "gamma"});
    const HookId play{"play", HookPhase::kBefore};

    // INFO: subscribe deliberately scrambled; dispatch must not follow it.
    std::vector<std::string> order;
    bus.Subscribe("gamma", 5, play,
                  [&](HookPayload&) { order.push_back("gamma/5"); });
    bus.Subscribe("beta", 2, play,
                  [&](HookPayload&) { order.push_back("beta/2"); });
    bus.Subscribe("alpha", 1, play,
                  [&](HookPayload&) { order.push_back("alpha/1"); });
    bus.Subscribe("beta", 1, play,
                  [&](HookPayload&) { order.push_back("beta/1"); });
    bus.Subscribe("alpha", 9, play,
                  [&](HookPayload&) { order.push_back("alpha/9"); });

    HookPayload payload;
    payload.hook = play;
    HookDispatchResult result = bus.DispatchBefore(play, payload);

    CHECK(result.ran == 5);
    const std::vector<std::string> expected = {
        "alpha/1", "alpha/9", "beta/1", "beta/2", "gamma/5"};
    CHECK(order == expected);
}

TEST_CASE("event_bus: all before-hooks run and veto is read after") {
    EventBus bus({"m"});
    const HookId play{"play", HookPhase::kBefore};

    std::vector<std::string> order;
    bus.Subscribe("m", 0, play, [&](HookPayload& payload) {
        order.push_back("first");
        payload.veto = true;
    });
    bus.Subscribe("m", 1, play, [&](HookPayload& payload) {
        order.push_back("second");
        payload.data["seen"] = true;
    });

    HookPayload payload;
    payload.hook = play;
    HookDispatchResult result = bus.DispatchBefore(play, payload);

    CHECK(result.ran == 2);
    CHECK(result.vetoed);
    CHECK(payload.veto);
    CHECK(payload.data["seen"] == true);
    const std::vector<std::string> expected = {"first", "second"};
    CHECK(order == expected);

    // INFO: after-hooks never consult veto.
    HookDispatchResult after = bus.DispatchAfter(play, payload);
    CHECK(after.ran == 0);
    CHECK_FALSE(after.vetoed);
}

TEST_CASE("event_bus: re-entry cap aborts the chain through the budget path") {
    EnvGuard cap("UNI_REENTRY_CAP", "3");
    EnvGuard disarm("UNI_MOD_DISARM_THRESHOLD", "100");
    EventBus bus({"m"});
    const HookId play{"play", HookPhase::kBefore};

    int invocations = 0;
    bool abort_seen = false;
    uint32_t abort_ran = 99;
    std::string abort_mod;
    bus.Subscribe("m", 0, play, [&](HookPayload& payload) {
        ++invocations;
        if (invocations >= 20) return;  // BUG guard: never spin if cap fails.
        HookDispatchResult nested = bus.DispatchBefore(play, payload);
        if (nested.aborted) {
            abort_seen = true;
            abort_ran = nested.ran;
            abort_mod = nested.aborted_mod;
        }
    });

    HookPayload payload;
    payload.hook = play;
    HookDispatchResult top = bus.DispatchBefore(play, payload);

    CHECK_FALSE(top.aborted);
    CHECK(invocations == 3);  // depths 1..3 run, the 4th entry aborts.
    CHECK(abort_seen);
    CHECK(abort_ran == 0);
    CHECK(abort_mod == "m");
    CHECK(bus.CurrentReentryDepth(play) == 0);  // depth fully unwound.
    CHECK(bus.AbortCount("m") == 1);
}

TEST_CASE("event_bus: mod is disarmed at the abort threshold") {
    EnvGuard cap("UNI_REENTRY_CAP", "2");
    EnvGuard disarm("UNI_MOD_DISARM_THRESHOLD", "2");
    EventBus bus({"good", "bad"});
    const HookId play{"play", HookPhase::kBefore};

    int good_invocations = 0;
    int bad_invocations = 0;
    std::string disarmed;
    bus.Subscribe("good", 0, play,
                  [&](HookPayload&) { ++good_invocations; });
    bus.Subscribe("bad", 0, play, [&](HookPayload& payload) {
        ++bad_invocations;
        if (bad_invocations >= 50) return;
        HookDispatchResult nested = bus.DispatchBefore(play, payload);
        if (!nested.disarmed_mod.empty()) disarmed = nested.disarmed_mod;
    });

    for (int i = 0; i < 2; ++i) {
        HookPayload payload;
        payload.hook = play;
        bus.DispatchBefore(play, payload);
    }

    CHECK(bus.AbortCount("bad") == 2);
    CHECK(bus.IsDisarmed("bad"));
    CHECK(disarmed == "bad");

    // INFO: a disarmed mod's systems are neutralized for the rest of the match.
    const int good_before = good_invocations;
    HookPayload payload;
    payload.hook = play;
    HookDispatchResult result = bus.DispatchBefore(play, payload);
    CHECK(result.ran == 1);
    CHECK(good_invocations == good_before + 1);
}

TEST_CASE("hooks: IsVetoCapable matches the veto column") {
    const HookPhase before = HookPhase::kBefore;
    const HookPhase after = HookPhase::kAfter;

    CHECK(match::ecs::IsVetoCapable(HookId{"turn_end", before}));
    CHECK(match::ecs::IsVetoCapable(HookId{"play", before}));
    CHECK(match::ecs::IsVetoCapable(HookId{"draw_attempt", before}));
    CHECK(match::ecs::IsVetoCapable(HookId{"draw", before}));
    CHECK(match::ecs::IsVetoCapable(HookId{"pile_empty", before}));
    CHECK(match::ecs::IsVetoCapable(HookId{"hand_empty", before}));
    CHECK(match::ecs::IsVetoCapable(HookId{"win_check", before}));

    // INFO: an after variant never vetoes, and non-listed hooks never do.
    CHECK_FALSE(match::ecs::IsVetoCapable(HookId{"play", after}));
    CHECK_FALSE(match::ecs::IsVetoCapable(HookId{"match_start", before}));
    CHECK_FALSE(match::ecs::IsVetoCapable(HookId{"shuffle", before}));
    CHECK_FALSE(match::ecs::IsVetoCapable(HookId{"status_applied", before}));
    CHECK_FALSE(match::ecs::IsVetoCapable(HookId{"not_a_hook", before}));
}

TEST_CASE("event_bus: veto only surfaces for a veto-capable hook") {
    EventBus bus({"m"});
    HookPayload payload;

    const HookId play{"play", HookPhase::kBefore};
    bus.Subscribe("m", 0, play, [](HookPayload& p) { p.veto = true; });
    HookDispatchResult vetoed = bus.DispatchBefore(play, payload);
    CHECK(vetoed.ran == 1);
    CHECK(vetoed.vetoed);

    // INFO: match_start is not veto-capable; the flag is ignored.
    const HookId start{"match_start", HookPhase::kBefore};
    bus.Subscribe("m", 0, start, [](HookPayload& p) { p.veto = true; });
    HookPayload start_payload;
    HookDispatchResult not_vetoed = bus.DispatchBefore(start, start_payload);
    CHECK(not_vetoed.ran == 1);
    CHECK_FALSE(not_vetoed.vetoed);
}

TEST_CASE("event_bus: dispatch clears a stale veto on a reused payload") {
    EventBus bus({"m"});
    const HookId play{"play", HookPhase::kBefore};
    bus.Subscribe("m", 0, play, [](HookPayload&) {});

    HookPayload payload;
    payload.veto = true;  // stale from a previous dispatch
    HookDispatchResult result = bus.DispatchBefore(play, payload);
    CHECK(result.ran == 1);
    CHECK_FALSE(result.vetoed);
    CHECK_FALSE(payload.veto);
}

TEST_CASE("event_bus: Subscribe rejects an unknown hook") {
    EventBus bus({"m"});
    const HookId play{"play", HookPhase::kBefore};
    CHECK(bus.Subscribe("m", 0, play, [](HookPayload&) {}));
    CHECK_FALSE(bus.Subscribe(
        "m", 1, HookId{"not_a_hook", HookPhase::kBefore},
        [](HookPayload&) {}));
    CHECK_FALSE(bus.Subscribe("", 0, play, [](HookPayload&) {}));
    CHECK_FALSE(bus.Subscribe("m", 0, play, match::ecs::HookCallback()));
}
