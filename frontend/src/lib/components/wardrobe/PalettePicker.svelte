<!-- The palette tiles above the swatch grid: each palette's colors and name
     on its rarity border. A palette the account cannot use yet is struck but
     stays selectable, so its swatches can prompt a buy. -->
<script lang="ts">
	import PaletteSwatches from "$components/shop/PaletteSwatches.svelte";
	import { shopItemText } from "$components/shop/ItemCard.svelte";
	import { isPaletteLocked } from "$lib/avatar/palettes";
	import type { Palette } from "$lib/avatar/types";
	import { RARITY_COLOURS, paletteRarity } from "$lib/shop/rarity";
	import { storeI18n } from "$stores/i18n.svelte";

	let {
		palettes,
		selected,
		locked,
		label,
		onselect
	}: {
		palettes: Palette[];
		selected: string | undefined;
		locked: ReadonlySet<string>;
		label: string;
		onselect: (paletteId: string) => void;
	} = $props();

	const locale = $derived(storeI18n.locale);
</script>

<div class="palette-picker" role="group" aria-label={label}>
	{#each palettes as palette (palette.id)}
		<button
			type="button"
			class="palette-tile"
			data-palette-id={palette.id}
			data-locked={isPaletteLocked(palette, locked) ? "true" : "false"}
			aria-pressed={palette.id === selected}
			style="--rarity-colour: {RARITY_COLOURS[paletteRarity(palette)]}"
			onclick={() => onselect(palette.id)}
		>
			<PaletteSwatches swatches={palette.swatches} size={6} />
			<span class="palette-tile-name font-tiny">
				{shopItemText(`shop_palette_${palette.id.replaceAll(":", "_")}`, palette.name, locale)}
			</span>
		</button>
	{/each}
</div>

<style>
	.palette-picker {
		display: flex;
		flex-wrap: wrap;
		gap: 0.4rem;
	}

	.palette-tile {
		position: relative;
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: 0.25rem;
		padding: 0.3rem;
		border: 2px solid var(--rarity-colour);
		background: var(--surface-deep);
		color: var(--text);
		cursor: pointer;
		clip-path: none;
	}

	.palette-tile[aria-pressed="true"] {
		box-shadow: 0 0 0 2px var(--accent);
		color: var(--text-h);
	}

	/* The same diagonal strike as a locked swatch. */
	.palette-tile[data-locked="true"] {
		opacity: 0.4;
	}

	.palette-tile[data-locked="true"]::after {
		content: "";
		position: absolute;
		inset: 0;
		pointer-events: none;
		background: linear-gradient(
			135deg,
			transparent 45%,
			var(--border) 45%,
			var(--border) 55%,
			transparent 55%
		);
	}
</style>
