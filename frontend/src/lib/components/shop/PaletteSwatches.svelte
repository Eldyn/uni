<!-- A palette's colors as flat square cells, six per row, no blur. -->
<script lang="ts">
	import type { Swatch } from "$lib/avatar/types";

	let { swatches, size = 8 }: { swatches: Swatch[]; size?: number } = $props();
</script>

<span class="palette-swatches" style="--cell-size: {size}px" aria-hidden="true">
	{#each swatches as swatch (swatch.id)}
		<span class="palette-cell" data-hex={swatch.hex} style="--cell: {swatch.hex}"></span>
	{/each}
</span>

<style>
	.palette-swatches {
		display: grid;
		grid-template-columns: repeat(6, var(--cell-size));
		grid-auto-rows: var(--cell-size);
		gap: 1px;
	}

	.palette-cell {
		display: block;
		background: var(--cell);
	}
</style>
