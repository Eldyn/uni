<!-- Threlte scene root: camera + lights + the playmat, the opponent ring, the
     local seat and hand, and the piles. Everything that used to be DOM —
     opponents, piles, the local player's own hand, and now the playmat and
     turn-direction arrows too — lives here, viewed from a straight top-down
     orthographic camera (no perspective). -->
<script lang="ts">
	import { onMount } from "svelte";
	import { T } from "@threlte/core";
	import { interactivity, useInteractivity } from "@threlte/extras";
	import type { OrthographicCamera } from "three";
	import { storeGame, Action, type GamePlayer } from "$stores/game.svelte";
	import { storeSpectator } from "$stores/spectator.svelte";
	import { storeRenderSettings } from "$stores/renderSettings.svelte";
	import { useCardBus } from "../card-bus.svelte";
	import type { SceneGeometry } from "../layout/sceneGeometry";
	import type { ViewportInfo } from "../layout/seatLayout";
	import { resolvePovPlayer } from "../layout/spectatorPov";
	import { storeTableSpin } from "$stores/tableSpin.svelte";
	import { buildSlotSequence, interpolateSlotPath } from "../layout/tableSpin";
	import type { SeatPosition3D } from "../layout/seatLayout3D";
	import Playmat3D from "./Playmat3D.svelte";
	import PlayerSeat3D from "./PlayerSeat3D.svelte";
	import LocalSeat3D from "./LocalSeat3D.svelte";
	import LocalHand3D from "./LocalHand3D.svelte";
	import DrawPile3D from "./DrawPile3D.svelte";
	import DiscardPile3D from "./DiscardPile3D.svelte";
	import AllCards3D from "./AllCards3D.svelte";
	import { preloadCardArt } from "./cardFaceAtlas";

	interactivity();
	const { raycaster } = useInteractivity();

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
		// Opponent avatars/labels are THREE.Sprite objects, and Sprite.raycast
		// throws ("Raycaster.camera needs to be set") when the shared raycaster
		// has no camera. Interactivity's default compute() only binds the camera
		// on events that pass its canvas-size guard, so a pointer event arriving
		// before its ResizeObserver has reported a non-zero size would raycast
		// the sprites with a null camera. Binding it here, as soon as the camera
		// exists, closes that window for every sprite in the scene.
		raycaster.camera = camRef;
	});

	function confirmTarget(username: string) {
		storeGame.submitInput(username);
	}

	// A click on an opponent is two things depending on who's watching: a
	// spectator switches the POV to that player (they then render in the local
	// seat), while a player in a ChooseTarget prompt confirms them as the card
	// effect's target. Same click, same seats — only the intent differs.
	function selectOpponent(username: string) {
		if (storeGame.isSpectator) {
			storeSpectator.setViewedUsername(username);
		} else {
			confirmTarget(username);
		}
	}

	// Whose POV the board shows: yourself normally, or — for a spectator — the
	// player being viewed (SpectatorBanner's fallback).
	// Their seat and hand render in the local slots, so the board reads as
	// "you are this player". Read-only is enforced on the hand itself. The same
	// resolver drives the animation layer (baseBeats), so the flight anchors and
	// the rendered row can never disagree about who "local" is.
	let povPlayer = $derived(
		resolvePovPlayer(
			storeGame.localPlayer,
			storeGame.isSpectator,
			storeGame.state?.players ?? [],
			storeSpectator.viewedUsername,
			storeGame.state?.current_turn
		)
	);

	let isLocalTurn = $derived(storeGame.state?.current_turn === povPlayer?.username);
	let localDimmed = $derived(DIM_LOCAL_WHEN_NOT_TURN && !isLocalTurn);

	// Spectator table spin, first half: every player is drawn as a seat that
	// slides along the ring's slot sequence from its old slot to its new one.
	// Slot 0 is the outgoing POV player (the bottom seat before the spin) —
	// `resolvePovPlayer` already reports the INCOMING player the moment the
	// spectator clicks, so the outgoing one has to be looked up by name from
	// `transition.from` rather than reusing `povPlayer`. The opponents come from
	// `mappedOpponents`, which is still derived from the outgoing `renderPov`
	// and so already carries the incoming player in their old ring position.
	// Null outside the spin phase, which restores the normal composition.
	const bottomPose = $derived({ x: 0, z: geometry.placement.localAvatarZ, rotationY: 0 });
	// The outgoing POV player. `resolvePovPlayer` already reports the INCOMING
	// player the moment the spectator clicks, so the outgoing one is looked up
	// by name from `transition.from`. Only non-null during phase "spin".
	const spinFromPlayer = $derived.by(() => {
		const t = storeTableSpin.transition;
		if (storeTableSpin.phase !== "spin" || !t) return null;
		return (storeGame.state?.players ?? []).find((p) => p.username === t.from) ?? null;
	});
	const spinSeatViews = $derived.by(() => {
		const t = storeTableSpin.transition;
		if (storeTableSpin.phase !== "spin" || !t) return null;
		const slots = buildSlotSequence(geometry.seats3D, bottomPose);
		const n = slots.length;
		const poseAt = (oldIndex: number) =>
			interpolateSlotPath(
				slots[oldIndex],
				slots[(oldIndex - t.steps + n) % n],
				storeTableSpin.spinProgress
			);
		const views: { player: GamePlayer; pose: SeatPosition3D }[] = [];
		if (spinFromPlayer) views.push({ player: spinFromPlayer, pose: poseAt(0) });
		mappedOpponents.forEach((o, i) => views.push({ player: o.player, pose: poseAt(i + 1) }));
		return views;
	});
	// The hand row's rendered player: the outgoing POV through phase 1 (its
	// cards stay in the bottom row while the ring spins), then the incoming POV
	// at commit. Hoisted above the spin/normal branch so the row never unmounts
	// on a POV change — that unmount was what stranded the outgoing cards'
	// registry entries.
	const handPlayer = $derived(storeTableSpin.phase === "spin" ? spinFromPlayer : povPlayer);
	const handDimmed = $derived(
		DIM_LOCAL_WHEN_NOT_TURN && storeGame.state?.current_turn !== handPlayer?.username
	);

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

<Playmat3D mat={placement.mat} showFelt={true} {viewport} />

{#if spinSeatViews}
	{#each spinSeatViews as view (view.player.username)}
		<PlayerSeat3D
			player={view.player}
			seat={view.pose}
			hasHoldingCard={bus.isHoldingOpponent(view.player.username)}
			isTurn={storeGame.state?.current_turn === view.player.username}
			isValidTarget={storeGame.actionRequired === Action.ChooseTarget &&
				Array.isArray(storeGame.actionContext) &&
				storeGame.actionContext.includes(view.player.username)}
			color={colorFor(view.player.username)}
			isViewable={storeGame.isSpectator}
			onSelect={() => selectOpponent(view.player.username)}
			cardScale={opponentCardScale}
			avatarPx={opponentAvatarPx}
			avatarWorld={opponentAvatarWorld}
			labelEm={opponentLabelEm}
			ringCardsHidden={view.player.username === spinFromPlayer?.username}
		/>
	{/each}
{:else}
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
				isViewable={storeGame.isSpectator}
				onSelect={() => selectOpponent(player.username)}
				cardScale={opponentCardScale}
				avatarPx={opponentAvatarPx}
				avatarWorld={opponentAvatarWorld}
				labelEm={opponentLabelEm}
				ringMorph={storeTableSpin.transition?.outgoing?.username === player.username
					? storeTableSpin.transition.outgoing
					: null}
				inheritProgress={storeTableSpin.inheritProgress}
			/>
		{/if}
	{/each}

	{#if povPlayer}
		<LocalSeat3D
			player={povPlayer}
			color={colorFor(povPlayer.username)}
			{placement}
			avatarPx={localAvatarPx}
			dimmed={localDimmed}
		/>
	{/if}
{/if}

{#if handPlayer}
	<LocalHand3D
		{rig}
		{viewport}
		{placement}
		dimmed={handDimmed}
		player={handPlayer}
		readOnly={storeGame.isSpectator}
		selectedId={storeGame.isSpectator ? null : selectedId}
		{onSelectionChange}
		{onPlay}
		{focusedId}
		{onPointerHover}
		handMorph={storeTableSpin.transition?.incoming?.username === handPlayer.username
			? storeTableSpin.transition.incoming
			: null}
		inheritProgress={storeTableSpin.inheritProgress}
	/>
{/if}

<DrawPile3D {placement} rotationY={placement.drawPileBesideHand ? 0 : storeTableSpin.pileAngle} />
<!-- The discard's own cards are all laid out around their pile's center, so the
     pile moves as a group rather than every jitter/shadow offset having to
     carry the placement's Z itself. -->
<T.Group
	position.x={placement.discardX}
	position.z={placement.discardZ}
	rotation.y={storeTableSpin.pileAngle}
>
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
