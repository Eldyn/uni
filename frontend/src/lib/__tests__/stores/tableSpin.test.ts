import { beforeEach, describe, expect, it } from "vitest";
import { storeAnimation } from "$stores/animation.svelte";
import { storeTableSpin } from "$stores/tableSpin.svelte";

const order = ["a", "b", "c", "d"];

function sync(target: string) {
	storeTableSpin.syncTarget(target, order, 0, null, null);
}

describe("storeTableSpin", () => {
	beforeEach(() => {
		storeAnimation.enabled = false; // instant commit path
		storeTableSpin.reset();
	});

	it("seeds the first target without animating", () => {
		sync("a");
		expect(storeTableSpin.renderPov).toBe("a");
		expect(storeTableSpin.phase).toBe("idle");
	});

	it("commits both phases instantly when animations are disabled", () => {
		sync("a");
		sync("b");
		expect(storeTableSpin.renderPov).toBe("b");
		expect(storeTableSpin.phase).toBe("idle");
		expect(storeTableSpin.inheritProgress).toBe(1);
	});

	it("skip commits immediately", () => {
		storeAnimation.enabled = true;
		sync("a");
		sync("c"); // starts a timed spin
		expect(storeTableSpin.phase).toBe("spin");
		storeTableSpin.skip();
		expect(storeTableSpin.renderPov).toBe("c");
		expect(storeTableSpin.phase).toBe("idle");
	});

	it("retargets from the settled state", () => {
		sync("a");
		sync("b");
		sync("c");
		expect(storeTableSpin.renderPov).toBe("c");
	});

	it("settles the in-flight target, then chains a new transition on a mid-flight retarget", () => {
		storeAnimation.enabled = true;
		sync("a");
		sync("b"); // starts a timed spin toward b
		expect(storeTableSpin.phase).toBe("spin");
		sync("c"); // retarget mid-flight
		expect(storeTableSpin.renderPov).toBe("b"); // old target settled
		expect(storeTableSpin.phase).toBe("spin"); // new transition to c in flight
		storeTableSpin.skip();
		expect(storeTableSpin.renderPov).toBe("c");
		expect(storeTableSpin.phase).toBe("idle");
	});

	it("cancelAndCommit settles instantly on the target", () => {
		storeAnimation.enabled = true;
		sync("a");
		sync("b");
		storeTableSpin.cancelAndCommit();
		expect(storeTableSpin.renderPov).toBe("b");
		expect(storeTableSpin.phase).toBe("idle");
	});

	it("does nothing for a single-player ring", () => {
		storeTableSpin.reset();
		storeTableSpin.syncTarget("a", ["a"], 0, null, null);
		storeTableSpin.syncTarget("a", ["a"], 0, null, null);
		expect(storeTableSpin.renderPov).toBe("a");
		expect(storeTableSpin.phase).toBe("idle");
	});
});
