<!-- The palette detail panel: the chosen color worn on the avatar, its swatch
     picks, the name, palette and rarity, the price and BUY. Escape closes it. -->
<script lang="ts">
	import { onMount } from "svelte";
	import Avatar from "$components/common/Avatar.svelte";
	import RarityFrame from "$components/shop/RarityFrame.svelte";
	import { shopItemText } from "$components/shop/ItemCard.svelte";
	import { BUY_FAILURE_TEXT } from "$lib/shop/buyFailure";
	import { isSwatchBuyable } from "$lib/shop/shopBackend";
	import { swatchName } from "$lib/shop/swatchText";
	import { RARITY_COLOURS, rarityLabelColour, rarityLabelKey } from "$lib/shop/rarity";
	import type { BuyFailure } from "$lib/shop/shopBackend";
	import type { Loadout, Palette, Swatch } from "$lib/avatar/types";
	import { storeI18n } from "$stores/i18n.svelte";

	let {
		palette,
		owned,
		balance,
		guest,
		loadout,
		onbuy,
		ontry,
		onclose,
		error = null
	}: {
		palette: Palette;
		owned: ReadonlySet<string>;
		balance: number;
		guest: boolean;
		/** The viewer's saved look: its slots dress the preview. */
		loadout: Loadout;
		onbuy: (swatch: Swatch) => void;
		ontry: (swatchId: string) => void;
		onclose: () => void;
		error?: BuyFailure | null;
	} = $props();

	const locale = $derived(storeI18n.locale);
	const paletteKey = $derived(`shop_palette_${palette.id.replaceAll(":", "_")}`);
	const paletteName = $derived(shopItemText(paletteKey, palette.name, locale));

	const shown = $derived(
		palette.swatches.filter((entry) => entry.unlisted !== true || owned.has(entry.id))
	);
	let chosenId = $state<string | null>(null);
	const swatch = $derived(
		shown.find((entry) => entry.id === chosenId) ??
			shown.find((entry) => isSwatchBuyable({ swatch: entry, palette }) && !owned.has(entry.id)) ??
			shown[0]
	);
	const isOwned = $derived(!!swatch && (owned.has(swatch.id) || owned.has(palette.id)));
	const forSale = $derived(!!swatch && isSwatchBuyable({ swatch, palette }));
	const price = $derived(swatch?.price ?? 0);
	const rarity = $derived(swatch?.rarity ?? "common");
	const rarityName = $derived(shopItemText(rarityLabelKey(rarity), rarity, locale));
	const name = $derived(swatch ? swatchName(swatch.id) : "");
	const preview = $derived<Loadout>({ ...loadout, skin: swatch?.id ?? loadout.skin });
	const tooPoor = $derived(!guest && !isOwned && forSale && balance < price);
	const buyDisabled = $derived(guest || isOwned || !forSale || tooPoor);
	const reason = $derived(
		isOwned
			? shopItemText("shop_error_already_owned", "You already own this.", locale)
			: !forSale
				? shopItemText("shop_swatch_not_sold", "This color is not sold alone.", locale)
				: tooPoor
					? shopItemText("shop_insufficient_cash", "Not enough cash", locale)
					: null
	);

	const buyLabel = $derived(
		guest
			? shopItemText("shop_register_to_buy", "Register to buy", locale)
			: shopItemText("shop_buy", "BUY", locale)
	);
	const closeLabel = $derived(shopItemText("shop_close", "Close", locale));
	const failure = $derived(
		error ? shopItemText(BUY_FAILURE_TEXT[error][0], BUY_FAILURE_TEXT[error][1], locale) : null
	);

	function pick(entry: Swatch): void {
		chosenId = entry.id;
		ontry(entry.id);
	}

	onMount(() => {
		function handleKeydown(event: KeyboardEvent): void {
			if (event.key === "Escape") onclose();
		}
		window.addEventListener("keydown", handleKeydown);
		return () => window.removeEventListener("keydown", handleKeydown);
	});
</script>

<div
	class="palette-detail"
	role="dialog"
	aria-label={paletteName}
	tabindex="-1"
	style="--rarity-colour: {RARITY_COLOURS[rarity]}"
>
	<button type="button" class="detail-close font-tiny" aria-label={closeLabel} onclick={onclose}>
		<span aria-hidden="true">×</span>
	</button>

	{#if swatch}
		<div class="detail-art" data-preview-skin={preview.skin}>
			<RarityFrame {rarity}>
				<span aria-hidden="true"><Avatar loadout={preview} size={96} /></span>
			</RarityFrame>
		</div>

		<div class="detail-picks" role="group" aria-label={paletteName}>
			{#each shown as entry (entry.id)}
				<button
					type="button"
					class="detail-pick"
					data-swatch-id={entry.id}
					aria-pressed={entry.id === swatch.id}
					aria-label={swatchName(entry.id)}
					style="--swatch: {entry.hex}"
					onclick={() => pick(entry)}
				></button>
			{/each}
		</div>

		<h2 class="detail-name font-heading">{name}</h2>
		<p class="detail-palette font-tiny">{paletteName}</p>
		<p class="detail-rarity font-tiny" style="color: {rarityLabelColour(rarity)}">{rarityName}</p>

		{#if forSale && !isOwned}
			<p class="detail-price font-pixel">
				<span class="coin" aria-hidden="true"></span>{price}
			</p>
		{/if}

		{#if failure}
			<p class="detail-error font-tiny" role="alert">{failure}</p>
		{/if}

		<div class="detail-actions">
			<button
				type="button"
				class="btn detail-buy"
				disabled={buyDisabled}
				onclick={() => {
					if (swatch && !buyDisabled) onbuy(swatch);
				}}
			>
				{buyLabel}
			</button>
		</div>

		{#if reason}
			<p class="detail-reason font-tiny">{reason}</p>
		{/if}
	{/if}
</div>

<style>
	.palette-detail {
		position: fixed;
		top: 0;
		right: 0;
		bottom: 0;
		z-index: 40;
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: 0.4rem;
		width: min(360px, 92vw);
		min-width: 0;
		padding: 1.5rem 1rem;
		overflow-y: auto;
		border-left: 2px solid var(--rarity-colour);
		background: var(--surface);
		box-shadow: -8px 0 24px color-mix(in srgb, var(--rarity-colour) 45%, transparent);
	}

	.detail-close {
		position: absolute;
		top: 0.25rem;
		right: 0.25rem;
		padding: 0.1rem 0.4rem;
		border: none;
		background: none;
		color: var(--text);
		cursor: pointer;
	}

	.detail-art {
		display: flex;
		width: 112px;
		height: 112px;
	}

	.detail-art :global(.rarity-frame) {
		flex: 1;
		align-items: center;
		justify-content: center;
	}

	.detail-picks {
		display: flex;
		flex-wrap: wrap;
		justify-content: center;
		gap: 0.35rem;
	}

	.detail-pick {
		width: 28px;
		height: 28px;
		padding: 0;
		border: 2px solid var(--border);
		background: var(--swatch);
		clip-path: none;
		cursor: pointer;
	}

	.detail-pick[aria-pressed="true"] {
		border-color: var(--accent);
		box-shadow: 0 0 0 2px var(--accent);
	}

	.detail-name {
		margin: 0;
		min-width: 0;
		overflow-wrap: anywhere;
		text-align: center;
		font-size: 1rem;
		color: var(--text-h);
	}

	.detail-palette,
	.detail-rarity {
		margin: 0;
		text-align: center;
	}

	.detail-rarity {
		text-transform: uppercase;
		letter-spacing: 0.08em;
	}

	.detail-price {
		display: flex;
		align-items: center;
		gap: 0.3rem;
		margin: 0;
		color: var(--gold);
	}

	.detail-error {
		margin: 0;
		color: var(--danger, #ff6b6b);
		text-align: center;
	}

	.detail-actions {
		display: flex;
		gap: 0.5rem;
		margin-top: 0.25rem;
	}

	.detail-actions button:disabled {
		opacity: 0.5;
		cursor: not-allowed;
	}

	.detail-reason {
		margin: 0;
		color: var(--text);
		text-align: center;
	}

	.coin {
		display: inline-block;
		width: 0.6em;
		height: 0.6em;
		border-radius: 50%;
		background: var(--gold);
	}
</style>
