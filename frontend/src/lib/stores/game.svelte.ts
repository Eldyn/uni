/**
 * @file game.svelte.ts
 * @brief Reactive global store for managing the game table and turns.
 * Decodes the JSON packets from the WebSocket and updates the game engine UI.
 */

import { z } from "zod";
import type { SessionStore } from "$stores/sessionStore";
import { storeAudio } from "./audio.svelte";
import { storeAnalytics } from "./analytics.svelte";
import { storeNavigation } from "./navigation.svelte";
import { storeLobby } from "./lobby.svelte";
import { ClientAction, ServerAction, ws } from "./ws.svelte";
import { storeAuth } from "./auth.svelte";
import { storeSpectator } from "./spectator.svelte";
import { storeTableSpin } from "./tableSpin.svelte";
import { storeCardDefs, type KindFace } from "./cardDefs.svelte";
import {
	MatchEventPayloadSchema,
	PlayersReadyPayloadSchema,
	PromptClosePayloadSchema,
	PromptOpenPayloadSchema,
	TurnAdvancePayloadSchema,
	TypeMap,
	ValueMap,
	WindowClosePayloadSchema,
	WindowOpenPayloadSchema
} from "$lib/generated/schemas";
import type { PromptOpenPayload, WindowOpenPayload } from "$lib/generated/schemas";
import type { CardBus } from "$components/game/card-bus.svelte";
import { mapMatchEventPacket, type MatchEventBeat } from "./matchEventMap";

export const TYPE_MAP = TypeMap;

export type CardType = (typeof TypeMap)[number];
export type CardValue = (typeof ValueMap)[number];

// INFO: wire shape emitted by ViewBuilder::BuildSnapshot. Cards carry only the
// aspects the viewer's mask allows, so every card field stays optional.
const RawCardSchema = z.object({
	slot: z.number().int().optional(),
	card: z.number().int().optional(),
	kind: z.string().optional(),
	color: z.string().optional(),
	value: z.string().optional(),
	can_play: z.boolean().optional()
});

const RawPlayerSchema = z.object({
	username: z.string(),
	seat: z.number().int().optional(),
	is_current: z.boolean().optional(),
	card_count: z.number().int(),
	is_bot: z.boolean(),
	hand: z.array(RawCardSchema).optional(),
	statuses: z.array(z.unknown()).optional(),
	spectator_count: z.number().int().optional()
});

const RawGameStateSchema = z.object({
	status: z.string().optional(),
	round: z.number().int().optional(),
	direction: z.number().int().optional(),
	active_type: z.string().nullable().optional(),
	current_player: z.string(),
	winner: z.string().nullable().optional(),
	placements: z.array(z.string()).optional(),
	players: z.array(RawPlayerSchema),
	pending_draws: z.number().int().default(0),
	pending_play_drawn: z
		.object({ player: z.string(), card: z.number().int().optional() })
		.nullable()
		.optional(),
	draw_pile_size: z.number().int().default(0),
	discard_pile_size: z.number().int().default(0),
	last_play: z.object({ player: z.string(), hand_index: z.number().int() }).optional(),
	turn_time_remaining_ms: z.number().optional(),
	mode: z.string().optional(),
	race_target: z.number().int().optional(),
	spectator_count: z.number().int().optional(),
	draw_pile: z.object({ count: z.number().int() }).optional(),
	discard_pile: z.object({ count: z.number().int(), top: RawCardSchema.optional() }).optional(),
	window: z.unknown().optional(),
	prompts: z.array(z.unknown()).optional(),
	turn_deadline_ms: z.number().optional(),
	server_now_ms: z.number().optional(),
	prompt_wait: z.object({ deadline_ms: z.number(), duration_ms: z.number() }).optional(),
	seq_watermark: z.number().int().optional()
});

/**
 * @interface Card
 * @brief Frontend representation of a single card.
 */
export interface Card {
	/** The unique 16-bit identifier of the card. */
	id: number;
	/** The decoded type of the card (maps to CSS class). */
	type: CardType;
	/** The value or action associated with the card. */
	value: CardValue;
	/** Whether this card is currently playable (set by the server, only present in the local player's hand). */
	can_play?: boolean;
	/** Frozen kind id from the snapshot, e.g. `vanilla:red_5` (present when the
	 *  viewer's aspect mask exposes identity). */
	kind?: string;
	/** Declarative face resolved from the `defs` kind table via `kind`. */
	face?: KindFace;
}

/**
 * @interface GamePlayer
 * @brief Data associated with an in-game player.
 * If the player is not the current client, the `hand` array will be undefined and replaced by `card_count` computed by the server.
 */
export interface GamePlayer {
	/** Username of the player. */
	username: string;
	/** Total number of cards in the player's hand. */
	card_count: number;
	/** The physical cards in the player's hand (present only for the Local Player). */
	hand?: Card[];
	/** Indicates whether the player is a bot controlled by the server. */
	is_bot: boolean;
	/** How many connected spectators are watching THIS player's POV. Absent on
	 *  older state payloads; treated as 0. */
	spectator_count?: number;
	/** Visible statuses of the player, as sent by the snapshot. */
	statuses?: unknown[];
}

/**
 * @interface PendingPlayDrawn
 * @brief The voluntary draw currently held for a play/keep decision. The
 * `card` id is present only for the owner; every other viewer receives just
 * the `player`, so the drawn face is never leaked.
 */
export interface PendingPlayDrawn {
	/** Username of the player holding the decision. */
	player: string;
	/** Id of the drawn card — owner only. */
	card?: number;
}

/**
 * @interface GameState
 * @brief Snapshot of the complete table state at a precise instant.
 */
export interface GameState {
	/** Type currently active for plays. */
	active_type: string;
	/** Username of the player whose turn it currently is. */
	current_turn: string;
	/** Direction of play (1 for clockwise, -1 for counter-clockwise). */
	play_direction: number;
	/** The card currently on top of the discard pile. */
	top_card?: Card;
	/** The list of all the players present in the match. */
	players: GamePlayer[];
	/** Accumulated cards (+2/+4 chain) that the next player will have to draw. */
	pending_draws: number;
	/** Voluntary draw held for a play/keep decision, or null when none is live.
	 *  The card id is present only in the owner's snapshot. */
	pendingPlayDrawn: PendingPlayDrawn | null;
	/** How many cards remain in the draw pile — drives the pile's visible stack height and reshuffle detection. */
	draw_pile_size: number;
	/** How many cards sit in the discard pile. When the draw pile is empty this
	 *  tells the client whether a draw can still trigger a reshuffle (the engine
	 *  keeps the top discard card, so it needs more than one). Absent on older
	 *  state payloads; treated as 0. */
	discard_pile_size?: number;
	/** Origin of the last played card, used to animate it from its source slot. */
	last_play?: LastPlay;
	/** Mode of the match ('standard' | 'race'). */
	mode?: string;
	/** Finishers needed to end a race (0 outside race mode). */
	race_target?: number;
	/** Number of connected spectators. */
	spectator_count?: number;
	/** Current or final placement list in race mode, best-first. */
	placements?: string[];
	/** Flag indicating whether the match has reached a terminal state. */
	is_over?: boolean;
	/** Username of the winning player, if the match has ended. */
	winner?: string;
	/** Server sequence watermark of the snapshot that produced this state. */
	seq_watermark?: number;
	/** Raw open response window from the snapshot, consumed by slice A4. */
	window?: unknown;
	/** Raw pending op-input prompts from the snapshot, consumed by slice A4. */
	prompts?: unknown[];
	/** Absolute epoch-ms deadline for the current turn (0 when none). */
	turn_deadline_ms?: number;
}

/**
 * @interface LastPlay
 * @brief Describes who played the last card and from which slot of the hand.
 */
export interface LastPlay {
	/** Username of the player who made the play. */
	player: string;
	/** Index of the slot in the hand from which the card departed. */
	hand_index: number;
}

/**
 * @interface ActiveWindow
 * @brief Open response window mirrored from a `window_open` packet or snapshot.
 */
export interface ActiveWindow {
	/** Server-issued identifier of the open window. */
	windowId: string;
	/** Absolute deadline on the client clock (epoch ms). */
	deadlineAt: number;
	/** Total length of the group's clock in ms. */
	durationMs: number;
	/** Length of the hold from the open in ms; 0 when the group has none. */
	holdMs: number;
	/** Kind of every member of the merged group, in member order. */
	kinds: string[];
	/** Usernames eligible to respond. */
	responders: string[];
	/** Digest of the engine-side eligibility filter. */
	eligibleFilterDigest: string;
}

/** Which countdown the fuse line is currently draining. */
export type FuseTimerSource = "window" | "prompt" | "ready" | "turn";

/**
 * @interface ActiveTimer
 * @brief The one countdown the fuse line shows, normalised onto the client
 * clock so consumers never see the wire's mixed deadline conventions.
 */
export interface ActiveTimer {
	source: FuseTimerSource;
	/** Total length of the clock in ms. */
	durationMs: number;
	/** Absolute deadline on the client clock (epoch ms). */
	deadlineAt: number;
	/** Hold from the start of the clock in ms; 0 unless a held window. */
	holdMs: number;
	/** Window member kinds in member order; empty for every other source. */
	kinds: string[];
	/** Set on a prompt timer the local player does not own (they only wait). */
	observer?: true;
}

/** The window control the local player is offered, if any. */
export interface WindowAction {
	/** `draw` takes the debt at once; `pass` is the legacy any-responder pass. */
	kind: "draw" | "pass";
	enabled: boolean;
}

const DRAW_DEBT_STATUS_ID = "vanilla:draw_debt";

interface ClockSpan {
	durationMs: number;
	deadlineAt: number;
}

/** True when the player carries a positive `vanilla:draw_debt` status. */
function holdsDrawDebt(player: { statuses?: unknown[] } | null): boolean {
	return (player?.statuses ?? []).some((status) => {
		if (typeof status !== "object" || status === null) return false;
		const { status_kind, magnitude } = status as Record<string, unknown>;
		return status_kind === DRAW_DEBT_STATUS_ID && typeof magnitude === "number" && magnitude > 0;
	});
}

/**
 * @class StoreGame
 * @brief Synchronizes the frontend state with the server Game Engine in real time.
 */
class StoreGame implements SessionStore {
	/** The current state of the match (players, deck, cards on the table). */
	state = $state<GameState | null>(null);

	/** Highest `match_event` seq observed, or null before the first frame. */
	lastSeq = $state<number | null>(null);

	/** True after a seq gap, until the next snapshot reconciles the watermark. */
	desynced = $state(false);

	/** Beats mapped from `match_event` frames, held until the next snapshot is applied. */
	#pendingBeats: MatchEventBeat[] = [];

	/** Handlers registered via `onMatchEventBeat` (the animation controller). */
	#beatHandlers = new Set<(beat: MatchEventBeat) => void>();

	/** Handlers registered via `onDesync` (gap detection). */
	#desyncHandlers = new Set<() => void>();

	/** True while a game action is in flight, cleared on next MatchStateUpdated. */
	isActionPending = $state(false);

	/** Seconds remaining to complete the turn, computed locally. */
	turnTimeRemaining = $state<number>(15);

	/** Open response window mirrored from a `window_open` packet or snapshot. */
	activeWindow = $state<ActiveWindow | null>(null);

	/** Open op-input prompt for the local viewer, or null when none is live. */
	activePrompt = $state<PromptOpenPayload | null>(null);

	/** Clock of the open prompt on the client clock, or null when it has none. */
	#promptClock = $state<ClockSpan | null>(null);

	/** Clock of a prompt another seat is resolving, from the public snapshot. */
	#observedPromptClock = $state<ClockSpan | null>(null);

	/** Clock of the turn on the client clock, or null while it is suspended. */
	#turnClock = $state<ClockSpan | null>(null);

	/** Clock of the ready barrier on the client clock, or null when unarmed. */
	#readyClock = $state<ClockSpan | null>(null);

	/** Client clock minus server clock, refreshed by every snapshot. */
	#serverClockOffsetMs = 0;

	/** True once a fresh `match_start` frame has been consumed, until reset. */
	matchIntroPending = $state(false);

	/** Ready-barrier progress from the last `players_ready` frame, or null. */
	readyProgress = $state<{ ready: number; total: number } | null>(null);

	/** True once `match_begin` confirms every player has loaded. */
	matchBegun = $state(false);

	/** True once `match_client_ready` went out for the current match. */
	clientReadySent = $state(false);

	/** True when the match snapshot is live, the intro is pending and the
	 *  server has confirmed every player loaded. */
	get introReady(): boolean {
		return this.state !== null && this.matchIntroPending && this.matchBegun;
	}

	/** Reference to the browser's native `setInterval` timer. */
	#timerInterval: number | null = null;

	/** Safety timeout that releases isActionPending if the server stops responding. */
	#pendingSafetyTimer: ReturnType<typeof setTimeout> | null = null;

	/** Timestamp (ms) when this client entered the current match, for duration analytics. */
	#matchStartedAt: number | null = null;

	/** Timestamp (ms) when the current turn began, for per-turn pacing analytics. */
	#turnStartedAt: number | null = null;

	/** Duration (ms) of every completed human turn this match, for time_to_play_avg. */
	#humanTurnDurations: number[] = [];

	/** Completed turns observed this match, from snapshot turn transitions. */
	#turnCount = 0;

	/** Of `#turnCount`, turns taken by a human seat. */
	#humanTurnCount = 0;

	/** Longest completed human turn this match (ms). */
	#longestTurnMs = 0;

	/** Cards played this match by any seat, from card_played/auto_played beats. */
	#totalPlays = 0;

	/** Of `#totalPlays`, plays the engine forced (auto_played). */
	#autoPlays = 0;

	/** Cards drawn this match by any seat, from cards_drawn beats. */
	#totalDraws = 0;

	/** WebSocket drops the local client saw while this match was live. */
	#matchDisconnects = 0;

	/** Derived property to instantly identify the local player. */
	localPlayer = $derived(
		this.state?.players.find((p) => p.username === storeAuth.username) ?? null
	);

	/** True while the match is a live race and the local player has already
	 *  finished: the server keeps them seated (no `is_spectator` flag), so the
	 *  placements list is the only signal that they now just watch. */
	hasFinishedRace = $derived(
		this.state?.mode === "race" &&
			this.state.is_over !== true &&
			this.state.placements?.includes(storeAuth.username) === true
	);

	/** Derived property indicating whether client is a spectator (or finished).
	 *  All conditions are OR-ed, not `??`-ed: the backend marks an eliminated
	 *  player `is_spectator` in the LOBBY but only broadcasts match state, so the
	 *  client's lobby copy keeps a stale `false` — which would win a `??` and hide
	 *  that the player has left the match. Being absent from `state.players` is the
	 *  reliable signal (a shed player is erased from the rotation), so it must be
	 *  able to flip the result on its own. */
	isSpectator = $derived(
		(storeLobby.current?.members.find((m) => m.username === storeAuth.username)?.is_spectator ??
			false) ||
			(this.state !== null && !this.state.players.some((p) => p.username === storeAuth.username)) ||
			this.hasFinishedRace
	);

	/** True while the local player may answer the open response window, even
	 *  out of turn (a draw-stacking or jump-in reply). */
	isWindowResponder = $derived(
		!this.isSpectator &&
			this.activeWindow !== null &&
			this.localPlayer !== null &&
			this.activeWindow.responders.includes(this.localPlayer.username)
	);

	/** True while the local player holds draw debt: the only seat that may
	 *  pass (draw) inside a held window. */
	isDebtVictim = $derived(holdsDrawDebt(this.localPlayer));

	/** The single countdown the fuse line drains: the open window group, else
	 *  the open prompt, else the ready barrier, else the turn clock. */
	activeTimer = $derived.by((): ActiveTimer | null => {
		const window = this.activeWindow;
		if (window !== null) {
			return {
				source: "window",
				durationMs: window.durationMs,
				deadlineAt: window.deadlineAt,
				holdMs: window.holdMs,
				kinds: window.kinds
			};
		}
		if (this.activePrompt !== null && this.#promptClock !== null) {
			return { source: "prompt", ...this.#promptClock, holdMs: 0, kinds: [] };
		}
		if (this.activePrompt === null && this.#observedPromptClock !== null) {
			return {
				source: "prompt",
				...this.#observedPromptClock,
				holdMs: 0,
				kinds: [],
				observer: true
			};
		}
		if (!this.matchBegun && this.#readyClock !== null) {
			return { source: "ready", ...this.#readyClock, holdMs: 0, kinds: [] };
		}
		if (this.#turnClock !== null && !this.state?.is_over) {
			return { source: "turn", ...this.#turnClock, holdMs: 0, kinds: [] };
		}
		return null;
	});

	/** Number of connected spectators in the whole lobby (kept for legacy
	 *  callers; the HUD now shows the per-player count instead). */
	spectatorCount = $derived(this.state?.spectator_count ?? 0);

	/** How many spectators are watching the player currently in view: the
	 *  local player themselves, or — while spectating — whoever's POV is being
	 *  watched (explicit choice, else the current turn). This is the "per-user"
	 *  eye count, not the lobby total. */
	povSpectatorCount = $derived.by(() => {
		const players = this.state?.players ?? [];
		const name = this.isSpectator
			? (storeSpectator.viewedUsername ?? this.state?.current_turn ?? null)
			: (this.localPlayer?.username ?? null);
		if (!name) return 0;
		return players.find((p) => p.username === name)?.spectator_count ?? 0;
	});

	/** Current or final placement list in race mode, best-first. */
	placements = $derived(this.state?.placements ?? []);

	/** True for the tick where the local player's own finish just landed in
	 *  `placements` — used to fire the one-shot mid-match outcome banner
	 *  exactly once, not on every subsequent state update. */
	#previousPlacementsHadMe = false;
	justFinished = $derived.by(() => {
		const inPlacementsNow = this.placements.includes(storeAuth.username);
		const isNew = inPlacementsNow && !this.#previousPlacementsHadMe;
		this.#previousPlacementsHadMe = inPlacementsNow;
		return isNew;
	});

	/** Race outcome for the local player; null while they are not in
	 *  `placements` yet (still racing, or not a race).
	 *  Race is a shedding race: emptying your hand takes the next place and
	 *  WINS it. `placements` is best-first (the backend never reorders it) and a
	 *  player enters it the moment they shed their hand, so mid-match every
	 *  entry is a finisher ("win"). When the match ends the left-behind players
	 *  are appended below the finishers: only the first `race_target` places
	 *  win, the rest lose.
	 *  Corner: a mid-game quitter drops out of `players` without entering
	 *  `placements` — rare, accepted. */
	raceOutcome = $derived<"win" | "lose" | null>(
		(() => {
			const idx = this.placements.indexOf(storeAuth.username);
			if (idx === -1) return null;
			if (this.state?.is_over !== true) return "win";
			const finishers = this.state.race_target ?? 0;
			return finishers > 0 && idx + 1 > finishers ? "lose" : "win";
		})()
	);

	constructor() {
		// FIXED: Register handlers exactly once at store initialization.
		// They will safely survive any underlying WebSocket re-connections.
		this.#registerListeners();
		// Count connectivity drops while a match is live, so match_abandoned can
		// separate a flaky-network abort from a deliberate one. Optional call:
		// partial test doubles of the ws store may not implement onClose.
		ws.onClose?.(() => {
			if (this.state !== null) this.#matchDisconnects += 1;
		});
	}

	/**
	 * @brief Registers a callback for every mapped `match_event` beat. Beats are
	 * buffered on arrival and drained after the next snapshot is applied, so a
	 * handler always observes beats against the state they belong to.
	 * @returns An unsubscribe function.
	 */
	onMatchEventBeat(cb: (beat: MatchEventBeat) => void): () => void {
		this.#beatHandlers.add(cb);
		return () => {
			this.#beatHandlers.delete(cb);
		};
	}

	/**
	 * @brief Registers a callback fired when a `match_event` seq gap is detected.
	 * @returns An unsubscribe function.
	 */
	onDesync(cb: () => void): () => void {
		this.#desyncHandlers.add(cb);
		return () => {
			this.#desyncHandlers.delete(cb);
		};
	}

	/**
	 * @brief Stops the active timers and closes the match screen, returning to the lobby.
	 */
	returnToLobby() {
		this.#clearTimer();
		this.#matchStartedAt = null;
		this.#turnStartedAt = null;
		this.#humanTurnDurations = [];
		this.#resetMatchCounters();
		this.state = null;
		this.turnTimeRemaining = 0;
		this.#clearWindowState();
		this.#setActivePrompt(null);
		this.lastSeq = null;
		this.desynced = false;
		this.#pendingBeats = [];
		this.matchIntroPending = false;
		this.readyProgress = null;
		this.#readyClock = null;
		this.#observedPromptClock = null;
		this.#serverClockOffsetMs = 0;
		this.matchBegun = false;
		this.clientReadySent = false;
		storeSpectator.reset();
		storeTableSpin.reset();
		storeCardDefs.reset();
		storeNavigation.goto("lobby");
	}

	/**
	 * @brief Zeroes the per-match analytics counters. Called on match end and
	 * whenever the match is torn down, so no counter leaks into the next match.
	 */
	#resetMatchCounters(): void {
		this.#turnCount = 0;
		this.#humanTurnCount = 0;
		this.#longestTurnMs = 0;
		this.#totalPlays = 0;
		this.#autoPlays = 0;
		this.#totalDraws = 0;
		this.#matchDisconnects = 0;
	}

	/**
	 * @brief Registers the listeners for the WebSocket events related to the game.
	 * Intercepts the end of the match and the state updates sent by the server.
	 */
	#registerListeners() {
		ws.on(ServerAction.MatchOver, (data) => {
			if (!this.state) return;

			const winner = data.winner as string;
			const reason = data.reason as string | undefined;
			this.state.is_over = true;
			this.state.winner = winner;
			if (data.placements) {
				this.state.placements = data.placements as string[];
			}

			this.#clearTimer();
			this.#clearWindowState();
			this.#setActivePrompt(null);

			const duration_seconds =
				this.#matchStartedAt !== null
					? Math.round((Date.now() - this.#matchStartedAt) / 1000)
					: undefined;
			const time_to_play_avg_ms = this.#humanTurnDurations.length
				? Math.round(
						this.#humanTurnDurations.reduce((a, b) => a + b, 0) / this.#humanTurnDurations.length
					)
				: undefined;

			// MatchOver fires on every client; emit the analytics events only from
			// the host so a single match counts once (mirrors host-only match_start).
			if (storeLobby.current?.host === storeAuth.username) {
				const s = storeLobby.current?.settings;
				const active_mods = s?.active_mods ?? [];
				const settingsParams = {
					match_key: storeLobby.matchKey ?? undefined,
					mods: active_mods.join(",") || "none",
					mod_count: active_mods.length,
					starting_cards: s?.starting_cards,
					turn_time_limit_ms: s?.turn_time_limit_ms,
					bot_count: s?.bot_count,
					bot_mode: s?.bot_mode,
					allow_bot_takeover: s?.allow_bot_takeover,
					allow_bot_replacement: s?.allow_bot_replacement,
					quit_deletes_match: s?.quit_deletes_match,
					is_public: s?.is_public
				};
				// Match-depth metrics, observed by this client from snapshot turn
				// transitions and match_event beats (all seats, bots included).
				const depthParams = {
					match_key: storeLobby.matchKey ?? undefined,
					turn_count: this.#turnCount,
					human_turn_count: this.#humanTurnCount,
					cards_played: this.#totalPlays,
					auto_plays: this.#autoPlays,
					cards_drawn: this.#totalDraws,
					longest_turn_ms: this.#longestTurnMs || undefined
				};

				if (winner) {
					storeAnalytics.track("match_end", {
						duration_seconds,
						time_to_play_avg_ms,
						winner_is_bot: this.state.players.find((p) => p.username === winner)?.is_bot,
						...depthParams
					});
					storeAnalytics.track("match_settings", settingsParams);
				} else if (reason) {
					storeAnalytics.track("match_saved", { duration_seconds, ...depthParams });
					storeAnalytics.track("match_settings", settingsParams);
				} else {
					// No winner and no save: the match was aborted/deleted. Fired
					// host-only like the rest, with what the client can observe.
					// `host_disconnects` separates a flaky-network abort from a
					// deliberate one; a host who left before receiving this frame
					// fires nothing (same gap as match_end/match_saved).
					storeAnalytics.track("match_abandoned", {
						duration_seconds,
						player_count: this.state.players.length,
						human_count: this.state.players.filter((p) => !p.is_bot).length,
						host_disconnects: this.#matchDisconnects,
						...depthParams
					});
					storeAnalytics.track("match_settings", settingsParams);
				}
			}
			this.#matchStartedAt = null;
			this.#turnStartedAt = null;
			this.#humanTurnDurations = [];
			this.#resetMatchCounters();
		});

		ws.on(ServerAction.MatchStateUpdated, (data: any) => {
			this.#clearActionPending();
			const parsed = RawGameStateSchema.safeParse(data.match_state);
			if (!parsed.success) {
				console.error("[MatchStateUpdated] payload validation failed:", parsed.error.message);
				return;
			}
			const stateJson = parsed.data;
			if (typeof stateJson.server_now_ms === "number") {
				this.#serverClockOffsetMs = Date.now() - stateJson.server_now_ms;
			}

			const currentTurn = stateJson.current_player;
			const previousTurn = this.state?.current_turn;
			const previousPlayers = this.state?.players;
			if (previousTurn && previousTurn !== currentTurn) {
				this.#turnCount += 1;
				const previousPlayer = previousPlayers?.find((p) => p.username === previousTurn);
				if (previousPlayer && !previousPlayer.is_bot && this.#turnStartedAt !== null) {
					const turnMs = Date.now() - this.#turnStartedAt;
					this.#humanTurnCount += 1;
					this.#humanTurnDurations.push(turnMs);
					if (turnMs > this.#longestTurnMs) this.#longestTurnMs = turnMs;
				}
			}
			if (previousTurn !== currentTurn) {
				this.#turnStartedAt = Date.now();
			}

			this.state = {
				active_type: stateJson.active_type ?? "white",
				current_turn: currentTurn,
				play_direction: stateJson.direction ?? 1,
				top_card: stateJson.discard_pile?.top
					? this.#parseCard(stateJson.discard_pile.top)
					: undefined,
				players: stateJson.players.map((p) => ({
					...p,
					hand: p.hand ? p.hand.map((card) => this.#parseCard(card)) : undefined
				})),
				pending_draws: stateJson.pending_draws,
				pendingPlayDrawn: stateJson.pending_play_drawn ?? null,
				draw_pile_size: stateJson.draw_pile?.count ?? stateJson.draw_pile_size ?? 0,
				discard_pile_size: stateJson.discard_pile?.count ?? stateJson.discard_pile_size ?? 0,
				mode: stateJson.mode,
				race_target: stateJson.race_target,
				spectator_count: stateJson.spectator_count,
				placements: stateJson.placements,
				seq_watermark: stateJson.seq_watermark,
				window: stateJson.window,
				prompts: stateJson.prompts,
				turn_deadline_ms: stateJson.turn_deadline_ms
			};

			// INFO: the snapshot deadline is absolute server-clock epoch ms and is
			// the reconnect-safe source for the turn countdown; only clobber a
			// live timer when the snapshot actually carries one.
			if (typeof stateJson.turn_deadline_ms === "number" && stateJson.turn_deadline_ms > 0) {
				this.#syncTurnTimer(
					Math.max(0, this.#toClientClock(stateJson.turn_deadline_ms) - Date.now())
				);
			} else if (stateJson.turn_deadline_ms === 0) {
				// INFO: 0 means the turn clock is paused behind a prompt or a
				// response window; freeze the countdown until it resumes.
				this.#clearTimer();
			}

			// INFO: the snapshot window/prompts are reconnect truth
			// a packet-driven open/close may already have set them.
			const snapshotWindow =
				stateJson.window == null ? null : WindowOpenPayloadSchema.safeParse(stateJson.window);
			if (snapshotWindow?.success) {
				// INFO: the snapshot window deadline is absolute server-clock epoch ms.
				this.#setActiveWindow(
					snapshotWindow.data,
					this.#toClientClock(snapshotWindow.data.deadline_ms)
				);
			} else {
				this.#clearWindowState();
			}

			// INFO: prompt_wait is the public prompt clock (timer only), so seats
			// that do not own the prompt still see the wait.
			const promptWait = stateJson.prompt_wait;
			this.#observedPromptClock =
				promptWait !== undefined && promptWait.duration_ms > 0 && promptWait.deadline_ms > 0
					? {
							durationMs: promptWait.duration_ms,
							deadlineAt: this.#toClientClock(promptWait.deadline_ms)
						}
					: null;

			const snapshotPrompts = stateJson.prompts;
			if (snapshotPrompts && snapshotPrompts.length > 0) {
				const parsedPrompt = PromptOpenPayloadSchema.safeParse(snapshotPrompts[0]);
				// INFO: the snapshot prompt deadline is absolute server-clock epoch ms.
				this.#setActivePrompt(
					parsedPrompt.success ? parsedPrompt.data : null,
					parsedPrompt.success && parsedPrompt.data.deadline_ms > 0
						? this.#toClientClock(parsedPrompt.data.deadline_ms)
						: null
				);
			} else {
				this.#setActivePrompt(null);
			}

			// INFO: the snapshot reconciles the packet watermark. The wire value is
			// `EventSink::NextSeq()` — the seq the NEXT wrapped packet will carry,
			// and events are emitted before the snapshot — so the last emitted seq
			// is `wm - 1`. Storing it keeps the next frame (seq === wm) from
			// reading as a gap. A snapshot at or beyond the next expected seq
			// clears a desync; otherwise the buffered beats still drain.
			const wm = stateJson.seq_watermark;
			if (
				typeof wm === "number" &&
				(this.desynced || this.lastSeq === null || wm >= this.lastSeq + 1)
			) {
				this.lastSeq = wm > 0 ? wm - 1 : null;
				this.desynced = false;
			}

			// INFO: drain AFTER state is applied.
			const beats = this.#pendingBeats;
			this.#pendingBeats = [];
			for (const b of beats) {
				for (const h of this.#beatHandlers) h(b);
			}

			// INFO: the snapshot now carries the engine turn deadline; the
			// packet `turn_advance` only refreshes it when it is non-zero.

			if (storeNavigation.current === "lobby" || storeNavigation.initialScreen === "game") {
				this.#matchStartedAt = Date.now();
				storeNavigation.goto("game");
			}
		});

		// INFO: the single MatchEvent subscription — `ws.on` warns in
		// DEV for a non-exempt action with more than one handler.
		ws.on(ServerAction.MatchEvent, (data) => this.#onMatchEventFrame(data));

		// INFO: a rejected play/draw (e.g. an unplayable card) answers with a
		//       bare error frame and no state update, so without this the
		//       latch swallows every action until the 3 s safety timer fires.
		ws.on(ServerAction.Error, () => {
			if (this.isActionPending) this.#clearActionPending();
		});
	}

	/**
	 * @brief Handles one `match_event` frame: advances the seq watermark,
	 * raises a desync on a gap, and buffers the mapped beat for the next
	 * snapshot drain.
	 */
	#onMatchEventFrame(data: unknown) {
		const env = MatchEventPayloadSchema.safeParse(data);
		if (!env.success) return;

		const seq = env.data.seq;
		if (this.lastSeq !== null && seq !== this.lastSeq + 1) {
			this.desynced = true;
			// INFO: the snapshot reconciles state; drop the pre-gap backlog rather
			// than animating stale beats on top of it.
			this.#pendingBeats = [];
			for (const h of this.#desyncHandlers) h();
		}
		this.lastSeq = seq;

		switch (env.data.type) {
			case "defs": {
				// INFO: the full kind table arrives ahead of the first snapshot;
				// held unconfirmed until match_start proves the digest.
				storeCardDefs.ingestDefs(env.data.payload);
				break;
			}
			case "match_start": {
				storeCardDefs.confirmMatchStart(env.data.payload);
				this.matchIntroPending = true;
				this.readyProgress = null;
				this.#readyClock = null;
				this.matchBegun = false;
				this.clientReadySent = false;
				break;
			}
			case "players_ready": {
				const parsed = PlayersReadyPayloadSchema.safeParse(env.data.payload);
				if (parsed.success) {
					this.readyProgress = { ready: parsed.data.ready, total: parsed.data.total };
					const { timeout_ms } = parsed.data;
					if (timeout_ms !== undefined && timeout_ms > 0) {
						this.#readyClock = { durationMs: timeout_ms, deadlineAt: Date.now() + timeout_ms };
					}
				}
				break;
			}
			case "match_begin": {
				this.matchBegun = true;
				this.#readyClock = null;
				break;
			}
			case "turn_advance": {
				const parsed = TurnAdvancePayloadSchema.safeParse(env.data.payload);
				// INFO: turn_advance.deadline_ms is an ABSOLUTE epoch-ms value
				// (`TurnState.turn_deadline_ms`), not a remaining duration. The
				// engine emits the incoming player's not-yet-armed deadline as
				// 0, so never clobber a good countdown with it.
				if (parsed.success && parsed.data.deadline_ms > 0) {
					this.#syncTurnTimer(
						Math.max(0, this.#toClientClock(parsed.data.deadline_ms) - Date.now())
					);
				}
				break;
			}
			case "window_open": {
				const parsed = WindowOpenPayloadSchema.safeParse(env.data.payload);
				// INFO: window_open.deadline_ms is a REMAINING duration (the sink
				// forwards `duration_ms`); normalize to an absolute client deadline.
				if (parsed.success) {
					const deadlineAt = Date.now() + parsed.data.deadline_ms;
					this.#setActiveWindow(parsed.data, deadlineAt);
				}
				break;
			}
			case "window_close": {
				const parsed = WindowClosePayloadSchema.safeParse(env.data.payload);
				if (parsed.success) this.#clearWindowState();
				break;
			}
			case "prompt_open": {
				const parsed = PromptOpenPayloadSchema.safeParse(env.data.payload);
				if (parsed.success) {
					// INFO: the packet deadline_ms is only the op's declared timeout;
					// the enforced clock length is duration_ms, armed at emission.
					const durationMs = parsed.data.duration_ms ?? 0;
					this.#setActivePrompt(parsed.data, durationMs > 0 ? Date.now() + durationMs : null);
				}
				break;
			}
			case "prompt_close": {
				const parsed = PromptClosePayloadSchema.safeParse(env.data.payload);
				if (parsed.success && this.activePrompt?.prompt_id === parsed.data.prompt_id) {
					this.#setActivePrompt(null);
				}
				break;
			}
		}

		const beat = mapMatchEventPacket(data);
		if (beat) {
			// Tally match depth from the wire (all seats), not just local actions.
			if (beat.kind === "play") {
				this.#totalPlays += 1;
				if (beat.auto) this.#autoPlays += 1;
			} else if (beat.kind === "draw") {
				this.#totalDraws += beat.count;
			}
			this.#pendingBeats.push(beat);
		}
	}

	#clearActionPending() {
		if (this.#pendingSafetyTimer !== null) {
			clearTimeout(this.#pendingSafetyTimer);
			this.#pendingSafetyTimer = null;
		}
		this.isActionPending = false;
	}

	/**
	 * @brief Cancels and destroys the currently running turn timer.
	 */
	#clearTimer() {
		this.#turnClock = null;
		if (this.#timerInterval !== null) {
			clearInterval(this.#timerInterval);
			this.#timerInterval = null;
		}
	}

	/**
	 * @brief Sets the open prompt and the clock the server enforces on it.
	 * @param prompt Open prompt, or null to clear it.
	 * @param deadlineAt Absolute deadline on the client clock, or null when the
	 * prompt carries no clock.
	 */
	#setActivePrompt(prompt: PromptOpenPayload | null, deadlineAt: number | null = null) {
		this.activePrompt = prompt;
		this.#promptClock =
			prompt !== null && deadlineAt !== null
				? { durationMs: prompt.duration_ms ?? Math.max(0, deadlineAt - Date.now()), deadlineAt }
				: null;
	}

	/**
	 * @brief Mirrors an open response window from a packet or snapshot payload.
	 * @param deadlineAt Absolute deadline on the client clock (epoch ms).
	 */
	#setActiveWindow(windowPayload: WindowOpenPayload, deadlineAt: number) {
		this.activeWindow = {
			windowId: windowPayload.window_id,
			deadlineAt,
			durationMs: windowPayload.duration_ms ?? Math.max(0, deadlineAt - Date.now()),
			holdMs: windowPayload.hold_ms ?? 0,
			kinds: windowPayload.kinds ?? (windowPayload.kind ? [windowPayload.kind] : []),
			responders: windowPayload.responders,
			eligibleFilterDigest: windowPayload.eligible_filter_digest
		};
	}

	/**
	 * @brief Clears the open response window.
	 */
	#clearWindowState() {
		this.activeWindow = null;
	}

	/** Maps a server-clock epoch-ms value onto the client clock. */
	#toClientClock(serverMs: number): number {
		return serverMs + this.#serverClockOffsetMs;
	}

	/**
	 * @brief Synchronizes the local timer, computing it from the remaining milliseconds received from the server.
	 * Starts a `setInterval` to decrement the time reactively on the UI every second.
	 * @param remainingMs The remaining time provided by the server payload (in ms).
	 */
	#syncTurnTimer(remainingMs: number) {
		this.#clearTimer();
		this.turnTimeRemaining = Math.ceil(remainingMs / 1000);
		const turnLimitMs = storeLobby.current?.settings?.turn_time_limit_ms ?? 0;
		this.#turnClock = {
			durationMs: Math.max(turnLimitMs, remainingMs),
			deadlineAt: Date.now() + remainingMs
		};

		this.#timerInterval = window.setInterval(() => {
			if (this.turnTimeRemaining <= 0) {
				this.#clearTimer();
				return;
			}
			this.turnTimeRemaining -= 1;
		}, 1000);
	}

	/**
	 * @brief Helper function to map the compact numeric output of a card to the textual enum values.
	 * @param rawCard The raw payload containing id, type (int) and value (int).
	 * @returns A formatted object of type Card.
	 */
	#parseCard(rawCard: z.infer<typeof RawCardSchema>): Card {
		const card: Card = {
			id: rawCard.card ?? 0,
			type: (rawCard.color ?? "white") as CardType,
			value: (rawCard.value ?? "0") as CardValue,
			can_play: rawCard.can_play
		};
		if (rawCard.kind) {
			card.kind = rawCard.kind;
			card.face = storeCardDefs.lookupByStringId(rawCard.kind)?.face;
		}
		return card;
	}

	/**
	 * @brief Sends the move to play a specific card in hand.
	 * @param cardId Unique 16-bit identifier of the selected card.
	 */
	playCard(cardId: number) {
		if (this.isSpectator || this.isActionPending) return;
		this.isActionPending = true;
		this.#pendingSafetyTimer = setTimeout(() => this.#clearActionPending(), 3000);
		// PLACEHOLDER-SFX: sfx.action.play-card, optimistic click SFX only,
		// fires on the client-side action, not confirmed by the server's state
		// broadcast; a human may want a separate confirmed-by-server SFX later
		// using the ws.on(ServerAction.MatchStateUpdated) handler instead/in addition.
		storeAudio.playSfx("sfx.action.play-card");
		ws.emit(ClientAction.MatchPlayCard, { card_id: cardId });
	}

	/**
	 * @brief Sends the request to draw a card from the central deck to the server.
	 */
	drawCard() {
		if (this.isSpectator || this.isActionPending) return;
		this.isActionPending = true;
		this.#pendingSafetyTimer = setTimeout(() => this.#clearActionPending(), 3000);
		// PLACEHOLDER-SFX: sfx.action.draw-card, optimistic click SFX only,
		// fires on the client-side action, not confirmed by the server's state
		// broadcast; a human may want a separate confirmed-by-server SFX later
		// using the ws.on(ServerAction.MatchStateUpdated) handler instead/in addition.
		storeAudio.playSfx("sfx.action.draw-card");
		ws.emit(ClientAction.MatchDrawCard);
	}

	/**
	 * @brief Tells the server the local player keeps the held drawn card,
	 * ending the play/keep choice with the card left in hand.
	 *
	 * Mirrors drawCard's latch: ignored while a request is already in flight,
	 * and a no-op unless the local player owns a live draw decision.
	 */
	keepDrawn() {
		if (this.isSpectator || this.isActionPending) return;
		const pending = this.state?.pendingPlayDrawn ?? null;
		if (pending === null || pending.player !== this.localPlayer?.username) return;
		this.isActionPending = true;
		this.#pendingSafetyTimer = setTimeout(() => this.#clearActionPending(), 3000);
		ws.emit(ClientAction.MatchKeepDrawn);
	}

	/**
	 * @brief Mirrors the snapshot's `pending_play_drawn` onto the animation bus.
	 *
	 * The owner's drawn card lifts by id; a waiting opponent's hold lifts a
	 * card back (never a face — the id only ever reaches its owner). The store
	 * is a plain singleton and cannot reach Svelte context, so the board's
	 * state-sync effect passes its bus in. Pass the viewer's POV username so a
	 * spectator's viewed player is matched as "local".
	 */
	applyPendingPlayDrawn(bus: CardBus, localUsername: string | null): void {
		const pending = this.state?.pendingPlayDrawn ?? null;
		const ownedCard =
			pending !== null && pending.player === localUsername && pending.card !== undefined
				? pending.card
				: null;
		bus.setPendingLocalPlayDrawnId(ownedCard);
		bus.setHoldingOpponents(
			pending !== null && ownedCard === null ? new Set([pending.player]) : new Set()
		);
	}

	/**
	 * @brief Tells the server this client has loaded, once per match.
	 *
	 * Spectators never take part in the ready barrier, and a second call within
	 * the same match is a no-op until the next `match_start` clears the flag.
	 */
	sendClientReady(): void {
		if (this.isSpectator || this.clientReadySent) return;
		this.clientReadySent = true;
		ws.emit(ClientAction.MatchClientReady);
	}

	/**
	 * @brief Answers the prompt identified by `promptId` with a raw value.
	 *
	 * The value is validated server-side against the prompt's response_schema,
	 * so it is forwarded as-is (string/number/boolean/object).
	 */
	respondToPrompt(promptId: string, value: unknown): void {
		ws.emit(ClientAction.MatchPromptResponse, { prompt_id: promptId, value });
	}

	/**
	 * @brief Declares a pass on the currently open response window.
	 *
	 * No-op unless a window is open and the local player is one of its
	 * responders; all other players (spectators included) can only watch.
	 */
	passWindow(): void {
		const window = this.activeWindow;
		if (!window || !this.isWindowResponder) return;
		ws.emit(ClientAction.MatchWindowResponse, { window_id: window.windowId, pass: true });
	}

	/**
	 * @brief The window control offered to the local player at `now`.
	 *
	 * A held group (it has a hold) offers only the debt victim a draw, enabled
	 * once the hold has elapsed; a group without a hold keeps the legacy pass
	 * for every responder, labelled draw for the debt victim (passing draws
	 * the whole debt).
	 */
	windowActionState(now: number): WindowAction | null {
		const window = this.activeWindow;
		if (window === null || !this.isWindowResponder) return null;
		if (window.holdMs === 0) {
			return { kind: this.isDebtVictim ? "draw" : "pass", enabled: true };
		}
		if (!this.isDebtVictim) return null;
		const holdEndsAt = window.deadlineAt - window.durationMs + window.holdMs;
		return { kind: "draw", enabled: now >= holdEndsAt };
	}

	/**
	 * @brief Clears session-scoped match state.
	 *
	 * `turnTimeRemaining` returns to its constructed default rather than zero, so
	 * a fresh match does not briefly render an expired timer.
	 */
	reset(): void {
		this.#clearTimer();
		this.#clearActionPending();
		this.#resetMatchCounters();
		this.state = null;
		this.isActionPending = false;
		this.turnTimeRemaining = 15;
		this.#clearWindowState();
		this.#setActivePrompt(null);
		this.lastSeq = null;
		this.desynced = false;
		this.#pendingBeats = [];
		this.matchIntroPending = false;
		this.readyProgress = null;
		this.#readyClock = null;
		this.#observedPromptClock = null;
		this.#serverClockOffsetMs = 0;
		this.matchBegun = false;
		this.clientReadySent = false;
		storeSpectator.reset();
		storeTableSpin.reset();
		storeCardDefs.reset();
	}
}

export const storeGame = new StoreGame();
