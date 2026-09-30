<script lang="ts">
	import GameBoard from "./GameBoard.svelte";
	import GameEndPopup from "./GameEndPopup.svelte";
	import GameHud from "./GameHud.svelte";
	import SpectatorBanner from "./SpectatorBanner.svelte";
	import SpectatorFade from "./SpectatorFade.svelte";
	import EliminationStandings from "./EliminationStandings.svelte";
	import EliminationOutcomeBanner from "./popup/EliminationOutcomeBanner.svelte";
	import CardDetailPopover from "./CardDetailPopover.svelte";
	import PromptRenderer from "./prompts/PromptRenderer.svelte";
	import FuseLine from "./FuseLine.svelte";
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

		<div class="top-overlay">
			<div class="game-controls">
				<GameHud />
				<PromptRenderer />
			</div>

			<SpectatorBanner
				eliminated={storeGame.state?.mode === "elimination" &&
					storeGame.eliminationOutcome !== null}
			/>
		</div>

		<EliminationOutcomeBanner />

		<EliminationStandings />

		<CardDetailPopover />

		<div class="game-board-container">
			<GameBoard />
		</div>
	</div>

	<SpectatorFade />
	<FuseLine />
</div>

<style>
	/* The playmat and the turn arrows are meshes inside the canvas now
	   (three/Playmat3D.svelte); the wood sprite that used to sit behind them is
	   gone, replaced by the flat table colour. Not var(--bg): the board's whole
	   job is to make four saturated card colours legible at a glance, and on a
	   light canvas they lose the contrast they're read by. */
	.game-screen {
		width: 100%;
		/* dvh, not vh: on mobile 100vh is the large viewport, which puts the
		   bottom HUD row below the visible fold until the URL bar retracts. */
		height: 100vh;
		height: 100dvh;
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

	/* Both the HUD row and the spectator banner float on top of the board
	   instead of stacking above it — the canvas must own the full screen or the
	   scene's center drifts below the true screen center. This wrapper owns the
	   top-left anchoring and stacking BOTH of them used to do independently,
	   which is what let them draw on the same pixels on a short mobile
	   viewport: now the banner simply flows below the HUD row's real height. */
	.top-overlay {
		position: absolute;
		top: 0;
		left: 0;
		right: 0;
		z-index: 2;
		display: flex;
		flex-direction: column;
		align-items: center;
		pointer-events: none;
	}

	.game-controls {
		width: 100%;
		display: flex;
		justify-content: flex-start;
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
