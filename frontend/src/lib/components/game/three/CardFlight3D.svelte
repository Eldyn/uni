<!-- The mesh a running GSAP tween actually drives. `pose` is the same $state
     object animationQueue.svelte.ts's stepRenderers write to every tick —
     because it's a Svelte $state object, GSAP's own per-frame property
     writes ARE the reactivity trigger; no polling loop needed here. -->
<script lang="ts">
	import type { FlightPose } from "../animation/renderContext";
	import CardMesh3D from "./CardMesh3D.svelte";

	let {
		card,
		pose
	}: {
		card: { type: string; value: string };
		pose: FlightPose;
	} = $props();
</script>

<CardMesh3D
	card={{ id: -1, type: card.type as never, value: card.value as never }}
	turned={pose.turned}
	position={[pose.x, pose.y, pose.z]}
	spinDeg={pose.spinDeg}
	scale={pose.scale}
	opacity={pose.opacity}
/>
