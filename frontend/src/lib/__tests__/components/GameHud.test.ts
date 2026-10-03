import { describe, it, expect, vi } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";

const { mockGameState, mockAuth } = vi.hoisted(() => ({
	mockGameState: {
		localPlayer: null as { username: string } | null,
		keepDrawn: vi.fn(),
		state: {
			pendingPlayDrawn: null as { player: string; card?: number } | null,
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

	it("no longer renders race placements in the HUD row", () => {
		mockGameState.state.mode = "race";
		mockGameState.placements = ["firstOut", "secondOut"];

		render(GameHud);

		expect(document.querySelector(".race-standings")).toBeNull();
		expect(document.querySelectorAll(".placement-chip")).toHaveLength(0);

		mockGameState.placements = [];
		mockGameState.state.mode = "standard";
	});

	it("renders a Keep button for the owner's held draw and calls keepDrawn", async () => {
		mockGameState.localPlayer = { username: "alice" };
		mockGameState.state.pendingPlayDrawn = { player: "alice", card: 10 };

		render(GameHud);

		await fireEvent.click(screen.getByRole("button", { name: "Keep" }));
		expect(mockGameState.keepDrawn).toHaveBeenCalledTimes(1);

		mockGameState.state.pendingPlayDrawn = null;
		mockGameState.localPlayer = null;
	});

	it("hides the Keep button when the held draw belongs to another player", () => {
		mockGameState.localPlayer = { username: "alice" };
		mockGameState.state.pendingPlayDrawn = { player: "bob", card: 10 };

		render(GameHud);

		expect(screen.queryByRole("button", { name: "Keep" })).not.toBeInTheDocument();

		mockGameState.state.pendingPlayDrawn = null;
		mockGameState.localPlayer = null;
	});

	it("hides the Keep button when the held draw has no card id", () => {
		mockGameState.localPlayer = { username: "alice" };
		mockGameState.state.pendingPlayDrawn = { player: "alice" };

		render(GameHud);

		expect(screen.queryByRole("button", { name: "Keep" })).not.toBeInTheDocument();

		mockGameState.state.pendingPlayDrawn = null;
		mockGameState.localPlayer = null;
	});
});
