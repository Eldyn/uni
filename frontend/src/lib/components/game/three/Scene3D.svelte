<!-- Threlte scene root: camera + lights + the opponent ring + piles. The
     playmat and turn-direction arrows stay the existing DOM sprites in
     GameScreen.svelte (already correctly tinted/positioned there), so this
     scene only draws what previously lived in GameBoard.svelte's
     .seat-layer and .piles-wrapper — the local hand stays a DOM overlay
     too, unchanged, sitting below the transparent canvas. -->
<script lang="ts">
	import { T } from "@threlte/core";
	import { interactivity } from "@threlte/extras";
	import type { PerspectiveCamera } from "three";
	import { storeGame, Action, type GamePlayer } from "$stores/game.svelte";
	import { useCardBus } from "../card-bus.svelte";
	import { computeCameraRig } from "../layout/cameraRig";
	import { computeSeatPositions3D } from "../layout/seatLayout3D";
	import type { ViewportInfo } from "../layout/seatLayout";
	import PlayerSeat3D from "./PlayerSeat3D.svelte";
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

	let camRef = $state<PerspectiveCamera>();
	$effect(() => {
		camRef?.lookAt(rig.lookAt[0], rig.lookAt[1], rig.lookAt[2]);
	});

	function confirmTarget(username: string) {
		storeGame.submitInput(username);
	}
</script>

<T.PerspectiveCamera makeDefault bind:ref={camRef} position={rig.position} fov={rig.fov} />

<T.AmbientLight intensity={0.9} />
<T.DirectionalLight intensity={0.6} position={[3, 6, 4]} />

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

<DrawPile3D />
<DiscardPile3D history={bus.discardHistory} />
