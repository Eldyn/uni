/**
 * @file chat.svelte.ts
 * @brief Reactive chat/friends store, wired to real `ws` traffic.
 * Global/lobby/DM chat rides `chat_send`/`chat_message`; DM history is
 * hydrated on-demand via `chat_history_request`; friends ride
 * `friend_list_request`/`friend_list`/`friend_request`/`friend_response`.
 */

import { storeAuth } from "$stores/auth.svelte";
import { storeToast } from "./toast.svelte";
import { errorText } from "./errors";
import { ClientAction, ServerAction, ws } from "./ws.svelte";

const CHAT_FRIEND_COLORS = ["#f97373", "#60a5fa", "#4ade80", "#facc15", "#c084fc", "#fb923c"];

export interface ChatLine {
	id: string;
	username: string;
	color: string;
	text: string;
}

export type ChatChannel = "global" | "party" | { friendId: string };

export type ChatFriendStatus = "offline" | "online";

export interface ChatFriend {
	username: string;
	status: ChatFriendStatus;
	color: string;
}

const DRAFTS_STORAGE_KEY = "uni:chat:drafts";
const COMPOSER_ERROR_DURATION_MS = 5_000;

let nextLineId = 0;

/** Stable string key for a channel — used to index drafts/unread maps. */
export function channelKey(channel: ChatChannel): string {
	if (channel === "global") return "global";
	if (channel === "party") return "party";
	return `friend:${channel.friendId}`;
}

/** Deterministic username → colour, so a given author renders consistently. */
function colorFor(username: string): string {
	let hash = 0;
	for (let i = 0; i < username.length; i++) hash = (hash * 31 + username.charCodeAt(i)) | 0;
	return CHAT_FRIEND_COLORS[Math.abs(hash) % CHAT_FRIEND_COLORS.length];
}

function makeLine(username: string, text: string): ChatLine {
	return { id: `srv-${++nextLineId}`, username, color: colorFor(username), text };
}

class StoreChat {
	/** Whether the chat dock panel is expanded. */
	isOpen = $state(false);
	/**
	 * Measured pixel height of the open dock panel (0 when closed), set by
	 * `ChatDock` via `bind:clientHeight`. Lets other fixed-position UI (the
	 * toast stack) sit above the dock without duplicating its responsive
	 * breakpoints/height — one measured source of truth instead of two files
	 * agreeing on magic numbers.
	 */
	dockHeight = $state(0);
	/** Currently selected channel/friend thread. */
	activeChannel = $state<ChatChannel>("global");
	/**
	 * Transient inline feedback for the composer (unsolicited `error` frames
	 * from fire-and-forget actions like `chat_send`, which have no
	 * `emitAndWait` caller to report to). Cleared automatically after a few
	 * seconds or on the next successful send.
	 */
	composerError = $state("");

	#global = $state<ChatLine[]>([]);
	#party = $state<ChatLine[]>([]);
	#friendThreads = $state<Record<string, ChatLine[]>>({});
	#hydratedThreads = new Set<string>();

	#friends = $state<ChatFriend[]>([]);
	incomingRequests = $state<string[]>([]);
	outgoingRequests = $state<string[]>([]);

	/** In-progress message text per channel, keyed by channelKey. */
	private drafts = $state<Record<string, string>>({});
	/** Unseen message count per channel, keyed by channelKey. */
	private unread = $state<Record<string, number>>({});

	#listenersRegistered = false;
	#composerErrorTimer: ReturnType<typeof setTimeout> | null = null;

	constructor() {
		try {
			const raw = localStorage.getItem(DRAFTS_STORAGE_KEY);
			const parsed = raw ? JSON.parse(raw) : null;
			if (parsed && typeof parsed === "object" && !Array.isArray(parsed)) {
				this.drafts = parsed;
			}
		} catch {
			// localStorage unavailable or drafts payload malformed — start empty.
		}

		this.#registerListeners();
		ws.onOpen(() => {
			ws.emit(ClientAction.FriendListRequest);
		});
	}

	get friends(): ChatFriend[] {
		return this.#friends;
	}

	/** True when the socket has joined a lobby — gates the party tab/send. */
	get isPartyAvailable(): boolean {
		return ws.connectionStatus.lobby_code !== "";
	}

	/** Sums unread counts across every channel, for the launcher badge. */
	get totalUnread(): number {
		return Object.values(this.unread).reduce((sum, count) => sum + count, 0);
	}

	/** Returns the (live, reactive) line array for the given channel. */
	linesFor(channel: ChatChannel): ChatLine[] {
		if (channel === "global") return this.#global;
		if (channel === "party") return this.#party;
		return this.#friendThreads[channel.friendId] ?? [];
	}

	/** Returns the in-progress draft text for a channel, or "" if none. */
	draftFor(channel: ChatChannel): string {
		return this.drafts[channelKey(channel)] ?? "";
	}

	/** Persists a per-channel draft, synced to localStorage on every call. */
	setDraft(channel: ChatChannel, text: string): void {
		this.drafts = { ...this.drafts, [channelKey(channel)]: text };
		try {
			localStorage.setItem(DRAFTS_STORAGE_KEY, JSON.stringify(this.drafts));
		} catch {
			// localStorage unavailable — draft still lives in memory.
		}
	}

	unreadCount(channel: ChatChannel): number {
		return this.unread[channelKey(channel)] ?? 0;
	}

	/**
	 * Appends an incoming line to its channel and bumps unread, unless the
	 * dock is open and that channel is the one currently being viewed.
	 */
	receiveLine(channel: ChatChannel, line: ChatLine): void {
		if (channel === "global") this.#global = [...this.#global, line];
		else if (channel === "party") this.#party = [...this.#party, line];
		else {
			const existing = this.#friendThreads[channel.friendId] ?? [];
			this.#friendThreads = { ...this.#friendThreads, [channel.friendId]: [...existing, line] };
		}

		const key = channelKey(channel);
		if (this.isOpen && channelKey(this.activeChannel) === key) return;
		this.unread = { ...this.unread, [key]: (this.unread[key] ?? 0) + 1 };
	}

	/** Opens the dock and clears unread for the currently active channel. */
	open(): void {
		this.isOpen = true;
		const key = channelKey(this.activeChannel);
		if (this.unread[key]) this.unread = { ...this.unread, [key]: 0 };
	}

	close(): void {
		this.isOpen = false;
	}

	/**
	 * Switches channel; clears its unread while the dock is open, and
	 * hydrates a DM thread's history the first time it's opened.
	 */
	selectChannel(channel: ChatChannel): void {
		this.activeChannel = channel;

		if (this.isOpen) {
			const key = channelKey(channel);
			if (this.unread[key]) this.unread = { ...this.unread, [key]: 0 };
		}

		if (typeof channel === "object" && !this.#hydratedThreads.has(channel.friendId)) {
			this.#hydratedThreads.add(channel.friendId);
			void this.#hydrateHistory(channel.friendId);
		}
	}

	/** Sends a message on the active channel over the real WS connection. */
	send(text: string): void {
		const trimmed = text.trim();
		if (!trimmed) return;

		const channel = this.activeChannel;
		if (channel === "party" && !this.isPartyAvailable) {
			this.#showComposerError("Join a lobby to use party chat.");
			return;
		}

		if (channel === "global") {
			ws.emit(ClientAction.ChatSend, { message: trimmed, channel: "global" });
		} else if (channel === "party") {
			ws.emit(ClientAction.ChatSend, { message: trimmed, channel: "lobby" });
		} else {
			ws.emit(ClientAction.ChatSend, { message: trimmed, channel: "dm", target: channel.friendId });
		}

		this.setDraft(channel, "");
	}

	/** Sends a friend request; toasts on rejection. */
	async requestFriend(username: string): Promise<void> {
		await ws.connect();
		const res = await ws.emitAndWait(ClientAction.FriendRequest, { username });
		if (!res.ok) storeToast.error(res.message);
	}

	/** Accepts or rejects an incoming friend request; toasts on rejection. */
	async respondToRequest(username: string, accept: boolean): Promise<void> {
		await ws.connect();
		const res = await ws.emitAndWait(ClientAction.FriendResponse, { username, accept });
		if (!res.ok) storeToast.error(res.message);
	}

	async #hydrateHistory(friendId: string): Promise<void> {
		try {
			await ws.connect();
			const res = await ws.emitAndWait(ClientAction.ChatHistoryRequest, { target: friendId });
			if (!res.ok) {
				this.#hydratedThreads.delete(friendId);
				return;
			}
			const messages = res.getOr<Array<{ username: string; message: string }>>("messages", []);
			this.#friendThreads = {
				...this.#friendThreads,
				[friendId]: messages.map((m) => makeLine(m.username, m.message))
			};
		} catch {
			this.#hydratedThreads.delete(friendId);
		}
	}

	#showComposerError(text: string): void {
		if (this.#composerErrorTimer) clearTimeout(this.#composerErrorTimer);
		this.composerError = text;
		this.#composerErrorTimer = setTimeout(() => {
			this.composerError = "";
		}, COMPOSER_ERROR_DURATION_MS);
	}

	#registerListeners(): void {
		if (this.#listenersRegistered) return;
		this.#listenersRegistered = true;

		ws.on(ServerAction.ChatMessage, (data) => {
			const username = data.username as string;
			const message = data.message as string;
			const channel = data.channel as string;
			const target = data.target as string | undefined;
			const line = makeLine(username, message);

			if (channel === "global") this.receiveLine("global", line);
			else if (channel === "lobby") this.receiveLine("party", line);
			else if (channel === "dm") {
				const other = username === storeAuth.username ? target : username;
				if (other) this.receiveLine({ friendId: other }, line);
			}
		});

		// Unsolicited push on join (see ChatController::OnOpen /
		// CHAT_GLOBAL_HISTORY_ON_JOIN) — replaces #global outright rather than
		// appending, since it's the authoritative snapshot for this connection.
		// DM replies to chat_history_request carry channel: "dm" and are
		// already resolved directly by their emitAndWait() caller; ignored here.
		ws.on(ServerAction.ChatHistory, (data) => {
			if (data.channel !== "global") return;
			const messages = (data.messages as Array<{ username: string; message: string }>) ?? [];
			this.#global = messages.map((m) => makeLine(m.username, m.message));
		});

		ws.on(ServerAction.FriendList, (data) => {
			const friends = (data.friends as Array<{ username: string; online: boolean }>) ?? [];
			this.#friends = friends.map((f) => ({
				username: f.username,
				status: f.online ? "online" : "offline",
				color: colorFor(f.username)
			}));
			this.incomingRequests = (data.incoming_requests as string[]) ?? [];
			this.outgoingRequests = (data.outgoing_requests as string[]) ?? [];
		});

		// Fire-and-forget actions (chat_send) send an empty request_id on
		// failure, so their errors arrive unsolicited rather than through
		// emitAndWait — only surface them while the composer is visible.
		ws.on(ServerAction.Error, (data) => {
			if (!this.isOpen) return;
			const text = errorText(data.code as string | undefined, data.detail as string | undefined);
			if (text) this.#showComposerError(text);
		});
	}
}

export const chatStore = new StoreChat();
