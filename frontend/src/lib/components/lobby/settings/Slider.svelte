<script lang="ts">
	import Tooltip from "../../common/Tooltip.svelte";

	let {
		id,
		label,
		description,
		value,
		min,
		max,
		disabled = false,
		format = (v: number) => String(v),
		oncommit
	}: {
		id: string;
		label: string;
		description?: string;
		value: number;
		min: number;
		max: number;
		disabled?: boolean;
		/** Optional display formatter — e.g. (v) => `${v}s` */
		format?: (value: number) => string;
		oncommit: (value: number) => void;
	} = $props();

	// Tracks the in-progress drag without firing server updates on every tick
	let localValue = $state(value);

	// Keep in sync if the store pushes a new value (e.g. host changes it)
	$effect(() => {
		localValue = value;
	});
</script>

{#snippet slider()}
	<div class="slider-row">
		<div class="slider-header">
			<label for={id} class="slider-label">{label}</label>
			<span class="slider-value">{format(localValue)}</span>
		</div>
		<input
			{id}
			type="range"
			{min}
			{max}
			bind:value={localValue}
			{disabled}
			oninput={(e) => (localValue = parseInt((e.target as HTMLInputElement).value, 10))}
			onchange={() => oncommit(localValue)}
			class="custom-slider"
			class:disabled
		/>
	</div>
{/snippet}

{#if (description?.length ?? 0) > 0}
	<Tooltip>
		{#snippet tooltipContent()}
			<span>{description}</span>
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
		gap: 10px;
	}

	.slider-header {
		display: flex;
		justify-content: space-between;
		align-items: center;
	}

	.slider-label {
		font-size: 13px;
		color: var(--text-h);
		font-weight: 500;
	}

	.slider-value {
		font-size: 13px;
		color: var(--accent);
		font-weight: bold;
		font-family: var(--mono);
	}

	.custom-slider {
		appearance: none;
		-webkit-appearance: none;
		width: 100%;
		background: transparent;
		cursor: pointer;
	}

	.custom-slider.disabled {
		cursor: not-allowed;
		opacity: 0.6;
	}

	.custom-slider::-webkit-slider-runnable-track {
		width: 100%;
		height: 6px;
		background: var(--border);
		border-radius: 3px;
	}

	.custom-slider::-webkit-slider-thumb {
		-webkit-appearance: none;
		height: 16px;
		width: 16px;
		border-radius: 50%;
		background: var(--accent);
		margin-top: -5px;
		transition: transform 0.1s;
	}

	.custom-slider:not(.disabled)::-webkit-slider-thumb:hover {
		transform: scale(1.2);
	}

	.custom-slider::-moz-range-track {
		height: 6px;
		background: var(--border);
		border-radius: 3px;
	}

	.custom-slider::-moz-range-thumb {
		height: 16px;
		width: 16px;
		border: none;
		border-radius: 50%;
		background: var(--accent);
		transition: transform 0.1s;
	}

	.custom-slider:not(.disabled)::-moz-range-thumb:hover {
		transform: scale(1.2);
	}
</style>
