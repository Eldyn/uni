import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, act } from "@testing-library/svelte";

const { mockGameState } = vi.hoisted(() => ({
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
			mode: "standard"
		},
		actionRequired: false,
		isSpectator: false,
		spectatorCount: 0,
		turnTimeRemaining: 12,
		placements: []
	}
}));

vi.mock("$stores/game.svelte", () => ({
	storeGame: mockGameState
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
		const keywordButtons = tooltip.querySelectorAll<HTMLButtonElement>("button.glossary-keyword-btn");
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
