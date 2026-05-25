/**
 * WebSocket communication utilities
 * Manages connection, message handlers, and emit/wait patterns using Svelte 5 state.
 */

import { gameStore, Lobby } from "./stores/game.svelte";
import { navigationStore, toastStore } from "./stores/ui.svelte";

export const ClientAction = {
    LobbyList: "lobby_list",
    LobbyCreate: "lobby_create",
    LobbyJoin: "lobby_join",
    LobbyRejoin: "lobby_rejoin",
    LobbyLeave: "lobby_leave",
    LobbyInvite: "lobby_invite",
    LobbySetRuleset: "lobby_set_ruleset",
    LobbySetSkin: "lobby_set_skin",
    Ready: "ready",
    PlayCard: "play_card",
    DrawCard: "draw_card",
    CallUno: "call_uno",
    ChallengeUno: "challenge_uno",
    ChallengePlusFour: "challenge_plus_four",
    ChooseColor: "choose_color",
    AckGameOver: "ack_game_over",
    StorePurchase: "store_purchase",
    InventoryRequest: "inventory_request",
    ChatSend: "chat_send"
} as const;

export const ServerAction = {
    SyncData: "sync_data",
    Error: "error",
    LobbyEvicted: "lobby_evicted",
    LobbyList: "lobby_list",
    LobbyUpdated: "lobby_updated",
    LobbyJoined: "lobby_joined",
    LobbyLeft: "lobby_left",
    InviteCreated: "invite_created",
    PlayerJoined: "player_joined",
    PlayerLeft: "player_left",
    PlayerReady: "player_ready",
    GameStarting: "game_starting",
    RulesetSync: "ruleset_sync",
    StateUpdate: "state_update",
    CardRejected: "card_rejected",
    CardDrawn: "card_drawn",
    TurnChanged: "turn_changed",
    ColorChanged: "color_changed",
    UnoCallSuccess: "uno_call_success",
    UnoCallFailed: "uno_call_failed",
    UnoChallenged: "uno_challenged",
    CardPlayed: "card_played",
    PlayerDrewCard: "player_drew_card",
    SkipApplied: "skip_applied",
    ReverseApplied: "reverse_applied",
    DrawStackApplied: "draw_stack_applied",
    GameOver: "game_over",
    RoundOver: "round_over",
    InventorySync: "inventory_sync",
    PurchaseConfirmed: "purchase_confirmed",
    PurchaseFailed: "purchase_failed",
    ChatMessage: "chat_message"
} as const;

export type ClientActionType = (typeof ClientAction)[keyof typeof ClientAction];
export type ServerActionType = (typeof ServerAction)[keyof typeof ServerAction];

export type ServerActionDef = ServerActionType | string;
export type MessageHandler = (data: Record<string, unknown>) => void;

/**
 * Represents the reactive state of the WebSocket connection.
 */
export interface ConnectionStatus {
    status: "disconnected" | "connecting" | "connected";
    username: string;
    room: string;
    lobby_code: string;
}

/**
 * Unified response wrapper for WebSocket messages.
 * Mimics the HTTP Fetch Response API to provide a linear, predictable control flow.
 */
export class WsResponse {
    /** True if the server action is NOT an error. */
    public readonly ok: boolean;
    /** The server action returned. */
    public readonly action: string;
    /** The original request ID, if provided. */
    public readonly request_id?: string;
    /** The raw JSON data dictionary returned by the server. */
    public readonly data: Record<string, unknown>;

    constructor(rawData: Record<string, unknown>) {
        this.data = rawData;
        this.action = (rawData.action as string) || "";
        this.request_id = rawData.request_id as string;

        this.ok = this.action !== "error";
    }

    /**
     * Safely extracts the server's error reason, falling back to a default string.
     */
    get reason(): string {
        return (this.data.reason as string) || "Unknown Server Error";
    }

    /**
     * Utility to extract a strongly-typed value from the payload, or null if it does not exist.
     * @param key The key to look up in the payload.
     */
    get<T>(key: string): T | null {
        return key in this.data ? (this.data[key] as T) : null;
    }

    /**
     * Utility to extract a strongly-typed value from the payload with a fallback.
     * @param key The key to look up in the payload.
     * @param fallback The fallback value if the key does not exist.
     */
    getOr<T>(key: string, fallback: T): T {
        return key in this.data ? (this.data[key] as T) : fallback;
    }
}

/**
 * Internal interface for tracking unresolved emitAndWait calls.
 */
interface PendingRequest {
    resolve: (res: WsResponse) => void;
    reject: (err: Error) => void;
    timer: ReturnType<typeof setTimeout>;
}

/**
 * Stateful WebSocket Client
 * Manages automatic reconnections, request/response correlation, and Svelte 5 reactive connection states.
 */
export class WebSocketClient {
    /** The raw WebSocket instance (reactive). */
    socket = $state<WebSocket | null>(null);

    /** Connection metadata and status (reactive). */
    connectionStatus = $state<ConnectionStatus>({
        status: "disconnected",
        username: "",
        room: "",
        lobby_code: ""
    });

    private _nextRequestId = 1;
    private handlers = new Map<string, Set<MessageHandler>>();
    private pendingRequests = new Map<string, PendingRequest>();

    private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
    private reconnectDelayMs = 1000;
    private intentionalClose = false;

    /**
     * Checks if the WebSocket is currently open and active.
     */
    get isConnected(): boolean {
        return this.socket?.readyState === WebSocket.OPEN;
    }

    /**
     * Establishes the WebSocket connection. Safe to call multiple times.
     */
    async connect(): Promise<void> {
        if (this.isConnected) return;

        this.intentionalClose = false;
        await this._connectOnce();
    }

    /**
     * Intentionally disconnects the WebSocket and halts exponential backoff reconnections.
     */
    disconnect(code: number, reason: string): void {
        this.intentionalClose = true;

        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }

        this.socket?.close(code, reason);
        this.socket = null;
        this.connectionStatus.status = "disconnected";
    }

    /**
     * Fires a message to the server without expecting a correlated response.
     * Does NOT attach a request_id.
     *
     * @param action The ClientAction to send
     * @param payload Optional data payload
     */
    emit(action: string, payload: Record<string, unknown> = {}): void {
        if (this.isConnected) {
            this.socket!.send(JSON.stringify({ action, ...payload }));
        }
    }

    /**
     * Sends a message and awaits a correlated response from the server via a request_id.
     * Resolves with a WsResponse (even for server logic errors), but rejects on infrastructure/timeout failures.
     *
     * @param action The ClientAction to send
     * @param payload Optional data payload
     * @param timeoutMs How long to wait before aborting (default 5000ms)
     */
    emitAndWait(
        action: string,
        payload: Record<string, unknown> = {},
        timeoutMs: number = 5000
    ): Promise<WsResponse> {
        return new Promise((resolve, reject) => {
            const requestId = String(this._nextRequestId++);

            const timer = setTimeout(() => {
                this.pendingRequests.delete(requestId);
                reject(new Error(`Timeout: no response for "${action}" (request_id=${requestId})`));
            }, timeoutMs);

            this.pendingRequests.set(requestId, { resolve, reject, timer });

            if (this.isConnected) {
                this.socket!.send(JSON.stringify({ action, request_id: requestId, ...payload }));
            } else {
                this.pendingRequests.delete(requestId);
                clearTimeout(timer);
                reject(new Error("WebSocket not connected"));
            }
        });
    }

    /**
     * Registers a listener for specific server actions, or "*" for all actions.
     *
     * @param action The ServerAction to listen for.
     * @param handler Callback triggered when the action is received.
     * @returns A cleanup function to remove the listener.
     */
    on(action: ServerActionDef | "*", handler: MessageHandler): () => void {
        if (!this.handlers.has(action)) this.handlers.set(action, new Set());
        this.handlers.get(action)!.add(handler);

        return () => this.handlers.get(action)?.delete(handler);
    }

    /**
     * Internal routine to establish the connection and bind events.
     */
    private async _connectOnce(): Promise<void> {
        return new Promise(async (resolve, reject) => {
            const protocol = location.protocol === "https:" ? "wss:" : "ws:";
            const wsInstance = new WebSocket(`${protocol}//${location.host}`);
            this.connectionStatus.status = "connecting";

            wsInstance.onopen = async () => {
                this.socket = wsInstance;
                this.connectionStatus.status = "connected";
                this.reconnectDelayMs = 1000;

                const lobbyCode = localStorage.getItem("lobby_code");

                if (!lobbyCode) {
                    resolve();
                    return;
                }

                const response = await ws.emitAndWait(ClientAction.LobbyRejoin, { code: lobbyCode });

                if (!response.ok) {
                    toastStore.showError(`Could not rejoin lobby: ${response.reason}`);
                    localStorage.removeItem("lobby_code");
                    ws.emit(ClientAction.LobbyLeave);
                    resolve();
                    return;
                }

                if (response.action === ServerAction.LobbyEvicted) {
                    // INFO: this is intended, so we don't necessarily need to send a toast
                    // toastStore.showInfo(`Could not rejoin lobby: expired`);
                    localStorage.removeItem("lobby_code");
                    resolve();
                    return;
                }

                const lobby = response.get<Lobby>("lobby");

                if (!lobby) {
                    toastStore.showError(`Could not rejoin lobby: Unknown Error`);
                    localStorage.removeItem("lobby_code");
                    ws.emit(ClientAction.LobbyLeave);
                    resolve();
                    return;
                }

                navigationStore.screen = "lobby";
                gameStore.currentLobby = lobby;

                resolve();
            };

            wsInstance.onerror = (e) => reject(e);

            wsInstance.onclose = () => {
                if (this.socket === wsInstance) {
                    this.socket = null;
                    this.connectionStatus.status = "disconnected";
                }
                this._scheduleReconnect();
            };

            wsInstance.onmessage = (e: MessageEvent) => {
                try {
                    const data = JSON.parse(e.data as string) as Record<string, unknown>;
                    if (data.action === "sync_data") {
                        this.connectionStatus.username = data.username as string;
                    }
                    this._dispatch(data);
                } catch (err) {
                    console.error(err);
                }
            };
        });
    }

    /**
     * Handles routing the incoming message to pending requests and global listeners.
     */
    private _dispatch(data: Record<string, unknown>) {
        const action = data.action as string;
        const requestId = data.request_id as string;

        // INFO: Resolve pending request promises if a request_id matches
        if (requestId && this.pendingRequests.has(requestId)) {
            const pending = this.pendingRequests.get(requestId)!;
            this.pendingRequests.delete(requestId);
            clearTimeout(pending.timer);

            // INFO: Wrap in our HTTP-like envelope and resolve
            const response = new WsResponse(data);
            pending.resolve(response);
        }

        // INFO: Fire standard global listeners
        if (action) {
            this.handlers.get(action)?.forEach((handler) => handler(data));
            this.handlers.get("*")?.forEach((handler) => handler(data));
        }
    }

    /**
     * Implements an exponential backoff strategy for reconnections.
     */
    private _scheduleReconnect() {
        if (this.intentionalClose) return;
        if (this.reconnectTimer) clearTimeout(this.reconnectTimer);

        this.reconnectTimer = setTimeout(() => {
            this.reconnectTimer = null;
            this.reconnectDelayMs = Math.min(this.reconnectDelayMs * 2, 16_000);

            this._connectOnce().catch(() => {
                this._scheduleReconnect();
            });
        }, this.reconnectDelayMs);
    }
}

// Export a global singleton instance for use across the Svelte application
export const ws = new WebSocketClient();
