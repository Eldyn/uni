<!-- Home screen's animated background: a flat #16171d canvas with dithered
     #2c354d ring-ripples propagating outward, one continuously looping from
     the UNI! logo, others spawning sparsely at random points along the
     screen edges. Only mounted when webglCapability.svelte.ts confirms real
     GPU acceleration and no reduced-motion preference; the static PNG +
     dither-halo is the fallback otherwise (see MainScreen.svelte).

     Just a thin <Canvas> wrapper — the mesh/shader/ripple-clock logic lives
     in RippleQuad.svelte because useTask requires a component nested INSIDE
     <Canvas>, not the component that renders <Canvas> itself. -->
<script lang="ts">
	import { Canvas } from "@threlte/core";
	import RippleQuad from "./RippleQuad.svelte";
</script>

<div class="shader-bg">
	<Canvas>
		<RippleQuad />
	</Canvas>
</div>

<style>
	.shader-bg {
		width: 100%;
		height: 100%;
	}
	.shader-bg :global(canvas) {
		width: 100%;
		height: 100%;
		display: block;
	}
</style>
