<script lang="ts">
	import { storeGame } from "../../stores/game.svelte";
	import { storeLobby } from "../../stores/lobby.svelte";

	function handleReturnToLobby() {
		storeLobby.leave();
	}
</script>

{#if storeGame.state && !storeGame.actionRequired}
	<div class="hud-container">
		<span>Current Turn: {storeGame.state.current_turn}</span>

		<span
			class="timer"
			style="background: {storeGame.turnTimeRemaining <= 5 ? '#dc251c' : '#333'};"
		>
			00:{storeGame.turnTimeRemaining.toString().padStart(2, "0")}
		</span>

		<button class="lobby-btn" onclick={handleReturnToLobby}> Esci </button>
	</div>
{/if}

<style>
	.hud-container {
		color: white;
		font-weight: bold;
		font-size: 1.2rem;
		display: flex;
		align-items: center;
		gap: 15px;
	}

	.timer {
		padding: 4px 10px;
		border-radius: 6px;
		font-family: monospace;
		transition: background-color 0.3s ease;
	}

	.lobby-btn {
		background-color: #333;
		color: #fff;
		border: none;
		padding: 6px 14px;
		border-radius: 6px;
		font-weight: bold;
		font-size: 0.9rem;
		cursor: pointer;
		transition:
			background-color 0.2s ease,
			transform 0.1s ease;
	}

	.lobby-btn:hover {
		background-color: #b51e16;
	}

	.lobby-btn:active {
		transform: scale(0.95);
	}
</style>

