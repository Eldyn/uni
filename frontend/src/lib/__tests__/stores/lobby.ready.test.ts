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

// The store resolves "self" by username; pin it so setReadyToScreen finds the
// local member regardless of the real auth session.
vi.mock("$lib/stores/auth.svelte", () => ({ storeAuth: { username: "eldyn" } }));

beforeEach(() => {
	vi.clearAllMocks();
	vi.resetModules();
});

function member(
	overrides: Partial<{
		username: string;
		is_host: boolean;
		is_bot: boolean;
		is_ready: boolean;
		is_connected: boolean;
	}>
) {
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

describe("storeLobby.setReadyToScreen", () => {
	function currentWithSelf(isReady: boolean) {
		return {
			invite_code: "ABCD",
			host: "eldyn",
			members: [
				member({ username: "eldyn", is_host: true, is_ready: isReady }),
				member({ username: "nyx", is_ready: false })
			]
		};
	}

	it("emits a toggle when the local player should be ready but is not", async () => {
		const { storeLobby } = await import("$lib/stores/lobby.svelte");
		(storeLobby as unknown as { current: unknown }).current = currentWithSelf(false);

		await storeLobby.setReadyToScreen(true);

		expect(wsState.emitAndWait).toHaveBeenCalledTimes(1);
	});

	it("does nothing when the local player already matches the screen", async () => {
		const { storeLobby } = await import("$lib/stores/lobby.svelte");
		(storeLobby as unknown as { current: unknown }).current = currentWithSelf(true);

		await storeLobby.setReadyToScreen(true);

		expect(wsState.emitAndWait).not.toHaveBeenCalled();
	});

	it("converges on the latest desired value when the screen flips mid-flight", async () => {
		let resolveToggle!: (value: unknown) => void;
		wsState.emitAndWait.mockImplementationOnce(
			() => new Promise((resolve) => (resolveToggle = resolve))
		);

		const { storeLobby } = await import("$lib/stores/lobby.svelte");
		const self = storeLobby as unknown as { current: unknown };
		self.current = currentWithSelf(false);

		const toReady = storeLobby.setReadyToScreen(true);
		const backToWaiting = storeLobby.setReadyToScreen(false);

		// The first toggle's broadcast lands: the local player is now ready.
		self.current = currentWithSelf(true);
		resolveToggle({});
		await Promise.all([toReady, backToWaiting]);

		expect(wsState.emitAndWait).toHaveBeenCalledTimes(2);
	});
});
