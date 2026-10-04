<!-- Threlte scene root: camera + lights + the playmat, the opponent ring, the
     local seat and hand, and the piles. Everything that used to be DOM —
     opponents, piles, the local player's own hand, and now the playmat and
     turn-direction loop too — lives here, viewed from a tilted perspective
     camera (see cameraRig.ts). Every piece of table furniture sits under one
     rotating group so a spectator spin turns the whole board as a single
     object; the local hand row is the one thing outside it. -->
<script lang="ts">
	import { onMount, tick } from "svelte";
	import { T, useThrelte } from "@threlte/core";
	import { interactivity, useInteractivity } from "@threlte/extras";
	import type { PerspectiveCamera } from "three";
	import { storeGame, type GamePlayer } from "$stores/game.svelte";
	import { storeSpectator } from "$stores/spectator.svelte";
	import { storeRenderSettings } from "$stores/renderSettings.svelte";
	import { useCardBus } from "../card-bus.svelte";
	import type { SceneGeometry } from "../layout/sceneGeometry";
	import type { ViewportInfo } from "../layout/seatLayout";
	import { resolvePovPlayer } from "../layout/spectatorPov";
	import { storeTableSpin } from "$stores/tableSpin.svelte";
	import { storeTurnSkip } from "$stores/turnSkip.svelte";
	import { storeBoardCamera } from "$stores/boardCamera.svelte";
	import { storeCameraOffset } from "$stores/cameraOffset.svelte";
	import { offsetCameraPosition } from "../layout/cameraRig";
	import { isChoosePlayerTarget } from "../prompts/promptTargets";
	import Playmat3D from "./Playmat3D.svelte";
	import AmbientDust3D from "./AmbientDust3D.svelte";
	import PlayerSeat3D from "./PlayerSeat3D.svelte";
	import LocalSeat3D from "./LocalSeat3D.svelte";
	import LocalHand3D from "./LocalHand3D.svelte";
	import DrawPile3D from "./DrawPile3D.svelte";
	import DiscardPile3D from "./DiscardPile3D.svelte";
	import AllCards3D from "./AllCards3D.svelte";
	import { preloadCardArt } from "./cardFaceAtlas";
	import { warmRenderer } from "./rendererWarmup";

	interactivity();
	const { raycaster } = useInteractivity();
	const { scene, camera, renderer } = useThrelte();

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

	// The perspective camera's aspect MUST track the canvas's real box, or the
	// projection stretches: a `T.PerspectiveCamera` defaults `aspect` to 1, which
	// on a 16:9 board skews the whole scene into a trapezoid and reads as a huge
	// unintended camera tilt. The orthographic camera this replaced never needed
	// this (its `left/right/top/bottom` already encoded the aspect), so switching
	// to perspective has to wire it up explicitly.
	let aspect = $derived(viewport.width / viewport.height);

	// The landing impact's punch rides on top of the rig's base position. It
	// only moves along the view axis, so the lookAt below stays valid.
	let cameraPosition = $derived(offsetCameraPosition(rig, storeCameraOffset.offset));

	let camRef = $state<PerspectiveCamera>();
	$effect(() => {
		if (!camRef) return;
		camRef.lookAt(rig.lookAt[0], rig.lookAt[1], rig.lookAt[2]);
		camRef.updateProjectionMatrix();
		// Published for the DOM overlays outside the canvas that have to project
		// world positions to screen space (ActionPlayDrawn, DrawStackIndicator).
		storeBoardCamera.camera = camRef;
		// Opponent avatars/labels are THREE.Sprite objects, and Sprite.raycast
		// throws ("Raycaster.camera needs to be set") when the shared raycaster
		// has no camera. Interactivity's default compute() only binds the camera
		// on events that pass its canvas-size guard, so a pointer event arriving
		// before its ResizeObserver has reported a non-zero size would raycast
		// the sprites with a null camera. Binding it here, as soon as the camera
		// exists, closes that window for every sprite in the scene.
		raycaster.camera = camRef;
		return () => {
			if (storeBoardCamera.camera === camRef) storeBoardCamera.camera = null;
		};
	});

	function confirmTarget(username: string) {
		const prompt = storeGame.activePrompt;
		if (prompt?.kind !== "choose_player") return;
		storeGame.respondToPrompt(prompt.prompt_id, username);
	}

	// A click on an opponent is two things depending on who's watching: a
	// spectator switches the POV to that player (they then render in the local
	// seat), while a player in a ChooseTarget prompt confirms them as the card
	// effect's target. Same click, same seats — only the intent differs.
	//
	// With click-to-play off, or on any touch press, a target needs a second
	// tap on the same seat to confirm, so a stray finger never picks a player.
	let armedTarget = $state<string | null>(null);
	let armedSeat = $derived(
		armedTarget !== null && isChoosePlayerTarget(armedTarget) ? armedTarget : null
	);

	function selectOpponent(username: string, pointerType: string) {
		if (storeGame.isSpectator) {
			storeSpectator.setViewedUsername(username);
			return;
		}
		const needsConfirm = !storeRenderSettings.clickToPlay || pointerType === "touch";
		if (needsConfirm && armedSeat !== username) {
			armedTarget = username;
			return;
		}
		armedTarget = null;
		confirmTarget(username);
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

	// INFO: while a skip presentation is in flight, the board keeps showing the
	//       outgoing player as current so the X reads before the turn moves on
	//       (storeTurnSkip is driven by the turn_advance beat's `skipped` list).
	let presentedTurn = $derived(storeTurnSkip.presentingTurn ?? storeGame.state?.current_turn);
	let isLocalTurn = $derived(presentedTurn === povPlayer?.username);
	let localDimmed = $derived(DIM_LOCAL_WHEN_NOT_TURN && !isLocalTurn);

	// Spectator table spin, first half: the WHOLE TABLE turns as one rigid
	// group about the mat's center. There is no per-seat orbital interpolation
	// any more — the seats, the mat, the loop and both piles all live under
	// `boardGroup`, so a single group rotation carries every one of them and
	// they can never disagree about where the table is.
	//
	// The outgoing POV player is looked up by name from `transition.from`
	// (rather than reusing `povPlayer`, which already reports the INCOMING
	// player the moment the spectator clicks). Only non-null during phase
	// "spin", when its bottom-row cards still need to be hidden from the ring.
	const spinFromPlayer = $derived.by(() => {
		const t = storeTableSpin.transition;
		if (storeTableSpin.phase !== "spin" || !t) return null;
		return (storeGame.state?.players ?? []).find((p) => p.username === t.from) ?? null;
	});
	// The hand row's rendered player: the outgoing POV through phase 1 (its
	// cards stay in the bottom row while the table turns), then the incoming
	// POV at commit. Hoisted above the branch so the row never unmounts on a
	// POV change — that unmount was what stranded the outgoing cards' registry
	// entries.
	const handPlayer = $derived(storeTableSpin.phase === "spin" ? spinFromPlayer : povPlayer);
	const handDimmed = $derived(DIM_LOCAL_WHEN_NOT_TURN && presentedTurn !== handPlayer?.username);

	let artLoaded = $state(false);

	onMount(() => {
		let cancelled = false;

		async function prepare(): Promise<void> {
			// 1. Decode the card art and bake the atlas (async, network + canvas).
			try {
				await preloadCardArt();
			} catch (err) {
				console.error("Scene3D: card art preload failed", err);
			}
			if (cancelled) return;
			artLoaded = true;

			// 2. Require the board's own meshes (gated on `artLoaded`) to mount so
			//    the scene graph handed to the warm-up is complete.
			await tick();
			if (cancelled) return;

			// 3. Compile the shaders and upload the atlas NOW, behind the loader,
			//    instead of on the deal's opening frames. `sendClientReady` — which
			//    releases the ready barrier and lets the match begin — waits for it.
			await warmRenderer(renderer, scene, camera.current);
			if (cancelled) return;
			storeGame.sendClientReady();
		}

		void prepare();
		return () => {
			cancelled = true;
		};
	});
</script>

<T.PerspectiveCamera
	makeDefault
	manual
	bind:ref={camRef}
	position={cameraPosition}
	fov={rig.fov}
	{aspect}
	near={rig.near}
	far={rig.far}
/>

<T.AmbientLight intensity={1.1} />
<T.DirectionalLight intensity={0.4} position={[3, 6, 4]} />

<!-- The whole table: mat, direction loop, both center piles, every seat (the
     outgoing POV player included, drawn at the bottom pivot) and the local
     avatar. One yaw drives all of it — see boardRotation.ts. The local HAND
     row is deliberately outside this group: it is the viewer's own UI, not
     table furniture, and always stays upright at the bottom of the screen. -->
<T.Group rotation.y={storeTableSpin.boardRotationY}>
	<Playmat3D mat={placement.mat} showFelt={true} {viewport} />

	{#if storeRenderSettings.ambientDustActive}
		<AmbientDust3D mat={placement.mat} {viewport} />
	{/if}

	{#each mappedOpponents as { player }, i (player.username)}
		{@const seat = seats3D[i]}
		{#if seat}
			<PlayerSeat3D
				{player}
				{seat}
				hasHoldingCard={bus.isHoldingOpponent(player.username)}
				isTurn={presentedTurn === player.username}
				isValidTarget={isChoosePlayerTarget(player.username)}
				isArmed={armedSeat === player.username}
				color={colorFor(player.username)}
				isViewable={storeGame.isSpectator}
				onSelect={(pointerType) => selectOpponent(player.username, pointerType)}
				cardScale={opponentCardScale}
				avatarPx={opponentAvatarPx}
				avatarWorld={opponentAvatarWorld}
				labelEm={opponentLabelEm}
				{rig}
				{viewport}
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
			isTurn={isLocalTurn}
		/>
	{/if}

	<DrawPile3D {placement} />
	<!-- The discard's own cards are all laid out around their pile's center, so
	     the pile moves as a group rather than every jitter/shadow offset having
	     to carry the placement's Z itself. -->
	<T.Group position.x={placement.discardX} position.z={placement.discardZ}>
		<DiscardPile3D
			history={bus.discardHistory}
			{placement}
			armed={selectedId !== null}
			onConfirm={() => selectedId !== null && onPlay(selectedId)}
		/>
	</T.Group>
</T.Group>

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

{#if artLoaded}
	<AllCards3D />
{/if}
