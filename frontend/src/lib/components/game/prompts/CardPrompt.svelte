<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import type { PromptOpenPayload } from "$lib/generated/schemas";
	import { autofocus } from "./autofocus";

	let { prompt }: { prompt: PromptOpenPayload } = $props();

	let options = $derived.by(() => {
		const raw = (prompt.payload as Record<string, unknown>).options;
		if (Array.isArray(raw) && raw.every((option) => typeof option === "number")) {
			return raw as number[];
		}
		return (storeGame.localPlayer?.hand ?? []).map((card) => card.id);
	});

	function label(cardId: number): string {
		const card = storeGame.localPlayer?.hand?.find((c) => c.id === cardId);
		return card ? `${card.type} ${card.value}` : `#${cardId}`;
	}
</script>

<div class="inline-action-container">
	<div class="cute-bubble pixel-corners">
		{#if options.length > 0}
			<div class="card-buttons">
				{#each options as cardId, i (cardId)}
					<button
						type="button"
						use:autofocus={{ enabled: i === 0, key: prompt.prompt_id }}
						class="btn pixel-corners card-button"
						disabled={storeGame.isActionPending}
						onclick={() => storeGame.respondToPrompt(prompt.prompt_id, cardId)}
					>
						{label(cardId)}
					</button>
				{/each}
			</div>
		{/if}
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

	.card-buttons {
		display: flex;
		flex-wrap: wrap;
		gap: 10px;
		justify-content: center;
	}

	.card-button {
		padding: 8px 16px;
		font-size: 14px;
		font-weight: bold;
		border: 3px solid var(--pixel-shadow);
	}
</style>
