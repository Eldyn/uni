import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render } from "@testing-library/svelte";

const { mockGameState, mockAuth } = vi.hoisted(() => ({
	mockGameState: {
		state: {
			mode: "elimination",
			is_over: false
		},
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

import EliminationStandings from "$components/game/EliminationStandings.svelte";

describe("EliminationStandings", () => {
	beforeEach(() => {
		mockAuth.username = "";
		mockGameState.state.mode = "elimination";
		mockGameState.state.is_over = false;
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

	it("is hidden outside elimination mode", () => {
		mockGameState.state.mode = "standard";
		mockGameState.placements = ["a"];
		render(EliminationStandings);
		expect(chips()).toHaveLength(0);
	});

	it("numbers live eliminations in elimination order with no medals", () => {
		mockGameState.placements = ["firstOut", "secondOut"];

		render(EliminationStandings);

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

		render(EliminationStandings);

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

		render(EliminationStandings);

		const rendered = chips();
		expect(rendered[0]).toHaveClass("is-me");
		expect(rendered[1]).not.toHaveClass("is-me");
	});
});
