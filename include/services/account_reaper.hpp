#pragma once

#include <chrono>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <cstdint>

class Database;

class AccountReaper {
public:
    AccountReaper(Database& db, std::chrono::seconds interval, std::chrono::seconds grace);
    ~AccountReaper();

    void Start();
    void Stop();
    int SweepAt(std::int64_t now_unix);

private:
    void Run();

    Database& db_;
    std::chrono::seconds interval_;
    std::chrono::seconds grace_;

    std::atomic<bool> running_{false};
    std::thread thread_;
    std::condition_variable cv_;
    std::mutex mutex_;
};
