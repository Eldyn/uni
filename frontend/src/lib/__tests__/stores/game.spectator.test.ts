import { describe, it, expect, vi, beforeEach } from "vitest";
import { storeGame } from "$lib/stores/game.svelte";
import { storeAuth } from "$lib/stores/auth.svelte";
import { storeLobby } from "$lib/stores/lobby.svelte";
import { ws } from "$lib/stores/ws.svelte";

describe("game store: spectator subsystem", () => {
	beforeEach(() => {
		storeGame.reset();
		storeAuth.username = "SpectatorGuy";
	});

	it("identifies spectator from lobby member flag", () => {
		storeLobby.current = {
			id: 1,
			code: "TEST1",
			host: "Alice",
			is_public: true,
			members: [
				{ username: "Alice", is_host: true, is_connected: true, is_bot: false, is_ready: true, is_spectator: false },
				{ username: "SpectatorGuy", is_host: false, is_connected: true, is_bot: false, is_ready: true, is_spectator: true }
			],
			settings: {} as any
		};

		expect(storeGame.isSpectator).toBe(true);
	});

	it("identifies spectator when absent from match players list", () => {
		storeLobby.current = null;
		storeGame.state = {
			active_type: "Red",
			current_turn: "Alice",
			play_direction: 1,
			players: [
				{ username: "Alice", card_count: 5, is_bot: false },
				{ username: "Bob", card_count: 5, is_bot: false }
			],
			pending_draws: 0,
			draw_pile_size: 40,
			mode: "elimination",
			spectator_count: 3,
			placements: ["Charlie"]
		};

		expect(storeGame.isSpectator).toBe(true);
		expect(storeGame.spectatorCount).toBe(3);
		expect(storeGame.placements).toEqual(["Charlie"]);
	});

	it("guards spectator against playCard, drawCard, and submitInput", () => {
		storeLobby.current = {
			id: 1,
			code: "TEST1",
			host: "Alice",
			is_public: true,
			members: [
				{ username: "SpectatorGuy", is_host: false, is_connected: true, is_bot: false, is_ready: true, is_spectator: true }
			],
			settings: {} as any
		};

		const emitSpy = vi.spyOn(ws, "emit");

		storeGame.playCard(10);
		expect(storeGame.isActionPending).toBe(false);
		expect(emitSpy).not.toHaveBeenCalled();

		storeGame.drawCard();
		expect(storeGame.isActionPending).toBe(false);
		expect(emitSpy).not.toHaveBeenCalled();

		storeGame.submitInput("red");
		expect(storeGame.isActionPending).toBe(false);
		expect(emitSpy).not.toHaveBeenCalled();

		emitSpy.mockRestore();
	});
});
