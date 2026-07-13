<script lang="ts">
	import { storeGame, Action } from "$stores/game.svelte";
	import { storeAudio } from "$stores/audio.svelte";

	const TYPE_BUTTONS = [
		{ name: "red", typeIndex: 0 },
		{ name: "yellow", typeIndex: 3 },
		{ name: "green", typeIndex: 2 },
		{ name: "blue", typeIndex: 1 }
	] as const;
</script>

{#if storeGame.actionRequired === Action.ChooseType}
	<div class="inline-action-container">
		<div class="cute-bubble pixel-corners">
			<h2 class="choose-color-text">Choose Color!</h2>
			<br />
			<div class="color-buttons">
				{#each TYPE_BUTTONS as { name, typeIndex }}
					<button
						type="button"
						class="btn pixel-corners color-button color-{name}"
						disabled={storeGame.isActionPending}
						onclick={() => {
							// PLACEHOLDER-SFX: sfx.color.confirm, confirmation blip when the
							// player picks a wild-card colour; a human may want per-colour pitch.
							storeAudio.playSfx("sfx.color.confirm");
							storeGame.submitInput(String(typeIndex));
						}}
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
		top: 60%;
		left: 50%;
		transform: translateX(-50%);
		z-index: 200;
		pointer-events: auto;
	}

	.cute-bubble {
		background: var(--bg);
		padding: 20px 25px;
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

	/* Square swatch sizing on top of the shared flat .btn */
	.color-button {
		width: 45px;
		height: 45px;
		border: 3px solid var(--pixel-shadow);
		font-weight: bold;
		font-size: 16px;
	}

	.color-red {
		background: var(--redCard);
		color: white;
	}
	.color-yellow {
		background: var(--yellowCard);
		color: black;
	}
	.color-green {
		background: var(--greenCard);
		color: white;
	}
	.color-blue {
		background: var(--blueCard);
		color: white;
	}
</style>
