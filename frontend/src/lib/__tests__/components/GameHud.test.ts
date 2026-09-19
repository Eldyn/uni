import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
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
});

describe("GameHud elimination standings", () => {
	beforeEach(() => {
		mockAuth.username = "";
		mockGameState.state.mode = "elimination";
		mockGameState.state.is_over = false;
		mockGameState.state.players = [
			{ username: "alice", card_count: 5, is_bot: false },
			{ username: "bob", card_count: 4, is_bot: false }
		];
		mockGameState.placements = [];
	});

	afterEach(() => {
		document.body.innerHTML = "";
		mockGameState.placements = [];
		mockGameState.state.mode = "standard";
		mockGameState.state.is_over = false;
	});

	function chips(): HTMLElement[] {
		return Array.from(document.querySelectorAll<HTMLElement>(".placement-chip"));
	}

	it("numbers live eliminations in elimination order with no medals", () => {
		mockGameState.placements = ["firstOut", "secondOut"];

		render(GameHud);

		const rendered = chips();
		expect(rendered).toHaveLength(2);
		expect(rendered[0]).toHaveTextContent("#1 firstOut");
		expect(rendered[1]).toHaveTextContent("#2 secondOut");
		expect(rendered[0]).not.toHaveClass("text-gold");
		expect(rendered[0]).not.toHaveClass("rank-silver");
		expect(rendered[1]).not.toHaveClass("rank-bronze");
	});

	it("numbers finished placements best-first and medals the podium", () => {
		mockGameState.state.is_over = true;
		mockGameState.placements = ["winner", "runnerUp", "third"];

		render(GameHud);

		const rendered = chips();
		expect(rendered).toHaveLength(3);
		expect(rendered[0]).toHaveTextContent("#1 winner");
		expect(rendered[1]).toHaveTextContent("#2 runnerUp");
		expect(rendered[2]).toHaveTextContent("#3 third");
		expect(rendered[0]).toHaveClass("text-gold");
		expect(rendered[1]).toHaveClass("rank-silver");
		expect(rendered[2]).toHaveClass("rank-bronze");
	});

	it("marks the local player's chip is-me even when absent from state.players", () => {
		mockAuth.username = "firstOut";
		mockGameState.placements = ["firstOut", "secondOut"];

		render(GameHud);

		const rendered = chips();
		expect(rendered[0]).toHaveClass("is-me");
		expect(rendered[1]).not.toHaveClass("is-me");
	});
});
