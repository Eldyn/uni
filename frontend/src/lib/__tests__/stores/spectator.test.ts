import { describe, it, expect, beforeEach } from "vitest";
import { storeSpectator } from "$stores/spectator.svelte";
import { storeGame } from "$stores/game.svelte";
import { storeAuth } from "$stores/auth.svelte";
import { storeLobby } from "$stores/lobby.svelte";

describe("storeSpectator", () => {
	beforeEach(() => {
		storeSpectator.reset();
		storeGame.reset();
		storeAuth.username = "SpectatorGuy";
		storeLobby.current = {
			id: 1,
			code: "TEST1",
			host: "Alice",
			is_public: true,
			members: [
				{ username: "SpectatorGuy", is_host: false, is_connected: true, is_bot: false, is_ready: true, is_spectator: true }
			],
			settings: {}
		};
	});

	it("starts with no viewed username", () => {
		expect(storeSpectator.viewedUsername).toBeNull();
	});

	it("setViewedUsername updates the viewed username", () => {
		storeSpectator.setViewedUsername("alice");
		expect(storeSpectator.viewedUsername).toBe("alice");
	});

	it("reset clears the viewed username", () => {
		storeSpectator.setViewedUsername("alice");
		storeSpectator.reset();
		expect(storeSpectator.viewedUsername).toBeNull();
	});
});