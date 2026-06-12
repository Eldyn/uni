<script lang="ts">
	import { storeGame } from "../../../stores/game.svelte";
</script>

{#if storeGame.actionRequired === "choose_target"}
	<div class="inline-action-container">
		<div class="cute-bubble">
			<h2 class="choose-target-text">Select a Target!</h2>
		</div>
	</div>
{/if}

<style>
	.inline-action-container {
		position: absolute;
		top: 60%;
		left: 50%;
		transform: translateX(-50%);
		z-index: 200;
		pointer-events: none; /* Let clicks pass through if needed, though it's positioned away from players */
	}

	.cute-bubble {
		background: var(--bg);
		padding: 15px 25px;
		border-radius: 16px;
		border: 4px solid var(--accent);
		text-align: center;
		box-shadow: 6px 6px 0px rgba(0, 0, 0, 0.4);
		animation: bounceIn 0.4s cubic-bezier(0.175, 0.885, 0.32, 1.275);
	}

	@keyframes bounceIn {
		0% {
			transform: scale(0.8) translateY(20px);
			opacity: 0;
		}
		100% {
			transform: scale(1) translateY(0);
			opacity: 1;
		}
	}

	.choose-target-text {
		color: var(--accent);
		font-family: "FatPixel", sans-serif;
		margin: 0 0 5px 0;
		font-size: 1.2rem;
		text-transform: uppercase;
		text-shadow: 2px 2px 0px #1a1a1a;
	}

	.instruction-text {
		font-family: "Pixel", sans-serif;
		color: var(--text-h);
		font-size: 0.9rem;
		margin: 0;
	}
</style>
