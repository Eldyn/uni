/**
 * @file lobby.svelte.ts
 * @brief Reactive global store for managing lobbies and saved matches.
 * Handles creation, joining, settings updates and the public list.
 */

import { z } from "zod";
import { MAX_LOBBY_MEMBERS } from "$lib/generated/schemas";
import { failureText } from "./errors";
import { storeAnalytics } from "./analytics.svelte";
import { storeNavigation } from "./navigation.svelte";
import { storeToast } from "./toast.svelte";
import { ClientAction, ServerAction, ws } from "./ws.svelte";

/**
 * @enum BotTakeoverMode
 * @brief Behaviour mode when a bot replaces or is replaced by a player.
 */
export enum BotTakeoverMode {
	/** The bot plays its move instantly as soon as its turn arrives. */
	PlayInstantly,
	/** The bot waits up to 5 seconds for its turn, or until the end of the turn for other AFK players. */
	WaitUntilTurnEnd
}

/**
 * @interface SavedMatch
 * @brief Basic data of a match saved on the server.
 */
export interface SavedMatch {
	/** Unique identifier of the save in the database. */
	match_id: string;
	/** Date and time of the save. */
	saved_at: string;
	/** List of the usernames of the players present in that match. */
	players: string[];
}

/**
 * @interface LobbySettings
 * @brief Represents the configuration and rules chosen for the lobby.
 * Defines the match parameters and the composition of the initial deck.
 */
export interface LobbySettings {
	/** If true, the lobby will appear in the public list of matches. */
	is_public: boolean;
	/** Array of special rules (Mods) enabled for this match. */
	active_mods: string[];
	/** Time limit in milliseconds allowed to complete a turn (e.g. 15000). */
	turn_time_limit_ms: number;

	/** If true, the match state will be saved to the database on every move. */
	save_state: boolean;
	/** If true, a player's voluntary quit will permanently delete the save. */
	quit_deletes_match: boolean;

	/** Number of cards dealt to each player at the start of the match. */
	starting_cards: number;

	/** Number of bots to add automatically when the match starts. */
	bot_count: number;
	/** Bot behaviour (e.g. play immediately or wait for the timer). */
	bot_mode: BotTakeoverMode;
	/** If true, new human players can join an ongoing match by replacing a bot. */
	allow_bot_takeover: boolean;
	/** If true, a human player who quits is replaced by a bot instead of ending the game. */
	allow_bot_replacement: boolean;

	/** Copies per colour of the number 0 in the deck. */
	count_zeros: number;
	/** Copies per colour of the numbers 1 to 9 in the deck. */
	count_numbered: number;
	/** Copies per colour of the "Skip" card. */
	count_skips: number;
	/** Copies per colour of the "Reverse" card. */
	count_reverses: number;
	/** Copies per colour of the "Draw Two (+2)" card. */
	count_draw_two: number;
	/** Total number of Wild (Colour Change) cards in the deck. */
	count_wild: number;
	/** Total number of Wild Draw Four (+4) cards in the deck. */
	count_wild_draw_four: number;
}

/**
 * @interface LobbyMember
 * @brief Represents a single connected player inside a lobby.
 */
export interface LobbyMember {
	/** The display name of the player. */
	username: string;
	/** Indicates whether the player is currently connected via WebSocket or is reconnecting. */
	is_connected: boolean;
	/** True if this player is the owner/host of the lobby and can start the match. */
	is_host: boolean;
	/** True if this "member" is actually a bot account managed by the server. */
	is_bot: boolean;
}

/**
 * @interface Lobby
 * @brief Represents a complete game lobby with all its state data.
 */
export interface Lobby {
	/** The unique 6-character alphanumeric code used to join. */
	invite_code: string;
	/** The username of the creator (Host) of the lobby. */
	host: string;
	/** The custom name of the lobby shown in the public list. */
	name: string;
	/** Detailed list of the players currently in the lobby. */
	members: LobbyMember[];
	/** The settings configured for this game room. */
	settings: LobbySettings;
}

/**
 * @interface ListedLobby
 * @brief Minimized data sent by the server to render the list of public lobbies.
 * Saves bandwidth by not sending the full member list and the detailed rules.
 */
export interface ListedLobby {
	/** The custom name of the lobby. */
	name: string;
	/** Total number of human players currently connected. */
	member_count: number;
	/** Total number of bots present in this lobby. */
	bot_count: number;
	/** The invite code allowing the client to join with one click. */
	invite_code: string;
	/** Derived lobby status: "open" | "in-game" | "full". */
	status: "open" | "in-game" | "full";
	/** Special rules (mods) currently active in this lobby. */
	active_mods: string[];
	/** If true, a human player can join an in-progress match by replacing a bot. */
	allow_bot_takeover: boolean;
}

// INFO: Incoming lobby payloads are validated at the WS boundary — outgoing //
// frames already go through the generated Zod schemas, incoming ones don't. //
// Loose objects: unknown extra fields pass through untouched.               //

const LobbyMemberSchema = z.looseObject({
	username: z.string(),
	is_connected: z.boolean(),
	is_host: z.boolean(),
	is_bot: z.boolean()
});

const LobbySchema = z.looseObject({
	invite_code: z.string(),
	host: z.string(),
	name: z.string(),
	members: z.array(LobbyMemberSchema).default([]),
	settings: z.looseObject({
		active_mods: z.array(z.string()).default([]),
		allow_bot_takeover: z.boolean().default(false)
	})
});

const ListedLobbySchema = z.looseObject({
	name: z.string(),
	member_count: z.number().default(0),
	bot_count: z.number().default(0),
	invite_code: z.string(),
	status: z.enum(["open", "in-game", "full"]).default("open"),
	active_mods: z.array(z.string()).default([]),
	allow_bot_takeover: z.boolean().default(false)
});

/** Parses a raw lobby payload; returns null (and logs) when malformed. */
function parseLobby(raw: unknown): Lobby | null {
	const parsed = LobbySchema.safeParse(raw);
	if (!parsed.success) {
		console.error("[lobby] Malformed lobby payload:", parsed.error.issues);
		return null;
	}
	return parsed.data as unknown as Lobby;
}

/**
 * @class StoreLobby
 * @brief Singleton store that manages all lobby-related state and the associated WebSocket interactions.
 * @tag FRONT-LOBBY-001
 */
class StoreLobby {
	/** Saved matches compatible with the current player list. Null if not in a lobby. */
	savedMatches = $state<SavedMatch[] | null>(null);
	/** The lobby the user is currently in. Null if not in a lobby. */
	current = $state<Lobby | null>(null);

	/** The list of public lobbies available to join. */
	available = $state<ListedLobby[]>([]);

	/** True while fetching the list of public lobbies from the server. */
	isLoadingList = $state(false);

	/**
	 * True when the most recent fetchList() failed (server rejection, malformed
	 * payload, or network/WS error) — distinct from a successful fetch that
	 * legitimately found zero lobbies, so the UI can tell "nothing to join"
	 * apart from "couldn't check."
	 */
	listError = $state(false);

	/** True while a lobby creation or join request is in progress. */
	isLoadingJoin = $state(false);

	/** True while the list of saved matches is being fetched. */
	isLoadingSavedMatchList = $state(false);

	/** True while a start-match request is in flight. */
	isLoadingStart = $state(false);

	#listenersRegistered = false;

	// Shared match-redirect guard: LobbyJoined and #tryRejoin both wait for a
	// MatchStateUpdated frame right after (re)join; only one listener may be
	// armed at a time so goto("game") fires at most once.
	#matchRedirectUnsub: (() => void) | null = null;
	#matchRedirectTimer: ReturnType<typeof setTimeout> | null = null;

	/**
	 * @brief Derived property to quickly check whether the user is in a lobby.
	 * @returns True if the user is in a lobby, false otherwise.
	 */
	get isInLobby(): boolean {
		return this.current !== null;
	}

	/**
	 * @brief Initializes the store and binds the basic connection events.
	 * @tag FRONT-LOBBY-MTH-001
	 */
	constructor() {
		this.#registerListeners();
		ws.onOpen(async () => {
			await this.#tryRejoin();
		});
	}

	/**
	 * @brief Promotes a user to the host role of the lobby.
	 * @param username The name of the player to promote.
	 * @tag FRONT-LOBBY-MTH-002
	 */
	async promote(username: string): Promise<void> {
		await ws.connect();
		const response = await ws.emitAndWait(ClientAction.LobbyPromote, { username });

		if (!response.ok) {
			storeToast.error(response.message);
			return;
		}

		storeToast.success(`Promoted ${username}!`);
	}

	/**
	 * @brief Kicks a player from the lobby.
	 * @param username The name of the player to kick.
	 * @tag FRONT-LOBBY-MTH-003
	 */
	async kick(username: string): Promise<void> {
		await ws.connect();
		const response = await ws.emitAndWait(ClientAction.LobbyKick, { username });

		if (!response.ok) {
			storeToast.error(response.message);
			return;
		}

		storeToast.success(`Kicked ${username}!`);
	}

	/**
	 * @brief Requests the server to start the match for the current lobby.
	 * Shows an error toast if the server rejects the request.
	 * @tag FRONT-LOBBY-MTH-004A
	 */
	async startMatch(): Promise<void> {
		if (this.isLoadingStart) return;
		this.isLoadingStart = true;
		try {
			await ws.connect();
			const response = await ws.emitAndWait(ClientAction.LobbyStartMatch);
			if (!response.ok) {
				storeToast.error(response.message);
			} else {
				this.#trackMatchStart();
			}
		} finally {
			this.isLoadingStart = false;
		}
	}

	/**
	 * @brief Reports a started match and its ruleset to analytics (host-only).
	 * Only the host can call `startMatch()`, so this fires exactly once per game,
	 * keeping match and rule-usage counts accurate. The unused `count_*` deck
	 * fields are deliberately omitted. See analytics.svelte.ts for the data-use
	 * policy: gameplay research only, no personal data.
	 * @tag FRONT-LOBBY-PRIV-002
	 */
	#trackMatchStart(): void {
		const s = this.current?.settings;
		const active_mods = s?.active_mods ?? [];
		const humanCount = this.current?.members.filter((m) => !m.is_bot).length;
		storeAnalytics.track("match_start", {
			settings_json: JSON.stringify({
				active_mods,
				is_public: s?.is_public,
				turn_time_limit_ms: s?.turn_time_limit_ms,
				starting_cards: s?.starting_cards,
				bot_count: s?.bot_count,
				bot_mode: s?.bot_mode,
				allow_bot_takeover: s?.allow_bot_takeover,
				allow_bot_replacement: s?.allow_bot_replacement,
				save_state: s?.save_state,
				quit_deletes_match: s?.quit_deletes_match
			}),
			mods: active_mods.join(",") || "none",
			mod_count: active_mods.length,
			starting_cards: s?.starting_cards,
			turn_time_limit_ms: s?.turn_time_limit_ms,
			bot_count: s?.bot_count,
			player_count: humanCount
		});
	}

	/**
	 * @brief Updates the lobby settings (including name and visibility).
	 * @param settings The settings fields to modify.
	 * @tag FRONT-LOBBY-MTH-004
	 */
	async updateSettings(settings: Partial<LobbySettings> & Partial<{ name: string }>) {
		try {
			await ws.connect();
			const response = await ws.emitAndWait(ClientAction.LobbyUpdateSettings, settings);

			if (!response.ok) {
				storeToast.error(response.message);
				return;
			}

			storeToast.success("Settings updated!");
		} catch (error) {
			storeToast.error(failureText(error));
		}
	}

	/**
	 * @brief Creates a new game lobby on the server.
	 * Automatically connects to the WebSocket and emits the creation payload.
	 * @param data The configuration for the new lobby.
	 * @returns True when the lobby was created, false on rejection or network error.
	 * @tag FRONT-LOBBY-MTH-005
	 */
	async create(data: { is_public: boolean; name: string }): Promise<boolean> {
		this.isLoadingJoin = true;

		try {
			await ws.connect();
			const response = await ws.emitAndWait(ClientAction.LobbyCreate, data);

			if (!response.ok) {
				storeToast.error(response.message);
				return false;
			}
			storeAnalytics.track("lobby_create", { is_public: data.is_public });
			return true;
		} catch (error) {
			storeToast.error(failureText(error));
			return false;
		} finally {
			this.isLoadingJoin = false;
		}
	}

	/**
	 * @brief Joins an existing lobby using a 6-character invite code.
	 * @param code The unique 6-character identifier of the lobby.
	 * @returns True when the lobby was joined, false on rejection or network error.
	 * @tag FRONT-LOBBY-MTH-006
	 */
	async join(code: string): Promise<boolean> {
		if (!code) {
			storeToast.error("Enter an invite code.");
			return false;
		}

		this.isLoadingJoin = true;

		try {
			await ws.connect();
			const response = await ws.emitAndWait(ClientAction.LobbyJoin, { code: code.toUpperCase() });

			if (!response.ok) {
				storeToast.error(response.message);
				return false;
			}
			storeAnalytics.track("lobby_join");
			return true;
		} catch (error) {
			storeToast.error(failureText(error));
			return false;
		} finally {
			this.isLoadingJoin = false;
		}
	}

	/**
	 * @brief Fetches the updated list of available public lobbies from the server.
	 * Updates the `available` state array.
	 * @returns A Promise that resolves when the list has been updated.
	 * @tag FRONT-LOBBY-MTH-007
	 */
	async fetchList(): Promise<void> {
		if (this.isLoadingList) return; // Poll ticks must not overlap an in-flight fetch.
		this.isLoadingList = true;

		try {
			await ws.connect();
			const response = await ws.emitAndWait(ClientAction.LobbyList);

			if (!response.ok) {
				this.available = [];
				this.listError = true;
				return;
			}
			const parsed = z.array(ListedLobbySchema).safeParse(response.get("lobbies") ?? []);
			if (!parsed.success) {
				console.error("[lobby] Malformed lobby list payload:", parsed.error.issues);
				this.listError = true;
				return;
			}
			this.available = parsed.data as ListedLobby[];
			this.listError = false;
		} catch (error) {
			// Toast only on the transition into failure, not every retry —
			// LobbyBrowse polls this every 8s, and once broken the always-
			// visible error card (with its own Retry action) already says so;
			// re-toasting each tick would just be noise on top of that.
			if (!this.listError) storeToast.error(failureText(error));
			this.listError = true;
		} finally {
			this.isLoadingList = false;
		}
	}

	/**
	 * @brief Emits a request to leave the current lobby.
	 * @tag FRONT-LOBBY-MTH-008
	 */
	async leave(): Promise<void> {
		try {
			// emit() silently drops frames on a closed socket — reconnect first so
			// the server actually processes the leave and echoes LobbyLeft back.
			await ws.connect();
			ws.emit(ClientAction.LobbyLeave);
			storeAnalytics.track("lobby_leave");
		} catch {
			this.#reset();
			storeNavigation.goto("lobbies");
			storeToast.error("Connection lost — left the lobby locally.");
		}
	}

	/**
	 * @brief Subscribes to all WebSocket events sent by the server related to lobbies.
	 * Handles events such as joining, receiving updates or being evicted.
	 * @tag FRONT-LOBBY-PRIV-001
	 */
	#registerListeners(): void {
		if (this.#listenersRegistered) return;
		this.#listenersRegistered = true;

		ws.on(ServerAction.LobbyJoined, async (data) => {
			const lobby = parseLobby(data.lobby);
			if (!lobby) return;
			const alreadyInThisLobby = this.current?.invite_code === lobby.invite_code;

			this.current = lobby;
			localStorage.setItem("lobby_code", lobby.invite_code);

			// Deduplicate: OnOpen push + HandleRejoin response both fire this handler.
			// Only the first arrival does navigation / listener / fetch.
			if (alreadyInThisLobby) return;

			// If a match state follows immediately (server-proactive reconnect with active match),
			// navigate to game. The 500ms window is generous — both messages are sent back-to-back.
			this.#armMatchRedirect(500);

			storeNavigation.goto("lobby");
			await this.#fetchSavedMatches();
		});

		ws.on(ServerAction.LobbyUpdated, async (data) => {
			const updatedLobby = parseLobby(data.lobby);
			if (!updatedLobby) return;

			// Guard on invite_code: never let a stray broadcast for another
			// lobby overwrite the one the user is actually in.
			if (this.current?.invite_code === updatedLobby.invite_code) {
				this.current = updatedLobby;
			}

			const idx = this.available.findIndex((l) => l.invite_code === updatedLobby.invite_code);
			if (idx !== -1) {
				// INFO: The update payload carries no member_count/bot_count fields —  //
				// derive both from the members list to match the LobbyList semantics.  //
				const humans = updatedLobby.members.filter((m) => !m.is_bot).length;
				this.available[idx].member_count = humans;
				this.available[idx].bot_count = updatedLobby.members.length - humans;
				this.available[idx].name = updatedLobby.name;
				this.available[idx].active_mods = updatedLobby.settings.active_mods;
				this.available[idx].allow_bot_takeover = updatedLobby.settings.allow_bot_takeover;
				// The update payload carries no match info: keep "in-game" from the
				// last fetchList(), but track full/open live from the member count.
				if (this.available[idx].status !== "in-game") {
					this.available[idx].status =
						updatedLobby.members.length >= MAX_LOBBY_MEMBERS ? "full" : "open";
				}
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

	/**
	 * @brief Arms the shared one-shot redirect to the game screen.
	 * Waits up to `windowMs` for a MatchStateUpdated frame; re-arming cancels
	 * any previous listener so the redirect can never fire twice.
	 */
	#armMatchRedirect(windowMs: number): void {
		this.#disarmMatchRedirect();
		this.#matchRedirectUnsub = ws.on(ServerAction.MatchStateUpdated, () => {
			this.#disarmMatchRedirect();
			storeNavigation.goto("game");
		});
		this.#matchRedirectTimer = setTimeout(() => this.#disarmMatchRedirect(), windowMs);
	}

	#disarmMatchRedirect(): void {
		if (this.#matchRedirectTimer) clearTimeout(this.#matchRedirectTimer);
		this.#matchRedirectTimer = null;
		this.#matchRedirectUnsub?.();
		this.#matchRedirectUnsub = null;
	}

	/**
	 * @brief Fetches from the server the previous saves compatible with the players in the lobby.
	 * @tag FRONT-LOBBY-PRIV-002
	 */
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
			storeToast.error("Saved games list error");
		} finally {
			this.isLoadingSavedMatchList = false;
		}
	}

	/**
	 * @brief Silently attempts to reconnect to a lobby using a stored session code.
	 * Reads `lobby_code` from localStorage. Triggered automatically after a reload or disconnection.
	 * @returns A Promise that resolves when the reconnection flow completes.
	 * @tag FRONT-LOBBY-PRIV-003
	 */
	async #tryRejoin(): Promise<void> {
		const code = localStorage.getItem("lobby_code");
		if (!code) return;

		try {
			// Guard against MatchStateUpdated leaking past a failed or no-match rejoin.
			this.#armMatchRedirect(1000);

			const response = await ws.emitAndWait(ClientAction.LobbyRejoin, { code });

			if (!response.ok) {
				this.#disarmMatchRedirect();
				this.#reset();
				if (response.action !== ServerAction.LobbyEvicted) {
					storeToast.error(`Could not rejoin lobby: ${response.message}`);
				}
				return;
			}

			const lobby = parseLobby(response.get("lobby"));
			if (!lobby) throw new Error("Invalid lobby data received.");

			// If the LobbyJoined event from OnOpen already populated state for this lobby,
			// skip duplicate navigation and fetch — the event handler already did the work.
			if (this.current?.invite_code === lobby.invite_code) return;

			this.current = lobby;
			storeNavigation.goto("lobby");

			await this.#fetchSavedMatches();
		} catch {
			this.#disarmMatchRedirect();
			this.#reset();
		}
	}

	/**
	 * @brief Clears the active lobby state and local storage.
	 * @tag FRONT-LOBBY-PRIV-004
	 */
	#reset(): void {
		this.current = null;
		localStorage.removeItem("lobby_code");
	}
}

export const storeLobby = new StoreLobby();
