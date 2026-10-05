#include "match/engine/match_instance.hpp"

#include "match/modload/play_conditions.hpp"
#include "match/modload/restriction.hpp"
#include "match/ops/op_helpers.hpp"
#include "match/rng.hpp"
#include "match/status.hpp"

#include <logger.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * @file match_instance.cpp
 * @brief Engine `MatchInstance` implementation.
 *
 * The engine implemented the engine default play / draw / turn advance / win
 * directly. The engine wraps every step in the EventBus hook dispatch,
 * drains the played card's behavior graphs through the Resolver (op events
 * into the ordered per-match log, `kNeedsInput` pauses resumed by
 * `SubmitInput`), bridges op events to their observation hooks and runs the
 *  must-apply auto cards. Windows and the full prompt UX stay the engine.
 */

namespace match::engine {
namespace {

using nlohmann::json;

/** @brief Entity handle as a JSON object. */
json EntityJson(ecs::Entity entity) {
    return json{{"index", entity.index}, {"generation", entity.generation}};
}

/** @brief A `ZoneRef` as the zone descriptor. */
json ZoneJson(const ecs::ZoneRef& zone) {
    json out = json::object();
    switch (zone.kind) {
        case ecs::ZoneKind::kHand:
            out["kind"] = "hand";
            out["owner"] = EntityJson(zone.owner);
            break;
        case ecs::ZoneKind::kDrawPile:
            out["kind"] = "draw_pile";
            break;
        case ecs::ZoneKind::kDiscardPile:
            out["kind"] = "discard_pile";
            break;
        case ecs::ZoneKind::kLimbo:
            out["kind"] = "limbo";
            break;
    }
    return out;
}

/** @brief Username of a player entity, or empty when not a live player. */
std::string PlayerUsername(const ecs::EntityStore& store,
                           ecs::Entity player) {
    const ecs::PlayerInfo* info = store.Get<ecs::PlayerInfo>(player);
    return info == nullptr ? std::string() : info->username;
}

/** @brief An optional entity as `{index, generation}` or JSON null. */
json EntityOrNull(const std::optional<ecs::Entity>& entity) {
    return entity.has_value() ? EntityJson(*entity) : json(nullptr);
}

/** @brief Parse a `{index, generation}` entity handle from JSON. */
std::optional<ecs::Entity> EntityFromJson(const json& value) {
    if (value.is_number_unsigned()) {
        ecs::Entity entity;
        entity.index = value.get<uint32_t>();
        return entity;
    }
    if (!value.is_object()) return std::nullopt;
    const auto index = value.find("index");
    if (index == value.end() || !index->is_number_unsigned()) {
        return std::nullopt;
    }
    ecs::Entity entity;
    entity.index = index->get<uint32_t>();
    const auto generation = value.find("generation");
    if (generation != value.end() && generation->is_number_unsigned()) {
        entity.generation = generation->get<uint32_t>();
    }
    return entity;
}

/** @brief Read an optional entity field from a schedule envelope. */
std::optional<ecs::Entity> EnvelopeEntity(const json& envelope,
                                          const char* key) {
    const auto it = envelope.find(key);
    if (it == envelope.end()) return std::nullopt;
    return EntityFromJson(*it);
}

}  // namespace

// --- construction / start --------------------------------------------------

MatchInstance::MatchInstance(std::unique_ptr<MatchAssembly> assembly)
    : MatchInstance(std::move(assembly), match::WindowConfig::FromEnv(),
                    match::DefaultNowMs()) {}

MatchInstance::MatchInstance(std::unique_ptr<MatchAssembly> assembly,
                             match::NowMs clock)
    : MatchInstance(std::move(assembly), match::WindowConfig::FromEnv(),
                    std::move(clock)) {}

MatchInstance::MatchInstance(std::unique_ptr<MatchAssembly> assembly,
                             match::WindowConfig window_config,
                             match::NowMs clock)
    : assembly_(std::move(assembly)),
      scheduler_(clock),
      timers_(window_config, std::move(clock)) {
    if (assembly_ == nullptr) {
        // ERROR: a null assembly cannot run; treat it as a finished match so
        //        every input is refused rather than dereferencing null.
        finished_ = true;
        return;
    }
    Start();
}

void MatchInstance::Start() {
    if (started_ || assembly_ == nullptr) return;
    started_ = true;

    ecs::EntityStore& store = assembly_->store;
    MatchRegistries& registries = assembly_->registries;

    // INFO: assembly seats player 0 as current; re-assert it only when no
    //       current player survived, so Start is safe to call twice.
    if (!CurrentPlayer().has_value() && !registries.players.empty()) {
        if (ecs::TurnState* turn =
                store.Get<ecs::TurnState>(registries.players.front())) {
            turn->is_current = true;
        }
    }

    const std::optional<ecs::Entity> current = CurrentPlayer();
    if (!current.has_value()) return;

    // INFO: round 0 opens at match start; `match_start` itself was dispatched
    //       by assembly and must not be re-fired.
    json round = json{{"round", 0}};
    Before("round_start", round);
    After("round_start", round);

    json turn = json{{"player", EntityJson(*current)}};
    Before("turn_start", turn);
    After("turn_start", turn);
}

bool MatchInstance::ArmCurrentTurnDeadline(int64_t duration_ms) {
    if (!started_ || finished_ || assembly_ == nullptr) return false;
    const std::optional<ecs::Entity> current = CurrentPlayer();
    if (!current.has_value()) return false;

    ecs::EntityStore& store = assembly_->store;
    ecs::TurnState* turn = store.Get<ecs::TurnState>(*current);
    if (turn == nullptr) {
        turn = store.Add<ecs::TurnState>(*current, ecs::TurnState{});
        if (turn == nullptr) return false;
    }
    return timers_.Turn().Arm(*turn, duration_ms);
}

void MatchInstance::SyncClocks(int64_t duration_ms) {
    if (!started_ || finished_ || assembly_ == nullptr) return;
    clock_duration_ms_ = duration_ms;
    const std::optional<ecs::Entity> current = CurrentPlayer();
    if (!current.has_value()) return;

    ecs::EntityStore& store = assembly_->store;
    ecs::TurnState* turn = store.Get<ecs::TurnState>(*current);
    if (turn == nullptr) {
        turn = store.Add<ecs::TurnState>(*current, ecs::TurnState{});
        if (turn == nullptr) return;
    }

    match::TurnTimer& clock = timers_.Turn();
    const bool new_owner =
        !turn_clock_owner_.has_value() || !(*turn_clock_owner_ == *current);
    const bool unarmed = turn->turn_deadline_ms == 0 && !clock.Suspended();
    if (new_owner || unarmed) {
        clock.Reset();
        clock.Arm(*turn, duration_ms);
        turn_clock_owner_ = *current;
    }

    const bool paused = pending_input_.has_value() || WindowOpen();
    if (paused && !clock.Suspended()) {
        clock.Suspend(*turn);
    } else if (!paused && clock.Suspended()) {
        clock.Resume(*turn);
    }

    if (pending_input_.has_value() && pending_input_->deadline_ms == 0) {
        pending_input_->deadline_ms = clock.Now() + duration_ms;
        pending_input_->duration_ms = duration_ms;
    }
}

int64_t MatchInstance::CurrentTurnDeadlineMs() const {
    if (!started_ || assembly_ == nullptr) return 0;
    const std::optional<ecs::Entity> current = CurrentPlayer();
    if (!current.has_value()) return 0;
    const ecs::TurnState* turn =
        assembly_->store.Get<ecs::TurnState>(*current);
    return turn == nullptr ? 0 : turn->turn_deadline_ms;
}

// --- input flow ------------------------------------------------------------

bool MatchInstance::PlayCard(const std::string& username, ecs::Entity card) {
    if (!started_ || finished_ || assembly_ == nullptr) return false;
    // INFO: while a window is open, a play attempt is a window response; a
    //       pending op input takes precedence and refuses ordinary plays.
    if (pending_input_.has_value()) return false;
    if (pending_window_.has_value()) return RespondWindow(username, card);

    ecs::EntityStore& store = assembly_->store;
    MatchRegistries& registries = assembly_->registries;

    const std::optional<ecs::Entity> player = FindPlayer(username);
    if (!player.has_value()) return false;

    // INFO:  - while a playable voluntary draw holds the turn, only the
    //       holder may act and only by playing the held card. Clearing the
    //       state before the play pipeline runs lets a wild open its
    //       colour prompt (a pending prompt refuses further plays).
    if (awaiting_play_drawn_.has_value()) {
        if (!(awaiting_play_drawn_->player == *player)) return false;
        if (!(awaiting_play_drawn_->card == card)) return false;
        awaiting_play_drawn_.reset();
    }

    if (!store.IsAlive(card) || !store.Has<ecs::CardIdentity>(card)) {
        return false;
    }

    const ecs::Hand* hand = store.Get<ecs::Hand>(*player);
    if (hand == nullptr) return false;

    int hand_index = -1;
    for (std::size_t i = 0; i < hand->cards.size(); ++i) {
        if (hand->cards[i] == card) {
            hand_index = static_cast<int>(i);
            break;
        }
    }

    const std::optional<ecs::Entity> current = CurrentPlayer();
    const bool in_turn = current.has_value() && (*current == *player);

    // -- restriction pipeline over true engine state -------------
    const modload::PlayAttempt attempt =
        BuildPlayAttempt(*player, card, in_turn);
    const modload::PlayDecision decision = CheckPlayRestrictions(attempt);
    if (!decision.allowed) {
        Emit("play_rejected",
             json{{"player", username}, {"reason_id", decision.reason_id}});
        return false;
    }
    if (hand_index < 0) {
        // INFO: the pipeline allowed but the card is not in the actor's hand;
        //       only reachable once `must_own_card` is removed. Refuse rather
        //       than move a card the player does not hold.
        return false;
    }

    // --: before:play -> engine default -> after:play -------------
    const std::string kind_id = attempt.card_kind;
    const json from_zone =
        ZoneJson(ecs::ZoneRef{ecs::ZoneKind::kHand, *player});
    const json to_zone =
        ZoneJson(ecs::ZoneRef{ecs::ZoneKind::kDiscardPile, ecs::Entity{}});

    json play_data = json{{"card", EntityJson(card)},
                          {"player", EntityJson(*player)},
                          {"from", from_zone}};

    // INFO: a `before:play` veto cancels the engine default entirely; the
    //       mod graph owns the outcome, so neither the move nor `after:play`
    //       runs (which would otherwise re-run the card's behavior).
    if (Before("play", play_data)) return true;

    json zone_data = json{{"card", EntityJson(card)},
                          {"from", from_zone},
                          {"to", to_zone}};
    Before("card_left_zone", zone_data);
    Before("card_entered_zone", zone_data);
    ops::MoveCardToZone(store, card,
                        ecs::ZoneRef{ecs::ZoneKind::kDiscardPile,
                                     ecs::Entity{}});
    After("card_left_zone", zone_data);
    After("card_entered_zone", zone_data);

    Emit("card_left_zone", json{{"card", EntityJson(card)},
                                {"from", from_zone},
                                {"to", to_zone}});
    Emit("card_entered_zone", json{{"card", EntityJson(card)},
                                   {"from", from_zone},
                                   {"to", to_zone}});
    Emit("card_played", json{{"player", username},
                             {"card", EntityJson(card)},
                             {"kind", kind_id},
                             {"from_ordinal", hand_index}});

    last_play_ = LastPlay{*player, card, static_cast<uint32_t>(hand_index)};

    if (ecs::ActiveTypeReq* req =
            store.Get<ecs::ActiveTypeReq>(registries.match)) {
        const ecs::FaceSpec* face = store.Get<ecs::FaceSpec>(card);
        if (face != nullptr && !face->color.empty()) {
            req->type = face->color;
        } else {
            const auto facts = assembly_->card_facts.find(kind_id);
            if (facts != assembly_->card_facts.end()) {
                req->type = facts->second.color;
            }
        }
    }

    // INFO: `after:play` is the resolver drain point for the played card's
    //       behavior graph. A pause stops the flow for SubmitInput.
    After("play", play_data);

    // INFO: `cards_played` legs advance on every `after:play`.
    ExecuteScheduled(scheduler_.OnCardPlayed(store, registries.match));

    if (Paused()) {
        if (pending_input_.has_value()) {
            pending_input_->settle_play = true;
            pending_input_->actor = *player;
        } else if (pending_window_.has_value()) {
            pending_window_->settle_play = true;
            pending_window_->actor = *player;
        }
        return true;
    }

    // INFO: Must-apply auto cards fire before a window would open.
    RunMustApply("play");
    if (Paused()) {
        if (pending_input_.has_value()) {
            pending_input_->settle_play = true;
            pending_input_->actor = *player;
        } else if (pending_window_.has_value()) {
            pending_window_->settle_play = true;
            pending_window_->actor = *player;
        }
        return true;
    }

    SettleAfterPlay(*player);
    return true;
}

bool MatchInstance::DrawCard(const std::string& username) {
    if (!started_ || finished_ || assembly_ == nullptr) return false;
    if (Paused()) return false;

    ecs::EntityStore& store = assembly_->store;
    MatchRegistries& registries = assembly_->registries;

    const std::optional<ecs::Entity> player = FindPlayer(username);
    if (!player.has_value()) return false;
    const std::optional<ecs::Entity> current = CurrentPlayer();
    if (!current.has_value() || !(*current == *player)) return false;

    // INFO:  - a held playable draw owns the turn until it is played, kept
    //       or the turn clock expires; the holder cannot draw again.
    if (awaiting_play_drawn_.has_value()) return false;

    // --- draw_attempt: a before-veto skips this draw ----------------------
    json attempt = json{{"player", EntityJson(*player)},
                        {"source", "draw_pile"},
                        {"cause", "action"}};
    const bool skip = Before("draw_attempt", attempt);
    After("draw_attempt", attempt);
    if (skip || Paused()) {
        if (!Paused()) AdvanceTurn();
        return true;
    }

    const ecs::PileContents* draw =
        store.Get<ecs::PileContents>(registries.draw_pile);
    if (draw == nullptr) return false;
    if (draw->cards.empty() && !ReshuffleDiscardIntoDraw()) return false;

    std::optional<ecs::Entity> card =
        ops::DrawTop(store, registries.draw_pile);
    if (!card.has_value()) {
        if (!ReshuffleDiscardIntoDraw()) return false;
        card = ops::DrawTop(store, registries.draw_pile);
        if (!card.has_value()) return false;
    }

    // --- draw: the before-veto is an undo, so it fires before the move ----
    json draw_data = json{{"card", EntityJson(*card)},
                          {"player", EntityJson(*player)},
                          {"cause", "action"}};
    const bool undo = Before("draw", draw_data);
    if (undo) {
        ops::MoveCardToZone(store, *card,
                            ecs::ZoneRef{ecs::ZoneKind::kDrawPile,
                                         ecs::Entity{}});
        After("draw", draw_data);
        if (!Paused()) AdvanceTurn();
        return true;
    }
    if (!ops::MoveCardToZone(
            store, *card,
            ecs::ZoneRef{ecs::ZoneKind::kHand, *player})) {
        return false;
    }
    After("draw", draw_data);

    // INFO: a playable voluntary draw is held for a play/keep choice. Flagged
    //       on the public event so every client can park the card for the
    //       choice even when a bot resolves it within the same step.
    const bool holds_turn = !Paused() && DrawnCardPlayable(*player, *card);
    Emit("cards_drawn", json{{"player", username},
                             {"count", 1},
                             {"source", "draw_pile"},
                             {"held", holds_turn},
                             {"cards", json::array({EntityJson(*card)})}});

    // INFO: An `after:draw` graph may request a forced play of the
    //       drawn card (legacy force_play); route it through the normal play
    //       pipeline instead of passing the turn.
    if (!Paused() && ExecuteForcedPlays()) return true;
    // INFO:  - a draw that paused the flow (input/window) is resolved by its
    //       own continuation, not here.
    if (Paused()) return true;
    // INFO:  - a playable voluntary draw holds the turn: the player chooses to
    //       play it or keep it, or the turn clock expires.
    if (holds_turn) {
        awaiting_play_drawn_ = PendingPlayDrawn{*player, *card};
        return true;
    }
    AdvanceTurn();
    return true;
}

bool MatchInstance::KeepDrawn(const std::string& username) {
    if (!started_ || finished_ || assembly_ == nullptr) return false;
    if (!awaiting_play_drawn_.has_value()) return false;

    const std::optional<ecs::Entity> player = FindPlayer(username);
    if (!player.has_value() || !(awaiting_play_drawn_->player == *player)) {
        return false;
    }

    awaiting_play_drawn_.reset();
    AdvanceTurn();
    return true;
}

bool MatchInstance::SubmitInput(const std::string& username,
                                const nlohmann::json& value) {
    if (!started_ || finished_ || assembly_ == nullptr) return false;
    if (!pending_input_.has_value()) return false;

    // INFO: Defence in depth - a card prompt must be answered with a
    //       `CompactCardV2.bits` integer that maps to a live card. Validate it
    //       BEFORE extracting the pause so a malformed answer leaves the parked
    //       prompt untouched and still answerable with a valid integer; an
    //       overridden prompt schema can never soft-lock the chooser.

    //       S-4: the answer must ALSO be one of the bits the prompt offered.
    //       Mapping against the whole frozen index map is not enough - a player
    //       could name an opponent's card, a discard card or a face-down card
    //       and have the consuming mod graph move/destroy/reveal it, defeating
    //       the revealed-subset/visibility model. The offered set is parked in
    //       the prompt payload's `options`.
    std::optional<ecs::Entity> prompt_card;
    if (pending_input_->kind == "choose_card") {
        if (!value.is_number_integer()) return false;
        const int64_t raw = value.get<int64_t>();
        if (raw < 0 || raw > 0xFFFFFFFFLL) return false;
        ecs::CompactCardV2 id;
        id.bits = static_cast<uint32_t>(raw);
        prompt_card = assembly_->registries.CardEntity(id);
        if (!prompt_card.has_value()) return false;
        if (!OfferedChoice(raw)) return false;
    }

    // INFO: `@choose_player` sugar defence in depth - the answer is a
    //       username and must name a live seat, so a spoofed or stale answer
    //       can never bind a phantom player into a graph's selector.
    if (pending_input_->kind == "choose_player") {
        if (!value.is_string()) return false;
        bool known = false;
        for (ecs::Entity entity :
             assembly_->store.EntitiesWith<ecs::PlayerInfo>()) {
            const ecs::PlayerInfo* info =
                assembly_->store.Get<ecs::PlayerInfo>(entity);
            if (info != nullptr
                && info->username == value.get<std::string>()) {
                // INFO: a race finisher left the table, it cannot be chosen.
                known = !ops::FinishedRace(assembly_->store, entity);
                break;
            }
        }
        if (!known) return false;
    }

    InputPause pending = std::move(*pending_input_);
    pending_input_.reset();

    // INFO: when the op asked a specific player, only that player may answer.
    if (pending.has_target) {
        const ecs::PlayerInfo* info =
            assembly_->store.Get<ecs::PlayerInfo>(pending.target);
        if (info != nullptr && !info->username.empty()
            && info->username != username) {
            pending_input_ = std::move(pending);
            return false;
        }
    }

    if (pending.system_index >= assembly_->systems.size()) {
        // INFO: an auto-trigger graph pause has no subscribed system to
        //       resume from here; the engine owns the window/prompt
        //       continuation.
        return false;
    }
    const modload::BehaviorGraph& graph =
        assembly_->systems[pending.system_index].graph;

    // INFO: The wire speaks `CompactCardV2.bits`; map the validated
    //       answer back to its entity handle before the graph's `from_prompt`
    //       binding consumes it.
    json answer = value;
    if (prompt_card.has_value()) answer = EntityJson(*prompt_card);

    ops::ResolutionFrame frame = pending.frame;
    const resolver::ResolveResult result = assembly_->resolver->ResumeInput(
        graph, pending.mod_id, pending.context, frame, pending.pause,
        std::move(answer));

    AppendResult(result, pending.system_index, pending.mod_id,
                 pending.context, frame, pending.settle_play,
                 pending.actor);
    return true;
}

// --- tick / round boundary -------------------------------------------------

void MatchInstance::Tick() {
    if (!started_ || finished_ || assembly_ == nullptr) return;

    ecs::EntityStore& store = assembly_->store;
    MatchRegistries& registries = assembly_->registries;

    // INFO: a round is one cycle of the seats still at the table, so race
    //       finishers stop counting toward it.
    const std::size_t seat_count = ops::ActivePlayersBySeat(store).size();
    if (seat_count == 0) return;
    ecs::MatchMeta* meta = store.Get<ecs::MatchMeta>(registries.match);
    if (meta == nullptr) return;

    if (turns_elapsed_ >= seat_count) {
        turns_elapsed_ -= seat_count;
        const uint32_t round = meta->round + 1;
        json end_data = json{{"round", meta->round}};
        Before("round_end", end_data);
        After("round_end", end_data);

        // INFO: `rounds` legs advance once per completed round.
        ExecuteScheduled(scheduler_.OnRoundEnd(store, registries.match));

        meta->round = round;

        json start_data = json{{"round", round}};
        Before("round_start", start_data);
        After("round_start", start_data);
        Emit("round_advance", json{{"round", round}});
    }

    // INFO: Timers at the Tick point. While a window is open only
    //       the window clock runs; timeout routes the default, early
    //       close routes the collected winner (or default). Otherwise the turn
    //       deadline is checked.
    const std::optional<ecs::Entity> current = CurrentPlayer();
    if (!current.has_value()) return;
    // INFO: a held group with a recorded winner closes at the hold end; the
    //       first response already decided the outcome, so waiting longer
    //       would be a hidden stall.
    if (WindowWinnerReady()) {
        CloseWindowRoute("response");
        ExecuteScheduled(scheduler_.Tick(store, registries.match, Now()));
        return;
    }
    const match::MatchTimerTick tick =
        timers_.Tick(store, registries.match, *current);
    if (tick.window_timeout) {
        CloseWindowRoute("timeout");
    } else if (tick.window_early_closed) {
        CloseWindowRoute(pending_window_.has_value()
                                 && pending_window_->has_winner
                             ? "response"
                             : "all_pass");
    } else if (tick.turn_expired) {
        // INFO: engine default for a lapsed turn clock; the engine may refine
        //       the AFK/bot takeover policy. A parked play/keep choice is
        //       dropped with the turn it belonged to.
        awaiting_play_drawn_.reset();
        AdvanceTurn();
    }

    // INFO: Scheduled graphs whose duration elapsed run here,
    //       through the same fresh-chain path the event drivers use.
    ExecuteScheduled(scheduler_.Tick(store, registries.match, Now()));
}

// --- turn advance ----------------------------------------------------------

void MatchInstance::AdvanceTurn() {
    if (EndRaceIfTargetReached()) return;

    const std::optional<ecs::Entity> before = CurrentPlayer();

    // INFO: A `before:turn_end` veto grants an extra turn, so the
    //       advance op is skipped and the same player stays current.
    if (before.has_value()) {
        json end_data = json{{"player", EntityJson(*before)}};
        // INFO: a race finisher has left the table, so a veto cannot hand
        //       them another turn.
        const bool vetoed = Before("turn_end", end_data);
        if (vetoed && !ops::FinishedRace(assembly_->store, *before)) return;
        if (Paused()) return;
    }

    // INFO: Reuse the `advance_turn` op so the direction / extra-turn /
    //       one-shot-skip bookkeeping exists in exactly one place.
    ops::ResolutionFrame frame;
    ops::OpContext context(assembly_->bus, assembly_->budget, frame);
    ops::OpArgs args("advance_turn");
    ops::OpResult result = assembly_->runtime.Invoke(
        "advance_turn", assembly_->store, args, context);
    for (json& event : result.events) {
        events_.push_back(std::move(event));
    }

    if (before.has_value()) {
        json end_data = json{{"player", EntityJson(*before)}};
        After("turn_end", end_data);

        // INFO: `turns` legs advance once per ended turn, after
        //       the `after:turn_end` dispatch (the turn-end point).
        ExecuteScheduled(scheduler_.OnTurnEnd(
            assembly_->store, assembly_->registries.match, *before));
    }

    // INFO: only a real seat change counts toward a round; an extra turn
    //       replays the same player (the op's `from == to`) and does not.
    const std::optional<ecs::Entity> after = CurrentPlayer();
    if (before.has_value() && after.has_value()
        && !(*before == *after)) {
        ++turns_elapsed_;
    }
    if (after.has_value()) {
        json start_data = json{{"player", EntityJson(*after)}};
        Before("turn_start", start_data);
        After("turn_start", start_data);
    }
}

// --- reshuffle -------------------------------------------------------------

bool MatchInstance::ReshuffleDiscardIntoDraw() {
    ecs::EntityStore& store = assembly_->store;
    MatchRegistries& registries = assembly_->registries;

    const ecs::PileContents* discard =
        store.Get<ecs::PileContents>(registries.discard_pile);
    if (discard == nullptr || discard->cards.size() <= 1) return false;

    // INFO: `pile_empty` fires when the draw pile is exhausted; a
    //       before-veto blocks the reshuffle (e.g. a stalemate rule).
    json empty_data = json{{"pile", "draw_pile"}};
    const bool blocked = Before("pile_empty", empty_data);
    After("pile_empty", empty_data);
    if (blocked) return false;

    const ecs::Entity top = discard->cards.back();
    std::vector<ecs::Entity> moved(discard->cards.begin(),
                                   discard->cards.end() - 1);

    json shuffle_before = json{{"draw_size", 0},
                               {"discard_size", discard->cards.size()}};
    Before("shuffle", shuffle_before);

    for (ecs::Entity card : moved) {
        ops::MoveCardToZone(
            store, card,
            ecs::ZoneRef{ecs::ZoneKind::kDrawPile, ecs::Entity{}});
    }
    ShuffleDrawPile();

    const ecs::PileContents* draw =
        store.Get<ecs::PileContents>(registries.draw_pile);
    const std::size_t draw_size =
        draw == nullptr ? 0 : draw->cards.size();
    json shuffle_after = json{{"draw_size", draw_size},
                              {"discard_size", 1}};
    After("shuffle", shuffle_after);

    Emit("reshuffle",
         json{{"draw_size", draw_size},
              {"discard_size", discard->cards.size()}});
    return true;
}

void MatchInstance::ShuffleDrawPile() {
    ecs::EntityStore& store = assembly_->store;
    MatchRegistries& registries = assembly_->registries;

    ecs::PileContents* draw =
        store.Get<ecs::PileContents>(registries.draw_pile);
    if (draw == nullptr) return;

    if (std::optional<Rng> rng = RngFor(store, registries.match);
        rng.has_value()) {
        std::vector<ecs::Entity>& cards = draw->cards;
        for (std::size_t i = cards.size(); i > 1; --i) {
            const std::size_t j =
                static_cast<std::size_t>(rng->NextDraw() % i);
            std::swap(cards[i - 1], cards[j]);
        }
    }

    for (std::size_t i = 0; i < draw->cards.size(); ++i) {
        if (ecs::InZone* in = store.Get<ecs::InZone>(draw->cards[i])) {
            in->zone =
                ecs::ZoneRef{ecs::ZoneKind::kDrawPile, ecs::Entity{}};
            in->ordinal = static_cast<uint32_t>(i);
        }
    }
}

// --- win / placement -------------------------------------------------------

void MatchInstance::SettleAfterPlay(ecs::Entity player) {
    ecs::EntityStore& store = assembly_->store;
    const ecs::Hand* after = store.Get<ecs::Hand>(player);
    if (after != nullptr && !after->cards.empty()) {
        AdvanceTurn();
        return;
    }

    // INFO: `hand_empty` before-veto blocks the default win.
    json hand_data = json{{"player", EntityJson(player)}};
    const bool block_hand = Before("hand_empty", hand_data);
    After("hand_empty", hand_data);
    if (Paused()) {
        if (pending_input_.has_value()) {
            pending_input_->settle_play = true;
            pending_input_->actor = player;
        }
        return;
    }
    if (block_hand) {
        AdvanceTurn();
        return;
    }

    // INFO: `win_check` before-veto blocks the default; a non-null rewrite of
    //       `data.player` declares a different finisher, null blocks it.
    json win_data = json{{"player", EntityJson(player)}};
    const bool block_win = Before("win_check", win_data);
    After("win_check", win_data);
    if (Paused()) {
        if (pending_input_.has_value()) {
            pending_input_->settle_play = true;
            pending_input_->actor = player;
        }
        return;
    }

    std::optional<ecs::Entity> winner = player;
    if (win_data.is_object()) {
        const auto it = win_data.find("player");
        if (it != win_data.end()) {
            if (it->is_null()) {
                winner = std::nullopt;
            } else if (const std::optional<ecs::Entity> rewritten =
                           EntityFromJson(*it);
                       rewritten.has_value()) {
                winner = rewritten;
            }
            // WARN: a malformed non-null rewrite keeps the default candidate.
        }
    }

    if (block_win || !winner.has_value() || !store.IsAlive(*winner)
        || !store.Has<ecs::PlayerInfo>(*winner)) {
        AdvanceTurn();
        return;
    }
    DeclareHandEmptyWin(*winner);
}

void MatchInstance::DeclareHandEmptyWin(ecs::Entity player) {
    ecs::EntityStore& store = assembly_->store;
    MatchRegistries& registries = assembly_->registries;

    ecs::Placements* placements =
        store.Get<ecs::Placements>(registries.match);
    const uint32_t race_target = ops::RaceTarget(store);
    if (race_target > 0 && placements != nullptr) {
        DeclareRaceFinish(player, *placements, race_target);
        return;
    }
    const uint32_t place =
        placements == nullptr
            ? 1u
            : static_cast<uint32_t>(placements->order.size()) + 1u;
    if (placements != nullptr) placements->order.push_back(player);

    winner_ = player;
    finished_ = true;
    Emit("placement",
         json{{"player", PlayerUsername(store, player)}, {"place", place}});
    EmitMatchEnd();
}

void MatchInstance::DeclareRaceFinish(ecs::Entity player,
                                      ecs::Placements& placements,
                                      uint32_t race_target) {
    ecs::EntityStore& store = assembly_->store;

    // INFO: a finisher already placed (a mod rewrite naming them again) is
    //       not re-ranked; play simply moves on.
    if (std::find(placements.order.begin(), placements.order.end(), player)
        == placements.order.end()) {
        placements.order.push_back(player);
        Emit("placement",
             json{{"player", PlayerUsername(store, player)},
                  {"place", placements.order.size()}});
    }

    // INFO: `winner` stays empty until the race ends; mid-match finishers are
    //       read from the placements.
    if (placements.order.size() < race_target) {
        AdvanceTurn();
        return;
    }
    EndRace(placements);
}

bool MatchInstance::EndRaceIfTargetReached() {
    if (finished_ || assembly_ == nullptr) return false;
    ecs::EntityStore& store = assembly_->store;
    const uint32_t race_target = ops::RaceTarget(store);
    if (race_target == 0) return false;
    ecs::Placements* placements =
        store.Get<ecs::Placements>(assembly_->registries.match);
    if (placements == nullptr || placements->order.size() < race_target) {
        return false;
    }
    EndRace(*placements);
    return true;
}

void MatchInstance::EndRace(ecs::Placements& placements) {
    ecs::EntityStore& store = assembly_->store;

    // INFO: everyone still holding cards lost; they rank below the finishers
    //       by fewest cards left, seat order breaking ties.
    std::vector<ecs::Entity> remaining;
    for (ecs::Entity seat : ops::PlayersBySeat(store)) {
        if (std::find(placements.order.begin(), placements.order.end(), seat)
            == placements.order.end()) {
            remaining.push_back(seat);
        }
    }
    std::stable_sort(remaining.begin(), remaining.end(),
                     [&store](ecs::Entity a, ecs::Entity b) {
                         return ops::HandOf(store, a).size()
                                < ops::HandOf(store, b).size();
                     });
    placements.order.insert(placements.order.end(), remaining.begin(),
                            remaining.end());

    winner_ = placements.order.front();
    finished_ = true;
    EmitMatchEnd();
}

void MatchInstance::EmitMatchEnd() {
    // INFO: `match_end` closes the match. The settings snapshot is the
    //       deck settings retained at assembly (the session; was an empty
    //       object until the assembly carried it).
    json end_data = json{{"settings", assembly_->Deck().settings}};
    Before("match_end", end_data);
    After("match_end", end_data);
    Emit("match_end", end_data);
}

// --- the engine response-window path
// -------------------------------------------

modload::PlayAttempt MatchInstance::BuildPlayAttempt(ecs::Entity player,
                                                     ecs::Entity card,
                                                     bool in_turn) const {
    ecs::EntityStore& store = assembly_->store;
    const MatchRegistries& registries = assembly_->registries;

    modload::PlayAttempt attempt;
    attempt.player = PlayerUsername(store, player);
    const ecs::CardIdentity* identity = store.Get<ecs::CardIdentity>(card);
    attempt.card_kind = identity == nullptr ? std::string() : identity->kind_id;
    attempt.in_turn = in_turn;

    json context = json::object();
    if (const std::optional<ecs::Entity> top = TopDiscard();
        top.has_value()) {
        if (const ecs::CardIdentity* top_id =
                store.Get<ecs::CardIdentity>(*top)) {
            context["top_kind"] = top_id->kind_id;
        }
    }
    if (const ecs::ActiveTypeReq* active =
            store.Get<ecs::ActiveTypeReq>(registries.match)) {
        context["active_type"] =
            active->type.has_value() ? *active->type : std::string();
    } else {
        context["active_type"] = std::string();
    }
    json hand_kinds = json::array();
    if (const ecs::Hand* hand = store.Get<ecs::Hand>(player)) {
        for (ecs::Entity held : hand->cards) {
            if (const ecs::CardIdentity* held_id =
                    store.Get<ecs::CardIdentity>(held)) {
                hand_kinds.push_back(held_id->kind_id);
            }
        }
    }
    context["hand"] = std::move(hand_kinds);
    attempt.context = std::move(context);
    return attempt;
}

modload::PlayDecision MatchInstance::CheckPlayRestrictions(
    const modload::PlayAttempt& attempt) const {
    return MakePlayEvaluator().Check(attempt);
}

bool MatchInstance::DrawnCardPlayable(ecs::Entity player,
                                      ecs::Entity card) const {
    // INFO: `CheckPlayRestrictions` is a pure probe (it only evaluates the
    //       ordered pipeline; it never emits `play_rejected` or mutates the
    //       store), so it is safe to ask mid-draw.
    // INFO: accepted divergence - the hold gates on the full restriction
    //       pipeline (`CheckPlayRestrictions`), whereas `force_play` and
    //       `progressive` gate on `EvalDrawnCardPlayable`
    //       (`condition_eval.cpp`). The two predicates are intentionally not
    //       converged.
    return CheckPlayRestrictions(BuildPlayAttempt(player, card, true)).allowed;
}

PlayEvaluator MatchInstance::MakePlayEvaluator() const {
    return PlayEvaluator(*this);
}

bool MatchInstance::ResponseEligible(
    const nlohmann::json& respond_with,
    const modload::PlayAttempt& attempt) const {
    return MakePlayEvaluator().IsEligible(respond_with, attempt);
}

std::string MatchInstance::ResponseDigest(
    const resolver::WindowRequest& request, ecs::Entity card) const {
    if (!request.filter_digest.empty()) return request.filter_digest;

    const json& filter = request.respond_with;
    if (filter.is_object()) {
        const auto condition = filter.find("condition");
        if (condition != filter.end() && condition->is_object()
            && condition->size() == 1) {
            return condition->begin().key();
        }
        if (filter.size() == 1
            && modload::IsPlayConditionKeyword(filter.begin().key())) {
            return filter.begin().key();
        }
        const auto any_tag = filter.find("any_tag");
        if (any_tag != filter.end()) {
            const auto facts = assembly_->card_facts.find(
                ops::CardKindId(assembly_->store, card));
            if (facts != assembly_->card_facts.end()) {
                if (any_tag->is_string()) {
                    return any_tag->get<std::string>();
                }
                if (any_tag->is_array()) {
                    for (const json& tag : *any_tag) {
                        if (!tag.is_string()) continue;
                        const std::string wanted = tag.get<std::string>();
                        if (std::find(facts->second.tags.begin(),
                                      facts->second.tags.end(), wanted)
                            != facts->second.tags.end()) {
                            return wanted;
                        }
                    }
                }
            }
        }
        const auto tag = filter.find("tag");
        if (tag != filter.end() && tag->is_string()) {
            return tag->get<std::string>();
        }
    }
    if (request.on_response.size() == 1) {
        return request.on_response.begin()->first;
    }
    return std::string();
}

std::string MatchInstance::WinningRoute(const resolver::WindowRequest& request,
                                        ecs::Entity card) const {
    const std::string digest = ResponseDigest(request, card);
    if (!digest.empty()) {
        const auto it = request.on_response.find(digest);
        if (it != request.on_response.end() && !it->second.empty()) {
            return it->second;
        }
    }
    return request.default_route;
}

void MatchInstance::OpenWindow(WindowPause pause, bool fresh_situation) {
    ecs::EntityStore& store = assembly_->store;
    const ecs::Entity match = assembly_->registries.match;

    ecs::WindowState* window = store.Get<ecs::WindowState>(match);
    if (window == nullptr) {
        store.Add(match, ecs::WindowState{});
        window = store.Get<ecs::WindowState>(match);
    }
    if (window == nullptr) return;

    window->window_id = ++next_window_id_;
    window->responders = pause.request.responders;
    // INFO: the group's responders are the union of its members'.
    for (const WindowPause& follower : pause.followers) {
        for (ecs::Entity responder : follower.request.responders) {
            if (std::find(window->responders.begin(),
                          window->responders.end(), responder)
                == window->responders.end()) {
                window->responders.push_back(responder);
            }
        }
    }
    window->default_route = pause.request.default_route;
    window->filter_digest = pause.request.filter_digest;
    window->kind = pause.request.kind;

    // INFO: Opening suspends the turn clock and arms the window
    //       duration; the turn remainder is restored on close.
    const std::optional<ecs::Entity> current = CurrentPlayer();
    // INFO: a merged group lasts as long as its longest member, and holds for
    //       its shortest jump_in member.
    const int64_t default_window_ms = timers_.Window().Config().window_ms;
    std::optional<int64_t> duration_override = pause.request.duration_ms;
    int64_t longest = 0;
    std::optional<int64_t> hold;
    for (std::size_t member = 0; member < pause.MemberCount(); ++member) {
        const resolver::WindowRequest& request = pause.Member(member).request;
        const int64_t member_ms =
            request.duration_ms.value_or(default_window_ms);
        longest = std::max(longest, member_ms);
        if (request.kind == ecs::kJumpInWindowKind) {
            hold = hold.has_value() ? std::min(*hold, member_ms) : member_ms;
        }
    }
    if (!pause.followers.empty()) duration_override = longest;
    const match::WindowDuration duration = timers_.OpenWindow(
        store, match, current.value_or(ecs::Entity{}), duration_override);
    window->opened_ms = Now();
    window->hold_ms = std::min(hold.value_or(0), duration.duration_ms);
    window->duration_ms = duration.duration_ms;

    json responders = json::array();
    for (ecs::Entity responder : window->responders) {
        responders.push_back(PlayerUsername(store, responder));
    }
    json kinds = json::array();
    for (std::size_t member = 0; member < pause.MemberCount(); ++member) {
        kinds.push_back(pause.Member(member).request.kind);
    }
    json body = json{{"id", window->window_id},
                     {"node", pause.request.node_id},
                     {"responders", std::move(responders)},
                     {"respond_with", pause.request.respond_with},
                     {"default_route", window->default_route},
                     {"filter_digest", window->filter_digest},
                     {"deadline_ms", window->deadline_ms},
                     {"duration_ms", duration.duration_ms},
                     {"kind", window->kind},
                     {"kinds", std::move(kinds)}};
    if (window->hold_ms > 0) body["hold_ms"] = window->hold_ms;
    // INFO: park before dispatching `window_open` so a hook that pauses cannot
    //       recursively open a second window from the same pause.
    pending_window_ = std::move(pause);

    // INFO: A stacking window opened by a draw-penalty play
    //       records that play's N as debt; a re-opened window counts its
    //       response instead (CommitWinningPlay), so it passes false. The
    //       flag keeps a second window over the same play (e.g. a jump-in
    //       window deferring this one) from recording it twice.
    if (fresh_situation && last_play_.has_value()
        && !last_play_->penalty_recorded
        && GroupStacksPenalty(*pending_window_, last_play_->card)) {
        last_play_->penalty_recorded = true;
        RecordDrawPenalty(last_play_->card, last_play_->player);
    }

    json hook_data = json{{"window", body}};
    Before("window_open", hook_data);
    After("window_open", hook_data);
    Emit("window_open", body);
}

void MatchInstance::OpenWindowGroup(std::vector<WindowPause> windows) {
    if (windows.empty()) return;
    WindowPause group = std::move(windows.front());
    for (std::size_t index = 1; index < windows.size(); ++index) {
        group.followers.push_back(std::move(windows[index]));
    }
    OpenWindow(std::move(group));
}

void MatchInstance::CommitWinningPlay(ecs::Entity player, ecs::Entity card,
                                      bool stacks_penalty) {
    ecs::EntityStore& store = assembly_->store;
    if (!store.IsAlive(card)) return;

    const std::optional<ecs::ZoneRef> from = ops::FindCardZone(store, card);
    const std::optional<uint32_t> ordinal = ops::CardOrdinal(store, card);
    const json from_zone = from.has_value() ? ZoneJson(*from) : json(nullptr);
    const json to_zone =
        ZoneJson(ecs::ZoneRef{ecs::ZoneKind::kDiscardPile, ecs::Entity{}});

    json zone_data = json{{"card", EntityJson(card)},
                          {"from", from_zone},
                          {"to", to_zone}};
    Before("card_left_zone", zone_data);
    Before("card_entered_zone", zone_data);
    ops::MoveCardToZone(
        store, card,
        ecs::ZoneRef{ecs::ZoneKind::kDiscardPile, ecs::Entity{}});
    After("card_left_zone", zone_data);
    After("card_entered_zone", zone_data);

    Emit("card_left_zone", json{{"card", EntityJson(card)},
                                {"from", from_zone},
                                {"to", to_zone}});
    Emit("card_entered_zone", json{{"card", EntityJson(card)},
                                   {"from", from_zone},
                                   {"to", to_zone}});

    const ecs::CardIdentity* identity = store.Get<ecs::CardIdentity>(card);
    const std::string kind =
        identity == nullptr ? std::string() : identity->kind_id;
    Emit("card_played",
         json{{"player", PlayerUsername(store, player)},
              {"card", EntityJson(card)},
              {"kind", kind},
              {"from_ordinal", ordinal.value_or(0)}});
    last_play_ =
        LastPlay{player, card, static_cast<uint32_t>(ordinal.value_or(0))};

    // INFO: Each response appends its own N to the debt, which
    //       passes on to the seat after the responder.
    if (stacks_penalty) {
        last_play_->penalty_recorded = true;
        RecordDrawPenalty(card, player);
    }
    // INFO: the winning response's own behavior graph is not re-drained; the
    //       window's `on_response` route is the resolution continuation.
}

bool MatchInstance::WindowReopens(
    const resolver::WindowRequest& request) const {
    if (!request.raw.is_object()) return false;
    if (request.raw.value("reopen", false)) return true;
    const auto window = request.raw.find("window");
    if (window != request.raw.end() && window->is_object()) {
        return window->value("reopen", false);
    }
    return false;
}

bool MatchInstance::GroupStacksPenalty(const WindowPause& group,
                                       ecs::Entity card) const {
    for (std::size_t member = 0; member < group.MemberCount(); ++member) {
        if (WindowStacksPenalty(group.Member(member).request, card)) {
            return true;
        }
    }
    return false;
}

std::optional<std::size_t> MatchInstance::StackingMember(
    const WindowPause& group, ecs::Entity card) const {
    for (std::size_t member = 0; member < group.MemberCount(); ++member) {
        if (WindowStacksPenalty(group.Member(member).request, card)
            && WindowReopens(group.Member(member).request)) {
            return member;
        }
    }
    return std::nullopt;
}

bool MatchInstance::WindowStacksPenalty(
    const resolver::WindowRequest& request, ecs::Entity card) const {
    const std::string kind = ops::CardKindId(assembly_->store, card);
    if (kind.empty() || !request.respond_with.is_object()) return false;
    const auto tags = request.respond_with.find("any_tag");
    if (tags == request.respond_with.end() || !tags->is_array()) return false;
    for (const json& tag : *tags) {
        if (tag.is_string()
            && ops::CardHasTag(&assembly_->registries, kind,
                               tag.get<std::string>())) {
            return true;
        }
    }
    return false;
}

int32_t MatchInstance::DrawPenaltyMagnitude(ecs::Entity card) const {
    ecs::EntityStore& store = assembly_->store;
    const ecs::FaceSpec* face = store.Get<ecs::FaceSpec>(card);
    if (face == nullptr) return 0;
    // INFO: legacy labels are `+2` and `jolly_draw4`; take the last digit run
    //       so both forms yield their N. An unlabelled card yields 0.
    int32_t magnitude = 0;
    bool digit = false;
    int32_t run = 0;
    for (char c : face->label) {
        if (c >= '0' && c <= '9') {
            run = run * 10 + (c - '0');
            if (run > 1000) run = 1000;
            digit = true;
        } else if (digit) {
            magnitude = run;
            run = 0;
            digit = false;
        }
    }
    if (digit) magnitude = run;
    return magnitude;
}

int32_t MatchInstance::RemoveDrawDebt(std::optional<ecs::Entity> keep) {
    ecs::EntityStore& store = assembly_->store;
    int32_t removed_total = 0;
    for (ecs::Entity holder : store.EntitiesWith<ecs::PlayerInfo>()) {
        if (keep.has_value() && holder == *keep) continue;
        for (const ecs::Status& removed :
             status::Remove(store, holder, ops::kDrawDebtStatusId)) {
            removed_total += removed.magnitude;
            json payload = json{{"target", EntityJson(holder)},
                                {"status_kind", removed.status_id},
                                {"instance", removed.instance_id}};
            events_.push_back(ops::MakeEvent("status_removed", payload));
            BridgeRunEvent(events_.back());
        }
    }
    return removed_total;
}

void MatchInstance::RecordDrawPenalty(ecs::Entity card, ecs::Entity player) {
    ecs::EntityStore& store = assembly_->store;
    if (!store.IsAlive(card) || assembly_->resolver == nullptr) return;
    const std::optional<ecs::Entity> victim =
        assembly_->resolver->NextInTurnOrder(player);
    if (!victim.has_value()) return;
    const ecs::Entity target = *victim;

    const std::string kind = ops::CardKindId(store, card);
    if (kind.empty()
        || !ops::CardHasTag(&assembly_->registries, kind, "draw_penalty")) {
        return;
    }

    const int32_t magnitude = DrawPenaltyMagnitude(card);
    if (magnitude <= 0) return;

    const int32_t carried = RemoveDrawDebt(target);

    status::ApplyRequest request;
    request.status_id = std::string(ops::kDrawDebtStatusId);
    request.magnitude = magnitude + carried;
    request.has_stack_policy = true;
    request.stack_policy = ecs::StackPolicy::kAccumulate;
    const status::ApplyResult applied =
        status::Apply(store, target, request);
    if (!applied.applied) return;

    json payload = json{{"target", EntityJson(target)},
                        {"status_kind", applied.status_id},
                        {"magnitude", applied.magnitude},
                        {"instance", applied.instance_id}};
    events_.push_back(ops::MakeEvent("status_applied", payload));
    BridgeRunEvent(events_.back());
}

void MatchInstance::CloseWindowRoute(const std::string& outcome) {
    if (!pending_window_.has_value() || assembly_ == nullptr) return;
    WindowPause group = std::move(*pending_window_);
    pending_window_.reset();

    ecs::EntityStore& store = assembly_->store;
    const ecs::Entity match = assembly_->registries.match;
    ecs::WindowState* window = store.Get<ecs::WindowState>(match);
    const uint32_t window_id = window == nullptr ? 0 : window->window_id;

    // INFO: a response on a debt-carrying play is resolved as
    //       a stack response by the responder, whichever member accepted it
    //       (a jump-in included): the stacking member owns the route, so the
    //       carried debt accumulates, the turn is redirected and the window
    //       re-opens. Any other winner supersedes the play instead.
    std::size_t route_member = group.winner_member;
    bool stacks = false;
    if (group.has_winner && store.IsAlive(group.winner_card)) {
        const bool debt_pending =
            last_play_.has_value() && last_play_->penalty_recorded;
        const std::optional<std::size_t> stacker =
            StackingMember(group, group.winner_card);
        if (debt_pending && stacker.has_value()) route_member = *stacker;
        stacks = WindowStacksPenalty(group.Member(route_member).request,
                                     group.winner_card);
        // INFO: a non-stacking win supersedes the play, so the debt the group
        //       recorded for it is stale and must not linger. With the shipped
        //       mods no jump-in on a debt-carrying play reaches this: it
        //       stacks above.
        if (!stacks && debt_pending) RemoveDrawDebt(std::nullopt);
        CommitWinningPlay(group.winner, group.winner_card, stacks);
    }

    // INFO: a winning response supersedes the play: only its owning member
    //       routes (`on_response`). Without a winner every member runs its
    //       `default_route` in member order (the debt victim draws).
    std::vector<QueuedRoute> routes;
    const auto queue_member = [&](std::size_t index, const std::string& route) {
        QueuedRoute queued{group.Member(index), route};
        queued.member.followers.clear();
        queued.member.settle_play = group.settle_play;
        queued.member.actor = group.actor;
        queued.member.has_winner = group.has_winner;
        queued.member.winner = group.winner;
        queued.member.winner_card = group.winner_card;
        routes.push_back(std::move(queued));
    };
    if (group.has_winner) {
        const WindowPause& owner = group.Member(route_member);
        queue_member(route_member,
                     WinningRoute(owner.request, group.winner_card));
    } else {
        for (std::size_t index = 0; index < group.MemberCount(); ++index) {
            queue_member(index, group.Member(index).request.default_route);
        }
    }
    const std::string closing_route = routes.front().route;

    // INFO: `Tick` already closes and resumes on timeout/early close; an
    //       explicit all-pass/response close does both here.
    if (window != nullptr && window->open) {
        const std::optional<ecs::Entity> current = CurrentPlayer();
        timers_.CloseWindow(store, match, current.value_or(ecs::Entity{}));
    }

    json body = json{
        {"id", window_id},
        {"outcome", outcome},
        {"winner", group.has_winner
                       ? json(PlayerUsername(store, group.winner))
                       : json(nullptr)},
        {"route", closing_route}};
    if (group.has_winner && store.IsAlive(group.winner_card)) {
        body["card"] = EntityJson(group.winner_card);
    }
    json hook_data = json{{"window", body}};
    Before("window_close", hook_data);
    After("window_close", hook_data);
    Emit("window_close", body);

    // INFO: routes queued by an enclosing group wait behind this window's.
    group_routes_.insert(group_routes_.begin(),
                         std::make_move_iterator(routes.begin()),
                         std::make_move_iterator(routes.end()));
    RunGroupRoutes(group.settle_play, group.actor);
}

void MatchInstance::RunGroupRoutes(bool settle_play, ecs::Entity actor) {
    while (!group_routes_.empty()) {
        QueuedRoute queued = std::move(group_routes_.front());
        group_routes_.erase(group_routes_.begin());
        const bool last = group_routes_.empty();
        const WindowPause& pause = queued.member;
        if (pause.system_index >= assembly_->systems.size()) continue;

        const modload::BehaviorGraph& graph =
            assembly_->systems[pause.system_index].graph;
        resolver::SelectorContext context = pause.context;
        if (pause.has_winner) context.responder = pause.winner;
        context.in_window = true;
        ops::ResolutionFrame frame;
        const resolver::ResolveResult result =
            assembly_->resolver->ResumeWindow(graph, pause.mod_id, context,
                                              frame, pause.pause,
                                              queued.route);

        if (!last) {
            const bool parked =
                result.status == resolver::ResolveStatus::kNeedsInput
                || result.status == resolver::ResolveStatus::kWindow
                || result.status == resolver::ResolveStatus::kSchedule;
            if (parked) {
                // INFO: the remaining routes resume once the pause resolves.
                AppendResult(result, pause.system_index, pause.mod_id,
                             context, frame, settle_play, actor);
                return;
            }
            AppendEvents(result);
            if (finished_) {
                group_routes_.clear();
                return;
            }
            continue;
        }

        // INFO: Window chaining. A window that declares `reopen` and
        //       collected a winning response re-opens after that response's
        //       route drains, so the next responder may stack. The re-open
        //       resumes through `ResumeWindow`, so the chain keeps one budget
        //       ledger.
        if (pause.has_winner && WindowReopens(pause.request)
            && result.status == resolver::ResolveStatus::kComplete
            && !result.aborted && !result.disarmed) {
            AppendEvents(result);
            if (!Paused() && !finished_) {
                resolver::SelectorContext reopen_context = pause.context;
                reopen_context.responder = pause.winner;
                reopen_context.self = pause.winner;
                reopen_context.target = pause.winner;
                reopen_context.in_window = true;
                ops::ResolutionFrame reopen_frame;
                const resolver::ResolveResult reopened =
                    assembly_->resolver->ResumeWindow(
                        graph, pause.mod_id, reopen_context, reopen_frame,
                        pause.pause, pause.request.node_id);
                if (reopened.status == resolver::ResolveStatus::kWindow
                    && reopened.window.has_value()) {
                    WindowPause next;
                    next.request = *reopened.window;
                    next.pause = reopened;
                    next.context = reopen_context;
                    next.system_index = pause.system_index;
                    next.mod_id = pause.mod_id;
                    next.settle_play = pause.settle_play;
                    next.actor = pause.actor;
                    OpenWindow(std::move(next), /*fresh_situation=*/false);
                    return;
                }
                AppendResult(reopened, pause.system_index, pause.mod_id,
                             reopen_context, reopen_frame, pause.settle_play,
                             pause.actor);
                return;
            }
            return;
        }

        AppendResult(result, pause.system_index, pause.mod_id, context, frame,
                     settle_play, actor);
    }
}

bool MatchInstance::CanRespondWindow(ecs::Entity player,
                                     ecs::Entity card) const {
    if (!started_ || finished_ || assembly_ == nullptr) return false;
    if (!pending_window_.has_value()) return false;

    const ecs::EntityStore& store = assembly_->store;
    const ecs::WindowState* window =
        store.Get<ecs::WindowState>(assembly_->registries.match);
    if (window == nullptr || !window->open) return false;

    const auto filters = WindowFilters();
    const PlayEvaluator::WindowView view{window->responders,
                                         window->responses, filters};
    return MakePlayEvaluator().CanRespond(view, player, card);
}

bool MatchInstance::RespondWindow(const std::string& username,
                                  ecs::Entity card) {
    if (!started_ || finished_ || assembly_ == nullptr) return false;
    if (!pending_window_.has_value()) return false;

    ecs::EntityStore& store = assembly_->store;
    ecs::WindowState* window =
        store.Get<ecs::WindowState>(assembly_->registries.match);
    if (window == nullptr || !window->open) return false;

    const std::optional<ecs::Entity> player = FindPlayer(username);
    if (!player.has_value()) return false;
    if (std::find(window->responders.begin(), window->responders.end(),
                  *player)
        == window->responders.end()) {
        return false;
    }
    for (const ecs::WindowResponse& existing : window->responses) {
        if (existing.responder == *player) return false;
    }
    if (!store.IsAlive(card) || !store.Has<ecs::CardIdentity>(card)) {
        return false;
    }
    const ecs::Hand* hand = store.Get<ecs::Hand>(*player);
    if (hand == nullptr
        || std::find(hand->cards.begin(), hand->cards.end(), card)
               == hand->cards.end()) {
        return false;
    }

    const std::optional<ecs::Entity> current = CurrentPlayer();
    const bool in_turn = current.has_value() && (*current == *player);
    modload::PlayAttempt attempt = BuildPlayAttempt(*player, card, in_turn);
    attempt.responding = true;

    const PlayEvaluator evaluator = MakePlayEvaluator();
    const modload::PlayDecision decision = evaluator.Check(attempt);
    if (!decision.allowed) {
        Emit("play_rejected",
             json{{"player", username}, {"reason_id", decision.reason_id}});
        return false;
    }
    // INFO: the first member (mod load order) whose filter accepts the card
    //       owns the response and supplies its `on_response` route.
    std::optional<std::size_t> owner;
    for (std::size_t member = 0; member < pending_window_->MemberCount();
         ++member) {
        const resolver::WindowRequest& request =
            pending_window_->Member(member).request;
        if (std::find(request.responders.begin(), request.responders.end(),
                      *player)
                != request.responders.end()
            && evaluator.IsEligible(request.respond_with, attempt)) {
            owner = member;
            break;
        }
    }
    if (!owner.has_value()) return false;

    json play_data =
        json{{"card", EntityJson(card)},
             {"player", EntityJson(*player)},
             {"from", ZoneJson(
                          ecs::ZoneRef{ecs::ZoneKind::kHand, *player})}};
    if (Before("play", play_data)) return false;

    const bool winning = !pending_window_->has_winner;
    ecs::WindowResponse response;
    response.responder = *player;
    response.card = card;
    response.pass = false;
    response.arrival_seq = pending_window_->next_arrival++;
    window->responses.push_back(response);
    if (winning) {
        pending_window_->has_winner = true;
        pending_window_->winner = *player;
        pending_window_->winner_card = card;
        pending_window_->winner_member = *owner;
    }

    const ecs::CardIdentity* identity = store.Get<ecs::CardIdentity>(card);
    Emit("window_response",
         json{{"window", window->window_id},
              {"player", username},
              {"card", EntityJson(card)},
              {"kind", identity == nullptr ? std::string()
                                           : identity->kind_id},
              {"pass", false},
              {"outcome", winning ? "winning" : "lost"}});

    if (timers_.Window().AllResponded(*window) || WindowWinnerReady()) {
        CloseWindowRoute("response");
    }
    return true;
}

bool MatchInstance::PassWindow(const std::string& username) {
    if (!started_ || finished_ || assembly_ == nullptr) return false;
    if (!pending_window_.has_value()) return false;

    ecs::EntityStore& store = assembly_->store;
    ecs::WindowState* window =
        store.Get<ecs::WindowState>(assembly_->registries.match);
    if (window == nullptr || !window->open) return false;

    const std::optional<ecs::Entity> player = FindPlayer(username);
    if (!player.has_value()) return false;
    if (std::find(window->responders.begin(), window->responders.end(),
                  *player)
        == window->responders.end()) {
        return false;
    }
    for (const ecs::WindowResponse& existing : window->responses) {
        if (existing.responder == *player) return false;
    }

    // INFO: a held group (it has a jump_in member) is passable only by the
    //       debt victim and only once the hold has elapsed; the rule keys on
    //       the hold and the victim, never on the group kind. The pass closes
    //       the group at once, so the default routes draw the recorded debt
    //       exactly as a timeout would.
    const bool held = window->hold_ms > 0;
    if (held && (!HoldsDrawDebt(*player) || !WindowHoldElapsed())) {
        return false;
    }

    ecs::WindowResponse response;
    response.responder = *player;
    response.pass = true;
    response.arrival_seq = pending_window_->next_arrival++;
    window->responses.push_back(response);
    Emit("window_response", json{{"window", window->window_id},
                                 {"player", username},
                                 {"pass", true},
                                 {"outcome", "pass"}});

    if (held || timers_.Window().AllResponded(*window)) {
        CloseWindowRoute(pending_window_->has_winner ? "response"
                         : held                      ? "pass"
                                                     : "all_pass");
    }
    return true;
}

bool MatchInstance::WindowWinnerReady() const {
    return WindowOpen() && pending_window_->has_winner
           && WindowHoldMs() > 0 && WindowHoldElapsed();
}

bool MatchInstance::HoldsDrawDebt(ecs::Entity player) const {
    const ecs::Status* debt =
        status::Find(assembly_->store, player, ops::kDrawDebtStatusId);
    return debt != nullptr && debt->magnitude > 0;
}

int64_t MatchInstance::WindowHoldMs() const {
    if (!WindowOpen()) return 0;
    return assembly_->store.Get<ecs::WindowState>(assembly_->registries.match)
        ->hold_ms;
}

bool MatchInstance::WindowHoldElapsed() const {
    const int64_t hold_ms = WindowHoldMs();
    if (hold_ms == 0) return true;
    const ecs::WindowState* window =
        assembly_->store.Get<ecs::WindowState>(assembly_->registries.match);
    return Now() - window->opened_ms >= hold_ms;
}

bool MatchInstance::WindowOpen() const {
    if (!pending_window_.has_value() || assembly_ == nullptr) return false;
    const ecs::WindowState* window =
        assembly_->store.Get<ecs::WindowState>(assembly_->registries.match);
    return window != nullptr && window->open;
}

nlohmann::json MatchInstance::ExportWindow() const {
    if (assembly_ == nullptr || !pending_window_.has_value()) {
        return json::object();
    }
    const ecs::EntityStore& store = assembly_->store;
    const ecs::WindowState* window =
        store.Get<ecs::WindowState>(assembly_->registries.match);
    if (window == nullptr) return json::object();

    json responders = json::array();
    for (ecs::Entity responder : window->responders) {
        responders.push_back(PlayerUsername(store, responder));
    }
    json responses = json::array();
    bool winner_seen = false;
    for (const ecs::WindowResponse& response : window->responses) {
        json entry =
            json{{"player", PlayerUsername(store, response.responder)},
                 {"pass", response.pass},
                 {"arrival_seq", response.arrival_seq}};
        if (response.pass) {
            entry["outcome"] = "pass";
        } else if (!winner_seen) {
            winner_seen = true;
            entry["outcome"] = "winning";
        } else {
            entry["outcome"] = "lost";
        }
        if (!response.pass && store.IsAlive(response.card)) {
            entry["card"] = EntityJson(response.card);
            const ecs::CardIdentity* identity =
                store.Get<ecs::CardIdentity>(response.card);
            entry["kind"] = identity == nullptr ? std::string()
                                                : identity->kind_id;
        }
        responses.push_back(std::move(entry));
    }

    json kinds = json::array();
    for (std::size_t member = 0; member < pending_window_->MemberCount();
         ++member) {
        kinds.push_back(pending_window_->Member(member).request.kind);
    }
    json exported = json{{"open", window->open},
                         {"id", window->window_id},
                         {"responders", std::move(responders)},
                         {"default_route", window->default_route},
                         {"filter_digest", window->filter_digest},
                         {"kind", window->kind},
                         {"kinds", std::move(kinds)},
                         {"deadline_ms", window->deadline_ms},
                         {"duration_ms", window->duration_ms},
                         {"respond_with",
                          pending_window_->request.respond_with},
                         {"responses", std::move(responses)}};
    if (window->hold_ms > 0) exported["hold_ms"] = window->hold_ms;
    return exported;
}

// --- the engine hook / resolver path
// -------------------------------------------

bool MatchInstance::Paused() const {
    return pending_input_.has_value() || pending_window_.has_value();
}

bool MatchInstance::Before(const char* name, nlohmann::json& data) {
    if (assembly_ == nullptr) return false;
    ecs::HookPayload payload;
    payload.hook = ecs::HookId{name, ecs::HookPhase::kBefore};
    payload.data = std::move(data);
    const ecs::HookDispatchResult result =
        assembly_->bus.DispatchBefore(payload.hook, payload);
    data = std::move(payload.data);
    CollectRuns();
    return result.vetoed;
}

void MatchInstance::After(const char* name, const nlohmann::json& data) {
    if (assembly_ == nullptr) return;
    ecs::HookPayload payload;
    payload.hook = ecs::HookId{name, ecs::HookPhase::kAfter};
    payload.data = data;
    assembly_->bus.DispatchAfter(payload.hook, payload);
    CollectRuns();
}

void MatchInstance::CollectRuns() {
    if (assembly_ == nullptr || collecting_runs_) return;
    collecting_runs_ = true;

    std::vector<WindowPause> forming_windows;
    while (collected_runs_ < assembly_->runs.size()) {
        const HookRun run = assembly_->runs[collected_runs_++];

        for (const json& event : run.events) {
            events_.push_back(event);
            BridgeRunEvent(event);
        }

        if (run.status == resolver::ResolveStatus::kNeedsInput
            && run.input_request.has_value()) {
            if (!pending_input_.has_value()) {
                InputPause pending;
                pending.target =
                    run.input_request->target.value_or(ecs::Entity{});
                pending.has_target = run.input_request->target.has_value();
                pending.kind = run.input_request->kind;
                pending.payload = run.input_request->payload;
                pending.pause.status = run.status;
                pending.pause.input_request = run.input_request;
                pending.pause.resume = run.resume;
                pending.context = run.context;
                pending.system_index = run.system_index;
                pending.mod_id = run.mod_id;
                pending_input_ = std::move(pending);
            }
        } else if (run.status == resolver::ResolveStatus::kWindow
                   && run.window.has_value()) {
            // INFO: Must-apply auto cards fire before a window
            //       opens; the engine then opens and times the window.
            if (forming_windows.empty()) RunMustApply("window");
            WindowPause window;
            window.request = *run.window;
            window.pause.status = run.status;
            window.pause.resume = run.resume;
            window.context = run.context;
            window.system_index = run.system_index;
            window.mod_id = run.mod_id;
            if (!pending_window_.has_value()
                && !pending_input_.has_value()) {
                // INFO: windows resolved by the same dispatch merge into one
                //       group, opened when its last member is collected.
                forming_windows.push_back(std::move(window));
                const bool more_windows =
                    collected_runs_ < assembly_->runs.size()
                    && assembly_->runs[collected_runs_].status
                           == resolver::ResolveStatus::kWindow
                    && assembly_->runs[collected_runs_].window.has_value();
                if (!more_windows) {
                    OpenWindowGroup(std::move(forming_windows));
                    forming_windows.clear();
                }
            } else {
                // INFO: Two-pause fix - a window resolving in the same
                //       dispatch as a parked op input is queued, not dropped;
                //       AppendResult opens it once the input resolves.
                deferred_windows_.push_back(std::move(window));
            }
        } else if (run.status == resolver::ResolveStatus::kSchedule
                   && run.schedule.has_value()) {
            // INFO: A schedule node defers its `next` subgraph;
            //       The timer layer arms it and Tick / the event drivers run it
            //       once the duration elapses.
            ArmSchedule(*run.schedule, run.system_index, run.mod_id,
                        run.context);
        }

        // INFO: A `play_card` op emits its play request as an effect
        //       descriptor; queue it for the flow-safe point that executes it.
        QueueForcedPlays(run.effects);

        // INFO: `effect_applied` observes a resolved op graph. The engine
        //       reports the run's source as the op summary; per-op node
        //       granularity would need the Resolver to surface it.
        if (run.hook.name != "effect_applied") {
            json effect = json{{"op", run.source_id},
                               {"mod", run.mod_id},
                               {"hook", run.hook.name},
                               {"args", json::object()},
                               {"targets", json::array()}};
            After("effect_applied", effect);
        }
    }

    for (WindowPause& window : forming_windows) {
        deferred_windows_.push_back(std::move(window));
    }
    collecting_runs_ = false;
}

void MatchInstance::BridgeRunEvent(const nlohmann::json& event) {
    if (!event.is_object()) return;
    const std::string type = event.value("type", std::string());
    const json payload =
        event.contains("payload") ? event["payload"] : json::object();

    // INFO: op-emitted events map onto their observation hooks so mod
    //       systems can react without the ops re-implementing dispatch.
    if (type == "status_applied" || type == "status_removed"
        || type == "visibility_granted" || type == "visibility_revoked") {
        json data = payload;
        Before(type.c_str(), data);
        After(type.c_str(), data);
        return;
    }
    if (type == "roll_result") {
        json data = json{{"spec", payload.contains("spec")
                                      ? payload["spec"]
                                      : json::object()},
                         {"outcome", payload.contains("outcomes")
                                         ? payload["outcomes"]
                                         : json()}};
        Before("roll", data);
        After("roll", data);
    }
}

void MatchInstance::RunMustApply(const char* trigger) {
    (void)trigger;
    if (assembly_ == nullptr || must_apply_active_ || Paused()) return;
    must_apply_active_ = true;

    const uint32_t cap = assembly_->resolver->Config().must_apply_cap;
    std::vector<ecs::Entity> played;
    for (uint32_t depth = 0; depth < cap; ++depth) {
        const std::optional<ecs::Entity> card = FindMustApplyCard(played);
        if (!card.has_value()) break;
        played.push_back(*card);
        if (!AutoPlayCard(*card, trigger)) break;
        if (Paused()) break;
    }

    must_apply_active_ = false;
}

std::optional<ecs::Entity> MatchInstance::FindMustApplyCard(
    const std::vector<ecs::Entity>& played) {
    ecs::EntityStore& store = assembly_->store;
    for (ecs::Entity card : store.EntitiesWith<ecs::AutoTrigger>()) {
        if (!store.IsAlive(card)) continue;
        // INFO: A must-apply card plays itself out of a hand. A
        //       card in any other zone is not a candidate; this also stops a
        //       discarded card with a still-true condition from re-firing on
        //       later trigger passes (the `played` vector is per-pass only).
        const ecs::InZone* zone = store.Get<ecs::InZone>(card);
        if (zone == nullptr || zone->zone.kind != ecs::ZoneKind::kHand) {
            continue;
        }
        if (std::find(played.begin(), played.end(), card) != played.end()) {
            continue;
        }
        if (AutoConditionMatches(card)) return card;
    }
    return std::nullopt;
}

bool MatchInstance::AutoConditionMatches(ecs::Entity card) {
    ecs::EntityStore& store = assembly_->store;
    const ecs::AutoTrigger* trigger = store.Get<ecs::AutoTrigger>(card);
    if (trigger == nullptr || !trigger->must_apply) return false;
    if (trigger->condition.is_null()) return false;

    resolver::SelectorContext context;
    context.card = card;
    context.in_card_context = true;
    if (const ecs::InZone* zone = store.Get<ecs::InZone>(card);
        zone != nullptr && zone->zone.kind == ecs::ZoneKind::kHand) {
        context.self = zone->zone.owner;
    } else {
        context.self = CurrentPlayer();
    }

    ops::ResolutionFrame frame;
    BindConditionSelectors(context, frame);
    ops::OpContext ctx(assembly_->bus, assembly_->budget, frame);
    ctx.registries = &assembly_->registries;
    return assembly_->conditions.Evaluate(store, trigger->condition, ctx);
}

bool MatchInstance::AutoPlayCard(ecs::Entity card, const char* trigger) {
    ecs::EntityStore& store = assembly_->store;
    const ecs::AutoTrigger* auto_trigger = store.Get<ecs::AutoTrigger>(card);
    if (auto_trigger == nullptr) return false;

    std::optional<ecs::Entity> owner;
    const std::optional<ecs::ZoneRef> zone = ops::FindCardZone(store, card);
    if (zone.has_value() && zone->kind == ecs::ZoneKind::kHand) {
        owner = zone->owner;
    } else {
        owner = CurrentPlayer();
    }

    const json from_zone = zone.has_value() ? ZoneJson(*zone)
                                            : json(nullptr);
    const json to_zone =
        ZoneJson(ecs::ZoneRef{ecs::ZoneKind::kDiscardPile, ecs::Entity{}});
    const json player_json =
        owner.has_value() ? EntityJson(*owner) : json(nullptr);

    json zone_data = json{{"card", EntityJson(card)},
                          {"from", from_zone},
                          {"to", to_zone}};
    Before("card_left_zone", zone_data);
    Before("card_entered_zone", zone_data);
    ops::MoveCardToZone(store, card,
                        ecs::ZoneRef{ecs::ZoneKind::kDiscardPile,
                                     ecs::Entity{}});
    After("card_left_zone", zone_data);
    After("card_entered_zone", zone_data);

    // INFO: run the auto-trigger graph with must-apply semantics so the
    //       Resolver's `must_apply` depth cap bounds re-triggering.
    modload::BehaviorGraph graph;
    graph.raw = auto_trigger->graph;
    if (auto_trigger->graph.is_object()
        && auto_trigger->graph.contains("nodes")) {
        graph.nodes = auto_trigger->graph["nodes"];
    }

    resolver::SelectorContext context;
    context.self = owner;
    context.card = card;
    context.in_card_context = true;
    ops::ResolutionFrame frame;
    const resolver::ResolveResult result = assembly_->resolver->Resolve(
        graph, std::string(), context, frame, std::string(), true);
    for (const json& event : result.events) {
        events_.push_back(event);
        BridgeRunEvent(event);
    }

    if (result.status == resolver::ResolveStatus::kNeedsInput
        && result.input_request.has_value() && !pending_input_.has_value()) {
        InputPause pending;
        pending.target = result.input_request->target.value_or(ecs::Entity{});
        pending.has_target = result.input_request->target.has_value();
        pending.kind = result.input_request->kind;
        pending.payload = result.input_request->payload;
        pending.pause = result;
        pending.context = context;
        pending.frame = frame;
        pending.system_index = assembly_->systems.size();
        pending_input_ = std::move(pending);
    }

    Emit("card_played", json{{"player", player_json},
                             {"card", EntityJson(card)}});
    Emit("auto_played", json{{"player", player_json},
                             {"card", EntityJson(card)},
                             {"trigger", trigger}});
    CollectRuns();
    return true;
}

void MatchInstance::AppendEvents(const resolver::ResolveResult& result) {
    for (const json& event : result.events) {
        events_.push_back(event);
        BridgeRunEvent(event);
    }
    CollectRuns();
}

void MatchInstance::AppendResult(const resolver::ResolveResult& result,
                                 std::size_t system_index,
                                 const std::string& mod_id,
                                 const resolver::SelectorContext& context,
                                 const ops::ResolutionFrame& frame,
                                 bool settle_play, ecs::Entity actor) {
    AppendEvents(result);

    if (result.status == resolver::ResolveStatus::kNeedsInput
        && result.input_request.has_value()) {
        InputPause next;
        next.target = result.input_request->target.value_or(ecs::Entity{});
        next.has_target = result.input_request->target.has_value();
        next.kind = result.input_request->kind;
        next.payload = result.input_request->payload;
        next.pause = result;
        next.context = context;
        next.frame = frame;
        next.system_index = system_index;
        next.mod_id = mod_id;
        next.settle_play = settle_play;
        next.actor = actor;
        pending_input_ = std::move(next);
        return;
    }
    if (result.status == resolver::ResolveStatus::kWindow
        && result.window.has_value()) {
        WindowPause next;
        next.request = *result.window;
        next.pause = result;
        next.context = context;
        next.system_index = system_index;
        next.mod_id = mod_id;
        next.settle_play = settle_play;
        next.actor = actor;
        OpenWindow(std::move(next), /*fresh_situation=*/false);
        return;
    }
    if (result.status == resolver::ResolveStatus::kSchedule
        && result.schedule.has_value()) {
        // INFO: a resumed graph that hits another schedule arms it rather than
        //       dropping the continuation.
        ArmSchedule(*result.schedule, system_index, mod_id, context);
        if (!Paused() && !group_routes_.empty()) {
            RunGroupRoutes(settle_play, actor);
        }
        return;
    }

    // INFO: a deferred window owns the flow from here; do not settle past it.
    if (Paused()) return;
    // INFO: member routes of a closed window group resume before the play
    //       settles.
    if (!group_routes_.empty()) {
        RunGroupRoutes(settle_play, actor);
        return;
    }
    if (OpenDeferredWindow(settle_play, actor)) return;
    if (settle_play) SettleAfterPlay(actor);

    // INFO: the parked pause is resolved by now; run any elapsed schedule that
    //       was deferred behind it so it is never lost.
    if (!Paused()) DrainDeferredScheduled();
}

bool MatchInstance::OpenDeferredWindow(bool settle_play, ecs::Entity actor) {
    if (Paused() || finished_ || deferred_windows_.empty()) return false;
    // INFO: windows deferred behind one parked input belong to one play and
    //       open as a single merged group.
    std::vector<WindowPause> windows = std::move(deferred_windows_);
    deferred_windows_.clear();
    WindowPause group = std::move(windows.front());
    for (std::size_t index = 1; index < windows.size(); ++index) {
        group.followers.push_back(std::move(windows[index]));
    }
    group.settle_play = settle_play;
    group.actor = actor;
    OpenWindow(std::move(group), /*fresh_situation=*/true);
    return true;
}

// --- the engine scheduled graphs
// -------------------------------------------------

void MatchInstance::ArmSchedule(const resolver::ScheduleRequest& schedule,
                                std::size_t system_index,
                                const std::string& mod_id,
                                const resolver::SelectorContext& context) {
    if (assembly_ == nullptr) return;

    const std::optional<match::Duration> duration =
        match::ParseDuration(schedule.duration);
    if (!duration.has_value()) {
        // INFO: fail-safe - a malformed duration must never crash or silently
        //       leave a dangling continuation.
        Logger::Warn("[MatchInstance] schedule node '", schedule.node_id,
                     "' has an unparsable duration; not armed");
        return;
    }

    // INFO: the envelope is the JSON the Scheduler stores and returns on
    //       expiry; it carries everything a fresh Resolve chain needs.
    const json envelope =
        json{{"system_index", system_index},
             {"mod_id", mod_id},
             {"resume_node", schedule.resume_node},
             {"pending", schedule.pending},
             {"self", EntityOrNull(context.self)},
             {"target", EntityOrNull(context.target)},
             {"responder", EntityOrNull(context.responder)},
             {"card", EntityOrNull(context.card)},
             {"in_window", context.in_window},
             {"in_card_context", context.in_card_context}};

    const std::optional<uint32_t> id = scheduler_.Arm(
        assembly_->store, assembly_->registries.match, envelope, *duration);
    if (!id.has_value()) {
        Logger::Warn("[MatchInstance] schedule node '", schedule.node_id,
                     "' could not be armed");
    }
}

void MatchInstance::ExecuteScheduled(
    const std::vector<nlohmann::json>& envelopes) {
    if (assembly_ == nullptr || envelopes.empty()) return;

    // INFO: an elapsed schedule must never be dropped. If a pause is already
    //       parked, queue the envelopes and run them once it resolves
    //       (mirrors `deferred_windows_`); otherwise run them now.
    if (Paused()) {
        deferred_scheduled_.insert(deferred_scheduled_.end(),
                                   envelopes.begin(), envelopes.end());
        return;
    }
    RunScheduled(envelopes);
}

void MatchInstance::RunScheduled(
    const std::vector<nlohmann::json>& envelopes) {
    for (std::size_t i = 0; i < envelopes.size(); ++i) {
        if (Paused()) {
            // INFO: a parked pause owns the flow; keep the remaining
            //       envelopes in arm order and run them after it resolves.
            deferred_scheduled_.insert(deferred_scheduled_.end(),
                                       envelopes.begin() + i, envelopes.end());
            return;
        }
        RunScheduledEnvelope(envelopes[i]);
    }
    // INFO: A scheduled subgraph may emit `play_card`; run the queue
    //       from a flow-safe point, exactly like the `after:draw` driver.
    if (!Paused()) ExecuteForcedPlays();
}

void MatchInstance::RunScheduledEnvelope(const nlohmann::json& envelope) {
    if (!envelope.is_object()) return;
    const std::size_t system_index =
        envelope.value("system_index", std::size_t{0});
    if (system_index >= assembly_->systems.size()) {
        Logger::Warn("[MatchInstance] scheduled graph for unknown system ",
                     system_index, "; dropped");
        return;
    }
    const ModSystem& system = assembly_->systems[system_index];
    const std::string mod_id = envelope.value("mod_id", system.mod_id);
    const std::string resume_node =
        envelope.value("resume_node", std::string());

    std::vector<std::string> pending;
    if (const auto it = envelope.find("pending");
        it != envelope.end() && it->is_array()) {
        pending = it->get<std::vector<std::string>>();
    }

    resolver::SelectorContext context;
    context.self = EnvelopeEntity(envelope, "self");
    context.target = EnvelopeEntity(envelope, "target");
    context.responder = EnvelopeEntity(envelope, "responder");
    context.card = EnvelopeEntity(envelope, "card");
    context.in_window = envelope.value("in_window", false);
    context.in_card_context = envelope.value("in_card_context", false);

    // INFO: a schedule resumes as a FRESH chain (ResumeToken doc), so no
    //       chain budget is reused; the result feeds the same pause path.
    ops::ResolutionFrame frame;
    const resolver::ResolveResult result = assembly_->resolver->Resolve(
        system.graph, mod_id, context, frame, resume_node,
        /*must_apply=*/false, pending);
    AppendResult(result, system_index, mod_id, context, frame,
                 /*settle_play=*/false, ecs::Entity{});

    // INFO: `AppendResult`/`AppendEvents` only carry `result.events`; queue
    //       the effects here so a scheduled `play_card` is not lost.
    QueueForcedPlays(result.effects);
}

void MatchInstance::DrainDeferredScheduled() {
    if (Paused() || deferred_scheduled_.empty()) return;
    std::vector<nlohmann::json> queued = std::move(deferred_scheduled_);
    deferred_scheduled_.clear();
    RunScheduled(queued);
}

int64_t MatchInstance::Now() const { return timers_.Turn().Now(); }

void MatchInstance::BindConditionSelectors(
    const resolver::SelectorContext& context, ops::ResolutionFrame& frame) {
    ecs::EntityStore& store = assembly_->store;
    if (context.self.has_value()) frame.BindSelector("@self", {*context.self});
    if (context.target.has_value()) {
        frame.BindSelector("@target", {*context.target});
    }
    if (context.card.has_value()) frame.BindSelector("@card", {*context.card});
    if (const std::optional<ecs::Entity> match = ops::FindMatch(store);
        match.has_value()) {
        frame.BindSelector("@match", {*match});
    }
    if (const std::optional<ecs::Entity> draw =
            ops::FindPile(store, ecs::PileKind::kDraw);
        draw.has_value()) {
        frame.BindSelector("@draw_pile", {*draw});
    }
    if (const std::optional<ecs::Entity> discard =
            ops::FindPile(store, ecs::PileKind::kDiscard);
        discard.has_value()) {
        frame.BindSelector("@discard_pile", {*discard});
    }
    const std::vector<ecs::Entity> players = ops::ActivePlayersBySeat(store);
    frame.BindSelector("@all_players", players);
    std::vector<ecs::Entity> others;
    others.reserve(players.size());
    for (ecs::Entity player : players) {
        if (!context.self.has_value() || !(player == *context.self)) {
            others.push_back(player);
        }
    }
    frame.BindSelector("@others", std::move(others));
    if (const std::optional<ecs::Entity> current =
            ops::FindCurrentPlayer(store);
        current.has_value()) {
        frame.BindSelector("@current_player", {*current});
    }
}

// --- the engine forced-play routing
// ----------------------------------------------

void MatchInstance::QueueForcedPlays(const std::vector<json>& effects) {
    for (const json& effect : effects) {
        if (!effect.is_object()) continue;
        if (effect.value("type", std::string()) != "play_card") continue;
        const json payload =
            effect.contains("payload") && effect["payload"].is_object()
                ? effect["payload"]
                : json::object();
        const std::optional<ecs::Entity> player =
            EntityFromJson(payload.contains("player") ? payload["player"]
                                                      : json());
        const std::optional<ecs::Entity> card =
            EntityFromJson(payload.contains("card") ? payload["card"]
                                                    : json());
        if (!player.has_value() || !card.has_value()) continue;
        forced_plays_.push_back(ForcedPlay{*player, *card});
    }
}

bool MatchInstance::ExecuteForcedPlays() {
    bool executed = false;
    // INFO: a forced play's own graphs may queue further requests; drain in
    //       bounded passes so a mis-authored loop cannot hang the engine.
    constexpr int kForcedPlayCap = 16;
    for (int pass = 0; pass < kForcedPlayCap && !forced_plays_.empty();
         ++pass) {
        std::vector<ForcedPlay> queued = std::move(forced_plays_);
        forced_plays_.clear();
        for (const ForcedPlay& play : queued) {
            if (finished_ || assembly_ == nullptr || Paused()) break;
            const std::string username =
                PlayerUsername(assembly_->store, play.player);
            if (username.empty()) continue;
            if (PlayCard(username, play.card)) executed = true;
        }
        if (finished_ || Paused()) break;
    }
    return executed;
}

// --- read-only accessors ---------------------------------------------------

bool MatchInstance::IsMatchOver() const { return finished_; }

std::string MatchInstance::GetWinner() const {
    if (!winner_.has_value()) return std::string();
    const ecs::EntityStore& store = assembly_->store;
    return PlayerUsername(store, *winner_);
}

std::vector<std::string> MatchInstance::GetPlacements() const {
    std::vector<std::string> names;
    if (assembly_ == nullptr) return names;
    const ecs::EntityStore& store = assembly_->store;
    const MatchRegistries& registries = assembly_->registries;
    const ecs::Placements* placements =
        store.Get<ecs::Placements>(registries.match);
    if (placements == nullptr) return names;
    names.reserve(placements->order.size());
    for (ecs::Entity player : placements->order) {
        names.push_back(PlayerUsername(store, player));
    }
    return names;
}

std::string MatchInstance::GetMode() const {
    return GetRaceTarget() > 0 ? "race" : "standard";
}

uint32_t MatchInstance::GetRaceTarget() const {
    if (assembly_ == nullptr) return 0;
    const ecs::MatchMeta* meta =
        assembly_->store.Get<ecs::MatchMeta>(assembly_->registries.match);
    return meta == nullptr ? 0 : meta->race_target;
}

std::optional<ecs::Entity> MatchInstance::GetCurrentPlayer() const {
    return CurrentPlayer();
}

std::string MatchInstance::GetCurrentPlayerUsername() const {
    const std::optional<ecs::Entity> current = CurrentPlayer();
    if (!current.has_value()) return std::string();
    const ecs::EntityStore& store = assembly_->store;
    return PlayerUsername(store, *current);
}

std::optional<ecs::Entity> MatchInstance::FindPlayer(
    const std::string& username) const {
    if (assembly_ == nullptr) return std::nullopt;
    const ecs::EntityStore& store = assembly_->store;
    const MatchRegistries& registries = assembly_->registries;
    for (ecs::Entity player : registries.players) {
        if (PlayerUsername(store, player) == username) return player;
    }
    return std::nullopt;
}

std::optional<ecs::Entity> MatchInstance::CurrentPlayer() const {
    if (assembly_ == nullptr) return std::nullopt;
    const ecs::EntityStore& store = assembly_->store;
    const MatchRegistries& registries = assembly_->registries;
    for (ecs::Entity player : registries.players) {
        const ecs::TurnState* turn = store.Get<ecs::TurnState>(player);
        if (turn != nullptr && turn->is_current) return player;
    }
    return std::nullopt;
}

std::optional<ecs::Entity> MatchInstance::TopDiscard() const {
    if (assembly_ == nullptr) return std::nullopt;
    const ecs::EntityStore& store = assembly_->store;
    const MatchRegistries& registries = assembly_->registries;
    const ecs::PileContents* discard =
        store.Get<ecs::PileContents>(registries.discard_pile);
    if (discard == nullptr || discard->cards.empty()) return std::nullopt;
    return discard->cards.back();
}

json MatchInstance::ExportState() const {
    if (assembly_ == nullptr) {
        return json{{"status", "finished"}, {"started", false}};
    }

    const ecs::EntityStore& store = assembly_->store;
    const MatchRegistries& registries = assembly_->registries;

    json out = json::object();
    out["status"] = finished_ ? "finished" : "playing";
    out["started"] = started_;

    const ecs::MatchMeta* meta =
        store.Get<ecs::MatchMeta>(registries.match);
    out["round"] = meta == nullptr ? 0u : meta->round;
    out["direction"] =
        meta == nullptr ? 1 : static_cast<int>(meta->direction);

    const ecs::ActiveTypeReq* active =
        store.Get<ecs::ActiveTypeReq>(registries.match);
    out["active_type"] = (active != nullptr && active->type.has_value())
                             ? json(*active->type)
                             : json(nullptr);

    out["current_player"] = GetCurrentPlayerUsername();
    out["winner"] = GetWinner();
    out["placements"] = GetPlacements();
    out["mode"] = GetMode();
    out["race_target"] = GetRaceTarget();

    const ecs::PileContents* draw =
        store.Get<ecs::PileContents>(registries.draw_pile);
    const ecs::PileContents* discard =
        store.Get<ecs::PileContents>(registries.discard_pile);
    out["draw_pile_size"] = draw == nullptr ? 0 : draw->cards.size();
    out["discard_pile_size"] =
        discard == nullptr ? 0 : discard->cards.size();

    if (const std::optional<ecs::Entity> top = TopDiscard();
        top.has_value()) {
        const ecs::CardIdentity* identity =
            store.Get<ecs::CardIdentity>(*top);
        const std::string kind =
            identity == nullptr ? std::string() : identity->kind_id;
        json top_json = json{{"kind", kind}};
        const auto facts = assembly_->card_facts.find(kind);
        if (facts != assembly_->card_facts.end()) {
            top_json["color"] = facts->second.color;
            top_json["value"] = facts->second.value;
        }
        out["top_card"] = std::move(top_json);
    } else {
        out["top_card"] = nullptr;
    }

    if (last_play_.has_value()) {
        const ecs::CardIdentity* identity =
            store.Get<ecs::CardIdentity>(last_play_->card);
        out["last_play"] = json{
            {"player", PlayerUsername(store, last_play_->player)},
            {"card", identity == nullptr ? std::string()
                                         : identity->kind_id},
            {"hand_ordinal", last_play_->hand_ordinal}};
    } else {
        out["last_play"] = nullptr;
    }

    json players = json::array();
    for (ecs::Entity player : registries.players) {
        const ecs::PlayerInfo* info =
            store.Get<ecs::PlayerInfo>(player);
        const ecs::Hand* hand = store.Get<ecs::Hand>(player);
        const ecs::TurnState* turn =
            store.Get<ecs::TurnState>(player);
        players.push_back(
            json{{"username", info == nullptr ? std::string()
                                              : info->username},
                 {"seat", info == nullptr ? 0u : info->seat},
                 {"is_bot", info != nullptr && info->is_bot},
                 {"card_count", hand == nullptr ? 0 : hand->cards.size()},
                 {"is_current", turn != nullptr && turn->is_current}});
    }
    out["players"] = std::move(players);
    return out;
}

bool MatchInstance::OfferedChoice(int64_t bits) const {
    if (!pending_input_.has_value()) return false;
    const json& payload = pending_input_->payload;
    if (!payload.is_object()) return false;
    const auto it = payload.find("options");
    if (it == payload.end() || !it->is_array()) return false;
    for (const json& option : *it) {
        if (option.is_number_integer() && option.get<int64_t>() == bits) {
            return true;
        }
    }
    return false;
}

std::optional<nlohmann::json> MatchInstance::PendingInput() const {
    if (!pending_input_.has_value()) return std::nullopt;
    return json{{"kind", pending_input_->kind},
                {"target", EntityJson(pending_input_->target)},
                {"payload", pending_input_->payload},
                {"deadline_ms", pending_input_->deadline_ms},
                {"duration_ms", pending_input_->duration_ms > 0
                                    ? pending_input_->duration_ms
                                    : clock_duration_ms_}};
}

std::optional<resolver::WindowRequest> MatchInstance::PendingWindow() const {
    return pending_window_.has_value()
               ? std::optional<resolver::WindowRequest>(
                     pending_window_->request)
               : std::nullopt;
}

const std::optional<PendingPlayDrawn>&
MatchInstance::PendingPlayDrawnState() const {
    return awaiting_play_drawn_;
}

std::vector<PlayEvaluator::WindowView::Member> MatchInstance::WindowFilters()
    const {
    std::vector<PlayEvaluator::WindowView::Member> filters;
    if (!pending_window_.has_value()) return filters;
    for (std::size_t member = 0; member < pending_window_->MemberCount();
         ++member) {
        const resolver::WindowRequest& request =
            pending_window_->Member(member).request;
        filters.push_back({request.responders, request.respond_with});
    }
    return filters;
}

std::vector<nlohmann::json> MatchInstance::TakeEvents() {
    std::vector<nlohmann::json> drained = std::move(events_);
    events_.clear();
    return drained;
}

// --- private helpers -------------------------------------------------------

void MatchInstance::Emit(std::string_view type, nlohmann::json payload) {
    events_.push_back(ops::MakeEvent(type, std::move(payload)));
}

}  // namespace match::engine
