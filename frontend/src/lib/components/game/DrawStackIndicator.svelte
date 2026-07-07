<script lang="ts">
	import { storeGame } from "$stores/game.svelte";

	// Number of cards currently stacked on the draw chain (+2 / +4 ruleset).
	let pending = $derived(storeGame.state?.pending_draws ?? 0);
</script>

{#if pending > 0}
	<div class="draw-stack-overlay">
		{#key pending}
			<div class="draw-stack-badge">+{pending}</div>
		{/key}
	</div>
{/if}

<style>
	.draw-stack-overlay {
		position: fixed;
		top: 50%;
		left: 50%;
		transform: translate(-50%, -50%);
		z-index: 9000;
		pointer-events: none;
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: 0.2em;
	}

	.draw-stack-badge {
		font-family: "FatPixel", sans-serif;
		font-size: 10rem;
		line-height: 1;
		color: #ffd23f;
		text-shadow:
			0 0 10px rgba(255, 100, 0, 0.9),
			4px 4px 0 #b02a00,
			-2px -2px 0 #b02a00;

		animation: badge-pop 0.45s cubic-bezier(0.18, 1.4, 0.4, 1) both;
	}

	@keyframes badge-pop {
		0% {
			transform: scale(0.2) rotate(-12deg);
			opacity: 0;
		}
		55% {
			transform: scale(1.35) rotate(5deg);
			opacity: 1;
		}
		100% {
			transform: scale(1) rotate(0deg);
			opacity: 1;
		}
	}
</style>
