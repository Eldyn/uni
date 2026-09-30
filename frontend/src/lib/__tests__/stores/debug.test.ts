import { describe, it, expect, beforeEach } from "vitest";
import { DebugFlag, DEBUG_STORAGE_KEY } from "$stores/debug.svelte";

describe("DebugFlag", () => {
	beforeEach(() => {
		localStorage.clear();
	});

	it("is off by default", () => {
		expect(new DebugFlag("").enabled).toBe(false);
	});

	it("is on when the debug query parameter is present", () => {
		expect(new DebugFlag("?debug").enabled).toBe(true);
	});

	it("is on when the persisted flag is set", () => {
		localStorage.setItem(DEBUG_STORAGE_KEY, "1");

		expect(new DebugFlag("").enabled).toBe(true);
	});

	it("persists a toggle", () => {
		const flag = new DebugFlag("");
		flag.enabled = true;

		expect(localStorage.getItem(DEBUG_STORAGE_KEY)).toBe("1");
		expect(new DebugFlag("").enabled).toBe(true);

		flag.enabled = false;
		expect(localStorage.getItem(DEBUG_STORAGE_KEY)).toBeNull();
	});
});
