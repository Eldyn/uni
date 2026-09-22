<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import type { PromptOpenPayload } from "$lib/generated/schemas";
	import { autofocus } from "./autofocus";

	let { prompt }: { prompt: PromptOpenPayload } = $props();
</script>

<div class="inline-action-container">
	<div class="cute-bubble pixel-corners">
		<div class="yesno-buttons">
			<button
				type="button"
				use:autofocus={{ key: prompt.prompt_id }}
				class="btn pixel-corners yesno-button yes-button"
				disabled={storeGame.isActionPending}
				onclick={() => storeGame.respondToPrompt(prompt.prompt_id, true)}
			>
				Yes
			</button>
			<button
				type="button"
				class="btn pixel-corners yesno-button no-button"
				disabled={storeGame.isActionPending}
				onclick={() => storeGame.respondToPrompt(prompt.prompt_id, false)}
			>
				No
			</button>
		</div>
	</div>
</div>

<style>
	.inline-action-container {
		position: fixed;
		top: 60%;
		left: 50%;
		transform: translateX(-50%);
		z-index: 200;
		pointer-events: auto;
	}

	.cute-bubble {
		background: var(--bg);
		padding: 15px 25px;
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

	.yesno-buttons {
		display: flex;
		gap: 15px;
		justify-content: center;
	}

	.yesno-button {
		padding: 8px 20px;
		font-size: 14px;
		font-weight: bold;
		border: 3px solid var(--pixel-shadow);
	}

	.yes-button {
		background-color: var(--success);
	}
	.no-button {
		background-color: var(--danger);
	}
</style>
