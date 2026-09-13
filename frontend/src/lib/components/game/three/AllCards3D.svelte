<!-- The one {#each} that mounts CardMesh3D for every card in the game, idle
     or mid-transition alike — real entries (hand + discard cards) never
     disappear from CardRegistry.activeFlights once created, so there is only
     ever one render site per card id. Replaces CardFlight3D.svelte, whose one
     job (draw a CardMesh3D from a live pose) this subsumes for every entry. -->
<script lang="ts">
	import { useCardRegistry } from "../animation/cardRegistry.svelte";
	import CardMesh3D from "./CardMesh3D.svelte";

	const cardRegistry = useCardRegistry();
</script>

{#each cardRegistry.activeFlights as flight (flight.id)}
	<CardMesh3D
		card={{ id: -1, type: flight.card.type as never, value: flight.card.value as never }}
		turned={flight.pose.turned}
		position={[flight.pose.x, flight.pose.y, flight.pose.z]}
		spinDeg={flight.pose.spinDeg}
		scale={flight.pose.scale}
		opacity={flight.decoration?.opacity ?? flight.pose.opacity}
		hovered={flight.decoration?.hovered}
		instant={flight.decoration?.instant}
		hoverPush={flight.decoration?.hoverPush}
		pushX={flight.decoration?.pushX}
		hoverSpinDeg={flight.decoration?.hoverSpinDeg}
		dimmed={flight.decoration?.dimmed}
		shadow={flight.decoration?.shadow}
		highlight={flight.decoration?.highlight}
	/>
{/each}
