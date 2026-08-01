<script lang="ts">
	import { Canvas } from "@threlte/core";
	import { storeGame } from "$stores/game.svelte";
	import { playerColorFor } from "$lib/palette";
	import { createCardBus } from "./card-bus.svelte";
	import { createGameLayoutContext } from "./game-layout-context.svelte";
	import Scene3D from "./three/Scene3D.svelte";
	import FlyingCardsOverlay from "./FlyingCardsOverlay.svelte";
	import DrawStackIndicator from "./DrawStackIndicator.svelte";
	import AccessibleHandControls from "./AccessibleHandControls.svelte";
	import { computeSceneGeometry } from "./layout/sceneGeometry";
	import { worldToScreenPercent } from "./layout/screenProjection";

	const bus = createCardBus();
	const layout = createGameLayoutContext();

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

	// The piles are now real WebGL meshes (DrawPile3D/DiscardPile3D), but
	// FlyingCardsOverlay's 2D flight animations still resolve their source/
	// destination via card-bus DOM rects — these invisible anchors keep that
	// resolution accurate without rendering a second, redundant DOM pile.
	let discardAnchorEl = $state<HTMLElement | null>(null);
	let drawAnchorEl = $state<HTMLElement | null>(null);

	$effect(() => {
		if (discardAnchorEl) bus.register("discard-pile", discardAnchorEl);
		return () => bus.unregister("discard-pile");
	});

	$effect(() => {
		if (drawAnchorEl) bus.register("draw-pile", drawAnchorEl);
		return () => bus.unregister("draw-pile");
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

	// The discard pile sits at the mat's own world origin — projected here
	// instead of assumed, so this stays correct even if that ever stops being
	// true (see layout/screenProjection.ts's file doc).
	let discardAnchor = $derived(worldToScreenPercent(geometry.rig, 0, 0));
	let drawAnchor = $derived(
		worldToScreenPercent(geometry.rig, geometry.placement.drawPileX, geometry.placement.localSeatZ)
	);
	let discardAnchorStyle = $derived(
		`left: ${discardAnchor.leftPercent}%; top: ${discardAnchor.topPercent}%;`
	);
	let drawAnchorStyle = $derived(
		`left: ${drawAnchor.leftPercent}%; top: ${drawAnchor.topPercent}%;`
	);
</script>

<FlyingCardsOverlay />
<DrawStackIndicator />
<AccessibleHandControls />

<div class="game-field" class:portrait={layout.viewport.orientation === "portrait"}>
	<div class="scene-layer" bind:clientWidth={sceneWidth} bind:clientHeight={sceneHeight}>
		<Canvas>
			<Scene3D {mappedOpponents} viewport={sceneViewport} {geometry} {colorFor} />
		</Canvas>
	</div>

	<div class="pile-anchor" style={discardAnchorStyle} bind:this={discardAnchorEl}></div>
	<div class="pile-anchor" style={drawAnchorStyle} bind:this={drawAnchorEl}></div>
</div>

<style>
	:global(body) {
		margin: 0;
		padding: 0;
		overflow: hidden;
		background-color: transparent;
	}

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

	/* Zero-size, invisible — exists only so card-bus can resolve a screen
	   point for flight animations; the actual pile art is the WebGL mesh.
	   Placement is inline, projected from the pile's real world position. */
	.pile-anchor {
		position: absolute;
		width: 0;
		height: 0;
		pointer-events: none;
	}
</style>
