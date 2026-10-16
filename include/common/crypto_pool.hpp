#pragma once
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <result.hpp>

/**
 * @file crypto_pool.hpp
 * @brief Bounded CPU worker pool for PBKDF2 password hashing.
 *
 * Mirrors the DbWorker pattern (`include/common/db_worker.hpp`): callers
 * only enqueue and (for the sync API) block on the returned future. Unlike
 * DbWorker's single serial thread, this pool runs N threads — PBKDF2 is
 * CPU-bound with no shared connection, so parallelism is safe and keeps
 * the loop thread off the ~200k-iteration hot path.
 *
 * Pool size is `std::thread::hardware_concurrency()` (4 when unknown),
 * overridable via the `CRYPTO_POOL_THREADS` environment variable.
 */
class CryptoPool {
public:
    /** @brief Process-wide singleton. Threads start on first call. */
    static CryptoPool& Get();

    CryptoPool(const CryptoPool&)            = delete;
    CryptoPool& operator=(const CryptoPool&) = delete;

    /**
     * @brief Hashes `password` on a pool thread (salt + pepper + PBKDF2).
     * @return Future holding `<base64_salt>:<base64_hash>` or an error.
     */
    std::future<Result<std::string>> HashPasswordAsync(std::string password);

    /**
     * @brief Verifies `password` against `stored` on a pool thread.
     * @return Future holding true on match, false otherwise (including
     * malformed rows and PBKDF2 failure — never throws).
     */
    std::future<bool> VerifyPasswordAsync(std::string password, std::string stored);

private:
    CryptoPool();
    ~CryptoPool();

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

    void Run();

    static std::size_t ThreadCount();

    mutable std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> q_;
    bool stop_ = false;
    std::vector<std::thread> threads_;
};
