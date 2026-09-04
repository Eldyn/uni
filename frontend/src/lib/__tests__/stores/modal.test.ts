import { describe, it, expect, beforeEach } from "vitest";
import { storeModal } from "$lib/stores/modal.svelte";

describe("storeModal", () => {
	beforeEach(() => {
		while (storeModal.isAnyOpen) storeModal.unregister();
	});

	it("reports no modal open by default", () => {
		expect(storeModal.isAnyOpen).toBe(false);
	});

	it("reports open once one modal registers", () => {
		storeModal.register();
		expect(storeModal.isAnyOpen).toBe(true);
	});

	it("stays open while a second modal is registered, closes once both unregister", () => {
		storeModal.register();
		storeModal.register();
		storeModal.unregister();
		expect(storeModal.isAnyOpen).toBe(true);
		storeModal.unregister();
		expect(storeModal.isAnyOpen).toBe(false);
	});
});
