<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import type { PromptOpenPayload } from "$lib/generated/schemas";
	import { autofocus } from "./autofocus";

	let { prompt }: { prompt: PromptOpenPayload } = $props();

	let schema = $derived(prompt.response_schema as Record<string, unknown>);
	let enumValues = $derived(Array.isArray(schema.enum) ? schema.enum : null);
	let isBoolean = $derived(schema.type === "boolean");
	let isNumeric = $derived(schema.type === "integer" || schema.type === "number");

	let textValue = $state("");
	let numberValue = $state(0);

	$effect(() => {
		numberValue = typeof schema.minimum === "number" ? schema.minimum : 0;
	});

	function submit(value: unknown) {
		storeGame.respondToPrompt(prompt.prompt_id, value);
	}

	function submitText() {
		const trimmed = textValue.trim();
		if (trimmed === "") return;
		if (trimmed === "true") return submit(true);
		if (trimmed === "false") return submit(false);
		const asNumber = Number(trimmed);
		submit(Number.isFinite(asNumber) ? asNumber : trimmed);
	}

	function step(delta: number) {
		const minimum = typeof schema.minimum === "number" ? schema.minimum : null;
		const maximum = typeof schema.maximum === "number" ? schema.maximum : null;
		let next = numberValue + delta;
		if (minimum !== null) next = Math.max(minimum, next);
		if (maximum !== null) next = Math.min(maximum, next);
		numberValue = next;
	}
</script>

<div class="inline-action-container">
	<div class="cute-bubble pixel-corners">
		{#if enumValues}
			<div class="value-buttons">
				{#each enumValues as value, i}
					<button
						type="button"
						use:autofocus={i === 0}
						class="btn pixel-corners enum-button"
						disabled={storeGame.isActionPending}
						onclick={() => submit(value)}
					>
						{String(value)}
					</button>
				{/each}
			</div>
		{:else if isBoolean}
			<label class="toggle">
				<input
					type="checkbox"
					role="switch"
					class="toggle-input"
					disabled={storeGame.isActionPending}
					onchange={(e) => submit((e.currentTarget as HTMLInputElement).checked)}
				/>
			</label>
		{:else if isNumeric}
			<div class="stepper">
				<button
					type="button"
					class="btn pixel-corners step-button"
					disabled={storeGame.isActionPending}
					onclick={() => step(-1)}
				>
					−
				</button>
				<span class="step-value">{numberValue}</span>
				<button
					type="button"
					class="btn pixel-corners step-button"
					disabled={storeGame.isActionPending}
					onclick={() => step(1)}
				>
					+
				</button>
				<button
					type="button"
					class="btn pixel-corners submit-button"
					disabled={storeGame.isActionPending}
					onclick={() => submit(numberValue)}
				>
					OK
				</button>
			</div>
		{:else}
			<div class="text-input">
				<input
					type="text"
					class="prompt-text-input"
					use:autofocus
					bind:value={textValue}
					onkeydown={(e) => {
						if (e.key === "Enter") submitText();
					}}
				/>
				<button
					type="button"
					class="btn pixel-corners submit-button"
					disabled={storeGame.isActionPending}
					onclick={submitText}
				>
					OK
				</button>
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
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: 10px;
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

	.value-buttons,
	.stepper,
	.text-input {
		display: flex;
		align-items: center;
		gap: 10px;
		justify-content: center;
		flex-wrap: wrap;
	}

	.enum-button,
	.step-button,
	.submit-button {
		padding: 8px 16px;
		font-size: 14px;
		font-weight: bold;
		border: 3px solid var(--pixel-shadow);
	}

	.step-value {
		color: var(--text-h);
		font-weight: bold;
		min-width: 2ch;
		text-align: center;
	}

	.prompt-text-input {
		padding: 8px 10px;
		font-size: 14px;
		border: 3px solid var(--pixel-shadow);
		background: var(--bg);
		color: var(--text-h);
	}

	.toggle-input {
		width: 24px;
		height: 24px;
	}
</style>
