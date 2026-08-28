import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { storeGame } from "$lib/stores/game.svelte";

// ws.emit is a no-op when the socket is not connected (checked in #validateAndSend),
// so the test can check the latch behavior without a real WebSocket.

describe("game store: double-submit prevention", () => {
	beforeEach(() => {
		vi.useFakeTimers();
		storeGame.isActionPending = false;
	});

	afterEach(() => {
		vi.useRealTimers();
		storeGame.isActionPending = false;
	});

	it("playCard sets isActionPending", () => {
		storeGame.playCard(1);
		expect(storeGame.isActionPending).toBe(true);
	});

	it("second playCard while pending is ignored", () => {
		storeGame.playCard(1);
		expect(storeGame.isActionPending).toBe(true);
		storeGame.playCard(2);
		expect(storeGame.isActionPending).toBe(true);
	});

	it("safety timer releases latch after 3 s", () => {
		storeGame.playCard(1);
		expect(storeGame.isActionPending).toBe(true);
		vi.advanceTimersByTime(3001);
		expect(storeGame.isActionPending).toBe(false);
	});

	it("drawCard sets isActionPending", () => {
		storeGame.drawCard();
		expect(storeGame.isActionPending).toBe(true);
	});

	it("drawCard is no-op while pending", () => {
		storeGame.drawCard();
		storeGame.drawCard();
		expect(storeGame.isActionPending).toBe(true);
	});
});
