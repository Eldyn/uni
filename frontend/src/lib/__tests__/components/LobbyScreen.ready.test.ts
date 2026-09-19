import { describe, it, expect, vi } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";

const { toggleReady, updateSettings } = vi.hoisted(() => ({
	toggleReady: vi.fn(),
	updateSettings: vi.fn()
}));

vi.mock("$stores/auth.svelte", () => ({ storeAuth: { username: "eldyn" } }));
vi.mock("$stores/chat.svelte", () => ({
	chatStore: { friends: [], selectChannel: vi.fn(), open: vi.fn() }
}));
vi.mock("$stores/lobby.svelte", () => ({
	storeLobby: {
		current: {
			invite_code: "ABCD",
			host: "eldyn",
			name: "eldyn's game",
			members: [
				{ username: "eldyn", is_connected: true, is_host: true, is_bot: false, is_ready: false },
				{ username: "nyx", is_connected: true, is_host: false, is_bot: false, is_ready: true }
			],
			settings: { max_players: 4, bot_count: 0 }
		},
		startEligibility: { canStart: false, reason: "Waiting on 1 player to ready up" },
		toggleReady,
		updateSettings,
		startMatch: vi.fn(),
		leave: vi.fn(),
		isLoadingStart: false
	}
}));

import LobbyScreen from "$components/lobby/LobbyScreen.svelte";

describe("LobbyScreen ready state", () => {
	it("shows the ineligibility reason near Start when not eligible", () => {
		render(LobbyScreen);
		expect(screen.getByText(/waiting on 1 player to ready up/i)).toBeInTheDocument();
	});

	it("calls toggleReady when the local player's ready control is used", async () => {
		render(LobbyScreen);
		const readyControl = screen.getByRole("button", { name: "Ready?" });
		await fireEvent.click(readyControl);
		expect(toggleReady).toHaveBeenCalled();
	});

	it("calls updateSettings to increment bot count when host clicks an empty seat", async () => {
		render(LobbyScreen);
		const addBotButtons = screen.getAllByRole("button", { name: "Add Bot" });
		expect(addBotButtons.length).toBeGreaterThan(0);
		await fireEvent.click(addBotButtons[0]);
		expect(updateSettings).toHaveBeenCalledWith({ bot_count: 1 });
	});
});
