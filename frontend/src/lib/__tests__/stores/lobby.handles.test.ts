import { describe, it, expect, vi, beforeEach } from "vitest";

const { mockEmitAndWait, mockEmit, mockConnect } = vi.hoisted(() => ({
	mockEmitAndWait: vi.fn(),
	mockEmit: vi.fn(),
	mockConnect: vi.fn()
}));

vi.mock("$lib/stores/ws.svelte", () => ({
	ws: {
		emitAndWait: mockEmitAndWait,
		emit: mockEmit,
		on: vi.fn(() => vi.fn()),
		onOpen: vi.fn(() => vi.fn()),
		connect: mockConnect
	},
	ServerAction: {
		LobbyJoined: "lobby_joined",
		LobbyUpdated: "lobby_updated",
		LobbyLeft: "lobby_left",
		LobbyEvicted: "lobby_evicted",
		MatchStateUpdated: "match_state_updated"
	},
	ClientAction: {
		LobbyCreate: "lobby_create",
		LobbyJoin: "lobby_join",
		LobbyQuickJoin: "lobby_quick_join",
		LobbyStartMatch: "lobby_start_match",
		LobbyPromote: "lobby_promote",
		LobbyKick: "lobby_kick",
		LobbyUpdateSettings: "lobby_update_settings",
		LobbyRejoin: "lobby_rejoin"
	}
}));

import { storeLobby } from "$lib/stores/lobby.svelte";
import { storeToast } from "$lib/stores/toast.svelte";

const okResponse = {
	ok: true,
	message: "",
	action: "success",
	get: () => null,
	getOr: (_key: string, fallback: unknown) => fallback
};

const errorResponse = {
	ok: false,
	message: "Error occurred",
	action: "error",
	get: () => null,
	getOr: (_key: string, fallback: unknown) => fallback
};

describe("lobby store: handle methods", () => {
	beforeEach(() => {
		vi.clearAllMocks();
		mockEmitAndWait.mockResolvedValue(okResponse);
		storeLobby.isLoadingStart = false;
		storeLobby.isLoadingJoin = false;
	});

	it("create calls emitAndWait with LobbyCreate action and payload", async () => {
		await storeLobby.create({ is_public: false, name: "Test" });
		expect(mockEmitAndWait).toHaveBeenCalledWith("lobby_create", {
			is_public: false,
			name: "Test"
		});
	});

	it("create does not set isLoadingStart", async () => {
		await storeLobby.create({ is_public: false, name: "Test" });
		expect(storeLobby.isLoadingStart).toBe(false);
	});

	it("join calls emitAndWait with LobbyJoin action and code", async () => {
		await storeLobby.join("AAAAAA");
		expect(mockEmitAndWait).toHaveBeenCalledWith("lobby_join", { code: "AAAAAA" });
	});

	it("join resolves to false when response.ok is false", async () => {
		mockEmitAndWait.mockResolvedValue(errorResponse);
		await expect(storeLobby.join("AAAAAA")).resolves.toBe(false);
	});

	it("quickJoin connects then calls emitAndWait with LobbyQuickJoin action", async () => {
		await storeLobby.quickJoin();
		expect(mockConnect).toHaveBeenCalled();
		expect(mockEmitAndWait).toHaveBeenCalledWith("lobby_quick_join");
	});

	it("quickJoin resolves to true when response.ok is true", async () => {
		await expect(storeLobby.quickJoin()).resolves.toBe(true);
	});

	it("quickJoin resolves to false when response.ok is false", async () => {
		mockEmitAndWait.mockResolvedValue(errorResponse);
		await expect(storeLobby.quickJoin()).resolves.toBe(false);
	});

	it("quickJoin shows a quick-play-specific message when the code is lobby_not_found", async () => {
		const { storeToast } = await import("$lib/stores/toast.svelte");
		const toastSpy = vi.spyOn(storeToast, "error");
		mockEmitAndWait.mockResolvedValue({
			ok: false,
			code: "lobby_not_found",
			message: "This code has no lobby associated.",
			action: "error",
			get: () => null,
			getOr: (_key: string, fallback: unknown) => fallback
		});

		await storeLobby.quickJoin();

		expect(toastSpy).toHaveBeenCalledWith("No lobbies are currently open to quick play with.");
	});

	it("join still shows the join-by-code message when the code is lobby_not_found", async () => {
		const { storeToast } = await import("$lib/stores/toast.svelte");
		const toastSpy = vi.spyOn(storeToast, "error");
		mockEmitAndWait.mockResolvedValue({
			ok: false,
			code: "lobby_not_found",
			message: "This code has no lobby associated.",
			action: "error",
			get: () => null,
			getOr: (_key: string, fallback: unknown) => fallback
		});

		await storeLobby.join("AAAAAA");

		expect(toastSpy).toHaveBeenCalledWith("This code has no lobby associated.");
	});

	it("startMatch calls emitAndWait with LobbyStartMatch action", async () => {
		await storeLobby.startMatch();
		expect(mockEmitAndWait).toHaveBeenCalledWith("lobby_start_match");
	});

	it("startMatch sets isLoadingStart true during request then false after", async () => {
		let duringRequest = false;
		mockEmitAndWait.mockImplementation(async () => {
			duringRequest = storeLobby.isLoadingStart;
			return okResponse;
		});
		await storeLobby.startMatch();
		expect(duringRequest).toBe(true);
		expect(storeLobby.isLoadingStart).toBe(false);
	});

	it("promote calls emitAndWait with LobbyPromote action and username", async () => {
		await storeLobby.promote("player2");
		expect(mockEmitAndWait).toHaveBeenCalledWith("lobby_promote", { username: "player2" });
	});

	it("kick calls emitAndWait with LobbyKick action and username", async () => {
		await storeLobby.kick("player2");
		expect(mockEmitAndWait).toHaveBeenCalledWith("lobby_kick", { username: "player2" });
	});

	it("updateSettings calls emitAndWait with LobbyUpdateSettings action and settings object", async () => {
		const settings = { is_public: true, name: "My Lobby" };
		await storeLobby.updateSettings(settings);
		expect(mockEmitAndWait).toHaveBeenCalledWith("lobby_update_settings", settings);
	});

	it("updateSettings does not emit a success toast on success, but emits an error toast on failure", async () => {
		const toastSuccessSpy = vi.spyOn(storeToast, "success");
		const toastErrorSpy = vi.spyOn(storeToast, "error");

		mockEmitAndWait.mockResolvedValueOnce({ ok: true, message: "" });
		await storeLobby.updateSettings({ name: "New Name" });
		expect(toastSuccessSpy).not.toHaveBeenCalled();

		mockEmitAndWait.mockResolvedValueOnce({ ok: false, message: "Invalid setting" });
		await storeLobby.updateSettings({ name: "Bad Name" });
		expect(toastErrorSpy).toHaveBeenCalledWith("Invalid setting");

		toastSuccessSpy.mockRestore();
		toastErrorSpy.mockRestore();
	});
});
