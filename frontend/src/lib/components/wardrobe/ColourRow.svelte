<!-- One row of swatch pickers, one group per tint region. When `skinFirst` is
     set (a motif) region 1 is the skin colour and is shown locked, so only the
     later regions are pickable. -->
<script lang="ts">
	import type { Swatch } from "$lib/avatar/types";

	let {
		regions,
		colours,
		swatches,
		onpick,
		label,
		skinFirst = false,
		skinHex = "#000000"
	}: {
		regions: number;
		colours: string[];
		swatches: Swatch[];
		onpick: (region: number, swatchId: string) => void;
		label: string;
		skinFirst?: boolean;
		skinHex?: string;
	} = $props();

	const pickable = $derived(skinFirst ? Math.max(0, regions - 1) : regions);
	const firstLabel = $derived(skinFirst ? 2 : 1);
</script>

<div class="colour-row" role="group" aria-label={label}>
	{#if skinFirst}
		<div class="colour-region" data-region="skin">
			<span class="colour-region-label font-tiny">1</span>
			<span class="colour-swatch colour-swatch--skin" style="--swatch: {skinHex}" aria-hidden="true"
			></span>
		</div>
	{/if}

	{#each Array.from({ length: pickable }, (_, index) => index) as index (index)}
		<div class="colour-region" data-region={index}>
			<span class="colour-region-label font-tiny">{index + firstLabel}</span>
			{#each swatches as swatch (swatch.id)}
				<button
					type="button"
					class="colour-swatch"
					data-swatch-id={swatch.id}
					aria-pressed={colours[index] === swatch.id}
					aria-label={swatch.id}
					style="--swatch: {swatch.hex}"
					onclick={() => onpick(index, swatch.id)}
				></button>
			{/each}
		</div>
	{/each}
</div>

<style>
	.colour-row {
		display: flex;
		flex-wrap: wrap;
		gap: 0.75rem;
	}

	.colour-region {
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: 0.3rem;
	}

	.colour-region-label {
		color: var(--text);
	}

	.colour-swatch {
		width: 1.25rem;
		height: 1.25rem;
		padding: 0;
		border: 2px solid var(--border);
		background: var(--swatch);
		cursor: pointer;
		/* Swatches are square tiles, so they opt out of the global notch clip. */
		clip-path: none;
	}

	.colour-swatch[aria-pressed="true"] {
		border-color: var(--text-h);
	}

	.colour-swatch--skin {
		cursor: default;
		border-style: dashed;
	}
</style>
