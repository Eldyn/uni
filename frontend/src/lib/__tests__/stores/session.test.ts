import { describe, it, expect, vi, beforeEach } from "vitest";

const { chatReset, lobbyReset, gameReset, statsReset } = vi.hoisted(() => ({
	chatReset: vi.fn(),
	lobbyReset: vi.fn(),
	gameReset: vi.fn(),
	statsReset: vi.fn()
}));

vi.mock("$lib/stores/chat.svelte", () => ({ chatStore: { reset: chatReset } }));
vi.mock("$lib/stores/lobby.svelte", () => ({ storeLobby: { reset: lobbyReset } }));
vi.mock("$lib/stores/game.svelte", () => ({ storeGame: { reset: gameReset } }));
vi.mock("$lib/stores/stats.svelte", () => ({ storeStats: { reset: statsReset } }));

beforeEach(() => {
	vi.clearAllMocks();
	vi.resetModules();
});

describe("session resets", () => {
	it("resets every session store when auth reports a logout", async () => {
		const { storeAuth } = await import("$lib/stores/auth.svelte");
		const { installSessionResets } = await import("$lib/stores/session");

		installSessionResets();
		(storeAuth as unknown as { setLoggedOut: () => void }).setLoggedOut();

		expect(chatReset).toHaveBeenCalledTimes(1);
		expect(lobbyReset).toHaveBeenCalledTimes(1);
		expect(gameReset).toHaveBeenCalledTimes(1);
		expect(statsReset).toHaveBeenCalledTimes(1);
	});

	it("installs its subscription only once", async () => {
		const { storeAuth } = await import("$lib/stores/auth.svelte");
		const { installSessionResets } = await import("$lib/stores/session");

		installSessionResets();
		installSessionResets();
		(storeAuth as unknown as { setLoggedOut: () => void }).setLoggedOut();

		expect(chatReset).toHaveBeenCalledTimes(1);
	});

	it("isolates each store's reset: one throwing must not skip the others", async () => {
		chatReset.mockImplementation(() => {
			throw new Error("boom");
		});
		const errorSpy = vi.spyOn(console, "error").mockImplementation(() => {});

		const { storeAuth } = await import("$lib/stores/auth.svelte");
		const { installSessionResets } = await import("$lib/stores/session");

		installSessionResets();
		(storeAuth as unknown as { setLoggedOut: () => void }).setLoggedOut();

		expect(chatReset).toHaveBeenCalledTimes(1);
		expect(lobbyReset).toHaveBeenCalledTimes(1);
		expect(gameReset).toHaveBeenCalledTimes(1);
		expect(statsReset).toHaveBeenCalledTimes(1);
		expect(errorSpy).toHaveBeenCalledTimes(1);

		errorSpy.mockRestore();
	});
});
