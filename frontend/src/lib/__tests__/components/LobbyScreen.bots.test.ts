import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";

const { kick, updateSettings } = vi.hoisted(() => ({
	kick: vi.fn(),
	updateSettings: vi.fn()
}));

vi.mock("$stores/auth.svelte", () => ({ storeAuth: { username: "eldyn" } }));
vi.mock("$stores/chat.svelte", () => ({
	chatStore: { friends: [], selectChannel: vi.fn(), open: vi.fn() }
}));
vi.mock("$stores/animation.svelte", () => ({ storeAnimation: { enabled: true } }));
vi.mock("$stores/lobby.svelte", () => ({
	storeLobby: {
		current: {
			invite_code: "ABCD",
			host: "eldyn",
			name: "eldyn's game",
			members: [
				{ username: "eldyn", is_connected: true, is_host: true, is_bot: false, is_ready: true },
				{ username: "bot_a", is_connected: true, is_host: false, is_bot: true, is_ready: false },
				{ username: "bot_b", is_connected: true, is_host: false, is_bot: true, is_ready: false }
			],
			settings: { max_players: 4, bot_count: 2 }
		},
		startEligibility: { canStart: false, reason: "Waiting on players" },
		toggleReady: vi.fn(),
		updateSettings,
		kick,
		startMatch: vi.fn(),
		leave: vi.fn(),
		isLoadingStart: false
	}
}));

import LobbyScreen from "$components/lobby/LobbyScreen.svelte";

describe("LobbyScreen bot removal", () => {
	beforeEach(() => vi.clearAllMocks());

	it("kicks the first bot's own username without decrementing bot_count", async () => {
		render(LobbyScreen);
		const kickButtons = screen.getAllByRole("button", { name: "Kick" });
		await fireEvent.click(kickButtons[0]);
		expect(kick).toHaveBeenCalledWith("bot_a");
		expect(updateSettings).not.toHaveBeenCalled();
	});

	it("kicks the second bot's own username without decrementing bot_count", async () => {
		render(LobbyScreen);
		const kickButtons = screen.getAllByRole("button", { name: "Kick" });
		await fireEvent.click(kickButtons[1]);
		expect(kick).toHaveBeenCalledWith("bot_b");
		expect(updateSettings).not.toHaveBeenCalled();
	});
});
