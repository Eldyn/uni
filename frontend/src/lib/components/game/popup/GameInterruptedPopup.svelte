<script lang="ts">
	import { storeGame } from "../../../stores/game.svelte";
</script>

<div class="modal-overlay interrupted-overlay">
	<div class="cute-modal-content">
		<h1>Interrupted</h1>
		<h2>Something went wrong.</h2>
		<p>A player disconnected or the match was forcefully cancelled.</p>
		<button type="button" class="action-btn pixel-btn" onclick={() => storeGame.returnToLobby()}>
			Back to Lobby
		</button>
	</div>
</div>

<style>
	.modal-overlay {
		position: fixed;
		top: 0;
		left: 0;
		width: 100%;
		height: 100%;
		display: flex;
		justify-content: center;
		align-items: center;
		z-index: 300;
		background: rgba(0, 0, 0, 0.75);
		backdrop-filter: blur(4px);
	}

	.cute-modal-content {
		text-align: center;
		background: var(--bg);
		padding: 30px;
		border-radius: 16px;
		border: 4px solid var(--accent);
		box-shadow: 8px 8px 0px rgba(0, 0, 0, 0.6);
		color: var(--text-h);
		max-width: 380px;
		width: 90%;
		box-sizing: border-box; /* <-- ADD THIS */
		animation: shakeIn 0.4s cubic-bezier(0.36, 0.07, 0.19, 0.97) both;
		display: flex;
		flex-direction: column;
		align-items: center;
	}

	@keyframes shakeIn {
		0% {
			transform: scale(0.9);
			opacity: 0;
		}
		30% {
			transform: scale(1.02) rotate(-2deg);
			opacity: 1;
		}
		60% {
			transform: scale(1) rotate(2deg);
		}
		100% {
			transform: scale(1) rotate(0deg);
		}
	}

	.cute-modal-content h1 {
		font-family: "FatPixel", sans-serif;
		font-size: 2rem;
		margin: 0 0 10px 0;
		color: var(--accent);
		text-shadow: 2px 2px 0px #1a1a1a;
	}

	.cute-modal-content h2 {
		font-family: "Pixel", sans-serif;
		font-size: 1.1rem;
		margin-bottom: 10px;
	}

	.cute-modal-content p {
		font-family: var(--sans);
		color: var(--text);
		margin-bottom: 25px;
		font-size: 0.95rem;
	}

	.pixel-btn {
		padding: 12px 24px;
		background: var(--accent);
		color: #fff;
		border: 3px solid #1a1a1a;
		border-radius: 8px;
		font-family: "Pixel", sans-serif;
		font-weight: bold;
		font-size: 1rem;
		cursor: pointer;
		box-shadow: 4px 4px 0px #1a1a1a;
		transition:
			transform 0.1s,
			box-shadow 0.1s;
	}

	.pixel-btn:hover {
		transform: translate(-2px, -2px);
		box-shadow: 6px 6px 0px #1a1a1a;
		filter: brightness(1.1);
	}

	.pixel-btn:active {
		transform: translate(4px, 4px);
		box-shadow: 0px 0px 0px #1a1a1a;
	}
</style>
