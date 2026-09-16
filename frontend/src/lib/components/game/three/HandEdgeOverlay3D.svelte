<!-- HandEdgeOverlay3D: Renders an alpha-blended overlay plane in front of the
     hand row edges. Supports "vignette" (darkened edge vignette) and "fadeOverlay"
     (fading to background felt/table color). Never touches card meshes' own materials
     or opacity, preserving opaque alphaTest depth testing. -->
<script lang="ts">
	import { T } from "@threlte/core";
	import { CARD_HEIGHT } from "./units";
	import { OVERLAY_Y, createGradientTexture } from "./handEdgeOverlay";

	let {
		rig,
		placement,
		mode = "vignette"
	}: {
		rig: { halfWidth: number; halfHeight: number; centerZ?: number };
		placement: { localSeatZ: number; handScale: number };
		mode: "vignette" | "fadeOverlay" | "none";
	} = $props();

	let texture = $derived(mode !== "none" ? createGradientTexture(mode) : null);
	let width = $derived(2 * rig.halfWidth);
	let height = $derived(CARD_HEIGHT * placement.handScale * 1.5);
</script>

{#if mode !== "none" && texture}
	<T.Mesh
		position={[0, OVERLAY_Y, placement.localSeatZ]}
		rotation.x={-Math.PI / 2}
		renderOrder={10}
	>
		<T.PlaneGeometry args={[width, height]} />
		<T.MeshBasicMaterial
			map={texture}
			transparent
			depthWrite={false}
			toneMapped={false}
		/>
	</T.Mesh>
{/if}
