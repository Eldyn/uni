import { describe, it, expect } from "vitest";
import { Vector4, type WebGLProgramParametersWithUniforms } from "three";
import { patchCardShader } from "$components/game/three/CardMesh3D.svelte";

// Snapshot of the exact fragment-shader string patchCardShader produced
// before the inline GLSL template literals were moved into .glsl files
// (frontend/src/lib/shaders/cardMesh/*.glsl). Pinning the byte-identical
// output here guards the move against any accidental whitespace drift.
const EXPECTED_UNIFORMS_BLOCK = `
uniform vec4 uUvRectFront;
uniform vec4 uUvRectBack;
`;

const EXPECTED_MAP_FRAGMENT_BLOCK = `
#ifdef USE_MAP
	vec4 rect = gl_FrontFacing ? uUvRectFront : uUvRectBack;
	vec2 baseUv = vec2(gl_FrontFacing ? vMapUv.x : (1.0 - vMapUv.x), vMapUv.y);
	vec2 atlasUv = rect.xy + baseUv * rect.zw;
	vec4 sampledDiffuseColor = texture2D( map, atlasUv );
	#ifdef DECODE_VIDEO_TEXTURE
		sampledDiffuseColor = sRGBTransferEOTF( sampledDiffuseColor );
	#endif
	diffuseColor *= sampledDiffuseColor;
#endif
`;

describe("CardMesh3D patchCardShader: byte-identical output (glsl file move)", () => {
	it("produces the exact pinned fragment shader for a stub shader object", () => {
		const stub = {
			uniforms: {},
			vertexShader: "VERTEX_SRC_UNCHANGED",
			fragmentShader: "before#include <map_fragment>after"
		} as unknown as WebGLProgramParametersWithUniforms;

		patchCardShader(stub, {
			uUvRectFront: new Vector4(0.1, 0.2, 0.3, 0.4),
			uUvRectBack: new Vector4(0.5, 0.6, 0.7, 0.8)
		});

		const expectedFragmentShader =
			EXPECTED_UNIFORMS_BLOCK + "before" + EXPECTED_MAP_FRAGMENT_BLOCK + "after";

		expect(stub.fragmentShader).toBe(expectedFragmentShader);
		expect(stub.vertexShader).toBe("VERTEX_SRC_UNCHANGED");
	});
});
