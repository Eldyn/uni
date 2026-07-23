<!-- Threlte scene root: camera + lights + the opponent ring, the local seat
     and hand, and the piles. The playmat and turn-direction arrows stay the
     existing DOM sprites in GameScreen.svelte (already correctly tinted/
     positioned there); everything that used to be DOM cards — opponents,
     piles, and now the local player's own hand — lives here instead, viewed
     from a straight top-down orthographic camera (no perspective). -->
<script lang="ts">
	import { T } from "@threlte/core";
	import { interactivity } from "@threlte/extras";
	import type { OrthographicCamera } from "three";
	import { storeGame, Action, type GamePlayer } from "$stores/game.svelte";
	import { useCardBus } from "../card-bus.svelte";
	import { computeCameraRig } from "../layout/cameraRig";
	import { computeSeatPositions3D } from "../layout/seatLayout3D";
	import type { ViewportInfo } from "../layout/seatLayout";
	import PlayerSeat3D from "./PlayerSeat3D.svelte";
	import LocalSeat3D from "./LocalSeat3D.svelte";
	import LocalHand3D from "./LocalHand3D.svelte";
	import DrawPile3D from "./DrawPile3D.svelte";
	import DiscardPile3D from "./DiscardPile3D.svelte";

	interactivity();

	let {
		mappedOpponents,
		viewport,
		colorFor
	}: {
		mappedOpponents: { player: GamePlayer }[];
		viewport: ViewportInfo;
		colorFor: (username: string | undefined) => string;
	} = $props();

	const bus = useCardBus();

	let seats3D = $derived(computeSeatPositions3D(mappedOpponents.length, viewport));
	let rig = $derived(computeCameraRig(viewport, mappedOpponents.length));

	let camRef = $state<OrthographicCamera>();
	$effect(() => {
		if (!camRef) return;
		camRef.lookAt(rig.lookAt[0], rig.lookAt[1], rig.lookAt[2]);
		camRef.updateProjectionMatrix();
	});

	function confirmTarget(username: string) {
		storeGame.submitInput(username);
	}
</script>

<T.OrthographicCamera
	makeDefault
	manual
	bind:ref={camRef}
	position={rig.position}
	left={-rig.halfWidth}
	right={rig.halfWidth}
	top={rig.halfHeight}
	bottom={-rig.halfHeight}
	near={0.1}
	far={100}
/>

<T.AmbientLight intensity={1.1} />
<T.DirectionalLight intensity={0.4} position={[3, 6, 4]} />

{#each mappedOpponents as { player }, i (player.username)}
	{@const seat = seats3D[i]}
	{#if seat}
		<PlayerSeat3D
			{player}
			{seat}
			isTurn={storeGame.state?.current_turn === player.username}
			isValidTarget={storeGame.actionRequired === Action.ChooseTarget &&
				Array.isArray(storeGame.actionContext) &&
				storeGame.actionContext.includes(player.username)}
			color={colorFor(player.username)}
			onSelect={() => confirmTarget(player.username)}
		/>
	{/if}
{/each}

{#if storeGame.localPlayer}
	<LocalSeat3D player={storeGame.localPlayer} color={colorFor(storeGame.localPlayer.username)} />
	<LocalHand3D />
{/if}

<DrawPile3D />
<DiscardPile3D history={bus.discardHistory} />
