import { describe, it, expect, vi, beforeEach } from "vitest";

const { handlers, mockOn } = vi.hoisted(() => {
	const handlers: Record<string, Array<(data: Record<string, unknown>) => void>> = {};
	return {
		handlers,
		mockOn: vi.fn((action: string, handler: (data: Record<string, unknown>) => void) => {
			(handlers[action] ??= []).push(handler);
			return () => {
				handlers[action] = handlers[action].filter((h) => h !== handler);
			};
		})
	};
});

vi.mock("$lib/stores/ws.svelte", () => ({
	ws: {
		emit: vi.fn(),
		emitAndWait: vi.fn(),
		on: mockOn,
		onOpen: vi.fn(() => vi.fn()),
		connect: vi.fn().mockResolvedValue(undefined),
		connectionStatus: { status: "connected", username: "", room: "", lobby_code: "" }
	},
	ClientAction: { FriendListRequest: "friend_list_request" },
	ServerAction: {
		ChatMessage: "chat_message",
		ChatHistory: "chat_history",
		FriendList: "friend_list",
		Error: "error"
	}
}));

vi.mock("$lib/stores/lobby.svelte", () => ({
	storeLobby: { current: null }
}));

vi.mock("$lib/stores/auth.svelte", () => ({
	storeAuth: { username: "eldyn", isLoggedIn: true, isGuest: false }
}));

beforeEach(() => {
	for (const key of Object.keys(handlers)) delete handlers[key];
	vi.resetModules();
});

describe("chat duplicate delivery", () => {
	it("registers exactly one chat_message handler for the store's lifetime", async () => {
		await import("$lib/stores/chat.svelte");

		expect(handlers["chat_message"]).toHaveLength(1);
	});

	it("delivers a broadcast to the global thread exactly once", async () => {
		const { chatStore } = await import("$lib/stores/chat.svelte");

		for (const handler of handlers["chat_message"]) {
			handler({ username: "nyx", message: "gg", channel: "global" });
		}

		const globalLines = (chatStore as unknown as { global: { text: string }[] }).global;
		expect(globalLines.filter((line) => line.text === "gg")).toHaveLength(1);
	});
});
