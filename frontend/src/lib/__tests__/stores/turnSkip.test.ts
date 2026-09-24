import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { storeTurnSkip } from "$stores/turnSkip.svelte";

describe("storeTurnSkip", () => {
	beforeEach(() => {
		storeTurnSkip.reset();
	});

	afterEach(() => {
		storeTurnSkip.reset();
		vi.useRealTimers();
	});

	it("holds the marks and the gated turn until the timer elapses", () => {
		vi.useFakeTimers();
		storeTurnSkip.present("alice", ["bob"], 500);

		expect(storeTurnSkip.marks).toEqual(["bob"]);
		expect(storeTurnSkip.presentingTurn).toBe("alice");

		vi.advanceTimersByTime(500);
		expect(storeTurnSkip.marks).toEqual([]);
		expect(storeTurnSkip.presentingTurn).toBeNull();
	});

	it("bumps the token on each presentation so a repeat skip retriggers", () => {
		storeTurnSkip.present("alice", ["bob"], 500);
		const first = storeTurnSkip.token;
		storeTurnSkip.present("alice", ["bob"], 500);
		expect(storeTurnSkip.token).toBe(first + 1);
	});

	it("does not gate the turn when no seat was skipped", () => {
		storeTurnSkip.present("alice", [], 500);
		expect(storeTurnSkip.presentingTurn).toBeNull();
		expect(storeTurnSkip.marks).toEqual([]);
	});

	it("dismisses an in-flight presentation on skip", () => {
		storeTurnSkip.present("alice", ["bob"], 500);
		storeTurnSkip.skip();
		expect(storeTurnSkip.marks).toEqual([]);
		expect(storeTurnSkip.presentingTurn).toBeNull();
	});
});
