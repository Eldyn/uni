import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";

const { mockGame } = vi.hoisted(() => ({
	mockGame: {
		isActionPending: false,
		activePrompt: {
			prompt_id: "p1",
			kind: "choose_player",
			payload: { options: ["bob", "cara"] }
		} as unknown,
		localPlayer: { username: "alice" },
		state: { players: [] },
		respondToPrompt: vi.fn()
	}
}));

vi.mock("$stores/game.svelte", () => ({ storeGame: mockGame }));

import PlayerPrompt from "$components/game/prompts/PlayerPrompt.svelte";

describe("PlayerPrompt", () => {
	beforeEach(() => {
		mockGame.isActionPending = false;
		mockGame.respondToPrompt.mockReset();
	});

	it("shows the select-a-target hint", () => {
		render(PlayerPrompt, { props: { prompt: mockGame.activePrompt as never } });

		expect(screen.getByRole("status")).toHaveTextContent(/select a target/i);
	});

	it("keeps each live target reachable as a button for keyboard users", async () => {
		render(PlayerPrompt, { props: { prompt: mockGame.activePrompt as never } });

		await fireEvent.click(screen.getByRole("button", { name: "cara" }));

		expect(mockGame.respondToPrompt).toHaveBeenCalledWith("p1", "cara");
		expect(screen.getAllByRole("button")).toHaveLength(2);
	});
});
