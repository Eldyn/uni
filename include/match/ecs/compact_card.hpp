#pragma once

#include <cstdint>
#include <optional>

/**
 * @file compact_card.hpp
 * @brief Wire identity for a card instance — additive V2 header.
 *
 * This is the ECS rewrite's card identity and intentionally does NOT replace
 * `include/common/match/card_types.hpp`; the old engine keeps that file until
 * the swap. Layout is frozen:
 *
 *   bits 31..24 : mod_index   (position in the frozen match mod list)
 *   bits 23..12 : kind_index  (position in that mod's card kind list)
 *   bits 11..0  : instance_id (copy number of this kind within the deck)
 *
 * Indices are meaningless outside one match (the registry is frozen at
 * match start). Anything that outlives a match stores kind string
 * IDs plus counts and re-indexes on restore.
 */

namespace match::ecs {

/** Bit width of each field (mod 8 / kind 12 / instance 12). */
inline constexpr uint32_t kCompactCardModBits = 8;
inline constexpr uint32_t kCompactCardKindBits = 12;
inline constexpr uint32_t kCompactCardInstanceBits = 12;

/** Right-shift of each field within the packed word. */
inline constexpr uint32_t kCompactCardModShift = 24;
inline constexpr uint32_t kCompactCardKindShift = 12;
inline constexpr uint32_t kCompactCardInstanceShift = 0;

/** Extraction masks for each field. */
inline constexpr uint32_t kCompactCardModMask = 0xFFu;
inline constexpr uint32_t kCompactCardKindMask = 0xFFFu;
inline constexpr uint32_t kCompactCardInstanceMask = 0xFFFu;

/** Index-space bounds: 256 mods, 4096 kinds, 4096 copies. */
inline constexpr uint32_t kMaxMods = 1u << kCompactCardModBits;
inline constexpr uint32_t kMaxKindsPerMod = 1u << kCompactCardKindBits;
inline constexpr uint32_t kMaxInstancesPerKind = 1u << kCompactCardInstanceBits;

/**
 * @struct CompactCardV2
 * @brief Packed card instance identity; serializes as a plain integer.
 */
struct CompactCardV2 {
    uint32_t bits = 0;

    /** Position in the frozen match mod list (0..255). */
    uint32_t ModIndex() const {
        return (bits >> kCompactCardModShift) & kCompactCardModMask;
    }

    /** Position in that mod's card kind list (0..4095). */
    uint32_t KindIndex() const {
        return (bits >> kCompactCardKindShift) & kCompactCardKindMask;
    }

    /** Copy number of this kind within the deck (0..4095). */
    uint32_t InstanceId() const {
        return (bits >> kCompactCardInstanceShift) & kCompactCardInstanceMask;
    }

    bool operator==(const CompactCardV2&) const = default;
};

/**
 * @brief Packs a card identity, rejecting out-of-bounds indices.
 *
 * @return The packed card, or nullopt when any index exceeds its field bound.
 */
inline std::optional<CompactCardV2> MakeCompactCard(uint32_t mod_index,
                                                    uint32_t kind_index,
                                                    uint32_t instance_id) {
    if (mod_index >= kMaxMods || kind_index >= kMaxKindsPerMod ||
        instance_id >= kMaxInstancesPerKind) {
        return std::nullopt;
    }
    CompactCardV2 card;
    card.bits = (mod_index << kCompactCardModShift) |
                (kind_index << kCompactCardKindShift) |
                (instance_id << kCompactCardInstanceShift);
    return card;
}

}  // namespace match::ecs
