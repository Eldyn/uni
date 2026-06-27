/**
 * @file game.svelte.ts
 * @brief Reactive global store for managing the game table and turns.
 * Decodes the JSON packets from the WebSocket and updates the game engine UI.
 */

import { z } from "zod";
import { storeNavigation } from "./navigation.svelte";
import { ClientAction, ServerAction, ws } from "./ws.svelte";
import { storeAuth } from "./auth.svelte";
import { Action, Type, TypeMap, ValueMap } from "../generated/schemas";

export const TYPE_MAP = TypeMap;
const VALUE_MAP = ValueMap;

export type CardType = (typeof TypeMap)[number];
export type CardValue = (typeof ValueMap)[number];

const RawCardSchema = z.object({
	id: z.number().int(),
	type: z.number().int().min(0).max(4),
	value: z.number().int().min(0).max(14),
	can_play: z.boolean().optional()
});

const RawPlayerSchema = z.object({
	username: z.string(),
	card_count: z.number().int(),
	has_called_uno: z.boolean(),
	hand: z.array(RawCardSchema).optional(),
	is_bot: z.boolean()
});

const RawGameStateSchema = z.object({
	active_type: z.number().int().min(0).max(4),
	current_turn: z.string(),
	play_direction: z.number(),
	top_card: RawCardSchema.optional(),
	players: z.array(RawPlayerSchema),
	pending_draws: z.number().int().default(0),
	last_play: z.object({ player: z.string(), hand_index: z.number().int() }).optional(),
	turn_time_remaining_ms: z.number().optional()
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
	/** Indicates whether the player has correctly clicked "UNO!". */
	has_called_uno: boolean;
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
	/** Origin of the last played card, used to animate it from its source slot. */
	last_play?: LastPlay;
	/** Flag indicating whether the match has reached a terminal state. */
	is_over?: boolean;
	/** Username of the winning player, if the match has ended. */
	winner?: string;
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
 * @class StoreGame
 * @brief Synchronizes the frontend state with the server Game Engine in real time.
 * @tag FRONT-GAME-001
 */
class StoreGame {
	/** The current state of the match (players, deck, cards on the table). */
	state = $state<GameState | null>(null);
	/** Action the engine is waiting for (Action enum value), or null if none. */
	actionRequired = $state<number | null>(null);
	/** Contextual data attached to the input request. */
	actionContext = $state<any>(null);

	/** True while a game action is in flight — cleared on next MatchStateUpdated. */
	isActionPending = $state(false);

	/** Seconds remaining to complete the turn, computed locally. */
	turnTimeRemaining = $state<number>(15);

	/** Reference to the browser's native `setInterval` timer. */
	#timerInterval: number | null = null;

	/** Safety timeout that releases isActionPending if the server stops responding. */
	#pendingSafetyTimer: ReturnType<typeof setTimeout> | null = null;

	/** Derived property to instantly identify the local player. */
	localPlayer = $derived(
		this.state?.players.find((p) => p.username === storeAuth.username) ?? null
	);

	constructor() {
		// FIXED: Register handlers exactly once at store initialization.
		// They will safely survive any underlying WebSocket re-connections.
		this.#registerListeners();
	}

	/**
	 * @brief Stops the active timers and closes the match screen, returning to the lobby.
	 * @tag FRONT-GAME-MTH-001
	 */
	returnToLobby() {
		this.#clearTimer();
		this.state = null;
		this.actionRequired = null;
		this.actionContext = null;
		this.turnTimeRemaining = 0;
		storeNavigation.goto("lobby");
	}

	/**
	 * @brief Registers the listeners for the WebSocket events related to the game.
	 * Intercepts the end of the match and the state updates sent by the server.
	 * @tag FRONT-GAME-MTH-002
	 */
	#registerListeners() {
		ws.on(ServerAction.MatchOver, (data) => {
			if (!this.state) return;

			const winner = data.winner;
			this.state.is_over = true;
			this.state.winner = winner as string;
			this.actionRequired = null;
			this.actionContext = null;

			this.#clearTimer();
		});

		ws.on(ServerAction.MatchStateUpdated, (data: any) => {
			this.#clearActionPending();
			const parsed = RawGameStateSchema.safeParse(data.match_state);
			if (!parsed.success) {
				console.error("[MatchStateUpdated] payload validation failed:", parsed.error.message);
				return;
			}
			const stateJson = parsed.data;

			this.state = {
				active_type: TYPE_MAP[stateJson.active_type],
				current_turn: stateJson.current_turn,
				play_direction: stateJson.play_direction,
				top_card: stateJson.top_card ? this.#parseCard(stateJson.top_card) : undefined,
				players: stateJson.players.map((p) => ({
					...p,
					hand: p.hand ? p.hand.map((card) => this.#parseCard(card)) : undefined
				})),
				pending_draws: stateJson.pending_draws,
				last_play: stateJson.last_play,
				is_over: undefined,
				winner: undefined
			};

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

			const remainingMs = stateJson.turn_time_remaining_ms ?? 15000;
			this.#syncTurnTimer(remainingMs);

			if (storeNavigation.current === "lobby") {
				storeNavigation.goto("game");
			}
		});
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
	 * @tag FRONT-GAME-MTH-003
	 */
	#clearTimer() {
		if (this.#timerInterval !== null) {
			clearInterval(this.#timerInterval);
			this.#timerInterval = null;
		}
	}

	/**
	 * @brief Synchronizes the local timer, computing it from the remaining milliseconds received from the server.
	 * Starts a `setInterval` to decrement the time reactively on the UI every second.
	 * @param remainingMs The remaining time provided by the server payload (in ms).
	 * @tag FRONT-GAME-MTH-004
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
	 * @tag FRONT-GAME-MTH-005
	 */
	#parseCard(rawCard: z.infer<typeof RawCardSchema>): Card {
		return {
			id: rawCard.id,
			type: TYPE_MAP[rawCard.type],
			value: VALUE_MAP[rawCard.value],
			can_play: rawCard.can_play
		};
	}

	/**
	 * @brief Sends the move to play a specific card in hand.
	 * @param cardId Unique 16-bit identifier of the selected card.
	 * @tag FRONT-GAME-MTH-006
	 */
	playCard(cardId: number) {
		if (this.isActionPending) return;
		this.isActionPending = true;
		this.#pendingSafetyTimer = setTimeout(() => this.#clearActionPending(), 3000);
		ws.emit(ClientAction.MatchPlayCard, { card_id: cardId });
	}

	/**
	 * @brief Sends the request to draw a card from the central deck to the server.
	 * @tag FRONT-GAME-MTH-007
	 */
	drawCard() {
		if (this.isActionPending) return;
		this.isActionPending = true;
		this.#pendingSafetyTimer = setTimeout(() => this.#clearActionPending(), 3000);
		ws.emit(ClientAction.MatchDrawCard);
	}

	/**
	 * @brief Formally declares "UNO!" to the server by pressing the dedicated button.
	 * @tag FRONT-GAME-MTH-008
	 */
	callUno() {
		ws.emit(ClientAction.MatchCallUno);
	}

	/**
	 * @brief Resolves a currently suspended effect by forwarding the user input.
	 * @param value The value chosen by the user via modal (e.g. the type index for the Wild).
	 * @tag FRONT-GAME-MTH-009
	 */
	submitInput(value: string) {
		if (this.isActionPending) return;
		this.isActionPending = true;
		this.#pendingSafetyTimer = setTimeout(() => this.#clearActionPending(), 3000);
		ws.emit(ClientAction.MatchSubmitInput, { value: value });
	}
}

export const storeGame = new StoreGame();
export { Action, Type };
