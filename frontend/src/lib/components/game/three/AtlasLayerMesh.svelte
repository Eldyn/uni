<!-- One composited atlas layer (background / value / border) drawn as its own
     plane, so mod art can be stacked between the background and the value/border.
     The composite mesh in CardMesh3D bakes all three into one slot,
     which cannot express an inset art layer. -->
<script lang="ts">
	import { T } from "@threlte/core";
	import {
		Color,
		Vector4,
		type MeshBasicMaterial,
		type WebGLProgramParametersWithUniforms
	} from "three";
	import { CARD_WIDTH, CARD_HEIGHT } from "./units";
	import { getAtlasPage, ATLAS_PAGE_VERSION, type AtlasEntry } from "./cardFaceAtlas";

	let {
		entry,
		positionZ = 0,
		renderOrder = 0,
		color = "#ffffff",
		opacity = 1,
		visible = true
	}: {
		entry: AtlasEntry;
		positionZ?: number;
		renderOrder?: number;
		color?: string | Color;
		opacity?: number;
		visible?: boolean;
	} = $props();

	const rect = new Vector4();

	$effect(() => {
		rect.set(entry.u0, entry.v0, entry.u1 - entry.u0, entry.v1 - entry.v0);
	});

	let texture = $derived.by(() => {
		void ATLAS_PAGE_VERSION.value;
		return getAtlasPage(entry.page);
	});

	function patch(shader: WebGLProgramParametersWithUniforms) {
		shader.uniforms.uUvRect = { value: rect };
		shader.fragmentShader =
			`uniform vec4 uUvRect;\n` +
			shader.fragmentShader.replace(
				"#include <map_fragment>",
				`
#ifdef USE_MAP
	vec2 atlasUv = uUvRect.xy + vMapUv * uUvRect.zw;
	vec4 sampledDiffuseColor = texture2D( map, atlasUv );
	diffuseColor *= sampledDiffuseColor;
#endif
`
			);
	}

	function onCreate(material: MeshBasicMaterial) {
		material.onBeforeCompile = patch;
		material.customProgramCacheKey = () => "CardMesh3D_AtlasLayerPlane";
		material.needsUpdate = true;
	}
</script>

{#if visible}
	<T.Mesh position.z={positionZ} {renderOrder}>
		<T.PlaneGeometry args={[CARD_WIDTH, CARD_HEIGHT]} />
		<T.MeshBasicMaterial
			map={texture}
			{color}
			transparent
			{opacity}
			alphaTest={0.01}
			depthWrite={false}
			toneMapped={false}
			oncreate={onCreate}
		/>
	</T.Mesh>
{/if}
