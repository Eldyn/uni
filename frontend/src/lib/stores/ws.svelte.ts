/**
 * @file ws.svelte.ts
 * @brief Utility di comunicazione e wrapper per il protocollo WebSocket.
 * Gestisce la connessione, l'instradamento dei messaggi (handler) e implementa un pattern
 * Sincrono/Asincrono (emit/wait) utilizzando lo stato reattivo di Svelte 5.
 */

/**
 * @brief Dizionario delle costanti per le azioni inviate dal Client verso il Server.
 */
export const ClientAction = {
    GamePlayCard: "game_play_card",
    GameDrawCard: "game_draw_card",
    GameSubmitInput: "game_submit_input",
    GameCallUno: "game_call_uno",
    GameExit: "game_exit",

    LobbyListSavedMatches: "lobby_list_saved_matches",
    LobbyDeleteSavedMatch: "lobby_delete_saved_match",
    LobbyResumeSavedMatch: "lobby_resume_saved_match",

    LobbyList: "lobby_list",
    LobbyCreate: "lobby_create",
    LobbyJoin: "lobby_join",
    LobbyRejoin: "lobby_rejoin",
    LobbyLeave: "lobby_leave",
    LobbyPromote: "lobby_promote",
    LobbyKick: "lobby_kick",
    LobbyUpdateSettings: "lobby_update_settings",
    LobbyStartMatch: "lobby_start_match",

    ChatSend: "chat_send"
} as const;

/**
 * @brief Dizionario delle costanti per le azioni inviate dal Server verso il Client.
 */
export const ServerAction = {
    Success: "success",
    Error: "error",

    GameStateUpdated: "game_state_updated",
    GameOver: "game_over",

    LobbyEvicted: "lobby_evicted",
    LobbyList: "lobby_list",
    LobbyUpdated: "lobby_updated",
    LobbyJoined: "lobby_joined",
    LobbyLeft: "lobby_left",

    ChatMessage: "chat_message"
} as const;

export type ClientActionType = (typeof ClientAction)[keyof typeof ClientAction];
export type ServerActionType = (typeof ServerAction)[keyof typeof ServerAction];

export type ServerActionDef = ServerActionType | string;

/**
 * @typedef MessageHandler
 * @brief Firma della funzione di callback per intercettare i messaggi WebSocket.
 */
export type MessageHandler = (data: Record<string, unknown>) => void;

/**
 * @interface ConnectionStatus
 * @brief Rappresenta lo stato reattivo della connessione WebSocket utile per la UI.
 */
export interface ConnectionStatus {
    /** Stato attuale della connessione. */
    status: "disconnected" | "connecting" | "connected";
    /** Username sincronizzato dal server. */
    username: string;
    /** Nome della stanza corrente. */
    room: string;
    /** Codice invito della lobby attuale. */
    lobby_code: string;
}

/**
 * @class WsResponse
 * @brief Wrapper unificato per le risposte dei messaggi WebSocket.
 * Mima l'API HTTP Fetch per fornire un flusso di controllo lineare e prevedibile
 * quando si attendono risposte dal server.
 * @tag FRONT-WS-001
 */
export class WsResponse {
    /** True se l'azione ritornata dal server NON è un errore. */
    public readonly ok: boolean;
    /** Il comando ServerAction ritornato nel pacchetto. */
    public readonly action: string;
    /** L'ID univoco della richiesta originaria inoltrata dal client, se presente. */
    public readonly request_id?: string;
    /** Il dizionario JSON grezzo coi dati restituiti dal server. */
    public readonly data: Record<string, unknown>;

    constructor(rawData: Record<string, unknown>) {
        this.data = rawData;
        this.action = (rawData.action as string) || "";
        this.request_id = rawData.request_id as string;

        this.ok = this.action !== "error";
    }

    /**
     * @brief Estrae in sicurezza il motivo dell'errore (reason) restituito dal server.
     * @returns Il messaggio di errore o un fallback predefinito.
     */
    get reason(): string {
        return (this.data.reason as string) || "Unknown Server Error";
    }

    /**
     * @brief Estrae un valore tipizzato dal payload.
     * @param key La chiave da cercare nel dizionario JSON.
     * @returns Il valore tipizzato o null se la chiave non esiste.
     */
    get<T>(key: string): T | null {
        return key in this.data ? (this.data[key] as T) : null;
    }

    /**
     * @brief Estrae un valore tipizzato dal payload fornendo un fallback in caso di assenza.
     * @param key La chiave da cercare nel payload JSON.
     * @param fallback Il valore di sicurezza da restituire.
     */
    getOr<T>(key: string, fallback: T): T {
        return key in this.data ? (this.data[key] as T) : fallback;
    }
}

/**
 * @interface PendingRequest
 * @brief Interfaccia ad uso interno per tracciare le richieste in attesa di risposta (emitAndWait).
 */
interface PendingRequest {
    resolve: (res: WsResponse) => void;
    reject: (err: Error) => void;
    timer: ReturnType<typeof setTimeout>;
}

/**
 * @class WebSocketClient
 * @brief Client WebSocket Stateful e Reattivo.
 * Gestisce riconnessioni automatiche (Exponential Backoff), correlazione tra richieste
 * e risposte, e incapsula gli stati della connessione sfruttando Svelte 5 (`$state`).
 * @tag FRONT-WS-002
 */
export class WebSocketClient {
    /** Istanza nativa del WebSocket (reattiva). */
    socket = $state<WebSocket | null>(null);

    /** Metadati e stato della connessione (reattivi per interfacciarsi con l'UI). */
    connectionStatus = $state<ConnectionStatus>({
        status: "disconnected",
        username: "",
        room: "",
        lobby_code: ""
    });

    private _nextRequestId = 1;
    private pendingRequests = new Map<string, PendingRequest>();

    private onHandlers = new Map<string, Set<MessageHandler>>();
    private openHandlers: Array<() => void | Promise<void>> = [];
    private closeHandlers: Array<() => void | Promise<void>> = [];

    private visibilityListenerAttached = false;

    private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
    private reconnectDelayMs = 1000;
    private intentionalClose = false;

    /**
     * @brief Verifica se il WebSocket è attualmente aperto e attivo.
     */
    get isConnected(): boolean {
        return this.socket?.readyState === WebSocket.OPEN;
    }

    /**
     * @brief Installa e stabilisce la connessione WebSocket. Sicuro da chiamare più volte.
     * @returns Promise risolta quando la connessione si apre con successo.
     */
    async connect(): Promise<void> {
        if (this.isConnected) return;

        this.intentionalClose = false;
        this.#attachVisibilityListener();
        await this._connectOnce();
    }

    /**
     * @brief Disconnette intenzionalmente il WebSocket bloccando il sistema di auto-riconnessione.
     * @param code Il codice di chiusura WebSocket.
     * @param reason La motivazione testuale per la chiusura.
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

        for (const handler of this.closeHandlers) {
            try {
                handler();
            } catch (error) {
                console.error("onClose Handler failed:", error);
            }
        }
    }

    /**
     * @brief Invia un messaggio asincrono al server senza attendere alcuna risposta (Fire and Forget).
     * Non allega nessun `request_id` al pacchetto.
     * @param action L'azione da compiere (ClientAction).
     * @param payload Dati opzionali da allegare in formato JSON.
     */
    emit(action: string, payload: Record<string, unknown> = {}): void {
        if (this.isConnected) {
            this.socket!.send(JSON.stringify({ action, ...payload }));
        }
    }

    /**
     * @brief Invia un messaggio e attende la risposta correlata dal server generando un `request_id`.
     * Risolve in una `WsResponse` anche per errori logici generati dal server, ma rigetta
     * in caso di fallimenti infrastrutturali (es. caduta rete o Timeout).
     * @param action L'azione da compiere (ClientAction).
     * @param payload Dati opzionali da allegare.
     * @param timeoutMs Tempo massimo di attesa prima di abortire (Default: 5000ms).
     * @returns Promise risolta col pacchetto di risposta del server.
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
     * @brief Registra un Listener globale per intercettare specifiche Server Action.
     * @param action L'azione del server da ascoltare (usa "*" per ascoltare tutto).
     * @param handler La funzione di callback innescata alla ricezione del messaggio.
     * @returns Una lambda per rimuovere l'iscrizione all'evento.
     */
    on(action: ServerActionDef | "*", handler: MessageHandler): () => void {
        if (!this.onHandlers.has(action)) this.onHandlers.set(action, new Set());
        this.onHandlers.get(action)!.add(handler);

        return () => this.onHandlers.get(action)?.delete(handler);
    }

    /**
     * @brief Registra un Listener innescato immediatamente all'apertura del socket.
     * @returns Funzione per rimuovere l'hook.
     */
    onOpen(handler: () => void | Promise<void>): () => void {
        this.openHandlers.push(handler);

        // Ritorna la funzione di cancellazione (unsubscriber)
        return () => {
            this.openHandlers = this.openHandlers.filter((h) => h !== handler);
        };
    }

    /**
     * @brief Registra un Listener innescato alla chiusura del socket.
     * @returns Funzione per rimuovere l'hook.
     */
    onClose(handler: () => void | Promise<void>): () => void {
        this.closeHandlers.push(handler);

        // Ritorna la funzione di cancellazione (unsubscriber)
        return () => {
            this.closeHandlers = this.closeHandlers.filter((h) => h !== handler);
        };
    }

    /**
     * @brief Routine interna per stabilire una singola connessione grezza e legare gli eventi nativi.
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

                for (const handler of this.openHandlers) {
                    try {
                        handler();
                    } catch (error) {
                        console.error("onOpen Handler failed:", error);
                    }
                }

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
     * @brief Smista il messaggio in ingresso risolvendo eventuali richieste pendenti o innescando Listener globali.
     */
    private _dispatch(data: Record<string, unknown>) {
        const action = data.action as string;
        const requestId = data.request_id as string;

        // INFO: Risolve le promise pendenti se è presente una corrispondenza con l'ID
        if (requestId && this.pendingRequests.has(requestId)) {
            const pending = this.pendingRequests.get(requestId)!;
            this.pendingRequests.delete(requestId);
            clearTimeout(pending.timer);

            // INFO: Inserisce il payload nel nostro inviluppo simile all'HTTP e risolve
            const response = new WsResponse(data);
            pending.resolve(response);
        }

        // INFO: Innesca i listener globali standard
        if (action) {
            this.onHandlers.get(action)?.forEach((handler) => handler(data));
            this.onHandlers.get("*")?.forEach((handler) => handler(data));
        }
    }

    /**
     * @brief Implementa un algoritmo di Exponential Backoff per tentare le riconnessioni.
     * Raddoppia il ritardo ad ogni tentativo fallito fino a un cap massimo.
     */
    private _scheduleReconnect() {
        if (this.intentionalClose) return;
        if (this.reconnectTimer) clearTimeout(this.reconnectTimer);

        this.reconnectTimer = setTimeout(() => {
            this.reconnectTimer = null;

            if (document.visibilityState === "hidden") return;

            this.reconnectDelayMs = Math.min(this.reconnectDelayMs * 2, 16_000);

            this._connectOnce().catch(() => {
                this._scheduleReconnect();
            });
        }, this.reconnectDelayMs);
    }

    /**
     * @brief Registra un Listener sul documento per rilevare il rientro dell'utente sulla scheda (tab).
     * Disattiva il timer di backoff forzando una riconnessione immediata qualora la scheda torni visibile.
     */
    #attachVisibilityListener() {
        if (this.visibilityListenerAttached) return;
        this.visibilityListenerAttached = true;

        document.addEventListener("visibilitychange", () => {
            if (document.visibilityState === "visible" && !this.isConnected && !this.intentionalClose) {
                // Annulla i timer pendenti e forza una connessione
                if (this.reconnectTimer) {
                    clearTimeout(this.reconnectTimer);
                    this.reconnectTimer = null;
                }

                this.reconnectDelayMs = 1000; // Reset del backoff
                this._connectOnce().catch(() => this._scheduleReconnect());
            }
        });
    }
}

// Esporta un'istanza singleton globale per l'utilizzo in tutta l'applicazione Svelte
export const ws = new WebSocketClient();
