import { describe, it, expect, beforeEach } from "vitest";
import { storeMenu } from "$lib/stores/menu.svelte";

describe("storeMenu", () => {
	beforeEach(() => {
		storeMenu.close();
	});

	it("starts closed", () => {
		expect(storeMenu.isOpen).toBe(false);
	});

	it("toggle opens then closes", () => {
		storeMenu.toggle();
		expect(storeMenu.isOpen).toBe(true);
		storeMenu.toggle();
		expect(storeMenu.isOpen).toBe(false);
	});

	it("close is idempotent", () => {
		storeMenu.open();
		storeMenu.close();
		storeMenu.close();
		expect(storeMenu.isOpen).toBe(false);
	});

	it("open is idempotent", () => {
		storeMenu.open();
		storeMenu.open();
		expect(storeMenu.isOpen).toBe(true);
	});
});
