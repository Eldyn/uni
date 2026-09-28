import { describe, it, expect, vi, beforeEach } from "vitest";
import type { BoardPlacement } from "$components/game/layout/boardPlacement";

const { startMatRipple } = vi.hoisted(() => ({ startMatRipple: vi.fn() }));

vi.mock("$components/game/three/ripple/matRipple.svelte", () => ({
	storeMatRipple: { startMatRipple },
	MAT_INITIAL_COLOR: "#663399"
}));

const placement: BoardPlacement = {
	mat: {} as never,
	handScale: 1,
	centerScale: 1,
	discardX: 0,
	discardZ: 0,
	localSeatZ: 5,
	localAvatarZ: 6,
	drawPileX: -3,
	drawPileZ: 5,
	drawPileScale: 1
};

const originUv = { u: 0.6, v: 0.4 };
const maxRadius = 1.2;

describe("buildPlayBeat — mat ripple trigger", () => {
	beforeEach(() => {
		startMatRipple.mockClear();
	});

	it("starts a normal ripple when the discard landing's move step completes", async () => {
		const { buildPlayBeat } = await import("$components/game/animation/baseBeats.svelte");
		const beat = buildPlayBeat({
			cardId: "card-1",
			playedByMe: true,
			placement,
			localHandSnapshot: { orderIds: [1], scrollEm: 0, maxHalfSpanEm: 10 },
			landingSpinDeg: 12,
			ripple: { cardColour: "#bd3130", strength: "normal", originUv, maxRadius }
		});

		const moveStep = beat.find((s) => s.op === "move")!;
		expect(startMatRipple).not.toHaveBeenCalled();
		(moveStep.payload?.onComplete as () => void)();

		expect(startMatRipple).toHaveBeenCalledWith("#bd3130", "normal", originUv, maxRadius);
	});

	it("starts a wild ripple with the chosen colour when it's already known at landing", async () => {
		const { buildPlayBeat } = await import("$components/game/animation/baseBeats.svelte");
		const beat = buildPlayBeat({
			cardId: "card-2",
			playedByMe: false,
			placement,
			localHandSnapshot: { orderIds: [], scrollEm: 0, maxHalfSpanEm: 10 },
			landingSpinDeg: 12,
			ripple: { cardColour: "#0470dd", strength: "wild", originUv, maxRadius }
		});

		const moveStep = beat.find((s) => s.op === "move")!;
		(moveStep.payload?.onComplete as () => void)();

		expect(startMatRipple).toHaveBeenCalledWith("#0470dd", "wild", originUv, maxRadius);
	});

	it("never calls startMatRipple when no ripple geometry is supplied", async () => {
		const { buildPlayBeat } = await import("$components/game/animation/baseBeats.svelte");
		const beat = buildPlayBeat({
			cardId: "card-3",
			playedByMe: true,
			placement,
			localHandSnapshot: { orderIds: [1], scrollEm: 0, maxHalfSpanEm: 10 },
			landingSpinDeg: 12
		});

		const moveStep = beat.find((s) => s.op === "move")!;
		expect(moveStep.payload?.onComplete).toBeUndefined();
		expect(startMatRipple).not.toHaveBeenCalled();
	});
});

describe("buildDrawBeats — never triggers the mat ripple", () => {
	beforeEach(() => {
		startMatRipple.mockClear();
	});

	it("hand draws carry no onComplete that calls startMatRipple", async () => {
		const { buildDrawBeats } = await import("$components/game/animation/baseBeats.svelte");
		const [beat] = buildDrawBeats({
			cardIds: ["card-4"],
			forLocalPlayer: true,
			placement
		});

		for (const step of beat) {
			if (typeof step.payload?.onComplete === "function") {
				(step.payload.onComplete as () => void)();
			}
		}

		expect(startMatRipple).not.toHaveBeenCalled();
	});
});
