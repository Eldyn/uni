<script lang="ts">
	import { storeGame, Action, TYPE_MAP } from "../../../stores/game.svelte";

	const TYPE_BUTTONS = [
		{ name: "red",    typeIndex: 0 },
		{ name: "yellow", typeIndex: 3 },
		{ name: "green",  typeIndex: 2 },
		{ name: "blue",   typeIndex: 1 },
	] as const;
</script>

{#if storeGame.actionRequired === Action.ChooseType}
	<div class="inline-action-container">
		<div class="cute-bubble">
			<h2 class="choose-color-text">Choose Type</h2>
			<br />
			<div class="color-buttons">
				{#each TYPE_BUTTONS as { name, typeIndex }}
					<button
						type="button"
						class="color-button color-{name}"
						onclick={() => storeGame.submitInput(String(typeIndex))}
					>
						{name.charAt(0).toUpperCase()}
					</button>
				{/each}
			</div>
		</div>
	</div>
{/if}

<style>
	.inline-action-container {
		position: absolute;
		top: 60%; /* Anchored under the pile */
		left: 50%;
		transform: translateX(-50%);
		z-index: 200;
		pointer-events: auto;
	}

	.cute-bubble {
		background: var(--bg);
		padding: 20px 25px;
		border-radius: 16px;
		border: 4px solid var(--accent);
		text-align: center;
		box-shadow: 6px 6px 0px rgba(0, 0, 0, 0.4);
		animation: bounceIn 0.4s cubic-bezier(0.175, 0.885, 0.32, 1.275);
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: 15px;
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

	.choose-color-text {
		color: var(--text-h);
		font-family: "FatPixel", sans-serif;
		margin: 0;
		font-size: 1.2rem;
		text-transform: uppercase;
	}

	.color-buttons {
		display: flex;
		gap: 12px;
		align-items: center;
		justify-content: center;
	}

	.color-button {
		width: 45px;
		height: 45px;
		border: 3px solid #1a1a1a;
		border-radius: 10px;
		font-weight: bold;
		font-family: "Pixel", sans-serif;
		cursor: pointer;
		box-shadow: 3px 3px 0px #1a1a1a;
		transition:
			transform 0.1s ease,
			box-shadow 0.1s ease,
			filter 0.2s ease;
		font-size: 16px;
	}

	.color-button:hover {
		transform: translate(-2px, -2px) scale(1.1);
		box-shadow: 5px 5px 0px #1a1a1a;
		filter: brightness(1.2);
	}

	.color-button:active {
		transform: translate(3px, 3px) scale(1);
		box-shadow: 0px 0px 0px #1a1a1a;
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
</style>
