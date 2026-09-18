import { describe, it, expect, vi, afterEach } from "vitest";
import { render, screen } from "@testing-library/svelte";

const { mockGame, mockAuth } = vi.hoisted(() => ({
	mockGame: {
		state: null as null | Record<string, unknown>,
		placements: [] as string[],
		returnToLobby: vi.fn()
	},
	mockAuth: {
		username: ""
	}
}));

vi.mock("$stores/game.svelte", () => ({ storeGame: mockGame }));
vi.mock("$stores/auth.svelte", () => ({ storeAuth: mockAuth }));
vi.mock("$stores/audio.svelte", () => ({ storeAudio: { playSfx: vi.fn() } }));
vi.mock("$stores/i18n.svelte", () => ({ storeI18n: { locale: "en" } }));

import GameEndPopup from "$components/game/GameEndPopup.svelte";

// Matches the real end-of-match shape: `placements` is BEST-FIRST and the
// eliminated local player has already been erased from `state.players`
// (survivor_count = 1 leaves only the winner). Rank identity must therefore
// come from storeAuth.username, never from state.players.
function setEndState(opts: { mode: "elimination" | "standard"; placements: string[] }): void {
	mockGame.state = {
		is_over: true,
		winner: opts.placements[0],
		mode: opts.mode,
		placements: opts.placements,
		players: [{ username: opts.placements[0], is_bot: false }]
	};
	mockGame.placements = opts.placements;
}

function titleEl(): HTMLElement {
	return screen.getByRole("heading", { level: 1 });
}

describe("GameEndPopup podium finish", () => {
	afterEach(() => {
		document.body.innerHTML = "";
	});

	it("titles the local winner VICTORY! with the win style", () => {
		mockAuth.username = "Alice";
		setEndState({ mode: "elimination", placements: ["Alice", "Bob", "Cara", "Dan"] });

		render(GameEndPopup);

		expect(titleEl()).toHaveTextContent("VICTORY!");
		expect(titleEl()).toHaveClass("result--win");
	});

	it("titles 2nd place PODIUM FINISH! even when absent from state.players", () => {
		mockAuth.username = "Bob";
		setEndState({ mode: "elimination", placements: ["Alice", "Bob", "Cara", "Dan"] });

		render(GameEndPopup);

		expect(titleEl()).toHaveTextContent("PODIUM FINISH!");
		expect(titleEl()).toHaveClass("result--podium");
	});

	it("titles 3rd place PODIUM FINISH! even when absent from state.players", () => {
		mockAuth.username = "Cara";
		setEndState({ mode: "elimination", placements: ["Alice", "Bob", "Cara", "Dan"] });

		render(GameEndPopup);

		expect(titleEl()).toHaveTextContent("PODIUM FINISH!");
		expect(titleEl()).toHaveClass("result--podium");
	});

	it("titles 4th place YOU LOST! with the loss style", () => {
		mockAuth.username = "Dan";
		setEndState({ mode: "elimination", placements: ["Alice", "Bob", "Cara", "Dan"] });

		render(GameEndPopup);

		expect(titleEl()).toHaveTextContent("YOU LOST!");
		expect(titleEl()).toHaveClass("result--lose");
	});

	it("never shows a podium finish in standard mode", () => {
		mockAuth.username = "Bob";
		setEndState({ mode: "standard", placements: ["Alice", "Bob"] });

		render(GameEndPopup);

		expect(titleEl()).toHaveTextContent("YOU LOST!");
		expect(titleEl()).toHaveClass("result--lose");
	});
});
