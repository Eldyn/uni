<script lang="ts">
	import GameBoard from "./GameBoard.svelte";
	import GameOverPopup from "./GameOverPopup.svelte";
	import GameHud from "./GameHud.svelte";
	import GameActions from "./GameActions.svelte";
	import { storeGame } from "$stores/game.svelte";

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
		<GameOverPopup />

		<div class="game-controls">
			<GameHud />
			<GameActions />
		</div>

		<div class="game-board-container">
			<GameBoard />
		</div>
	</div>
</div>

<style>
	:root {
		--cardSize: 5em;
		--white: rebeccapurple; /* Fallback color */
		--red: #dc251c;
		--yellow: #fcf604;
		--blue: #0493de;
		--green: #018d41;
		--black: #1f1b18;
	}

	.game-screen {
		width: 100%;
		height: 100vh;
		background: var(--bg);
		position: relative;
		background-image: url("/assets/background.png");
		background-size: cover;
		background-position: center;
		background-repeat: no-repeat;
		overflow: hidden;
	}

	/* The playmat and the turn arrows are meshes inside the canvas now
	   (three/Playmat3D.svelte) — only the wood backdrop is still CSS. */

	/* --- UI LAYOUT --- */
	.ui-layer {
		position: relative;
		z-index: 10; /* Keeps interactive UI safely above all backgrounds */
		height: 100%;
	}

	/* The HUD floats on top of the board instead of stacking above it — the
	   canvas must own the full screen or the scene's center drifts below the
	   true screen center by half the HUD row's height. */
	.game-controls {
		position: absolute;
		top: 0;
		left: 0;
		right: 0;
		z-index: 2;
		display: flex;
		justify-content: space-between;
		align-items: center;
		padding: 16px;
		pointer-events: none;
	}

	/* Re-enable clicks on the HUD's actual widgets; the row's empty middle
	   stays click-through so it never blocks the board underneath. */
	.game-controls > :global(*) {
		pointer-events: auto;
	}

	/* Narrow screens: the HUD wraps (see GameHud.svelte) — center the bar so
	   the wrapped rows don't hug the left edge. */
	@media (max-width: 700px) {
		.game-controls {
			justify-content: center;
		}
	}

	.game-board-container {
		position: absolute;
		inset: 0;
		z-index: 1;
	}
</style>
