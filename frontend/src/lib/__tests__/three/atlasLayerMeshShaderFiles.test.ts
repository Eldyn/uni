import { describe, it, expect } from "vitest";
import { Vector4, type WebGLProgramParametersWithUniforms } from "three";
import {
	patchAtlasLayerShader,
	ATLAS_LAYER_SHADER_PROGRAM_KEY
} from "$components/game/three/AtlasLayerMesh.svelte";

// Snapshot of the exact fragment-shader string the onBeforeCompile patch
// produced before the inline GLSL template literals were moved into .glsl
// files (frontend/src/lib/shaders/atlasLayer/*.glsl). Pinning the
// byte-identical output here guards the move against whitespace drift.
const EXPECTED_UNIFORMS_BLOCK = `uniform vec4 uUvRect;\n`;

const EXPECTED_MAP_FRAGMENT_BLOCK = `
#ifdef USE_MAP
	vec2 atlasUv = uUvRect.xy + vMapUv * uUvRect.zw;
	vec4 sampledDiffuseColor = texture2D( map, atlasUv );
	diffuseColor *= sampledDiffuseColor;
#endif
`;

describe("AtlasLayerMesh patchAtlasLayerShader: byte-identical output (glsl file move)", () => {
	it("exports a stable shader program cache key", () => {
		expect(ATLAS_LAYER_SHADER_PROGRAM_KEY).toBe("CardMesh3D_AtlasLayerPlane");
	});

	it("produces the exact pinned fragment shader for a stub shader object", () => {
		const stub = {
			uniforms: {},
			vertexShader: "VERTEX_SRC_UNCHANGED",
			fragmentShader: "before#include <map_fragment>after"
		} as unknown as WebGLProgramParametersWithUniforms;

		patchAtlasLayerShader(stub, { uUvRect: new Vector4(0.1, 0.2, 0.3, 0.4) });

		const expectedFragmentShader =
			EXPECTED_UNIFORMS_BLOCK + "before" + EXPECTED_MAP_FRAGMENT_BLOCK + "after";

		expect(stub.fragmentShader).toBe(expectedFragmentShader);
		expect(stub.vertexShader).toBe("VERTEX_SRC_UNCHANGED");
	});
});
