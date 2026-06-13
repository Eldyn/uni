import { storeNavigation } from "./navigation.svelte";
import { storeToast } from "./toast.svelte";
import { ClientAction, ServerAction, ws } from "./ws.svelte";

export enum BotTakeoverMode {
    PlayInstantly,
    WaitUntilTurnEnd
}

export enum Rule {
    SevenZero = "seven_zero"
}

export interface SavedMatch {
    match_id: string;
    saved_at: string;
    players: string[];
    // TODO: pass from the backend more data
}
/**
 * Represents a lobby's settings
 *
 */
export interface LobbySettings {
    is_public: boolean;
    active_mods: Rule[];
    turn_time_limit_ms: number;

    save_state: boolean;
    quit_deletes_match: boolean;

    starting_cards: number;

    bot_count: number;
    bot_mode: BotTakeoverMode;
    allow_bot_takeover: boolean;
    allow_bot_replacement: boolean;

    count_zeros: number;
    count_numbered: number;
    count_skips: number;
    count_reverses: number;
    count_draw_two: number;
    count_wild: number;
    count_wild_draw_four: number;
}

/**
 * Represents a single player inside a lobby.
 */
export interface LobbyMember {
    /** The display name of the player. */
    username: string;
    /** Whether the player is currently connected via WebSocket. */
    is_connected: boolean;
    /** True if this player is the owner/host of the lobby. */
    is_host: boolean;
    /** True if this is a bot account. */
    is_bot: boolean;
}

/**
 * Represents a game lobby.
 */
export interface Lobby {
    /** The unique 6-character code used to join the lobby. */
    invite_code: string;
    /** The username of the lobby host. */
    host: string;
    /** The display name of the lobby. */
    name: string;
    /** The current number of players in the lobby (max 4). */
    member_count: number;
    /** List of players currently in the lobby. */
    members: LobbyMember[];

    settings: LobbySettings;
}

/**
 * Represents a game lobby with minimal data for a browsing list.
 */
export interface ListedLobby {
    name: string;
    member_count: number;
    bot_count: number;
    invite_code: string;
}

/**
 * Singleton store managing all Lobby-related state and WebSocket interactions.
 */
class StoreLobby {
    /** The saved matches that match the current player list. Null if not in a lobby. */
    savedMatches = $state<SavedMatch[] | null>(null);
    /** The lobby the user is currently inside. Null if not in a lobby. */
    current = $state<Lobby | null>(null);

    /** The list of public lobbies available to join. */
    available = $state<ListedLobby[]>([]);

    /** True when fetching the public lobby list from the server. */
    isLoadingList = $state(false);

    /** True when a create or join request is pending. */
    isLoadingJoin = $state(false);

    /** True when a list of saved matches is pending  */
    isLoadingSavedMatchList = $state(false);

    /**
     * Computed property to quickly check if the user is currently in a lobby.
     * @returns True if the user is in a lobby, false otherwise.
     */
    get isInLobby(): boolean {
        return this.current !== null;
    }

    /**
     * Initializes the store and binds core connection events.
     * @remarks
     * * **Code Number:** #LOBBY-000
     */
    constructor() {
        ws.onOpen(async () => {
            this.#registerListeners();
            await this.#tryRejoin();
        });
    }

    async promote(username: string): Promise<void> {
        await ws.connect();
        ws.emit(ClientAction.LobbyPromote, { username });

        storeToast.success(`Promoted ${username}!`);
    }

    async kick(username: string): Promise<void> {
        await ws.connect();
        ws.emit(ClientAction.LobbyKick, { username });

        storeToast.success(`Kicked ${username}!`);
    }

    /**
     * Updates the lobby settings (name and visibility).
     * @param settings - The settings fields to modify.
     */
    async updateSettings(
        settings: Partial<LobbySettings> & Partial<{ name: string }>
    ): Promise<void> {
        try {
            await ws.connect();
            const response = await ws.emitAndWait(ClientAction.LobbyUpdateSettings, settings);
            if (!response.ok) {
                storeToast.error(response.reason);
                return;
            }

            storeToast.success("Impostazioni Aggiornate!");
        } catch (error) {
            storeToast.error("Failed to update lobby settings.");
        }
    }

    /**
     * Creates a new game lobby on the server.
     * @remarks
     * **Context:** Triggered when the user submits the `LobbyCreateForm`.
     * Automatically connects the WebSocket and emits the creation payload.
     * **Code Number:** #LOBBY-001
     * @param data - The configuration for the new lobby.
     * @param data.is_public - Whether the lobby is visible in the public list.
     * @param data.name - The custom name of the lobby.
     * @returns A Promise resolving when the network request completes.
     */
    async create(data: { is_public: boolean; name: string }): Promise<void> {
        this.isLoadingJoin = true;

        try {
            await ws.connect();
            const response = await ws.emitAndWait(ClientAction.LobbyCreate, data);

            if (!response.ok) {
                storeToast.error(response.reason);
            }
        } finally {
            this.isLoadingJoin = false;
        }
    }

    /**
     * Joins an existing lobby using a 6-character invite code.
     * @remarks
     * **Context:** Triggered from `LobbyJoinForm` or by clicking a public lobby card.
     * **Code Number:** #LOBBY-002
     * @param code - The 6-character unique identifier for the lobby.
     * @returns A Promise resolving when the join attempt completes.
     */
    async join(code: string): Promise<void> {
        if (!code) {
            storeToast.error("Enter an invite code.");
            return;
        }

        this.isLoadingJoin = true;

        try {
            await ws.connect();
            const response = await ws.emitAndWait(ClientAction.LobbyJoin, { code });

            if (!response.ok) {
                storeToast.error(response.reason);
            }
        } finally {
            this.isLoadingJoin = false;
        }
    }

    /**
     * Fetches the latest list of public, available lobbies from the server.
     * @remarks
     * **Context:** Triggered when the user mounts the `LobbiesScreen` or clicks the refresh button.
     * Updates the `available` state array.
     * **Code Number:** #LOBBY-003
     * @returns A Promise resolving when the list has been updated.
     */
    async fetchList(): Promise<void> {
        this.isLoadingList = true;

        try {
            await ws.connect();
            const response = await ws.emitAndWait(ClientAction.LobbyList);

            if (response.ok) {
                this.available = response.get<ListedLobby[]>("lobbies") ?? [];
            }
        } finally {
            this.isLoadingList = false;
        }
    }

    /**
     * Emits a request to leave the current lobby.
     * @remarks
     * **Context:** Triggered when the user clicks "Leave" in the `LobbyScreen`.
     * **Code Number:** #LOBBY-004
     */
    leave(): void {
        ws.emit(ClientAction.LobbyLeave);
    }

    /**
     * Subscribes to all server-pushed WebSocket events related to lobbies.
     * @remarks
     * **Context:** Called automatically upon WebSocket connection.
     * Handles events like joining, receiving updates, or being kicked.
     * **Code Number:** #LOBBY-005
     */
    #registerListeners(): void {
        ws.on(ServerAction.LobbyJoined, async (data) => {
            const lobby = data.lobby as Lobby;
            this.current = lobby;

            localStorage.setItem("lobby_code", lobby.invite_code);
            storeNavigation.goto("lobby");

            await this.#fetchSavedMatches();
        });

        ws.on(ServerAction.LobbyUpdated, async (data) => {
            const updatedLobby = data.lobby as Lobby;
            if (!updatedLobby) return;

            if (this.current) {
                this.current = updatedLobby;
                console.log(this.current);
            }

            const idx = this.available.findIndex((l) => l.invite_code === updatedLobby.invite_code);
            if (idx !== -1) {
                this.available[idx].member_count = updatedLobby.member_count;
                this.available[idx].bot_count = updatedLobby.settings.bot_count;
                this.available[idx].name = updatedLobby.name;
            }

            await this.#fetchSavedMatches();
        });

        const handleDisconnection = () => {
            this.#reset();
            storeNavigation.goto("lobbies");
        };

        ws.on(ServerAction.LobbyLeft, handleDisconnection);
        ws.on(ServerAction.LobbyEvicted, handleDisconnection);
    }

    async #fetchSavedMatches(): Promise<void> {
        if (!this.current) return;

        this.isLoadingSavedMatchList = true;
        try {
            const result = await ws.emitAndWait(ClientAction.LobbyListSavedMatches);
            if (result.ok) {
                this.savedMatches = result.getOr<SavedMatch[]>("saved_matches", []);
            } else {
                storeToast.error("Saved games list error");
            }
        } catch (err) {
            console.error("Failed to parse saved rooms details:", err);
        } finally {
            this.isLoadingSavedMatchList = false;
        }
    }

    /**
     * Silently attempts to rejoin a lobby using a stored session code.
     * @remarks
     * **Context:** Triggered automatically upon page reload or unexpected WebSocket disconnects.
     * Reads `lobby_code` from localStorage.
     * **Code Number:** #LOBBY-006
     * @returns A Promise resolving when the rejoin flow completes.
     */
    async #tryRejoin(): Promise<void> {
        const code = localStorage.getItem("lobby_code");
        if (!code) return;

        try {
            const unsubscribeGameRejoin = ws.on(ServerAction.GameStateUpdated, (_data) => {
                storeNavigation.goto("game");

                unsubscribeGameRejoin();
            });

            const response = await ws.emitAndWait(ClientAction.LobbyRejoin, { code });

            if (!response.ok) {
                this.#reset();
                if (response.action !== ServerAction.LobbyEvicted) {
                    storeToast.error(`Could not rejoin lobby: ${response.reason}`);
                }
                return;
            }

            const lobby = response.get<Lobby>("lobby");
            if (!lobby) throw new Error("Invalid lobby data received.");

            this.current = lobby;
            storeNavigation.goto("lobby");

            await this.#fetchSavedMatches();
        } catch (error) {
            this.#reset();
        }
    }

    /**
     * Clears the active lobby state and local storage.
     * @remarks
     * **Context:** Helper function called during disconnects, evictions, or failed rejoins.
     * **Code Number:** #LOBBY-007
     */
    #reset(): void {
        this.current = null;
        localStorage.removeItem("lobby_code");
    }
}

export const storeLobby = new StoreLobby();
