<!-- The baked strip on a plane inside the stage Canvas. The Canvas draws on
     demand, and a texture offset change is not a prop change, so each frame
     step asks Threlte for a new render. -->
<script lang="ts">
	import { T, useThrelte } from "@threlte/core";
	import type { CanvasTexture } from "three";
	import { FRAME_COUNT } from "$lib/avatar/types";

	let { texture, frame }: { texture: CanvasTexture; frame: number } = $props();

	const { invalidate } = useThrelte();

	$effect(() => {
		texture.offset.x = frame / FRAME_COUNT;
		invalidate();
	});
</script>

<T.Mesh>
	<T.PlaneGeometry args={[1, 1]} />
	<T.MeshBasicMaterial
		map={texture}
		transparent
		alphaTest={0.05}
		depthWrite={false}
		toneMapped={false}
	/>
</T.Mesh>
