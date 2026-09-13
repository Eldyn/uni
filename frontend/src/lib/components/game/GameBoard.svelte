<script lang="ts">
	import { Canvas } from "@threlte/core";
	import { storeGame } from "$stores/game.svelte";
	import { playerColorFor } from "$lib/palette";
	import { createCardBus } from "./card-bus.svelte";
	import { createAnimationQueue } from "./animation/animationQueue.svelte";
	import { createBaseBeatsWatcher } from "./animation/baseBeats.svelte";
	import { createGameLayoutContext } from "./game-layout-context.svelte";
	import Scene3D from "./three/Scene3D.svelte";
	import DrawStackIndicator from "./DrawStackIndicator.svelte";
	import AccessibleHandControls from "./AccessibleHandControls.svelte";
	import { computeSceneGeometry } from "./layout/sceneGeometry";
	import { devFixturePreset } from "../../dev/devFixturePreset.svelte";

	const bus = createCardBus();
	const animationQueue = createAnimationQueue();
	const layout = createGameLayoutContext();

	// On touch, playing is a two-step gesture: pick a card in the hand, then tap
	// the discard pile to commit it; on keyboard, Enter does the same two steps.
	// The selection lives here, above both Scene3D (the hand + pile) and
	// AccessibleHandControls (the keyboard path), because all three need to
	// agree on what's picked. Hover devices skip it entirely and play on click,
	// so the selection simply stays null there.
	let selectedCardId = $state<number | null>(devFixturePreset.selectId);
	// The keyboard-focused card, from AccessibleHandControls — threaded down
	// into the 3D hand so Tab/Arrow navigation lifts/highlights a card the same
	// way a mouse hover does.
	let keyboardFocusId = $state<number | null>(null);

	// Nothing stays picked across a turn boundary; coming back to your turn with a
	// stale card already armed is how you play a card you never meant to.
	$effect(() => {
		if (storeGame.state?.current_turn !== storeGame.localPlayer?.username) selectedCardId = null;
	});

	function play(cardId: number) {
		if (storeGame.isActionPending) return;
		storeGame.playCard(cardId);
		selectedCardId = null;
	}

	// The camera's aspect ratio must match the canvas's actual rendered box,
	// not the raw window — GameScreen.svelte's HUD row shrinks
	// .game-board-container below window.innerHeight, and the mismatch
	// otherwise clips content (the local hand) at the true bottom edge.
	let sceneWidth = $state(0);
	let sceneHeight = $state(0);
	let sceneViewport = $derived({
		width: sceneWidth || layout.viewport.width,
		height: sceneHeight || layout.viewport.height,
		orientation: layout.viewport.orientation
	});

	// Seat-position color, not a UNO card color: cycles through the 4 game
	// colors regardless of player count. Dropped in favor of a real
	// player-picked character color in a future pass.
	function colorFor(username: string | undefined): string {
		const idx = storeGame.state?.players?.findIndex((p) => p.username === username) ?? -1;
		return playerColorFor(idx);
	}

	// Rotate the player list so opponents read in turn order starting right
	// after the local player, then hand that list + the current viewport to
	// the Threlte scene's own seat solver (layout/seatLayout3D.ts).
	let mappedOpponents = $derived.by(() => {
		const players = storeGame.state?.players ?? [];
		const myUsername = storeGame.localPlayer?.username;
		if (!myUsername || players.length <= 1) return [];

		const rawOpponents = players.filter((p) => p.username !== myUsername);
		const myIdx = players.findIndex((p) => p.username === myUsername);
		const rotated =
			myIdx === -1 ? rawOpponents : [...players.slice(myIdx + 1), ...players.slice(0, myIdx)];

		return rotated.map((player) => ({ player }));
	});

	// Single source of truth for the scene's camera/seat/pile geometry — Scene3D
	// draws from this same object (passed down as a prop below), so the pile
	// anchors below and the actual WebGL scene can never disagree about where
	// the piles really sit (see layout/sceneGeometry.ts's file doc).
	let geometry = $derived(computeSceneGeometry(sceneViewport, mappedOpponents.length));

	const disposeBaseBeatsWatcher = createBaseBeatsWatcher({
		bus,
		animationQueue,
		getPlacement: () => geometry.placement,
		getOpponentCardScale: () => geometry.opponentCardScale,
		getOpponentSeatAnchor: (username) => {
			const idx = mappedOpponents.findIndex((o) => o.player.username === username);
			if (idx === -1) {
				console.warn(`GameBoard: no seat found for opponent "${username}" — falling back to discard pile.`);
				return [geometry.placement.discardX, 0, geometry.placement.discardZ];
			}
			const seat = geometry.seats3D[idx];
			return seat ? [seat.x, 0, seat.z] : [0, 0, 0];
		},
		getOpponentSeatRotationDeg: (username) => {
			const idx = mappedOpponents.findIndex((o) => o.player.username === username);
			const seat = idx === -1 ? undefined : geometry.seats3D[idx];
			return seat ? (seat.rotationY * 180) / Math.PI : 0;
		}
	});

	$effect(() => disposeBaseBeatsWatcher);

	// The "skippable queue" had no reachable UI trigger — Escape is free
	// (AccessibleHandControls claims arrows/Enter/Space/digits, never Escape).
	$effect(() => {
		function onWindowKeydown(event: KeyboardEvent) {
			if (event.key === "Escape") animationQueue.skipCurrent();
		}
		window.addEventListener("keydown", onWindowKeydown);
		return () => window.removeEventListener("keydown", onWindowKeydown);
	});

	// A backgrounded tab still receives state updates (and so still queues
	// beats) while nobody is watching. Flush on both transitions: hiding
	// drains what's already queued, and returning drains anything that
	// queued up *after* that first flush while still backgrounded — without
	// the second flush, that backlog plays through the moment focus returns.
	$effect(() => {
		function onVisibilityChange() {
			animationQueue.flushImmediately();
		}
		document.addEventListener("visibilitychange", onVisibilityChange);
		return () => document.removeEventListener("visibilitychange", onVisibilityChange);
	});
</script>

<DrawStackIndicator />
<AccessibleHandControls
	selectedId={selectedCardId}
	onSelectionChange={(id) => (selectedCardId = id)}
	onPlay={play}
	focusedId={keyboardFocusId}
	onFocusChange={(id) => (keyboardFocusId = id)}
/>

<div class="game-field" class:portrait={layout.viewport.orientation === "portrait"}>
	<div
		class="scene-layer"
		bind:clientWidth={sceneWidth}
		bind:clientHeight={sceneHeight}
		onpointerdown={() => animationQueue.skipCurrent()}
	>
		<Canvas>
			<Scene3D
				{mappedOpponents}
				viewport={sceneViewport}
				{geometry}
				{colorFor}
				selectedId={selectedCardId}
				onSelectionChange={(id) => (selectedCardId = id)}
				onPlay={play}
				focusedId={keyboardFocusId}
				onPointerHover={() => (keyboardFocusId = null)}
			/>
		</Canvas>
	</div>
</div>

<style>
	:root {
		--cardSize: 5em;
		--shadowColor: rgba(0, 0, 0, 0.16);
	}

	/* Shrink cards on narrow/short viewports so the ring/rails and hand never
	   outgrow the screen; seatLayout.ts + PlayerHand's overlap step already
	   handle count-based fitting, this handles viewport-based fitting. */
	@media (max-width: 640px), (max-height: 480px) {
		:root {
			--cardSize: 3.2em;
		}
	}

	.game-field {
		position: relative;
		z-index: 2;
		width: 100%;
		height: 100%;
		-webkit-user-select: none;
		user-select: none;
	}

	.scene-layer {
		position: absolute;
		inset: 0;
	}

	.scene-layer :global(canvas) {
		width: 100%;
		height: 100%;
		display: block;
	}
</style>
