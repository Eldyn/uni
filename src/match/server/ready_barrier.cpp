#include <match/server/ready_barrier.hpp>

namespace match::server {

void ReadyBarrier::Arm(const std::vector<std::string>& pending,
                       std::size_t total) {
    pending_ = std::unordered_set<std::string>(pending.begin(), pending.end());
    total_ = total;
    open_ = false;
}

bool ReadyBarrier::MarkReady(const std::string& username) {
    if (open_) return false;
    return pending_.erase(username) > 0;
}

bool ReadyBarrier::Rename(const std::string& from, const std::string& to) {
    if (pending_.erase(from) == 0) return false;
    pending_.insert(to);
    return true;
}

bool ReadyBarrier::Complete() const { return !open_ && pending_.empty(); }

bool ReadyBarrier::Open() {
    if (open_) return false;
    open_ = true;
    pending_.clear();
    return true;
}

bool ReadyBarrier::IsOpen() const { return open_; }

std::size_t ReadyBarrier::Ready() const {
    return total_ >= pending_.size() ? total_ - pending_.size() : 0;
}

std::size_t ReadyBarrier::Total() const { return total_; }

}  // namespace match::server
