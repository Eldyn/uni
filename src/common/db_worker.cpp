#include <common/db_worker.hpp>

DbWorker::DbWorker() : th_(&DbWorker::Run, this) {}

DbWorker::~DbWorker() {
    {
        std::lock_guard<std::mutex> lock(m_);
        stop_ = true;
    }
    cv_.notify_all();
    if (th_.joinable()) th_.join();
}

bool DbWorker::IsWorkerThread() const {
    std::lock_guard<std::mutex> lock(m_);
    return worker_id_ != std::thread::id{} && std::this_thread::get_id() == worker_id_;
}

void DbWorker::Run() {
    {
        std::lock_guard<std::mutex> lock(m_);
        worker_id_ = std::this_thread::get_id();
    }
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_);
            cv_.wait(lock, [this] { return stop_ || !q_.empty(); });
            if (q_.empty()) {
                if (stop_) return;
                continue;
            }
            task = std::move(q_.front());
            q_.pop_front();
        }
        task();
    }
}
