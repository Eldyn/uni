<!-- A single card in an opponent seat's hand arc. Smoothly tweens its position and
     spin angle when the hand count changes, so the arc organically fans out or
     contracts to close gaps when cards are played or drawn. -->
<script lang="ts">
	import { untrack } from "svelte";
	import { useTask } from "@threlte/core";
	import CardMesh3D from "./CardMesh3D.svelte";

	let {
		targetPosition,
		targetSpinDeg,
		scale,
		dimmed
	}: {
		targetPosition: [number, number, number];
		targetSpinDeg: number;
		scale: number;
		dimmed: boolean;
	} = $props();

	let currentX = $state(untrack(() => targetPosition[0]));
	let currentY = $state(untrack(() => targetPosition[1]));
	let currentZ = $state(untrack(() => targetPosition[2]));
	let currentSpinDeg = $state(untrack(() => targetSpinDeg));

	const LERP_SPEED = 12;

	useTask((delta) => {
		const factor = Math.min(1, delta * LERP_SPEED);
		currentX += (targetPosition[0] - currentX) * factor;
		currentY += (targetPosition[1] - currentY) * factor;
		currentZ += (targetPosition[2] - currentZ) * factor;

		const diff = ((targetSpinDeg - currentSpinDeg + 540) % 360) - 180;
		currentSpinDeg += diff * factor;
	});
</script>

<CardMesh3D
	card={{ id: -1, type: "wild", value: "0" }}
	turned={true}
	position={[currentX, currentY, currentZ]}
	spinDeg={currentSpinDeg}
	{scale}
	{dimmed}
/>
