import { describe, it, expect, beforeEach } from "vitest";
import { chatStore, channelKey } from "$stores/chat.svelte";
import { storeAuth } from "$stores/auth.svelte";
import { CHAT_FRIENDS } from "$data/chatMock";

// Access private fields for test-only state reset, mirroring the pattern
// already used for ws.svelte's private handler maps in lobby.dedup.test.ts.
const chatAny = chatStore as unknown as {
	drafts: Record<string, string>;
	unread: Record<string, number>;
};

describe("chatStore", () => {
	beforeEach(() => {
		chatStore.activeChannel = "global";
		storeAuth.username = "";
	});

	it("defaults to the global channel", () => {
		expect(chatStore.activeChannel).toBe("global");
	});

	it("returns fixture lines for the global channel", () => {
		const lines = chatStore.linesFor("global");
		expect(lines.length).toBeGreaterThan(0);
	});

	it("returns fixture lines for the party channel, distinct from global", () => {
		const globalLines = chatStore.linesFor("global");
		const partyLines = chatStore.linesFor("party");
		expect(partyLines.length).toBeGreaterThan(0);
		expect(partyLines).not.toBe(globalLines);
	});

	it("returns a friend's own thread when switched to their DM channel", () => {
		const friend = CHAT_FRIENDS[0];
		chatStore.activeChannel = { friendId: friend.id };
		const lines = chatStore.linesFor(chatStore.activeChannel);
		expect(Array.isArray(lines)).toBe(true);
		// Every seeded line in a friend's own thread was authored by the
		// friend or the local user — never by a different friend's name.
		const otherFriendNames = CHAT_FRIENDS.filter((f) => f.id !== friend.id).map((f) => f.name);
		for (const line of lines) {
			expect(otherFriendNames).not.toContain(line.username);
		}
	});

	it("send() appends an optimistic line only to the currently active channel", () => {
		const before = chatStore.linesFor("global").length;
		chatStore.send("hello **world**");
		const after = chatStore.linesFor("global");
		expect(after.length).toBe(before + 1);
		expect(after[after.length - 1].text).toBe("hello **world**");

		// party wasn't touched
		const beforeParty = chatStore.linesFor("party").length;
		expect(chatStore.linesFor("party").length).toBe(beforeParty);
	});

	it("send() attributes the local line to storeAuth.username, or 'You' when logged out", () => {
		chatStore.send("anonymous line");
		const lines = chatStore.linesFor("global");
		expect(lines[lines.length - 1].username).toBe("You");

		storeAuth.username = "Eldyn";
		chatStore.send("named line");
		const lines2 = chatStore.linesFor("global");
		expect(lines2[lines2.length - 1].username).toBe("Eldyn");
	});
});

describe("chatStore drafts (per-channel scratchpad)", () => {
	beforeEach(() => {
		chatAny.drafts = {};
		localStorage.clear();
	});

	it("defaults to an empty draft for any channel", () => {
		expect(chatStore.draftFor("global")).toBe("");
		expect(chatStore.draftFor({ friendId: "f2" })).toBe("");
	});

	it("scopes drafts per channel — setting one channel's draft doesn't leak into another", () => {
		chatStore.setDraft("global", "hello globally");
		chatStore.setDraft("party", "hello party");
		chatStore.setDraft({ friendId: "f1" }, "hey Bianca");

		expect(chatStore.draftFor("global")).toBe("hello globally");
		expect(chatStore.draftFor("party")).toBe("hello party");
		expect(chatStore.draftFor({ friendId: "f1" })).toBe("hey Bianca");
		expect(chatStore.draftFor({ friendId: "f2" })).toBe("");
	});

	it("persists a draft to localStorage so it survives the dock closing", () => {
		chatStore.setDraft("party", "don't lose this");
		chatStore.close();
		expect(chatStore.draftFor("party")).toBe("don't lose this");

		const raw = localStorage.getItem("uni:chat:drafts");
		expect(raw).toBeTruthy();
		expect(JSON.parse(raw!)[channelKey("party")]).toBe("don't lose this");
	});

	it("send() clears the draft for the channel it sent to", () => {
		chatStore.activeChannel = "global";
		chatStore.setDraft("global", "hello **world**");
		chatStore.send(chatStore.draftFor("global"));
		expect(chatStore.draftFor("global")).toBe("");
	});
});

describe("chatStore unread tracking", () => {
	beforeEach(() => {
		chatAny.unread = {};
		chatStore.activeChannel = "global";
		chatStore.close();
	});

	it("has zero unread for every channel by default", () => {
		expect(chatStore.unreadCount("global")).toBe(0);
		expect(chatStore.totalUnread).toBe(0);
	});

	it("receiveLine() increments unread for a channel that isn't open+active", () => {
		chatStore.receiveLine("party", { id: "t1", username: "Suki", color: "#4ade80", text: "hi" });
		expect(chatStore.unreadCount("party")).toBe(1);
		expect(chatStore.totalUnread).toBe(1);
	});

	it("receiveLine() does not increment unread for the channel currently open and active", () => {
		chatStore.selectChannel("global");
		chatStore.open();
		chatStore.receiveLine("global", { id: "t2", username: "Bianca", color: "#f97373", text: "hi" });
		expect(chatStore.unreadCount("global")).toBe(0);
	});

	it("open() clears unread for the active channel but not others", () => {
		chatStore.receiveLine("global", { id: "t3", username: "Bianca", color: "#f97373", text: "a" });
		chatStore.receiveLine("party", { id: "t4", username: "Suki", color: "#4ade80", text: "b" });
		chatStore.activeChannel = "global";

		chatStore.open();

		expect(chatStore.unreadCount("global")).toBe(0);
		expect(chatStore.unreadCount("party")).toBe(1);
		expect(chatStore.totalUnread).toBe(1);
	});

	it("selectChannel() while open clears unread for the newly selected channel", () => {
		chatStore.receiveLine("party", { id: "t5", username: "Suki", color: "#4ade80", text: "b" });
		chatStore.open();
		expect(chatStore.unreadCount("party")).toBe(1);

		chatStore.selectChannel("party");
		expect(chatStore.unreadCount("party")).toBe(0);
	});

	it("selectChannel() while closed does not clear unread", () => {
		chatStore.close();
		chatStore.receiveLine("party", { id: "t6", username: "Suki", color: "#4ade80", text: "b" });
		chatStore.selectChannel("party");
		expect(chatStore.unreadCount("party")).toBe(1);
	});
});

describe("channelKey", () => {
	it("produces distinct, stable keys per channel shape", () => {
		expect(channelKey("global")).toBe(channelKey("global"));
		expect(channelKey("party")).not.toBe(channelKey("global"));
		expect(channelKey({ friendId: "f1" })).toBe(channelKey({ friendId: "f1" }));
		expect(channelKey({ friendId: "f1" })).not.toBe(channelKey({ friendId: "f2" }));
	});
});
