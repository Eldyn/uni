#pragma once
#include <common/email_sender.hpp>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

/**
 * @file email_queue.hpp
 * @brief Async send worker: decouples callers from IEmailSender latency by
 * handing mail off to a single background thread.
 */

/**
 * @class EmailQueue
 * @brief Fire-and-forget mail queue backed by one worker thread. Enqueue
 * never blocks on network I/O; a bounded deque protects memory if the
 * worker falls behind, and send failures are logged and dropped rather
 * than retried (retrying against a daily send cap is a footgun users can
 * work around by resending).
 */
class EmailQueue {
public:
    /**
     * @param sender Ownership transferred in; used exclusively by the
     * worker thread.
     */
    explicit EmailQueue(std::unique_ptr<IEmailSender> sender);

    /**
     * @brief Signals the worker to stop, wakes it, and joins it. Any mail
     * still queued at destruction is discarded rather than awaited.
     */
    ~EmailQueue();

    EmailQueue(const EmailQueue&) = delete;
    EmailQueue& operator=(const EmailQueue&) = delete;

    /**
     * @brief Appends mail to the queue and wakes the worker. Never blocks
     * on the network. If the queue is already at capacity, the oldest
     * pending mail is dropped (with a warning) to make room.
     */
    void Enqueue(OutboundEmail mail);

    /**
     * @brief Returns the current queue depth. Test-only introspection.
     */
    std::size_t PendingForTests() const;

private:
    void Run();

    static constexpr std::size_t kMaxQueueSize = 256;

    std::unique_ptr<IEmailSender> sender_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<OutboundEmail> queue_;
    bool stop_ = false;
    std::thread worker_;
};
