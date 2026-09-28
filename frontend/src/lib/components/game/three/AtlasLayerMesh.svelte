<!-- One composited atlas layer (background / value / border) drawn as its own
     plane, so mod art can be stacked between the background and the value/border.
     The composite mesh in CardMesh3D bakes all three into one slot,
     which cannot express an inset art layer.

     Material flags mirror CardMesh3D's composite face mesh: an opaque cutout
     (`alphaTest`, `transparent` only while dragging) so the layer sorts with
     neighbouring cards by depth and keeps the card's rounded-corner silhouette.
     A `transparent` layer would join the transparent pass and draw over opaque
     neighbour cards regardless of depth (see renderOrder.ts). -->
<script module lang="ts">
	import type { Vector4, WebGLProgramParametersWithUniforms } from "three";
	import atlasLayerUniforms from "$lib/shaders/atlasLayer/uniforms.frag.glsl?raw";
	import atlasLayerMapFragment from "$lib/shaders/atlasLayer/mapFragment.frag.glsl?raw";

	export const ATLAS_LAYER_SHADER_PROGRAM_KEY = "CardMesh3D_AtlasLayerPlane";

	export function patchAtlasLayerShader(
		shader: WebGLProgramParametersWithUniforms,
		uniforms: { uUvRect: Vector4 }
	) {
		shader.uniforms.uUvRect = { value: uniforms.uUvRect };
		shader.fragmentShader =
			atlasLayerUniforms +
			shader.fragmentShader.replace("#include <map_fragment>", atlasLayerMapFragment);
	}
</script>

<script lang="ts">
	import { T } from "@threlte/core";
	import {
		Color,
		DoubleSide,
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
		visible = true,
		alphaTest = 0.01,
		transparent = true,
		depthWrite = false,
		depthTest = true
	}: {
		entry: AtlasEntry;
		positionZ?: number;
		renderOrder?: number;
		color?: string | Color;
		opacity?: number;
		visible?: boolean;
		alphaTest?: number;
		transparent?: boolean;
		depthWrite?: boolean;
		depthTest?: boolean;
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
		patchAtlasLayerShader(shader, { uUvRect: rect });
	}

	function onCreate(material: MeshBasicMaterial) {
		material.onBeforeCompile = patch;
		material.customProgramCacheKey = () => ATLAS_LAYER_SHADER_PROGRAM_KEY;
		material.needsUpdate = true;
	}
</script>

{#if visible}
	<T.Mesh position.z={positionZ} {renderOrder}>
		<T.PlaneGeometry args={[CARD_WIDTH, CARD_HEIGHT]} />
		<T.MeshBasicMaterial
			map={texture}
			{color}
			{transparent}
			{opacity}
			{alphaTest}
			{depthWrite}
			{depthTest}
			toneMapped={false}
			side={DoubleSide}
			oncreate={onCreate}
		/>
	</T.Mesh>
{/if}
