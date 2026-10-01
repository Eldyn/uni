import { describe, it, expect, vi, beforeEach } from "vitest";
import { render } from "@testing-library/svelte";

vi.mock("$lib/stores/ws.svelte", () => ({
	ws: {
		emit: vi.fn(),
		emitAndWait: vi.fn(),
		on: vi.fn(() => () => {}),
		onOpen: vi.fn(() => () => {}),
		connect: vi.fn().mockResolvedValue(undefined)
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

vi.mock("$lib/stores/lobby.svelte", () => ({
	storeLobby: { current: null as { invite_code: string } | null }
}));

import ChatLog from "$components/chat/ChatLog.svelte";
import { chatStore } from "$lib/stores/chat.svelte";
import { storeLobby } from "$lib/stores/lobby.svelte";

describe("ChatLog play log lines", () => {
	beforeEach(() => {
		// INFO: jsdom has no Element.scrollTo; ChatLog's auto-scroll needs one.
		Element.prototype.scrollTo = vi.fn();
		chatStore.reset();
		storeLobby.current = { invite_code: "ABC123" } as typeof storeLobby.current;
	});

	it("renders a real log line through the real log copy", () => {
		chatStore.appendLocalLog({
			kind: "log",
			key: "log_skip",
			params: { name: "Zed" },
			tier: 0
		});

		const { container } = render(ChatLog, { props: { channel: "party" } });

		expect(container.textContent).toContain("Zed");
		expect(container.textContent).not.toContain("No messages yet.");
	});
});
