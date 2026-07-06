<script lang="ts">
	import Modal from "../../common/Modal.svelte";
	import { storeGame } from "../../../stores/game.svelte";
</script>

<Modal
	open={true}
	dismissible={false}
	titleId="interrupted-title"
	contentClass="interrupted-content"
>
	<h1 id="interrupted-title">Interrupted</h1>
	<h2>Something went wrong.</h2>
	<p>A player disconnected or the match was forcefully cancelled.</p>
	<button type="button" class="btn pixel-corners" onclick={() => storeGame.returnToLobby()}>
		Back to Lobby
	</button>
</Modal>

<style>
	:global(.interrupted-content) {
		text-align: center;
		color: var(--text-h);
		max-width: 380px;
		width: 90%;
		box-sizing: border-box;
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

	:global(.interrupted-content) h1 {
		font-family: "FatPixel", sans-serif;
		font-size: 1rem;
		margin: 0 0 10px 0;
		color: var(--accent);
		text-shadow: 2px 2px 0px var(--pixel-shadow);
	}

	:global(.interrupted-content) h2 {
		font-family: "Pixel", sans-serif;
		font-size: 1.1rem;
		margin-bottom: 10px;
	}

	:global(.interrupted-content) p {
		font-family: var(--sans);
		color: var(--text);
		margin-bottom: 25px;
		font-size: 0.95rem;
	}
</style>
