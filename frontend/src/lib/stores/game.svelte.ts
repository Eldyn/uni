/**
 * @file game.svelte.ts
 * @brief Reactive global store for managing the game table and turns.
 * Decodes the JSON packets from the WebSocket and updates the game engine UI.
 */

import { storeNavigation } from "./navigation.svelte";
import { ClientAction, ServerAction, ws } from "./ws.svelte";
import { storeAuth } from "./auth.svelte";

const COLOR_MAP = ["red", "blue", "green", "yellow", "wild"] as const;
const VALUE_MAP = [
	"0",
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7",
	"8",
	"9",
	"skip",
	"reverse",
	"+2",
	"colorswitch",
	"+4"
] as const;

export type CardColor = (typeof COLOR_MAP)[number];
export type CardValue = (typeof VALUE_MAP)[number];

/**
 * @interface Card
 * @brief Frontend representation of a single card.
 */
export interface Card {
	/** The unique 16-bit identifier of the card. */
	id: number;
	/** The decoded colour of the card. */
	color: CardColor;
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
	/** Colour currently active for plays. */
	active_color: string;
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
	/** Indicates whether the game engine is suspended waiting for input. */
	actionRequired = $state<string | null>(null);
	/** Contextual data attached to the input request. */
	actionContext = $state<any>(null);

	/** Seconds remaining to complete the turn, computed locally. */
	turnTimeRemaining = $state<number>(15);

	/** Reference to the browser's native `setInterval` timer. */
	#timerInterval: number | null = null;

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
		storeNavigation.goto("lobby");
	}

	/**
	 * @brief Registers the listeners for the WebSocket events related to the game.
	 * Intercepts the end of the match and the state updates sent by the server.
	 * @tag FRONT-GAME-MTH-002
	 */
	#registerListeners() {
		ws.on(ServerAction.GameOver, (data) => {
			if (!this.state) return;

			const winner = data.winner;
			this.state.is_over = true;
			this.state.winner = winner as string;

			this.#clearTimer();
		});

		ws.on(ServerAction.GameStateUpdated, (data: any) => {
			const stateJson = data.game_state;

			this.state = {
				active_color: COLOR_MAP[stateJson.active_color] || "green",
				current_turn: stateJson.current_turn,
				play_direction: stateJson.play_direction,
				top_card: stateJson.top_card ? this.#parseCard(stateJson.top_card) : undefined,
				players: stateJson.players.map((p: any) => ({
					...p,
					hand: p.hand ? p.hand.map((card: any) => this.#parseCard(card)) : undefined
				})),
				pending_draws: stateJson.pending_draws ?? 0,
				last_play: stateJson.last_play
					? {
							player: stateJson.last_play.player,
							hand_index: stateJson.last_play.hand_index
						}
					: undefined,
				is_over: undefined,
				winner: undefined
			};

			this.actionRequired = data.action_required || null;

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
	 * @param rawCard The raw payload containing id, color (int) and value (int).
	 * @returns A formatted object of type Card.
	 * @tag FRONT-GAME-MTH-005
	 */
	#parseCard(rawCard: { id: number; color: number; value: number; can_play?: boolean }): Card {
		return {
			id: rawCard.id,
			color: COLOR_MAP[rawCard.color] || "wild",
			value: VALUE_MAP[rawCard.value] || "",
			can_play: rawCard.can_play
		};
	}

	/**
	 * @brief Sends the move to play a specific card in hand.
	 * @param cardId Unique 16-bit identifier of the selected card.
	 * @tag FRONT-GAME-MTH-006
	 */
	playCard(cardId: number) {
		ws.emit(ClientAction.GamePlayCard, { card_id: cardId });
	}

	/**
	 * @brief Sends the request to draw a card from the central deck to the server.
	 * @tag FRONT-GAME-MTH-007
	 */
	drawCard() {
		ws.emit(ClientAction.GameDrawCard);
	}

	/**
	 * @brief Formally declares "UNO!" to the server by pressing the dedicated button.
	 * @tag FRONT-GAME-MTH-008
	 */
	callUno() {
		ws.emit(ClientAction.GameCallUno);
	}

	/**
	 * @brief Resolves a currently suspended effect by forwarding the user input.
	 * @param value The value chosen by the user via modal (e.g. the colour for the Wild).
	 * @tag FRONT-GAME-MTH-009
	 */
	submitInput(value: string) {
		ws.emit(ClientAction.GameSubmitInput, { value: value });
	}
}

export const storeGame = new StoreGame();
