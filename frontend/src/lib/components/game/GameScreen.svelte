<script lang="ts">
	import GameBoard from "./GameBoard.svelte";
	import GameUnoButton from "./GameUnoButton.svelte";
	import GameOverPopup from "./GameOverPopup.svelte";
	import GameHud from "./GameHud.svelte";
	import GameActions from "./GameActions.svelte";
	import TintedSprite from "../common/TintedSprite.svelte";
	import { storeGame } from "../../stores/game.svelte";
</script>

<div class="game-screen">
	<div class="bg-layer playmat-layout">
		<TintedSprite
			src="/assets/playmat.png"
			color="var(--{storeGame.state?.active_type ?? 'white'})"
		/>
	</div>

	<div class="bg-layer arrows-layout">
		<TintedSprite
			src="/assets/{storeGame.state?.play_direction > 0 ? 'cw.png' : 'ccw.png'}"
			color="var(--{storeGame.state?.active_type ?? 'white'})"
		/>
	</div>

	<div class="ui-layer">
		<GameOverPopup />

		<div class="game-controls">
			<GameHud />
			<GameActions />
		</div>

		<div class="game-board-container">
			<GameBoard />
		</div>

		<GameUnoButton />
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

	/* --- OVERLAY LOGIC --- */
	/* Both wrappers perfectly mirror the game-screen's dimensions */
	.bg-layer {
		position: absolute;
		inset: 0;
		width: 100%;
		height: 100%;
		pointer-events: none; /* Let clicks pass straight through to the board */
		z-index: 1;
	}

	/* Empty placeholder classes just in case you want to change layer order later */
	.playmat-layout {
		z-index: 1;
	}
	.arrows-layout {
		z-index: 2; /* Keeps arrows visually stacked on top of the playmat */
	}

	/* --- UI LAYOUT --- */
	.ui-layer {
		position: relative;
		z-index: 10; /* Keeps interactive UI safely above all backgrounds */
		display: flex;
		flex-direction: column;
		height: 100%;
	}

	.game-controls {
		display: flex;
		justify-content: space-between;
		align-items: center;
		padding: 16px;
	}

	.game-board-container {
		flex-grow: 1;
		display: flex;
		align-items: center;
		justify-content: center;
		position: relative;
	}
</style>
