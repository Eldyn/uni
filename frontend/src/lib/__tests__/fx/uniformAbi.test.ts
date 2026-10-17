import { describe, expect, it } from "vitest";
import {
	ENGINE_UNIFORM_NAMES,
	FRAGMENT_UNIFORM_PRELUDE,
	MOD_PARAM_SLOTS,
	POST_UNIFORM_NAMES,
	POST_UNIFORM_PRELUDE,
	UNIFORM_ABI_VERSION,
	createUniformBlock,
	setParams,
	updateUniformBlock
} from "../../components/game/fx/uniformAbi";

describe("uniform ABI names", () => {
	it("is version 1", () => {
		expect(UNIFORM_ABI_VERSION).toBe(1);
	});

	it("pins the engine uniform names in order", () => {
		expect([...ENGINE_UNIFORM_NAMES]).toEqual([
			"uTime",
			"uDelta",
			"uVelocity",
			"uSpeed",
			"uProgress",
			"uAge",
			"uLifetime",
			"uOutcomeValue",
			"uOutcomeIndex",
			"uActiveColor",
			"uTargetPosition",
			"uOriginPosition"
		]);
	});

	it("pins the post uniform names in order", () => {
		expect([...POST_UNIFORM_NAMES]).toEqual(["uResolution", "uSceneColor", "uIntensity"]);
	});

	it("keeps both name lists frozen and free of duplicates (additive-only)", () => {
		expect(Object.isFrozen(ENGINE_UNIFORM_NAMES)).toBe(true);
		expect(Object.isFrozen(POST_UNIFORM_NAMES)).toBe(true);
		const all = [...ENGINE_UNIFORM_NAMES, ...POST_UNIFORM_NAMES];
		expect(new Set(all).size).toBe(all.length);
	});
});

describe("GLSL preludes", () => {
	it("declares every engine uniform exactly once with its type", () => {
		const types: Record<string, string> = {
			uTime: "float",
			uDelta: "float",
			uVelocity: "vec3",
			uSpeed: "float",
			uProgress: "float",
			uAge: "float",
			uLifetime: "float",
			uOutcomeValue: "float",
			uOutcomeIndex: "float",
			uActiveColor: "vec3",
			uTargetPosition: "vec3",
			uOriginPosition: "vec3"
		};
		for (const name of ENGINE_UNIFORM_NAMES) {
			const pattern = new RegExp(`uniform\\s+${types[name]}\\s+${name}\\b(?!\\w)`, "g");
			expect(
				FRAGMENT_UNIFORM_PRELUDE.match(pattern)?.length,
				`${name} declaration count in fragment prelude`
			).toBe(1);
		}
	});

	it("declares the post uniforms exactly once with their types", () => {
		expect(POST_UNIFORM_PRELUDE.match(/uniform\s+vec2\s+uResolution\b/g)?.length).toBe(1);
		expect(POST_UNIFORM_PRELUDE.match(/uniform\s+sampler2D\s+uSceneColor\b/g)?.length).toBe(1);
		expect(POST_UNIFORM_PRELUDE.match(/uniform\s+float\s+uIntensity\b/g)?.length).toBe(1);
	});

	it("declares uParams[8] as vec4 in both preludes, once each", () => {
		for (const prelude of [FRAGMENT_UNIFORM_PRELUDE, POST_UNIFORM_PRELUDE]) {
			expect(prelude.match(/uniform\s+vec4\s+uParams\[8\];/g)?.length).toBe(1);
		}
	});

	it("does not leak post uniforms into the fragment prelude or engine uniforms into the post prelude", () => {
		for (const name of POST_UNIFORM_NAMES) {
			expect(FRAGMENT_UNIFORM_PRELUDE).not.toMatch(new RegExp(`uniform\\s+\\w+\\s+${name}\\b`));
		}
		for (const name of ENGINE_UNIFORM_NAMES) {
			expect(POST_UNIFORM_PRELUDE).not.toMatch(new RegExp(`uniform\\s+\\w+\\s+${name}\\b`));
		}
	});
});

describe("createUniformBlock", () => {
	it("returns three-style {value} entries with zeroed defaults", () => {
		const block = createUniformBlock();
		expect(block.uTime).toEqual({ value: 0 });
		expect(block.uVelocity).toEqual({ value: [0, 0, 0] });
		expect(block.uActiveColor.value).toEqual([0, 0, 0]);
		expect(block.uSceneColor.value).toBeNull();
		expect(block.uParams.value).toHaveLength(MOD_PARAM_SLOTS);
		for (const row of block.uParams.value) {
			expect(row).toEqual([0, 0, 0, 0]);
		}
	});
});

describe("updateUniformBlock", () => {
	it("writes frame fields in place and keeps the value object identity", () => {
		const block = createUniformBlock();
		const timeRef = block.uTime;
		const velocityRef = block.uVelocity.value;
		const result = updateUniformBlock(block, {
			uTime: 1.5,
			uVelocity: [1, 2, 3],
			uOutcomeIndex: 7
		});
		expect(result).toBeUndefined();
		expect(block.uTime).toBe(timeRef);
		expect(block.uTime.value).toBe(1.5);
		expect(block.uVelocity.value).toBe(velocityRef);
		expect(block.uVelocity.value).toEqual([1, 2, 3]);
		expect(block.uOutcomeIndex.value).toBe(7);
	});

	it("keeps defaults of 0 for fields the frame omits", () => {
		const block = createUniformBlock();
		updateUniformBlock(block, { uProgress: 0.25 });
		expect(block.uProgress.value).toBe(0.25);
		expect(block.uAge.value).toBe(0);
		expect(block.uTargetPosition.value).toEqual([0, 0, 0]);
	});

	it("mutates vec3 slots without allocating new arrays across calls", () => {
		const block = createUniformBlock();
		const before = block.uOriginPosition.value;
		updateUniformBlock(block, { uOriginPosition: [4, 5, 6] });
		expect(block.uOriginPosition.value).toBe(before);
		updateUniformBlock(block, { uOriginPosition: [7, 8, 9] });
		expect(block.uOriginPosition.value).toBe(before);
		expect(before).toEqual([7, 8, 9]);
	});
});

describe("setParams", () => {
	it("fills uParams and keeps slot identity", () => {
		const block = createUniformBlock();
		const rowRef = block.uParams.value[0];
		setParams(block, [
			[1, 2, 3, 4],
			[5, 6, 7, 8]
		]);
		expect(block.uParams.value[0]).toBe(rowRef);
		expect(block.uParams.value[0]).toEqual([1, 2, 3, 4]);
		expect(block.uParams.value[1]).toEqual([5, 6, 7, 8]);
		expect(block.uParams.value[2]).toEqual([0, 0, 0, 0]);
	});

	it("clamps to 8 vec4 slots", () => {
		const block = createUniformBlock();
		setParams(
			block,
			Array.from({ length: 20 }, (_, i) => [i, 0, 0, 0])
		);
		expect(block.uParams.value).toHaveLength(MOD_PARAM_SLOTS);
		expect(block.uParams.value[7]).toEqual([7, 0, 0, 0]);
	});

	it("pads short rows with zeros and drops extra columns", () => {
		const block = createUniformBlock();
		setParams(block, [[9, 8, 7, 6, 5, 4], [1]]);
		expect(block.uParams.value[0]).toEqual([9, 8, 7, 6]);
		expect(block.uParams.value[1]).toEqual([1, 0, 0, 0]);
	});

	it("replaces non-finite components with zero", () => {
		const block = createUniformBlock();
		setParams(block, [[NaN, Infinity, -Infinity, 2]]);
		expect(block.uParams.value[0]).toEqual([0, 0, 0, 2]);
	});

	it("zeroes slots beyond the given rows on a shorter call", () => {
		const block = createUniformBlock();
		setParams(block, [
			[1, 1, 1, 1],
			[2, 2, 2, 2]
		]);
		setParams(block, [[3, 3, 3, 3]]);
		expect(block.uParams.value[0]).toEqual([3, 3, 3, 3]);
		expect(block.uParams.value[1]).toEqual([0, 0, 0, 0]);
	});
});
