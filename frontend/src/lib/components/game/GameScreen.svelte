<script lang="ts">
	import GameBoard from "./GameBoard.svelte";
	import GameEndPopup from "./GameEndPopup.svelte";
	import GameHud from "./GameHud.svelte";
	import SpectatorBanner from "./SpectatorBanner.svelte";
	import EliminationOutcomeBanner from "./popup/EliminationOutcomeBanner.svelte";
	import PromptRenderer from "./prompts/PromptRenderer.svelte";
	import { storeGame } from "$stores/game.svelte";
	import { createGameLayoutContext } from "./game-layout-context.svelte";

	createGameLayoutContext();

	let matchEnded = $state(false);

	$effect(() => {
		if (storeGame.state?.is_over && !matchEnded) {
			matchEnded = true;
		} else if (!storeGame.state?.is_over) {
			matchEnded = false;
		}
	});
</script>

<div class="game-screen">
	<div class="ui-layer">
		<GameEndPopup />

		<div class="game-controls">
			<GameHud />
			<PromptRenderer />
		</div>

		<SpectatorBanner
			eliminated={storeGame.state?.mode === "elimination" && storeGame.eliminationOutcome !== null}
		/>

		<EliminationOutcomeBanner />

		<div class="game-board-container">
			<GameBoard />
		</div>
	</div>
</div>

<style>
	/* The playmat and the turn arrows are meshes inside the canvas now
	   (three/Playmat3D.svelte); the wood sprite that used to sit behind them is
	   gone, replaced by the flat table colour. Not var(--bg): the board's whole
	   job is to make four saturated card colours legible at a glance, and on a
	   light canvas they lose the contrast they're read by. */
	.game-screen {
		width: 100%;
		height: 100vh;
		background: var(--table);
		position: relative;
		overflow: hidden;
	}

	/* --- UI LAYOUT --- */
	.ui-layer {
		position: relative;
		z-index: 10; /* Keeps interactive UI safely above all backgrounds */
		height: 100%;
	}

	/* The HUD floats on top of the board instead of stacking above it — the
	   canvas must own the full screen or the scene's center drifts below the
	   true screen center by half the HUD row's height. Centering keeps the
	   HUD in the middle at every width; PromptRenderer renders only fixed
	   overlays, so it never shifts this row. */
	.game-controls {
		position: absolute;
		top: 0;
		left: 0;
		right: 0;
		z-index: 2;
		display: flex;
		justify-content: center;
		align-items: center;
		padding: 16px;
		pointer-events: none;
	}

	/* Re-enable clicks on the HUD's actual widgets; the row's empty middle
	   stays click-through so it never blocks the board underneath. */
	.game-controls > :global(*) {
		pointer-events: auto;
	}

	.game-board-container {
		position: absolute;
		inset: 0;
		z-index: 1;
	}
</style>
