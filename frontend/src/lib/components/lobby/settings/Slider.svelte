<script lang="ts">
	import { storeAudio } from "$stores/audio.svelte";
	import Tooltip from "$components/common/Tooltip.svelte";
	import RichText from "$components/common/RichText.svelte";

	let {
		id,
		label,
		description,
		value,
		min,
		max,
		step = 1,
		disabled = false,
		live = false,
		format = (v: number) => String(v),
		oncommit
	}: {
		id: string;
		label: string;
		description?: string;
		value: number;
		min: number;
		max: number;
		/** Granularity of the range input and of the +/- nudge buttons. */
		step?: number;
		disabled?: boolean;
		/** When true, oncommit fires on every drag tick instead of only on release. Client-side-only controls (e.g. audio volume) want this; anything that sends a server update on commit must leave this false to avoid flooding the server mid-drag. */
		live?: boolean;
		format?: (value: number) => string;
		oncommit: (value: number) => void;
	} = $props();

	// Tracks the in-progress drag without firing server updates on every tick
	let localValue = $state(0);

	// Keep in sync if the store pushes a new value (e.g. host changes it)
	$effect(() => {
		localValue = value;
	});

	let pct = $derived(max > min ? ((localValue - min) / (max - min)) * 100 : 0);

	function nudge(delta: number) {
		if (disabled) return;
		localValue = Math.min(max, Math.max(min, localValue + delta * step));
		storeAudio.playSfx(delta < 0 ? "sfx.ui.untick" : "sfx.ui.tick");
		oncommit(localValue);
	}
</script>

{#snippet slider()}
	<div class="slider-row">
		<div class="slider-header">
			<label for={id} class="slider-label">{label}</label>
			<span class="slider-value">{format(localValue)}</span>
		</div>
		<div class="slider-control">
			<button
				type="button"
				class="slider-nudge"
				disabled={disabled || localValue <= min}
				aria-label="{label} −"
				onclick={() => nudge(-1)}>−</button
			>
			<input
				{id}
				type="range"
				{min}
				{max}
				{step}
				bind:value={localValue}
				{disabled}
				style="--slider-pct: {pct}%"
				oninput={(e) => {
					localValue = parseInt((e.target as HTMLInputElement).value, 10);
					if (live) oncommit(localValue);
				}}
				onchange={() => oncommit(localValue)}
				class="custom-slider"
				class:disabled
			/>
			<button
				type="button"
				class="slider-nudge"
				disabled={disabled || localValue >= max}
				aria-label="{label} +"
				onclick={() => nudge(1)}>+</button
			>
		</div>
	</div>
{/snippet}

{#if (description?.length ?? 0) > 0}
	<Tooltip interactive={true} title={label}>
		{#snippet tooltipContent()}
			<RichText text={description ?? ""} allowKeywords={true} />
		{/snippet}
		{@render slider()}
	</Tooltip>
{:else}
	{@render slider()}
{/if}

<style>
	.slider-row {
		display: flex;
		flex-direction: column;
		gap: var(--space-2);
		min-width: 0;
	}

	.slider-header {
		display: flex;
		align-items: baseline;
		justify-content: space-between;
		gap: var(--space-3);
	}

	.slider-label {
		font-family: var(--tiny);
		font-size: 15px;
		color: var(--text-h);
		font-weight: 500;
		min-width: 0;
	}

	.slider-control {
		display: flex;
		align-items: center;
		gap: var(--space-3);
		min-width: 0;
	}

	.slider-nudge {
		display: grid;
		place-items: center;
		flex: 0 0 auto;
		width: 28px;
		height: 28px;
		padding: 0;
		border: 2px solid var(--border);
		background: var(--code-bg);
		color: var(--text-h);
		font-family: var(--pixel);
		font-size: 15px;
		line-height: 1;
		cursor: pointer;
		/* Global `button:not(.pixel-bordered)` notches buttons; these chips are
		   plain squares. */
		clip-path: none !important;
		transition:
			border-color var(--duration-fast) var(--ease-standard),
			color var(--duration-fast) var(--ease-standard);
	}
	.slider-nudge:hover:not(:disabled) {
		border-color: var(--accent);
		color: var(--accent);
	}
	.slider-nudge:disabled {
		opacity: 0.4;
		cursor: not-allowed;
	}

	.custom-slider {
		appearance: none;
		-webkit-appearance: none;
		flex: 1 1 auto;
		min-width: 0;
		height: 6px;
		border-radius: 0;
		background: transparent;
		cursor: pointer;
	}

	.custom-slider.disabled {
		cursor: not-allowed;
		opacity: 0.6;
	}

	.custom-slider::-webkit-slider-runnable-track {
		height: 6px;
		border-radius: 0;
		background: linear-gradient(
			to right,
			var(--accent) var(--slider-pct),
			var(--border) var(--slider-pct)
		);
	}

	.custom-slider::-webkit-slider-thumb {
		-webkit-appearance: none;
		appearance: none;
		width: 12px;
		height: 12px;
		margin-top: -3px;
		border: 2px solid var(--accent-h);
		border-radius: 0;
		background: var(--accent);
		transition: transform var(--duration-fast) var(--ease-standard);
	}

	.custom-slider:not(.disabled)::-webkit-slider-thumb:hover {
		transform: scale(1.15);
	}

	.custom-slider::-moz-range-track {
		height: 6px;
		border-radius: 0;
		background: linear-gradient(
			to right,
			var(--accent) var(--slider-pct),
			var(--border) var(--slider-pct)
		);
	}

	.custom-slider::-moz-range-thumb {
		width: 12px;
		height: 12px;
		border: 2px solid var(--accent-h);
		border-radius: 0;
		background: var(--accent);
		transition: transform var(--duration-fast) var(--ease-standard);
	}

	.custom-slider:not(.disabled)::-moz-range-thumb:hover {
		transform: scale(1.15);
	}

	.slider-value {
		flex: 0 0 auto;
		font-family: var(--tiny);
		font-size: 15px;
		line-height: 1;
		color: var(--accent);
	}
</style>
