import { describe, it, expect, vi, beforeEach } from "vitest";

const { mockEmit, mockEmitAndWait, mockConnect, mockOn, handlers, connectionStatus } = vi.hoisted(
	() => {
		const handlers: Record<string, Array<(data: Record<string, unknown>) => void>> = {};
		return {
			handlers,
			mockEmit: vi.fn(),
			mockEmitAndWait: vi.fn(),
			mockConnect: vi.fn().mockResolvedValue(undefined),
			mockOn: vi.fn((action: string, handler: (data: Record<string, unknown>) => void) => {
				(handlers[action] ??= []).push(handler);
				return () => {};
			}),
			connectionStatus: { status: "connected", username: "", room: "", lobby_code: "" }
		};
	}
);

vi.mock("$lib/stores/ws.svelte", () => ({
	ws: {
		emit: mockEmit,
		emitAndWait: mockEmitAndWait,
		on: mockOn,
		onOpen: vi.fn(() => vi.fn()),
		connect: mockConnect,
		connectionStatus
	},
	ClientAction: {
		ChatSend: "chat_send",
		ChatHistoryRequest: "chat_history_request",
		FriendRequest: "friend_request",
		FriendResponse: "friend_response",
		FriendListRequest: "friend_list_request"
	},
	ServerAction: {
		ChatMessage: "chat_message",
		ChatHistory: "chat_history",
		FriendList: "friend_list",
		Error: "error"
	}
}));

import { chatStore } from "$lib/stores/chat.svelte";
import { storeAuth } from "$lib/stores/auth.svelte";

const fire = (action: string, data: Record<string, unknown>) => {
	for (const h of handlers[action] ?? []) h(data);
};

const okResponse = (data: Record<string, unknown> = {}) => ({
	ok: true,
	message: "",
	action: "success",
	get: (key: string) => data[key] ?? null,
	getOr: (key: string, fallback: unknown) => data[key] ?? fallback
});

const errorResponse = (message = "Error occurred") => ({
	ok: false,
	message,
	action: "error",
	get: () => null,
	getOr: (_key: string, fallback: unknown) => fallback
});

describe("chatStore: chat_send wiring", () => {
	beforeEach(() => {
		vi.clearAllMocks();
		chatStore.activeChannel = "global";
		connectionStatus.lobby_code = "";
		storeAuth.username = "Eldyn";
	});

	it("send() on the global channel emits chat_send with channel: global", () => {
		chatStore.send("hello");
		expect(mockEmit).toHaveBeenCalledWith("chat_send", { message: "hello", channel: "global" });
	});

	it("send() on a DM channel maps to channel: dm with the friend as target", () => {
		chatStore.activeChannel = { friendId: "bianca" };
		chatStore.send("hey there");
		expect(mockEmit).toHaveBeenCalledWith("chat_send", {
			message: "hey there",
			channel: "dm",
			target: "bianca"
		});
	});

	it("send() clears the draft for the channel after emitting", () => {
		chatStore.setDraft("global", "hello");
		chatStore.send("hello");
		expect(chatStore.draftFor("global")).toBe("");
	});

	it("send() with only whitespace does not emit", () => {
		chatStore.send("   ");
		expect(mockEmit).not.toHaveBeenCalled();
	});
});

describe("chatStore: global history push on join (unsolicited chat_history)", () => {
	beforeEach(() => {
		vi.clearAllMocks();
	});

	it("seeds the global thread from a channel: global chat_history push", () => {
		fire("chat_history", {
			channel: "global",
			messages: [
				{ username: "Bianca", message: "welcome" },
				{ username: "Renn", message: "hey" }
			]
		});
		expect(chatStore.linesFor("global").map((l) => l.text)).toEqual(["welcome", "hey"]);
	});

	it("replaces rather than appends to the global thread", () => {
		fire("chat_history", { channel: "global", messages: [{ username: "Bianca", message: "a" }] });
		fire("chat_history", { channel: "global", messages: [{ username: "Bianca", message: "b" }] });
		expect(chatStore.linesFor("global").map((l) => l.text)).toEqual(["b"]);
	});

	it("ignores a chat_history push with channel: dm (handled by emitAndWait instead)", () => {
		fire("chat_history", { channel: "global", messages: [{ username: "Bianca", message: "kept" }] });
		fire("chat_history", { channel: "dm", target: "Bianca", messages: [{ username: "Bianca", message: "dm-only" }] });
		expect(chatStore.linesFor("global").map((l) => l.text)).toEqual(["kept"]);
	});
});

describe("chatStore: chat_message wiring", () => {
	beforeEach(() => {
		vi.clearAllMocks();
		storeAuth.username = "Eldyn";
	});

	it("routes a global chat_message into the global thread", () => {
		const before = chatStore.linesFor("global").length;
		fire("chat_message", { username: "Bianca", message: "hi all", channel: "global" });
		const lines = chatStore.linesFor("global");
		expect(lines.length).toBe(before + 1);
		expect(lines[lines.length - 1]).toMatchObject({ username: "Bianca", text: "hi all" });
	});

	it("routes a lobby chat_message into the party thread", () => {
		const before = chatStore.linesFor("party").length;
		fire("chat_message", { username: "Bianca", message: "gg", channel: "lobby" });
		const lines = chatStore.linesFor("party");
		expect(lines.length).toBe(before + 1);
	});

	it("routes my own outgoing DM using target as the thread partner", () => {
		fire("chat_message", {
			username: "Eldyn",
			message: "hey bianca",
			channel: "dm",
			target: "Bianca"
		});
		const lines = chatStore.linesFor({ friendId: "Bianca" });
		expect(lines[lines.length - 1]).toMatchObject({ username: "Eldyn", text: "hey bianca" });
	});

	it("routes an incoming DM using the sender's username as the thread partner", () => {
		fire("chat_message", {
			username: "Bianca",
			message: "hey back",
			channel: "dm",
			target: "Eldyn"
		});
		const lines = chatStore.linesFor({ friendId: "Bianca" });
		expect(lines[lines.length - 1]).toMatchObject({ username: "Bianca", text: "hey back" });
	});
});

describe("chatStore: party channel gated by lobby_code", () => {
	beforeEach(() => {
		vi.clearAllMocks();
		chatStore.activeChannel = "party";
		connectionStatus.lobby_code = "";
	});

	it("isPartyAvailable is false when not in a lobby", () => {
		expect(chatStore.isPartyAvailable).toBe(false);
	});

	it("isPartyAvailable is true once lobby_code is set", () => {
		connectionStatus.lobby_code = "ABC123";
		expect(chatStore.isPartyAvailable).toBe(true);
	});

	it("send() on party while not in a lobby does not emit, and sets a composer error", () => {
		chatStore.send("anyone here?");
		expect(mockEmit).not.toHaveBeenCalled();
		expect(chatStore.composerError).toBeTruthy();
	});

	it("send() on party while in a lobby emits channel: lobby", () => {
		connectionStatus.lobby_code = "ABC123";
		chatStore.send("anyone here?");
		expect(mockEmit).toHaveBeenCalledWith("chat_send", { message: "anyone here?", channel: "lobby" });
	});
});

describe("chatStore: DM history hydration", () => {
	beforeEach(() => {
		vi.clearAllMocks();
	});

	it("selectChannel on a fresh DM thread requests chat_history_request", async () => {
		mockEmitAndWait.mockResolvedValue(
			okResponse({ target: "renn-hydrate", messages: [{ username: "Renn", message: "yo" }] })
		);
		chatStore.selectChannel({ friendId: "renn-hydrate" });

		await vi.waitFor(() =>
			expect(mockEmitAndWait).toHaveBeenCalledWith("chat_history_request", { target: "renn-hydrate" })
		);
	});

	it("seeds the thread with the returned history, oldest first", async () => {
		mockEmitAndWait.mockResolvedValue(
			okResponse({
				target: "suki-hydrate",
				messages: [
					{ username: "Suki", message: "first" },
					{ username: "Eldyn", message: "second" }
				]
			})
		);
		chatStore.selectChannel({ friendId: "suki-hydrate" });

		await vi.waitFor(() =>
			expect(chatStore.linesFor({ friendId: "suki-hydrate" }).map((l) => l.text)).toEqual([
				"first",
				"second"
			])
		);
	});

	it("does not re-fetch history the second time the same thread is opened", async () => {
		mockEmitAndWait.mockResolvedValue(okResponse({ target: "otto-hydrate", messages: [] }));
		chatStore.selectChannel({ friendId: "otto-hydrate" });
		await vi.waitFor(() => expect(mockEmitAndWait).toHaveBeenCalledTimes(1));
		mockEmitAndWait.mockClear();

		chatStore.selectChannel("global");
		chatStore.selectChannel({ friendId: "otto-hydrate" });
		expect(mockEmitAndWait).not.toHaveBeenCalled();
	});

	it("allows retrying hydration after a failed request", async () => {
		mockEmitAndWait.mockResolvedValueOnce(errorResponse());
		chatStore.selectChannel({ friendId: "retry-hydrate" });
		await vi.waitFor(() => expect(mockEmitAndWait).toHaveBeenCalledTimes(1));

		mockEmitAndWait.mockResolvedValueOnce(okResponse({ target: "retry-hydrate", messages: [] }));
		chatStore.selectChannel("global");
		chatStore.selectChannel({ friendId: "retry-hydrate" });
		await vi.waitFor(() => expect(mockEmitAndWait).toHaveBeenCalledTimes(2));
	});
});

describe("chatStore: unsolicited error surfacing in the composer", () => {
	beforeEach(() => {
		vi.clearAllMocks();
		chatStore.close();
		chatStore.composerError = "";
	});

	it("surfaces an unsolicited error while the dock is open", () => {
		chatStore.open();
		fire("error", { code: "rate_limited" });
		expect(chatStore.composerError).toBeTruthy();
	});

	it("does not surface an unsolicited error while the dock is closed", () => {
		chatStore.close();
		fire("error", { code: "rate_limited" });
		expect(chatStore.composerError).toBe("");
	});
});
