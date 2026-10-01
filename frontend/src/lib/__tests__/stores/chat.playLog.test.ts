import { describe, it, expect, vi, beforeEach } from "vitest";

const { mockEmit, mockEmitAndWait } = vi.hoisted(() => ({
	mockEmit: vi.fn(),
	mockEmitAndWait: vi.fn()
}));

vi.mock("$lib/stores/ws.svelte", () => ({
	ws: {
		emit: mockEmit,
		emitAndWait: mockEmitAndWait,
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

import { chatStore, MAX_CHANNEL_LINES } from "$lib/stores/chat.svelte";
import { storeLobby } from "$lib/stores/lobby.svelte";
import type { LogLine } from "$lib/chat/playLog/logEvent";

const logLine = (name = "bob"): LogLine => ({
	kind: "log",
	key: "log_skip",
	params: { name },
	tier: 0
});

describe("chatStore.appendLocalLog", () => {
	beforeEach(() => {
		vi.clearAllMocks();
		chatStore.reset();
		storeLobby.current = { invite_code: "ABC123" } as typeof storeLobby.current;
	});

	it("appends a log line to the party channel without touching the websocket", () => {
		chatStore.appendLocalLog(logLine("bob"));

		const [line] = chatStore.linesFor("party");
		expect(line).toMatchObject({ kind: "log", logKey: "log_skip", logParams: { name: "bob" } });
		expect(mockEmit).not.toHaveBeenCalled();
	});

	it("never writes to the global channel", () => {
		chatStore.appendLocalLog(logLine());
		expect(chatStore.linesFor("global")).toEqual([]);
	});

	it("skips silently when the player is not in a lobby", () => {
		storeLobby.current = null;
		chatStore.appendLocalLog(logLine());
		expect(chatStore.linesFor("party")).toEqual([]);
		expect(chatStore.totalUnread).toBe(0);
	});

	it("leaves the unread badge alone for log lines while the party tab is closed", () => {
		chatStore.appendLocalLog(logLine());
		chatStore.appendLocalLog(logLine());
		expect(chatStore.unreadCount("party")).toBe(0);
		expect(chatStore.totalUnread).toBe(0);
	});

	it("still bumps unread for a real player line among log lines", () => {
		chatStore.appendLocalLog(logLine());
		chatStore.receiveLine("party", {
			id: "player-1",
			username: "ann",
			color: "",
			text: "gg"
		});
		chatStore.appendLocalLog(logLine());
		expect(chatStore.unreadCount("party")).toBe(1);
	});

	it("leaves unread alone while the party tab is open and active", () => {
		chatStore.selectChannel("party");
		chatStore.open();
		chatStore.appendLocalLog(logLine());
		expect(chatStore.unreadCount("party")).toBe(0);
	});

	it("bounds log lines to the newest MAX_CHANNEL_LINES", () => {
		for (let index = 0; index < MAX_CHANNEL_LINES + 5; index++) {
			chatStore.appendLocalLog(logLine(`p${index}`));
		}

		const lines = chatStore.linesFor("party");
		expect(lines.filter((line) => line.kind === "log")).toHaveLength(MAX_CHANNEL_LINES);
		expect(lines[lines.length - 1].logParams).toEqual({ name: `p${MAX_CHANNEL_LINES + 4}` });
		expect(lines[0].logParams).toEqual({ name: "p5" });
	});

	describe("with history-loaded player chat", () => {
		const historyIds = [10, 11, 12];
		const okResponse = (hasMore: boolean) => ({
			ok: true,
			message: "",
			getOr: (key: string, fallback: unknown) =>
				key === "messages"
					? historyIds.map((id) => ({ id, username: "ann", message: `m${id}` }))
					: key === "has_more"
						? hasMore
						: fallback
		});

		async function loadHistoryThenFlood() {
			mockEmitAndWait.mockResolvedValue(okResponse(true));
			chatStore.selectChannel("party");
			await vi.waitFor(() => expect(chatStore.linesFor("party")).toHaveLength(3));
			for (let index = 0; index < MAX_CHANNEL_LINES + 20; index++) {
				chatStore.appendLocalLog(logLine(`p${index}`));
			}
		}

		it("never evicts player chat lines when the log cap trims", async () => {
			await loadHistoryThenFlood();

			const human = chatStore.linesFor("party").filter((line) => line.kind !== "log");
			expect(human.map((line) => line.serverId)).toEqual(historyIds);
			expect(chatStore.linesFor("party")[0].serverId).toBe(10);
		});

		it("still pages history from the oldest player line after a log flood", async () => {
			await loadHistoryThenFlood();
			mockEmitAndWait.mockClear();
			mockEmitAndWait.mockResolvedValue(okResponse(false));

			await chatStore.loadMoreHistory("party");

			expect(mockEmitAndWait).toHaveBeenCalledWith("chat_history_request", {
				channel: "lobby",
				before_id: 10
			});
		});
	});
});
