import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { flushSync } from "svelte";
import MockThrelteMesh from "./MockThrelteMesh.svelte";
import MockTintMaterial from "./MockTintMaterial.svelte";
import { meshInstances, resetMockState } from "./drawPileMockState";

vi.mock("@threlte/core", () => ({
	T: new Proxy({}, { get: (_, key) => (key === "Mesh" ? MockThrelteMesh : MockTintMaterial) }),
	useTask: vi.fn(),
	useThrelte: () => ({ invalidate: vi.fn() })
}));

import { render, cleanup, screen } from "@testing-library/svelte";
import * as THREE from "three";
import TurnLoop3D from "$components/game/three/TurnLoop3D.svelte";
import { storeDirectionRing } from "$stores/directionRing.svelte";
import { DESKTOP_MAT_SHEET, PHONE_MAT_SHEET } from "$components/game/layout/playmat";
import { loopLength, loopRect, sheetBlockCount } from "$components/game/three/loopGeometry";
import {
	LOOP_CORNER_RADIUS_BLOCKS,
	LOOP_INSET_BLOCKS,
	LOOP_TARGET_PITCH_BLOCKS
} from "$components/game/animation/loopPlan";

type Uniforms = Record<string, { value: unknown }>;

const mat = { size: [16, 9], offsetX: 0, offsetZ: 0, quarterTurn: false } as never;

function rippleUniforms(): Uniforms {
	return {
		uFromColor: { value: new THREE.Color("#663399") },
		uToColor: { value: new THREE.Color("#663399") },
		uBlockCount: {
			value: new THREE.Vector2(
				sheetBlockCount(DESKTOP_MAT_SHEET).x,
				sheetBlockCount(DESKTOP_MAT_SHEET).y
			)
		}
	};
}

function loopMaterialUniforms(): Uniforms {
	const set = meshInstances
		.map((instance) => (instance as unknown as { uniforms?: Uniforms }).uniforms)
		.find((uniforms) => uniforms && "uLoopLength" in uniforms);
	if (!set) throw new Error("loop material not mounted");
	return set;
}

describe("TurnLoop3D", () => {
	beforeEach(() => {
		resetMockState();
		storeDirectionRing.reset();
	});

	afterEach(() => {
		cleanup();
		vi.restoreAllMocks();
	});

	it("renders the loop pointing the current way", () => {
		render(TurnLoop3D, {
			props: { mat, rippleUniforms: rippleUniforms(), sheet: DESKTOP_MAT_SHEET }
		});
		flushSync();
		expect(screen.getByTestId("turn-loop").dataset.direction).toBe("1");
	});

	it("writes the inset felt rectangle and a seamless pitch into its uniforms", () => {
		render(TurnLoop3D, {
			props: { mat, rippleUniforms: rippleUniforms(), sheet: DESKTOP_MAT_SHEET }
		});
		flushSync();
		const expected = loopRect(
			sheetBlockCount(DESKTOP_MAT_SHEET),
			DESKTOP_MAT_SHEET.feltUvRect,
			LOOP_INSET_BLOCKS,
			LOOP_CORNER_RADIUS_BLOCKS
		);
		const uniforms = loopMaterialUniforms();
		const center = uniforms.uLoopCenter.value as THREE.Vector2;
		const halfSize = uniforms.uLoopHalfSize.value as THREE.Vector2;
		expect(center.x).toBeCloseTo(expected.centerX);
		expect(center.y).toBeCloseTo(expected.centerY);
		expect(halfSize.x).toBeCloseTo(expected.halfWidth);
		expect(halfSize.y).toBeCloseTo(expected.halfHeight);
		const length = uniforms.uLoopLength.value as number;
		expect(length).toBeCloseTo(loopLength(expected));
		const cells = length / (uniforms.uPitch.value as number);
		expect(cells).toBeCloseTo(Math.round(cells));
		expect(Math.abs((uniforms.uPitch.value as number) - LOOP_TARGET_PITCH_BLOCKS)).toBeLessThan(
			LOOP_TARGET_PITCH_BLOCKS / 2
		);
	});

	it("shares the felt's uniform objects instead of copying them", () => {
		const shared = rippleUniforms();
		render(TurnLoop3D, {
			props: { mat, rippleUniforms: shared, sheet: DESKTOP_MAT_SHEET }
		});
		flushSync();
		const uniforms = loopMaterialUniforms();
		expect(uniforms.uFromColor).toBe(shared.uFromColor);
		expect(uniforms.uBlockCount).toBe(shared.uBlockCount);
	});

	it("uses the phone sheet's felt box when given it", () => {
		render(TurnLoop3D, {
			props: { mat, rippleUniforms: rippleUniforms(), sheet: PHONE_MAT_SHEET }
		});
		flushSync();
		const expected = loopRect(
			sheetBlockCount(PHONE_MAT_SHEET),
			PHONE_MAT_SHEET.feltUvRect,
			LOOP_INSET_BLOCKS,
			LOOP_CORNER_RADIUS_BLOCKS
		);
		const center = loopMaterialUniforms().uLoopCenter.value as THREE.Vector2;
		expect(center.x).toBeCloseTo(expected.centerX);
	});
});
