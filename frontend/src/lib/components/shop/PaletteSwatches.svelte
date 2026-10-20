<!-- A palette's colors as flat square cells, six per row, no blur. -->
<script lang="ts">
	import type { Swatch } from "$lib/avatar/types";

	let { swatches, size = 8 }: { swatches: Swatch[]; size?: number } = $props();

	// INFO: the reset sentinel is a wardrobe control, never a palette colour.
	const shown = $derived(swatches.filter((swatch) => swatch.pattern !== "none"));
	// INFO: six cells plus the five 1px gaps; computed here because jsdom cannot
	// resolve var() inside calc() and the wardrobe tests read computed styles.
	const cap = $derived(`${size * 6 + 5}px`);
</script>

<span
	class="palette-swatches"
	style="--cell-size: {size}px; --swatch-cap: {cap}"
	aria-hidden="true"
>
	{#each shown as swatch (swatch.id)}
		<span class="palette-cell" data-hex={swatch.hex} style="--cell: {swatch.hex}"></span>
	{/each}
</span>

<style>
	.palette-swatches {
		display: grid;
		grid-template-columns: repeat(6, minmax(0, 1fr));
		grid-auto-rows: 1fr;
		gap: 1px;
		width: 100%;
		/* INFO: fill a narrow card, but never grow past the requested cell size. */
		max-width: var(--swatch-cap);
	}

	.palette-cell {
		display: block;
		aspect-ratio: 1;
		background: var(--cell);
	}
</style>
