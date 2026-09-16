<!-- Threlte scene root: camera + lights + the playmat, the opponent ring, the
     local seat and hand, and the piles. Everything that used to be DOM —
     opponents, piles, the local player's own hand, and now the playmat and
     turn-direction arrows too — lives here, viewed from a straight top-down
     orthographic camera (no perspective). -->
<script lang="ts">
	import { onMount } from "svelte";
	import { T } from "@threlte/core";
	import { interactivity } from "@threlte/extras";
	import type { OrthographicCamera } from "three";
	import { storeGame, Action, type GamePlayer } from "$stores/game.svelte";
	import { storeRenderSettings } from "$stores/renderSettings.svelte";
	import { useCardBus } from "../card-bus.svelte";
	import type { SceneGeometry } from "../layout/sceneGeometry";
	import type { ViewportInfo } from "../layout/seatLayout";
	import Playmat3D from "./Playmat3D.svelte";
	import PlayerSeat3D from "./PlayerSeat3D.svelte";
	import LocalSeat3D from "./LocalSeat3D.svelte";
	import LocalHand3D from "./LocalHand3D.svelte";
	import DrawPile3D from "./DrawPile3D.svelte";
	import DiscardPile3D from "./DiscardPile3D.svelte";
	import AllCards3D from "./AllCards3D.svelte";
	import { preloadCardArt } from "./cardFaceAtlas";

	interactivity();

	// Whether the local player's own avatar/hand also dim outside their turn,
	// the same way every opponent seat now does — kept as a single flip so the
	// two variants can be screenshot and compared before picking one for real.
	const DIM_LOCAL_WHEN_NOT_TURN = true;

	let {
		mappedOpponents,
		viewport,
		geometry,
		colorFor,
		selectedId,
		onSelectionChange,
		onPlay,
		focusedId = null,
		onPointerHover
	}: {
		mappedOpponents: { player: GamePlayer }[];
		viewport: ViewportInfo;
		/** Everything geometric — camera, seats, sizing, pile placement — computed
		 *  once by GameBoard.svelte's computeSceneGeometry() call and threaded down
		 *  here, so this component only composes children instead of also owning
		 *  a second, independently-derived copy of the same math (see
		 *  layout/sceneGeometry.ts's file doc for why that used to be a bug). */
		geometry: SceneGeometry;
		colorFor: (username: string | undefined) => string;
		/** Owned by GameBoard, not here — AccessibleHandControls sits outside the
		 *  canvas entirely, so the touch-pick/keyboard-select state that both the
		 *  hand and the discard pile's confirm target need to agree on has to live
		 *  above both of them. */
		selectedId: number | null;
		onSelectionChange: (cardId: number | null) => void;
		onPlay: (cardId: number) => void;
		/** The keyboard-focused card, from AccessibleHandControls. */
		focusedId?: number | null;
		/** A real pointer entered the hand — tells GameBoard to drop keyboard
		 *  focus so the two inputs can't both light up a card at once. */
		onPointerHover?: () => void;
	} = $props();

	const bus = useCardBus();

	let rig = $derived(geometry.rig);
	let seats3D = $derived(geometry.seats3D);
	let opponentCardScale = $derived(geometry.opponentCardScale);
	let opponentAvatarPx = $derived(geometry.opponentAvatarPx);
	let opponentAvatarWorld = $derived(geometry.opponentAvatarWorld);
	let localAvatarPx = $derived(geometry.localAvatarPx);
	let opponentLabelEm = $derived(geometry.opponentLabelEm);
	let placement = $derived(geometry.placement);

	let camRef = $state<OrthographicCamera>();
	$effect(() => {
		if (!camRef) return;
		camRef.lookAt(rig.lookAt[0], rig.lookAt[1], rig.lookAt[2]);
		camRef.updateProjectionMatrix();
	});

	function confirmTarget(username: string) {
		storeGame.submitInput(username);
	}

	let isLocalTurn = $derived(storeGame.state?.current_turn === storeGame.localPlayer?.username);
	let localDimmed = $derived(DIM_LOCAL_WHEN_NOT_TURN && !isLocalTurn);

	let artLoaded = $state(false);

	onMount(() => {
		let cancelled = false;
		preloadCardArt().then(() => {
			if (!cancelled) artLoaded = true;
		});
		return () => {
			cancelled = true;
		};
	});
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

<Playmat3D mat={placement.mat} showFelt={viewport.orientation !== "portrait"} />

{#each mappedOpponents as { player }, i (player.username)}
	{@const seat = seats3D[i]}
	{#if seat}
		<PlayerSeat3D
			{player}
			{seat}
			hasHoldingCard={bus.isHoldingOpponent(player.username)}
			isTurn={storeGame.state?.current_turn === player.username}
			isValidTarget={storeGame.actionRequired === Action.ChooseTarget &&
				Array.isArray(storeGame.actionContext) &&
				storeGame.actionContext.includes(player.username)}
			color={colorFor(player.username)}
			onSelect={() => confirmTarget(player.username)}
			cardScale={opponentCardScale}
			avatarPx={opponentAvatarPx}
			avatarWorld={opponentAvatarWorld}
			labelEm={opponentLabelEm}
		/>
	{/if}
{/each}

{#if storeGame.localPlayer}
	<LocalSeat3D
		player={storeGame.localPlayer}
		color={colorFor(storeGame.localPlayer.username)}
		{placement}
		avatarPx={localAvatarPx}
		dimmed={localDimmed}
	/>
	<LocalHand3D
		{rig}
		{viewport}
		{placement}
		dimmed={localDimmed}
		{selectedId}
		{onSelectionChange}
		{onPlay}
		{focusedId}
		{onPointerHover}
	/>
{/if}

<DrawPile3D {placement} />
<!-- The discard's own cards are all laid out around their pile's center, so the
     pile moves as a group rather than every jitter/shadow offset having to
     carry the placement's Z itself. -->
<T.Group position.x={placement.discardX} position.z={placement.discardZ}>
	<DiscardPile3D
		history={bus.discardHistory}
		{placement}
		armed={selectedId !== null}
		onConfirm={() => selectedId !== null && onPlay(selectedId)}
	/>
</T.Group>

{#if artLoaded}
	<AllCards3D />
{/if}
