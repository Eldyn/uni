#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

/**
 * @file db_worker.hpp
 * @brief Single-threaded serial executor for SQLite work.
 *
 * Mirrors the EmailQueue pattern (`src/common/email_queue.cpp`): one
 * background thread drains a queue of closures. All `sqlite3_*` calls for a
 * connection must happen on this thread — SQLite connections are not safe
 * for concurrent use from multiple threads, so the loop thread only
 * enqueues and (for the sync API) blocks on the returned future.
 */
class DbWorker {
public:
    DbWorker();
    ~DbWorker();

    DbWorker(const DbWorker&)            = delete;
    DbWorker& operator=(const DbWorker&) = delete;

    /**
     * @brief Enqueues `fn` to run on the worker thread.
     * @return Future holding `fn`'s return value (or its exception).
     * @note Never block on the returned future from inside another worker
     * task — use `IsWorkerThread()` + inline execution for nesting
     * (see `Database::RunInTxn`).
     */
    template <typename F>
    auto Enqueue(F&& fn) -> std::future<std::invoke_result_t<F>> {
        using R = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(fn));
        std::future<R> fut = task->get_future();
        {
            std::lock_guard<std::mutex> lock(m_);
            q_.emplace_back([task]() { (*task)(); });
        }
        cv_.notify_one();
        return fut;
    }

    /** @brief True when called on the worker thread itself. */
    bool IsWorkerThread() const;

private:
    void Run();

    mutable std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> q_;
    bool stop_ = false;
    std::thread th_;
    std::thread::id worker_id_;
};
