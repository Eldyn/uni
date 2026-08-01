import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup } from "@testing-library/svelte";

import AccessibleHandControls from "$components/game/AccessibleHandControls.svelte";
import { storeGame, type Card } from "$stores/game.svelte";
import { storeAuth } from "$stores/auth.svelte";

function card(id: number, overrides: Partial<Card> = {}): Card {
	return { id, type: "red", value: "6", can_play: true, ...overrides };
}

describe("AccessibleHandControls", () => {
	beforeEach(() => {
		storeAuth.username = "me";
		storeGame.state = {
			active_type: "red",
			current_turn: "me",
			play_direction: 1,
			players: [
				{
					username: "me",
					card_count: 2,
					hand: [card(1), card(2, { value: "skip", can_play: false })],
					is_bot: false
				},
				{ username: "opponent", card_count: 3, is_bot: false }
			],
			pending_draws: 0
		};
		storeGame.isActionPending = false;
	});

	afterEach(() => {
		cleanup();
		vi.restoreAllMocks();
		storeGame.state = null;
		storeGame.isActionPending = false;
	});

	it("exposes a focusable, labelled control for every playable card", () => {
		render(AccessibleHandControls);
		const button = screen.getByRole("button", { name: "Play red 6" });
		expect(button).toHaveAttribute("tabindex", "0");
	});

	it("marks a non-playable card as not focusable and disabled", () => {
		render(AccessibleHandControls);
		const button = screen.getByRole("button", { name: "Play red skip" });
		expect(button).toHaveAttribute("tabindex", "-1");
		expect(button).toHaveAttribute("aria-disabled", "true");
	});

	it("Enter on a playable card calls storeGame.playCard exactly once", async () => {
		const playCard = vi.spyOn(storeGame, "playCard").mockImplementation(() => {});
		render(AccessibleHandControls);

		const button = screen.getByRole("button", { name: "Play red 6" });
		await fireEvent.keyDown(button, { key: "Enter" });

		expect(playCard).toHaveBeenCalledTimes(1);
		expect(playCard).toHaveBeenCalledWith(1);
	});

	it("a click has the same effect as Enter", async () => {
		const playCard = vi.spyOn(storeGame, "playCard").mockImplementation(() => {});
		render(AccessibleHandControls);

		await fireEvent.click(screen.getByRole("button", { name: "Play red 6" }));

		expect(playCard).toHaveBeenCalledWith(1);
	});

	it("Enter on a non-playable card is a no-op", async () => {
		const playCard = vi.spyOn(storeGame, "playCard").mockImplementation(() => {});
		render(AccessibleHandControls);

		const button = screen.getByRole("button", { name: "Play red skip" });
		await fireEvent.keyDown(button, { key: "Enter" });

		expect(playCard).not.toHaveBeenCalled();
	});

	it("the Draw card control calls storeGame.drawCard", async () => {
		const drawCard = vi.spyOn(storeGame, "drawCard").mockImplementation(() => {});
		render(AccessibleHandControls);

		await fireEvent.click(screen.getByRole("button", { name: "Draw card" }));

		expect(drawCard).toHaveBeenCalledTimes(1);
	});
});
