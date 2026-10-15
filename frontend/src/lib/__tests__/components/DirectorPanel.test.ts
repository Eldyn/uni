import { describe, it, expect, vi, afterEach } from "vitest";
import { render, cleanup } from "@testing-library/svelte";
import { setCleanView } from "$lib/stores/director.svelte";

vi.mock("$stores/game.svelte", () => ({
	storeGame: {
		state: {
			players: [{ username: "alice" }, { username: "bob" }]
		}
	}
}));

vi.mock("$stores/lobby.svelte", () => ({
	storeLobby: {
		current: { settings: { creative: true } }
	}
}));

import DirectorPanel from "$components/game/DirectorPanel.svelte";

describe("DirectorPanel clean view", () => {
	afterEach(() => {
		cleanup();
		setCleanView(false);
	});

	it("renders the panel while clean view is off", () => {
		setCleanView(false);
		render(DirectorPanel);
		expect(document.querySelector(".director-panel")).not.toBeNull();
	});

	it("unmounts the panel when clean view is on", () => {
		setCleanView(true);
		render(DirectorPanel);
		expect(document.querySelector(".director-panel")).toBeNull();
	});
});
