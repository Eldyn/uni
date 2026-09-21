import { describe, it, expect, vi } from "vitest";

vi.mock("$lib/stores/ws.svelte", () => ({
	ws: {
		emit: vi.fn(),
		emitAndWait: vi.fn().mockResolvedValue({}),
		on: vi.fn(() => vi.fn()),
		onOpen: vi.fn(() => vi.fn()),
		onClose: vi.fn(() => vi.fn()),
		connect: vi.fn().mockResolvedValue(undefined),
		connectionStatus: { status: "connected", username: "", room: "", lobby_code: "" }
	},
	ClientAction: new Proxy({}, { get: (_t, key) => String(key) }),
	ServerAction: new Proxy({}, { get: (_t, key) => String(key) })
}));

describe("storeLobby.reset", () => {
	it("clears lobby membership, listings and loading flags", async () => {
		const { storeLobby } = await import("$lib/stores/lobby.svelte");

		storeLobby.current = { invite_code: "ABCD" } as never;
		storeLobby.available = [{ id: 1 } as never];
		storeLobby.isLoadingList = true;
		storeLobby.listError = true;

		storeLobby.reset();

		expect(storeLobby.current).toBeNull();
		expect(storeLobby.available).toEqual([]);
		expect(storeLobby.isLoadingList).toBe(false);
		expect(storeLobby.isLoadingJoin).toBe(false);
		expect(storeLobby.isLoadingStart).toBe(false);
		expect(storeLobby.listError).toBe(false);
	});

	it("removes the persisted lobby_code so a reconnect can't rejoin the previous account's lobby", async () => {
		const { storeLobby } = await import("$lib/stores/lobby.svelte");

		localStorage.setItem("lobby_code", "ABCD12");
		storeLobby.reset();

		expect(localStorage.getItem("lobby_code")).toBeNull();
	});
});

describe("storeGame.reset", () => {
	it("clears match state and pending prompt/window", async () => {
		const { storeGame } = await import("$lib/stores/game.svelte");

		storeGame.state = { players: [] } as never;
		storeGame.activePrompt = { prompt_id: "choose_color" } as never;
		storeGame.activeWindow = { windowId: "1" } as never;
		storeGame.isActionPending = true;
		storeGame.turnTimeRemaining = 2;

		storeGame.reset();

		expect(storeGame.state).toBeNull();
		expect(storeGame.activePrompt).toBeNull();
		expect(storeGame.activeWindow).toBeNull();
		expect(storeGame.isActionPending).toBe(false);
		expect(storeGame.turnTimeRemaining).toBe(15);
	});
});

describe("storeStats.reset", () => {
	it("clears the previous account's stats and leaderboard", async () => {
		const { storeStats } = await import("$lib/stores/stats.svelte");

		storeStats.myStats = { username: "eldyn" } as never;
		storeStats.leaderboard = [{ username: "eldyn" } as never];
		storeStats.isLoading = true;

		storeStats.reset();

		expect(storeStats.myStats).toBeNull();
		expect(storeStats.leaderboard).toEqual([]);
		expect(storeStats.isLoading).toBe(false);
	});
});
