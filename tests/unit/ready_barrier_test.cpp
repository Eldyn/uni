#include <doctest/doctest.h>
#include <match/server/ready_barrier.hpp>

using match::server::ReadyBarrier;

TEST_CASE("ReadyBarrier: default constructed barrier is open") {
    ReadyBarrier barrier;
    CHECK(barrier.IsOpen());
    CHECK_FALSE(barrier.Open());
}

TEST_CASE("ReadyBarrier: counts non-pending seats as ready") {
    ReadyBarrier barrier;
    barrier.Arm({"alice", "bob"}, 4);
    CHECK_FALSE(barrier.IsOpen());
    CHECK(barrier.Total() == 4);
    CHECK(barrier.Ready() == 2);
    CHECK_FALSE(barrier.Complete());
}

TEST_CASE("ReadyBarrier: marking every pending seat completes it") {
    ReadyBarrier barrier;
    barrier.Arm({"alice", "bob"}, 2);
    CHECK(barrier.MarkReady("alice"));
    CHECK_FALSE(barrier.Complete());
    CHECK(barrier.MarkReady("bob"));
    CHECK(barrier.Complete());
    CHECK(barrier.Ready() == 2);
}

TEST_CASE("ReadyBarrier: duplicate and unknown seats do not change the count") {
    ReadyBarrier barrier;
    barrier.Arm({"alice", "bob"}, 2);
    CHECK(barrier.MarkReady("alice"));
    CHECK_FALSE(barrier.MarkReady("alice"));
    CHECK_FALSE(barrier.MarkReady("mallory"));
    CHECK(barrier.Ready() == 1);
}

TEST_CASE("ReadyBarrier: opens exactly once") {
    ReadyBarrier barrier;
    barrier.Arm({"alice"}, 1);
    CHECK(barrier.Open());
    CHECK(barrier.IsOpen());
    CHECK_FALSE(barrier.Open());
    CHECK_FALSE(barrier.MarkReady("alice"));
}

TEST_CASE("ReadyBarrier: arming with nobody pending is complete at once") {
    ReadyBarrier barrier;
    barrier.Arm({}, 3);
    CHECK(barrier.Complete());
}

TEST_CASE("ReadyBarrier: rename keeps a pending seat pending") {
    ReadyBarrier barrier;
    barrier.Arm({"alice"}, 2);
    CHECK(barrier.Rename("alice", "alice2"));
    CHECK_FALSE(barrier.MarkReady("alice"));
    CHECK(barrier.MarkReady("alice2"));
    CHECK(barrier.Complete());
}
