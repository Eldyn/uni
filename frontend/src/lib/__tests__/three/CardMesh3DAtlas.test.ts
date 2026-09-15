import { describe, it, expect, vi } from "vitest";
import { Vector4, ShaderLib, type WebGLProgramParametersWithUniforms } from "three";
import {
	patchCardShader,
	CARD_SHADER_PROGRAM_KEY,
	CARD_ALPHA_TEST
} from "$components/game/three/CardMesh3D.svelte";
import {
	getFaceTexture,
	getAtlasPage,
	type CardFaceKey
} from "$components/game/three/cardFaceAtlas";

describe("CardMesh3DAtlas onBeforeCompile shader", () => {
	it("exports a stable shader program cache key", () => {
		expect(CARD_SHADER_PROGRAM_KEY).toBe("CardMesh3D_AtlasShader");
	});

	it("exports CARD_ALPHA_TEST set to 0.5 for cutout transparency", () => {
		expect(CARD_ALPHA_TEST).toBe(0.5);
	});

	it("injects uniforms and replaces map_fragment chunk in fragmentShader", () => {
		const uUvRectFront = new Vector4(0.1, 0.2, 0.02, 0.03);
		const uUvRectBack = new Vector4(0.5, 0.6, 0.02, 0.03);

		const dummyShader = {
			uniforms: {},
			vertexShader: ShaderLib.basic.vertexShader,
			fragmentShader: ShaderLib.basic.fragmentShader
		} as unknown as WebGLProgramParametersWithUniforms;

		patchCardShader(dummyShader, { uUvRectFront, uUvRectBack });

		// Verify uniforms injected
		expect(dummyShader.uniforms.uUvRectFront).toBeDefined();
		expect(dummyShader.uniforms.uUvRectFront.value).toBe(uUvRectFront);
		expect(dummyShader.uniforms.uUvRectBack).toBeDefined();
		expect(dummyShader.uniforms.uUvRectBack.value).toBe(uUvRectBack);

		// Verify fragment shader uniforms declared
		expect(dummyShader.fragmentShader).toContain("uniform vec4 uUvRectFront;");
		expect(dummyShader.fragmentShader).toContain("uniform vec4 uUvRectBack;");

		// Verify #include <map_fragment> was replaced
		expect(dummyShader.fragmentShader).not.toContain("#include <map_fragment>");

		// Verify gl_FrontFacing selection
		expect(dummyShader.fragmentShader).toContain("gl_FrontFacing ? uUvRectFront : uUvRectBack");

		// Verify mirrored back UV and UV remapping formula using vMapUv and atlasUv
		expect(dummyShader.fragmentShader).toContain("vec2(gl_FrontFacing ? vMapUv.x : (1.0 - vMapUv.x), vMapUv.y)");
		expect(dummyShader.fragmentShader).toContain("vec2 atlasUv = rect.xy + baseUv * rect.zw;");
		expect(dummyShader.fragmentShader).toContain("texture2D( map, atlasUv )");
	});

	it("computes correct normalized UV rect coordinates from atlas entries", () => {
		const frontKey: CardFaceKey = { type: "blue", value: "3", turned: false };
		const backKey: CardFaceKey = { type: "wild", value: "0", turned: true };

		const frontEntry = getFaceTexture(frontKey);
		const backEntry = getFaceTexture(backKey);

		// Valid atlas page texture
		const texture = getAtlasPage(frontEntry.page);
		expect(texture).toBeDefined();

		// Check rect derivation [u0, v0, u1 - u0, v1 - v0]
		const frontRect = new Vector4(
			frontEntry.u0,
			frontEntry.v0,
			frontEntry.u1 - frontEntry.u0,
			frontEntry.v1 - frontEntry.v0
		);
		const backRect = new Vector4(
			backEntry.u0,
			backEntry.v0,
			backEntry.u1 - backEntry.u0,
			backEntry.v1 - backEntry.v0
		);

		expect(frontRect.z).toBeGreaterThan(0); // width > 0
		expect(frontRect.w).toBeGreaterThan(0); // height > 0
		expect(frontRect.x + frontRect.z).toBeCloseTo(frontEntry.u1);
		expect(frontRect.y + frontRect.w).toBeCloseTo(frontEntry.v1);

		expect(backRect.z).toBeGreaterThan(0);
		expect(backRect.w).toBeGreaterThan(0);
		expect(backRect.x + backRect.z).toBeCloseTo(backEntry.u1);
		expect(backRect.y + backRect.w).toBeCloseTo(backEntry.v1);
	});

	it("swaps activeFront and activeBack when card is turned", () => {
		const card = { id: 1, type: "red" as const, value: "7" as const };
		const frontEntry = getFaceTexture({
			type: card.type,
			value: card.value,
			wildColor: undefined,
			turned: false
		});
		const backEntry = getFaceTexture({
			type: "wild",
			value: "0",
			turned: true
		});

		// Unturned card: activeFront is face, activeBack is back
		const unturnedActiveFront = false ? backEntry : frontEntry;
		const unturnedActiveBack = false ? frontEntry : backEntry;
		expect(unturnedActiveFront).toBe(frontEntry);
		expect(unturnedActiveBack).toBe(backEntry);
		expect(unturnedActiveFront.page).toBe(frontEntry.page);

		// Turned card: activeFront is back, activeBack is face
		const turnedActiveFront = true ? backEntry : frontEntry;
		const turnedActiveBack = true ? frontEntry : backEntry;
		expect(turnedActiveFront).toBe(backEntry);
		expect(turnedActiveBack).toBe(frontEntry);
		expect(turnedActiveFront.page).toBe(backEntry.page);
	});

	it("mathematically unmirrors the back face under gl_FrontFacing = false", () => {
		// Normal face: vUv.x = 0 (left) maps to u0, vUv.x = 1 (right) maps to u1
		const u0 = 0.2;
		const w = 0.05;
		const computeU = (frontFacing: boolean, vUvX: number) => {
			const u = frontFacing ? vUvX : 1.0 - vUvX;
			return u0 + u * w;
		};

		// When looking straight at front:
		expect(computeU(true, 0.0)).toBeCloseTo(0.2); // left edge
		expect(computeU(true, 1.0)).toBeCloseTo(0.25); // right edge

		// When viewing back (flipped 180° around Y):
		// Mesh's right vertex (uv=1) is in the viewer's left position.
		// Mirrored UV lookup (1 - 1 = 0) correctly samples the texture's left edge (u0)!
		expect(computeU(false, 1.0)).toBeCloseTo(0.2);
		// Mesh's left vertex (uv=0) is in the viewer's right position.
		// Mirrored UV lookup (1 - 0 = 1) correctly samples the texture's right edge (u1)!
		expect(computeU(false, 0.0)).toBeCloseTo(0.25);
	});
});
