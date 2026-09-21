import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, act } from "@testing-library/svelte";

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
		activePrompt: null as unknown,
		activeWindow: null as unknown,
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
		leave: vi.fn()
	}
}));

import GameHud from "$components/game/GameHud.svelte";
import { storeTooltipStack } from "$stores/tooltipStack.svelte";

describe("GameHud top card inspection and rich tooltip", () => {
	beforeEach(() => {
		vi.useFakeTimers();
	});

	afterEach(() => {
		vi.useRealTimers();
		storeTooltipStack.closeAll();
		document.body.innerHTML = "";
	});

	it("renders top card chip in HUD and opens tooltip with keyword link on hover", async () => {
		render(GameHud);

		const chip = screen.getByText("Skip");
		expect(chip).toBeInTheDocument();

		// Hover over chip container
		const container = chip.closest(".tooltip-container")!;
		await fireEvent.mouseEnter(container);
		await act(() => vi.runAllTimers());

		const tooltip = screen.getByRole("tooltip");
		expect(tooltip).toBeInTheDocument();
		expect(tooltip).toHaveTextContent("Causes the next player in turn order to be skipped");

		// Find keyword buttons inside the tooltip
		const keywordButtons = tooltip.querySelectorAll<HTMLButtonElement>(
			"button.glossary-keyword-btn"
		);
		expect(keywordButtons.length).toBeGreaterThan(0);

		// Click the first keyword button ("turn" or "skip")
		const firstKeyword = keywordButtons[0];
		expect(firstKeyword).toBeInTheDocument();

		// Spying on storeTooltipStack.open
		const openSpy = vi.spyOn(storeTooltipStack, "open");
		await fireEvent.click(firstKeyword);
		expect(openSpy).toHaveBeenCalled();
	});
});

describe("GameHud prompt/window gating", () => {
	afterEach(() => {
		mockGameState.activePrompt = null;
		mockGameState.activeWindow = null;
		document.body.innerHTML = "";
	});

	it("hides the HUD while a prompt is open", () => {
		mockGameState.activePrompt = { prompt_id: "choose_color" } as never;

		render(GameHud);

		expect(document.querySelector(".hud-container")).toBeNull();
	});

	it("hides the HUD while a response window is open", () => {
		mockGameState.activeWindow = { windowId: "1" } as never;

		render(GameHud);

		expect(document.querySelector(".hud-container")).toBeNull();
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
