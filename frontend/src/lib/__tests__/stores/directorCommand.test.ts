import { describe, it, expect, vi, beforeEach } from "vitest";

const { mockEmit } = vi.hoisted(() => ({
	mockEmit: vi.fn()
}));

vi.mock("$lib/stores/ws.svelte", () => ({
	ws: {
		emit: mockEmit,
		emitAndWait: vi.fn(),
		on: vi.fn(() => () => {}),
		onOpen: vi.fn(() => () => {}),
		connect: vi.fn()
	},
	ClientAction: {
		DirectorCommand: "director_command"
	},
	ServerAction: {}
}));

import { sendDirectorCommand, resolveSeatIsBot } from "$lib/stores/director.svelte";

describe("director sendDirectorCommand payload shapes", () => {
	beforeEach(() => {
		vi.clearAllMocks();
	});

	it("sends pause with no args", () => {
		sendDirectorCommand("pause");
		expect(mockEmit).toHaveBeenCalledWith("director_command", { op: "pause" });
	});

	it("sends step with no args", () => {
		sendDirectorCommand("step");
		expect(mockEmit).toHaveBeenCalledWith("director_command", { op: "step" });
	});

	it("sends force_turn with no args", () => {
		sendDirectorCommand("force_turn");
		expect(mockEmit).toHaveBeenCalledWith("director_command", { op: "force_turn" });
	});

	it("sends script with seat and intents", () => {
		const intents = [{ type: "play", bits: 7 }];
		sendDirectorCommand("script", { seat: "bot1", intents });
		expect(mockEmit).toHaveBeenCalledWith("director_command", {
			op: "script",
			args: { seat: "bot1", intents }
		});
	});

	it("sends stack with kinds in order", () => {
		sendDirectorCommand("stack", { kinds: ["uni:blue_skip", "uni:red_5"] });
		expect(mockEmit).toHaveBeenCalledWith("director_command", {
			op: "stack",
			args: { kinds: ["uni:blue_skip", "uni:red_5"] }
		});
	});

	it("sends rename with seat and to", () => {
		sendDirectorCommand("rename", { seat: "bot1", to: "robbie" });
		expect(mockEmit).toHaveBeenCalledWith("director_command", {
			op: "rename",
			args: { seat: "bot1", to: "robbie" }
		});
	});

	it("sends skin with seat and skin", () => {
		sendDirectorCommand("skin", { seat: "bot1", skin: "player" });
		expect(mockEmit).toHaveBeenCalledWith("director_command", {
			op: "skin",
			args: { seat: "bot1", skin: "player" }
		});
	});

	it("sends flip with seat and to", () => {
		sendDirectorCommand("flip", { seat: "bot1", to: "human" });
		expect(mockEmit).toHaveBeenCalledWith("director_command", {
			op: "flip",
			args: { seat: "bot1", to: "human" }
		});
	});

	it("sends checkpoint save and load modes", () => {
		sendDirectorCommand("checkpoint", { mode: "save" });
		expect(mockEmit).toHaveBeenCalledWith("director_command", {
			op: "checkpoint",
			args: { mode: "save" }
		});
		sendDirectorCommand("checkpoint", { mode: "load" });
		expect(mockEmit).toHaveBeenCalledWith("director_command", {
			op: "checkpoint",
			args: { mode: "load" }
		});
	});

	it("sends load_scene with bare name", () => {
		sendDirectorCommand("load_scene", { name: "opener" });
		expect(mockEmit).toHaveBeenCalledWith("director_command", {
			op: "load_scene",
			args: { name: "opener" }
		});
	});
});

describe("resolveSeatIsBot skin override", () => {
	it("skin bot forces the bot strip on a human seat", () => {
		expect(resolveSeatIsBot({ username: "alice", is_bot: false, skin: "bot" })).toBe(true);
	});

	it("skin player forces the player strip on a bot seat", () => {
		expect(resolveSeatIsBot({ username: "bot1", is_bot: true, skin: "player" })).toBe(false);
	});

	it("skin player beats a bot-like username", () => {
		expect(resolveSeatIsBot({ username: "bot1", is_bot: false, skin: "player" })).toBe(false);
	});

	it("falls back to is_bot without a skin", () => {
		expect(resolveSeatIsBot({ username: "alice", is_bot: false })).toBe(false);
		expect(resolveSeatIsBot({ username: "bot1", is_bot: true })).toBe(true);
	});

	it("falls back to bot in the username", () => {
		expect(resolveSeatIsBot({ username: "HelpfulBot", is_bot: false })).toBe(true);
	});
});
