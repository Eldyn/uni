<!-- One palette swatch on the shop stage: a color tile, the swatch name, the
     palette name, and either the price or an OWNED ribbon. -->
<script lang="ts">
	import RarityFrame from "$components/shop/RarityFrame.svelte";
	import { shopItemText } from "$components/shop/ItemCard.svelte";
	import { swatchName } from "$lib/shop/swatchText";
	import type { Palette, Swatch } from "$lib/avatar/types";
	import { storeI18n } from "$stores/i18n.svelte";

	let {
		swatch,
		palette,
		owned,
		selected,
		onselect
	}: {
		swatch: Swatch;
		palette: Palette;
		owned: boolean;
		selected: boolean;
		onselect: () => void;
	} = $props();

	const locale = $derived(storeI18n.locale);
	const name = $derived(swatchName(swatch.id));
	const paletteKey = $derived(`shop_palette_${palette.id.replaceAll(":", "_")}`);
	const paletteName = $derived(shopItemText(paletteKey, palette.name, locale));
	const rarity = $derived(swatch.rarity ?? "common");
	const ownedLabel = $derived(shopItemText("shop_owned", "OWNED", locale));

	function activate(event: KeyboardEvent): void {
		if (event.key !== "Enter") return;
		event.preventDefault();
		onselect();
	}
</script>

<button
	type="button"
	class="swatch-card"
	data-item-id={swatch.id}
	data-selected={selected ? "true" : "false"}
	tabindex={selected ? 0 : -1}
	onclick={onselect}
	onkeydown={activate}
>
	<RarityFrame {rarity}>
		<span class="swatch-tile pixel-bordered" style="--swatch: {swatch.hex}" aria-hidden="true"
		></span>
		<span class="swatch-name font-tiny">{name}</span>
		<span class="swatch-palette font-tiny">{paletteName}</span>
		{#if owned}
			<span class="swatch-owned font-tiny">{ownedLabel}</span>
		{:else}
			<span class="swatch-price font-pixel">
				<span class="coin" aria-hidden="true"></span>{swatch.price}
			</span>
		{/if}
	</RarityFrame>
</button>

<style>
	.swatch-card {
		display: block;
		width: 100%;
		padding: 0;
		border: none;
		background: none;
		color: var(--text);
		cursor: pointer;
		clip-path: none;
	}

	.swatch-card :global(.rarity-frame) {
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

	.swatch-card:hover :global(.rarity-frame),
	.swatch-card:focus-visible :global(.rarity-frame) {
		filter: brightness(1.12);
		transform: translateY(-2px);
		box-shadow: 0 0 14px color-mix(in srgb, var(--rarity-colour) 65%, transparent);
	}

	.swatch-tile {
		display: block;
		width: 100%;
		aspect-ratio: 1;
		background: var(--swatch);
		--pc-fill: var(--surface);
	}

	.swatch-name {
		padding: 0.35rem 0.4rem 0;
		overflow-wrap: anywhere;
		text-align: center;
		color: var(--text-h);
	}

	.swatch-palette {
		padding: 0.1rem 0.4rem 0;
		overflow-wrap: anywhere;
		text-align: center;
		color: var(--text);
	}

	.swatch-price,
	.swatch-owned {
		display: flex;
		align-items: center;
		justify-content: center;
		gap: 0.3rem;
		padding: 0.35rem 0.4rem 0.5rem;
		font-size: 0.85rem;
	}

	.swatch-price {
		color: var(--gold);
	}

	.swatch-owned {
		color: var(--accent);
		letter-spacing: 0.06em;
	}

	.coin {
		display: inline-block;
		width: 0.6em;
		height: 0.6em;
		border-radius: 50%;
		background: var(--gold);
	}
</style>
