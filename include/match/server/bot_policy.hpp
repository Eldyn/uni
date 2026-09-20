#pragma once

#include <match/modload/artifacts.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

/**
 * @file bot_policy.hpp
 * @brief Bot strategy interface + phase-1 heuristics.
 *
 * Bots on the new flow are split in two: `IBotPolicy` is the small strategy
 * seam a future effect-aware ("learning") policy plugs into, and
 * `HeuristicBotPolicy` is the deterministic phase-1 implementation. The policy
 * only DECIDES; `BotStep` performs exactly one action on a live
 * `match::server::MatchSession`.
 *
 * Phase-1 heuristics:
 *
 * - card play: a legal card from the bot's own hand, read from the view layer
 *   snapshot's own-hand `can_play` flags (the engine restriction pipeline);
 * - prompts: known kinds get heuristic answers (choose_color = most common
 *   colour in hand; choose_player = fewest cards; choose_card = first option;
 *   yes_no = accept; choose_value = middle); an unknown kind uses the prompt
 *   `default` when the payload carries one, else the first schema-valid value;
 * - windows: pass by default, with a deterministic 20% chance (per-bot seed,
 *   stable across replays) to offer the best eligible card.
 *
 * ADDITIVE: new `match::server` files only; `MatchSession` and the
 * engine are untouched. The driver glue (`BotStep` / `BuildBotView`) is the
 * seam the controller's controller will reuse.
 */

namespace match::server {

class MatchSession;

/**
 * @struct BotHandCard
 * @brief One own-hand card a policy may consider, from the snapshot.
 *
 * `bits` is the wire `CompactCardV2.bits`; `color` / `value` are the frozen
 * `card_facts` strings (`"red"`, `"jolly_draw4"`, ...). `can_play` is the
 * engine's playability flag and is only meaningful on the bot's own turn.
 */
struct BotHandCard {
    uint32_t bits = 0;      /**< Packed wire card id. */
    std::string kind;       /**< Frozen kind id, e.g. `vanilla:red_5`. */
    std::string color;      /**< Face colour, `white` for wild. */
    std::string value;      /**< Face label, e.g. `7` / `draw2`. */
    bool can_play = false;  /**< Engine playability (own turn only). */
};

/**
 * @struct BotPlayerRow
 * @brief One seated player's public hand size.
 */
struct BotPlayerRow {
    std::string username;  /**< Seated username. */
    int card_count = 0;    /**< Public hand size. */
};

/**
 * @struct BotView
 * @brief The read-only slice of a match a policy decides from.
 *
 * Built once per decision by `BuildBotView`; keeping the policy on this plain
 * struct (rather than on `MatchSession`) makes every heuristic unit-testable
 * without assembling a match.
 */
struct BotView {
    std::string username;   /**< The deciding bot. */
    std::string current_player;  /**< Whose turn it is (empty when over). */
    std::vector<BotHandCard> hand;       /**< The bot's own hand. */
    std::vector<BotPlayerRow> players;   /**< All seats, public counts. */
    bool prompt_pending = false;         /**< A prompt is parked for the bot. */
    std::string prompt_kind;             /**< Parked prompt kind. */
    nlohmann::json prompt_payload = nlohmann::json::object();
    bool window_open = false;            /**< A response window is open. */
    uint64_t window_id = 0;              /**< Window id (RNG stability). */
    bool is_responder = false;           /**< Bot may reply to the window. */
    bool window_responded = false;       /**< Bot already replied / passed. */
};

/**
 * @class IBotPolicy
 * @brief The bot strategy seam.
 *
 * Pure decisions; no session mutation. A future effect-aware policy implements
 * the same three methods without touching the engine or the driver.
 */
class IBotPolicy {
public:
    virtual ~IBotPolicy() = default;

    /**
     * @brief Pick a card to play this turn, or `nullopt` to draw.
     *
     * Only called when `view.current_player == view.username`.
     *
     * @param view The deciding bot's read-only view.
     * @return Wire card bits, or `nullopt` to draw.
     */
    virtual std::optional<uint32_t> ChoosePlay(const BotView& view) = 0;

    /**
     * @brief Answer the parked prompt addressed to this bot.
     *
     * @param view The deciding bot's read-only view.
     * @return The JSON answer `MatchSession::SubmitInput` will validate.
     */
    virtual nlohmann::json ChoosePrompt(const BotView& view) = 0;

    /**
     * @brief Preference-ordered window response candidates.
     *
     * Empty means "pass". `BotStep` offers each candidate in order until the
     * engine accepts one (ineligible cards are rejected silently), then passes.
     *
     * @param view The deciding bot's read-only view.
     * @return Candidate wire card bits, best first; empty = pass.
     */
    virtual std::vector<uint32_t> ChooseWindowResponses(
        const BotView& view) = 0;
};

/**
 * @class HeuristicBotPolicy
 * @brief Deterministic phase-1 bot heuristics.
 */
class HeuristicBotPolicy : public IBotPolicy {
public:
    /**
     * @brief Bind the bot's RNG seed and its prompt-schema table.
     *
     * Built-in phase-1 schemas (`choose_color` enum, `choose_yes_no` boolean)
     * are installed first; `extra_schemas` (mod-declared kinds, see
     * `PromptSchemasFor`) overwrite them by kind. An undeclared kind stays
     * permissive, matching `MatchSession`'s own table.
     *
     * @param seed          Per-bot deterministic seed.
     * @param extra_schemas Optional kind -> `response_schema` overrides.
     */
    explicit HeuristicBotPolicy(
        uint64_t seed,
        std::map<std::string, nlohmann::json> extra_schemas = {});

    std::optional<uint32_t> ChoosePlay(const BotView& view) override;
    nlohmann::json ChoosePrompt(const BotView& view) override;
    std::vector<uint32_t> ChooseWindowResponses(const BotView& view) override;

private:
    /** @brief Schema for `kind`, or nullptr when undeclared (permissive). */
    const nlohmann::json* SchemaFor(const std::string& kind) const;

    uint64_t seed_;
    std::map<std::string, nlohmann::json> schemas_;
};

/**
 * @brief Build the read-only decision view for `username`.
 *
 * Reads the own-hand snapshot (`can_play` flags) plus the parked prompt
 * and open window state. Never mutates the match.
 *
 * @param session  Live match session.
 * @param username The bot to build for.
 * @return The bot's decision view.
 */
BotView BuildBotView(const MatchSession& session, const std::string& username);

/**
 * @brief Perform exactly one bot action on `session` for `username`.
 *
 * Order: answer a parked prompt, else reply to an open window, else take the
 * turn (play a chosen card, or draw). Returns false when the bot has nothing
 * to do (not its turn / not a responder / match over).
 *
 * @param session  Live match session to mutate.
 * @param policy   The deciding strategy.
 * @param username The bot to act for.
 * @return true when an action was accepted by the engine.
 */
bool BotStep(MatchSession& session, IBotPolicy& policy,
             const std::string& username);

/**
 * @brief Collect mod-declared prompt schemas (`manifest.prompts`).
 *
 * Mirrors `MatchSession`'s kind -> `response_schema` table so a bot answers an
 * unknown mod prompt with a schema-valid value. Built-ins are NOT added here;
 * `HeuristicBotPolicy` installs those itself.
 *
 * @param mods Loaded mods backing the match.
 * @return kind -> `response_schema` for every valid declaration.
 */
std::map<std::string, nlohmann::json> PromptSchemasFor(
    const std::vector<match::modload::LoadedMod>& mods);

}  // namespace match::server
