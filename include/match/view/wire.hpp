#pragma once

#include <glaze/glaze.hpp>

#include <string>
#include <vector>

/**
 * @file wire.hpp
 * @brief Glaze wire structs for the match emit path (perf Task 8).
 *
 * Minimal glaze-based packets for the ECS->WS fan-out. The structs serialize
 * with `glz::write_json` straight into a caller-owned scratch `std::string`
 * that the broadcast loops reserve once and reuse, so a full broadcast pays
 * one buffer instead of one allocation per recipient. `MatchOverWire` is
 * additionally byte-identical for every recipient, so `BroadcastMatchOver`
 * serializes it once into a shared frame and sends it N times (N->1 dumps).
 */

namespace match::view {

/**
 * @brief Shared terminal frame, identical for every recipient.
 *
 * Same fields as the `match_over` packet `BroadcastMatchOver` used to build
 * with nlohmann_json (`action`, `winner`, `placements`), so the wire shape is
 * unchanged and existing packet parsers keep working.
 */
struct MatchOverWire {
    std::string action = "match_over";
    std::string winner;
    std::vector<std::string> placements;
};

/**
 * @brief Serialize `p` into `scratch`, reusing its capacity.
 *
 * Clears first (glaze appends), so a reserved scratch buffer stays flat
 * across loop iterations. Returns true on success.
 */
inline bool WritePacket(const MatchOverWire& p, std::string& scratch) {
    scratch.clear();
    return !static_cast<bool>(glz::write_json(p, scratch));
}

}  // namespace match::view
