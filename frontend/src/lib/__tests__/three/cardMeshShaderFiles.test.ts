import { describe, it, expect } from "vitest";
import { Vector4, type WebGLProgramParametersWithUniforms } from "three";
import { patchCardShader } from "$components/game/three/CardMesh3D.svelte";

function patchStub() {
	const stub = {
		uniforms: {},
		vertexShader: "VERTEX_SRC_UNCHANGED",
		fragmentShader: "before#include <map_pars_fragment>mid#include <map_fragment>after"
	} as unknown as WebGLProgramParametersWithUniforms;
	const glintStrength = { value: 0 };
	const glintPhase = { value: 0 };

	patchCardShader(stub, {
		uUvRectFront: new Vector4(0.1, 0.2, 0.3, 0.4),
		uUvRectBack: new Vector4(0.5, 0.6, 0.7, 0.8),
		uGlintStrength: glintStrength,
		uGlintPhase: glintPhase
	});
	return { stub, glintStrength, glintPhase };
}

describe("CardMesh3D patchCardShader", () => {
	it("shares the per-card glint uniform objects so the card can drive them", () => {
		const { stub, glintStrength, glintPhase } = patchStub();
		const uniforms = stub.uniforms as Record<string, unknown>;

		expect(uniforms.uGlintStrength).toBe(glintStrength);
		expect(uniforms.uGlintPhase).toBe(glintPhase);
		expect(uniforms.uUvRectFront).toBeDefined();
		expect(uniforms.uUvRectBack).toBeDefined();
	});

	it("declares the glint helper after the map uniforms and calls it from the map fragment", () => {
		const { stub } = patchStub();
		const shader = stub.fragmentShader;

		expect(shader.indexOf("glintContribution(vec4 rect")).toBeGreaterThan(
			shader.indexOf("#include <map_pars_fragment>")
		);
		expect(shader).toContain("diffuseColor.rgb += glintContribution(rect, baseUv)");
		expect(shader).not.toContain("#include <map_fragment>");
	});

	it("leaves the vertex shader untouched", () => {
		const { stub } = patchStub();
		expect(stub.vertexShader).toBe("VERTEX_SRC_UNCHANGED");
	});
});
