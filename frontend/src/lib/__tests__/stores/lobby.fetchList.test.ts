import { describe, it, expect, vi, beforeEach } from "vitest";

const { mockEmitAndWait } = vi.hoisted(() => ({
	mockEmitAndWait: vi.fn()
}));

vi.mock("$lib/stores/ws.svelte", () => ({
	ws: {
		emitAndWait: mockEmitAndWait,
		emit: vi.fn(),
		on: vi.fn(() => vi.fn()),
		onOpen: vi.fn(() => vi.fn()),
		connect: vi.fn()
	},
	ServerAction: {
		LobbyJoined: "lobby_joined",
		LobbyUpdated: "lobby_updated",
		LobbyLeft: "lobby_left",
		LobbyEvicted: "lobby_evicted",
		MatchStateUpdated: "match_state_updated"
	},
	ClientAction: {
		LobbyList: "lobby_list"
	}
}));

import { storeLobby } from "$lib/stores/lobby.svelte";
import { storeToast } from "$lib/stores/toast.svelte";

const okResponse = (lobbies: unknown[] = []) => ({
	ok: true,
	message: "",
	action: "success",
	get: (key: string) => (key === "lobbies" ? lobbies : null),
	getOr: (_key: string, fallback: unknown) => fallback
});

const errorResponse = {
	ok: false,
	message: "Error occurred",
	action: "error",
	get: () => null,
	getOr: (_key: string, fallback: unknown) => fallback
};

describe("lobby store: fetchList error vs. empty distinction", () => {
	beforeEach(() => {
		vi.clearAllMocks();
		storeLobby.available = [];
		storeLobby.listError = false;
		storeLobby.isLoadingList = false;
		storeToast.items = [];
	});

	it("clears listError on a successful fetch, even with zero lobbies", async () => {
		storeLobby.listError = true;
		mockEmitAndWait.mockResolvedValue(okResponse([]));
		await storeLobby.fetchList();
		expect(storeLobby.listError).toBe(false);
		expect(storeLobby.available).toEqual([]);
	});

	it("sets listError when the server responds with ok: false", async () => {
		mockEmitAndWait.mockResolvedValue(errorResponse);
		await storeLobby.fetchList();
		expect(storeLobby.listError).toBe(true);
	});

	it("sets listError when the request throws (network/WS failure)", async () => {
		mockEmitAndWait.mockRejectedValue(new Error("network down"));
		await storeLobby.fetchList();
		expect(storeLobby.listError).toBe(true);
	});

	it("does not set listError on a successful fetch that returns real lobbies", async () => {
		mockEmitAndWait.mockResolvedValue(okResponse([{ invite_code: "AAAAAA", name: "Test Lobby" }]));
		await storeLobby.fetchList();
		expect(storeLobby.listError).toBe(false);
		expect(storeLobby.available).toHaveLength(1);
	});

	it("toasts once on a throwing failure, but not again on a repeat failure", async () => {
		mockEmitAndWait.mockRejectedValue(new Error("network down"));
		await storeLobby.fetchList();
		expect(storeToast.items).toHaveLength(1);
		await storeLobby.fetchList();
		expect(storeToast.items).toHaveLength(1);
	});

	it("toasts again after a failure that follows a successful recovery", async () => {
		mockEmitAndWait.mockRejectedValue(new Error("network down"));
		await storeLobby.fetchList();
		expect(storeToast.items).toHaveLength(1);

		mockEmitAndWait.mockResolvedValue(okResponse([]));
		await storeLobby.fetchList();
		expect(storeLobby.listError).toBe(false);

		mockEmitAndWait.mockRejectedValue(new Error("network down again"));
		await storeLobby.fetchList();
		expect(storeToast.items).toHaveLength(2);
	});
});
