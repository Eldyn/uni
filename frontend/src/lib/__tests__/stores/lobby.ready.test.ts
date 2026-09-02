import { describe, it, expect, vi, beforeEach } from "vitest";

const wsState = { emit: vi.fn(), emitAndWait: vi.fn().mockResolvedValue({}) };

vi.mock("$lib/stores/ws.svelte", () => ({
	ws: {
		emit: wsState.emit,
		emitAndWait: wsState.emitAndWait,
		on: vi.fn(() => vi.fn()),
		onOpen: vi.fn(() => vi.fn()),
		onClose: vi.fn(() => vi.fn()),
		connectionStatus: { status: "connected", username: "eldyn", room: "", lobby_code: "" }
	},
	ClientAction: new Proxy({}, { get: (_t, key) => String(key) }),
	ServerAction: new Proxy({}, { get: (_t, key) => String(key) })
}));

beforeEach(() => {
	vi.clearAllMocks();
	vi.resetModules();
});

function member(overrides: Partial<{
	username: string;
	is_host: boolean;
	is_bot: boolean;
	is_ready: boolean;
	is_connected: boolean;
}>) {
	return {
		username: "p",
		is_connected: true,
		is_host: false,
		is_bot: false,
		is_ready: false,
		...overrides
	};
}

describe("storeLobby.toggleReady", () => {
	it("emits lobby_toggle_ready", async () => {
		const { storeLobby } = await import("$lib/stores/lobby.svelte");
		await storeLobby.toggleReady();
		expect(wsState.emitAndWait).toHaveBeenCalledWith(
			"LobbyToggleReady",
			undefined,
			expect.anything()
		);
	});
});

describe("storeLobby.startEligibility", () => {
	it("is ineligible with fewer than two members", async () => {
		const { storeLobby } = await import("$lib/stores/lobby.svelte");
		(storeLobby as unknown as { current: unknown }).current = {
			invite_code: "ABCD",
			host: "eldyn",
			members: [member({ username: "eldyn", is_host: true })]
		};

		expect(storeLobby.startEligibility.canStart).toBe(false);
		expect(storeLobby.startEligibility.reason).toMatch(/players/i);
	});

	it("is ineligible when a human member is not ready", async () => {
		const { storeLobby } = await import("$lib/stores/lobby.svelte");
		(storeLobby as unknown as { current: unknown }).current = {
			invite_code: "ABCD",
			host: "eldyn",
			members: [
				member({ username: "eldyn", is_host: true, is_ready: true }),
				member({ username: "nyx", is_ready: false })
			]
		};

		expect(storeLobby.startEligibility.canStart).toBe(false);
		expect(storeLobby.startEligibility.reason).toMatch(/ready/i);
	});

	it("is eligible once every human is ready, ignoring bots", async () => {
		const { storeLobby } = await import("$lib/stores/lobby.svelte");
		(storeLobby as unknown as { current: unknown }).current = {
			invite_code: "ABCD",
			host: "eldyn",
			members: [
				member({ username: "eldyn", is_host: true, is_ready: true }),
				member({ username: "bot_1", is_bot: true, is_ready: false })
			]
		};

		expect(storeLobby.startEligibility.canStart).toBe(true);
		expect(storeLobby.startEligibility.reason).toBeNull();
	});
});
