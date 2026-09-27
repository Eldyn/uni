#pragma once

#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

namespace match::server {

/**
 * @class ReadyBarrier
 * @brief Tracks which seated humans still have to report their client loaded
 *        before the first turn may start.
 */
class ReadyBarrier {
public:
    void Arm(const std::vector<std::string>& pending, std::size_t total);
    bool MarkReady(const std::string& username);
    bool Rename(const std::string& from, const std::string& to);
    bool Complete() const;
    bool Open();
    bool IsOpen() const;
    std::size_t Ready() const;
    std::size_t Total() const;

private:
    std::unordered_set<std::string> pending_;
    std::size_t total_ = 0;
    bool open_ = true;
};

}  // namespace match::server
