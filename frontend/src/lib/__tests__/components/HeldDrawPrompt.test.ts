import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";

const { mockGame } = vi.hoisted(() => ({
	mockGame: {
		localPlayer: { username: "alice" } as { username: string } | null,
		isActionPending: false,
		state: {
			pendingPlayDrawn: { player: "alice", card: 7 } as {
				player: string;
				card?: number;
			} | null
		},
		playCard: vi.fn(),
		keepDrawn: vi.fn()
	}
}));

vi.mock("$stores/game.svelte", () => ({ storeGame: mockGame }));

import HeldDrawPrompt from "$components/game/prompts/HeldDrawPrompt.svelte";

describe("HeldDrawPrompt", () => {
	beforeEach(() => {
		mockGame.localPlayer = { username: "alice" };
		mockGame.isActionPending = false;
		mockGame.state = { pendingPlayDrawn: { player: "alice", card: 7 } };
		mockGame.playCard.mockReset();
		mockGame.keepDrawn.mockReset();
	});

	it("shows the drawn-card heading with Play and Draw actions for the owner", () => {
		render(HeldDrawPrompt);

		expect(screen.getByText(/drew a playable card/i)).toBeInTheDocument();
		expect(screen.getByRole("button", { name: "Play" })).toBeInTheDocument();
		expect(screen.getByRole("button", { name: "Draw" })).toBeInTheDocument();
	});

	it("renders nothing when another player owns the hold", () => {
		mockGame.state = { pendingPlayDrawn: { player: "bob", card: 7 } };

		const { container } = render(HeldDrawPrompt);

		expect(container.querySelector(".cute-bubble")).toBeNull();
		expect(container.querySelectorAll("button")).toHaveLength(0);
	});

	it("renders nothing once the hold clears", () => {
		mockGame.state = { pendingPlayDrawn: null };

		const { container } = render(HeldDrawPrompt);

		expect(container.querySelectorAll("button")).toHaveLength(0);
	});

	it("Play routes the drawn card through the store's play path", async () => {
		render(HeldDrawPrompt);

		await fireEvent.click(screen.getByRole("button", { name: "Play" }));

		expect(mockGame.playCard).toHaveBeenCalledWith(7);
	});

	it("Draw keeps the drawn card", async () => {
		render(HeldDrawPrompt);

		await fireEvent.click(screen.getByRole("button", { name: "Draw" }));

		expect(mockGame.keepDrawn).toHaveBeenCalledTimes(1);
	});

	it("disables both actions while a request is in flight", () => {
		mockGame.isActionPending = true;

		render(HeldDrawPrompt);

		expect(screen.getByRole("button", { name: "Play" })).toBeDisabled();
		expect(screen.getByRole("button", { name: "Draw" })).toBeDisabled();
	});
});
