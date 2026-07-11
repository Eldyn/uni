import { describe, it, expect, vi, beforeEach } from "vitest";

const { mockEmit, mockEmitAndWait, mockConnect, mockOn, mockOnOpen, handlers, openHandlers } =
	vi.hoisted(() => {
		const handlers: Record<string, Array<(data: Record<string, unknown>) => void>> = {};
		const openHandlers: Array<() => void> = [];
		return {
			handlers,
			openHandlers,
			mockEmit: vi.fn(),
			mockEmitAndWait: vi.fn(),
			mockConnect: vi.fn().mockResolvedValue(undefined),
			mockOn: vi.fn((action: string, handler: (data: Record<string, unknown>) => void) => {
				(handlers[action] ??= []).push(handler);
				return () => {};
			}),
			mockOnOpen: vi.fn((handler: () => void) => {
				openHandlers.push(handler);
				return () => {};
			})
		};
	});

vi.mock("$lib/stores/ws.svelte", () => ({
	ws: {
		emit: mockEmit,
		emitAndWait: mockEmitAndWait,
		on: mockOn,
		onOpen: mockOnOpen,
		connect: mockConnect,
		connectionStatus: { status: "connected", username: "", room: "", lobby_code: "" }
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
import { storeToast } from "$lib/stores/toast.svelte";

const fire = (action: string, data: Record<string, unknown>) => {
	for (const h of handlers[action] ?? []) h(data);
};

const okResponse = () => ({
	ok: true,
	message: "",
	action: "success",
	get: () => null,
	getOr: (_key: string, fallback: unknown) => fallback
});

const errorResponse = (message = "Error occurred") => ({
	ok: false,
	message,
	action: "error",
	get: () => null,
	getOr: (_key: string, fallback: unknown) => fallback
});

describe("chatStore: requests friend_list on connect", () => {
	it("registers an onOpen handler that emits friend_list_request", () => {
		expect(openHandlers.length).toBeGreaterThan(0);
		for (const h of openHandlers) h();
		expect(mockEmit).toHaveBeenCalledWith("friend_list_request");
	});
});

describe("chatStore: friend_list wiring", () => {
	beforeEach(() => {
		vi.clearAllMocks();
	});

	it("populates friends with online status from a friend_list broadcast", () => {
		fire("friend_list", {
			friends: [
				{ username: "Bianca", online: true },
				{ username: "Renn", online: false }
			],
			incoming_requests: [],
			outgoing_requests: []
		});

		expect(chatStore.friends).toEqual([
			expect.objectContaining({ username: "Bianca", status: "online" }),
			expect.objectContaining({ username: "Renn", status: "offline" })
		]);
	});

	it("populates incoming and outgoing friend requests", () => {
		fire("friend_list", {
			friends: [],
			incoming_requests: ["Suki"],
			outgoing_requests: ["Otto"]
		});

		expect(chatStore.incomingRequests).toEqual(["Suki"]);
		expect(chatStore.outgoingRequests).toEqual(["Otto"]);
	});
});

describe("chatStore: friend_request / friend_response", () => {
	beforeEach(() => {
		vi.clearAllMocks();
		storeToast.items = [];
	});

	it("requestFriend emits friend_request with the username", async () => {
		mockEmitAndWait.mockResolvedValue(okResponse());
		await chatStore.requestFriend("Bianca");
		expect(mockEmitAndWait).toHaveBeenCalledWith("friend_request", { username: "Bianca" });
	});

	it("requestFriend toasts on rejection", async () => {
		mockEmitAndWait.mockResolvedValue(errorResponse("A friend request is already pending"));
		await chatStore.requestFriend("Bianca");
		expect(storeToast.items).toHaveLength(1);
	});

	it("respondToRequest emits friend_response with username and accept", async () => {
		mockEmitAndWait.mockResolvedValue(okResponse());
		await chatStore.respondToRequest("Suki", true);
		expect(mockEmitAndWait).toHaveBeenCalledWith("friend_response", { username: "Suki", accept: true });
	});

	it("respondToRequest toasts on rejection", async () => {
		mockEmitAndWait.mockResolvedValue(errorResponse("That friend request no longer exists"));
		await chatStore.respondToRequest("Suki", false);
		expect(storeToast.items).toHaveLength(1);
	});
});
