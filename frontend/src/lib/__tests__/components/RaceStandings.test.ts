import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render } from "@testing-library/svelte";

const { mockGameState, mockAuth } = vi.hoisted(() => ({
	mockGameState: {
		state: {
			mode: "race",
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

import RaceStandings from "$components/game/RaceStandings.svelte";

describe("RaceStandings", () => {
	beforeEach(() => {
		mockAuth.username = "";
		mockGameState.state.mode = "race";
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

	it("is hidden outside race mode", () => {
		mockGameState.state.mode = "standard";
		mockGameState.placements = ["a"];
		render(RaceStandings);
		expect(chips()).toHaveLength(0);
	});

	it("numbers live finishers in finishing order and medals them as they finish", () => {
		mockGameState.placements = ["firstOut", "secondOut", "thirdOut", "fourthOut"];

		render(RaceStandings);

		const rendered = chips();
		expect(rendered).toHaveLength(4);
		expect(rendered[0]).toHaveTextContent("#1 firstOut");
		expect(rendered[1]).toHaveTextContent("#2 secondOut");
		expect(rendered[0]).toHaveClass("rank-gold");
		expect(rendered[1]).toHaveClass("rank-silver");
		expect(rendered[2]).toHaveClass("rank-bronze");
		expect(rendered[3]).not.toHaveClass("rank-gold");
		expect(rendered[3]).not.toHaveClass("rank-silver");
		expect(rendered[3]).not.toHaveClass("rank-bronze");
	});

	it("numbers finished placements best-first and medals the podium", () => {
		mockGameState.state.is_over = true;
		mockGameState.placements = ["winner", "runnerUp", "third"];

		render(RaceStandings);

		const rendered = chips();
		expect(rendered).toHaveLength(3);
		expect(rendered[0]).toHaveTextContent("#1 winner");
		expect(rendered[1]).toHaveTextContent("#2 runnerUp");
		expect(rendered[2]).toHaveTextContent("#3 third");
		expect(rendered[0]).toHaveClass("rank-gold");
		expect(rendered[1]).toHaveClass("rank-silver");
		expect(rendered[2]).toHaveClass("rank-bronze");
	});

	it("marks the local player's chip is-me even when absent from state.players", () => {
		mockAuth.username = "firstOut";
		mockGameState.placements = ["firstOut", "secondOut"];

		render(RaceStandings);

		const rendered = chips();
		expect(rendered[0]).toHaveClass("is-me");
		expect(rendered[1]).not.toHaveClass("is-me");
	});
});
