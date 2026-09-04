import { describe, it, expect, beforeEach } from "vitest";
import { storeNavigation, pathForScreen } from "$stores/navigation.svelte";

describe("settings screen", () => {
	beforeEach(() => {
		window.history.replaceState(null, "", "/");
		storeNavigation.current = "main";
	});

	it("settings is a real screen with a real path", () => {
		expect(pathForScreen("settings")).toBe("/settings");
	});

	it("goto('settings') navigates there with no guard", () => {
		const applied = storeNavigation.goto("settings");
		expect(applied).toBe(true);
		expect(storeNavigation.current).toBe("settings");
	});
});
