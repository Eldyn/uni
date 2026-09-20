import { describe, it, expect, beforeEach } from "vitest";
import { storeCardDetail } from "$stores/cardDetail.svelte";

describe("storeCardDetail", () => {
	beforeEach(() => {
		storeCardDetail.close();
		storeCardDetail.resetLongPress();
	});

	it("opens with the card and pointer coordinates", () => {
		storeCardDetail.open({ type: "red", value: "skip" }, 120, 240);

		expect(storeCardDetail.current).toEqual({
			card: { type: "red", value: "skip" },
			x: 120,
			y: 240
		});
	});

	it("replaces the current card when a new one is opened", () => {
		storeCardDetail.open({ type: "red", value: "skip" }, 1, 2);
		storeCardDetail.open({ type: "blue", value: "7" }, 3, 4);

		expect(storeCardDetail.current?.card).toEqual({ type: "blue", value: "7" });
		expect(storeCardDetail.current?.x).toBe(3);
	});

	it("closes back to nothing", () => {
		storeCardDetail.open({ type: "red", value: "skip" }, 1, 2);
		storeCardDetail.close();

		expect(storeCardDetail.current).toBeNull();
	});

	it("consumes the long-press mark exactly once", () => {
		storeCardDetail.markLongPress();

		expect(storeCardDetail.consumeLongPress()).toBe(true);
		expect(storeCardDetail.consumeLongPress()).toBe(false);
	});

	it("clears a pending long-press on the next press", () => {
		storeCardDetail.markLongPress();
		storeCardDetail.resetLongPress();

		expect(storeCardDetail.consumeLongPress()).toBe(false);
	});
});
