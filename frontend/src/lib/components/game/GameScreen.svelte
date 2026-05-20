<script lang="ts">
	import GameBoard from './GameBoard.svelte';

	let gameColor = $state('green');

	let { onBack }: { onBack?: () => void } = $props();

	function changeColor(color: string) {
		gameColor = color;
	}
</script>

<div class="game-screen">
	<div class="game-controls">
		<button type="button" class="back-button" onclick={onBack}>← Back to Lobbies</button>
		<div class="color-buttons">
			{#each ['red', 'yellow', 'green', 'blue'] as color}
				<button
					type="button"
					class="color-button color-{color}"
					class:active={gameColor === color}
					onclick={() => changeColor(color)}
					aria-label="Change color to {color}"
				>
					{color.charAt(0).toUpperCase()}
				</button>
			{/each}
		</div>
	</div>

	<GameBoard {gameColor} />
</div>

<style>
	.game-screen {
		display: flex;
		flex-direction: column;
		width: 100%;
		height: 100vh;
		background: var(--bg);
	}

	.game-controls {
		display: flex;
		justify-content: space-between;
		align-items: center;
		padding: 16px;
		border-bottom: 1px solid var(--border);
		background: var(--bg);
		position: relative;
		z-index: 100;
	}

	.back-button {
		padding: 8px 16px;
		background: var(--accent);
		color: white;
		border: none;
		border-radius: 6px;
		font-size: 14px;
		font-weight: 500;
		cursor: pointer;
		transition: opacity 0.2s;
	}

	.back-button:hover {
		opacity: 0.9;
	}

	.color-buttons {
		display: flex;
		gap: 8px;
	}

	.color-button {
		width: 36px;
		height: 36px;
		border: 2px solid transparent;
		border-radius: 6px;
		font-weight: 600;
		cursor: pointer;
		transition: all 0.2s;
		font-size: 12px;
	}

	.color-red {
		background: #dc251c;
		color: white;
	}

	.color-yellow {
		background: #fcf604;
		color: black;
	}

	.color-green {
		background: #018d41;
		color: white;
	}

	.color-blue {
		background: #0493de;
		color: white;
	}

	.color-button.active {
		border-color: var(--text-h);
		box-shadow: 0 0 0 2px var(--bg), 0 0 0 4px var(--text-h);
	}

	.color-button:hover {
		transform: scale(1.1);
	}
</style>
