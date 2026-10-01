import { describe, it, expect, beforeEach } from "vitest";
import { storeTooltipStack } from "$stores/tooltipStack.svelte";

function rect(left: number, top: number, width: number, height: number) {
	return {
		left,
		top,
		width,
		height,
		right: left + width,
		bottom: top + height,
		x: left,
		y: top,
		toJSON: () => {}
	};
}

function fullyOverlaps(a: { x: number; y: number }, b: { x: number; y: number }) {
	const W = 260;
	const H = 140;
	return a.x < b.x + W && b.x < a.x + W && a.y < b.y + H && b.y < a.y + H;
}

describe("storeTooltipStack: two children from the same root anchor", () => {
	beforeEach(() => storeTooltipStack.closeAll());

	it("does not fully overlap when the anchor is mid-viewport", () => {
		const card = document.createElement("button");
		card.getBoundingClientRect = () => rect(100, 300, 200, 120);
		document.body.appendChild(card);

		const a = storeTooltipStack.open("vanilla:play", card, null)!;
		storeTooltipStack.measureAndReposition(a.id, { width: 260, height: 140 });
		const b = storeTooltipStack.open("vanilla:draw", card, null)!;
		storeTooltipStack.measureAndReposition(b.id, { width: 260, height: 140 });

		expect(storeTooltipStack.stack).toHaveLength(2);
		expect(fullyOverlaps(a, b), `a=(${a.x},${a.y}) b=(${b.x},${b.y})`).toBe(false);
	});

	it("does not fully overlap when the anchor is pinned to the top edge", () => {
		const card = document.createElement("button");
		card.getBoundingClientRect = () => rect(100, 4, 200, 100);
		document.body.appendChild(card);

		const a = storeTooltipStack.open("vanilla:play", card, null)!;
		storeTooltipStack.measureAndReposition(a.id, { width: 260, height: 140 });
		const b = storeTooltipStack.open("vanilla:draw", card, null)!;
		storeTooltipStack.measureAndReposition(b.id, { width: 260, height: 140 });

		expect(storeTooltipStack.stack).toHaveLength(2);
		expect(fullyOverlaps(a, b), `a=(${a.x},${a.y}) b=(${b.x},${b.y})`).toBe(false);
	});
});
