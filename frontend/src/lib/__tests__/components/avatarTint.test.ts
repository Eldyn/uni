import { describe, expect, it } from "vitest";
import { Color } from "three";
import { localAvatarTint, opponentAvatarTint } from "$components/game/three/avatarTint";

const TARGET = new Color("#ffffff");

function rgb(color: Color): number[] {
	return [color.r, color.g, color.b].map((channel) => Math.round(channel * 100) / 100);
}

describe("opponentAvatarTint", () => {
	it("is white for a seat in play", () => {
		expect(rgb(opponentAvatarTint(false, 0, false, TARGET))).toEqual([1, 1, 1]);
	});

	it("dims to 0.45 grey when it is not this seat's turn", () => {
		expect(rgb(opponentAvatarTint(true, 0, false, TARGET))).toEqual([0.45, 0.45, 0.45]);
	});

	it("never depends on a seat color", () => {
		expect(opponentAvatarTint.length).toBe(4);
	});

	it("leans to red on a draw debt target", () => {
		const tint = opponentAvatarTint(false, 0, true, TARGET);
		expect(tint.r).toBeGreaterThan(tint.g);
	});
});

describe("localAvatarTint", () => {
	it("is white when in play", () => {
		expect(rgb(localAvatarTint(false, false))).toEqual([1, 1, 1]);
	});

	it("dims to 0.45 grey", () => {
		expect(rgb(localAvatarTint(true, false))).toEqual([0.45, 0.45, 0.45]);
	});

	it("is the danger red on a draw debt target", () => {
		const tint = localAvatarTint(false, true);
		expect(tint.r).toBeGreaterThan(tint.g);
		expect(tint.r).toBeGreaterThan(tint.b);
	});
});
