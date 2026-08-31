import { describe, it, expect, vi, beforeEach } from "vitest";

const { handlers, mockOn, mockOnOpen } = vi.hoisted(() => {
	const handlers: Record<string, Array<(data: Record<string, unknown>) => void>> = {};
	return {
		handlers,
		mockOn: vi.fn((action: string, handler: (data: Record<string, unknown>) => void) => {
			(handlers[action] ??= []).push(handler);
			return () => {
				handlers[action] = handlers[action].filter((h) => h !== handler);
			};
		}),
		mockOnOpen: vi.fn((handler: () => void) => {
			(handlers["__open"] ??= []).push(handler);
			return () => {
				handlers["__open"] = handlers["__open"].filter((h) => h !== handler);
			};
		})
	};
});

vi.mock("$lib/stores/ws.svelte", () => ({
	ws: {
		emit: vi.fn(),
		emitAndWait: vi.fn(),
		on: mockOn,
		onOpen: mockOnOpen,
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

describe("chatStore.dispose", () => {
	it("removes every registration the store made", async () => {
		const { chatStore } = await import("$lib/stores/chat.svelte");

		expect(handlers["chat_message"]).toHaveLength(1);
		expect(handlers["friend_list"]).toHaveLength(1);
		expect(handlers["__open"]).toHaveLength(1);

		chatStore.dispose();

		expect(handlers["chat_message"]).toHaveLength(0);
		expect(handlers["chat_history"]).toHaveLength(0);
		expect(handlers["friend_list"]).toHaveLength(0);
		expect(handlers["error"]).toHaveLength(0);
		expect(handlers["__open"]).toHaveLength(0);
	});

	it("does not double-register when listeners are re-established after dispose", async () => {
		const { chatStore } = await import("$lib/stores/chat.svelte");

		chatStore.dispose();
		(chatStore as unknown as { registerListeners: () => void }).registerListeners();

		expect(handlers["chat_message"]).toHaveLength(1);
	});
});
