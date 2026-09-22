import { describe, it, expect, vi } from "vitest";
import { render } from "@testing-library/svelte";

const { mockGameState, mockAuth } = vi.hoisted(() => ({
	mockGameState: {
		state: {
			active_type: 0,
			current_turn: "alice",
			play_direction: 1,
			top_card: { id: 10, type: "red", value: "skip" },
			players: [
				{ username: "alice", card_count: 5, is_bot: false },
				{ username: "bob", card_count: 4, is_bot: false }
			],
			pending_draws: 0,
			draw_pile_size: 40,
			mode: "standard",
			is_over: false
		},
		actionRequired: false,
		isSpectator: false,
		spectatorCount: 0,
		turnTimeRemaining: 12,
		placements: [] as string[]
	},
	mockAuth: {
		username: ""
	}
}));

vi.mock("$stores/game.svelte", () => ({
	storeGame: mockGameState
}));

vi.mock("$stores/auth.svelte", () => ({
	storeAuth: mockAuth
}));

vi.mock("$stores/lobby.svelte", () => ({
	storeLobby: {
		leave: vi.fn(),
		current: { settings: { ranked: false } }
	}
}));

import GameHud from "$components/game/GameHud.svelte";

describe("GameHud", () => {
	it("does not render a top-card indicator — details live on the cards themselves", () => {
		render(GameHud);

		// The old chip (colored dot + card title) must be gone; the played card
		// is inspected by right-clicking it on the board instead.
		expect(document.querySelector(".top-card-chip")).toBeNull();
		expect(document.querySelector(".top-card-dot")).toBeNull();
	});

	it("no longer renders elimination placements in the HUD row", () => {
		mockGameState.state.mode = "elimination";
		mockGameState.placements = ["firstOut", "secondOut"];

		render(GameHud);

		expect(document.querySelector(".elimination-standings")).toBeNull();
		expect(document.querySelectorAll(".placement-chip")).toHaveLength(0);

		mockGameState.placements = [];
		mockGameState.state.mode = "standard";
	});
});
