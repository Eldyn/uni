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
import {
	Action,
	MatchEventPayloadSchema,
	PromptClosePayloadSchema,
	PromptOpenPayloadSchema,
	TurnAdvancePayloadSchema,
	Type,
	TypeMap,
	ValueMap,
	WindowClosePayloadSchema,
	WindowOpenPayloadSchema
} from "$lib/generated/schemas";
import type { PromptOpenPayload, WindowOpenPayload } from "$lib/generated/schemas";
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
	statuses: z.array(z.unknown()).optional()
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
	draw_pile: z.object({ count: z.number().int() }).optional(),
	discard_pile: z.object({ count: z.number().int(), top: RawCardSchema.optional() }).optional(),
	window: z.unknown().optional(),
	prompts: z.array(z.unknown()).optional(),
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
	/** How many cards remain in the draw pile — drives the pile's visible stack height and reshuffle detection. */
	draw_pile_size: number;
	/** Origin of the last played card, used to animate it from its source slot. */
	last_play?: LastPlay;
	/** Mode of the match ('standard' | 'elimination'). */
	mode?: string;
	/** Number of connected spectators. */
	spectator_count?: number;
	/** Current or final placement list in elimination mode. */
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
	/** Usernames eligible to respond. */
	responders: string[];
	/** Digest of the engine-side eligibility filter. */
	eligibleFilterDigest: string;
}

/**
 * @class StoreGame
 * @brief Synchronizes the frontend state with the server Game Engine in real time.
 */
class StoreGame implements SessionStore {
	/** The current state of the match (players, deck, cards on the table). */
	state = $state<GameState | null>(null);
	/** Action the engine is waiting for (Action enum value), or null if none. */
	actionRequired = $state<number | null>(null);
	/** Contextual data attached to the input request. */
	actionContext = $state<any>(null);

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

	/** Seconds remaining before the open response window closes, computed locally. */
	windowTimeRemaining = $state<number>(0);

	/** Open op-input prompt for the local viewer, or null when none is live. */
	activePrompt = $state<PromptOpenPayload | null>(null);

	/** Reference to the browser's native `setInterval` timer. */
	#timerInterval: number | null = null;

	/** Reference to the browser's native `setInterval` timer for the window countdown. */
	#windowTimerInterval: number | null = null;

	/** Safety timeout that releases isActionPending if the server stops responding. */
	#pendingSafetyTimer: ReturnType<typeof setTimeout> | null = null;

	/** Timestamp (ms) when this client entered the current match, for duration analytics. */
	#matchStartedAt: number | null = null;

	/** Timestamp (ms) when the current turn began, for per-turn pacing analytics. */
	#turnStartedAt: number | null = null;

	/** Duration (ms) of every completed human turn this match, for time_to_play_avg. */
	#humanTurnDurations: number[] = [];

	/** Derived property to instantly identify the local player. */
	localPlayer = $derived(
		this.state?.players.find((p) => p.username === storeAuth.username) ?? null
	);

	/** Derived property indicating whether client is a spectator (or eliminated).
	 *  Both conditions are OR-ed, not `??`-ed: the backend marks an eliminated
	 *  player `is_spectator` in the LOBBY but only broadcasts match state, so the
	 *  client's lobby copy keeps a stale `false` — which would win a `??` and hide
	 *  that the player has left the match. Being absent from `state.players` is the
	 *  reliable signal (a shed player is erased from the rotation), so it must be
	 *  able to flip the result on its own. */
	isSpectator = $derived(
		(storeLobby.current?.members.find((m) => m.username === storeAuth.username)?.is_spectator ??
			false) ||
			(this.state !== null && !this.state.players.some((p) => p.username === storeAuth.username))
	);

	/** Number of connected spectators. */
	spectatorCount = $derived(this.state?.spectator_count ?? 0);

	/** Current or final placement list in elimination mode. */
	placements = $derived(this.state?.placements ?? []);

	/** True for the tick where the local player's own elimination just landed
	 *  in `placements` — used to fire the one-shot mid-match outcome banner
	 *  exactly once, not on every subsequent state update. */
	#previousPlacementsHadMe = false;
	justEliminated = $derived.by(() => {
		const inPlacementsNow = this.placements.includes(storeAuth.username);
		const isNew = inPlacementsNow && !this.#previousPlacementsHadMe;
		this.#previousPlacementsHadMe = inPlacementsNow;
		return isNew;
	});

	/** "win" if the local player's rank is top 3, "lose" otherwise; null if they
	 *  aren't in `placements` yet (match ongoing for them, or not elimination
	 *  mode). Fixed rank<=3 threshold — no small-lobby exception.
	 *  Elimination is a shedding race: the first player to drop every card
	 *  leaves first and WINS, so `placements` is best-first (1st, 2nd, ...) in
	 *  both regimes — the backend never reorders it — and the rank is always
	 *  idx + 1. A player enters `placements` the moment they shed their hand.
	 *  Corner: a mid-game quitter drops out of `players` without entering
	 *  `placements` — rare, accepted. */
	eliminationOutcome = $derived<"win" | "lose" | null>(
		(() => {
			const idx = this.placements.indexOf(storeAuth.username);
			if (idx === -1) return null;
			return idx + 1 <= 3 ? "win" : "lose";
		})()
	);

	constructor() {
		// FIXED: Register handlers exactly once at store initialization.
		// They will safely survive any underlying WebSocket re-connections.
		this.#registerListeners();
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
		this.state = null;
		this.actionRequired = null;
		this.actionContext = null;
		this.turnTimeRemaining = 0;
		this.#clearWindowState();
		this.activePrompt = null;
		this.lastSeq = null;
		this.desynced = false;
		this.#pendingBeats = [];
		storeSpectator.reset();
		storeNavigation.goto("lobby");
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
			this.actionRequired = null;
			this.actionContext = null;

			this.#clearTimer();
			this.#clearWindowState();
			this.activePrompt = null;

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

				if (winner) {
					storeAnalytics.track("match_end", {
						duration_seconds,
						time_to_play_avg_ms,
						winner_is_bot: this.state.players.find((p) => p.username === winner)?.is_bot
					});
					storeAnalytics.track("match_settings", settingsParams);
				} else if (reason) {
					storeAnalytics.track("match_saved", { duration_seconds });
					storeAnalytics.track("match_settings", settingsParams);
				}
				// winner empty AND reason empty: true abort/delete, no event —
				// counted as abandoned only by exclusion (match_start - match_end - match_saved).
			}
			this.#matchStartedAt = null;
			this.#turnStartedAt = null;
			this.#humanTurnDurations = [];
		});

		ws.on(ServerAction.MatchStateUpdated, (data: any) => {
			this.#clearActionPending();
			const parsed = RawGameStateSchema.safeParse(data.match_state);
			if (!parsed.success) {
				console.error("[MatchStateUpdated] payload validation failed:", parsed.error.message);
				return;
			}
			const stateJson = parsed.data;

			const currentTurn = stateJson.current_player;
			const previousTurn = this.state?.current_turn;
			const previousPlayers = this.state?.players;
			if (previousTurn && previousTurn !== currentTurn) {
				const previousPlayer = previousPlayers?.find((p) => p.username === previousTurn);
				if (previousPlayer && !previousPlayer.is_bot && this.#turnStartedAt !== null) {
					this.#humanTurnDurations.push(Date.now() - this.#turnStartedAt);
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
				pending_draws: 0,
				draw_pile_size: stateJson.draw_pile?.count ?? 0,
				placements: stateJson.placements,
				seq_watermark: stateJson.seq_watermark,
				window: stateJson.window,
				prompts: stateJson.prompts
			};

			// INFO: the snapshot window/prompts are reconnect truth
			// a packet-driven open/close may already have set them.
			const snapshotWindow =
				stateJson.window == null ? null : WindowOpenPayloadSchema.safeParse(stateJson.window);
			if (snapshotWindow?.success) {
				// INFO: the snapshot window deadline is already absolute epoch ms.
				this.#setActiveWindow(snapshotWindow.data, snapshotWindow.data.deadline_ms);
				this.#syncWindowTimer(snapshotWindow.data.deadline_ms);
			} else {
				this.#clearWindowState();
			}

			const snapshotPrompts = stateJson.prompts;
			if (snapshotPrompts && snapshotPrompts.length > 0) {
				const parsedPrompt = PromptOpenPayloadSchema.safeParse(snapshotPrompts[0]);
				this.activePrompt = parsedPrompt.success ? parsedPrompt.data : null;
			} else {
				this.activePrompt = null;
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

			this.actionRequired = data.action_required ?? null;

			let parsedContext = data.action_context || null;
			if (typeof parsedContext === "string") {
				try {
					parsedContext = JSON.parse(parsedContext);
				} catch (e) {
					console.error("Failed to parse action_context", e);
				}
			}
			this.actionContext = parsedContext;

			// INFO: the snapshot carries no turn clock; the timer becomes
			// packet-driven from `turn_advance` in slice A4.

			if (storeNavigation.current === "lobby" || storeNavigation.initialScreen === "game") {
				this.#matchStartedAt = Date.now();
				storeNavigation.goto("game");
			}
		});

		// INFO: the single MatchEvent subscription — `ws.on` warns in
		// DEV for a non-exempt action with more than one handler.
		ws.on(ServerAction.MatchEvent, (data) => this.#onMatchEventFrame(data));
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
			case "turn_advance": {
				const parsed = TurnAdvancePayloadSchema.safeParse(env.data.payload);
				// INFO: turn_advance.deadline_ms is an ABSOLUTE epoch-ms value
				// (`TurnState.turn_deadline_ms`), not a remaining duration.
				if (parsed.success) {
					this.#syncTurnTimer(Math.max(0, parsed.data.deadline_ms - Date.now()));
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
					this.#syncWindowTimer(deadlineAt);
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
				if (parsed.success) this.activePrompt = parsed.data;
				break;
			}
			case "prompt_close": {
				const parsed = PromptClosePayloadSchema.safeParse(env.data.payload);
				if (parsed.success && this.activePrompt?.prompt_id === parsed.data.prompt_id) {
					this.activePrompt = null;
				}
				break;
			}
		}

		const beat = mapMatchEventPacket(data);
		if (beat) this.#pendingBeats.push(beat);
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
		if (this.#timerInterval !== null) {
			clearInterval(this.#timerInterval);
			this.#timerInterval = null;
		}
	}

	/**
	 * @brief Cancels and destroys the currently running response-window timer.
	 */
	#clearWindowTimer() {
		if (this.#windowTimerInterval !== null) {
			clearInterval(this.#windowTimerInterval);
			this.#windowTimerInterval = null;
		}
	}

	/**
	 * @brief Mirrors an open response window from a packet or snapshot payload.
	 * @param deadlineAt Absolute deadline on the client clock (epoch ms).
	 */
	#setActiveWindow(windowPayload: WindowOpenPayload, deadlineAt: number) {
		this.activeWindow = {
			windowId: windowPayload.window_id,
			deadlineAt,
			responders: windowPayload.responders,
			eligibleFilterDigest: windowPayload.eligible_filter_digest
		};
	}

	/**
	 * @brief Clears the open response window and stops its countdown.
	 */
	#clearWindowState() {
		this.#clearWindowTimer();
		this.activeWindow = null;
		this.windowTimeRemaining = 0;
	}

	/**
	 * @brief Starts the response-window countdown from an absolute deadline,
	 * recomputing the remaining seconds each tick so the value cannot drift.
	 * @param deadlineAt Absolute deadline on the client clock (epoch ms).
	 */
	#syncWindowTimer(deadlineAt: number) {
		this.#clearWindowTimer();
		this.windowTimeRemaining = this.#remainingSeconds(deadlineAt);

		this.#windowTimerInterval = window.setInterval(() => {
			this.windowTimeRemaining = this.#remainingSeconds(deadlineAt);
			if (this.windowTimeRemaining <= 0) {
				this.#clearWindowTimer();
			}
		}, 1000);
	}

	/**
	 * @brief Whole seconds from now until `deadlineAt`, floored at zero.
	 */
	#remainingSeconds(deadlineAt: number): number {
		return Math.max(0, Math.ceil((deadlineAt - Date.now()) / 1000));
	}

	/**
	 * @brief Synchronizes the local timer, computing it from the remaining milliseconds received from the server.
	 * Starts a `setInterval` to decrement the time reactively on the UI every second.
	 * @param remainingMs The remaining time provided by the server payload (in ms).
	 */
	#syncTurnTimer(remainingMs: number) {
		this.#clearTimer();
		this.turnTimeRemaining = Math.ceil(remainingMs / 1000);

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
		return {
			id: rawCard.card ?? 0,
			type: (rawCard.color ?? "white") as CardType,
			value: (rawCard.value ?? "0") as CardValue,
			can_play: rawCard.can_play
		};
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
	 * @brief Resolves a currently suspended effect by forwarding the user input.
	 * @param value The value chosen by the user via modal (e.g. the type index for the Wild).
	 */
	submitInput(value: string) {
		if (this.isSpectator || this.isActionPending) return;
		this.isActionPending = true;
		this.#pendingSafetyTimer = setTimeout(() => this.#clearActionPending(), 3000);
		// PLACEHOLDER-SFX: sfx.action.submit-input, optimistic click SFX only,
		// fires on the client-side action, not confirmed by the server's state
		// broadcast; a human may want a separate confirmed-by-server SFX later
		// using the ws.on(ServerAction.MatchStateUpdated) handler instead/in addition.
		storeAudio.playSfx("sfx.action.submit-input");
		ws.emit(ClientAction.MatchSubmitInput, { value: value });
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
		this.state = null;
		this.actionRequired = null;
		this.actionContext = null;
		this.isActionPending = false;
		this.turnTimeRemaining = 15;
		this.#clearWindowState();
		this.activePrompt = null;
		this.lastSeq = null;
		this.desynced = false;
		this.#pendingBeats = [];
		storeSpectator.reset();
	}
}

export const storeGame = new StoreGame();
export { Action, Type };
