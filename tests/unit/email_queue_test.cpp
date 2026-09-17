#include <doctest/doctest.h>
#include <common/email_queue.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

class CapturingTestSender final : public IEmailSender {
public:
    explicit CapturingTestSender(std::shared_ptr<std::vector<OutboundEmail>> captured)
        : captured_(std::move(captured)) {}

    VoidResult Send(const OutboundEmail& mail) override {
        std::lock_guard<std::mutex> lock(mutex_);
        captured_->push_back(mail);
        return {};
    }

private:
    std::shared_ptr<std::vector<OutboundEmail>> captured_;
    std::mutex mutex_;
};

class SlowTestSender final : public IEmailSender {
public:
    explicit SlowTestSender(std::shared_ptr<std::atomic<int>> send_count = nullptr)
        : send_count_(std::move(send_count)) {}

    VoidResult Send(const OutboundEmail& /*mail*/) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (send_count_) send_count_->fetch_add(1);
        return {};
    }

private:
    std::shared_ptr<std::atomic<int>> send_count_;
};

}  // namespace

TEST_SUITE("EmailQueue") {

TEST_CASE("Enqueue does not block and eventually sends") {
    auto captured = std::make_shared<std::vector<OutboundEmail>>();
    auto sender = std::make_unique<CapturingTestSender>(captured);
    EmailQueue queue(std::move(sender));
    queue.Enqueue({"a@b.com", "A", "Subj", "<p>hi</p>", "hi"});
    for (int i = 0; i < 50 && captured->empty(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(captured->size() == 1);
}

TEST_CASE("Destroying queue with pending items does not hang") {
    auto sender = std::make_unique<SlowTestSender>();
    auto start = std::chrono::steady_clock::now();
    {
        EmailQueue queue(std::move(sender));
        queue.Enqueue({"a@b.com", "A", "S", "h", "t"});
    }
    auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(elapsed < std::chrono::seconds(2));
}

TEST_CASE("Destroying queue with multiple pending items does not drain the backlog") {
    auto send_count = std::make_shared<std::atomic<int>>(0);
    auto sender = std::make_unique<SlowTestSender>(send_count);
    auto start = std::chrono::steady_clock::now();
    {
        EmailQueue queue(std::move(sender));
        for (int i = 0; i < 5; ++i)
            queue.Enqueue({"a@b.com", "A", "S", "h", "t"});
        // Give the worker a moment to pick up the first item before the test
        // starts to destroy the queue, so the "in-flight send is awaited,
        // backlog is not" behaviour is actually exercised.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    auto elapsed = std::chrono::steady_clock::now() - start;
    // Only the single in-flight send (~50ms) should be awaited; draining
    // all 5 items would take ~250ms+.
    CHECK(elapsed < std::chrono::milliseconds(150));
    CHECK(send_count->load() <= 1);
}

}  // TEST_SUITE
