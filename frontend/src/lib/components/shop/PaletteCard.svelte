<!-- One paid palette on the shop stage: its colors as a pixel grid, the
     name, and the lowest price still for sale or an OWNED ribbon. -->
<script lang="ts">
	import RarityFrame from "$components/shop/RarityFrame.svelte";
	import BlankIcon from "$components/common/BlankIcon.svelte";
	import PaletteSwatches from "$components/shop/PaletteSwatches.svelte";
	import { shopItemText } from "$components/shop/ItemCard.svelte";
	import { paletteRarity } from "$lib/shop/rarity";
	import type { Palette } from "$lib/avatar/types";
	import { storeI18n } from "$stores/i18n.svelte";

	let {
		palette,
		price,
		selected,
		onselect
	}: { palette: Palette; price: number | null; selected: boolean; onselect: () => void } = $props();

	const locale = $derived(storeI18n.locale);
	const currencyName = $derived(shopItemText("shop_currency_name", "Blank", locale));
	const paletteKey = $derived(`shop_palette_${palette.id.replaceAll(":", "_")}`);
	const name = $derived(shopItemText(paletteKey, palette.name, locale));
	const ownedLabel = $derived(shopItemText("shop_owned", "OWNED", locale));

	function activate(event: KeyboardEvent): void {
		if (event.key !== "Enter") return;
		event.preventDefault();
		onselect();
	}
</script>

<button
	type="button"
	class="palette-card"
	data-item-id={palette.id}
	data-selected={selected ? "true" : "false"}
	tabindex={selected ? 0 : -1}
	onclick={onselect}
	onkeydown={activate}
>
	<RarityFrame rarity={paletteRarity(palette)}>
		<span class="palette-card-art"><PaletteSwatches swatches={palette.swatches} size={14} /></span>
		<span class="palette-card-name font-tiny">{name}</span>
		{#if price === null}
			<span class="palette-card-owned font-tiny">{ownedLabel}</span>
		{:else}
			<span class="palette-card-price font-pixel">
				<BlankIcon label={currencyName} />{price}
			</span>
		{/if}
	</RarityFrame>
</button>

<style>
	.palette-card {
		display: block;
		width: 100%;
		padding: 0;
		border: none;
		background: none;
		color: var(--text);
		cursor: pointer;
		clip-path: none;
	}

	.palette-card :global(.rarity-frame) {
		background: repeating-linear-gradient(
			135deg,
			color-mix(in srgb, var(--rarity-colour) 22%, var(--surface-deep)) 0 9px,
			color-mix(in srgb, var(--rarity-colour) 8%, var(--surface-deep)) 9px 18px
		);
		transition:
			transform 0.12s ease,
			box-shadow 0.12s ease,
			filter 0.12s ease;
	}

	.palette-card:hover :global(.rarity-frame),
	.palette-card:focus-visible :global(.rarity-frame) {
		filter: brightness(1.12);
		transform: translateY(-2px);
		box-shadow: 0 0 14px color-mix(in srgb, var(--rarity-colour) 65%, transparent);
	}

	.palette-card-art {
		display: grid;
		place-items: center;
		width: 100%;
		aspect-ratio: 1;
	}

	.palette-card-name {
		padding: 0.35rem 0.4rem 0;
		overflow-wrap: anywhere;
		text-align: center;
		color: var(--text-h);
	}

	.palette-card-price,
	.palette-card-owned {
		display: flex;
		align-items: center;
		justify-content: center;
		gap: 0.3rem;
		padding: 0.35rem 0.4rem 0.5rem;
		font-size: 0.85rem;
	}

	.palette-card-price {
		color: var(--accent);
	}

	.palette-card-owned {
		color: var(--accent);
		letter-spacing: 0.06em;
	}
</style>
