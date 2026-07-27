<!-- A colored card-shaped halo, drawn just under the card it marks so only a
     rim of it shows past the card's own edges. Used for the two halves of the
     play gesture: the card you picked, and the discard pile you tap to confirm
     it. Reuses the card background texture purely as a silhouette (rounded
     corners included), bleached to white first so the tint survives — see
     loadSilhouette in textures.ts. -->
<script lang="ts">
	import { T, useTask } from "@threlte/core";
	import { loadSilhouette } from "./textures";
	import { CARD_WIDTH, CARD_HEIGHT } from "./units";

	let {
		position,
		spinDeg = 0,
		scale = 1,
		color = "#ffe27a",
		pulse = false
	}: {
		position: [number, number, number];
		spinDeg?: number;
		scale?: number;
		color?: string;
		/** Breathes the halo's opacity — reserved for the "now tap here" target,
		 *  so only one thing on the board is ever asking to be touched. */
		pulse?: boolean;
	} = $props();

	/** How far the halo reaches past the card's own edge. */
	const RIM_GROWTH = 0.14;
	const BASE_OPACITY = 0.95;
	const PULSE_DEPTH = 0.35;
	const PULSE_SPEED = 3.4;

	let elapsed = $state(0);
	useTask((delta) => {
		if (!pulse) return;
		elapsed += delta;
	});

	let opacity = $derived(
		pulse ? BASE_OPACITY - PULSE_DEPTH * (0.5 - Math.cos(elapsed * PULSE_SPEED) / 2) : BASE_OPACITY
	);

	let texture = $state<import("three").Texture | null>(null);
	$effect(() => {
		let cancelled = false;
		loadSilhouette("/assets/cards/background.png").then((t) => {
			if (!cancelled) texture = t;
		});
		return () => {
			cancelled = true;
		};
	});
</script>

{#if texture}
	<T.Mesh
		{position}
		rotation.x={-Math.PI / 2}
		rotation.z={(spinDeg * Math.PI) / 180}
		scale={scale * (1 + RIM_GROWTH)}
	>
		<T.PlaneGeometry args={[CARD_WIDTH, CARD_HEIGHT]} />
		<T.MeshBasicMaterial
			map={texture}
			{color}
			transparent
			{opacity}
			depthWrite={false}
			toneMapped={false}
		/>
	</T.Mesh>
{/if}
