import { describe, it, expect, beforeEach, vi } from "vitest";
import { storeSpectator } from "$stores/spectator.svelte";
import { storeAnimation } from "$stores/animation.svelte";
import { storeGame } from "$stores/game.svelte";
import { storeAuth } from "$stores/auth.svelte";
import { storeLobby } from "$stores/lobby.svelte";
import { ClientAction, ws } from "$stores/ws.svelte";

describe("storeSpectator", () => {
	beforeEach(() => {
		storeSpectator.reset();
		storeGame.reset();
		storeAnimation.enabled = false;
		storeAuth.username = "SpectatorGuy";
		storeLobby.current = {
			id: 1,
			code: "TEST1",
			host: "Alice",
			is_public: true,
			members: [
				{
					username: "SpectatorGuy",
					is_host: false,
					is_connected: true,
					is_bot: false,
					is_ready: true,
					is_spectator: true
				}
			],
			settings: {}
		};
	});

	it("starts with no viewed username", () => {
		expect(storeSpectator.viewedUsername).toBeNull();
	});

	it("setViewedUsername updates the viewed username when animation is off", () => {
		storeSpectator.setViewedUsername("alice");
		expect(storeSpectator.viewedUsername).toBe("alice");
		expect(storeSpectator.transitioning).toBe(false);
		expect(storeSpectator.fadeOpacity).toBe(0);
	});

	it("reports the viewed player to the server so the per-player spectator count is accurate", () => {
		const emitSpy = vi.spyOn(ws, "emit").mockImplementation(() => {});
		storeSpectator.setViewedUsername("alice");
		expect(emitSpy).toHaveBeenCalledWith(ClientAction.SpectatorView, {
			viewed_username: "alice"
		});
		emitSpy.mockRestore();
	});

	it("ignores a switch when the local client is not spectating", () => {
		storeLobby.current!.members[0].is_spectator = false;
		storeSpectator.setViewedUsername("alice");
		expect(storeSpectator.viewedUsername).toBeNull();
	});

	it("defers the POV switch until the screen is dark when animation is on", () => {
		storeAnimation.enabled = true;
		storeSpectator.setViewedUsername("alice");
		expect(storeSpectator.transitioning).toBe(true);
		expect(storeSpectator.viewedUsername).toBeNull();
		storeSpectator.reset();
	});

	it("reset clears the viewed username and any fade in flight", () => {
		storeAnimation.enabled = true;
		storeSpectator.setViewedUsername("alice");
		storeSpectator.reset();
		expect(storeSpectator.viewedUsername).toBeNull();
		expect(storeSpectator.transitioning).toBe(false);
		expect(storeSpectator.fadeOpacity).toBe(0);
	});
});
