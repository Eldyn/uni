import { describe, it, expect } from "vitest";
import { pathForScreen, screenForPath } from "$lib/stores/navigation.svelte";
import type { AppScreen } from "$lib/stores/navigation.svelte";

describe("pathForScreen / screenForPath", () => {
	it.each([
		["main", "/"],
		["lobbies", "/browse"],
		["lobby", "/lobby"],
		["game", "/play"],
		["settings", "/settings"],
		["stats", "/profile/stats"],
		["detailedStats", "/profile/stats/all"]
	] satisfies Array<[AppScreen, string]>)("%s maps to %s and back", (screen, path) => {
		expect(pathForScreen(screen)).toBe(path);
		expect(screenForPath(path)).toBe(screen);
	});

	it("returns null for an unrecognised path", () => {
		expect(screenForPath("/nonexistent")).toBeNull();
	});
});
