import { beforeEach, describe, expect, it } from "vitest";
import { storeAnimation } from "$stores/animation.svelte";
import { storeTableSpin } from "$stores/tableSpin.svelte";

const order = ["a", "b", "c", "d"];

function sync(target: string) {
	storeTableSpin.syncTarget(target, order, 0, null, null);
}

describe("storeTableSpin", () => {
	beforeEach(() => {
		storeAnimation.enabled = true; // even with animation on, commits are instant
		storeTableSpin.reset();
	});

	it("seeds the first target without any transition", () => {
		sync("a");
		expect(storeTableSpin.renderPov).toBe("a");
		expect(storeTableSpin.targetPov).toBe("a");
		expect(storeTableSpin.phase).toBe("idle");
		expect(storeTableSpin.transition).toBeNull();
	});

	it("commits a POV switch immediately — the spin is gone", () => {
		sync("a");
		sync("b");
		expect(storeTableSpin.renderPov).toBe("b");
		expect(storeTableSpin.phase).toBe("idle");
		expect(storeTableSpin.inheritProgress).toBe(1);
		expect(storeTableSpin.boardRotationY).toBe(0);
	});

	it("skip is a no-op with nothing in flight", () => {
		sync("a");
		sync("c");
		storeTableSpin.skip();
		expect(storeTableSpin.renderPov).toBe("c");
		expect(storeTableSpin.phase).toBe("idle");
	});

	it("retargets straight to the newest target", () => {
		sync("a");
		sync("b");
		sync("c");
		expect(storeTableSpin.renderPov).toBe("c");
	});

	it("cancelAndCommit keeps the committed target", () => {
		sync("a");
		sync("b");
		storeTableSpin.cancelAndCommit();
		expect(storeTableSpin.renderPov).toBe("b");
		expect(storeTableSpin.phase).toBe("idle");
	});

	it("accepts a null target (no players)", () => {
		sync("a");
		storeTableSpin.syncTarget(null, [], 0, null, null);
		expect(storeTableSpin.renderPov).toBeNull();
	});

	it("never rotates the board", () => {
		sync("a");
		storeTableSpin.syncTarget("c", order, Math.PI / 2, null, null);
		expect(storeTableSpin.renderPov).toBe("c");
		expect(storeTableSpin.boardRotationY).toBe(0);
		expect(storeTableSpin.active).toBe(false);
	});

	it("resets to a clean idle state", () => {
		sync("a");
		storeTableSpin.reset();
		expect(storeTableSpin.renderPov).toBeNull();
		expect(storeTableSpin.targetPov).toBeNull();
		expect(storeTableSpin.phase).toBe("idle");
	});
});
