import { describe, it, expect, vi } from "vitest";
import { screenEffectRenderer } from "$components/game/animation/stepRenderers/screenEffect";
import { materialEffectRenderer } from "$components/game/animation/stepRenderers/materialEffect";
import type { RenderContext } from "$components/game/animation/renderContext";

const noopCtx: RenderContext = {
	getPose: () => ({ x: 0, y: 0, z: 0, spinDeg: 0, flipDeg: 0, scale: 1, turned: false, opacity: 1 }),
	resolveAnchor: () => [0, 0, 0]
};

describe("reserved step renderer stubs", () => {
	it("screenEffect returns an already-complete timeline and logs a warning", () => {
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
		const timeline = screenEffectRenderer(
			{ op: "screenEffect", target: "screen", payload: { effect: "desaturate" } },
			noopCtx
		);
		expect(timeline.progress()).toBe(1);
		expect(warn).toHaveBeenCalledWith(expect.stringContaining("screenEffect"));
		warn.mockRestore();
	});

	it("materialEffect returns an already-complete timeline and logs a warning", () => {
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
		const timeline = materialEffectRenderer(
			{ op: "materialEffect", target: "card-1", payload: {} },
			noopCtx
		);
		expect(timeline.progress()).toBe(1);
		expect(warn).toHaveBeenCalledWith(expect.stringContaining("materialEffect"));
		warn.mockRestore();
	});
});
