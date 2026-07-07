/**
 * @file chat.svelte.ts
 * @brief Frontend-only chat store — backed by fixture data, no `ws` calls.
 * Deliberately isolated behind this one file so swapping the mock data for
 * real `ws.emit`/`ws.on` traffic later is a single-file change. See memory
 * `chat-system-design` for the real backend design.
 */

import { storeAuth } from "$stores/auth.svelte";
import {
	CHAT_FRIENDS,
	CHAT_MOCK_GLOBAL,
	CHAT_MOCK_PARTY,
	CHAT_MOCK_FRIEND_THREADS,
	type ChatFriend,
	type ChatLine
} from "$data/chatMock";

export type ChatChannel = "global" | "party" | { friendId: string };

const LOCAL_USER_COLOR = "#c084fc";
const DRAFTS_STORAGE_KEY = "uni:chat:drafts";

let nextLocalLineId = 0;

/** Stable string key for a channel — used to index drafts/unread maps. */
export function channelKey(channel: ChatChannel): string {
	if (channel === "global") return "global";
	if (channel === "party") return "party";
	return `friend:${channel.friendId}`;
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

	#global = $state<ChatLine[]>([...CHAT_MOCK_GLOBAL]);
	#party = $state<ChatLine[]>([...CHAT_MOCK_PARTY]);
	#friendThreads = $state<Record<string, ChatLine[]>>(
		Object.fromEntries(
			Object.entries(CHAT_MOCK_FRIEND_THREADS).map(([id, lines]) => [id, [...lines]])
		)
	);

	/** In-progress message text per channel, keyed by channelKey. */
	private drafts = $state<Record<string, string>>({});
	/** Unseen message count per channel, keyed by channelKey. */
	private unread = $state<Record<string, number>>({});

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
	}

	get friends(): ChatFriend[] {
		return CHAT_FRIENDS;
	}

	/** Sums unread counts across every channel, for the launcher badge. */
	get totalUnread(): number {
		return Object.values(this.unread).reduce((sum, count) => sum + count, 0);
	}

	/** Returns the (live, reactive) line array for the given channel. */
	linesFor(channel: ChatChannel): ChatLine[] {
		if (channel === "global") return this.#global;
		if (channel === "party") return this.#party;
		return (this.#friendThreads[channel.friendId] ??= []);
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
	 * Wiring point for a future real backend integration: appends an
	 * incoming line to its channel and bumps unread, unless the dock is
	 * open and that channel is the one currently being viewed.
	 */
	receiveLine(channel: ChatChannel, line: ChatLine): void {
		this.linesFor(channel).push(line);

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

	/** Switches channel; clears its unread too, but only while the dock is open. */
	selectChannel(channel: ChatChannel): void {
		this.activeChannel = channel;
		if (!this.isOpen) return;

		const key = channelKey(channel);
		if (this.unread[key]) this.unread = { ...this.unread, [key]: 0 };
	}

	/** Appends an optimistic local line to the currently active channel. */
	send(text: string): void {
		const trimmed = text.trim();
		if (!trimmed) return;

		const line: ChatLine = {
			id: `local-${++nextLocalLineId}`,
			username: storeAuth.username || "You",
			color: LOCAL_USER_COLOR,
			text: trimmed
		};
		this.linesFor(this.activeChannel).push(line);
		this.setDraft(this.activeChannel, "");
	}
}

export const chatStore = new StoreChat();
