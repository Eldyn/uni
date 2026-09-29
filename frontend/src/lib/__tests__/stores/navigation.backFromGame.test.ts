import { describe, it, expect, vi, beforeEach } from "vitest";

const quitMatch = vi.fn();
const returnToLobby = vi.fn();
const leave = vi.fn();
let gameState: { is_over?: boolean } | null = null;

vi.mock("$lib/stores/analytics.svelte", () => ({ storeAnalytics: { track: vi.fn() } }));
vi.mock("$lib/stores/ws.svelte", () => ({ ws: { onOpen: vi.fn(() => () => {}) } }));
vi.mock("$lib/stores/game.svelte", () => ({
	storeGame: {
		get state() {
			return gameState;
		},
		quitMatch,
		returnToLobby
	}
}));
vi.mock("$lib/stores/lobby.svelte", () => ({
	storeLobby: {
		get isInLobby() {
			return true;
		},
		leave
	}
}));
vi.mock("$lib/stores/auth.svelte", () => ({ storeAuth: { isLoggedIn: true, isGuest: false } }));

async function pressBackFromGame() {
	const { storeNavigation } = await import("$lib/stores/navigation.svelte");
	storeNavigation.current = "game";
	window.dispatchEvent(
		new PopStateEvent("popstate", {
			state: { screen: "lobby", authModalOpen: false, authTab: "login", settingsOpen: false }
		})
	);
}

describe("browser back from the game screen", () => {
	beforeEach(() => {
		vi.resetModules();
		quitMatch.mockClear();
		returnToLobby.mockClear();
		leave.mockClear();
		window.localStorage.clear();
		window.history.replaceState(null, "", "/");
	});

	it("quits a running match through quitMatch so game state is torn down", async () => {
		gameState = { is_over: false };

		await pressBackFromGame();

		expect(quitMatch).toHaveBeenCalled();
		expect(returnToLobby).not.toHaveBeenCalled();
		expect(leave).not.toHaveBeenCalled();
	});

	it("returns to the lobby without a server leave once the match is over", async () => {
		gameState = { is_over: true };

		await pressBackFromGame();

		expect(returnToLobby).toHaveBeenCalled();
		expect(quitMatch).not.toHaveBeenCalled();
	});

	it("returns to the lobby when there is no match state", async () => {
		gameState = null;

		await pressBackFromGame();

		expect(returnToLobby).toHaveBeenCalled();
		expect(quitMatch).not.toHaveBeenCalled();
	});
});
