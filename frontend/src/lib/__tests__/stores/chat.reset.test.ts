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

vi.mock("$lib/stores/lobby.svelte", () => ({ storeLobby: { current: null } }));
vi.mock("$lib/stores/auth.svelte", () => ({
	storeAuth: { username: "eldyn", isLoggedIn: true, isGuest: false }
}));

beforeEach(() => {
	for (const key of Object.keys(handlers)) delete handlers[key];
	vi.resetModules();
});

describe("chatStore.reset", () => {
	it("clears messages, friends and requests from the previous session", async () => {
		const { chatStore } = await import("$lib/stores/chat.svelte");

		for (const handler of handlers["chat_message"]) {
			handler({ username: "nyx", message: "gg", channel: "global" });
		}
		for (const handler of handlers["friend_list"]) {
			handler({
				friends: [{ username: "nyx", online: true }],
				incoming_requests: ["mox"],
				outgoing_requests: ["vex"]
			});
		}

		expect(chatStore.global.length).toBeGreaterThan(0);
		expect(chatStore.friends).toHaveLength(1);

		chatStore.reset();

		expect(chatStore.global).toHaveLength(0);
		expect(chatStore.friends).toHaveLength(0);
		expect(chatStore.incomingRequests).toHaveLength(0);
		expect(chatStore.outgoingRequests).toHaveLength(0);
		expect(chatStore.activeChannel).toBe("global");
		expect(chatStore.composerError).toBe("");
		expect(chatStore.isOpen).toBe(false);
	});

	it("keeps its WebSocket handlers registered after a reset", async () => {
		const { chatStore } = await import("$lib/stores/chat.svelte");

		chatStore.reset();

		expect(handlers["chat_message"]).toHaveLength(1);
	});
});
