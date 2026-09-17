#include <common/email_queue.hpp>
#include <logger.hpp>

EmailQueue::EmailQueue(std::unique_ptr<IEmailSender> sender)
    : sender_(std::move(sender)), worker_(&EmailQueue::Run, this) {}

EmailQueue::~EmailQueue() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    worker_.join();
}

void EmailQueue::Enqueue(OutboundEmail mail) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= kMaxQueueSize) {
        Logger::Warn("[Email] queue full (", kMaxQueueSize, "), dropping oldest pending mail");
        queue_.pop_front();
    }
    queue_.push_back(std::move(mail));
    cv_.notify_one();
}

std::size_t EmailQueue::PendingForTests() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void EmailQueue::Run() {
    while (true) {
        OutboundEmail mail;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (queue_.empty()) {
                if (stop_) return;
                continue;
            }
            mail = std::move(queue_.front());
            queue_.pop_front();
        }

        auto result = sender_->Send(mail);
        if (!result.has_value()) {
            Logger::Error("[Email] send failed to=", mail.to_address, " ", result.error().message);
        }
    }
}
