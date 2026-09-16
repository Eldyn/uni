<!-- The one {#each} that mounts CardMesh3D for every card in the game, idle
     or mid-transition alike — real entries (hand + discard cards) never
     disappear from CardRegistry.activeFlights once created, so there is only
     ever one render site per card id. Replaces CardFlight3D.svelte, whose one
     job (draw a CardMesh3D from a live pose) this subsumes for every entry. -->
<script lang="ts">
	import { useTask } from "@threlte/core";
	import { useCardRegistry } from "../animation/cardRegistry.svelte";
	import CardMesh3D from "./CardMesh3D.svelte";
	import { loadSilhouette } from "./textures";

	const cardRegistry = useCardRegistry();

	useTask((delta) => {
		cardRegistry.tick(delta);
	});

	let shadowTexture = $state<import("three").Texture | null>(null);
	$effect(() => {
		let cancelled = false;
		loadSilhouette("/assets/cards/background.png").then((t) => {
			if (!cancelled) shadowTexture = t;
		});
		return () => {
			cancelled = true;
		};
	});

	const FLIGHT_SHADOW_OFFSET = 0.08;
	const FLIGHT_SHADOW_DROP_Z = 0.05;
	const FLIGHT_SHADOW_OPACITY = 0.35;
</script>

{#each cardRegistry.activeFlights as handle (handle.id)}
	<CardMesh3D
		card={{ id: -1, type: handle.card.type as never, value: handle.card.value as never }}
		wildColor={handle.card.wildColor}
		turned={handle.pose.turned}
		position={[handle.pose.x, handle.pose.y, handle.pose.z]}
		spinDeg={handle.pose.spinDeg}
		flipDeg={handle.pose.flipDeg}
		flipAxis={handle.pose.flipAxis}
		scale={handle.pose.scale}
		opacity={handle.decoration?.opacity ?? handle.pose.opacity}
		hovered={handle.decoration?.hovered}
		instant={handle.decoration?.instant}
		hoverPush={handle.decoration?.hoverPush}
		liftT={handle.pose.liftT}
		dragT={handle.pose.dragT ?? 0}
		pushX={handle.pose.pushX}
		hoverSpinDeg={(handle.decoration?.hoverSpinDeg ?? 0) + (handle.pose.hoverSpinDeg ?? 0)}
		dimmed={handle.decoration?.dimmed}
		shadow={handle.decoration?.shadow ??
			(shadowTexture
				? {
						texture: shadowTexture,
						offsetX: FLIGHT_SHADOW_OFFSET,
						dropZ: FLIGHT_SHADOW_DROP_Z,
						opacity: FLIGHT_SHADOW_OPACITY
					}
				: undefined)}
		highlight={handle.decoration?.highlight}
	/>
{/each}
