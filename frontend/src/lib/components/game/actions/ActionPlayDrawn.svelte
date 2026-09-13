<script lang="ts">
	import { storeGame, Action } from "$stores/game.svelte";
	import { storeAudio } from "$stores/audio.svelte";
</script>

{#if storeGame.actionRequired === Action.PlayDrawn && storeGame.actionContext}
	<div class="inline-action-container">
		<div class="prompt-badge pixel-corners">You drew a playable card!</div>
		<div class="action-buttons-group">
			<button
				class="btn pixel-corners sized-btn play-btn"
				onclick={() => {
					// PLACEHOLDER-SFX: sfx.play-drawn.confirm, confirmation blip when
					// the player chooses to play the just-drawn card.
					storeAudio.playSfx("sfx.play-drawn.confirm");
					storeGame.submitInput("0");
				}}
			>
				Play It
			</button>
			<button
				class="btn pixel-corners sized-btn keep-btn"
				onclick={() => {
					// PLACEHOLDER-SFX: sfx.play-drawn.confirm, confirmation blip when
					// the player chooses to keep the just-drawn card instead.
					storeAudio.playSfx("sfx.play-drawn.confirm");
					storeGame.submitInput("1");
				}}
			>
				Keep It
			</button>
		</div>
	</div>
{/if}

<style>
	.inline-action-container {
		position: absolute;
		top: 58%;
		left: 50%;
		transform: translateX(-50%);
		display: flex;
		flex-direction: column;
		align-items: center;
		z-index: 200;
		pointer-events: auto;
	}

	.prompt-badge {
		background: var(--bg);
		color: var(--text-h);
		border: 3px solid var(--accent);
		padding: 6px 12px;
		font-family: "Pixel", sans-serif;
		font-weight: bold;
		font-size: 14px;
		margin-bottom: 15px;
		box-shadow: 4px 4px 0px rgba(0, 0, 0, 0.3);
		animation: popIn 0.3s cubic-bezier(0.175, 0.885, 0.32, 1.275);
	}


	@keyframes popIn {
		0% {
			transform: scale(0.8);
			opacity: 0;
		}
		100% {
			transform: scale(1);
			opacity: 1;
		}
	}

	.action-buttons-group {
		display: flex;
		gap: 15px;
		animation: popIn 0.4s cubic-bezier(0.175, 0.885, 0.32, 1.275) forwards;
	}

	/* Size override on top of the shared flat .btn */
	.sized-btn {
		padding: 10px 20px;
		font-size: 14px;
		font-weight: bold;
		border: 3px solid var(--pixel-shadow);
	}

	.play-btn {
		background-color: var(--success);
	}
	.keep-btn {
		background-color: var(--danger);
	}
</style>
