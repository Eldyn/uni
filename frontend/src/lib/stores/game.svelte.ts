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

export interface Card {
    id: number;
    color: CardColor;
    value: CardValue;
}

export interface GamePlayer {
    username: string;
    card_count: number;
    has_called_uno: boolean;
    hand?: Card[];
}

export interface GameState {
    active_color: string;
    current_turn: string;
    play_direction: number;
    top_card?: Card;
    players: GamePlayer[];
    is_over?: boolean;
    winner?: string;
}

class StoreGame {
    state = $state<GameState | null>(null);
    actionRequired = $state<string | null>(null);
    actionContext = $state<any>(null);

    turnTimeRemaining = $state<number>(15);
    #timerInterval: number | null = null;

    localPlayer = $derived(
        this.state?.players.find((p) => p.username === storeAuth.username) ?? null
    );

    constructor() {
        // FIXED: Register handlers exactly once at store initialization.
        // They will safely survive any underlying WebSocket re-connections.
        this.#registerListeners();
    }

    returnToLobby() {
        this.#clearTimer();
        this.state = null;
        this.actionRequired = null;
        storeNavigation.goto("lobby");
    }

    #registerListeners() {
        ws.on(ServerAction.GameOver, (data) => {
            console.log(data);
            if (!this.state) return;

            console.log("gameover!");
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
                    hand: p.hand ? p.hand.map(this.#parseCard) : undefined
                })),
                is_over: undefined,
                winner: undefined
            };

            this.actionRequired = data.action_required || null;
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

            console.log(stateJson);
            const remainingMs = stateJson.turn_time_remaining_ms ?? 15000;
            this.#syncTurnTimer(remainingMs);

            if (storeNavigation.current === "lobby") {
                storeNavigation.goto("game");
            }
        });
    }

    #clearTimer() {
        if (this.#timerInterval !== null) {
            clearInterval(this.#timerInterval);
            this.#timerInterval = null;
        }
    }

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

    #parseCard(rawCard: { id: number; color: number; value: number }): Card {
        return {
            id: rawCard.id,
            color: COLOR_MAP[rawCard.color] || "wild",
            value: VALUE_MAP[rawCard.value] || ""
        };
    }

    playCard(cardId: number) {
        ws.emit(ClientAction.GamePlayCard, { card_id: cardId });
    }

    drawCard() {
        ws.emit(ClientAction.GameDrawCard);
    }

    callUno() {
        ws.emit(ClientAction.GameDrawCard);
    }

    submitInput(value: string) {
        ws.emit(ClientAction.GameSubmitInput, { value: value });
    }
}

export const storeGame = new StoreGame();
