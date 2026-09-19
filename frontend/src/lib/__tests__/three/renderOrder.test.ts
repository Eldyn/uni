import { describe, it, expect } from "vitest";
import { RENDER_ORDER, cardRenderOrder } from "$components/game/three/renderOrder";

describe("render-order tiers", () => {
	it("orders the in-scene tiers bottom-to-top", () => {
		expect(RENDER_ORDER.idle).toBeLessThan(RENDER_ORDER.lifted);
		expect(RENDER_ORDER.lifted).toBeLessThan(RENDER_ORDER.seatSprite);
		expect(RENDER_ORDER.seatSprite).toBeLessThan(RENDER_ORDER.pendingPlay);
		expect(RENDER_ORDER.pendingPlay).toBeLessThan(RENDER_ORDER.dragged);
	});

	it("maps a dragged handle to the top tier regardless of other flags", () => {
		expect(cardRenderOrder({ dragT: 1 }, false)).toBe(RENDER_ORDER.dragged);
		expect(cardRenderOrder({ dragT: 1, liftT: 1 }, true)).toBe(RENDER_ORDER.dragged);
		// A partially-lifted drag (dragT mid-tween down) still counts as dragged.
		expect(cardRenderOrder({ dragT: 0.01 }, false)).toBe(RENDER_ORDER.dragged);
	});

	it("does not regress the existing non-dragged tiers", () => {
		expect(cardRenderOrder({ dragT: 0, liftT: 1 }, false)).toBe(RENDER_ORDER.lifted);
		expect(cardRenderOrder({ dragT: 0, liftT: 0 }, false)).toBe(RENDER_ORDER.idle);
		expect(cardRenderOrder({ dragT: 0, liftT: 1 }, true)).toBe(RENDER_ORDER.pendingPlay);
		expect(cardRenderOrder({}, false)).toBe(RENDER_ORDER.idle);
	});

	it("keeps a dragged card above the seat sprites and below no in-scene object", () => {
		expect(cardRenderOrder({ dragT: 1 }, false)).toBeGreaterThan(RENDER_ORDER.seatSprite);
		expect(RENDER_ORDER.dragged).toBe(Math.max(...Object.values(RENDER_ORDER)));
	});
});
